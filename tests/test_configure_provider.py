# SPDX-License-Identifier: AGPL-3.0-or-later
"""How `configure` / `set_model` turn a request into a provider, and what a refused key reads like.

The Board regression (owner, 2026-09-18: "'ask the agent' didnt work. it said provider HTTP
401"): the board built its `configure` out of four independent QSettings keys — `provider/preset`,
which every model switch rewrites, and `provider/base|model|extra`, which only a full re-configure
rewrites. Once those disagreed it named one preset and carried another provider's URL, and
`provider_config` looked the key up *by the preset* and posted it to the *other* endpoint, which is
an HTTP 401 and nothing else.

No network and no keyring here: `keystore.lookup` is stubbed and every endpoint is a string.
"""
import unittest

from relay_core import session_protocol as S
from relay_core.presets import PRESETS
from relay_core.provider import ChatProvider, ProviderConfig

KEYS = {"glm-coding": "zai-coding-key", "kimi": "moonshot-key"}


class ConfigureProviderTests(unittest.TestCase):
    def setUp(self):
        self.looked_up = []
        self.real_lookup = S.keystore.lookup
        S.keystore.lookup = self._lookup
        self.addCleanup(setattr, S.keystore, "lookup", self.real_lookup)

    def _lookup(self, preset_id):
        self.looked_up.append(preset_id)
        return KEYS.get(preset_id, "")

    # ----- a named preset is an endpoint (protocol 1) ---------------------------------------
    def test_a_preset_alone_resolves_that_preset_s_endpoint_model_and_extra(self):
        config = S.provider_config({"preset": "glm-coding", "use_stored_key": True})
        preset = PRESETS["glm-coding"]
        self.assertEqual((config.base_url, config.model, config.extra),
                         (preset.base_url, preset.model, dict(preset.extra)))
        self.assertEqual(config.api_key, KEYS["glm-coding"])
        self.assertEqual(self.looked_up, ["glm-coding"])

    def test_the_board_s_configure_cannot_pair_a_key_with_a_foreign_endpoint(self):
        """What the Board sends now: the preset, and no endpoint of its own.

        Whatever stale `provider/base` sits in the settings, the key that is looked up and the URL
        it is sent to belong to the same preset.
        """
        for preset_id in PRESETS:
            board = {"type": "configure", "agent_role": "switchboard", "use_stored_key": True,
                     "api_key": "", "preset": preset_id, "max_tokens": 32768}
            if PRESETS[preset_id].hosted:
                continue                         # Relay Free has no key to pair: see the tests below
            if not KEYS.get(preset_id):
                with self.assertRaises(ValueError):
                    S.provider_config(board)
                continue
            config = S.provider_config(board)
            self.assertEqual(config.base_url, PRESETS[preset_id].base_url)
            self.assertEqual(config.api_key, KEYS[preset_id])

    # ----- Relay Free needs no key (protocol 13.9) -------------------------------------------
    def test_relay_free_configures_with_no_key_stored_and_looks_none_up(self):
        config = S.provider_config({"preset": "relay-free", "use_stored_key": True})
        self.assertTrue(config.hosted)
        self.assertEqual(config.api_key, "")
        self.assertEqual((config.base_url, config.model), (PRESETS["relay-free"].base_url, "relay-main"))
        self.assertEqual(self.looked_up, [])
        # The other way round too: a key the request carries is dropped, the transport takes a token.
        self.assertEqual(S.provider_config({"preset": "relay-free", "api_key": "typed"}).api_key, "")

    def test_relay_free_pointed_at_another_endpoint_is_a_custom_provider_that_needs_a_key(self):
        # The hosted transport authenticates with Relay's gateway only; anything else named under
        # the relay-free preset is a custom endpoint and keeps the rule every other one has.
        with self.assertRaises(ValueError):
            S.provider_config({"preset": "relay-free", "use_stored_key": True,
                               "base_url": "https://proxy.example/v1"})

    def test_the_old_board_configure_is_what_produced_the_401(self):
        """The shape the board used to send, kept as the description of the bug.

        `provider/preset` said Z.AI Coding Plan while `provider/base` still said Moonshot, so the
        Z.AI key went to `api.moonshot.ai` — authenticated nowhere, rejected with 401.
        """
        stale = {"preset": "glm-coding", "use_stored_key": True,
                 "base_url": PRESETS["kimi"].base_url, "model": PRESETS["kimi"].model}
        config = S.provider_config(stale)
        self.assertEqual(config.api_key, KEYS["glm-coding"])
        self.assertNotEqual(config.base_url, PRESETS["glm-coding"].base_url)

    def test_an_explicit_endpoint_still_wins_so_the_provider_dialog_keeps_working(self):
        config = S.provider_config({"preset": "glm-coding", "use_stored_key": True,
                                    "base_url": "https://proxy.example/v1", "model": "glm-5.3",
                                    "extra": {}})
        self.assertEqual((config.base_url, config.model, config.extra),
                         ("https://proxy.example/v1", "glm-5.3", {}))
        self.assertEqual(config.api_key, KEYS["glm-coding"])

    def test_a_preset_with_a_model_override_keeps_the_preset_s_base_url(self):
        config = S.provider_config({"preset": "glm-coding", "model": "glm-5.3-flash",
                                    "use_stored_key": True})
        self.assertEqual((config.base_url, config.model),
                         (PRESETS["glm-coding"].base_url, "glm-5.3-flash"))

    def test_a_custom_endpoint_without_a_preset_is_unchanged(self):
        config = S.provider_config({"use_stored_key": True, "base_url": PRESETS["kimi"].base_url,
                                    "model": PRESETS["kimi"].model})
        self.assertEqual(config.api_key, KEYS["kimi"])
        self.assertEqual(self.looked_up, ["kimi"])

    # ----- a missing key says which provider ------------------------------------------------
    def test_a_missing_key_names_the_provider(self):
        with self.assertRaises(ValueError) as ctx:
            S.provider_config({"preset": "openai", "use_stored_key": True})
        # The row's label (lower-case since 2026-09-20) and its preset id, so the user can find it.
        self.assertIn(PRESETS["openai"].label, str(ctx.exception))
        self.assertIn("(openai)", str(ctx.exception))

    def test_a_missing_key_for_a_custom_endpoint_names_its_host(self):
        with self.assertRaises(ValueError) as ctx:
            S.provider_config({"use_stored_key": True, "base_url": "https://llm.example/v1",
                               "model": "m"})
        self.assertIn("llm.example", str(ctx.exception))


