# SPDX-License-Identifier: AGPL-3.0-or-later
"""The `app_*` tools: the catalog, the round trip, the refusals and the change log (§30, #FEJQ).

Nothing here starts a worker, calls a model or touches the GUI: the "GUI" is a callable that
answers `app_command` with whatever the test wants it to say, which is exactly the contract
`app_command_result` describes.
"""
import time
import unittest

from relay_core import app_tools as A

APP = {
    "tab": "tab-7",
    "writes_enabled": True,
    "options": [
        {"id": "agent.heading", "section": "agent", "section_label": "Agent", "label": "Agent",
         "kind": "heading", "settable": False},
        {"id": "agent.app_writes", "section": "agent", "section_label": "Agent",
         "label": "Agents may change options and run actions", "detail": "Gates the helper too.",
         "kind": "toggle", "value": False, "settable": True},
        {"id": "agent.turn_limit", "section": "agent", "section_label": "Agent",
         "label": "Turn limit", "kind": "number", "value": 40, "min": 1, "max": 200,
         "settable": True},
        {"id": "appearance.theme", "section": "appearance", "section_label": "Appearance",
         "label": "Theme", "kind": "choice", "value": "dark", "settable": True,
         "choices": [{"value": "dark", "label": "Dark"}, {"value": "light", "label": "Light"}]},
        {"id": "appearance.font", "section": "appearance", "section_label": "Appearance",
         "label": "Terminal font", "kind": "text", "value": "Iosevka", "settable": True},
        {"id": "keys.openrouter", "section": "keys", "section_label": "Keys",
         "label": "OpenRouter API key", "kind": "text", "settable": True, "secret": True},
        {"id": "keys.test", "section": "keys", "section_label": "Keys", "label": "Test key",
         "kind": "button", "settable": False},
    ],
    "actions": [
        {"key": "settings.open", "section": "Relay", "label": "Open settings", "agent_safe": True},
        {"key": "keys.reset_all", "section": "Relay", "label": "Reset everything",
         "agent_safe": False},
    ],
}


class FakeGui:
    """Answers an `app_command` the moment it is emitted, the way a pane would."""

    def __init__(self, reply=None):
        self.commands = []
        self.events = []
        self.reply = reply or (lambda command: {"ok": True})
        self.bridge = None

    def emit(self, event):
        self.events.append(event)
        if event.get("event") != "app_command" or self.bridge is None:
            return
        self.commands.append(event)
        answer = self.reply(event)
        if answer is not None:
            self.bridge.answer({"id": event["id"], **answer})

    def build(self, catalog=APP, **kwargs):
        catalog = A.AppCatalog.from_request(catalog) if catalog is not None else None
        self.bridge = A.AppBridge(self.emit, timeout=kwargs.pop("timeout", 2.0))
        return A.AppTools(catalog, self.bridge, **kwargs)

    @property
    def last(self):
        return self.commands[-1]


