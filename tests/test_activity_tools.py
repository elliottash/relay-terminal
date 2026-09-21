# SPDX-License-Identifier: AGPL-3.0-or-later
"""The pane agent's `session_info` and `activity` read tools (§30.5, card #FEJQ).

The point of both tools is that they are **bounded**: a long session must not produce a long
tool result, because the tool that answers "why is my context low" must not be what lowers it.
So the fake agent here holds far more turns and calls than any window asks for, and every test
is about what does *not* come back as much as what does.
"""
import threading
import unittest
from collections import OrderedDict

from relay_core import activity_tools as T


def make_record(index: int, *, calls: int = 3, running: bool = False) -> dict:
    """One turn of `Agent.turn_log`, in the shape `Agent._begin_record`/`_record_tool` leave."""
    tools = OrderedDict()
    for call in range(calls):
        name = "run_command" if call % 2 else "read_file"
        tools[f"t{index}-{call}"] = {
            "call_id": f"t{index}-{call}", "name": name,
            "preview": f"RUN COMMAND\n\ncommand number {index}.{call}",
            "result": {"exit_code": 0} if call % 2 else {"text": "x"},
            "ok": call != 2, "ms": 100 * (call + 1) + index,
            "label": {"title": "Ran", "path": f"src/file{call}.cpp"},
            "args": {}}
    return {"turn_id": f"turn-{index}", "started": 0.0,
            "prompt": f"Prompt number {index}\nand a second line nobody needs",
            "thinking_ms": 25, "thinking_chars": 10, "thinking_open": False, "tools": tools,
            "messages": [{"role": "user", "content": "x" * 5000}] * 20,
            "model": "glm-5.3", "usage": {"prompt_tokens": 100 * index, "completion_tokens": 7},
            "elapsed_ms": None if running else 1000 + index,
            "outcome": None if running else "done"}


class FakeAgent:
    def __init__(self, turns: int = 30, calls: int = 3):
        self._lock = threading.RLock()
        self.turn_log = OrderedDict(
            (f"turn-{i}", make_record(i, calls=calls)) for i in range(turns))
        self.app = None
        self.activity = None
        self.refreshed = 0

    def refresh_system_prompt(self):
        self.refreshed += 1


class AttachTest(unittest.TestCase):
    def test_attach_puts_the_tools_on_the_agent_and_refreshes_its_prompt(self):
        agent = FakeAgent()
        tools = T.ActivityTools.attach(agent)
        self.assertIs(agent.activity, tools)
        self.assertEqual(agent.refreshed, 1)
        self.assertEqual([s["function"]["name"] for s in tools.tool_specs()],
                         ["session_info", "activity"])
        self.assertTrue(tools.handles("activity"))
        self.assertFalse(tools.handles("board_list"))

    def test_the_prompt_note_is_there_only_with_the_tools(self):
        self.assertEqual(T.prompt_section(None), "")
        self.assertIn("session_info", T.prompt_section(T.ActivityTools(FakeAgent())))


class ActivityWindowTest(unittest.TestCase):
    def setUp(self):
        self.agent = FakeAgent(turns=30, calls=3)
        self.tools = T.ActivityTools(self.agent)

    def test_the_default_window_is_the_last_three_turns(self):
        result = self.tools.run("activity", {})
        self.assertEqual(result["count"], T.DEFAULT_TURNS)
        self.assertEqual(result["session_turns"], 30)
        self.assertEqual([t["turn_id"] for t in result["turns"]],
                         ["turn-27", "turn-28", "turn-29"])

    def test_a_digest_is_a_roll_up_not_a_transcript(self):
        turn = self.tools.run("activity", {"turns": 1})["turns"][0]
        self.assertEqual(turn["request"], "Prompt number 29")     # the first line only
        self.assertEqual(turn["model"], "glm-5.3")
        self.assertEqual(turn["outcome"], "done")
        self.assertEqual(turn["tool_calls"], 3)
        self.assertEqual(turn["tokens"], {"prompt_tokens": 2900, "completion_tokens": 7})
        self.assertEqual({row["tool"]: row["calls"] for row in turn["tools"]},
                         {"read_file": 2, "run_command": 1})
        self.assertEqual([row["failed"] for row in turn["tools"] if row["tool"] == "read_file"], [1])
        # No messages, no tool output, no previews of the conversation itself.
        self.assertNotIn("calls", turn)
        self.assertNotIn("messages", turn)

    def test_the_window_is_capped_however_much_is_asked_for(self):
        big = self.tools.run("activity", {"turns": 999})
        self.assertEqual(big["count"], T.MAX_TURNS)
        self.assertLess(len(repr(big)), 20000)

    def test_a_long_session_stays_small(self):
        agent = FakeAgent(turns=50, calls=60)
        tools = T.ActivityTools(agent)
        whole = repr(tools.run("activity", {"turns": T.MAX_TURNS}))
        # 50 turns of 60 calls is 3000 calls; the digest must not grow with them.
        self.assertLess(len(whole), 30000)
        self.assertNotIn("x" * 100, whole)

    def test_an_empty_log(self):
        tools = T.ActivityTools(FakeAgent(turns=0))
        result = tools.run("activity", {})
        self.assertEqual(result["turns"], [])
        self.assertIn("no turns", result["note"])

    def test_a_running_turn_says_so(self):
        self.agent.turn_log["turn-live"] = make_record(99, running=True)
        turn = self.tools.run("activity", {"turns": 1})["turns"][0]
        self.assertTrue(turn["running"])
        self.assertEqual(turn["outcome"], "running")

    def test_bad_arguments(self):
        self.assertEqual(self.tools.run("activity", {"turns": "three"})["code"], "invalid_value")
        self.assertEqual(self.tools.run("activity", {"slowest": 1.5})["code"], "invalid_value")
        self.assertEqual(self.tools.run("activity", {"turn": 4})["code"], "invalid_value")


