# SPDX-License-Identifier: AGPL-3.0-or-later
"""Custom providers: a named OpenAI-compatible endpoint with a key and model ids is a preset row
(backend/relay_core/customproviders.py, protocol section 28.6; owner, 2026-09-20).

The keyring is a dict here: nothing reaches secret-tool, and the tests can prove where a key went
and that it never appears in an event.
"""
import json
import os
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import unittest
from pathlib import Path
from unittest import mock

from relay_core import customproviders as C
from relay_core import keystore, keytest, presets as P, roles, session_protocol as S
from relay_core.provider import ChatProvider, ProviderConfig
from relay_core.roles import RoleResolver, validate_roles

REAL_FETCH = C.fetch_models          # the tests below patch C.fetch_models; this one tests it

PROXY = {"name": "My Proxy", "base_url": "https://llm.example.com/v1/",
         "models": ["big-model", "small-model"], "api_key": "sk-secret-123"}


class Case(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        env = {C.ENV_PATH: str(Path(self.dir.name) / "custom-providers.json"),
               "RELAY_LOCAL_MODELS": str(Path(self.dir.name) / "local-models.json"),
               "RELAY_KEYRING": "off"}
        patch = mock.patch.dict(os.environ, env)
        patch.start()
        self.addCleanup(patch.stop)
        self.keys = {}
        self.stored = []

        def store(pid, key):
            keystore._check_id(pid)
            if not key.strip() or any(c.isspace() for c in key.strip()):
                raise ValueError("API key must be non-empty text without whitespace.")
            self.stored.append(pid)
            self.keys[pid] = key

        def lookup(pid):
            keystore._check_id(pid)
            return os.environ.get(keystore.env_name(pid), "") or self.keys.get(pid, "")

        def remove(pid):
            keystore._check_id(pid)
            return self.keys.pop(pid, None) is not None

        for name, fake in (("store", store), ("lookup", lookup), ("remove", remove)):
            p = mock.patch.object(keystore, name, fake)
            p.start()
            self.addCleanup(p.stop)
        # No network: the /models probe answers nothing unless a test says otherwise.
        p = mock.patch.object(C, "fetch_models", lambda *a, **k: None)
        p.start()
        self.addCleanup(p.stop)
        self.addCleanup(C.set_listener, None)

    def save(self, spec=PROXY, request_id="r1"):
        events = []
        thread = C.handle({"type": "custom_provider_save", "id": request_id, "provider": dict(spec)}, events.append)
        if thread is not None:
            thread.join(5)
        return events


class SaveTests(Case):
    def test_a_save_stores_the_key_and_answers_the_preset_row(self):
        events = self.save()
        self.assertEqual([e["event"] for e in events], ["custom_provider_saved"])
        row = events[0]["provider"]
        self.assertEqual(events[0]["id"], "r1")
        self.assertEqual(row["id"], "custom:my-proxy")
        self.assertEqual((row["label"], row["provider"], row["plan"], row["group"]),
                         ("my proxy", "my proxy", "custom endpoint", "custom"))
        self.assertEqual((row["base_url"], row["model"]), ("https://llm.example.com/v1", "big-model"))
        self.assertEqual(row["model_ids"], ["big-model", "small-model"])
        self.assertEqual(row["models"], [
            {"id": "big-model", "name": "big-model", "label": "big-model", "tier": None, "efforts": [],
             "intelligence": None, "openrouter": None},
            {"id": "small-model", "name": "small-model", "label": "small-model", "tier": None, "efforts": [],
             "intelligence": None, "openrouter": None}])
        self.assertTrue(row["custom"])
        self.assertFalse(row["local"] or row["hosted"])
        self.assertEqual((row["has_stored_key"], row["key_source"], row["effort_style"]), (True, "keyring", "none"))
        self.assertEqual(self.keys, {"custom:my-proxy": "sk-secret-123"})
        self.assertNotIn("sk-secret-123", json.dumps(events))
        self.assertNotIn("sk-secret-123", Path(os.environ[C.ENV_PATH]).read_text())

    def test_the_row_is_in_the_presets_list_with_the_key_marked_stored(self):
        self.save()
        rows = C.rows()
        self.assertEqual([r["id"] for r in rows], ["custom:my-proxy"])
        self.assertTrue(rows[0]["has_stored_key"])
        # Every key a built-in row carries is on this one too: the GUI reads one shape.
        self.assertTrue(set(P.PRESETS["kimi"].to_dict()) <= set(rows[0]))
        self.assertFalse(P.PRESETS["kimi"].to_dict()["custom"])

    def test_an_env_key_counts_and_names_the_slug(self):
        self.assertEqual(keystore.env_name("custom:my-proxy"), "RELAY_CUSTOM_MY_PROXY_API_KEY")
        with mock.patch.dict(os.environ, {"RELAY_CUSTOM_MY_PROXY_API_KEY": "from-env"}):
            events = self.save({**PROXY, "api_key": None})
            self.assertEqual(events[0]["provider"]["key_source"], "env")
            self.assertEqual(S.provider_config({"preset": "custom:my-proxy", "use_stored_key": True}).api_key, "from-env")

    def test_the_same_slug_replaces_and_an_edit_without_a_key_keeps_it(self):
        self.save()
        self.save({"id": "custom:my-proxy", "name": "My Proxy", "base_url": "https://other.example.com/v1",
                   "models": "one-model, two-model"})
        self.assertEqual(list(C.catalog()), ["custom:my-proxy"])
        entry = C.find("custom:my-proxy")
        self.assertEqual((entry.base_url, entry.models), ("https://other.example.com/v1", ("one-model", "two-model")))
        self.assertEqual(self.keys, {"custom:my-proxy": "sk-secret-123"})
        self.assertEqual(self.stored, ["custom:my-proxy"])

    def test_the_id_may_be_given_or_come_from_the_name(self):
        self.assertEqual(C.make_id("Fireworks (EU) "), "custom:fireworks-eu")
        self.assertEqual(C.make_id("custom:already"), "custom:already")
        self.assertEqual(C.from_dict({**PROXY, "id": "proxy-two"}).id, "custom:proxy-two")

    def test_an_invalid_entry_is_a_sentence_and_nothing_is_written(self):
        for bad, words in (({**PROXY, "models": []}, "at least one model"),
                           ({**PROXY, "base_url": "http://llm.example.com/v1"}, "https"),
                           ({**PROXY, "base_url": "https://u:p@llm.example.com/v1"}, "credentials"),
                           ({**PROXY, "name": ""}, "needs a name"),
                           ({**PROXY, "effort_style": "glm"}, "effort_style"),
                           ({**PROXY, "models": ["has space"]}, "one word")):
            with self.assertRaises(ValueError) as caught:
                C.save_with_key(bad, bad.get("api_key"))
            self.assertIn(words, str(caught.exception))
        self.assertEqual(C.catalog(), {})
        self.assertEqual(self.keys, {})

    def test_a_key_the_keyring_will_not_take_saves_nothing(self):
        with self.assertRaises(ValueError):
            C.save_with_key(PROXY, "has whitespace")
        self.assertEqual(C.catalog(), {})

    def test_effort_style_defaults_to_none_and_openrouter_for_openrouter(self):
        self.assertEqual(C.from_dict(PROXY).effort_style, "none")
        via = C.from_dict({**PROXY, "base_url": "https://openrouter.ai/api/v1"})
        self.assertEqual(via.effort_style, "openrouter")
        self.assertEqual(via.as_preset().to_dict()["efforts"], P.effort_levels("openrouter"))
        kimi = C.from_dict({**PROXY, "effort_style": "kimi"})
        self.assertEqual(kimi.catalog_rows()[0]["efforts"], P.effort_levels("kimi"))
        extra, applied = P.apply_effort({}, kimi.effort_style, "high")
        self.assertEqual(applied, {"reasoning_effort": "high"})

    def test_a_probe_merges_the_served_models_and_tells_the_listener(self):
        asked, pushed = [], []
        with mock.patch.object(C, "fetch_models",
                               lambda url, key, **kw: (asked.append((url, key)), ["big-model", "served-one"])[1]):
            C.set_listener(lambda: pushed.append(True))
            self.save()
        self.assertEqual(asked, [("https://llm.example.com/v1", "sk-secret-123")])
        self.assertEqual(pushed, [True])
        rows = C.rows()[0]["models"]
        self.assertEqual([r["id"] for r in rows], ["big-model", "small-model", "served-one"])
        self.assertEqual(C.find("custom:my-proxy").served_models, ("big-model", "served-one"))
        # A re-save of the same endpoint keeps the list until the next probe lands.
        with mock.patch.object(C, "fetch_models", lambda *a, **k: None):
            self.save({**PROXY, "api_key": None})
        self.assertEqual(C.find("custom:my-proxy").served_models, ("big-model", "served-one"))

    def test_a_remote_endpoint_is_not_probed_without_a_key(self):
        asked = []
        with mock.patch.object(C, "fetch_models", lambda url, key, **kw: (asked.append(key), [])[1]):
            self.save({**PROXY, "api_key": None})
        self.assertEqual(asked, [])

    def test_the_list_request(self):
        self.save()
        events = []
        C.handle({"type": "custom_providers", "id": "l1"}, events.append)
        self.assertEqual(events[0]["event"], "custom_providers")
        self.assertEqual([r["id"] for r in events[0]["items"]], ["custom:my-proxy"])


class ExtraTests(Case):
    EXTRA = {"thinking": {"type": "enabled", "budget_tokens": 2048},
             "reasoning": {"effort": "low"}, "reasoning_effort": "high",
             "temperature": 0.7, "top_p": 0.9}

    def test_save_reload_probe_and_legacy_edit_preserve_extra_then_clear(self):
        row = self.save({**PROXY, "extra": self.EXTRA})[0]["provider"]
        self.assertEqual(row["extra"], self.EXTRA)
        C._cache = None
        self.assertEqual(C.find(row["id"]).extra, self.EXTRA)
        C._record_served(row["id"], ["discovered"])
        self.save({**PROXY, "api_key": None})  # older caller omits the new field
        entry = C.find(row["id"])
        self.assertEqual(entry.extra, self.EXTRA)
        self.assertEqual(entry.served_models, ("discovered",))
        self.assertEqual(entry.as_preset().extra, self.EXTRA)
        self.assertEqual(C.rows()[0]["extra"], self.EXTRA)
        self.save({**PROXY, "extra": {}})
        C._cache = None
        self.assertEqual(C.find(row["id"]).extra, {})
        self.assertEqual(json.loads(C.config_path().read_text())["providers"][0]["extra"], {})

    def test_legacy_file_without_extra_still_loads(self):
        C.config_path().write_text(json.dumps({"version": 1, "providers": [PROXY]}))
        self.assertEqual(C.find("custom:my-proxy").extra, {})

    def test_invalid_extra_never_changes_file_or_key(self):
        self.save({**PROXY, "extra": self.EXTRA})
        before = C.config_path().read_bytes()
        for extra in (None, [], "{}", "{broken", 1, {"model": "override"},
                      {"messages": []}, {"temperature": float("nan")}, {"thinking": object()}):
            with self.subTest(extra=extra):
                with self.assertRaisesRegex(ValueError, "Extra parameters"):
                    self.save({**PROXY, "extra": extra, "api_key": "replacement-key"})
                self.assertEqual(C.config_path().read_bytes(), before)
                self.assertEqual(self.keys["custom:my-proxy"], PROXY["api_key"])

    def test_supported_keys_match_transport_and_roles_keep_defaults(self):
        config = ProviderConfig("https://llm.example.com/v1", "big-model", "key", self.EXTRA)
        config.validate()
        self.save({**PROXY, "extra": self.EXTRA})
        got = S.provider_config({"preset": "custom:my-proxy", "model": "small-model", "use_stored_key": True})
        self.assertEqual(got.extra, self.EXTRA)
        self.assertEqual(S.provider_config({"preset": "custom:my-proxy", "use_stored_key": True,
                                            "extra": {"temperature": 0.1}}).extra, {"temperature": 0.1})
        resolver = RoleResolver(config, "custom:my-proxy",
                                validate_roles({"subagent": {"preset": "custom:my-proxy"}}),
                                key_lookup=keystore.lookup)
        self.assertEqual(resolver.resolve("subagent").config.extra, self.EXTRA)
        self.assertEqual(resolver.fallback_candidate({"preset": "custom:my-proxy"}, "main", set()).config.extra,
                         self.EXTRA)

    def test_real_http_after_save_probe_reload_and_model_switch(self):
        captures = []

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b'{"data":[{"id":"discovered"}]}')

            def do_POST(self):
                captures.append((self.path, self.headers.get("Authorization"),
                                 json.loads(self.rfile.read(int(self.headers["Content-Length"])))))
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.end_headers()
                self.wfile.write(b'data: {"choices":[{"delta":{"content":"ok"},"finish_reason":"stop"}]}\n\ndata: [DONE]\n\n')

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        spec = {"name": "Capture", "base_url": f"http://127.0.0.1:{server.server_port}/v1",
                "models": ["first", "second"], "extra": self.EXTRA}
        with mock.patch.object(C, "fetch_models", REAL_FETCH):
            self.save(spec)
        C._cache = None
        self.assertEqual(C.find("custom:capture").served_models, ("discovered",))
        for model in ("first", "second"):
            config = S.provider_config({"preset": "custom:capture", "model": model})
            result = ChatProvider(config).complete([{"role": "user", "content": "capture"}], [],
                                                   lambda event: None, threading.Event())
            self.assertEqual(result["content"], "ok")
            path, auth, body = captures[-1]
            self.assertEqual(path, "/v1/chat/completions")
            self.assertIsNone(auth)
            self.assertEqual(body["model"], model)
            self.assertEqual({k: body[k] for k in self.EXTRA}, self.EXTRA)
        self.save({**spec, "extra": {}})
        config = S.provider_config({"preset": "custom:capture"})
        ChatProvider(config).complete([{"role": "user", "content": "clear"}], [],
                                      lambda event: None, threading.Event())
        self.assertFalse(set(self.EXTRA) & set(captures[-1][2]))