class CatalogTest(unittest.TestCase):
    def test_parses_the_configure_block(self):
        catalog = A.AppCatalog.from_request(APP)
        self.assertEqual(catalog.tab, "tab-7")
        self.assertTrue(catalog.writes_enabled)
        self.assertEqual(len(catalog.options), 7)
        self.assertEqual([s["section"] for s in catalog.sections()],
                         ["agent", "appearance", "keys"])
        self.assertEqual(catalog.find("agent.turn_limit").path, "Agent › Turn limit")

    def test_absent_block_means_no_tools(self):
        self.assertIsNone(A.AppCatalog.from_request(None))

    def test_secret_and_non_value_rows_are_not_settable(self):
        catalog = A.AppCatalog.from_request(APP)
        self.assertFalse(catalog.find("keys.openrouter").settable)
        self.assertFalse(catalog.find("keys.test").settable)
        self.assertTrue(catalog.find("agent.app_writes").settable)

    def test_a_secret_value_is_dropped_even_when_sent(self):
        rows = [dict(r) for r in APP["options"]]
        rows[5] = {**rows[5], "value": "sk-live-not-a-real-key"}
        catalog = A.AppCatalog.from_request({**APP, "options": rows})
        row = catalog.find("keys.openrouter")
        self.assertIsNone(row.value)
        self.assertNotIn("value", row.row())

    def test_bad_catalogs_are_refused(self):
        for bad in ({"options": [{"id": "x", "kind": "spinner"}]},
                    {"options": [{"kind": "toggle"}]},
                    {"actions": [{"label": "no key"}]},
                    {"options": "not a list"}):
            with self.assertRaises(A.AppToolError):
                A.AppCatalog.from_request(bad)

    def test_app_catalog_message_replaces_the_catalog(self):
        gui = FakeGui()
        commands = A.AppCommands(gui.emit)
        tools = commands.configure({"app": APP})
        self.assertEqual(tools.run("app_option_get", {"id": "appearance.theme"})["value"], "dark")
        commands.dispatch({"type": "app_catalog", "id": 4, "app": {
            "tab": "tab-7", "writes_enabled": False,
            "options": [{"id": "appearance.theme", "section": "appearance", "label": "Theme",
                         "kind": "choice", "value": "light", "settable": True,
                         "choices": [{"value": "light"}]}],
            "actions": []}})
        # The same AppTools object, so the change log survives; a new picture of the app.
        self.assertIs(commands.tools, tools)
        self.assertEqual(tools.run("app_option_get", {"id": "appearance.theme"})["value"], "light")
        # A row the new catalog does not carry is gone from the app (§30.2).
        self.assertEqual(tools.run("app_option_get", {"id": "agent.turn_limit"})["code"],
                         "unknown_row")
        self.assertEqual(gui.events[-1]["event"], "app_catalog_updated")
        self.assertEqual(gui.events[-1]["id"], 4)


class ReadToolsTest(unittest.TestCase):
    def setUp(self):
        self.gui = FakeGui()
        self.tools = self.gui.build()

    def test_list_filters_by_section_and_search(self):
        result = self.tools.run("app_option_list", {"section": "appearance"})
        self.assertEqual([r["id"] for r in result["rows"]],
                         ["appearance.theme", "appearance.font"])
        self.assertEqual(result["total"], 7)
        found = self.tools.run("app_option_list", {"search": "turn limit"})
        self.assertEqual([r["id"] for r in found["rows"]], ["agent.turn_limit"])

    def test_list_shows_no_secret_value_and_marks_the_row(self):
        rows = {r["id"]: r for r in self.tools.run("app_option_list", {"section": "keys"})["rows"]}
        self.assertNotIn("value", rows["keys.openrouter"])
        self.assertTrue(rows["keys.openrouter"]["secret"])
        self.assertFalse(rows["keys.openrouter"]["settable"])
        # A button row is listed so the agent can name it and open the pane at it.
        self.assertEqual(rows["keys.test"]["kind"], "button")

    def test_get_returns_choices_and_bounds(self):
        theme = self.tools.run("app_option_get", {"id": "appearance.theme"})
        self.assertEqual([c["value"] for c in theme["choices"]], ["dark", "light"])
        limit = self.tools.run("app_option_get", {"id": "agent.turn_limit"})
        self.assertEqual((limit["min"], limit["max"]), (1, 200))
        secret = self.tools.run("app_option_get", {"id": "keys.openrouter"})
        self.assertNotIn("value", secret)

    def test_unknown_row_suggests_near_misses(self):
        result = self.tools.run("app_option_get", {"id": "theme"})
        self.assertEqual(result["code"], "unknown_row")
        self.assertIn("appearance.theme", result["error"])

    def test_action_list_marks_what_may_be_run(self):
        result = self.tools.run("app_action_list", {})
        self.assertEqual({a["key"]: a["agent_safe"] for a in result["actions"]},
                         {"settings.open": True, "keys.reset_all": False})
        self.assertEqual(result["runnable"], 1)

    def test_reads_need_no_round_trip(self):
        self.tools.run("app_option_list", {})
        self.tools.run("app_action_list", {})
        self.tools.run("app_changes", {})
        self.assertEqual(self.gui.commands, [])