class OneTurnTest(unittest.TestCase):
    def setUp(self):
        self.tools = T.ActivityTools(FakeAgent(turns=8, calls=5))

    def test_one_turn_by_id_lists_its_calls(self):
        result = self.tools.run("activity", {"turn": "turn-3"})
        self.assertEqual(result["turn_id"], "turn-3")
        self.assertEqual(len(result["calls"]), 5)
        first = result["calls"][0]
        self.assertEqual(first["tool"], "read_file")
        self.assertEqual(first["what"], "Ran src/file0.cpp")     # the §23 label, not the output
        self.assertEqual(first["outcome"], "ok")
        self.assertEqual(result["calls"][2]["outcome"], "error")

    def test_the_call_list_is_capped(self):
        tools = T.ActivityTools(FakeAgent(turns=2, calls=200))
        result = tools.run("activity", {"turn": "turn-1"})
        self.assertEqual(len(result["calls"]), T.MAX_CALLS_IN_DETAIL)
        self.assertEqual(result["calls_omitted"], 200 - T.MAX_CALLS_IN_DETAIL)

    def test_an_unknown_turn_names_the_ones_that_are_there(self):
        result = self.tools.run("activity", {"turn": "turn-999"})
        self.assertEqual(result["code"], "unknown_turn")
        self.assertIn("turn-7", result["error"])


class SlowestTest(unittest.TestCase):
    def test_the_slowest_calls_of_the_window(self):
        tools = T.ActivityTools(FakeAgent(turns=6, calls=4))
        result = tools.run("activity", {"slowest": 3})
        self.assertEqual(len(result["slowest"]), 3)
        times = [row["ms"] for row in result["slowest"]]
        self.assertEqual(times, sorted(times, reverse=True))
        self.assertEqual(result["tool_calls"], 24)
        self.assertEqual(result["turns_searched"], 6)
        self.assertIn("turn_id", result["slowest"][0])

    def test_slowest_is_capped(self):
        tools = T.ActivityTools(FakeAgent(turns=20, calls=20))
        self.assertEqual(len(tools.run("activity", {"slowest": 500})["slowest"]), T.MAX_SLOWEST)


class SessionInfoTest(unittest.TestCase):
    def payload(self, turns: int) -> dict:
        return {"event": "session_info", "kind": "session", "id": 7, "session_id": "a" * 32,
                "turns": turns, "model": "glm-5.3", "live": True,
                "context": {"used_tokens": 12000, "window": 200000, "percent": 6.0},
                "history": [{"turn": i, "prompt": "p" * 200} for i in range(turns)]}

    def test_the_event_shape_comes_through_with_the_history_windowed(self):
        tools = T.ActivityTools(FakeAgent(), live_info=lambda: self.payload(120))
        info = tools.run("session_info", {})
        self.assertNotIn("event", info)
        self.assertNotIn("id", info)
        self.assertEqual(info["turns"], 120)                    # the true count, not the window
        self.assertEqual(len(info["history"]), T.MAX_INFO_HISTORY)
        self.assertEqual(info["history"][-1]["turn"], 119)      # the *last* 20
        self.assertEqual(info["history_truncated"], 100)
        self.assertEqual(info["context"]["percent"], 6.0)

    def test_a_short_session_is_not_marked_truncated(self):
        tools = T.ActivityTools(FakeAgent(), live_info=lambda: self.payload(4))
        info = tools.run("session_info", {})
        self.assertEqual(len(info["history"]), 4)
        self.assertNotIn("history_truncated", info)
        self.assertNotIn("note", info)

    def test_without_a_session_reader(self):
        tools = T.ActivityTools(FakeAgent())
        self.assertIn("cannot read", tools.run("session_info", {})["error"])

    def test_a_reader_that_raises_is_a_tool_error(self):
        def boom():
            raise ValueError("This pane has no saved sessions.")
        tools = T.ActivityTools(FakeAgent(), live_info=boom)
        self.assertIn("no saved sessions", tools.run("session_info", {})["error"])