class ConfigureTests(Case):
    def setUp(self):
        super().setUp()
        self.save()

    def test_configure_on_the_id_resolves_url_model_and_key(self):
        config = S.provider_config({"preset": "custom:my-proxy", "use_stored_key": True})
        self.assertEqual((config.base_url, config.model, config.api_key, config.extra),
                         ("https://llm.example.com/v1", "big-model", "sk-secret-123", {}))
        self.assertFalse(config.local or config.hosted)
        other = S.provider_config({"preset": "custom:my-proxy", "model": "small-model", "use_stored_key": True})
        self.assertEqual(other.model, "small-model")

    def test_resolve_preset_and_provider_name_know_the_id(self):
        preset = P.resolve_preset("custom:my-proxy")
        self.assertTrue(preset.custom)
        self.assertEqual(P.resolve_preset(None, "https://llm.example.com/v1", "big-model").id, "custom:my-proxy")
        self.assertEqual(S.provider_name("custom:my-proxy"), "my proxy (custom:my-proxy)")

    def test_an_unknown_custom_id_is_a_sentence(self):
        with self.assertRaises(ValueError) as caught:
            S.provider_config({"preset": "custom:nope", "use_stored_key": True})
        self.assertIn("custom:nope", str(caught.exception))

    def test_a_role_and_a_fallback_entry_on_the_id_build_with_the_stored_key(self):
        main = ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "kimi-key", {}, 8192)
        table = validate_roles({"subagent": {"preset": "custom:my-proxy", "effort": "high"}})
        made = RoleResolver(main, "kimi", table, key_lookup=keystore.lookup)
        got = made.resolve("subagent")
        self.assertEqual(got.source, "configured")
        self.assertEqual((got.config.base_url, got.config.model, got.config.api_key),
                         ("https://llm.example.com/v1", "big-model", "sk-secret-123"))
        self.assertEqual(got.config.extra, {})                 # effort_style none: nothing sent
        candidate = made.fallback_candidate({"preset": "custom:my-proxy"}, "main", set())
        self.assertEqual(candidate.config.model, "big-model")

    def test_test_key_on_the_id_makes_the_usual_call_with_the_stored_key(self):
        seen = []

        def factory(preset_id, key):
            seen.append((preset_id, key))
            raise RuntimeError("no network in tests")

        events = []
        thread = keytest.run("custom:my-proxy", events.append, "t1", factory=factory)
        thread.join(5)
        self.assertEqual(seen, [("custom:my-proxy", "sk-secret-123")])
        self.assertEqual((events[0]["event"], events[0]["preset"], events[0]["model"]),
                         ("key_tested", "custom:my-proxy", "big-model"))

    def test_test_key_without_a_key_says_so_and_names_the_model(self):
        self.keys.clear()
        events = []
        self.assertIsNone(keytest.run("custom:my-proxy", events.append, "t2"))
        self.assertEqual((events[0]["ok"], events[0]["model"]), (False, "big-model"))
        self.assertIn("No key", events[0]["error"])