class SetOptionTest(unittest.TestCase):
    def setUp(self):
        self.gui = FakeGui(lambda command: {"ok": True, "previous": False, "value": True,
                                            "change_id": "c1"})
        self.tools = self.gui.build()

    def test_a_set_emits_the_command_and_reports_what_happened(self):
        result = self.tools.run("app_option_set", {"id": "agent.app_writes", "value": True})
        self.assertEqual(self.gui.last["command"], "set_option")
        self.assertEqual(self.gui.last["id"], "ac-1")
        self.assertEqual(self.gui.last["row"], "agent.app_writes")
        self.assertEqual(self.gui.last["value"], True)
        self.assertTrue(result["ok"])
        self.assertEqual(result["change_id"], "c1")
        self.assertIn("off → on", result["text"])
        self.assertIn("Undo", result["text"])
        # The catalog now shows what the app shows.
        self.assertIs(self.tools.run("app_option_get", {"id": "agent.app_writes"})["value"], True)

    def test_the_guis_before_and_after_win_over_the_request(self):
        self.gui.reply = lambda command: {"ok": True, "previous": 40, "value": 60,
                                          "change_id": "c9"}
        result = self.tools.run("app_option_set", {"id": "agent.turn_limit", "value": 61})
        self.assertEqual((result["previous"], result["value"]), (40, 60))
        self.assertEqual(self.tools.run("app_option_get", {"id": "agent.turn_limit"})["value"], 60)

    def test_a_secret_is_refused_before_anything_is_sent(self):
        result = self.tools.run("app_option_set", {"id": "keys.openrouter", "value": "sk-x"})
        self.assertEqual(result["code"], "secret")
        self.assertEqual(self.gui.commands, [])

    def test_a_non_value_row_is_refused_and_points_at_the_action(self):
        result = self.tools.run("app_option_set", {"id": "keys.test", "value": True})
        self.assertEqual(result["code"], "not_settable")
        self.assertIn("app_action_run", result["error"])
        self.assertEqual(self.gui.commands, [])

    def test_writes_disabled_refuses_every_write_and_keeps_the_reads(self):
        tools = self.gui.build({**APP, "writes_enabled": False})
        self.assertEqual(tools.run("app_option_set", {"id": "agent.turn_limit", "value": 5})["code"],
                         "writes_disabled")
        self.assertEqual(tools.run("app_action_run", {"key": "settings.open"})["code"],
                         "writes_disabled")
        self.assertEqual(tools.run("app_option_list", {})["count"], 7)
        self.assertEqual(self.gui.commands, [])

    def test_open_is_not_a_write(self):
        tools = self.gui.build({**APP, "writes_enabled": False})
        self.gui.reply = lambda command: {"ok": True}
        self.assertTrue(tools.run("app_open", {"target": "options", "row": "agent.turn_limit"})["ok"])

    def test_values_are_checked_against_the_kind(self):
        cases = [({"id": "agent.app_writes", "value": "yes"}, "switch"),
                 ({"id": "agent.turn_limit", "value": 900}, "above"),
                 ({"id": "agent.turn_limit", "value": 0}, "below"),
                 ({"id": "agent.turn_limit", "value": "many"}, "number"),
                 ({"id": "appearance.theme", "value": "neon"}, "takes one of"),
                 ({"id": "appearance.font", "value": 12}, "text"),
                 ({"id": "appearance.font", "value": "Iosevka\nrm -rf"}, "control characters")]
        for args, needle in cases:
            result = self.tools.run("app_option_set", args)
            self.assertEqual(result["code"], "invalid_value", args)
            self.assertIn(needle, result["error"], args)
        self.assertEqual(self.gui.commands, [])

    def test_a_choice_given_by_its_label_is_corrected(self):
        self.gui.reply = lambda command: {"ok": True, "previous": "dark", "value": command["value"],
                                          "change_id": "c2"}
        result = self.tools.run("app_option_set", {"id": "appearance.theme", "value": "Light"})
        self.assertEqual(result["value"], "light")

    def test_a_refusal_from_the_gui_becomes_a_tool_error(self):
        self.gui.reply = lambda command: {"ok": False, "error": "busy"}
        result = self.tools.run("app_option_set", {"id": "agent.turn_limit", "value": 10})
        self.assertEqual(result["code"], "busy")
        self.assertIn("busy", result["error"])
        self.assertEqual(self.tools.run("app_changes", {})["count"], 0)

    def test_a_pane_that_never_answers_times_out(self):
        gui = FakeGui(lambda command: None)          # emits, answers nothing
        tools = gui.build(timeout=0.0)
        started = time.monotonic()
        result = tools.run("app_option_set", {"id": "agent.turn_limit", "value": 10})
        self.assertEqual(result["code"], "no_reply")
        self.assertIn("nothing was changed", result["error"])
        self.assertLess(time.monotonic() - started, 5)
        # A late answer nobody is waiting for is ignored rather than thrown.
        self.assertEqual(gui.bridge.answer({"id": gui.last["id"], "ok": True})["pending"], False)


