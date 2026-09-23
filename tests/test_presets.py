# SPDX-License-Identifier: AGPL-3.0-or-later
"""Provider presets, the Main/Flash/Lite tier table, and the GUI mirror in src/Pane.h.

Nothing here touches the network or the keyring: the tables are plain data and the mirror check
reads the C++ source as text.
"""
import json
import re
import unittest
from unittest import mock
from pathlib import Path

from relay_core import openrouter_catalog
from relay_core import presets as P
from relay_core import roles as model_roles

ROOT = Path(__file__).resolve().parents[1]


class PresetTableTests(unittest.TestCase):
    def test_every_preset_is_a_usable_openai_compatible_endpoint(self):
        for preset in P.PRESETS.values():
            with self.subTest(preset.id):
                self.assertTrue(preset.base_url.startswith("https://"), preset.base_url)
                self.assertFalse(preset.base_url.endswith("/"), "the transport appends /chat/completions")
                self.assertTrue(preset.model.strip())
                self.assertIn(preset.group, P.GROUPS)
                self.assertIn(preset.effort_style, P.EFFORT_LEVELS)
                self.assertGreaterEqual(preset.context_window, 128_000)
                self.assertTrue(preset.key_url.startswith("https://"), preset.id)
                self.assertTrue(preset.note)
                # ProviderConfig only accepts these request fields.
                self.assertFalse(set(preset.extra) - {"thinking", "reasoning", "reasoning_effort",
                                                      "temperature", "top_p"})

    def test_new_providers_are_present_with_their_documented_endpoints(self):
        expected = {
            # Verified 2026-09-17; the doc URL for each sits next to the entry in presets.py.
            "minimax": ("https://api.minimax.io/v1", "MiniMax-M3"),
            "openai": ("https://api.openai.com/v1", "gpt-6-astra"),
            "anthropic": ("https://api.anthropic.com/v1", "claude-opus-5-5"),
            "gemini": ("https://generativelanguage.googleapis.com/v1beta/openai", "gemini-3.1-pro-preview"),
        }
        for preset_id, (base_url, model) in expected.items():
            preset = P.PRESETS[preset_id]
            self.assertEqual((preset.base_url, preset.model), (base_url, model), preset_id)

    # ----- output limits (card #Z79Y) ---------------------------------------------------------
    def test_every_preset_documents_an_output_cap_that_fits_its_window(self):
        from relay_core.provider import MIN_OUTPUT_TOKENS
        for preset in P.PRESETS.values():
            with self.subTest(preset.id):
                # No floor at the fallback: a cap can legitimately be lower than it, as a gateway's
                # own limit is. Only the settable minimum and the window bound it.
                self.assertGreaterEqual(preset.max_output, MIN_OUTPUT_TOKENS)
                # A cap above the window would be unaskable; a cap above a quarter of it would leave
                # the reply fighting the compaction reserve.
                self.assertLessEqual(preset.max_output, preset.context_window // 4)

    def test_the_output_cap_is_the_providers_number_not_a_share_of_the_window(self):
        """Verified 2026-09-18; the doc URL for each sits next to the entry in presets.py."""
        self.assertEqual(P.PRESETS["glm-coding"].max_output, 131_072)      # docs.z.ai: 128K
        self.assertEqual(P.PRESETS["kimi"].max_output, 131_072)            # platform.kimi.ai
        self.assertEqual(P.PRESETS["anthropic"].max_output, 131_072)       # platform.claude.com
        self.assertEqual(P.PRESETS["openai"].max_output, 128_000)          # developers.openai.com
        self.assertEqual(P.PRESETS["gemini"].max_output, 65_536)           # ai.google.dev: "1M / 64k"
        # Gemini is why: a larger window than GLM-5.3 and half the output cap. Any rule that derives
        # output from the window — a flat 128K, or a share of it — gets this model wrong.
        gemini, glm = P.PRESETS["gemini"], P.PRESETS["glm-coding"]
        self.assertGreater(gemini.context_window, glm.context_window)
        self.assertLessEqual(gemini.max_output * 2, glm.max_output)
        # An aggregator picks the endpoint and the caps differ across them; so does a gateway that
        # rebuilds the request. Neither claims a number Relay cannot check.
        self.assertEqual(P.PRESETS["openrouter"].max_output, P.DEFAULT_MAX_OUTPUT)
        # Relay Free's cap is the gateway's, not a provider's: what it serves the Main role, which
        # is what a pane's turns run on. The gateway clamps rather than refusing, so asking more was
        # never an error — it just made Relay's own number twice the truth.
        self.assertEqual(P.PRESETS["relay-free"].max_output, 16_000)

    def test_automatic_means_the_models_own_cap_and_a_pinned_number_never_exceeds_it(self):
        gemini = P.PRESETS["gemini"]
        self.assertEqual(P.resolve_max_tokens(0, gemini), 65_536)          # automatic
        self.assertEqual(P.resolve_max_tokens(None, gemini), 65_536)       # nothing sent
        self.assertEqual(P.resolve_max_tokens(131_072, gemini), 65_536)    # pinned too high: clamped
        self.assertEqual(P.resolve_max_tokens(8_192, gemini), 8_192)       # pinned lower: kept
        # An endpoint Relay cannot name: automatic is conservative, but a number the user typed for
        # their own server is theirs to choose.
        self.assertEqual(P.resolve_max_tokens(0, None), P.DEFAULT_MAX_OUTPUT)
        self.assertEqual(P.resolve_max_tokens(131_072, None), 131_072)

    def test_the_gui_is_told_each_models_output_cap(self):
        self.assertEqual(P.PRESETS["gemini"].to_dict()["max_output"], 65_536)

    def test_subscriptions_aggregator_and_payg_are_all_represented(self):
        by_group = {}
        for preset in P.PRESETS.values():
            by_group.setdefault(preset.group, []).append(preset.id)
        self.assertEqual(sorted(by_group["subscription"]), ["glm-coding", "kimi-code", "minimax", "relay-pro"])
        self.assertEqual(by_group["aggregator"], ["openrouter"])
        self.assertEqual(sorted(by_group["payg"]),
                         ["anthropic", "deepseek", "gemini", "glm", "kimi", "openai"])
        self.assertEqual(by_group["included"], ["relay-free"])

    # ----- Relay Free (owner decision 2026-09-18) --------------------------------------------
    def test_relay_free_is_the_one_hosted_preset_and_is_listed_first(self):
        free = P.PRESETS["relay-free"]
        self.assertTrue(free.hosted)
        self.assertEqual([p.id for p in P.PRESETS.values() if p.hosted], ["relay-free", "relay-pro"])
        # The keys modal groups by GROUPS in order; "Included" leads, because a fresh install runs
        # on it before any key is stored.
        self.assertEqual(P.GROUPS[0], "included")
        self.assertEqual(P.GROUP_LABELS["included"], "Included")
        self.assertEqual((free.group, free.provider, free.plan), ("included", "relay", "included"))
        self.assertEqual(free.base_url, "https://api.relay-terminal.ai/v1")
        self.assertEqual(free.key_url, "https://relay-terminal.ai/free.html")
        # Medium reasoning and below (owner, 2026-09-18): the picker offers Low and Medium, the
        # default is Medium, and a higher level maps onto medium exactly as the gateway clamps it.
        self.assertEqual(free.effort_style, "relay")
        self.assertEqual(free.to_dict()["efforts"], ["low", "medium"])
        self.assertEqual(free.extra, {"reasoning_effort": "medium"})
        for level in ("high", "max"):
            self.assertEqual(P.apply_effort({}, "relay", level)[0], {"reasoning_effort": "medium"})
        self.assertEqual(P.apply_effort({}, "relay", "low")[0], {"reasoning_effort": "low"})
        self.assertEqual(P.infer_effort("relay", {"reasoning_effort": "medium"}), "medium")
        self.assertEqual(P.infer_effort("relay", {"reasoning_effort": "low"}), "low")
        self.assertEqual(P.TIER_DEFAULTS["relay-free"]["flash"][2], {"reasoning_effort": "low"})
        # It is neither a local server nor a plain BYOK row; to_dict says which it is.
        self.assertFalse(free.local)
        self.assertTrue(free.to_dict()["hosted"])
        self.assertFalse(P.PRESETS["kimi"].to_dict()["hosted"])

    def test_relay_free_tiers_are_the_gateways_three_roles(self):
        table = P.TIER_DEFAULTS["relay-free"]
        self.assertEqual({tier: entry[:2] for tier, entry in table.items()},
                         {"main": ("relay-free", "relay-main"), "flash": ("relay-free", "relay-flash"),
                          "lite": ("relay-free", "relay-lite")})
        # Nothing steps out to another provider: that would need the key the row exists to do without.
        for tier in P.PROVIDER_TIERS:
            self.assertEqual(P.provider_tier_model("relay-free", tier)[0], f"relay-{tier}")

    def test_every_preset_names_its_company_and_its_plan(self):
        # The roles modal picks a provider, so it shows the company, never the preset's model name.
        expected = {"relay-free": "relay", "relay-pro": "relay", "kimi": "kimi", "kimi-code": "kimi", "glm": "z.ai (glm)",
                    "glm-coding": "z.ai (glm)", "minimax": "minimax", "openrouter": "openrouter",
                    "openai": "openai (chatgpt)", "anthropic": "anthropic (claude)",
                    "gemini": "google (gemini)", "deepseek": "deepseek"}
        self.assertEqual({p.id: p.to_dict()["provider"] for p in P.PRESETS.values()}, expected)
        # Two presets of one company are told apart by their plan, so neither can be nameless.
        shared = {name for name in expected.values() if list(expected.values()).count(name) > 1}
        for preset in P.PRESETS.values():
            if expected[preset.id] in shared:
                self.assertTrue(preset.plan, preset.id)

    def test_a_provider_serves_a_tier_with_its_own_model_for_that_tier(self):
        self.assertEqual(P.provider_tier_model("glm-coding", "flash"), ("glm-5.3-flash", P.GLM_FAST_EXTRA))
        self.assertEqual(P.provider_tier_model("glm-coding", "main")[0], "glm-5.3")
        # Z.AI's Lite is Gemini on OpenRouter; asking for Z.AI stays on Z.AI.
        self.assertEqual(P.provider_tier_model("glm", "lite")[0], "glm-5.3-flash")
        self.assertEqual(P.provider_tier_model("openrouter", "lite")[0], "google/gemini-3.5-flash-lite")
        for preset_id in P.PRESETS:
            for tier in P.TIERS:
                model, _ = P.provider_tier_model(preset_id, tier)
                self.assertTrue(model, (preset_id, tier))

    def test_providers_without_an_effort_knob_send_no_effort_fields(self):
        # Anthropic's compat layer ignores reasoning_effort and MiniMax has no such field.
        for preset_id in ("anthropic", "minimax"):
            self.assertEqual(P.PRESETS[preset_id].effort_style, "none", preset_id)
        self.assertEqual(P.effort_levels("none"), [])
        self.assertEqual(P.effort_note("none"), "")
        for level in P.EFFORTS:
            extra, applied = P.apply_effort({"temperature": 0.2}, "none", level)
            self.assertEqual(applied, {})
            self.assertEqual(extra, {"temperature": 0.2})
        self.assertIsNone(P.infer_effort("none", {"reasoning_effort": "high"}))

    def test_every_style_offers_exactly_the_words_its_endpoint_takes(self):
        """Since 2026-09-21 the level offered *is* the level sent (card #MDL1), so this is the
        whole of what a picker may show for each provider."""
        self.assertEqual(P.effort_levels("openai"), ["low", "medium", "high", "xhigh"])
        self.assertEqual(P.effort_levels("openrouter"), ["low", "medium", "high", "xhigh"])
        # "minimal" is rejected by gemini-3.8-flash and "none" only works on 2.5 models.
        self.assertEqual(P.effort_levels("gemini"), ["low", "medium", "high"])
        self.assertEqual(P.effort_levels("kimi"), ["low", "high", "max"])
        self.assertEqual(P.effort_levels("glm"), ["low", "high", "max"])
        # Relay Free stops at medium: the gateway clamps each role, so anything above it is a
        # control that would only pretend (`effort_fixed` greys the box).
        self.assertEqual(P.effort_levels("relay"), ["low", "medium"])
        self.assertEqual(P.effort_levels("none"), [])

    def test_an_older_clients_level_lands_on_the_one_the_model_has(self):
        """Relay's own four are no longer the universe; they are a compatibility read.

        `nearest_effort` is the whole of it: the weakest listed level that is at least as much
        work, and the top of the list when there is none. That reproduces, exactly, every answer
        the {Relay level: provider value} table used to give — which is why the table could go.
        """
        self.assertEqual(P.nearest_effort("max", P.effort_levels("openai")), "xhigh")
        self.assertEqual(P.nearest_effort("max", P.effort_levels("gemini")), "high")
        self.assertEqual(P.nearest_effort("medium", P.effort_levels("kimi")), "high")
        self.assertEqual(P.nearest_effort("high", P.effort_levels("relay")), "medium")
        self.assertEqual(P.nearest_effort("max", P.effort_levels("kimi")), "max")
        self.assertIsNone(P.nearest_effort("high", P.effort_levels("none")))
        # A guest word off Relay's ladder is the provider's to judge, not this function's.
        self.assertEqual(P.nearest_effort("ultra", ["low", "medium", "high", "xhigh", "max", "ultra"]),
                         "ultra")
        self.assertEqual(P.nearest_effort("turbo", ["low", "high"]), "turbo")

    def test_validate_effort_checks_the_level_against_the_model_that_will_run_it(self):
        self.assertEqual(P.validate_effort("xhigh"), "xhigh")     # shape only, with no model
        self.assertEqual(P.validate_effort("max", P.effort_levels("openai")), "xhigh")
        self.assertEqual(P.validate_effort("high", ["low", "medium", "high"]), "high")
        self.assertEqual(P.validate_effort("ULTRA ", []), "ultra")   # no knob: nothing to check
        for bad in (None, 17, "", "  ", "Very High", "a" * 40):
            with self.assertRaises(ValueError):
                P.validate_effort(bad)

    def test_effort_fixed_is_no_knob_or_relay_free(self):
        """Owner, 2026-09-21: "for no knob models, the effort box should be grayed out. same for
        relay free"."""
        self.assertTrue(P.effort_fixed([]))
        self.assertTrue(P.effort_fixed(["low", "medium"], hosted=True))
        self.assertFalse(P.effort_fixed(["low", "medium"]))
        rows = {row["id"]: row for row in P.catalog_rows("relay-free")}
        self.assertTrue(all(row["effort_fixed"] for row in rows.values()), rows)
        self.assertTrue(P.PRESETS["relay-free"].to_dict()["effort_fixed"])
        # MiniMax and Anthropic have no knob at all; Kimi's high-speed model has none either.
        self.assertTrue(all(row["effort_fixed"] for row in P.catalog_rows("anthropic")))
        self.assertTrue({row["id"]: row for row in P.catalog_rows("kimi")}
                        ["kimi-k2.7-code-highspeed"]["effort_fixed"])
        self.assertFalse({row["id"]: row for row in P.catalog_rows("kimi")}["kimi-k3"]["effort_fixed"])

    def test_glm_flash_never_asks_to_disable_thinking(self):
        # Z.AI errors when thinking.type is "disabled" on GLM-5.3 and GLM-5.3-Flash. The rule is
        # Z.AI's, not everyone's — DeepSeek documents the same switch and *does* take "disabled"
        # (the test below) — so it is checked on the presets it belongs to.
        self.assertEqual(P.GLM_FAST_EXTRA["thinking"], {"type": "enabled"})
        for preset_id in ("glm", "glm-coding"):
            for _, _, extra in P.TIER_DEFAULTS[preset_id].values():
                self.assertNotEqual((extra.get("thinking") or {}).get("type"), "disabled", preset_id)

    def test_only_deepseeks_lite_turns_thinking_off(self):
        """Relay has no "off" among its four levels, so "off" is the tier's request (card #MDL1).

        https://api-docs.deepseek.com/api/create-chat-completion/: `thinking` takes enabled |
        disabled and is enabled by default. Lite is titles, labels and duplicate checks, so it asks
        for no reasoning tokens at all; Main and Flash keep the switch on and differ by level.
        """
        table = P.TIER_DEFAULTS["deepseek"]
        self.assertEqual(table["main"][2], {"thinking": {"type": "enabled"}, "reasoning_effort": "high"})
        self.assertEqual(table["flash"][2], {"thinking": {"type": "enabled"}, "reasoning_effort": "low"})
        self.assertEqual(table["lite"][2], {"thinking": {"type": "disabled"}})
        # Nobody else disables it, so a stray "disabled" elsewhere is still caught.
        off = {preset_id for preset_id, tiers in P.TIER_DEFAULTS.items()
               for _, _, extra in tiers.values()
               if (extra.get("thinking") or {}).get("type") == "disabled"}
        self.assertEqual(off, {"deepseek"})
        # And picking a level on that row switches thinking back on rather than sending both.
        extra, applied = P.apply_effort(table["lite"][2], "deepseek", "max")
        self.assertEqual(extra, {"thinking": {"type": "enabled"}, "reasoning_effort": "max"})
        self.assertEqual(applied, extra)


class DeepSeekTests(unittest.TestCase):
    """DeepSeek's own API (card #MDL1; owner, 2026-09-21: "i added deepseek as an api option").

    Everything here was read off https://api-docs.deepseek.com on 2026-09-21: the pricing page
    (models, windows, vision), /api/list-models (the two ids), /api/create-chat-completion
    (reasoning_effort, thinking, max_tokens) and /guides/thinking_mode.
    """

    def test_it_is_a_first_party_pay_as_you_go_api_like_glm_and_kimi(self):
        preset = P.PRESETS["deepseek"]
        self.assertEqual(preset.label, "deepseek \u00b7 v4.1 flash")
        self.assertEqual(preset.label, preset.label.lower())      # lower-case, Warp style
        self.assertEqual(preset.base_url, "https://api.deepseek.com")
        self.assertEqual(preset.group, "payg")
        self.assertEqual((preset.provider, preset.plan), ("deepseek", "pay-as-you-go"))
        # "deepseek pro is never used ... use deepseek-flash for all" (owner, 2026-09-21).
        self.assertEqual(preset.model, "deepseek-flash")
        self.assertEqual(preset.context_window, 1_048_576)        # "1M" on the pricing page
        self.assertEqual(preset.max_output, 131_072)              # its own thinking-mode ceiling
        self.assertFalse(preset.hosted or preset.local or preset.custom)

    def test_the_key_is_looked_up_like_every_other_providers(self):
        # Nothing was added for it: the env name and the keyring entry are derived from the id, so
        # the keys dialog and `has_stored_key` work the moment the preset exists.
        from relay_core import keystore
        self.assertEqual(keystore.env_name("deepseek"), "RELAY_DEEPSEEK_API_KEY")

    def test_the_two_models_are_the_two_ids_the_api_lists(self):
        rows = {row["id"]: row for row in P.catalog_rows("deepseek")}
        self.assertEqual(sorted(rows), ["deepseek-flash", "deepseek-v4-pro"])
        # `deepseek-flash` is a moving alias serving DeepSeek-V4.1-Flash, which OpenRouter serves
        # as `deepseek/deepseek-v4.1-flash` — one model, one name (rule 1), so the two fold into
        # one row of the picker instead of appearing twice.
        self.assertEqual(rows["deepseek-flash"]["name"], "deepseek-v4.1-flash")
        self.assertEqual(rows["deepseek-flash"]["name"],
                         P.model_name("openrouter", "deepseek/deepseek-v4.1-flash"))
        self.assertEqual(rows["deepseek-v4-pro"]["name"], "deepseek-v4-pro")
        self.assertEqual(rows["deepseek-flash"]["openrouter"], "deepseek/deepseek-v4.1-flash")
        self.assertEqual(rows["deepseek-v4-pro"]["openrouter"], "deepseek/deepseek-v4-pro")
        # Flash is the default for Main, Flash *and* Lite since 2026-09-21; a row carries the
        # first in PROVIDER_TIERS order, and Pro is the default for nothing.
        self.assertEqual(rows["deepseek-flash"]["tier"], "main")
        self.assertIsNone(rows["deepseek-v4-pro"]["tier"])
        self.assertEqual(P.tier_default("deepseek", "main")[1], "deepseek-flash")

    def test_deepseek_offers_its_own_three_words(self):
        # reasoning_effort is none | low | high | max; the picker offers exactly those three, and
        # a "medium" from an older client is read as high, which is what the API does with it too.
        self.assertEqual(P.effort_levels("deepseek"), ["low", "high", "max"])
        self.assertEqual(P.effort_note("deepseek"), "")
        self.assertEqual(P.apply_effort({}, "deepseek", "medium")[1],
                         {"thinking": {"type": "enabled"}, "reasoning_effort": "high"})

    def test_flash_reads_images_and_pro_does_not(self):
        self.assertTrue(P.model_supports_vision("deepseek-flash"))
        self.assertTrue(P.model_supports_vision("deepseek/deepseek-v4.1-flash"))
        self.assertFalse(P.model_supports_vision("deepseek-v4-pro"))
        self.assertTrue(P.PRESETS["deepseek"].vision)             # the preset's own model is Flash
        # So an image turn steps to Flash and back, exactly as it does on Z.AI.
        from relay_core.roles import VISION_DEFAULTS
        self.assertEqual(VISION_DEFAULTS["deepseek"], ("deepseek", "deepseek-flash", {}))

    def test_its_lite_tier_needs_no_second_providers_key(self):
        # Every other first-party API borrows Gemini through OpenRouter for Lite. DeepSeek serves
        # its own cheap model, so all three tiers stay on the one key.
        for _, (preset_id, _, _) in P.TIER_DEFAULTS["deepseek"].items():
            self.assertEqual(preset_id, "deepseek")


class ProviderKindAndOrderTests(unittest.TestCase):
    """`kind` and `order` on a `presets` row (card #MDL1, protocol 13.2)."""

    def test_every_row_carries_the_ranking_files_kind_and_order(self):
        from relay_core import model_ranking
        rank = model_ranking.load()
        for preset_id, preset in P.PRESETS.items():
            row = preset.to_dict()
            with self.subTest(preset_id):
                self.assertIn(row["kind"], model_ranking.KINDS)
                self.assertIsInstance(row["order"], int)
                if preset_id in rank.providers:
                    self.assertEqual(row["kind"], rank.providers[preset_id].kind)
                    self.assertEqual(row["order"], rank.providers[preset_id].order)

    def test_a_provider_the_file_does_not_name_gets_the_safe_pair(self):
        # The owner edits `model-ranking.md` by hand and may be half-way through it. A provider it
        # does not name must not break the worker or empty the picker: it is an `api` sorting after
        # every provider the file does name, and nothing raises.
        self.assertEqual(P.provider_rank("no-such-provider"),
                         ("api", model_ranking_unknown_order()))
        # The two cases the row itself settles keep their kind with no row at all.
        self.assertEqual(P.provider_rank("no-such-provider", hosted=True)[0], "free")
        self.assertEqual(P.provider_rank("guest:nobody")[0], "harness")

    def test_the_rows_order_is_what_the_picker_sorts_providers_by(self):
        # src/ModelCatalog.cpp `grouped()` reads these two off the row instead of keeping a second
        # copy of rule 2.2, so a provider Relay prefers sorts first among those serving one model.
        rows = {p.id: p.to_dict() for p in P.PRESETS.values()}
        self.assertLess(rows["relay-free"]["order"], 1000)
        self.assertEqual(rows["relay-free"]["kind"], "free")
        # Relay Free is spent last: every other built-in sorts before it.
        for preset_id, row in rows.items():
            if preset_id != "relay-free":
                self.assertLess(row["order"], rows["relay-free"]["order"], preset_id)


def model_ranking_unknown_order():
    from relay_core import model_ranking
    return model_ranking.UNKNOWN_PROVIDER_ORDER



class TierTableTests(unittest.TestCase):
    def test_every_preset_has_all_three_tiers_pointing_at_real_presets(self):
        # PROVIDER_TIERS, not TIERS: "local" is a fourth tier that belongs to no provider and is
        # resolved from the local-endpoint registry, so it has no row here (2026-09-18).
        self.assertEqual(sorted(P.TIER_DEFAULTS), sorted(P.PRESETS))
        for provider, table in P.TIER_DEFAULTS.items():
            self.assertEqual(sorted(table), sorted((*P.PROVIDER_TIERS, "high")
                             if provider == "relay-pro" else P.PROVIDER_TIERS), provider)
            for tier, (preset_id, model, extra) in table.items():
                self.assertIn(preset_id, P.PRESETS, f"{provider}.{tier}")
                self.assertTrue(model.strip(), f"{provider}.{tier}")
                self.assertIsInstance(extra, dict)

    def test_owner_requested_defaults(self):
        expected = {
            "glm": ("glm-5.3", "glm-5.3-flash", "google/gemini-3.8-flash"),
            "glm-coding": ("glm-5.3", "glm-5.3-flash", "google/gemini-3.8-flash"),
            "kimi": ("kimi-k3", "kimi-k2.7-code-highspeed", "google/gemini-3.8-flash"),
            "kimi-code": ("k3", "kimi-for-coding-highspeed", "google/gemini-3.8-flash"),
            # No non-flash DeepSeek V4.1 exists on OpenRouter, so Main falls back to the flash model.
            "openrouter": ("deepseek/deepseek-v4.1-flash", "deepseek/deepseek-v4.1-flash",
                           "google/gemini-3.5-flash-lite"),
            "minimax": ("MiniMax-M3", "MiniMax-M2.7-highspeed", "google/gemini-3.8-flash"),
            "anthropic": ("claude-opus-5-5", "claude-sonnet-6", "claude-haiku-4-5"),
            "gemini": ("gemini-3.1-pro-preview", "gemini-3.8-flash", "gemini-3.5-flash-lite"),
        }
        for provider, (main, flash, lite) in expected.items():
            table = P.TIER_DEFAULTS[provider]
            self.assertEqual((table["main"][1], table["flash"][1], table["lite"][1]),
                             (main, flash, lite), provider)

    def test_the_cross_provider_lite_tier_is_openrouter(self):
        for provider in ("glm", "glm-coding", "kimi", "kimi-code", "minimax"):
            self.assertEqual(P.TIER_DEFAULTS[provider]["lite"][0], "openrouter", provider)

    def test_recommended_pairs_name_real_presets(self):
        self.assertEqual(P.RECOMMENDED, (("glm-coding", "openrouter"), ("kimi-code", "openrouter")))
        for first, second in P.RECOMMENDED:
            self.assertIn(first, P.PRESETS)
            self.assertIn(second, P.PRESETS)

    def test_tier_fallbacks_always_end_at_main(self):
        self.assertEqual(P.tier_fallbacks("lite"), ("lite", "flash", "main"))
        self.assertEqual(P.tier_fallbacks("flash"), ("flash", "main"))
        self.assertEqual(P.tier_fallbacks("main"), ("main",))
        self.assertEqual(P.tier_fallbacks("high"), ("high", "main"))
        with self.assertRaises(ValueError):
            P.tier_fallbacks("turbo")

    def test_high_is_a_tier_above_main_with_no_provider_row(self):
        # Owner, 2026-09-20: "a 'high' default on top of main, used by the planner by default".
        self.assertEqual(P.TIERS, ("high", "main", "flash", "lite", "local"))
        self.assertEqual(P.PROVIDER_TIERS, ("main", "flash", "lite"))
        self.assertEqual(P.TIER_LABELS["high"], "high")   # lower-case, card #MDL1 rule 1
        self.assertIn("max reasoning", P.TIER_HINTS["high"])
        self.assertEqual(P.validate_tier("high"), "high")
        for provider in P.TIER_DEFAULTS:
            if provider == "relay-pro":
                self.assertEqual(P.tier_default(provider, "high")[1], "relay-pro-high")
                continue
            self.assertNotIn("high", P.TIER_DEFAULTS[provider], provider)
            self.assertIsNone(P.tier_default(provider, "high"), provider)
            # Naming a provider for High means its Main model: there is no bigger one to pick.
            self.assertEqual(P.provider_tier_model(provider, "high"), P.provider_tier_model(provider, "main"))

    def test_role_tiers_cover_every_role(self):
        self.assertEqual(sorted(model_roles.ROLE_TIERS), sorted(model_roles.ROLES))
        # The High tier serves the pane role /high switches to (card #MDL1) alone: planning left
        # the tier with card #HR5E; plan mode puts the pane on /high itself (#PH9G).
        self.assertEqual(sorted(r for r, t in model_roles.ROLE_TIERS.items() if t == "high"),
                         ["high"])
        self.assertEqual([role for role, tier in model_roles.ROLE_TIERS.items() if tier == "main"],
                         ["main", "subagent", "switchboard"])
        self.assertEqual(sorted(r for r, t in model_roles.ROLE_TIERS.items() if t == "flash"),
                         ["flash", "suggestions", "summaries", "terminal_use"])
        self.assertEqual(sorted(r for r, t in model_roles.ROLE_TIERS.items() if t == "lite"),
                         ["audit", "chores", "loop_check"])
        # Command routing is its own override, never moved by the Lite row.
        self.assertIsNone(model_roles.ROLE_TIERS["route_assist"])
        self.assertEqual(model_roles.ROUTE_ASSIST_DEFAULT[1], "google/gemini-3.5-flash-lite")

    def test_action_catalog_names_every_role_once(self):
        actions = model_roles.action_catalog()
        self.assertEqual(sorted(a["role"] for a in actions), sorted(model_roles.ROLES))
        for action in actions:
            self.assertTrue(action["label"] and action["hint"])
            # Named after the job, not the protocol id.
            self.assertNotEqual(action["label"], action["role"])
        self.assertFalse(next(a for a in actions if a["role"] == "main")["settable"])

    def test_tier_catalog_is_json_safe_and_complete(self):
        catalog = model_roles.tier_catalog()
        json.dumps(catalog)
        self.assertEqual([t["id"] for t in catalog["tiers"]], list(P.TIERS))
        # High is drawn first, above Main, and carries its label and hint like every other row.
        self.assertEqual(catalog["tiers"][0], {"id": "high", "label": "high", "hint": P.TIER_HINTS["high"]})
        self.assertEqual(sorted(catalog["providers"]), sorted(P.PRESETS))
        for provider, table in catalog["providers"].items():
            self.assertEqual(sorted(table), sorted((*P.PROVIDER_TIERS, "high")
                             if provider == "relay-pro" else P.PROVIDER_TIERS), provider)


class ModelCatalogTests(unittest.TestCase):
    """One list of models per provider row (owner, 2026-09-20): MODEL_CATALOG and catalog_rows()."""

    ROW_KEYS = {"id", "name", "label", "tier", "efforts", "effort_fixed", "intelligence",
                "openrouter", "default_effort", "tier_effort"}

    def setUp(self):
        # The `openrouter` row also carries OpenRouter's live listing when this machine has fetched
        # one (openrouter_catalog.py, tests/test_openrouter_catalog.py); here the table is the subject.
        patcher = mock.patch("relay_core.openrouter_catalog.rows", return_value=[])
        patcher.start()
        self.addCleanup(patcher.stop)

    @staticmethod
    def tiers_naming(target: str, model: str) -> set:
        """Every tier, in any provider's table, whose entry is (target, model)."""
        return {tier for table in P.TIER_DEFAULTS.values()
                for tier, entry in table.items() if entry[:2] == (target, model)}

    def test_every_preset_has_a_catalog_and_every_catalog_names_a_preset(self):
        self.assertEqual(sorted(P.MODEL_CATALOG), sorted(P.PRESETS))
        for preset_id, rows in P.MODEL_CATALOG.items():
            with self.subTest(preset_id):
                self.assertGreaterEqual(len(rows), 1)
                # The preset's own default model is always one of its rows.
                self.assertIn(P.PRESETS[preset_id].model, [row["id"] for row in rows])

    def test_ids_are_unique_within_a_preset(self):
        for preset_id, rows in P.MODEL_CATALOG.items():
            ids = [row["id"] for row in rows]
            self.assertEqual(len(ids), len(set(ids)), preset_id)
            for row in rows:
                self.assertTrue(row["id"].strip(), (preset_id, row))
                # Only the few that cannot be derived carry a `name` in the table.
                self.assertTrue(row.get("name", "x").strip(), (preset_id, row))

    def test_tier_rows_match_tier_defaults(self):
        # Every model a tier names has a row on the *target* preset, marked with a tier that names
        # it: Z.AI's Lite is Gemini 3.8 Flash on OpenRouter, so that row is a lite row of openrouter.
        for provider, table in P.TIER_DEFAULTS.items():
            for tier, (target, model, _) in table.items():
                with self.subTest(f"{provider}.{tier}"):
                    row = next((r for r in P.MODEL_CATALOG[target] if r["id"] == model), None)
                    self.assertIsNotNone(row, (target, model))
                    self.assertIn(row["tier"], self.tiers_naming(target, model))
        # And a row's tier is never a claim no table makes.
        for preset_id, rows in P.MODEL_CATALOG.items():
            for row in rows:
                named = self.tiers_naming(preset_id, row["id"])
                if row["tier"] is None:
                    self.assertEqual(named, set(), (preset_id, row["id"]))
                else:
                    self.assertIn(row["tier"], P.TIERS)
        # Two tiers naming one model (DeepSeek on OpenRouter) keep the first in PROVIDER_TIERS order.
        deepseek = next(r for r in P.MODEL_CATALOG["openrouter"] if r["id"] == "deepseek/deepseek-v4.1-flash")
        self.assertEqual(deepseek["tier"], "main")
        self.assertEqual(self.tiers_naming("openrouter", "deepseek/deepseek-v4.1-flash"), {"main", "flash"})

    def test_every_label_provider_and_plan_is_lower_case(self):
        # Warp style (owner, 2026-09-20): the GUI shows these as they are and never re-cases them.
        for preset in P.PRESETS.values():
            for text in (preset.label, preset.provider, preset.plan):
                self.assertEqual(text, text.lower(), preset.id)
        for preset_id in P.MODEL_CATALOG:
            for row in P.catalog_rows(preset_id):
                self.assertEqual(row["label"], row["label"].lower(), (preset_id, row["id"]))
        self.assertEqual(P.PRESETS["glm-coding"].label, "z.ai · glm-5.3 · coding plan")
        self.assertEqual(P.PRESETS["relay-free"].label, "relay free")

    def test_catalog_rows_resolve_efforts_and_carry_intelligence(self):
        for preset_id, preset in P.PRESETS.items():
            with self.subTest(preset_id):
                rows = P.catalog_rows(preset_id)
                self.assertEqual([r["id"] for r in rows], [r["id"] for r in P.MODEL_CATALOG[preset_id]])
                offered = P.effort_levels(preset.effort_style)
                for row, raw in zip(rows, P.MODEL_CATALOG[preset_id]):
                    self.assertEqual(set(row), self.ROW_KEYS)
                    self.assertIsInstance(row["efforts"], list)
                    # None means the preset's style; a list only ever narrows what it offers.
                    self.assertEqual(row["efforts"], offered if raw["efforts"] is None else raw["efforts"])
                    self.assertTrue(set(row["efforts"]) <= set(offered), (preset_id, row["id"]))
                    # INTELLIGENCE is keyed by the *name* since card #MDL1: one model, one score.
                    self.assertEqual(row["name"], P.model_name(preset_id, row["id"]))
                    self.assertIn(row["name"], P.INTELLIGENCE)
                    self.assertEqual(row["intelligence"], P.INTELLIGENCE[row["name"]])
                    self.assertEqual(row["label"], row["name"])
                    self.assertIn(row["intelligence"], (None, *range(0, 101)))
                    # The same model on OpenRouter, or None: where the per-model toggle shows.
                    self.assertEqual(row["openrouter"], P.openrouter_twin(row["id"]))
        # Resolved, not the style's name: the GUI never has to know about styles.
        self.assertEqual(P.catalog_rows("relay-free")[0]["efforts"], ["low", "medium"])
        self.assertEqual(P.catalog_rows("anthropic")[0]["efforts"], [])
        # Kimi documents reasoning_effort for K3 alone, so its high-speed rows carry no levels.
        by_id = {r["id"]: r for r in P.catalog_rows("kimi-code")}
        self.assertEqual(by_id["k3"]["efforts"], ["low", "high", "max"])
        self.assertEqual(by_id["kimi-for-coding-highspeed"]["efforts"], [])
        # An id with no catalog — a local endpoint, a guest, nonsense — is [] and never raises.
        for other in ("local:bonsai", "guest:codex", "", None, 7):
            self.assertEqual(P.catalog_rows(other), [])

    # ----- one model, one name (card #MDL1, docs/MODEL-PICKING-DESIGN.md rule 1) ------------------

    NAME_RE = re.compile(r"^[a-z0-9][a-z0-9._:+-]*$")

    def test_every_row_of_every_preset_has_a_well_formed_name(self):
        # Lower-case, no spaces, no vendor prefix (owner, 2026-09-21), on every row the worker can
        # emit: the built-in table and the cached OpenRouter listing, which is the only one whose
        # ids carry a "/", a ":variant" and a "~" moving alias.
        for preset_id in P.MODEL_CATALOG:
            for row in P.catalog_rows(preset_id):
                with self.subTest(preset=preset_id, model=row["id"]):
                    self.assertRegex(row["name"], self.NAME_RE)
                    self.assertEqual(row["label"], row["name"])
        listing = openrouter_catalog.parse_rows({"data": [
            {"id": "openai/gpt-6-sol", "name": "OpenAI: GPT-6 Sol", "context_length": 400000},
            {"id": "anthropic/claude-haiku-4.5", "name": "Anthropic: Claude Haiku 4.5"},
            {"id": "moonshotai/kimi-k3:batch", "name": "MoonshotAI: Kimi K3 (batch)"},
            {"id": "~openai/gpt-sol-latest", "name": "OpenAI: GPT Sol (latest)"},
            {"id": "z-ai/glm-5.3", "name": "Z.AI: GLM 5.3"},
        ]})
        self.assertEqual([row["name"] for row in listing],
                         ["gpt-6-sol", "claude-haiku-4.5", "kimi-k3:batch", "gpt-sol-latest", "glm-5.3"])
        for row in listing:
            self.assertRegex(row["name"], self.NAME_RE)
            self.assertEqual(row["label"], row["name"])

    def test_the_names_the_design_doc_tabulates(self):
        # Section 1.2: the same model spelled several ways, and what it is called.
        self.assertEqual(P.model_name("kimi-code", "k3"), "kimi-k3")           # the one hand-set alias
        self.assertEqual(P.model_name("kimi", "kimi-k3"), "kimi-k3")
        self.assertEqual(P.model_name("anthropic", "claude-haiku-4-5"), "claude-haiku-4.5")
        self.assertEqual(P.model_name("anthropic", "claude-fable-5-1"), "claude-fable-5.1")
        self.assertEqual(P.model_name("minimax", "MiniMax-M3"), "minimax-m3")  # capitals never reach a person
        self.assertEqual(P.model_name("openai", "gpt-6-sol"), "gpt-6-sol")
        self.assertEqual(P.model_name("guest:codex", "gpt-6-sol"), "gpt-6-sol")
        self.assertEqual(P.model_name("openrouter", "openai/gpt-6-sol"), "gpt-6-sol")
        # A guest alias is named after the model it points at (owner, 2026-09-21).
        for alias, model in P.GUEST_MODEL_ALIASES.items():
            self.assertEqual(P.model_name("guest:claude", alias), P.model_name("anthropic", model), alias)
        self.assertEqual(P.model_name("guest:claude", "opus"), "claude-opus-5-5")
        self.assertEqual(P.model_name("guest:claude", "fable"), "claude-fable-5.1")
        # Relay Free's three are role names and keep them, hyphenated (design 3.5).
        self.assertEqual([r["name"] for r in P.catalog_rows("relay-free")],
                         ["relay-main", "relay-flash", "relay-lite"])
        # Serving variants are their own models to the person picking one (design 3.3).
        self.assertEqual(P.model_name("kimi-code", "k3-256k"), "k3-256k")
        self.assertEqual(P.model_name("kimi", "kimi-k2.7-code-highspeed"), "kimi-k2.7-code-highspeed")
        self.assertEqual(P.model_name("minimax", "MiniMax-M2.7-highspeed"), "minimax-m2.7-highspeed")
        # A moving alias loses OpenRouter's "~" and nothing else (design 3.4).
        self.assertEqual(P.derived_name("~openai/gpt-sol-latest"), "gpt-sol-latest")
        # A hand-typed id with capitals and spaces is still a name (design 3.13).
        self.assertEqual(P.derived_name("  OpenAI/GPT 6 Sol "), "gpt-6-sol")
        # Nothing at all is nothing, not a crash.
        for junk in ("", "   ", None, 7):
            self.assertEqual(P.model_name("openai", junk), "")

    def test_a_first_party_model_and_its_openrouter_twin_share_one_name(self):
        # The point of the rule: the twin table and the names must not drift, or the picker grows a
        # second row for a model it already has. The exceptions are the three ids that deliberately
        # twin onto a *different* model — a serving variant mapped to the plain slug — which the
        # OPENROUTER_TWINS comment sets out.
        variants = {"kimi-k2.7-code-highspeed", "k3-256k", "MiniMax-M2.7-highspeed"}
        by_id = {row["id"]: preset_id for preset_id, rows in P.MODEL_CATALOG.items() for row in rows}
        for model_id, slug in P.OPENROUTER_TWINS.items():
            with self.subTest(model_id):
                same = P.model_name(by_id[model_id], model_id) == P.derived_name(slug)
                self.assertEqual(same, model_id not in variants)

    def test_one_model_is_scored_once_and_the_numbers_did_not_move(self):
        # INTELLIGENCE is keyed by name, so Kimi Code's "k3" and the Kimi platform's "kimi-k3" are
        # one entry — they used to be two hand-kept 44s. The numbers the GUI reads are unchanged.
        was = {("kimi-code", "k3"): 44, ("kimi", "kimi-k3"): 44, ("glm", "glm-5.3"): 45,
               ("glm-coding", "glm-5.3"): 45, ("openai", "gpt-6-astra"): 53,
               ("openai", "gpt-6-sol"): 47, ("anthropic", "claude-opus-5-5"): 51,
               ("anthropic", "claude-fable-5-1"): 53, ("anthropic", "claude-sonnet-6"): None,
               ("relay-free", "relay-main"): None}
        for (preset_id, model_id), score in was.items():
            rows = {row["id"]: row for row in P.catalog_rows(preset_id)}
            self.assertEqual(rows[model_id]["intelligence"], score, (preset_id, model_id))
        self.assertNotIn("k3", P.INTELLIGENCE)     # scored through the name now, not by hand twice
        self.assertEqual(P.INTELLIGENCE["kimi-k3"], 44)

    def test_the_scores_and_the_group_order_come_out_of_the_ranking_file(self):
        # Card #MDL1: the numbers left presets.py for `model-ranking.md`, the file the owner asked
        # to be able to review and edit. INTELLIGENCE is a view over its `score` column, so every
        # reader goes on saying .get / [] / `in` — and the old hand-written _MAIN_GROUP_ORDER is a
        # view over its `order`. tests/test_model_ranking.py tests the file and the rules.
        from relay_core import model_ranking
        rank = model_ranking.load()
        self.assertEqual(dict(P.INTELLIGENCE),
                         {name: row.score for name, row in rank.models.items()})
        self.assertEqual(P.INTELLIGENCE.get("glm-5.3"), rank.score("glm-5.3"))
        # A group is the *first* order of the built-ins that carry it, and `guest` the first
        # harness. Which provider that is belongs to the file and to nobody here: the owner
        # re-ordered the table on 2026-09-21 and a group named by hand went stale the same day, so
        # each expectation is computed from the file rather than spelled out.
        first = {}
        for preset_id, preset in P.PRESETS.items():
            order = rank.provider_order(preset_id)
            first[preset.group] = min(order, first.get(preset.group, order))
        for group, order in first.items():
            self.assertEqual(P._MAIN_GROUP_ORDER[group], order, group)
        self.assertEqual(P._MAIN_GROUP_ORDER["guest"],
                         min(row.order for row in rank.providers.values() if row.kind == "harness"))
        self.assertEqual(P._MAIN_GROUP_ORDER["included"], rank.provider_order("relay-free"))
        # No preset carries `guest` or `custom`, so those two are read off the file another way:
        # the first harness, and — for a custom OpenAI-compatible endpoint the user added — the
        # same band as pay-as-you-go, which is where the hand-written table put it.
        self.assertEqual(P._MAIN_GROUP_ORDER["custom"], P._MAIN_GROUP_ORDER["payg"])
        # Relay Free is last whatever else moves, and every group the file names sorts before it.
        included = P._MAIN_GROUP_ORDER["included"]
        for group in ("subscription", "guest", "payg", "aggregator"):
            self.assertLess(P._MAIN_GROUP_ORDER[group], included, group)

    def test_every_openrouter_twin_names_a_catalog_model_and_a_real_slug(self):
        # OPENROUTER_TWINS (owner, 2026-09-20): keyed by a cloud catalog model id, valued by the
        # OpenRouter slug serving the same model (verified against openrouter.ai/api/v1/models).
        catalog_ids = {row["id"] for preset_id, rows in P.MODEL_CATALOG.items()
                       if preset_id not in ("relay-free", "openrouter") for row in rows}
        self.assertTrue(set(P.OPENROUTER_TWINS) <= catalog_ids, set(P.OPENROUTER_TWINS) - catalog_ids)
        for model_id, slug in P.OPENROUTER_TWINS.items():
            with self.subTest(model_id):
                self.assertIsInstance(slug, str)
                self.assertTrue(slug.strip(), model_id)
                self.assertEqual(slug, slug.strip())
                # A slug is vendor/model, exactly one slash, no variant suffix (":batch", ":free").
                self.assertEqual(slug.count("/"), 1, slug)
                self.assertNotIn(":", slug)
                self.assertNotIn("/", model_id)
                self.assertEqual(P.openrouter_twin(model_id), slug)
        # The rows that are already OpenRouter, and Relay's own gateway, have no twin.
        for preset_id in ("openrouter", "relay-free"):
            for row in P.catalog_rows(preset_id):
                self.assertIsNone(row["openrouter"], (preset_id, row["id"]))
        for other in ("deepseek/deepseek-v4.1-flash", "kimi-for-coding", "", None, 7):
            self.assertIsNone(P.openrouter_twin(other), other)
        # The one the owner asked for by name.
        self.assertEqual(P.openrouter_twin("glm-5.3-flash"), "z-ai/glm-5.3-flash")
        self.assertEqual(P.openrouter_twin("glm-5.3"), "z-ai/glm-5.3")

    def test_the_presets_event_carries_the_catalog(self):
        # to_dict() is what the worker's `presets` event sends per row, so `models` rides on it.
        for preset_id, preset in P.PRESETS.items():
            self.assertEqual(preset.to_dict()["models"], P.catalog_rows(preset_id), preset_id)
        json.dumps([p.to_dict() for p in P.PRESETS.values()])
        # The same three keys a guest row's models already carry (29.3), so one GUI reads both.
        for row in P.PRESETS["openai"].to_dict()["models"]:
            self.assertTrue({"id", "name", "label", "efforts"} <= set(row))


class GuiMirrorTests(unittest.TestCase):
    """The GUI consumes the worker's presets instead of maintaining a second table."""

    def test_the_gui_uses_worker_presets_without_a_duplicate_table(self):
        source = (ROOT / "src/Pane.h").read_text(encoding="utf-8")
        # #MDL1 retired the advanced provider dialog and its hand-maintained mirror.
        # The surviving provider UI must still receive the worker's authoritative rows.
        self.assertNotIn("// Mirrors backend/relay_core/presets.py", source)
        block = source.split('type == QStringLiteral("presets")', 1)[1].split(
            'else if (type ==', 1)[0]
        self.assertIn('m_presets = event.value(QStringLiteral("presets")).toArray();', block)
        self.assertIn('m_keysDialog->setPresets(providerPresets());', block)

    def test_one_serving_model_state_feeds_the_picker(self):
        """The model box names the model actually serving the turn, from one state (C5).

        Relay moves a turn off the pane's own model in three places — plan mode's `planning` role
        (protocol 13.11), an image turn's vision model (17.3) and a failover onto a provider that
        answers (15.2.2). Two of the three used to have a member of their own and the third had
        nothing, so a turn that failed over went on claiming the pane's model was serving it. The
        box, the tooltip and the clearing are one mechanism now, which is what this checks.
        """
        source = (ROOT / "src/Pane.h").read_text(encoding="utf-8")
        # The retired members are gone, and with them the two prefixes only they used.
        for retired in ("m_visionModel", "m_planModel", 'QStringLiteral("vision:")',
                        'QStringLiteral("planning:")'):
            self.assertNotIn(retired, source)
        # All three events feed it, and each move's own `*_ended` takes it back out.
        for event, call in (("vision_route", 'pushServingModel(QStringLiteral("vision"), event)'),
                            ("plan_route", 'pushServingModel(QStringLiteral("plan"), event)'),
                            ("vision_route_ended", 'popServingModel(QStringLiteral("vision"))'),
                            ("plan_route_ended", 'popServingModel(QStringLiteral("plan"))')):
            self.assertIn(call, source, event)
        retry = source.split('type == QStringLiteral("provider_retry")', 1)[1].split("else if (type ==", 1)[0]
        self.assertIn('if (reason == QStringLiteral("failover")) pushServingModel(reason, event);', retry)
        self.assertIn('popServingModel(QStringLiteral("failover"))', retry)
        # A stall, a truncated step or an HTTP retry is the same model trying again: not a move.
        for reason in ('QStringLiteral("stall")', 'QStringLiteral("truncated")'):
            self.assertNotIn(reason, retry)
        # The turn's end empties it however the turn ended, so nothing can be left naming a model.
        clock = source.split("void stopTurnClock() {", 1)[1].split("void tickTurnClock()", 1)[0]
        self.assertIn("clearServingModels();", clock)
        # One row in the box, marked with what moved the turn, and it is not a preset to pick.
        picker = source.split("void refreshPickers() {", 1)[1].split("// ----- Claude Code and Codex", 1)[0]
        # The row prints the model's *name* since card #MDL1 (rule 1); the data is still the id.
        self.assertIn('QStringLiteral("%1 %2 · this turn").arg(servingMark(serving.why), servingModelName(serving))', picker)
        self.assertIn('QStringLiteral("serving:") + serving.model', picker)
        self.assertIn("servingTooltip()", picker)
        guard = source.split("void selectModel(const QString &id, const QString &model = QString()) {", 1)[1].split("\n    }", 1)[0]
        self.assertIn('id.startsWith(QStringLiteral("serving:"))', guard)
        # The tooltip says which model the turn goes back to, and that a pick still lands at its end.
        tooltip = source.split("QString servingTooltip() const {", 1)[1].split("\n    }", 1)[0]
        self.assertIn("for this turn; back to %2 after", tooltip)
        self.assertIn("from the end of this turn", tooltip)

    def test_the_cpp_role_list_matches_roles_py(self):
        source = (ROOT / "src/Pane.h").read_text(encoding="utf-8")
        block = source.split("static QStringList roleIds() {", 1)[1].split("}", 1)[0]
        mirrored = re.findall(r'QStringLiteral\("([a-z_]+)"\)', block)
        self.assertEqual(sorted(mirrored), sorted(model_roles.SETTABLE))


if __name__ == "__main__":
    unittest.main()