class RealAgentTest(unittest.TestCase):
    """The digest reads the record a real turn leaves (`Agent.turn_log`), not a second one."""

    def test_a_real_turn_is_digested_from_its_own_record(self):
        import json
        import tempfile
        from relay_core.agent import Agent
        from relay_core.provider import ProviderConfig

        class FakeProvider:
            def __init__(self):
                self.calls = 0

            def complete(self, messages, tools, emit, cancel):
                self.calls += 1
                if self.calls == 1:
                    emit({"event": "usage", "usage": {"prompt_tokens": 120,
                                                      "completion_tokens": 8}})
                    return {"role": "assistant", "content": "", "tool_calls": [
                        {"id": "call-1", "type": "function", "function": {
                            "name": "run_command",
                            "arguments": json.dumps({"command": "printf hi"})}}]}
                emit({"event": "delta", "text": "Done."})
                return {"role": "assistant", "content": "Done."}

            def cancel(self):
                pass

        with tempfile.TemporaryDirectory() as root:
            agent = Agent(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""), root,
                          lambda event: None, provider=FakeProvider())
            tools = T.ActivityTools.attach(agent)
            agent.ask("print hi")
            turn = tools.run("activity", {})["turns"][0]
            self.assertEqual(turn["request"], "print hi")
            self.assertEqual(turn["outcome"], "done")
            self.assertEqual(turn["model"], "mock")
            self.assertEqual(turn["tokens"]["prompt_tokens"], 120)
            self.assertEqual([row["tool"] for row in turn["tools"]], ["run_command"])
            self.assertIsInstance(turn["tools"][0]["ms"], int)
            detail = tools.run("activity", {"turn": turn["turn_id"]})
            self.assertEqual(detail["calls"][0]["tool"], "run_command")
            self.assertIsNotNone(detail["calls"][0]["ms"])
            self.assertIn("printf hi", detail["calls"][0]["what"])



class StubBridge:
    """`AppBridge`'s one method: the GUI's answer to an `app_command`, recorded."""

    def __init__(self, answer):
        self.answer = answer
        self.sent = []

    def send(self, command, fields):
        self.sent.append((command, dict(fields)))
        return self.answer


def app_tools_with(panes, *, writes_enabled=False):
    """A real `AppTools` over a stub GUI, so these tests exercise the tool an agent would call.

    `writes_enabled` is **off** by default on purpose: listing the panes is a read, and the
    point of #AG7R group 8 is that it is answered whatever "Agents may change options and run
    actions" says (§30.4).
    """
    from relay_core import app_tools as A
    catalog = A.AppCatalog({"tab": "t0123456789ab", "writes_enabled": writes_enabled,
                            "options": [], "actions": []})
    return A.AppTools(catalog, StubBridge({"ok": True, "panes": panes}))


PANES = [{"id": "pane-one", "title": "deploy", "cwd": "/srv/app", "tab": "t1",
          "model": "glm-5.3", "mode": "agent", "busy": True, "focused": False},
         {"id": "pane-two", "title": "notes", "cwd": "/srv/notes", "tab": "t1",
          "model": "kimi-k3", "mode": "terminal", "busy": False, "focused": True}]