class ActionsTest(unittest.TestCase):
    def setUp(self):
        self.gui = FakeGui(lambda command: {"ok": True})
        self.tools = self.gui.build()

    def test_running_a_safe_action(self):
        result = self.tools.run("app_action_run", {"key": "settings.open"})
        self.assertEqual(self.gui.last["command"], "run_action")
        self.assertEqual(self.gui.last["key"], "settings.open")
        self.assertIn("Open settings", result["text"])

    def test_an_unsafe_action_is_refused_here(self):
        result = self.tools.run("app_action_run", {"key": "keys.reset_all"})
        self.assertEqual(result["code"], "not_agent_safe")
        self.assertEqual(self.gui.commands, [])

    def test_an_unknown_action(self):
        self.assertEqual(self.tools.run("app_action_run", {"key": "nope"})["code"],
                         "unknown_action")

    def test_an_action_the_gui_logged_joins_the_change_list(self):
        self.gui.reply = lambda command: {"ok": True, "change_id": "c7", "detail": "2 servers."}
        result = self.tools.run("app_action_run", {"key": "settings.open"})
        self.assertEqual(result["change_id"], "c7")
        self.assertIn("2 servers.", result["text"])
        self.assertEqual(self.tools.run("app_changes", {})["changes"][0]["kind"], "action")


class OpenTest(unittest.TestCase):
    def setUp(self):
        self.gui = FakeGui(lambda command: {"ok": True})
        self.tools = self.gui.build()

    def test_open_sends_every_field_it_was_given(self):
        result = self.tools.run("app_open", {"target": "options", "section": "agent",
                                             "row": "agent.turn_limit"})
        self.assertEqual(self.gui.last["command"], "open")
        self.assertEqual(self.gui.last["target"], "options")
        self.assertEqual(self.gui.last["row"], "agent.turn_limit")
        self.assertIn("Opened options", result["text"])

    def test_open_normalizes_a_card_id_and_checks_a_row(self):
        self.tools.run("app_open", {"target": "switchboard", "card": "#k7q2"})
        self.assertEqual(self.gui.last["card"], "K7Q2")
        self.assertEqual(self.tools.run("app_open", {"target": "options", "row": "nope"})["code"],
                         "unknown_row")

    def test_an_unknown_target(self):
        result = self.tools.run("app_open", {"target": "inbox"})
        self.assertEqual(result["code"], "unknown_target")
        self.assertEqual(self.gui.commands, [])


