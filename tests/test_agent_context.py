# SPDX-License-Identifier: AGPL-3.0-or-later
"""`relay_core.agent_context`: the `context` block of `configure` (protocol 33, card #AGNT).

The C++ half is `src/AgentContext.h` (`ContextSpec::toJson` / `fromJson`) and is tested by
`tests/agentcontext_test.cpp` against the same shape — which is the point of having the block
written down in one place rather than assembled at each end.
"""
import pathlib
import unittest

from relay_core import agent_context as AC


TERMINAL = {"name": "terminal"}
SWITCHBOARD = {"name": "switchboard", "agent_role": "switchboard", "workspace": "/w",
               "persist": {"scope": "helper", "key": "t0123456789ab"},
               "brief": {"key": "switchboard", "title": "Switchboard agent"},
               "scope": "console", "shell": False, "routing": "agent"}


class ParsingTests(unittest.TestCase):
    def test_no_block_at_all_is_a_gui_from_before_this_card(self):
        self.assertIsNone(AC.from_request({}))
        self.assertIsNone(AC.from_request({"context": None}))

    def test_a_terminal_context_takes_the_terminals_defaults(self):
        spec = AC.ContextSpec.from_json(TERMINAL)
        self.assertEqual(spec.name, "terminal")
        self.assertEqual(spec.scope, "pane")
        self.assertEqual(spec.routing, "auto")
        self.assertTrue(spec.shell)
        self.assertFalse(spec.is_console())

    def test_every_other_context_defaults_to_a_console_with_no_shell(self):
        for name in ("switchboard", "options", "actions", "sessions"):
            spec = AC.ContextSpec.from_json({"name": name})
            self.assertEqual(spec.scope, "console", name)
            self.assertEqual(spec.routing, "agent", name)
            self.assertFalse(spec.shell, name)

    def test_a_card_context_defaults_to_the_card_scope(self):
        self.assertEqual(AC.ContextSpec.from_json({"name": "card"}).scope, "card")

    def test_the_block_round_trips(self):
        spec = AC.ContextSpec.from_json(SWITCHBOARD)
        self.assertEqual(spec.to_json(), {
            "name": "switchboard", "agent_role": "switchboard", "workspace": "/w",
            "persist": {"scope": "helper", "key": "t0123456789ab"},
            "brief": {"key": "switchboard", "title": "Switchboard agent", "screen": ""},
            "scope": "console", "shell": False, "routing": "agent"})
        self.assertEqual(AC.ContextSpec.from_json(spec.to_json()), spec)

    def test_a_context_with_no_workspace_is_fine(self):
        spec = AC.ContextSpec.from_json({"name": "options", "workspace": ""})
        self.assertEqual(spec.workspace, "")
        self.assertEqual(spec.store(""), (None, None))

    def test_an_unknown_name_scope_or_routing_is_refused(self):
        for block in ({"name": "diffview"}, {"name": "options", "scope": "everything"},
                      {"name": "options", "routing": "sideways"},
                      {"name": "options", "shell": "yes"},
                      {"name": "options", "persist": {"scope": "cloud", "key": "k"}}):
            with self.assertRaises(ValueError, msg=block):
                AC.ContextSpec.from_json(block)

    def test_a_name_is_required_and_the_block_must_be_an_object(self):
        with self.assertRaises(ValueError):
            AC.ContextSpec.from_json({})
        with self.assertRaises(ValueError):
            AC.ContextSpec.from_json("switchboard")

    def test_a_persist_scope_without_a_key_is_refused(self):
        # Silently keying by "" would give every tab of every project one shared conversation.
        with self.assertRaises(ValueError):
            AC.ContextSpec.from_json({"name": "options", "persist": {"scope": "helper"}})


