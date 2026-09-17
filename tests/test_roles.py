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
        self.assertEqual(validate_roles({"fast": None, "vision": {}, "subagent": {"inherit": True}}), {})

    def test_accepts_preset_custom_and_effort(self):
        table = validate_roles({"subagent": {"preset": "glm", "effort": "low"},
                                "fast": {"base_url": "https://example.invalid/v1", "model": "m", "extra": {"a": 1}},
                                "switchboard": {"preset": "openrouter", "model": "google/gemini-3.8-flash"}})
        self.assertEqual(table["subagent"], {"preset": "glm", "effort": "low"})
        self.assertEqual(table["fast"]["model"], "m")
        self.assertEqual(table["switchboard"]["model"], "google/gemini-3.8-flash")

    def test_rejects_bad_tables(self):
        for bad in ({"nope": {"preset": "glm"}}, {"main": {"preset": "glm"}}, {"fast": {"preset": "nope"}},
                    {"fast": {"model": "m"}}, {"fast": {"effort": "turbo"}}, {"fast": {"extra": 3}},
                    {"fast": {"zzz": 1}}, [1, 2]):
            with self.assertRaises(ValueError):
                validate_roles(bad)


# ----- defaults -----------------------------------------------------------------------------
class DefaultTests(unittest.TestCase):
    def test_fast_agent_default_per_main_provider(self):
        for preset, model in (("glm", "glm-5.3-flash"), ("glm-coding", "glm-5.3-flash"),
                              ("openrouter", "deepseek/deepseek-v4.1-flash"),
                              ("kimi", "kimi-k2.7-code-highspeed"), ("kimi-code", "kimi-for-coding-highspeed")):
            fast = resolver(preset, keys=(preset,)).resolve("fast")
            self.assertEqual(fast.model, model, preset)
            self.assertEqual(fast.source, "default")
            self.assertFalse(fast.is_main)

    def test_glm_fast_default_turns_thinking_off(self):
        fast = resolver("glm-coding", keys=("glm-coding",)).resolve("fast")
        self.assertEqual(fast.config.extra, {"thinking": {"type": "disabled"}})

    def test_fast_reuses_the_main_key_without_a_keyring_lookup(self):
        made = resolver("kimi", keys=())        # nothing stored: only the in-memory main key exists
        self.assertEqual(made.resolve("fast").model, "kimi-k2.7-code-highspeed")
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

    def test_terminal_use_subagent_and_switchboard_default_to_main(self):
        made = resolver("kimi")
        for role in ("terminal_use", "subagent", "switchboard"):
            self.assertTrue(made.resolve(role).is_main, role)
            self.assertEqual(made.resolve(role).source, "main")


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
        made = resolver("kimi", {"fast": {"base_url": "https://openrouter.ai/api/v1/",
                                          "model": "google/gemini-3.8-flash"}}, keys=("kimi", "openrouter"))
        fast = made.resolve("fast")
        self.assertEqual((fast.preset_id, fast.model, fast.config.api_key),
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
        self.assertEqual(summary["fast"]["source"], "default")
        self.assertNotIn("glm-coding-key", json.dumps(summary))

    def test_rebase_and_set_roles_recompute(self):
        made = resolver("kimi", keys=("kimi", "glm"))
        self.assertEqual(made.resolve("fast").model, "kimi-k2.7-code-highspeed")
        made.rebase(main_config("glm", "glm-key"), "glm", "high")
        self.assertEqual(made.resolve("fast").model, "glm-5.3-flash")
        made.set_roles(validate_roles({"fast": {"preset": "glm", "model": "glm-5-turbo"}}))
        self.assertEqual(made.resolve("fast").model, "glm-5-turbo")


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
        config, preset = factory.resolve("fast", [])
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
        self.assertEqual(agent.side_provider(cheap=True, role="fast").config.model, "kimi-k2.7-code-highspeed")
        chores = agent.side_provider(cheap=True, role="chores", max_tokens=64)
        self.assertEqual((chores.config.model, chores.config.max_tokens), ("glm-5.3-flash", 64))
        self.assertEqual(agent.role_model("chores"), "glm-5.3-flash")

    def test_side_provider_without_roles_keeps_the_main_model(self):
        config = ProviderConfig("https://example.invalid/v1", "house", "k", {}, 8192)
        made = RoleResolver(config, None, {}, key_lookup=lambda pid: "")
        agent = self.agent(made)
        self.assertEqual(agent.side_provider(cheap=True, role="fast").config.model, "house")
        self.assertEqual(agent.role_model("fast"), "house")


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

    def test_configure_reports_roles_and_a_fast_pane(self):
        events, proc = run_worker(self.messages({"agent_role": "fast"}),
                                  {"RELAY_KIMI_API_KEY": "KIMI_SECRET_NEVER_ECHO"})
        configured = next(e for e in events if e["event"] == "configured")
        self.assertEqual(configured["agent_role"], "fast")
        self.assertEqual(configured["model"], "kimi-k2.7-code-highspeed")
        self.assertEqual(configured["roles"]["subagent"]["source"], "main")
        self.assertEqual(configured["roles"]["fast"]["model"], "kimi-k2.7-code-highspeed")
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
                                 "workspace": str(ROOT), "agent_role": "fast",
                                 "roles": {"fast": {"preset": "openrouter"}}},
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
                                 "workspace": str(ROOT), "roles": {"fast": {"preset": "nope"}}},
                                {"type": "shutdown"}], {"RELAY_KIMI_API_KEY": "k"})
        self.assertEqual(events[1]["event"], "error")
        self.assertIn("preset", events[1]["text"])


if __name__ == "__main__":
    unittest.main()