class ChangesTest(unittest.TestCase):
    def setUp(self):
        self.clock = [1000.0]
        self.gui = FakeGui(lambda command: {"ok": True, "previous": "dark", "value": "light",
                                            "change_id": "c1"})
        self.tools = self.gui.build(clock=lambda: self.clock[0])

    def test_changes_then_undo(self):
        self.tools.run("app_option_set", {"id": "appearance.theme", "value": "light"})
        self.clock[0] += 12.5
        listed = self.tools.run("app_changes", {})
        self.assertEqual(listed["count"], 1)
        entry = listed["changes"][0]
        self.assertEqual((entry["change_id"], entry["previous"], entry["value"]),
                         ("c1", "dark", "light"))
        self.assertEqual(entry["when"], 12.5)

        self.gui.reply = lambda command: {"ok": True, "value": "dark"}
        undone = self.tools.run("app_undo", {"change_id": "c1"})
        self.assertEqual(self.gui.last["command"], "undo")
        self.assertEqual(self.gui.last["change_id"], "c1")
        self.assertIn("dark again", undone["text"])
        self.assertEqual(self.tools.run("app_option_get", {"id": "appearance.theme"})["value"],
                         "dark")
        # Twice is refused, and so is a change this worker never made.
        self.assertEqual(self.tools.run("app_undo", {"change_id": "c1"})["code"], "unknown_change")
        self.assertEqual(self.tools.run("app_undo", {"change_id": "zz"})["code"], "unknown_change")

    def test_undo_works_after_writes_are_switched_off(self):
        self.tools.run("app_option_set", {"id": "appearance.theme", "value": "light"})
        self.tools.set_catalog(A.AppCatalog.from_request({**APP, "writes_enabled": False}))
        self.gui.reply = lambda command: {"ok": True, "value": "dark"}
        self.assertTrue(self.tools.run("app_undo", {"change_id": "c1"})["ok"])

    def test_the_newest_change_is_first(self):
        for index, value in enumerate(("light", "dark", "light")):
            self.gui.reply = (lambda v: lambda command: {"ok": True, "previous": "x", "value": v,
                                                         "change_id": f"c{v}"})(value)
            self.tools.run("app_option_set", {"id": "appearance.theme", "value": value})
        self.assertEqual([c["value"] for c in self.tools.run("app_changes", {})["changes"]],
                         ["light", "dark", "light"][::-1])


class FakeIndex:
    def __init__(self, items):
        self.items, self.calls = items, []

    def search(self, query, **kwargs):
        self.calls.append((query, kwargs))
        return {"items": self.items, "total": len(self.items)}


class SessionsSearchTest(unittest.TestCase):
    def setUp(self):
        self.index = FakeIndex([
            {"id": "a" * 32, "title": "The keybinding rewrite", "updated": "2026-09-19",
             "model": "glm-5.3", "workspace": "/home/e/relay", "turns": 9, "extra": "dropped",
             "matches": [{"text": "x" * 500}, {"text": "b"}, {"text": "c"}, {"text": "d"}]},
        ])
        self.gui = FakeGui()
        self.tools = self.gui.build(sessions=lambda: self.index, workspace="/home/e/relay")

    def test_search_is_answered_worker_side(self):
        result = self.tools.run("app_sessions_search", {"query": "keybinding", "limit": 3})
        self.assertEqual(self.gui.commands, [])
        self.assertEqual(self.index.calls[0][0], "keybinding")
        self.assertEqual(self.index.calls[0][1]["limit"], 3)
        self.assertEqual(self.index.calls[0][1]["scope"], "all")
        row = result["items"][0]
        self.assertEqual(row["title"], "The keybinding rewrite")
        self.assertNotIn("extra", row)
        self.assertEqual(len(row["matches"]), 3)
        self.assertEqual(len(row["matches"][0]), 200)

    def test_limits_are_capped_and_an_empty_query_is_refused(self):
        self.tools.run("app_sessions_search", {"query": "x", "limit": 9999})
        self.assertEqual(self.index.calls[-1][1]["limit"], A.MAX_SESSION_ROWS)
        self.assertEqual(self.tools.run("app_sessions_search", {"query": "  "})["code"],
                         "invalid_value")

    def test_a_pane_with_no_index(self):
        tools = self.gui.build()
        self.assertIn("not available",
                      tools.run("app_sessions_search", {"query": "x"})["error"])