class StoreTests(unittest.TestCase):
    def test_the_helper_file_layout_is_the_one_feJQ_wrote(self):
        # #FEJQ's `3ddd2193`: <data>/relay/helper-sessions/<workspace digest>/<key digest>.json.
        # The digest is derived, never stored, so the same tab in the same project resolves to
        # the same file at every start — which is why the layout may not drift.
        spec = AC.ContextSpec.from_json(SWITCHBOARD)
        directory, session = spec.store("/w")
        self.assertTrue(directory.endswith("/helper-sessions/" + AC.helper_dir("/w").name))
        self.assertEqual(session, AC.helper_session_id("t0123456789ab"))
        self.assertEqual(len(session), 32)

    def test_two_tabs_of_one_project_are_two_conversations_in_one_directory(self):
        one = AC.ContextSpec.from_json({**SWITCHBOARD, "persist": {"scope": "helper", "key": "tA"}})
        two = AC.ContextSpec.from_json({**SWITCHBOARD, "persist": {"scope": "helper", "key": "tB"}})
        self.assertEqual(one.store("/w")[0], two.store("/w")[0])
        self.assertNotEqual(one.store("/w")[1], two.store("/w")[1])

    def test_a_tab_that_gains_a_project_keeps_the_conversation_it_had(self):
        """The live drive of card #AGNT read one tab id under two workspace digests.

        A console asked before its tab's project was known — or the tab gained one afterwards —
        so its workspace digest changed, the derived directory changed with it, and the history
        was left behind: a restart came back to an empty console. The name is already unique per
        tab, so the directory is filing; a conversation that exists under any digest for this key
        is the one that is opened.
        """
        import os
        import tempfile
        spec = AC.ContextSpec.from_json({**SWITCHBOARD, "persist": {"scope": "helper", "key": "tG"}})
        with tempfile.TemporaryDirectory() as home:
            os.environ["XDG_DATA_HOME"] = home
            try:
                nowhere, name = spec.store("")          # asked before the project was known
                project, same = spec.store("/w")        # and again once it was
                self.assertEqual(name, same)            # one tab, one file name, always
                self.assertNotEqual(nowhere, project)   # and two directories, until one exists
                pathlib.Path(nowhere).mkdir(parents=True, exist_ok=True)
                (pathlib.Path(nowhere) / f"{name}.json").write_text("{}", encoding="utf-8")
                # Now the project arrives: the conversation is found where it actually is.
                self.assertEqual(spec.store("/w"), (nowhere, name))
                # A tab with no conversation anywhere is still filed under its own project.
                fresh = AC.ContextSpec.from_json({**SWITCHBOARD, "persist": {"scope": "helper", "key": "tH"}})
                self.assertEqual(fresh.store("/w")[0], project)
            finally:
                os.environ.pop("XDG_DATA_HOME", None)

    def test_a_pane_keeps_its_own_session_store(self):
        self.assertEqual(AC.ContextSpec.from_json(TERMINAL).store("/w"), (None, None))

    def test_the_key_says_which_configures_keep_the_conversation(self):
        spec = AC.ContextSpec.from_json(SWITCHBOARD)
        same = AC.ContextSpec.from_json({**SWITCHBOARD, "agent_role": "flash"})
        moved = AC.ContextSpec.from_json({**SWITCHBOARD, "workspace": "/other"})
        self.assertEqual(spec.key(), same.key())        # a model swap keeps it
        self.assertNotEqual(spec.key(), moved.key())    # another project does not


class BriefTests(unittest.TestCase):
    def test_a_terminal_carries_no_brief(self):
        self.assertEqual(AC.ContextSpec.from_json(TERMINAL).brief_text(), "")

    def test_a_console_brief_is_the_title_the_paragraph_and_the_say_what_you_do_rule(self):
        spec = AC.ContextSpec.from_json({"name": "options",
                                         "brief": {"key": "options", "title": "Options helper"}})
        text = spec.brief_text()
        self.assertTrue(text.startswith("[Options helper]"))
        self.assertIn("app_option_list", text)
        self.assertIn("Say what you are doing", text)

    def test_the_switchboards_brief_is_the_file_beside_the_policy(self):
        spec = AC.ContextSpec.from_json(SWITCHBOARD)
        self.assertIn("card", spec.brief_text().lower())
        self.assertNotIn("<!--", spec.brief_text())

    def test_an_unknown_brief_key_is_no_brief_rather_than_an_error(self):
        # The GUI may name a context this worker is older than; a console with no brief still works.
        spec = AC.ContextSpec.from_json({"name": "options", "brief": {"key": "diffview"}})
        self.assertNotIn("diffview", spec.brief_text())

    def test_a_standing_screen_line_rides_the_brief(self):
        spec = AC.ContextSpec.from_json({"name": "sessions",
                                         "brief": {"key": "sessions", "screen": "the search box"}})
        self.assertIn("On screen now: the search box", spec.brief_text())


class AskFieldTests(unittest.TestCase):
    def test_surface_is_free_text_of_one_line(self):
        self.assertEqual(AC.validate_surface(None), "")
        self.assertEqual(AC.validate_surface("card:AGNT"), "card:AGNT")
        with self.assertRaises(ValueError):
            AC.validate_surface("two\nlines")
        with self.assertRaises(ValueError):
            AC.validate_surface("x" * (AC.MAX_SURFACE + 1))
        with self.assertRaises(ValueError):
            AC.validate_surface(7)

    def test_screen_is_cut_rather_than_refused(self):
        # It is a hint the GUI scrapes off its own widget: too much of it must not cost a turn.
        self.assertEqual(len(AC.validate_screen("x" * 9000)), AC.MAX_SCREEN)
        self.assertEqual(AC.validate_screen(None), "")
        with self.assertRaises(ValueError):
            AC.validate_screen({"rows": 3})

    def test_the_screen_line_is_one_line_under_the_brief(self):
        self.assertEqual(AC.screen_line("  two   words \n more "), "On screen now: two words more")
        self.assertEqual(AC.screen_line(""), "")


if __name__ == "__main__":
    unittest.main()