class OtherPaneTest(unittest.TestCase):
    """The tab's helper reading the panes it is helping (#AG7R group 8).

    The helper has no pane of its own, so nothing `app_panes` returns is ever marked `you`: every
    pane it can name is somebody else's, which is the case this whole path exists for.
    """

    def setUp(self):
        self.agent = FakeAgent(turns=4)
        self.agent.app = app_tools_with(PANES)
        self.tools = T.ActivityTools(self.agent, live_info=lambda: {"event": "session_info",
                                                                    "turns": 4, "model": "glm-5.3"})

    def test_both_tools_take_a_pane(self):
        specs = {s["function"]["name"]: s["function"]["parameters"]["properties"]
                 for s in self.tools.tool_specs()}
        self.assertIn("pane", specs["session_info"])
        self.assertIn("pane", specs["activity"])

    def test_session_info_for_a_named_pane_says_what_the_window_knows(self):
        info = self.tools.run("session_info", {"pane": "pane-one"})
        self.assertEqual(info["pane"]["id"], "pane-one")
        self.assertTrue(info["busy"])
        self.assertFalse(info["you"])
        self.assertFalse(info["turns_readable"])
        self.assertIn("deploy", info["text"])
        self.assertIn("busy", info["text"])
        self.assertIn("glm-5.3", info["text"])
        # Not this agent's own record, which is what it would have answered with before.
        self.assertNotIn("history", info)
        self.assertNotIn("turns", info)
        self.assertIn("its own agent", info["note"])

    def test_activity_for_a_named_pane_is_the_same_read_and_not_its_turns(self):
        result = self.tools.run("activity", {"pane": "pane-two"})
        self.assertEqual(result["pane"]["id"], "pane-two")
        self.assertFalse(result["busy"])
        self.assertNotIn("turn-3", repr(result))          # never this agent's own turn log
        self.assertIn("its turns", result["note"])
        self.assertIn("idle", result["text"])

    def test_a_pane_that_has_gone_answers_cleanly_and_names_the_live_ones(self):
        result = self.tools.run("session_info", {"pane": "pane-gone"})
        self.assertEqual(result["code"], "unknown_pane")
        self.assertIn("pane-one", result["error"])
        self.assertIn("pane-two", result["error"])
        self.assertIn("app_panes", result["error"])
        # And the same on activity, by the same path.
        self.assertEqual(self.tools.run("activity", {"pane": "nope"})["code"], "unknown_pane")

    def test_a_window_with_no_panes_left_still_answers(self):
        self.agent.app = app_tools_with([])
        result = self.tools.run("activity", {"pane": "pane-one"})
        self.assertEqual(result["code"], "unknown_pane")
        self.assertIn("none", result["error"])

    def test_the_writes_toggle_does_not_gate_the_read(self):
        # Off (the default above) and on: the same answer, and no round trip but `list_panes`.
        off = self.tools.run("session_info", {"pane": "pane-one"})
        self.agent.app = app_tools_with(PANES, writes_enabled=True)
        on = self.tools.run("session_info", {"pane": "pane-one"})
        self.assertEqual(off, on)
        self.assertEqual([command for command, _ in self.agent.app.bridge.sent], ["list_panes"])

    def test_your_own_pane_is_your_own_session(self):
        mine = [{**PANES[0], "you": True}, PANES[1]]
        self.agent.app = app_tools_with(mine)
        info = self.tools.run("session_info", {"pane": "pane-one"})
        self.assertEqual(info["turns"], 4)                # the §25.3 payload, not the pane row
        self.assertNotIn("turns_readable", info)
        # And `activity` likewise digests this agent's own log.
        self.assertEqual(self.tools.run("activity", {"pane": "pane-one"})["session_turns"], 4)

    def test_no_pane_named_is_untouched(self):
        self.assertEqual(self.tools.run("activity", {"turns": 1})["count"], 1)
        self.assertEqual(self.tools.run("session_info", {})["turns"], 4)
        self.assertEqual(self.agent.app.bridge.sent, [])   # no round trip for one's own session

    def test_a_bad_pane_argument(self):
        self.assertEqual(self.tools.run("session_info", {"pane": 7})["code"], "invalid_value")
        self.assertEqual(self.tools.run("activity", {"pane": "  "})["code"], "invalid_value")

    def test_a_worker_with_no_app_tools_says_so_rather_than_answering_for_itself(self):
        self.agent.app = None
        result = self.tools.run("session_info", {"pane": "pane-one"})
        self.assertEqual(result["code"], "failed")
        self.assertIn("cannot see Relay's panes", result["error"])

    def test_a_refused_listing_comes_back_as_the_refusal_it_was(self):
        from relay_core import app_tools as A
        catalog = A.AppCatalog({"tab": "t1", "options": [], "actions": []})
        self.agent.app = A.AppTools(catalog, StubBridge({"ok": False, "error": "failed",
                                                         "text": "The window has gone."}))
        result = self.tools.run("activity", {"pane": "pane-one"})
        self.assertIn("window has gone", result["error"])

    def test_the_preview_names_the_pane_it_is_about(self):
        self.assertIn("pane-one", self.tools.preview("session_info", {"pane": "pane-one"}))
        self.assertIn("pane-two", self.tools.preview("activity", {"pane": "pane-two"}))
        self.assertEqual(self.tools.preview("session_info", {}), "SESSION INFO")

    def test_the_prompt_note_points_at_the_panes_only_where_they_can_be_read(self):
        self.assertIn("app_panes", T.prompt_section(self.tools))
        self.assertNotIn("app_panes", T.prompt_section(T.ActivityTools(FakeAgent())))

if __name__ == "__main__":
    unittest.main()
