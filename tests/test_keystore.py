import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

from relay_core import keystore, keytest
from relay_core.presets import PRESETS, match_preset

ROOT = Path(__file__).resolve().parents[1]

FAKE_SECRET_TOOL = r'''#!/usr/bin/env python3
import json, os, sys
db = os.environ["FAKE_KEYRING"]
data = json.load(open(db)) if os.path.exists(db) else {}
argv = sys.argv[1:]
if argv[0] == "store":
    if "--label" in argv:
        i = argv.index("--label"); del argv[i:i+2]
    attrs = argv[1:]
    data["|".join(attrs)] = sys.stdin.read()
    json.dump(data, open(db, "w"))
    open(db + ".argv", "a").write(" ".join(sys.argv) + "\n")
elif argv[0] == "lookup":
    value = data.get("|".join(argv[1:]))
    if value is None: sys.exit(1)
    sys.stdout.write(value)
'''

WARP_SETTINGS = textwrap.dedent('''
    ai_label = "a { brace, in a string }"  # comment with {
    [agents.custom_endpoints.legacy-or]
    base_url = "https://openrouter.ai/api/v1"
    models = [
      {
        alias = "deepseek-v4.1-flash",
        config_key = "x",
        name = "deepseek/deepseek-v4.1-flash",
      },
    ]
    name = "OpenRouter"
    schema = "openai_chat_completions"

    [agents.custom_endpoints.legacy-glm]
    base_url = "https://api.z.ai/api/coding/paas/v4"
    models = [{ config_key = "y", name = "glm-5.3" }]
    name = "GLM 5.3"
    schema = "openai_chat_completions"

    [agents.custom_endpoints.legacy-kimi]
    base_url = "https://api.moonshot.ai/v1 "
    models = [{ config_key = "z", name = "kimi-k3" }]
    name = "Kimi"
    schema = "openai_chat_completions"

    [agents.custom_endpoints.legacy-other]
    base_url = "https://example.com/v1"
    models = [{ config_key = "w", name = "m" }]
    name = "Other"
    schema = "openai_chat_completions"
''')


class KeystoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        bindir = self.root / "bin"; bindir.mkdir()
        tool = bindir / "secret-tool"; tool.write_text(FAKE_SECRET_TOOL); tool.chmod(0o755)
        self.db = self.root / "keyring.json"
        env = {k: v for k, v in os.environ.items() if not k.startswith("RELAY_")}
        env.update(PATH=f"{bindir}:{os.environ['PATH']}", FAKE_KEYRING=str(self.db),
                   XDG_CONFIG_HOME=str(self.root / "config"))
        patcher = mock.patch.dict(os.environ, env, clear=True)
        patcher.start(); self.addCleanup(patcher.stop)

    def tearDown(self):
        self.temp.cleanup()

    def seed_warp(self, keys):
        data = {f"service|{keystore.WARP_SERVICE}|key|AiCustomEndpointKeys": json.dumps(keys)}
        self.db.write_text(json.dumps(data))
        settings = self.root / "settings.toml"; settings.write_text(WARP_SETTINGS)
        return settings

    def test_preset_matching_tolerates_whitespace_and_slash(self):
        self.assertEqual(match_preset("https://api.moonshot.ai/v1 ", "kimi-k3").id, "kimi")
        self.assertEqual(match_preset("https://openrouter.ai/api/v1/").id, "openrouter")
        self.assertEqual(match_preset("https://api.z.ai/api/coding/paas/v4", "glm-5.3").id, "glm-coding")
        self.assertIsNone(match_preset("https://example.com/v1"))
        for preset in PRESETS.values():
            self.assertEqual(preset.base_url, preset.base_url.strip())

    def test_store_lookup_passes_secret_on_stdin(self):
        keystore.store("kimi", "sk-test-secret")
        self.assertEqual(keystore.lookup("kimi"), "sk-test-secret")
        self.assertNotIn("sk-test-secret", Path(str(self.db) + ".argv").read_text())
        self.assertEqual(keystore.lookup("glm"), "")

    def test_environment_overrides_keyring(self):
        keystore.store("openrouter", "from-keyring")
        with mock.patch.dict(os.environ, {"RELAY_OPENROUTER_API_KEY": "from-env"}):
            self.assertEqual(keystore.lookup("openrouter"), "from-env")
        self.assertEqual(keystore.env_name("glm-coding"), "RELAY_GLM_CODING_API_KEY")
        self.assertEqual(keystore.env_name("kimi-code"), "RELAY_KIMI_CODE_API_KEY")
        self.assertEqual(match_preset("https://api.kimi.ai/coding/v1", "kimi-for-coding").id, "kimi-code")

    def test_rejects_bad_ids_and_keys(self):
        for bad in ["", "../x", "Kimi", "a b"]:
            with self.assertRaises(ValueError): keystore.lookup(bad)
        with self.assertRaises(ValueError): keystore.store("kimi", "has space")

    def test_warp_toml11_inline_tables(self):
        settings = self.root / "settings.toml"; settings.write_text(WARP_SETTINGS)
        endpoints = keystore.read_warp_endpoints(settings)
        self.assertEqual(endpoints["legacy-or"]["models"][0]["name"], "deepseek/deepseek-v4.1-flash")
        self.assertEqual(len(endpoints), 4)

    def test_warp_default_preset(self):
        settings = self.root / "settings.toml"
        settings.write_text('[agents.execution_profiles.default]\nbase_model = "y"\n' + WARP_SETTINGS)
        self.assertEqual(keystore.warp_default_preset(settings), "glm-coding")
        settings.write_text(WARP_SETTINGS)
        self.assertIsNone(keystore.warp_default_preset(settings))
        self.assertIsNone(keystore.warp_default_preset(self.root / "missing.toml"))

    def test_worker_custom_preset_matches_stored_key_by_url(self):
        keystore.store("openrouter", "OR_SECRET_NEVER_ECHO")
        messages = [{"type": "configure", "preset": "custom", "use_stored_key": True,
                     "base_url": "https://openrouter.ai/api/v1/", "model": "deepseek/deepseek-v4.1-flash",
                     "workspace": str(ROOT)},
                    {"type": "configure", "preset": "custom", "use_stored_key": True,
                     "base_url": "https://example.com/v1", "model": "m", "workspace": str(ROOT)},
                    {"type": "shutdown"}]
        proc = subprocess.run([sys.executable, "-S", str(ROOT / "backend/worker.py")],
                              input="".join(json.dumps(m) + "\n" for m in messages),
                              text=True, capture_output=True, timeout=10, cwd=ROOT)
        results = [json.loads(line) for line in proc.stdout.splitlines()]
        self.assertEqual(results[1]["event"], "configured")
        self.assertEqual(results[2]["event"], "error")
        self.assertNotIn("OR_SECRET_NEVER_ECHO", proc.stdout + proc.stderr)

    def test_import_from_warp(self):
        settings = self.seed_warp({"legacy-or": "sk-or-1", "legacy-glm": "glm-2", "legacy-kimi": "sk-kimi-3",
                                   "legacy-other": "other-4"})
        imported, skipped = keystore.import_from_warp(settings)
        self.assertEqual({i.preset for i in imported}, {"openrouter", "glm-coding", "kimi"})
        self.assertEqual(keystore.lookup("openrouter"), "sk-or-1")
        self.assertEqual(keystore.lookup("glm-coding"), "glm-2")
        self.assertEqual(keystore.lookup("kimi"), "sk-kimi-3")
        self.assertEqual(len(skipped), 1)
        self.assertNotIn("sk-", json.dumps([i.to_dict() for i in imported] + skipped))

    def test_import_reports_missing_keys(self):
        settings = self.seed_warp({"legacy-or": "sk-or-1"})
        imported, skipped = keystore.import_from_warp(settings)
        self.assertEqual([i.preset for i in imported], ["openrouter"])
        self.assertTrue(any("no key" in s for s in skipped))

    def test_warp_import_preserves_an_existing_relay_key(self):
        settings = self.seed_warp({"legacy-or": "NEW_SECRET_NEVER_ECHO"})
        keystore.store("openrouter", "existing")
        imported, skipped = keystore.import_from_warp(settings)
        self.assertNotIn("openrouter", [item.preset for item in imported])
        self.assertEqual(keystore.lookup("openrouter"), "existing")
        self.assertTrue(any("already has a key" in item for item in skipped))

    def test_import_without_warp_keys_fails_cleanly(self):
        settings = self.root / "settings.toml"; settings.write_text(WARP_SETTINGS)
        with self.assertRaises(keystore.KeystoreError): keystore.import_from_warp(settings)

    def test_worker_uses_stored_key_without_echoing_it(self):
        keystore.store("kimi", "STORED_SECRET_NEVER_ECHO")
        messages = [{"type": "presets", "id": "p"},
                    {"type": "configure", "preset": "kimi", "use_stored_key": True,
                     "base_url": "https://api.moonshot.ai/v1", "model": "kimi-k3", "workspace": str(ROOT)},
                    {"type": "configure", "preset": "glm", "use_stored_key": True,
                     "base_url": "https://api.z.ai/api/paas/v4", "model": "glm-5.3", "workspace": str(ROOT)},
                    {"type": "shutdown"}]
        proc = subprocess.run([sys.executable, "-S", str(ROOT / "backend/worker.py")],
                              input="".join(json.dumps(m) + "\n" for m in messages),
                              text=True, capture_output=True, timeout=10, cwd=ROOT)
        results = [json.loads(line) for line in proc.stdout.splitlines()]
        presets = {p["id"]: p for p in results[1]["presets"]}
        self.assertTrue(presets["kimi"]["has_stored_key"])
        self.assertFalse(presets["glm"]["has_stored_key"])
        self.assertEqual(results[2]["event"], "configured")
        self.assertEqual(results[3]["event"], "error")
        self.assertNotIn("STORED_SECRET_NEVER_ECHO", proc.stdout + proc.stderr)


