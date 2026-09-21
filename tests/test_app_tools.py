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

    def test_action_list_carries_the_keys_and_the_actions_only_a_shortcut_has(self):
        # #GMCF: `set_keybinding`'s schema stopped listing the 91 actions with their keys, so
        # this is where the model finds an id — including the registry entries that have no
        # palette row of their own (here: pane.focusLeft).
        from relay_core.keybindings import KeybindingCatalog
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            keys = KeybindingCatalog(
                temp + "/relay/keybindings.json",
                [{"id": "settings.open", "description": "Open settings", "keys": ["Ctrl+,"]},
                 {"id": "pane.focusLeft", "description": "Focus the pane on the left",
                  "keys": ["Alt+Left"]}])
            tools = self.gui.build(keybindings=lambda: keys)
            rows = {a["key"]: a for a in tools.run("app_action_list", {})["actions"]}
            self.assertEqual(rows["settings.open"]["keys"], ["Ctrl+,"])
            self.assertNotIn("keys", rows["keys.reset_all"])     # a palette row, not a shortcut
            self.assertEqual(rows["pane.focusLeft"]["keys"], ["Alt+Left"])
            self.assertFalse(rows["pane.focusLeft"]["agent_safe"])
            self.assertEqual(tools.run("app_action_list", {})["total"], 3)
            # The search reaches the shortcut-only rows by id and by description.
            found = tools.run("app_action_list", {"search": "focus the pane"})["actions"]
            self.assertEqual([a["key"] for a in found], ["pane.focusLeft"])
            # A rebind is visible at once: the catalog is read through the callable, not copied.
            keys.apply(keys.prepare({"action": "pane.focusLeft", "keys": ["Ctrl+Alt+H"]})[0])
            rows = {a["key"]: a for a in tools.run("app_action_list", {})["actions"]}
            self.assertEqual(rows["pane.focusLeft"]["keys"], ["Ctrl+Alt+H"])

    def test_action_list_without_a_keybinding_catalog_is_unchanged(self):
        result = self.tools.run("app_action_list", {})
        self.assertEqual(result["total"], 2)
        for row in result["actions"]:
            self.assertNotIn("keys", row)

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

    def test_no_pane_is_sent_when_none_was_named(self):
        """The GUI aims a pane-scoped action at the asking pane itself (§30.3, #AG7R group 2).

        It can: the command arrives down that pane's own pipe, carrying its session token. So the
        common case — a pane agent acting on its own pane — costs the model nothing, and a helper,
        which has no pane, goes on landing where the person is focused.
        """
        self.tools.run("app_action_run", {"key": "settings.open"})
        self.assertNotIn("pane", self.gui.last)

    def test_a_named_pane_travels_and_comes_back(self):
        self.gui.reply = lambda command: {"ok": True, "pane": command.get("pane")}
        result = self.tools.run("app_action_run", {"key": "settings.open", "pane": " p-42 "})
        self.assertEqual(self.gui.last["pane"], "p-42")
        self.assertEqual(result["pane"], "p-42")

    def test_a_pane_that_has_gone_is_a_refusal_with_its_own_word(self):
        self.gui.reply = lambda command: {"ok": False, "error": "unknown_pane",
                                          "message": "Relay has no pane p-9 in this window any more."}
        result = self.tools.run("app_action_run", {"key": "settings.open", "pane": "p-9"})
        self.assertEqual(result["code"], "unknown_pane")
        self.assertIn("no pane", result["error"])
        self.assertIn("unknown_pane", A.ERRORS)

    def test_a_pane_that_is_not_text_is_refused_before_anything_is_sent(self):
        self.assertEqual(self.tools.run("app_action_run", {"key": "settings.open",
                                                           "pane": 7})["code"], "invalid_value")
        self.assertEqual(self.gui.commands, [])


