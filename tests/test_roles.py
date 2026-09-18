# SPDX-License-Identifier: GPL-3.0-or-later
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
from relay_core.provider import ProviderConfig
from relay_core.roles import RoleResolver, validate_roles
from relay_core.subagents import SubagentFactory
from relay_core.agents_defs import AgentDefinition

ROOT = Path(__file__).resolve().parents[1]

CONFIGS = {
    "kimi": ("https://api.moonshot.ai/v1", "kimi-k3"),
    "kimi-code": ("https://api.kimi.ai/coding/v1", "k3"),
    "glm": ("https://api.z.ai/api/paas/v4", "glm-5.3"),
    "glm-coding": ("https://api.z.ai/api/coding/paas/v4", "glm-5.3"),
    "openrouter": ("https://openrouter.ai/api/v1", "deepseek/deepseek-v4.1-flash"),
}


def main_config(preset="kimi", key="main-key"):
    base, model = CONFIGS[preset]
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

    def test_rejects_bad_tables(self):
        for bad in ({"nope": {"preset": "glm"}}, {"main": {"preset": "glm"}}, {"flash": {"preset": "nope"}},
                    {"flash": {"model": "m"}}, {"flash": {"effort": "turbo"}}, {"flash": {"extra": 3}},
                    {"flash": {"zzz": 1}}, [1, 2]):
            with self.assertRaises(ValueError):
                validate_roles(bad)


# ----- defaults -----------------------------------------------------------------------------
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
            self.assertTrue(made.resolve(role).is_main, role)
            self.assertEqual(made.resolve(role).model, "house-model")

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
    env = {k: v for k, v in os.environ.items()}
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

    def test_lite_without_its_key_steps_down_to_flash_with_an_inline_note(self):
        made = self.tiered("kimi", ("kimi",))          # no OpenRouter key
        chores = made.resolve("chores")
        self.assertEqual(chores.model, "kimi-k2.7-code-highspeed")
        self.assertEqual(chores.tier, "flash")
        self.assertIn("using Flash", chores.note)
        # A step-down is expected, not a failure: it never reaches the protocol warnings list.
        self.assertIsNone(chores.warning)
        self.assertEqual(made.warnings, [])

    def test_a_tier_falls_back_to_main_when_nothing_else_has_a_key(self):
        # Custom endpoint: no tier table at all, so every tiered role is the main agent.
        config = ProviderConfig("https://example.invalid/v1", "house-model", "k", {}, 8192)
        made = RoleResolver(config, None, {}, key_lookup=lambda pid: "")
        for role in model_roles.ROLES:
            self.assertTrue(made.resolve(role).is_main, role)
        self.assertEqual(made.warnings, [])

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
        self.assertEqual(sorted(summary), ["flash", "lite", "main"])
        self.assertEqual(summary["main"]["model"], "kimi-k3")
        self.assertEqual(summary["flash"]["model"], "kimi-k2.7-code-highspeed")
        self.assertEqual(summary["lite"]["using"], "flash")
        self.assertIn("using Flash", summary["lite"]["note"])
        self.assertNotIn("kimi-key", json.dumps(summary))

    def test_probing_the_tiers_does_not_poison_the_role_cache(self):
        made = self.tiered("kimi", ("kimi",))
        made.tier_summary()
        self.assertTrue(made.resolve("subagent").is_main)
        self.assertEqual(made.resolve("flash").role, "flash")

    def test_validate_tiers(self):
        self.assertEqual(model_roles.validate_tiers(None), {})
        self.assertEqual(model_roles.validate_tiers({"flash": None}), {})
        self.assertEqual(model_roles.validate_tiers({"lite": {"preset": "openrouter", "effort": "low"}}),
                         {"lite": {"preset": "openrouter", "effort": "low"}})
        for bad in ({"main": {"preset": "glm"}}, {"turbo": {"preset": "glm"}}, {"flash": {"preset": "nope"}},
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


if __name__ == "__main__":
    unittest.main()