class LoopbackTests(Case):
    LOCAL = {"name": "Local Proxy", "base_url": "http://127.0.0.1:4000/v1", "models": ["house"], "api_key": "sk-x"}

    def test_a_loopback_url_sends_no_key_whatever_is_stored(self):
        asked = []
        with mock.patch.object(C, "fetch_models", lambda url, key, **kw: (asked.append(key), None)[1]):
            events = self.save(self.LOCAL)
        row = events[0]["provider"]
        self.assertEqual((row["has_stored_key"], row["key_source"]), (False, "local"))
        self.assertEqual(asked, [""])                          # the probe carried no key either
        config = S.provider_config({"preset": "custom:local-proxy", "use_stored_key": True})
        self.assertEqual((config.api_key, config.local, config.base_url), ("", True, "http://127.0.0.1:4000/v1"))
        events = []
        self.assertIsNotNone(keytest.run("custom:local-proxy", events.append, "t3",
                                         factory=lambda *a: (_ for _ in ()).throw(RuntimeError("no network"))))

    def test_fetch_models_never_sends_a_key_to_loopback(self):
        seen = {}

        class Opener:
            def open(self, request, timeout):
                seen["auth"] = request.get_header("Authorization")
                raise OSError("down")

        # The opener is built once per process now (#TZWF), so the probe is given one here.
        with mock.patch.object(C, "shared_opener", lambda **kw: Opener()):
            self.assertIsNone(REAL_FETCH("http://localhost:4000/v1", "sk-x"))
            self.assertIsNone(seen["auth"])
            self.assertIsNone(REAL_FETCH("https://llm.example.com/v1", "sk-x"))
            self.assertEqual(seen["auth"], "Bearer sk-x")


