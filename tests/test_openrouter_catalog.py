# SPDX-License-Identifier: AGPL-3.0-or-later
"""OpenRouter's live model list as the `openrouter` preset's catalog (owner, 2026-09-20).

Offline throughout: the fetcher is a canned listing handed to `start_refresh`, the cache lives
under a temporary XDG_CACHE_HOME, and nothing here ever opens a socket.
"""
import json
import os
import threading
import time
import unittest
import tempfile
from pathlib import Path
from unittest import mock

from relay_core import openrouter_catalog as oc
from relay_core import presets as P

LISTING = {"data": [
    {"id": "deepseek/deepseek-v4.1-flash", "name": "DeepSeek: DeepSeek V4.1 Flash",
     "context_length": 1048576, "supported_parameters": ["reasoning", "tools"],
     "pricing": {"prompt": "0.00000015", "completion": "0.0000006"}},     # per token, as strings
    {"id": "z-ai/glm-5.3", "name": "Z.AI: GLM 5.3", "context_length": 1310720,
     "supported_parameters": ["reasoning", "tools", "include_reasoning"],
     "pricing": {"prompt": "0.00000091", "completion": "0.00000286"}},
    {"id": "mistralai/mistral-small-4", "name": "Mistral: Mistral Small 4", "context_length": 128000,
     "supported_parameters": ["tools", "temperature"]},                    # no reasoning knob
    {"id": "openrouter/auto", "name": "Auto Router", "top_provider": {"context_length": 200000},
     "pricing": {"prompt": "-1", "completion": "-1"}},                     # priced by where it lands
    {"id": "meta/no-window", "name": "  "},                                 # blank name, no window
    {"name": "no id at all"}, "junk", None,
    {"id": "z-ai/glm-5.3", "name": "a repeat"},
]}


class OpenRouterCatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        env = mock.patch.dict(os.environ, {"XDG_CACHE_HOME": self.temp.name})
        env.start()
        self.addCleanup(env.stop)
        os.environ.pop(oc.ENV_SWITCH, None)
        oc.reset()
        self.addCleanup(oc.reset)

    def cache(self):
        return Path(self.temp.name) / "relay" / "openrouter-models.json"

    def fetched(self, listing=LISTING):
        calls = []

        def fetcher():
            calls.append(1)
            return json.loads(json.dumps(listing))
        return fetcher, calls

    def test_the_cache_path_is_under_xdg_cache_home(self):
        self.assertEqual(oc.cache_path(), self.cache())
        with mock.patch.dict(os.environ, {"XDG_CACHE_HOME": ""}):
            self.assertEqual(oc.cache_path(), Path.home() / ".cache" / "relay" / "openrouter-models.json")

    def test_rows_are_parsed_into_the_catalog_shape(self):
        rows = oc.parse_rows(LISTING)
        self.assertEqual([r["id"] for r in rows],
                         ["deepseek/deepseek-v4.1-flash", "z-ai/glm-5.3", "mistralai/mistral-small-4",
                          "openrouter/auto", "meta/no-window"])
        levels = P.effort_levels("openrouter")
        self.assertEqual(rows[0], {"id": "deepseek/deepseek-v4.1-flash", "label": "deepseek: deepseek v4.1 flash",
                                   "tier": None, "efforts": levels,
                                   # OpenRouter's own word for each level: max is sent as "xhigh".
                                   "effort_labels": {"low": "low", "medium": "medium", "high": "high",
                                                     "max": "xhigh"},
                                   "intelligence": None, "openrouter": None,
                                   "context_window": 1048576,
                                   # Dollars per million tokens, from the API's per-token strings.
                                   "price_prompt_per_mtok": 0.15, "price_completion_per_mtok": 0.6})
        self.assertEqual(rows[1]["price_completion_per_mtok"], 2.86)
        self.assertEqual(rows[2]["efforts"], [])                           # says it takes no reasoning
        self.assertEqual(rows[2]["effort_labels"], {})
        # No pricing at all, and OpenRouter's "-1" for a router row: neither is a price.
        self.assertIsNone(rows[2]["price_completion_per_mtok"])
        self.assertIsNone(rows[3]["price_completion_per_mtok"])
        self.assertEqual(rows[3]["context_window"], 200000)                # from top_provider
        self.assertEqual((rows[4]["label"], rows[4]["context_window"]),
                         ("meta/no-window", P.DEFAULT_CONTEXT_WINDOW))    # the slug stands in for a blank name
        self.assertEqual(oc.parse_rows({"data": "nope"}), [])
        self.assertEqual(oc.parse_rows(None), [])
        json.dumps(rows)

    def test_nothing_is_known_and_nothing_is_fetched_until_the_refresh_is_started(self):
        self.assertEqual(oc.rows(), [])
        self.assertTrue(oc.stale())
        self.assertFalse(self.cache().exists())
        # catalog_rows reads memory and the cache only: the built-in rows stand alone.
        self.assertEqual([r["id"] for r in P.catalog_rows("openrouter")],
                         [r["id"] for r in P.MODEL_CATALOG["openrouter"]])

    def test_the_refresh_fetches_once_caches_and_tells_the_listener(self):
        fetcher, calls = self.fetched()
        told = []
        oc.set_listener(lambda: told.append(threading.current_thread().name))
        self.assertTrue(oc.start_refresh(fetcher))
        self.assertTrue(oc.ready.wait(5))
        self.assertEqual(calls, [1])
        self.assertEqual(told, ["relay-openrouter-catalog"])               # on the fetch thread, never the caller's
        self.assertEqual([r["id"] for r in oc.rows()][:2], ["deepseek/deepseek-v4.1-flash", "z-ai/glm-5.3"])
        self.assertFalse(oc.stale())
        saved = json.loads(self.cache().read_text())
        self.assertEqual(saved["rows"], oc.rows())
        self.assertAlmostEqual(saved["fetched_at"], time.time(), delta=60)
        # Once per process: a second start does nothing, however often presets is asked.
        self.assertFalse(oc.start_refresh(fetcher))
        self.assertEqual(calls, [1])

    def test_catalog_rows_puts_the_built_in_tier_rows_first_then_the_live_ones(self):
        fetcher, _ = self.fetched()
        oc.start_refresh(fetcher)
        oc.ready.wait(5)
        rows = P.catalog_rows("openrouter")
        built_in = [r["id"] for r in P.MODEL_CATALOG["openrouter"]]
        self.assertEqual([r["id"] for r in rows[:len(built_in)]], built_in)
        # DeepSeek is a built-in row already, so the live copy is not repeated; the rest follow
        # in the listing's order, with the live shape (context_window included).
        self.assertEqual([r["id"] for r in rows[len(built_in):]],
                         ["z-ai/glm-5.3", "mistralai/mistral-small-4", "openrouter/auto", "meta/no-window"])
        self.assertEqual(rows.count(next(r for r in rows if r["id"] == "deepseek/deepseek-v4.1-flash")), 1)
        self.assertEqual(rows[0]["tier"], "main")                          # the built-in row, untouched
        self.assertNotIn("context_window", rows[0])
        self.assertEqual(rows[-1]["context_window"], P.DEFAULT_CONTEXT_WINDOW)
        # The presets event carries them, and no other preset is touched.
        self.assertEqual(P.PRESETS["openrouter"].to_dict()["models"], rows)
        self.assertEqual(P.PRESETS["kimi"].to_dict()["models"], P.catalog_rows("kimi"))
        json.dumps(rows)

    def test_a_fresh_cache_is_served_and_not_refetched(self):
        rows = oc.parse_rows(LISTING)
        self.cache().parent.mkdir(parents=True)
        self.cache().write_text(json.dumps({"fetched_at": time.time() - 3600, "rows": rows}))
        fetcher, calls = self.fetched()
        self.assertEqual(oc.rows(), rows)                                   # from the file, read once
        self.assertFalse(oc.start_refresh(fetcher))
        self.assertTrue(oc.ready.is_set())
        self.assertEqual(calls, [])
        self.assertIn("z-ai/glm-5.3", [r["id"] for r in P.catalog_rows("openrouter")])

    def test_a_cache_from_before_the_prices_is_served_and_refreshed(self):
        """tier_list_defaults decides by price, so a day-old cache written before the rows carried
        one would keep every twin out of Main and High until it expired. It counts as stale."""
        old = [{key: value for key, value in row.items() if not key.startswith("price_")}
               for row in oc.parse_rows(LISTING)]
        self.cache().parent.mkdir(parents=True)
        self.cache().write_text(json.dumps({"fetched_at": time.time() - 60, "rows": old}))
        self.assertEqual([r["id"] for r in oc.rows()], [r["id"] for r in old])     # served first
        self.assertTrue(oc.stale())
        fetcher, calls = self.fetched()
        self.assertTrue(oc.start_refresh(fetcher))
        self.assertTrue(oc.ready.wait(5))
        self.assertEqual(len(calls), 1)
        self.assertEqual(oc.rows()[0]["price_completion_per_mtok"], 0.6)
        self.assertFalse(oc.stale())

    def test_a_stale_cache_is_served_first_and_replaced_when_the_fetch_lands(self):
        old = [{"id": "old/model", "label": "old model", "tier": None, "efforts": [], "intelligence": None,
                "openrouter": None, "context_window": 8000}]
        self.cache().parent.mkdir(parents=True)
        self.cache().write_text(json.dumps({"fetched_at": time.time() - oc.CACHE_TTL_S - 1, "rows": old}))
        gate = threading.Event()

        def fetcher():
            gate.wait(5)
            return LISTING
        self.assertEqual(oc.rows(), old)                                    # what there is, now
        self.assertTrue(oc.stale())
        self.assertTrue(oc.start_refresh(fetcher))
        self.assertEqual(oc.rows(), old)                                    # still, while the fetch runs
        gate.set()
        self.assertTrue(oc.ready.wait(5))
        self.assertEqual([r["id"] for r in oc.rows()][:1], ["deepseek/deepseek-v4.1-flash"])
        self.assertEqual(json.loads(self.cache().read_text())["rows"], oc.rows())

    def test_a_failed_or_empty_fetch_leaves_what_was_there(self):
        old = [{"id": "old/model", "label": "old model", "tier": None, "efforts": [], "intelligence": None,
                "openrouter": None, "context_window": 8000}]
        self.cache().parent.mkdir(parents=True)
        self.cache().write_text(json.dumps({"fetched_at": 1.0, "rows": old}))
        told = []
        oc.set_listener(lambda: told.append(1))

        def broken():
            raise OSError("no network")
        with self.assertLogs("relay.openrouter_catalog", level="DEBUG") as caught:
            self.assertTrue(oc.start_refresh(broken))
            self.assertTrue(oc.ready.wait(5))
        self.assertTrue(any("no network" in line for line in caught.output), caught.output)
        self.assertEqual(oc.rows(), old)
        self.assertEqual(told, [])                                          # nothing new to push
        self.assertEqual(json.loads(self.cache().read_text())["rows"], old)
        # An empty listing is not a reason to forget a good one either.
        oc.reset()
        oc.start_refresh(lambda: {"data": []})
        self.assertTrue(oc.ready.wait(5))
        self.assertEqual(oc.rows(), old)

    def test_a_garbled_cache_is_as_good_as_none(self):
        self.cache().parent.mkdir(parents=True)
        self.cache().write_text("{not json")
        self.assertEqual(oc.rows(), [])
        self.assertTrue(oc.stale())
        oc.reset()
        self.cache().write_text(json.dumps({"rows": {"a": 1}}))
        self.assertEqual(oc.rows(), [])

    def test_the_switch_turns_the_fetch_off_but_not_the_cache(self):
        rows = oc.parse_rows(LISTING)
        self.cache().parent.mkdir(parents=True)
        self.cache().write_text(json.dumps({"fetched_at": 1.0, "rows": rows}))
        fetcher, calls = self.fetched()
        with mock.patch.dict(os.environ, {oc.ENV_SWITCH: "off"}):
            self.assertFalse(oc.start_refresh(fetcher))
        self.assertTrue(oc.ready.is_set())
        self.assertEqual(calls, [])
        self.assertEqual(oc.rows(), rows)

    def test_the_worker_starts_the_refresh_from_presets_and_pushes_a_fresh_one_when_it_lands(self):
        # The same hook the codex catalogue uses: `presets` answers at once with what is cached,
        # and the listener re-emits it when the fetch lands.
        source = (Path(__file__).resolve().parent.parent / "backend" / "worker.py").read_text()
        self.assertIn("openrouter_catalog.start_refresh()", source)
        self.assertIn("openrouter_catalog.set_listener(lambda: emit_presets())", source)
        self.assertLess(source.index("openrouter_catalog.start_refresh()"), source.index('emit({"event": "presets"'))

    def test_the_default_fetcher_is_a_plain_get_of_the_listing(self):
        class Response:
            def __enter__(self):
                return self

            def __exit__(self, *exc):
                return False

            def read(self):
                return json.dumps(LISTING).encode()
        with mock.patch("urllib.request.urlopen", return_value=Response()) as opened:
            self.assertEqual(oc.fetch()["data"][0]["id"], "deepseek/deepseek-v4.1-flash")
        request = opened.call_args.args[0]
        self.assertEqual(request.full_url, "https://openrouter.ai/api/v1/models")
        self.assertNotIn("Authorization", request.headers)                # no key, ever
        self.assertEqual(opened.call_args.kwargs["timeout"], oc.FETCH_TIMEOUT_S)


if __name__ == "__main__":
    unittest.main()