# ----- keys modal: remove, sources, imports and the Test button (protocol 13.8) -----------------
class KeysModalTests(unittest.TestCase):
    """Nothing here reaches a real keyring or the network: secret-tool is faked and the Test button's
    provider is a stub."""

    def test_remove_clears_the_entry_and_is_a_no_op_without_a_keyring(self):
        with mock.patch.dict(os.environ, {"RELAY_KEYRING": "off"}, clear=False):
            self.assertFalse(keystore.remove("kimi"))
        calls = []

        def run(args, stdin=None):
            calls.append(args)
            return subprocess.CompletedProcess(args, 0, stdout="", stderr="")

        with mock.patch.dict(os.environ, {"RELAY_KEYRING": "on"}, clear=False), \
                mock.patch.object(keystore, "_run", run), \
                mock.patch.object(keystore.shutil, "which", lambda name: "/usr/bin/secret-tool"):
            self.assertTrue(keystore.remove("kimi"))
        self.assertEqual(calls[0], ["clear", "service", keystore.SERVICE, "provider", "kimi"])

    def test_remove_rejects_a_bad_identifier(self):
        with self.assertRaises(ValueError):
            keystore.remove("../etc/passwd")

    def test_key_source_says_env_keyring_or_nothing(self):
        with mock.patch.dict(os.environ, {"RELAY_KIMI_API_KEY": "from-env", "RELAY_KEYRING": "off"}, clear=False):
            self.assertEqual(keystore.key_source("kimi"), "env")
            self.assertEqual(keystore.key_source("glm"), "")
            sources = keystore.sources()
            self.assertEqual(sources["kimi"], "env")
            self.assertEqual(set(sources), set(PRESETS))
        with mock.patch.dict(os.environ, {"RELAY_KEYRING": "on"}, clear=False), \
                mock.patch.object(keystore, "lookup", lambda pid: "stored" if pid == "glm" else ""):
            self.assertEqual(keystore.key_source("glm"), "keyring")

    def test_import_from_claude_code_and_codex_takes_api_keys_only(self):
        with tempfile.TemporaryDirectory() as home:
            root = Path(home)
            (root / ".claude").mkdir()
            (root / ".codex").mkdir()
            (root / ".claude/settings.json").write_text(json.dumps({"env": {"ANTHROPIC_API_KEY": "sk-ant-test"}}))
            # An OAuth login, not an API key: it must be skipped.
            (root / ".codex/auth.json").write_text(json.dumps({"auth_mode": "chatgpt", "OPENAI_API_KEY": None,
                                                               "tokens": {"access_token": "oauth-token"}}))
            stored = {}
            with mock.patch.object(keystore, "store", lambda pid, key: stored.__setitem__(pid, key)):
                imported, skipped = keystore.import_from_agent_tools(root)
            self.assertEqual([item.preset for item in imported], ["anthropic"])
            self.assertEqual(stored, {"anthropic": "sk-ant-test"})
            self.assertEqual(len(skipped), 1)
            self.assertIn("Codex", skipped[0])
            self.assertIn("OAuth", skipped[0])

    def test_import_reports_missing_files_without_failing(self):
        with tempfile.TemporaryDirectory() as home:
            imported, skipped = keystore.import_from_agent_tools(Path(home))
            self.assertEqual(imported, [])
            self.assertEqual(len(skipped), 2)

    def test_opencode_imports_only_matching_plain_api_keys_without_overwriting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "auth.json"
            path.write_text(json.dumps({
                "openai": {"type": "api", "key": "OPENAI_SECRET_NEVER_ECHO"},
                "anthropic": {"type": "oauth", "access": "OAUTH_SECRET_NEVER_ECHO"},
                "openrouter": {"type": "api", "key": "OTHER_SECRET_NEVER_ECHO"},
                "unrecognized": {"type": "api", "key": "UNKNOWN_SECRET_NEVER_ECHO"},
            }))
            stored = {}
            with mock.patch.object(keystore, "lookup", lambda pid: "existing" if pid == "openrouter" else ""), \
                    mock.patch.object(keystore, "store", lambda pid, key: stored.__setitem__(pid, key)):
                imported, skipped = keystore.import_from_opencode(path)
            self.assertEqual([item.preset for item in imported], ["openai"])
            self.assertEqual(stored, {"openai": "OPENAI_SECRET_NEVER_ECHO"})
            self.assertEqual(len(skipped), 3)
            self.assertNotIn("SECRET_NEVER_ECHO", json.dumps([item.to_dict() for item in imported] + skipped))

    def test_opencode_missing_or_malformed_auth_fails_without_exposing_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "auth.json"
            with self.assertRaises(keystore.KeystoreError):
                keystore.import_from_opencode(path)
            path.write_text("BROKEN_SECRET_NEVER_ECHO{")
            with self.assertRaises(keystore.KeystoreError) as error:
                keystore.import_from_opencode(path)
            self.assertNotIn("BROKEN_SECRET_NEVER_ECHO", str(error.exception))

    def test_test_button_reports_success_without_the_key(self):
        events = []
        provider = mock.Mock()
        provider.complete.return_value = {"role": "assistant", "content": "ok"}
        thread = keytest.run("kimi", events.append, "t1", lookup=lambda pid: "SECRET_NEVER_ECHO",
                             factory=lambda preset_id, key: provider)
        thread.join(5)
        self.assertEqual(len(events), 1)
        self.assertTrue(events[0]["ok"])
        self.assertEqual(events[0]["preset"], "kimi")
        self.assertEqual(events[0]["model"], PRESETS["kimi"].model)
        self.assertNotIn("SECRET_NEVER_ECHO", json.dumps(events[0]))
        # One tiny no-tools call, nothing else.
        messages, tools = provider.complete.call_args[0][0], provider.complete.call_args[0][1]
        self.assertEqual(len(messages), 2)
        self.assertEqual(tools, [])

    def test_a_truncated_answer_still_proves_the_key(self):
        # Reasoning models spend the budget on thinking; that is not a key problem. Matched by type,
        # so rewording the sentence the user reads cannot turn every such test into a failure.
        from relay_core.provider import ProviderTruncated
        provider = mock.Mock()
        provider.complete.side_effect = ProviderTruncated("length", keytest.MAX_TOKENS)
        result = keytest.check("openrouter", "k", lambda preset_id, key: provider)
        self.assertTrue(result["ok"])
        self.assertTrue(result["truncated"])
        self.assertNotIn("error", result)

    def test_a_stalled_test_says_so_instead_of_naming_the_exception(self):
        from relay_core.provider import ProviderStalled
        provider = mock.Mock()
        provider.complete.side_effect = ProviderStalled(30.0)
        result = keytest.check("openrouter", "k", lambda preset_id, key: provider)
        self.assertFalse(result["ok"])
        self.assertIn("30 s", result["error"])

    def test_the_test_call_asks_for_the_least_thinking_the_provider_allows(self):
        seen = {}

        def factory(preset_id, key):
            from relay_core.provider import ChatProvider, ProviderConfig
            from relay_core.presets import PRESETS as P
            made = keytest._provider(preset_id, key)
            seen[preset_id] = made.config.extra
            provider = mock.Mock()
            provider.complete.return_value = {"role": "assistant", "content": "ok"}
            return provider

        keytest.check("glm-coding", "k", factory)
        keytest.check("anthropic", "k", factory)
        self.assertEqual(seen["glm-coding"]["reasoning_effort"], "low")
        # Anthropic's compat layer has no effort knob, so nothing is added.
        self.assertEqual(seen["anthropic"], {})

    def test_test_button_reports_a_provider_failure_without_the_body(self):
        from relay_core.provider import ProviderError
        events = []
        provider = mock.Mock()
        provider.complete.side_effect = ProviderError("Provider HTTP 401. Check endpoint, model access, key.")
        thread = keytest.run("glm", events.append, None, lookup=lambda pid: "k",
                             factory=lambda preset_id, key: provider)
        thread.join(5)
        self.assertFalse(events[0]["ok"])
        self.assertIn("401", events[0]["error"])

    def test_test_button_without_a_stored_key_answers_at_once(self):
        events = []
        self.assertIsNone(keytest.run("openai", events.append, "t2", lookup=lambda pid: ""))
        self.assertEqual(events[0]["ok"], False)
        self.assertIn("No key", events[0]["error"])

    def test_test_button_refuses_an_unknown_provider(self):
        with self.assertRaises(ValueError):
            keytest.run("nope", lambda event: None, None, lookup=lambda pid: "k")

    def test_worker_presets_event_carries_sources_tiers_and_actions(self):
        messages = [{"type": "presets", "id": "p"}, {"type": "shutdown"}]
        env = dict(os.environ, RELAY_KEYRING="off", RELAY_OPENROUTER_API_KEY="or-key",
                   PYTHONPATH=str(ROOT / "backend"))
        proc = subprocess.run([sys.executable, "-S", str(ROOT / "backend/worker.py")],
                              input="".join(json.dumps(m) + "\n" for m in messages),
                              text=True, capture_output=True, timeout=20, cwd=ROOT, env=env)
        event = [json.loads(line) for line in proc.stdout.splitlines()][1]
        by_id = {p["id"]: p for p in event["presets"]}
        self.assertEqual(by_id["openrouter"]["key_source"], "env")
        self.assertTrue(by_id["openrouter"]["has_stored_key"])
        self.assertEqual(by_id["anthropic"]["key_source"], "")
        self.assertEqual(by_id["minimax"]["group"], "subscription")
        self.assertTrue(by_id["minimax"]["key_url"].startswith("https://"))
        self.assertIn("providers", event["tier_defaults"])
        self.assertTrue(any(a["role"] == "route_assist" for a in event["role_actions"]))
        self.assertNotIn("or-key", proc.stdout)


if __name__ == "__main__":
    unittest.main()
