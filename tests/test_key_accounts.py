# SPDX-License-Identifier: AGPL-3.0-or-later
"""A second Z.AI Coding Plan or Kimi Code subscription, side by side (card #YC0T). Offline."""
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import key_accounts, keystore, keytest, provider_limits
from relay_core.agent import Agent
from relay_core.presets import PRESETS, resolve_preset
from relay_core.provider import ProviderConfig, ProviderQuotaExhausted
from relay_core.roles import RoleResolver, _preset, _usage_weight
from relay_core.session_protocol import provider_config


class Registry(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        env = mock.patch.dict(os.environ, {"RELAY_KEY_ACCOUNTS": str(Path(self.temp.name) / "k.json"),
                                           "RELAY_KEYRING": "off"})
        env.start()
        self.addCleanup(env.stop)
        self.stored = {}
        store = mock.patch("relay_core.keystore.store", side_effect=lambda i, k: self.stored.__setitem__(i, k))
        store.start()
        self.addCleanup(store.stop)
        provider_limits._last.clear()
        self.addCleanup(provider_limits._last.clear)


class RegistryTests(Registry):
    def test_save_stores_the_key_under_the_accounts_own_id(self):
        entry = key_accounts.save({"preset": "glm-coding", "label": "ETH Zürich", "api_key": "k2"})
        self.assertEqual((entry.preset_id, entry.label), ("glm-coding:eth-z-rich", "ETH Zürich"))
        self.assertEqual(self.stored, {"glm-coding:eth-z-rich": "k2"})
        self.assertEqual([a.preset_id for a in key_accounts.accounts()], ["glm-coding:eth-z-rich"])

    def test_only_the_two_plans_take_accounts_and_names_do_not_collide(self):
        with self.assertRaises(ValueError):
            key_accounts.save({"preset": "openai", "label": "x"})
        a = key_accounts.save({"preset": "kimi-code", "label": "work"})
        b = key_accounts.save({"preset": "kimi-code", "label": "Work"})
        self.assertEqual((a.preset_id, b.preset_id), ("kimi-code:work", "kimi-code:work-2"))

    def test_delete_forgets_the_account_and_its_key(self):
        key_accounts.save({"preset": "glm-coding", "label": "ethz"})
        with mock.patch("relay_core.keystore.remove") as remove:
            self.assertTrue(key_accounts.delete("glm-coding:ethz"))
        remove.assert_called_once_with("glm-coding:ethz")
        self.assertEqual(key_accounts.accounts(), [])
        self.assertFalse(key_accounts.delete("glm-coding:ethz"))

    def test_the_keyring_accepts_an_account_of_a_builtin_and_nothing_else(self):
        keystore._check_id("glm-coding:ethz")
        for bad in ("nope:ethz", "glm-coding:Bad", "glm-coding:a:b"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                keystore._check_id(bad)
        self.assertEqual(keystore.env_name("glm-coding:ethz"), "RELAY_GLM_CODING_ETHZ_API_KEY")


class ResolutionTests(Registry):
    def setUp(self):
        super().setUp()
        key_accounts.save({"preset": "glm-coding", "label": "ethz"})

    def test_every_layer_resolves_the_account_to_its_plan_under_its_own_id(self):
        base = PRESETS["glm-coding"]
        for found in (_preset("glm-coding:ethz"), resolve_preset("glm-coding:ethz", base.base_url, base.model),
                      keytest._preset("glm-coding:ethz")):
            self.assertEqual((found.id, found.base_url, found.model), ("glm-coding:ethz", base.base_url, base.model))
            self.assertIn("ethz", found.label)
        self.assertIsNone(_preset("glm-coding:gone"))

    def test_configure_uses_the_accounts_own_key(self):
        with mock.patch.dict(os.environ, {"RELAY_GLM_CODING_ETHZ_API_KEY": "second", "RELAY_GLM_CODING_API_KEY": "first"}):
            config = provider_config({"preset": "glm-coding:ethz", "use_stored_key": True})
        self.assertEqual((config.api_key, config.key_source), ("second", "glm-coding:ethz"))
        self.assertEqual(config.base_url, PRESETS["glm-coding"].base_url)

    def test_each_account_is_polled_and_weighted_on_its_own_report(self):
        now = int(time.time())
        seen = []
        def fetch(preset, key):
            seen.append((preset, key))
            used = 100.0 if preset == "glm-coding" else 10.0
            return [{"kind": "5h", "used_percent": used, "resets_at": now + 3600}]
        provider_limits.poll_once(key_lookup=lambda p: {"glm-coding": "a", "glm-coding:ethz": "b"}.get(p, ""),
                                  emit=lambda e: None, fetcher=fetch, clock=lambda: now)
        self.assertIn(("glm-coding:ethz", "b"), seen)
        self.assertEqual(_usage_weight("glm-coding", now), 0.0)
        self.assertGreater(_usage_weight("glm-coding:ethz", now), 80.0)
        self.assertEqual(provider_limits.fetch.__name__, "fetch")   # the plan's own endpoint parser

    def test_a_spent_subscription_hands_the_turn_to_its_sibling(self):
        keys = {"glm-coding": "first", "glm-coding:ethz": "second"}
        calls = []

        class Stub:
            def __init__(self, config):
                self.config = config

            def complete(self, messages, tools, emit, cancel):
                calls.append(self.config.api_key)
                if self.config.api_key == "first":
                    raise ProviderQuotaExhausted("5-hour usage limit reached", 60)
                return {"role": "assistant", "content": "from ethz"}

        base = PRESETS["glm-coding"]
        config = ProviderConfig(base.base_url, base.model, "first")
        roles = RoleResolver(config, "glm-coding", {}, key_lookup=lambda p: keys.get(p, ""),
                             tiers={"main": [{"preset": "glm-coding", "model": base.model},
                                             {"preset": "glm-coding:ethz", "model": base.model}]})
        events = []
        with tempfile.TemporaryDirectory() as work, \
             mock.patch("relay_core.agent._provider_for", side_effect=lambda c, stall: Stub(c)), \
             mock.patch("relay_core.logs.routing_draw"):
            agent = Agent(config, work, events.append, preset_id="glm-coding", roles=roles,
                          provider=Stub(config))
            agent.ask("hello")
        self.assertEqual(events[-1]["event"], "done")
        self.assertEqual(calls, ["first", "second"])
        moves = [e for e in events if e.get("reason") == "quota_exhausted"]
        self.assertEqual(moves[0]["to_preset"], "glm-coding:ethz")


if __name__ == "__main__":
    unittest.main()
