import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

from relay_core import keystore
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