class PanesTest(unittest.TestCase):
    """`app_panes` (§30.3, #AG7R group 2): the only place a pane's id can be read.

    Before it, an agent could aim at no pane but its own: the catalog carries the *tab*,
    `session_info` is about this conversation and `app_sessions_search` about saved ones.
    """

    def setUp(self):
        self.panes = [{"id": "p-1", "title": "~/relay", "focused": True},
                      {"id": "p-2", "title": "build", "you": True}]
        self.gui = FakeGui(lambda command: {"ok": True, "panes": self.panes})
        self.tools = self.gui.build()

    def test_the_panes_come_back_with_the_agents_own_marked(self):
        result = self.tools.run("app_panes", {})
        self.assertEqual(self.gui.last["command"], "list_panes")
        self.assertEqual([p["id"] for p in result["panes"]], ["p-1", "p-2"])
        self.assertEqual(result["count"], 2)
        self.assertIn('yours is "build"', result["text"])

    def test_a_window_that_cannot_answer_is_a_tool_error(self):
        self.gui.reply = lambda command: {"ok": False, "error": "failed",
                                          "message": "There is no window left to list panes in."}
        result = self.tools.run("app_panes", {})
        self.assertEqual(result["code"], "failed")
        self.assertIn("no window", result["error"])

    def test_it_is_a_read_so_the_writes_toggle_does_not_gate_it(self):
        gui = FakeGui(lambda command: {"ok": True, "panes": self.panes})
        tools = gui.build({**APP, "writes_enabled": False})
        self.assertEqual(tools.run("app_panes", {})["count"], 2)
        self.assertNotIn("app_panes", A.WRITE_TOOLS)

    def test_it_is_in_the_app_group_so_its_schema_is_deferred_with_the_rest(self):
        # A tool missing from the group would have its schema in *every* request, which is what
        # the groups exist to avoid (#GMCF).
        from relay_core import tool_groups
        self.assertIn("app_panes", tool_groups.GROUPS["app"][0])
        self.assertEqual(tool_groups.group_of("app_panes"), "app")


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