class DeleteTests(Case):
    def test_delete_removes_the_entry_and_its_key(self):
        self.save()
        events = []
        C.handle({"type": "custom_provider_delete", "id": "d1", "provider_id": "custom:my-proxy"}, events.append)
        self.assertEqual(events, [{"event": "custom_provider_deleted", "id": "d1", "provider_id": "custom:my-proxy",
                                   "removed": True, "key_removed": True}])
        self.assertIsNone(C.find("custom:my-proxy"))
        self.assertEqual(self.keys, {})
        self.assertEqual(C.rows(), [])
        with self.assertRaises(ValueError):
            S.provider_config({"preset": "custom:my-proxy", "use_stored_key": True})

    def test_deleting_twice_is_not_an_error(self):
        self.save()
        events = []
        for _ in range(2):
            C.handle({"type": "custom_provider_delete", "id": "d", "provider_id": "custom:my-proxy"}, events.append)
        self.assertEqual([e["removed"] for e in events], [True, False])
        self.assertFalse(events[1]["key_removed"])


class KeystoreIdTests(unittest.TestCase):
    def test_a_custom_id_is_a_keyring_name_and_a_local_id_is_not(self):
        keystore._check_id("custom:my-proxy")
        for bad in ("local:bonsai", "custom:", "custom:Has-Caps", "guest:claude", "custom:a:b"):
            with self.assertRaises(ValueError):
                keystore._check_id(bad)


if __name__ == "__main__":
    unittest.main()
