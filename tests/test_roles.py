# SPDX-License-Identifier: AGPL-3.0-or-later
"""Model roles (protocol 13): validation, per-provider defaults, fallbacks and wiring.

Every key lookup here is a dict in the test; nothing reaches the desktop keyring or the network.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import roles as model_roles
from relay_core import route_assist
from relay_core.agent import Agent
from relay_core import presets
from relay_core.presets import PRESETS, tier_default
from relay_core.provider import ProviderConfig
from relay_core.roles import RoleResolver, validate_roles
from relay_core.subagents import SubagentFactory
from relay_core.agents_defs import AgentDefinition

ROOT = Path(__file__).resolve().parents[1]


def setUpModule():
    # These tests describe a machine with no saved model server: the owner's own catalog
    # (~/.config/relay/local-models.json) would otherwise make the Local tier resolve to it, and
    # "every role stays on main" fail on the one machine the suite runs on most.
    global _no_local_catalog
    _no_local_catalog = mock.patch.dict(os.environ, {"RELAY_LOCAL_MODELS": os.devnull})
    _no_local_catalog.start()


def tearDownModule():
    _no_local_catalog.stop()

CONFIGS = {
    "relay-free": ("https://api.relay-terminal.ai/v1", "relay-main"),
    "kimi": ("https://api.moonshot.ai/v1", "kimi-k3"),
    "kimi-code": ("https://api.kimi.ai/coding/v1", "k3"),
    "glm": ("https://api.z.ai/api/paas/v4", "glm-5.3"),
    "glm-coding": ("https://api.z.ai/api/coding/paas/v4", "glm-5.3"),
    "openrouter": ("https://openrouter.ai/api/v1", "deepseek/deepseek-v4.1-flash"),
}


def main_config(preset="kimi", key="main-key"):
    base, model = CONFIGS[preset]
    if preset == "relay-free":
        # No key and no effort field: the gateway takes a token and owns reasoning per role.
        return ProviderConfig(base, model, "", {}, 8192, hosted=True)
    return ProviderConfig(base, model, key, {"reasoning_effort": "high"}, 8192)


def resolver(preset="kimi", roles=None, keys=("kimi",), effort="high"):
    store = {name: f"{name}-key" for name in keys}
    lookup = mock.Mock(side_effect=lambda pid: store.get(pid, ""))
    made = RoleResolver(main_config(preset), preset, validate_roles(roles), key_lookup=lookup, main_effort=effort)
    made.lookup_mock = lookup
    return made


# ----- validation ---------------------------------------------------------------------------
class ValidateTests(unittest.TestCase):
    def test_inherit_forms_are_dropped(self):
        self.assertEqual(validate_roles(None), {})
        self.assertEqual(validate_roles({}), {})
        self.assertEqual(validate_roles({"flash": None, "vision": {}, "subagent": {"inherit": True}}), {})

    def test_accepts_preset_custom_and_effort(self):
        table = validate_roles({"subagent": {"preset": "glm", "effort": "low"},
                                "flash": {"base_url": "https://example.invalid/v1", "model": "m", "extra": {"a": 1}},
                                "switchboard": {"preset": "openrouter", "model": "google/gemini-3.8-flash"}})
        self.assertEqual(table["subagent"], {"preset": "glm", "effort": "low"})
        self.assertEqual(table["flash"]["model"], "m")
        self.assertEqual(table["switchboard"]["model"], "google/gemini-3.8-flash")

    # The helper agent's model box lists a provider's models one by one since card #PK5Q, so a
    # role value routinely names a provider *and* a model *and* a level. It always could —
    # protocol 13.7 has carried all three since the roles modal — and this is the proof that it
    # still means what it meant when it named only a provider.
    def test_a_role_may_name_a_provider_a_model_and_a_level(self):
        table = validate_roles({"switchboard": {"preset": "glm-coding", "model": "glm-5.3-flash",
                                                "effort": "low"}})
        self.assertEqual(table["switchboard"],
                         {"preset": "glm-coding", "model": "glm-5.3-flash", "effort": "low"})
        made = resolver("kimi", {"switchboard": {"preset": "glm-coding", "model": "glm-5.3-flash",
                                                 "effort": "low"}}, keys=("kimi", "glm-coding"))
        helper = made.resolve("switchboard")
        self.assertEqual((helper.preset_id, helper.model, helper.source),
                         ("glm-coding", "glm-5.3-flash", "configured"))
        self.assertEqual(helper.config.extra.get("reasoning_effort"), "low")

    def test_a_pinned_model_runs_that_models_own_request(self):
        # presets.model_extra: the extras of the tier row that names the model, not the preset's.
        # Kimi's high-speed model carries no effort field at all, so a level asked of it is not
        # sent — the same rule a tier *list* entry has always followed.
        made = resolver("glm-coding", {"switchboard": {"preset": "kimi-code",
                                                       "model": "kimi-for-coding-highspeed",
                                                       "effort": "max"}},
                        keys=("glm-coding", "kimi-code"))
        helper = made.resolve("switchboard")
        self.assertEqual(helper.model, "kimi-for-coding-highspeed")
        self.assertNotIn("reasoning_effort", helper.config.extra)

    def test_a_role_naming_only_a_provider_still_means_its_own_model(self):
        # The value every helper box wrote before #PK5Q. Nothing about it may have changed.
        made = resolver("kimi", {"switchboard": {"preset": "glm-coding"}}, keys=("kimi", "glm-coding"))
        helper = made.resolve("switchboard")
        self.assertEqual((helper.preset_id, helper.model), ("glm-coding", PRESETS["glm-coding"].model))
        self.assertEqual(helper.config.extra, dict(PRESETS["glm-coding"].extra))

    def test_rejects_bad_tables(self):
        for bad in ({"nope": {"preset": "glm"}}, {"main": {"preset": "glm"}}, {"flash": {"preset": "nope"}},
                    {"flash": {"model": "m"}}, {"flash": {"effort": "turbo"}}, {"flash": {"extra": 3}},
                    {"flash": {"zzz": 1}}, [1, 2]):
            with self.assertRaises(ValueError):
                validate_roles(bad)


# ----- defaults -----------------------------------------------------------------------------
class BackgroundRoleTests(unittest.TestCase):
    """BACKGROUND_ROLES is derived twice — here from ROLE_TIERS, and in the GUI's jobs tab from the
    same rule over `relay::modelrows::roleTier` (`rolestore::background`, src/JobsTab.cpp, card
    #MDL1). The tab uses it to decide which jobs may not be pointed at a guest harness, so the two
    have to agree; writing the set out is what makes a role added to either tier fail loudly here
    rather than silently offer a model the worker would skip."""

    def test_the_set_the_jobs_tab_derives(self):
        self.assertEqual(set(model_roles.BACKGROUND_ROLES),
                         {"terminal_use", "summaries", "suggestions", "chores", "audit", "loop_check"})

    def test_it_is_the_flash_and_lite_tiers_less_the_pane_mode(self):
        derived = {role for role, tier in model_roles.ROLE_TIERS.items()
                   if tier in ("flash", "lite") and role != "flash"}
        self.assertEqual(set(model_roles.BACKGROUND_ROLES), derived)
        # `flash` is a pane's own mode, not a side call, so it may run on a harness.
        self.assertNotIn("flash", model_roles.BACKGROUND_ROLES)


class DefaultTests(unittest.TestCase):
    def test_fast_agent_default_per_main_provider(self):
        for preset, model in (("glm", "glm-5.3-flash"), ("glm-coding", "glm-5.3-flash"),
                              ("openrouter", "deepseek/deepseek-v4.1-flash"),
                              ("kimi", "kimi-k2.7-code-highspeed"), ("kimi-code", "kimi-for-coding-highspeed")):
            flash = resolver(preset, keys=(preset,)).resolve("flash")
            self.assertEqual(flash.model, model, preset)
            self.assertEqual(flash.source, "default")
            self.assertFalse(flash.is_main)

    def test_glm_flash_default_uses_the_lowest_legal_thinking(self):
        # Z.AI rejects thinking.type=disabled on GLM-5.3 and GLM-5.3-Flash
        # (https://docs.z.ai/guides/capabilities/thinking), so the Flash tier asks for the least
        # thinking it can instead of turning it off.
        flash = resolver("glm-coding", keys=("glm-coding",)).resolve("flash")
        self.assertEqual(flash.config.extra, {"thinking": {"type": "enabled"}, "reasoning_effort": "low"})

    def test_flash_reuses_the_main_key_without_a_keyring_lookup(self):
        made = resolver("kimi", keys=())        # nothing stored: only the in-memory main key exists
        self.assertEqual(made.resolve("flash").model, "kimi-k2.7-code-highspeed")
        made.lookup_mock.assert_not_called()

    def test_unknown_main_provider_keeps_every_role_on_main(self):
        config = ProviderConfig("https://example.invalid/v1", "house-model", "k", {}, 8192)
        made = RoleResolver(config, None, {}, key_lookup=lambda pid: "")
        for role in model_roles.ROLES:
            if role == "high":
                continue   # the High tier: the main model pushed to max, asserted below
            self.assertTrue(made.resolve(role).is_main, role)
            self.assertEqual(made.resolve(role).model, "house-model")
        # Plan mode adds nothing of its own (owner, 2026-09-22): entering it puts the pane on
        # /high (#PH9G), so the unpinned planning role is the pane as it is.
        self.assertIsNone(made.planning_target())
        # /high puts the pane itself on the High tier (card #MDL1), which with no list is the
        # main model pushed to max.
        high = made.resolve("high")
        self.assertEqual((high.model, high.tier, high.effort, high.source),
                         ("house-model", "high", "max", "default"))
        self.assertEqual(high.config.extra, {"reasoning_effort": "max"})

    # ----- /high as a pane mode (card #MDL1, the model box's modes) -------------------------
    def test_high_is_a_settable_pane_role_on_the_high_tier(self):
        self.assertIn("high", model_roles.ROLES)
        self.assertIn("high", model_roles.SETTABLE)
        self.assertEqual(model_roles.ROLE_TIERS["high"], "high")
        self.assertIn("high", dict((row["role"], row) for row in model_roles.action_catalog()))

    def test_high_resolves_off_the_high_list_exactly_as_flash_does_off_flash(self):
        """The one rule for both: the first usable entry of the tier's list, at that entry's level."""
        made = resolver("kimi", keys=("kimi", "glm-coding"))
        made.set_tiers(model_roles.validate_tiers(
            {"high": [{"preset": "openai", "model": "gpt-6"},          # no key: stepped over
                      {"preset": "glm-coding", "model": "glm-5.3", "effort": "max"}],
             "flash": [{"preset": "glm-coding", "model": "glm-5.3-flash", "effort": "low"}]}))
        high = made.resolve("high")
        self.assertEqual((high.preset_id, high.model, high.effort, high.tier),
                         ("glm-coding", "glm-5.3", "max", "high"))
        self.assertFalse(high.is_main)
        flash = made.resolve("flash")
        self.assertEqual((flash.preset_id, flash.model, flash.tier), ("glm-coding", "glm-5.3-flash", "flash"))
        # The step-down note names the entries that could not be used, as it does for every tier.
        self.assertIn("high", high.note or "")   # lower-case, card #MDL1 rule 1

    # ----- a pane's own pick for a role (protocol 13.5, set_agent_role {preset, model, effort}) ---
    def test_resolve_entry_pins_one_pane_without_touching_the_list(self):
        made = resolver("kimi", keys=("kimi", "glm-coding"))
        made.set_tiers(model_roles.validate_tiers(
            {"flash": [{"preset": "glm-coding", "model": "glm-5.3-flash", "effort": "low"}]}))
        pinned = made.resolve_entry("flash", {"preset": "kimi", "model": "kimi-k2.7-code-highspeed"})
        self.assertEqual((pinned.preset_id, pinned.model, pinned.tier, pinned.source),
                         ("kimi", "kimi-k2.7-code-highspeed", "flash", "configured"))
        self.assertFalse(pinned.is_main)
        # The list is untouched: it belongs to every other pane and to every side call.
        self.assertEqual(made.tiers["flash"], [{"preset": "glm-coding", "model": "glm-5.3-flash",
                                                "effort": "low"}])
        self.assertEqual(made.resolve("flash").model, "glm-5.3-flash")

    def test_resolve_entry_takes_the_level_and_the_models_own_extras(self):
        made = resolver("kimi", keys=("kimi", "glm-coding"))
        pinned = made.resolve_entry("high", {"preset": "glm-coding", "model": "glm-5.3", "effort": "max"})
        self.assertEqual((pinned.preset_id, pinned.model, pinned.effort, pinned.tier),
                         ("glm-coding", "glm-5.3", "max", "high"))
        # The model's own request extras, exactly as the same entry in a list would run
        self.assertEqual(pinned.config.extra, presets.model_extra("glm-coding", "glm-5.3")
                         | {"reasoning_effort": "max"})

    def test_resolve_entry_falls_back_rather_than_failing(self):
        made = resolver("kimi", keys=("kimi",))
        for entry in ({"preset": "openai", "model": "gpt-6"},          # no stored key
                      {"preset": "nope-there-is-no-such-provider"}):   # not a provider at all
            resolved = made.resolve_entry("flash", entry)
            self.assertTrue(resolved.is_main, entry)
            self.assertTrue(resolved.warning)
        # A guest: the /flash *pane* may run on one since 2026-09-21 (GUEST_TIERS), so what makes
        # this fall back is the harness not running here, not the tier. A background job on the
        # same tier refuses it either way — it is a side call, and a harness cannot take one.
        made.guest_check = lambda guest_id: False
        self.assertTrue(made.resolve_entry("flash", {"preset": "guest:claude"}).is_main)
        made.guest_check = lambda guest_id: True
        self.assertEqual(made.resolve_entry("flash", {"preset": "guest:claude"}).preset_id,
                         "guest:claude")
        self.assertTrue(made.resolve_entry("terminal_use", {"preset": "guest:claude"}).is_main)
        self.assertTrue(made.resolve_entry("chores", {"preset": "guest:claude"}).warning)
        for bad in (None, {}, {"preset": ""}, {"preset": "kimi", "effort": "turbo"}):
            with self.assertRaises(ValueError):
                made.resolve_entry("flash", bad)
        with self.assertRaises(ValueError):
            made.resolve_entry("main", {"preset": "kimi"})

    def test_chores_prefers_openrouter_then_the_fast_agent(self):
        with_or = resolver("kimi", keys=("kimi", "openrouter")).resolve("chores")
        self.assertEqual((with_or.model, with_or.preset_id), ("google/gemini-3.8-flash", "openrouter"))
        without = resolver("kimi", keys=("kimi",)).resolve("chores")
        self.assertEqual(without.model, "kimi-k2.7-code-highspeed")
        alone = RoleResolver(ProviderConfig("https://example.invalid/v1", "house", "k", {}, 8192), None, {},
                             key_lookup=lambda pid: "")
        self.assertTrue(alone.resolve("chores").is_main)

    def test_vision_default_is_glm_flash_or_main(self):
        self.assertEqual(resolver("glm", keys=("glm",)).resolve("vision").model, "glm-5.3-flash")
        self.assertTrue(resolver("kimi").resolve("vision").is_main)

    def test_route_assist_keeps_its_own_fast_model(self):
        with_or = resolver("kimi", keys=("kimi", "openrouter")).resolve("route_assist")
        self.assertEqual(with_or.model, route_assist.ROUTER_MODEL)
        self.assertEqual(with_or.config.base_url, route_assist.ROUTER_BASE_URL)
        self.assertTrue(resolver("kimi").resolve("route_assist").is_main)

    # ----- Relay Free (protocol 13.9): every tier without a key stored ----------------------
    def test_relay_free_tiers_resolve_with_no_keys_stored(self):
        made = resolver("relay-free", keys=(), effort=None)
        for role, model, tier in (("flash", "relay-flash", "flash"), ("terminal_use", "relay-flash", "flash"),
                                  ("summaries", "relay-flash", "flash"), ("chores", "relay-lite", "lite"),
                                  ("audit", "relay-lite", "lite")):
            resolved = made.resolve(role)
            self.assertEqual((resolved.model, resolved.preset_id, resolved.tier), (model, "relay-free", tier), role)
            self.assertEqual(resolved.source, "default")
            self.assertFalse(resolved.is_main)
            self.assertIsNone(resolved.note)
            self.assertTrue(resolved.config.hosted)
            self.assertEqual(resolved.config.api_key, "")
        # Nothing was asked of the keyring for the hosted preset: it has no entry there.
        self.assertNotIn("relay-free", [c.args[0] for c in made.lookup_mock.call_args_list])
        self.assertEqual(made.warnings, [])
        self.assertTrue(made.has_key("relay-free"))

    def test_route_assist_on_relay_free_is_the_gateways_lite_role_unless_openrouter_is_stored(self):
        alone = resolver("relay-free", keys=(), effort=None).resolve("route_assist")
        self.assertEqual((alone.model, alone.preset_id), ("relay-lite", "relay-free"))
        self.assertFalse(alone.is_main)
        # The fixed fast router still wins where its key exists, as it does for every other preset.
        with_or = resolver("relay-free", keys=("openrouter",), effort=None).resolve("route_assist")
        self.assertEqual((with_or.model, with_or.preset_id), (route_assist.ROUTER_MODEL, "openrouter"))
        # Another preset without the OpenRouter key keeps routing on the main model: the table is
        # per preset, and only Relay Free has an entry.
        self.assertTrue(resolver("kimi").resolve("route_assist").is_main)

    def test_relay_free_falls_back_like_a_missing_key_where_cryptography_is_absent(self):
        with mock.patch.object(model_roles.hosted, "available", return_value=False):
            made = resolver("relay-free", keys=(), effort=None)
            self.assertFalse(made.has_key("relay-free"))
            self.assertTrue(made.resolve("chores").is_main)
            self.assertTrue(made.resolve("route_assist").is_main)

    def test_subagent_and_switchboard_follow_the_main_tier(self):
        made = resolver("kimi")
        for role in ("subagent", "switchboard"):
            self.assertTrue(made.resolve(role).is_main, role)
            self.assertEqual(made.resolve(role).source, "main")
            self.assertEqual(made.resolve(role).tier, "main")

    def test_terminal_use_takes_the_flash_tier(self):
        # Owner, 2026-09-17: driving programs is a Flash job, not a main-agent job.
        made = resolver("kimi", keys=("kimi",))
        terminal_use = made.resolve("terminal_use")
        self.assertEqual(terminal_use.model, "kimi-k2.7-code-highspeed")
        self.assertEqual(terminal_use.tier, "flash")


class TiedRankTests(unittest.TestCase):
    def test_effective_availability_uses_fractional_hours_and_tighter_window(self):
        now = 100000
        limits = {"updated_at": now, "windows": [
            {"kind": "five_hour", "used_percent": 70, "resets_at": now + 5400},
            {"kind": "weekly", "used_percent": 40, "resets_at": now + 10800}]}
        self.assertAlmostEqual(model_roles._usage_weight("kimi", now, limits), 20.0)
        limits["windows"][1]["used_percent"] = 70
        self.assertAlmostEqual(model_roles._usage_weight("kimi", now, limits), 10.0)
        limits["windows"][0]["used_percent"] = 100
        self.assertEqual(model_roles._usage_weight("kimi", now, limits), 0)
        limits["updated_at"] = now - 1801
        self.assertIsNone(model_roles._usage_weight("kimi", now, limits))

    def test_exhausted_account_is_absent_from_a_tied_draw(self):
        now = 100000
        rows = [{"preset": "kimi", "rank": 1}, {"preset": "glm", "rank": 1}]
        limits = {"kimi": {"updated_at": now, "windows": [
            {"kind": "five_hour", "used_percent": 100, "resets_at": now + 3600}]}}
        self.assertEqual([r["preset"] for r in model_roles.ordered_candidates(
            rows, choose=True, limits_lookup=limits.get, now=now)], ["glm"])

    def test_legacy_rows_keep_distinct_ranks_and_ties_can_draw_both(self):
        rows = [{"preset": "kimi", "model": "kimi-k3"},
                {"preset": "glm", "model": "glm-5.3"}]
        self.assertEqual(model_roles.ordered_candidates(rows, choose=True, draw=lambda: .99), rows)
        tied = [{**row, "rank": 1} for row in rows]
        self.assertEqual(model_roles.ordered_candidates(tied, choose=True, draw=lambda: 0)[0], tied[0])
        self.assertEqual(model_roles.ordered_candidates(tied, choose=True, draw=lambda: .99)[0], tied[1])

    def test_every_draw_is_recorded_with_its_probabilities(self):
        now = 100000
        rows = [{"preset": "kimi", "model": "k3", "rank": 1},
                {"preset": "glm", "model": "glm-5.3", "rank": 1},
                {"preset": "local", "model": "q", "rank": 2}]
        limits = {  # kimi: 80 % left over 2 h = 40 %/h; glm: 20 % left over 1 h = 20 %/h
            "kimi": {"updated_at": now, "windows": [
                {"kind": "five_hour", "used_percent": 20, "resets_at": now + 7200}]},
            "glm": {"updated_at": now, "windows": [
                {"kind": "five_hour", "used_percent": 80, "resets_at": now + 3600}]}}
        records = []
        order = model_roles.ordered_candidates(rows, choose=True, draw=lambda: .9,
                                               limits_lookup=limits.get, now=now,
                                               surface="role:subagent", record=records.append)
        self.assertEqual([r["preset"] for r in order], ["glm", "kimi", "local"])
        (record,) = records
        self.assertEqual(record["surface"], "role:subagent")
        self.assertEqual(record["order"], ["glm|glm-5.3", "kimi|k3", "local|q"])
        first = record["steps"][0]
        self.assertEqual((first["rank"], first["u"], first["chosen"]), (1, .9, "glm|glm-5.3"))
        self.assertEqual([(c["key"], round(c["p"], 4)) for c in first["candidates"]],
                         [("kimi|k3", .6667), ("glm|glm-5.3", .3333)])
        self.assertEqual(first["candidates"][0]["score"], 40.0)
        # What was left once glm was drawn, and the rank below: certain, and recorded as such.
        self.assertEqual([(s["rank"], s["candidates"][0]["p"]) for s in record["steps"][1:]],
                         [(1, 1.0), (2, 1.0)])
        # Ordering for display is not a choice and records nothing.
        model_roles.ordered_candidates(rows, record=records.append)
        self.assertEqual(len(records), 1)

    def test_draw_record_goes_to_the_routing_file_and_not_from_a_stray_unit_test(self):
        from relay_core import logs
        rows = [{"preset": "kimi", "model": "k3", "rank": 1},
                {"preset": "glm", "model": "glm-5.3", "rank": 1}]
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.dict(os.environ, {"XDG_DATA_HOME": tmp, "RELAY_LOG_ORIGIN": "interactive"}):
                model_roles.ordered_candidates(rows, choose=True, surface="role:subagent")
            self.assertFalse((Path(tmp) / "relay/logs" / logs.ROUTING_DRAWS).exists())
            with mock.patch.dict(os.environ, {"XDG_DATA_HOME": tmp, "RELAY_LOG_ORIGIN": "test",
                                              "RELAY_PANE_ID": "p7"}):
                model_roles.ordered_candidates(rows, choose=True, surface="quota_failover:main")
            path = Path(tmp) / "relay/logs" / logs.ROUTING_DRAWS
            (line,) = path.read_text().splitlines()
            record = json.loads(line)
            self.assertEqual((record["v"], record["component"], record["surface"], record["origin"]),
                             (1, "worker", "quota_failover:main", "test"))
            self.assertEqual(sum(c["p"] for c in record["steps"][0]["candidates"]), 1.0)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_recent_unused_allowance_near_reset_gets_more_weight(self):
        rows = [{"preset": "kimi", "rank": 1}, {"preset": "glm", "rank": 1}]
        now = 100000
        limits = {
            "kimi": {"updated_at": now - 60, "windows": [
                {"kind": "weekly", "used_percent": 20, "resets_at": now + 3600}]},
            "glm": {"updated_at": now - 60, "windows": [
                {"kind": "weekly", "used_percent": 20, "resets_at": now + 10 * 86400}]},
        }
        order = model_roles.ordered_candidates(rows, choose=True, draw=lambda: .7,
                                               limits_lookup=limits.get, now=now)
        self.assertEqual(order[0]["preset"], "kimi")
        limits["kimi"]["updated_at"] = now - 3600
        order = model_roles.ordered_candidates(rows, choose=True, draw=lambda: .7,
                                               limits_lookup=limits.get, now=now)
        self.assertEqual(order[0]["preset"], "glm")

    def test_subagent_draws_fresh_without_changing_display_resolution(self):
        configured = validate_roles({"subagent": {"candidates": [
            {"preset": "kimi", "model": "kimi-k3", "rank": 1},
            {"preset": "glm", "model": "glm-5.3", "rank": 1}]}})
        store = {"kimi": "kimi-key", "glm": "glm-key"}
        made = RoleResolver(main_config("kimi"), "kimi", configured,
                            key_lookup=store.get)
        self.assertEqual(made.resolve("subagent").preset_id, "kimi")
        with mock.patch.object(model_roles.random, "random", side_effect=[.99, 0]):
            self.assertEqual(made.choose_role("subagent").preset_id, "glm")
            self.assertEqual(made.choose_role("subagent").preset_id, "kimi")
        self.assertEqual(made.choose_role("subagent", skip_presets={"kimi"}).preset_id, "glm")
        self.assertEqual(made.resolve("subagent").preset_id, "kimi")

    def test_selected_tie_fails_over_to_earlier_peer_before_lower_rank(self):
        rows = [{"preset": "kimi", "model": "kimi-k3", "rank": 1},
                {"preset": "glm", "model": "glm-5.3", "rank": 1},
                {"preset": "openrouter", "model": "deepseek/deepseek-v4.1-flash", "rank": 2}]
        made = RoleResolver(main_config("glm"), "glm", tiers={"main": rows},
                            key_lookup=lambda preset: "key")
        self.assertEqual([row["preset"] for row in made.failover_chain("main", "glm", "glm-5.3")],
                         ["kimi", "openrouter"])
        self.assertEqual([row["preset"] for row in made.failover_chain(
            "main", "glm", "glm-5.3", entries=rows)], ["kimi", "openrouter"])


# ----- plan mode (owner, 2026-09-19) --------------------------------------------------------------
class PlanRoleTests(unittest.TestCase):
    """The planning role (owner, 2026-09-22: plan mode "is supposed to go into /high"; "it doesnt
    need to be the same model"). Entering plan mode puts the pane on /high (#PH9G), so the unpinned
    role adds no swap and no effort boost; only a hand-pinned `roles.planning` entry routes a plan
    turn. Before this it was the pane's own model at max (2026-09-19, again after #HR5E)."""

    def plan_resolver(self, preset="kimi", extra=None, roles=None, keys=("kimi",), tiers=None):
        endpoint = PRESETS[preset]
        store = {name: f"{name}-key" for name in keys}
        config = ProviderConfig(endpoint.base_url, endpoint.model, f"{preset}-key", dict(extra or {}), 8192)
        return RoleResolver(config, preset, validate_roles(roles),
                            key_lookup=lambda pid: store.get(pid, ""), main_effort="high",
                            tiers=model_roles.validate_tiers(tiers))

    def test_the_unpinned_default_is_the_pane_as_it_is(self):
        made = self.plan_resolver("kimi", extra={})
        planning = made.resolve("planning")
        self.assertTrue(planning.is_main)
        self.assertEqual((planning.model, planning.config.extra, planning.effort), ("kimi-k3", {}, "high"))
        self.assertIsNone(made.planning_target())

    def test_the_high_tier_is_the_main_model_at_max_by_default(self):
        made = self.plan_resolver("kimi", extra={})
        high = made.resolve("high")
        summary = made.tier_summary()["high"]
        self.assertEqual((summary["model"], summary["preset"], summary["effort"], summary["source"],
                          summary["using"]), ("kimi-k3", "kimi", "max", "default", "high"))
        self.assertNotIn("note", summary)
        self.assertEqual((high.model, high.preset_id, high.effort, high.tier), ("kimi-k3", "kimi", "max", "high"))
        # And it needs no per-provider row: nothing in the tier table names a "high" model.
        for provider in PRESETS:
            if provider == "relay-pro":
                self.assertEqual(tier_default(provider, "high")[1], "relay-pro-high")
            else:
                self.assertIsNone(tier_default(provider, "high"), provider)

    def test_planning_is_not_tiered(self):
        self.assertIsNone(model_roles.ROLE_TIERS["planning"])
        self.assertTrue(self.plan_resolver("kimi", extra={}).resolve("planning").is_main)
        self.assertEqual(model_roles.action_catalog()[1]["role"], "planning")
        self.assertIsNone(model_roles.action_catalog()[1]["tier"])

    def test_a_high_override_serves_high_and_plan_adds_nothing(self):
        # The High list decides what a plan turn runs on, through /high: the planning role itself
        # stays on the pane, whatever the list names and whether or not its key is stored.
        for keys, tiers in ((("kimi", "glm"), {"high": {"preset": "glm", "model": "glm-5.3", "effort": "high"}}),
                            (("kimi", "glm"), {"high": {"preset": "glm"}}),
                            (("kimi",), {"high": {"preset": "glm"}})):
            with self.subTest(keys=keys, tiers=tiers):
                made = self.plan_resolver("kimi", extra={}, keys=keys, tiers=tiers)
                self.assertTrue(made.resolve("planning").is_main)
                self.assertIsNone(made.planning_target())
        made = self.plan_resolver("kimi", extra={}, keys=("kimi", "glm"),
                                  tiers={"high": {"preset": "glm", "model": "glm-5.3", "effort": "high"}})
        high = made.resolve("high")
        self.assertEqual((high.preset_id, high.model, high.tier, high.effort),
                         ("glm", "glm-5.3", "high", "high"))

    def test_a_planning_role_pinned_to_another_tier_still_wins(self):
        made = self.plan_resolver("kimi", extra={}, roles={"planning": {"tier": "flash"}})
        planning = made.resolve("planning")
        self.assertEqual((planning.model, planning.tier, planning.source),
                         ("kimi-k2.7-code-highspeed", "flash", "configured"))
        self.assertIs(made.planning_target(), planning)
        # And a role may follow High explicitly, with its own effort in place of max.
        made = self.plan_resolver("kimi", extra={}, roles={"subagent": {"tier": "high", "effort": "high"}})
        subagent = made.resolve("subagent")
        self.assertEqual((subagent.model, subagent.tier, subagent.effort, subagent.config.extra),
                         ("kimi-k3", "high", "high", {"reasoning_effort": "high"}))

    def test_plan_mode_adds_no_effort_of_its_own(self):
        made = self.plan_resolver("kimi", extra={"reasoning_effort": "high"})
        self.assertEqual(made.resolve("planning").config.extra, {"reasoning_effort": "high"})

    def test_a_provider_with_no_effort_knob_stays_on_the_main_agent(self):
        # Anthropic's compat layer ignores reasoning_effort (presets.EFFORT_MAP["none"]), so
        # "the main model at max reasoning" is not a request this endpoint can make.
        made = self.plan_resolver("anthropic", extra={}, keys=())
        planning = made.resolve("planning")
        self.assertTrue(planning.is_main)
        self.assertEqual(planning.config.extra, {})
        self.assertIsNone(made.planning_target())

    def test_a_configured_planning_role_wins_over_the_default(self):
        made = self.plan_resolver("kimi", roles={"planning": {"preset": "glm", "model": "glm-5.3"}},
                                  keys=("kimi", "glm"))
        planning = made.resolve("planning")
        self.assertEqual((planning.preset_id, planning.model, planning.source),
                         ("glm", "glm-5.3", "configured"))
        self.assertEqual(planning.config.api_key, "glm-key")
        self.assertIs(made.planning_target(), planning)

    def test_a_configured_planning_role_without_a_key_falls_back_to_main(self):
        made = self.plan_resolver("kimi", roles={"planning": {"preset": "glm"}}, keys=("kimi",))
        self.assertTrue(made.resolve("planning").is_main)
        self.assertIsNone(made.planning_target())
        self.assertTrue(made.warnings)          # a misconfiguration, unlike the max-effort no-op


# ----- configured roles and fallbacks ----------------------------------------------------------
class ConfiguredTests(unittest.TestCase):
    def test_preset_role_uses_its_stored_key_and_effort(self):
        made = resolver("kimi", {"subagent": {"preset": "glm", "effort": "low"}}, keys=("kimi", "glm"))
        sub = made.resolve("subagent")
        self.assertEqual((sub.preset_id, sub.model, sub.effort), ("glm", "glm-5.3", "low"))
        self.assertEqual(sub.config.api_key, "glm-key")
        self.assertEqual(sub.config.extra["reasoning_effort"], "low")
        self.assertEqual(sub.config.extra["thinking"], {"type": "enabled"})
        self.assertEqual(sub.source, "configured")

    def test_custom_endpoint_role_finds_the_key_by_url(self):
        made = resolver("kimi", {"flash": {"base_url": "https://openrouter.ai/api/v1/",
                                           "model": "google/gemini-3.8-flash"}}, keys=("kimi", "openrouter"))
        flash = made.resolve("flash")
        self.assertEqual((flash.preset_id, flash.model, flash.config.api_key),
                         ("openrouter", "google/gemini-3.8-flash", "openrouter-key"))

    def test_missing_key_falls_back_to_main_with_one_warning(self):
        made = resolver("kimi", {"subagent": {"preset": "glm"}}, keys=("kimi",))
        sub = made.resolve("subagent")
        self.assertTrue(sub.is_main)
        self.assertEqual(sub.source, "fallback")
        self.assertEqual(sub.model, "kimi-k3")
        self.assertIn("no stored key", sub.warning)
        event = made.event("main")
        self.assertEqual(event["event"], "model_roles")
        self.assertEqual(len(event["warnings"]), 1)
        self.assertNotIn("kimi-key", json.dumps(event))

    def test_summary_covers_every_role_and_never_carries_keys(self):
        made = resolver("glm-coding", {"chores": {"preset": "glm-coding", "model": "glm-5-turbo"}},
                        keys=("glm-coding",))
        summary = made.summary()
        self.assertEqual(set(summary), set(model_roles.ROLES))
        self.assertEqual(summary["chores"]["model"], "glm-5-turbo")
        self.assertEqual(summary["main"]["model"], "glm-5.3")
        self.assertEqual(summary["flash"]["source"], "default")
        self.assertNotIn("glm-coding-key", json.dumps(summary))

    def test_the_old_fast_name_still_resolves_to_flash(self):
        # Settings, saved layouts and subagent definitions written before 2026-09-18 say "fast".
        # They are read as "flash" (roles.DEPRECATED_ROLES); nothing writes the old name any more.
        self.assertEqual(model_roles.canonical_role("fast"), "flash")
        self.assertEqual(model_roles.validate_role("fast"), "flash")
        self.assertEqual(validate_roles({"fast": {"preset": "glm"}}), {"flash": {"preset": "glm"}})
        made = resolver("kimi", keys=("kimi",))
        self.assertEqual(made.resolve("fast").model, made.resolve("flash").model)
        self.assertNotIn("fast", model_roles.ROLES)

    def test_rebase_and_set_roles_recompute(self):
        made = resolver("kimi", keys=("kimi", "glm"))
        self.assertEqual(made.resolve("flash").model, "kimi-k2.7-code-highspeed")
        made.rebase(main_config("glm", "glm-key"), "glm", "high")
        self.assertEqual(made.resolve("flash").model, "glm-5.3-flash")
        made.set_roles(validate_roles({"flash": {"preset": "glm", "model": "glm-5-turbo"}}))
        self.assertEqual(made.resolve("flash").model, "glm-5-turbo")


# ----- wiring: subagents and side calls ---------------------------------------------------------
DEFINITION = AgentDefinition(name="probe", description="d", prompt="p", tools=("read_file",),
                             source="test", model=None, max_steps=4)


class WiringTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        patcher = mock.patch('relay_core.keystore.lookup', side_effect=AssertionError('keyring used'))
        patcher.start(); self.addCleanup(patcher.stop)
        router = mock.patch.object(route_assist, 'router_provider', side_effect=AssertionError('router used'))
        router.start(); self.addCleanup(router.stop)

    def factory(self, roles_table=None, keys=("kimi", "glm")):
        made = resolver("kimi", roles_table, keys=keys)
        store = {name: f"{name}-key" for name in keys}
        return SubagentFactory(made.main_config, self.temp.name, preset_id="kimi",
                               key_lookup=lambda pid: store.get(pid, ""), roles=made), made

    def test_inheriting_subagent_uses_the_subagent_role(self):
        factory, _ = self.factory({"subagent": {"preset": "glm"}})
        config, preset = factory.resolve(None, [])
        self.assertEqual((preset, config.model), ("glm", "glm-5.3"))

    def test_subagent_role_falls_back_to_main_without_a_key(self):
        factory, made = self.factory({"subagent": {"preset": "openrouter"}})
        config, preset = factory.resolve(None, [])
        self.assertEqual((preset, config.model), ("kimi", "kimi-k3"))
        self.assertTrue(made.warnings)

    def test_role_name_as_a_subagent_model(self):
        factory, _ = self.factory()
        config, preset = factory.resolve("flash", [])
        self.assertEqual((preset, config.model), ("kimi", "kimi-k2.7-code-highspeed"))

    def test_definition_model_still_wins_over_the_role(self):
        factory, _ = self.factory({"subagent": {"preset": "glm"}})
        warnings = []
        config, preset = factory.resolve("openrouter", warnings)   # no OpenRouter key stored
        self.assertEqual(preset, "glm")                       # falls back to the subagent role
        self.assertTrue(warnings)

    def agent(self, made):
        return Agent(made.main_config, self.temp.name, lambda e: None, roles=made,
                     session_dir=str(Path(self.temp.name) / "s"))

    def test_side_provider_uses_the_named_role(self):
        made = resolver("kimi", {"chores": {"preset": "glm", "model": "glm-5.3-flash"}}, keys=("kimi", "glm"))
        agent = self.agent(made)
        self.assertEqual(agent.side_provider(cheap=True, role="flash").config.model, "kimi-k2.7-code-highspeed")
        chores = agent.side_provider(cheap=True, role="chores", max_tokens=64)
        self.assertEqual((chores.config.model, chores.config.max_tokens), ("glm-5.3-flash", 64))
        self.assertEqual(agent.role_model("chores"), "glm-5.3-flash")

    def test_side_provider_without_roles_keeps_the_main_model(self):
        config = ProviderConfig("https://example.invalid/v1", "house", "k", {}, 8192)
        made = RoleResolver(config, None, {}, key_lookup=lambda pid: "")
        agent = self.agent(made)
        self.assertEqual(agent.side_provider(cheap=True, role="flash").config.model, "house")
        self.assertEqual(agent.role_model("flash"), "house")


# ----- worker protocol -------------------------------------------------------------------------
def run_worker(messages, env_extra):
    from relay_core.test_logging import environment
    with environment() as env:
        env["RELAY_KEYRING"] = "off"          # never the real keyring
        env.update(env_extra)
        proc = subprocess.run([sys.executable, "-S", str(ROOT / "backend/worker.py")],
                              input="".join(json.dumps(m) + "\n" for m in messages),
                              text=True, capture_output=True, timeout=20, cwd=ROOT, env=env)
    return [json.loads(line) for line in proc.stdout.splitlines()], proc


class WorkerTests(unittest.TestCase):
    def messages(self, extra):
        return [{"type": "configure", "preset": "kimi", "use_stored_key": True,
                 "base_url": CONFIGS["kimi"][0], "model": CONFIGS["kimi"][1],
                 "workspace": str(ROOT), **extra},
                {"type": "set_agent_role", "id": "r1", "role": "main"},
                {"type": "set_agent_options", "id": "o1", "roles": {"subagent": {"preset": "glm"}}},
                {"type": "shutdown"}]

    def test_configure_reports_roles_and_a_flash_pane(self):
        events, proc = run_worker(self.messages({"agent_role": "flash"}),
                                  {"RELAY_KIMI_API_KEY": "KIMI_SECRET_NEVER_ECHO"})
        configured = next(e for e in events if e["event"] == "configured")
        self.assertEqual(configured["agent_role"], "flash")
        self.assertEqual(configured["model"], "kimi-k2.7-code-highspeed")
        self.assertEqual(configured["roles"]["subagent"]["source"], "main")
        self.assertEqual(configured["roles"]["flash"]["model"], "kimi-k2.7-code-highspeed")
        # Switching the pane back to the main agent keeps the conversation.
        changed = next(e for e in events if e["event"] == "model_changed")
        self.assertEqual((changed["model"], changed["agent_role"]), ("kimi-k3", "main"))
        # set_agent_options carries a new role table and answers with the effective models.
        options = next(e for e in events if e["event"] == "agent_options")
        self.assertEqual(options["roles"]["subagent"]["source"], "fallback")   # no GLM key here
        roles_event = next(e for e in events if e["event"] == "model_roles")
        self.assertTrue(roles_event["warnings"])
        self.assertNotIn("KIMI_SECRET_NEVER_ECHO", proc.stdout + proc.stderr)

    def test_configure_with_an_unusable_role_never_fails(self):
        events, _ = run_worker([{"type": "configure", "preset": "kimi", "use_stored_key": True,
                                 "base_url": CONFIGS["kimi"][0], "model": CONFIGS["kimi"][1],
                                 "workspace": str(ROOT), "agent_role": "flash",
                                 "roles": {"flash": {"preset": "openrouter"}}},
                                {"type": "shutdown"}], {"RELAY_KIMI_API_KEY": "k"})
        configured = next(e for e in events if e["event"] == "configured")
        self.assertEqual(configured["agent_role"], "main")
        self.assertEqual(configured["model"], "kimi-k3")
        warning = next(e for e in events if e["event"] == "model_roles")["warnings"]
        self.assertEqual(len(warning), 1)
        self.assertNotIn("error", [e["event"] for e in events])

    # set_agent_role {role, preset, model, effort}: this pane's own pick for a mode (13.5, #MDL1).
    # The box's Flash page lists the Flash models; Enter on one means "this pane, flash, that
    # model", which the tier list alone cannot say.
    def test_set_agent_role_takes_a_pane_pick_of_preset_and_model(self):
        events, _ = run_worker([{"type": "configure", "preset": "kimi", "use_stored_key": True,
                                 "base_url": CONFIGS["kimi"][0], "model": CONFIGS["kimi"][1],
                                 "workspace": str(ROOT),
                                 "tiers": {"flash": [{"preset": "kimi", "model": "kimi-k2.7-code-highspeed"}]}},
                                {"type": "set_agent_role", "id": "r1", "role": "flash",
                                 "preset": "kimi", "model": "kimi-k3", "effort": "high"},
                                {"type": "shutdown"}], {"RELAY_KIMI_API_KEY": "k"})
        changed = next(e for e in events if e["event"] == "model_changed")
        self.assertEqual((changed["model"], changed["agent_role"]), ("kimi-k3", "flash"))
        self.assertNotIn("error", [e["event"] for e in events])

    def test_set_agent_role_without_a_pick_still_reads_the_list(self):
        events, _ = run_worker([{"type": "configure", "preset": "kimi", "use_stored_key": True,
                                 "base_url": CONFIGS["kimi"][0], "model": CONFIGS["kimi"][1],
                                 "workspace": str(ROOT)},
                                {"type": "set_agent_role", "id": "r1", "role": "flash"},
                                {"type": "shutdown"}], {"RELAY_KIMI_API_KEY": "k"})
        changed = next(e for e in events if e["event"] == "model_changed")
        self.assertEqual((changed["model"], changed["agent_role"]),
                         ("kimi-k2.7-code-highspeed", "flash"))

    def test_bad_role_table_is_an_error_and_changes_nothing(self):
        events, _ = run_worker([{"type": "configure", "preset": "kimi", "use_stored_key": True,
                                 "base_url": CONFIGS["kimi"][0], "model": CONFIGS["kimi"][1],
                                 "workspace": str(ROOT), "roles": {"flash": {"preset": "nope"}}},
                                {"type": "shutdown"}], {"RELAY_KIMI_API_KEY": "k"})
        self.assertEqual(events[1]["event"], "error")
        self.assertIn("preset", events[1]["text"])


# ----- Main / Flash / Lite tiers (protocol 13.7) ----------------------------------------------
class TierTests(unittest.TestCase):
    def tiered(self, preset, keys, roles=None, tiers=None):
        store = {name: f"{name}-key" for name in keys}
        return RoleResolver(main_config(preset), preset, validate_roles(roles),
                            key_lookup=lambda pid: store.get(pid, ""), main_effort="high",
                            tiers=model_roles.validate_tiers(tiers))

    def test_tier_defaults_per_provider(self):
        expected = {
            "glm": ("glm-5.3-flash", "google/gemini-3.8-flash"),
            "glm-coding": ("glm-5.3-flash", "google/gemini-3.8-flash"),
            "kimi": ("kimi-k2.7-code-highspeed", "google/gemini-3.8-flash"),
            "kimi-code": ("kimi-for-coding-highspeed", "google/gemini-3.8-flash"),
            "openrouter": ("deepseek/deepseek-v4.1-flash", "google/gemini-3.5-flash-lite"),
        }
        for preset, (flash, lite) in expected.items():
            made = self.tiered(preset, (preset, "openrouter"))
            self.assertEqual(made.resolve("flash").model, flash, preset)
            self.assertEqual(made.resolve("chores").model, lite, preset)
            self.assertEqual(made.resolve("flash").tier, "flash", preset)
            self.assertEqual(made.resolve("chores").tier, "lite", preset)

    def test_main_tier_roles_follow_the_panes_own_model(self):
        made = self.tiered("kimi", ("kimi",))
        for role in ("subagent", "switchboard"):
            resolved = made.resolve(role)
            self.assertTrue(resolved.is_main, role)
            self.assertEqual(resolved.tier, "main", role)
            self.assertEqual(resolved.config.model, "kimi-k3")

    def test_the_new_roles_default_exactly_like_the_ones_they_came_from(self):
        made = self.tiered("glm-coding", ("glm-coding", "openrouter"))
        self.assertEqual(made.resolve("summaries").model, made.resolve("flash").model)
        self.assertEqual(made.resolve("suggestions").model, made.resolve("flash").model)
        self.assertEqual(made.resolve("audit").model, made.resolve("chores").model)

    def test_loop_check_is_a_lite_role_and_falls_back_to_main_without_a_key(self):
        # Card #2CZP: the trigger-only loop double-check is a small structured judgement, so it rides
        # the Lite tier beside chores and the request audit rather than the pane's own model.
        made = self.tiered("kimi", ("kimi", "openrouter"))
        self.assertEqual(made.resolve("loop_check").model, made.resolve("chores").model)
        self.assertEqual(made.resolve("loop_check").tier, "lite")
        # With no Lite key it steps down like any other Lite role, and with no key at all it is the
        # main agent — which is what lets the check be skipped rather than fail a turn.
        self.assertEqual(made.resolve("loop_check").source, "default")
        stepped = self.tiered("kimi", ("kimi",)).resolve("loop_check")
        self.assertEqual(stepped.tier, "flash")

    def test_lite_without_its_key_steps_down_to_flash_with_an_inline_note(self):
        made = self.tiered("kimi", ("kimi",))          # no OpenRouter key
        chores = made.resolve("chores")
        self.assertEqual(chores.model, "kimi-k2.7-code-highspeed")
        self.assertEqual(chores.tier, "flash")
        self.assertIn("using flash", chores.note)
        # A step-down is expected, not a failure: it never reaches the protocol warnings list.
        self.assertIsNone(chores.warning)
        self.assertEqual(made.warnings, [])

    def test_a_tier_falls_back_to_main_when_nothing_else_has_a_key(self):
        # Custom endpoint: no tier table at all, so every tiered role is the main agent.
        config = ProviderConfig("https://example.invalid/v1", "house-model", "k", {}, 8192)
        made = RoleResolver(config, None, {}, key_lookup=lambda pid: "")
        for role in model_roles.ROLES:
            if role == "high":
                continue   # the High tier: the main model at max reasoning (see DefaultTests)
            self.assertTrue(made.resolve(role).is_main, role)
        self.assertEqual(made.warnings, [])
        self.assertEqual(made.resolve("high").tier, "high")

    def test_minimax_flash_is_its_own_highspeed_model(self):
        made = self.tiered("kimi", ("minimax",))
        made.rebase(ProviderConfig("https://api.minimax.io/v1", "MiniMax-M3", "minimax-key", {}, 8192),
                    "minimax", "high")
        self.assertEqual(made.resolve("flash").model, "MiniMax-M2.7-highspeed")
        # No OpenRouter key here, so Lite steps down to Flash.
        self.assertEqual(made.resolve("chores").model, "MiniMax-M2.7-highspeed")

    def test_a_role_can_name_a_tier(self):
        made = self.tiered("glm-coding", ("glm-coding",), roles={"subagent": {"tier": "flash"}})
        subagent = made.resolve("subagent")
        self.assertEqual(subagent.model, "glm-5.3-flash")
        self.assertEqual(subagent.source, "configured")
        self.assertEqual(subagent.tier, "flash")

    def test_a_tier_override_moves_every_role_that_follows_it(self):
        made = self.tiered("glm-coding", ("glm-coding", "openrouter"),
                           tiers={"flash": {"preset": "openrouter", "model": "google/gemini-3.8-flash"}})
        for role in ("flash", "terminal_use", "summaries", "suggestions"):
            self.assertEqual(made.resolve(role).model, "google/gemini-3.8-flash", role)
        # Main-tier roles are untouched.
        self.assertEqual(made.resolve("subagent").model, "glm-5.3")

    def test_a_tier_that_names_only_a_provider_uses_that_providers_tier_model(self):
        # The owner's case: Main on Kimi, Flash on the GLM Coding Plan, chosen as a provider with no
        # model typed. Flash must be glm-5.3-flash — picking the provider used to hand over glm-5.3,
        # the Main-grade model, at Main-grade prices.
        made = self.tiered("kimi", ("kimi", "glm-coding"), tiers={"flash": {"preset": "glm-coding"}})
        flash = made.resolve("flash")
        self.assertEqual((flash.preset_id, flash.model), ("glm-coding", "glm-5.3-flash"))
        self.assertEqual(flash.config.extra["reasoning_effort"], "low")   # GLM_FAST_EXTRA, not GLM_EXTRA
        self.assertEqual(made.resolve("main").model, "kimi-k3")
        # Lite on Z.AI: Z.AI's own Lite entry is Gemini through OpenRouter, and asking for Z.AI must not
        # drag OpenRouter in — it steps to the nearest tier that stays on the provider.
        lite = self.tiered("kimi", ("kimi", "glm-coding"), tiers={"lite": {"preset": "glm-coding"}}).resolve("chores")
        self.assertEqual((lite.preset_id, lite.model), ("glm-coding", "glm-5.3-flash"))
        # A model typed by hand still wins.
        typed = self.tiered("kimi", ("kimi", "glm-coding"),
                            tiers={"flash": {"preset": "glm-coding", "model": "glm-5.3"}})
        self.assertEqual(typed.resolve("flash").model, "glm-5.3")

    def test_tier_summary_reports_models_notes_and_no_keys(self):
        made = self.tiered("kimi", ("kimi",))
        summary = made.tier_summary()
        self.assertEqual(sorted(summary), ["flash", "high", "lite", "local", "main"])
        self.assertEqual(summary["main"]["model"], "kimi-k3")
        self.assertEqual((summary["high"]["model"], summary["high"]["effort"]), ("kimi-k3", "max"))
        self.assertEqual(summary["flash"]["model"], "kimi-k2.7-code-highspeed")
        self.assertEqual(summary["lite"]["using"], "flash")
        self.assertIn("using flash", summary["lite"]["note"])
        self.assertNotIn("kimi-key", json.dumps(summary))

    def test_probing_the_tiers_does_not_poison_the_role_cache(self):
        made = self.tiered("kimi", ("kimi",))
        made.tier_summary()
        self.assertTrue(made.resolve("subagent").is_main)
        self.assertEqual(made.resolve("flash").role, "flash")

    def test_validate_tiers(self):
        self.assertEqual(model_roles.validate_tiers(None), {})
        self.assertEqual(model_roles.validate_tiers({"flash": None}), {})
        # One object per tier, the form of before 2026-09-20, is a one-element list and is as
        # strict as it was; the list form and what it drops are tests/test_tier_lists.py.
        self.assertEqual(model_roles.validate_tiers({"lite": {"preset": "openrouter", "effort": "low"}}),
                         {"lite": [{"preset": "openrouter", "effort": "low"}]})
        self.assertEqual(model_roles.validate_tiers({"high": {"preset": "glm", "model": "glm-5.3"}}),
                         {"high": [{"preset": "glm", "model": "glm-5.3"}]})
        self.assertEqual(model_roles.validate_tiers({"high": None}), {})
        # Main is a list like the others now (the order a failing Main turn walks), and a tier
        # name Relay does not know is ignored rather than refused.
        self.assertEqual(model_roles.validate_tiers({"main": {"preset": "glm"}}), {"main": [{"preset": "glm"}]})
        self.assertEqual(model_roles.validate_tiers({"turbo": {"preset": "glm"}}), {})
        for bad in ({"flash": {"preset": "nope"}},
                    {"flash": {"model": "m"}}, {"flash": {"zzz": 1}}, {"flash": 3}, [1]):
            with self.assertRaises(ValueError):
                model_roles.validate_tiers(bad)

    def test_a_role_cannot_be_both_tiered_and_pinned(self):
        with self.assertRaises(ValueError):
            validate_roles({"flash": {"tier": "flash", "preset": "glm"}})
        with self.assertRaises(ValueError):
            validate_roles({"flash": {"tier": "turbo"}})

    def test_worker_accepts_tiers_and_reports_them(self):
        events, _ = run_worker([{"type": "configure", "preset": "kimi", "use_stored_key": True,
                                 "base_url": CONFIGS["kimi"][0], "model": CONFIGS["kimi"][1],
                                 "workspace": str(ROOT),
                                 "tiers": {"flash": {"preset": "kimi", "model": "kimi-k2.6"}}},
                                {"type": "set_agent_options", "id": "o1",
                                 "roles": {"subagent": {"tier": "flash"}},
                                 "tiers": {"flash": {"preset": "kimi", "model": "kimi-k2.6"}}},
                                {"type": "shutdown"}], {"RELAY_KIMI_API_KEY": "k"})
        configured = next(e for e in events if e["event"] == "configured")
        self.assertEqual(configured["tiers"]["flash"]["model"], "kimi-k2.6")
        self.assertEqual(configured["roles"]["flash"]["model"], "kimi-k2.6")
        options = next(e for e in events if e["event"] == "agent_options")
        self.assertEqual(options["roles"]["subagent"]["model"], "kimi-k2.6")
        roles_event = next(e for e in events if e["event"] == "model_roles")
        self.assertIn("tiers", roles_event)
        self.assertNotIn("error", [e["event"] for e in events])

    def test_a_bad_tier_table_is_an_error_and_changes_nothing(self):
        events, _ = run_worker([{"type": "configure", "preset": "kimi", "use_stored_key": True,
                                 "base_url": CONFIGS["kimi"][0], "model": CONFIGS["kimi"][1],
                                 "workspace": str(ROOT), "tiers": {"flash": {"preset": "nope"}}},
                                {"type": "shutdown"}], {"RELAY_KIMI_API_KEY": "k"})
        self.assertEqual(events[1]["event"], "error")
        self.assertIn("preset", events[1]["text"])


# ----- the helper agent leaves a guest (card #GH5T) -------------------------------------------
class LeaveGuestTests(unittest.TestCase):
    """`RoleResolver.leave_guest`: a helper worker whose Main is a guest harness walks the
    Options › Models priority list for a model it can actually build.

    Owner report, 2026-09-20: his Main is Claude Code, so the tab's helper worker was configured
    on `harness://claude` and every card and page turn died in `ProviderConfig.validate` —
    "Base URL must be an HTTPS URL without credentials, query, or fragment".
    """

    def guest(self, keys=("kimi",)):
        store = {name: f"{name}-key" for name in keys}
        return RoleResolver(ProviderConfig("harness://claude", "", "", {}, 32_768), "guest:claude",
                            key_lookup=lambda pid: store.get(pid, ""))

    def test_the_first_usable_entry_becomes_main_and_the_helper_follows_it(self):
        made = self.guest()
        spare = made.leave_guest([{"preset": "kimi", "model": "kimi-k3"}], "Claude Code")
        self.assertIsNotNone(spare)
        self.assertEqual(spare.preset_id, "kimi")
        self.assertEqual(made.main_config.model, "kimi-k3")
        self.assertEqual(made.main_preset_id, "kimi")
        helper = made.resolve(model_roles.HELPER_ROLE)
        self.assertEqual(helper.config.model, "kimi-k3")
        self.assertEqual(helper.config.base_url, CONFIGS["kimi"][0])
        # The model box reads the reason out of the role's note and the Main tier's.
        self.assertIn("Claude Code", helper.note)
        self.assertIn("kimi-k3", helper.note)
        self.assertIn("Claude Code", made.tier_summary()["main"]["note"])
        self.assertEqual(made.tier_summary()["main"]["model"], "kimi-k3")
        self.assertFalse(made.warnings)          # an expected fallback, not a protocol warning

    def test_a_guest_row_and_an_entry_with_no_key_are_skipped_in_the_users_order(self):
        made = self.guest(keys=("glm-coding",))
        spare = made.leave_guest([{"preset": "guest:codex", "model": ""},
                                  {"preset": "kimi", "model": "kimi-k3"},
                                  {"preset": "glm-coding", "model": "glm-5.3"}], "Claude Code")
        self.assertEqual(spare.preset_id, "glm-coding")
        self.assertEqual(made.main_config.model, "glm-5.3")

    def test_relay_free_is_a_target_only_when_the_list_names_it_and_it_works(self):
        made = self.guest(keys=())
        with mock.patch.object(model_roles.hosted, "available", return_value=False):
            self.assertIsNone(made.leave_guest([{"preset": "relay-free", "model": ""}], "Claude Code"))
        with mock.patch.object(model_roles.hosted, "available", return_value=True):
            spare = self.guest(keys=()).leave_guest([{"preset": "relay-free", "model": ""}], "Claude Code")
        self.assertIsNotNone(spare)
        self.assertEqual(spare.preset_id, "relay-free")

    def test_nothing_usable_leaves_the_resolver_on_the_guest_and_says_so(self):
        made = self.guest(keys=())
        self.assertIsNone(made.leave_guest([{"preset": "kimi", "model": "kimi-k3"}], "Claude Code"))
        self.assertEqual(made.main_config.base_url, "harness://claude")
        self.assertIn("nothing else it can use", made.resolve(model_roles.HELPER_ROLE).note)
        self.assertIsNone(made.leave_guest([], "Claude Code"))

    def test_a_role_pick_still_wins_over_where_the_resolver_landed(self):
        store = {"kimi": "kimi-key", "glm-coding": "glm-key"}
        made = RoleResolver(ProviderConfig("harness://claude", "", "", {}, 32_768), "guest:claude",
                            validate_roles({"switchboard": {"preset": "glm-coding"}}),
                            key_lookup=lambda pid: store.get(pid, ""))
        made.leave_guest([{"preset": "kimi", "model": "kimi-k3"}], "Claude Code")
        helper = made.resolve(model_roles.HELPER_ROLE)
        self.assertEqual(helper.preset_id, "glm-coding")
        self.assertEqual(helper.source, "configured")
        self.assertIsNone(helper.note)            # it was picked, so nothing fell back

    def test_a_later_set_model_clears_the_note(self):
        made = self.guest()
        made.leave_guest([{"preset": "kimi", "model": "kimi-k3"}], "Claude Code")
        made.rebase(main_config("glm"), "glm")
        self.assertIsNone(made.main_note)
        self.assertIsNone(made.resolve(model_roles.HELPER_ROLE).note)


if __name__ == "__main__":
    unittest.main()