class OpenConversationTest(unittest.TestCase):
    """`app_open {target: "conversation"}`: the Sessions row's Enter, as a tool (§30.4).

    The index here answers `conversation()` the way `ConversationIndex` does — the header of one
    saved session, or ValueError when there is none.
    """

    def setUp(self):
        self.rows = {
            "s1": {"session_id": "s1", "title": "The keybinding rewrite", "source": "agent",
                   "session_dir": "/home/e/.local/share/relay/sessions",
                   "workspace": "/home/e/relay", "model": "glm-5.3", "turns": 9,
                   "items": [{"kind": "prompt", "text": "not part of a row"}],
                   "overview": {"turns": 9}},
            "s2": {"session_id": "s2", "title": "Continue the pane split", "source": "agent",
                   "session_dir": "/home/e/.local/share/relay/sessions",
                   "workspace": "/home/e/relay", "turns": 2},
            "g1": {"session_id": "g1", "title": "A claude session", "source": "claude",
                   "session_dir": "", "workspace": "/home/e/relay",
                   "raw_cwd": "/home/e/relay", "turns": 4},
        }
        self.gui = FakeGui(lambda command: {"ok": True})
        self.tools = self.gui.build(sessions=lambda: self)

    # the index's two methods this needs
    def conversation(self, session_id, turn=None, query="", limit=400):
        if session_id not in self.rows:
            raise ValueError("No indexed conversation with that id.")
        return dict(self.rows[session_id])

    def search(self, query, **kwargs):
        return {"items": [dict(row) for row in self.rows.values()], "total": len(self.rows)}

    def test_one_id_opens_one_conversation_in_a_new_pane(self):
        result = self.tools.run("app_open", {"target": "conversation", "id": "s1"})
        command = self.gui.last
        self.assertEqual(command["command"], "open")
        self.assertEqual(command["target"], "conversation")
        # The session id never travels as `id`: that name is the request id's (AppBridge.send).
        self.assertEqual(command["conversation"], "s1")
        self.assertTrue(command["new_pane"])
        self.assertEqual(command["item"]["session_id"], "s1")
        self.assertEqual(command["item"]["title"], "The keybinding rewrite")
        # The transcript is not dragged through a tool result.
        self.assertNotIn("items", command["item"])
        self.assertNotIn("overview", command["item"])
        self.assertIn("The keybinding rewrite", result["text"])
        self.assertEqual(result["opened"], 1)

    def test_a_group_opens_each_in_its_own_pane_in_order(self):
        result = self.tools.run("app_open", {"target": "conversation", "ids": ["s2", "s1"],
                                             "new_pane": True})
        self.assertEqual([c["conversation"] for c in self.gui.commands], ["s2", "s1"])
        self.assertEqual([r["id"] for r in result["results"]], ["s2", "s1"])
        self.assertTrue(all(r["ok"] for r in result["results"]))
        self.assertIn("in new panes", result["text"])
        self.assertIn("Continue the pane split, The keybinding rewrite", result["text"])

    def test_a_named_pane_is_the_pane_it_opens_from(self):
        """With `new_pane: false` that is the pane it loads into: "here" is a pane, not the focus."""
        self.tools.run("app_open", {"target": "conversation", "id": "s1", "new_pane": False,
                                    "pane": "p-2"})
        self.assertEqual(self.gui.last["pane"], "p-2")
        self.tools.run("app_open", {"target": "conversation", "id": "s1"})
        self.assertNotIn("pane", self.gui.last)
        self.assertEqual(self.tools.run("app_open", {"target": "conversation", "id": "s1",
                                                     "pane": ""})["code"], "invalid_value")

    def test_new_pane_false_loads_it_where_the_person_is(self):
        result = self.tools.run("app_open", {"target": "conversation", "id": "s1",
                                             "new_pane": False})
        self.assertFalse(self.gui.last["new_pane"])
        self.assertIn("in this pane", result["text"])

    def test_a_guest_row_carries_the_command_that_resumes_it(self):
        self.tools.run("app_open", {"target": "conversation", "id": "g1"})
        item = self.gui.last["item"]
        self.assertEqual(item["source"], "claude")
        self.assertIn("g1", item["resume_command"])
        self.assertEqual(item["resume_cwd"], "/home/e/relay")

    def test_an_unknown_id_is_named_and_the_rest_still_open(self):
        result = self.tools.run("app_open", {"target": "conversation", "ids": ["nope", "s1"]})
        self.assertEqual([c["conversation"] for c in self.gui.commands], ["s1"])
        self.assertEqual(result["results"][0], {"id": "nope", "ok": False,
                                                "error": "unknown_conversation",
                                                "text": result["results"][0]["text"]})
        self.assertIn("unknown_conversation", A.ERRORS)
        self.assertIn("Not opened: nope.", result["text"])

    def test_every_id_unknown_is_a_refusal(self):
        result = self.tools.run("app_open", {"target": "conversation", "id": "nope"})
        self.assertEqual(result["code"], "unknown_conversation")
        self.assertEqual(self.gui.commands, [])

    def test_a_refusal_from_the_gui_is_reported_per_id(self):
        self.gui.reply = lambda command: ({"ok": False, "error": "failed",
                                           "message": "No pane to open it in."}
                                          if command["conversation"] == "s2" else {"ok": True})
        result = self.tools.run("app_open", {"target": "conversation", "ids": ["s1", "s2"]})
        self.assertTrue(result["results"][0]["ok"])
        self.assertEqual(result["results"][1]["error"], "failed")
        self.assertIn("No pane to open it in.", result["results"][1]["text"])
        self.assertIn("Not opened: Continue the pane split.", result["text"])

    def test_the_bad_shapes(self):
        self.assertEqual(self.tools.run("app_open", {"target": "conversation"})["code"],
                         "invalid_value")
        self.assertEqual(self.tools.run("app_open", {"target": "conversation",
                                                     "ids": "s1"})["code"], "invalid_value")
        self.assertEqual(self.tools.run("app_open", {"target": "conversation", "id": "s1",
                                                     "new_pane": "yes"})["code"], "invalid_value")
        many = [f"s{n}" for n in range(A.MAX_OPEN_CONVERSATIONS + 1)]
        self.assertEqual(self.tools.run("app_open", {"target": "conversation",
                                                     "ids": many})["code"], "invalid_value")
        self.assertEqual(self.gui.commands, [])

    def test_the_search_gives_the_id_the_open_takes(self):
        row = self.tools.run("app_sessions_search", {"query": "keybinding"})["items"][0]
        self.assertEqual(row["id"], "s1")
        self.assertTrue(self.tools.run("app_open", {"target": "conversation",
                                                    "id": row["id"]})["ok"])


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

    def load_the_app_tools(self):
        """#GMCF decision 9: the app tools are an on-demand group — named in the prompt, their
        schemas fetched by `load_tools`. Everything below is about what happens once they are."""
        self.agent._execute(self.agent._prepare("load_tools", {"group": "app"}), {})

    def test_the_specs_arrive_when_the_group_is_loaded(self):
        names = [t["function"]["name"] for t in self.agent.tools()]
        self.assertNotIn("app_option_list", names)
        self.assertIn("load_tools", names)
        self.load_the_app_tools()
        names = [t["function"]["name"] for t in self.agent.tools()]
        for name in A.TOOL_NAMES:
            self.assertIn(name, names)

    def test_a_call_before_the_group_is_loaded_says_how_to_load_it(self):
        with self.assertRaises(ValueError) as caught:
            self.agent._prepare("app_option_list", {})
        self.assertIn('load_tools with group="app"', str(caught.exception))

    def test_a_call_is_prepared_and_executed_through_the_tools(self):
        self.load_the_app_tools()
        prepared = self.agent._prepare("app_option_set", {"id": "agent.app_writes", "value": True})
        self.assertIn("RELAY OPTION SET", prepared.preview)
        result = self.agent._execute(prepared, {})
        self.assertTrue(result["ok"])
        self.assertEqual(self.gui.last["command"], "set_option")

    def test_plan_mode_keeps_the_reads_and_refuses_the_writes(self):
        self.agent.set_mode("plan")
        self.load_the_app_tools()
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