class PromptSectionTest(unittest.TestCase):
    def test_nothing_without_a_catalog(self):
        self.assertEqual(A.prompt_section(None), "")
        self.assertEqual(A.prompt_section(FakeGui().build(None)), "")

    def test_the_gate_is_stated_either_way(self):
        gui = FakeGui()
        on = A.prompt_section(gui.build())
        self.assertIn("app_option_set", on)
        self.assertIn("Undo", on)
        off = A.prompt_section(gui.build({**APP, "writes_enabled": False}))
        self.assertIn("switched off", off)
        self.assertIn("secret", off)


class DispatchTest(unittest.TestCase):
    def test_the_worker_half_owns_its_two_messages(self):
        self.assertTrue(A.AppCommands.handles("app_catalog"))
        self.assertTrue(A.AppCommands.handles("app_command_result"))
        self.assertFalse(A.AppCommands.handles("configure"))

    def test_a_result_reaches_the_waiting_tool(self):
        events = []
        commands = A.AppCommands(events.append)
        tools = commands.configure({"app": APP}, workspace="/w")
        self.assertEqual(tools.workspace, "/w")
        answers = []

        def answer_later():
            while not events or events[-1].get("event") != "app_command":
                time.sleep(0.01)
            commands.dispatch({"type": "app_command_result", "id": events[-1]["id"], "ok": True,
                               "previous": "dark", "value": "light", "change_id": "g1"})

        import threading
        threading.Thread(target=answer_later, daemon=True).start()
        answers.append(tools.run("app_option_set", {"id": "appearance.theme", "value": "light"}))
        self.assertEqual(answers[0]["change_id"], "g1")

    def test_configure_without_an_app_block_leaves_no_tools(self):
        commands = A.AppCommands(lambda event: None)
        self.assertIsNone(commands.configure({}))
        self.assertIsNone(commands.tools)

    def test_a_bad_result_is_a_protocol_error(self):
        commands = A.AppCommands(lambda event: None)
        with self.assertRaises(ValueError):
            commands.dispatch({"type": "app_command_result", "ok": True})


class AgentWiringTest(unittest.TestCase):
    """`agent.app`: the tools reach the model the way `agent.board` does (§30.4)."""

    def setUp(self):
        import tempfile
        from relay_core.agent import Agent
        from relay_core.provider import ProviderConfig
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.gui = FakeGui(lambda command: {"ok": True, "previous": False, "value": True,
                                            "change_id": "c1"})
        self.tools = self.gui.build()
        self.agent = Agent(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""),
                           self.temp.name, lambda event: None, app=self.tools)

    def test_the_specs_are_in_the_tool_list(self):
        names = [t["function"]["name"] for t in self.agent.tools()]
        for name in A.TOOL_NAMES:
            self.assertIn(name, names)

    def test_a_call_is_prepared_and_executed_through_the_tools(self):
        prepared = self.agent._prepare("app_option_set", {"id": "agent.app_writes", "value": True})
        self.assertIn("RELAY OPTION SET", prepared.preview)
        result = self.agent._execute(prepared, {})
        self.assertTrue(result["ok"])
        self.assertEqual(self.gui.last["command"], "set_option")

    def test_plan_mode_keeps_the_reads_and_refuses_the_writes(self):
        self.agent.set_mode("plan")
        names = [t["function"]["name"] for t in self.agent.tools()]
        self.assertIn("app_option_list", names)
        with self.assertRaises(ValueError):
            self.agent._prepare("app_option_set", {"id": "agent.app_writes", "value": True})
        self.agent._prepare("app_open", {"target": "options"})

    def test_the_brief_is_in_the_system_prompt(self):
        self.assertIn("app_option_list", self.agent.system_prompt())

    def test_an_agent_with_no_app_block_is_unchanged(self):
        from relay_core.agent import Agent
        from relay_core.provider import ProviderConfig
        bare = Agent(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""), self.temp.name,
                     lambda event: None)
        self.assertIsNone(bare.app)
        names = [t["function"]["name"] for t in bare.tools()]
        self.assertNotIn("app_option_list", names)
        self.assertNotIn("app_option_list", bare.system_prompt())


if __name__ == "__main__":
    unittest.main()