class HttpErrorTextTests(unittest.TestCase):
    """A 401 has to say which provider refused and that the answer is a key, not a retry."""

    def provider(self, base_url="https://api.z.ai/api/coding/paas/v4", model="glm-5.3"):
        return ChatProvider(ProviderConfig(base_url, model, "k", {}, 1024))

    def test_a_401_names_the_model_the_host_and_the_preset(self):
        text = self.provider().http_message(401)
        self.assertIn("401", text)
        self.assertIn("glm-5.3", text)
        self.assertIn("api.z.ai", text)
        self.assertIn(PRESETS["glm-coding"].label, text)
        self.assertIn("key was rejected", text)

    def test_a_403_reads_the_same_way(self):
        self.assertIn("key was rejected", self.provider().http_message(403))

    def test_another_status_keeps_the_general_advice_and_gains_the_endpoint(self):
        text = self.provider().http_message(500)
        self.assertIn("Check endpoint, model access, key, quota, and parameters.", text)
        self.assertIn("api.z.ai", text)
        self.assertNotIn("key was rejected", text)

    def test_an_unknown_endpoint_is_named_by_its_host_and_model_only(self):
        text = self.provider("https://llm.example/v1", "mystery-1").http_message(401)
        self.assertIn("llm.example", text)
        self.assertIn("mystery-1", text)
        # No preset matches that URL, so no preset label is invented for it.
        self.assertNotIn("·", text)

    def test_the_message_never_carries_the_key(self):
        provider = ChatProvider(ProviderConfig("https://llm.example/v1", "m", "SECRET-KEY-VALUE",
                                               {}, 1024))
        for code in (401, 403, 429, 500):
            self.assertNotIn("SECRET-KEY-VALUE", provider.http_message(code))


if __name__ == "__main__":
    unittest.main()
