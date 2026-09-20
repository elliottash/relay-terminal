# SPDX-License-Identifier: AGPL-3.0-or-later
"""`relay_core.board_chat` and its wiring in `board_protocol` (protocol 19.18).

No model, no network, no keyring: the page agent runs against a stub, the way the card-turn
pool's tests do. What is under test is the rule set — the FIFO queue, the read-only survey
turn, the busy guards, the survey's marker file and the import tool.
"""
import json
import threading
import time
import unittest
from pathlib import Path

from relay_core import board as B
from relay_core import board_chat
from relay_core import board_protocol as P
from relay_core import board_tools as T

CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, discussing, ready, in-progress, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 50}
"""


def write_card(root: Path, name: str, card_id: str, title: str, status: str = "inbox"):
    """A valid card file, the way `board_create_card` would have written it."""
    folder = {"needs-qa-llm": "needs_qa_llm", "needs-qa-human": "needs_qa_human"}.get(status, "")
    path = root / "features" / folder / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"---\nid: {card_id}\ntype: work\nstatus: {status}\nrank: z{card_id.lower()}z\n"
        f"created: '2026-09-19'\nlabels: []\n---\n\n# {title}\n\n## Issue\n\n{title}\n",
        encoding="utf-8")
    return path


class StubTurns:
    """`board_protocol`'s supervisor stub: no provider, nothing submitted."""

    agent = None
    busy = False

    def reset(self):
        pass


class FakeTools:
    """What the page agent touches on its tools: the turn counters and the chat scope."""

    def __init__(self):
        self.turns: list[str] = []
        self.scopes: list[tuple[bool, bool]] = []      # (chat scope open, readonly)
        self.card_scope = None
        self.readonly = False

    def begin_turn(self, turn_id=None):
        self.turns.append(turn_id)

    def begin_chat_turn(self, *, readonly=False):
        self.card_scope = T.ChatScope()
        self.readonly = bool(readonly)
        self.scopes.append((True, self.readonly))
        return self.card_scope

    def end_chat_turn(self):
        self.card_scope, self.readonly = None, False
        self.scopes.append((False, False))


class FakeAgent:
    """Stands in for the page agent's `Agent`: gated, cancellable, records the prompt."""

    def __init__(self, emit):
        self.emit = emit
        self.cancel_event = threading.Event()
        self.prompts: list[str] = []
        self.gate = threading.Event()
        self.messages = [{"role": "system", "content": "s"}]
        self.scope_during = None
        self.stopped = 0

    def ask(self, prompt, reset_cancellation=True, turn_id=None, **kw):
        self.prompts.append(prompt)
        self.messages.append({"role": "user", "content": prompt})
        self.scope_during = self.tools.card_scope if self.tools else None
        self.gate.wait(10)
        if self.cancel_event.is_set():
            self.emit({"event": "cancelled", "turn_id": turn_id})
            return
        self.emit({"event": "delta", "text": "Answer.", "turn_id": turn_id})
        self.emit({"event": "done", "turn_id": turn_id})

    def stop(self):
        self.stopped += 1
        self.cancel_event.set()
        self.gate.set()


def wait_idle(agent, timeout: float = 10.0):
    """Poll until no turn is running and the queue is empty (a finished turn drains on its own
    thread, so the thread object keeps being replaced and cannot simply be joined)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not agent.busy() and not agent.state()["queue"]:
            thread = agent.thread
            if thread is None or not thread.is_alive():
                return
        time.sleep(0.005)

class ChatTestBase(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name).resolve()
        self.root = self.repo / "issues"
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        write_card(self.root, "2026-09-19-one.md", "ABCD", "One card")
        self.events: list[dict] = []
        self.ended: list[tuple] = []
        self.agent: FakeAgent | None = None
        self.chat = board_chat.PageAgent(self.events.append, self.build,
                                        on_turn_end=self.turn_ended)
        self.addCleanup(self.chat.drop)

    def build(self, emit):
        self.agent = FakeAgent(emit)
        self.agent.tools = FakeTools()
        self.agent.tools.board = B.Board(self.root, self.repo)
        return self.agent, self.agent.tools

    def turn_ended(self, turn_id, survey, outcome):
        self.ended.append((turn_id, survey, outcome))

    def of(self, name):
        return [e for e in self.events if e.get("event") == name]

    def wait_idle(self):
        wait_idle(self.chat)


class QueueTest(ChatTestBase):
    def test_a_prompt_starts_a_turn_and_streams_tagged_events(self):
        what, ident = self.chat.ask("How many cards are in Inbox?")
        self.assertEqual(what, "turn")
        self.assertTrue(ident.startswith("chat-"))
        self.agent.gate.set()
        self.wait_idle()
        self.assertTrue(self.agent.prompts[0].startswith("[Switchboard page agent]"))
        deltas = [e for e in self.events if e.get("event") == "delta"]
        self.assertTrue(deltas and all(e.get("chat") for e in deltas))

    def test_a_second_prompt_queues_and_then_runs_in_order(self):
        self.chat.ask("first")
        self.assertEqual(self.chat.ask("second"), ("queued", "c1"))
        self.assertEqual([i["text"] for i in self.chat.state()["queue"]], ["second"])
        self.chat.ask("third")
        self.agent.gate.set()
        self.wait_idle()
        self.assertEqual([p.split("\n")[-1] for p in self.agent.prompts],
                         ["first", "second", "third"])   # the roster seeds only the first
        self.assertEqual(self.chat.state()["queue"], [])

    def test_the_queue_is_announced_and_removable(self):
        self.chat.ask("first")
        self.chat.ask("second")
        self.chat.ask("third")
        self.assertTrue(self.of("board_chat_queued"))
        self.assertTrue(self.chat.remove("c1"))
        self.assertFalse(self.chat.remove("c1"))
        self.assertEqual([i["text"] for i in self.chat.state()["queue"]], ["third"])
        self.assertTrue(self.chat.move("c2", 0))
        self.agent.gate.set()
        self.wait_idle()
        self.assertEqual(self.agent.prompts[-1], "third")

    def test_stop_cancels_the_turn_and_the_queue_carries_on(self):
        self.chat.ask("first")
        self.chat.ask("second")
        self.assertTrue(self.chat.stop())
        self.wait_idle()
        # The pane's rule: Stop ends the running turn; the queue is not discarded, and the
        # next prompt starts on a cleared cancel event.
        self.assertEqual([p.split("\n")[-1] for p in self.agent.prompts],
                         ["first", "second"])
        self.assertFalse(self.chat.busy())
        self.assertEqual(self.chat.state()["queue"], [])

    def test_a_full_queue_refuses(self):
        self.chat.ask("first")
        for _ in range(board_chat.MAX_QUEUE):
            self.chat.ask("filler")
        with self.assertRaises(ValueError):
            self.chat.ask("one too many")
        self.agent.gate.set()
        self.wait_idle()

    def test_the_history_collects_the_answer_for_a_reopened_page(self):
        self.chat.ask("hello")
        self.agent.gate.set()
        self.wait_idle()
        roles = [h["role"] for h in self.chat.state()["history"]]
        self.assertEqual(roles, ["owner", "agent"])
        self.assertEqual(self.chat.state()["history"][-1]["text"], "Answer.")

    def test_a_model_switch_keeps_the_conversation(self):
        self.chat.ask("first")
        self.agent.gate.set()
        self.wait_idle()
        self.chat.model = "flash"
        self.chat.ask("second")
        self.agent.gate.set()
        self.wait_idle()
        self.assertEqual(self.agent.prompts[-1], "second")     # not re-seeded
        self.assertEqual(len(self.agent.messages), 3)          # system + first + second

    def test_the_survey_turn_is_readonly_and_reported(self):
        self.chat.ask("survey", prompt="[Switchboard survey] …", readonly=True, survey=True)
        self.assertTrue(self.chat.state()["survey"])
        self.agent.gate.set()
        self.wait_idle()
        self.assertEqual(self.agent.tools.scopes[0], (True, True))
        self.assertTrue(self.chat.surveyed)
        self.assertEqual(self.ended, [(self.chat.turn_id, True, "done")] or
                         [(t, s, o) for t, s, o in self.ended if s])


class DrainStateTest(ChatTestBase):
    def test_the_queue_stops_listing_a_prompt_once_it_is_running(self):
        """A prompt drained off the queue has no `board_chat_started` of its own — that answers a
        `board_chat` message — so the page only learns it started from the state that follows."""
        self.chat.ask("first")
        self.chat.ask("second")
        self.assertEqual([i["text"] for i in self.chat.state()["queue"]], ["second"])
        self.agent.gate.set()
        self.wait_idle()
        states = [e["chat"] for e in self.of("board_chat_state")]
        # The queue empties when the prompt starts, and the page is told so then. Before the
        # drain announced itself the only state carrying an empty queue was the one sent when
        # *everything* had finished, so the page drew a queue row for the prompt it was already
        # streaming, for the whole of that turn.
        #
        # The assertion is about the queue and not about `running`: a stub agent finishes its
        # turn before the announcement is even written, so whether the drained turn is still
        # marked running here is a race and says nothing. What the page draws is the queue.
        emptied = [i for i, chat in enumerate(states) if not chat["queue"]]
        self.assertTrue(emptied, [[i["text"] for i in chat["queue"]] for chat in states])
        self.assertLess(emptied[0], len(states) - 1,
                        "the queue only reads empty in the very last state: the page was never "
                        "told the queued prompt had started")
        self.assertEqual(self.chat.state()["queue"], [])


class SurveyFileTest(ChatTestBase):
    def test_mark_and_read_the_state_file(self):
        board = B.Board(self.root, self.repo)
        self.assertIsNone(board_chat.survey_state(board))
        board_chat.mark_survey(board, "pending")
        self.assertEqual(board_chat.survey_state(board), "pending")
        data = json.loads((self.root / board_chat.SURVEY_FILE).read_text())
        self.assertEqual(data["state"], "pending")

    def test_a_corrupt_file_reads_as_absent(self):
        (self.root / board_chat.SURVEY_FILE).write_text("{not json", encoding="utf-8")
        board = B.Board(self.root, self.repo)
        self.assertIsNone(board_chat.survey_state(board))


class ProtocolChatTest(unittest.TestCase):
    """`board_chat` through `BoardCommands.dispatch`, against the real board tools."""

    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name).resolve()
        self.root = self.repo / "issues"
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        write_card(self.root, "2026-09-19-one.md", "ABCD", "One card")
        write_card(self.root, "2026-09-19-two.md", "EFGH", "Two card", "ready")
        self.events: list[dict] = []
        self.commands = P.BoardCommands(StubTurns(), self.events.append)
        self.commands.configure(str(self.repo), {})
        self.agent: FakeAgent | None = None
        self.commands.chat._build = self.build
        self.addCleanup(self.commands.chat.drop)

    def build(self, emit):
        self.agent = FakeAgent(emit)
        self.agent.tools = self.commands.agent_tools(str(self.repo), {})
        return self.agent, self.agent.tools

    def of(self, name):
        return [e for e in self.events if e.get("event") == name]

    def wait_idle(self):
        wait_idle(self.commands.chat)

    def ask(self, text, **extra):
        request = {"type": "board_chat", "id": "r1", "text": text, **extra}
        self.commands.dispatch(request)

    def test_a_prompt_starts_a_turn_with_the_board_as_context(self):
        self.ask("What is ready?")
        self.assertTrue(self.of("board_chat_started"))
        self.assertIn("#EFGH", self.agent.prompts[0])          # the roster carries every card
        self.agent.gate.set()
        self.wait_idle()

    def test_a_prompt_while_the_page_agent_runs_queues(self):
        self.ask("first")
        self.ask("second")
        self.assertEqual(len(self.of("board_chat_queued")), 1)
        self.agent.gate.set()
        self.wait_idle()
        self.assertEqual(len(self.agent.prompts), 2)

    def test_a_card_turn_is_refused_while_the_page_agent_runs(self):
        self.ask("hold the board")
        self.commands.dispatch({"type": "board_ask", "id": "r2", "card": "ABCD",
                                "text": "meanwhile"})
        busy = [e for e in self.events if e.get("event") == "error"]
        self.assertEqual(busy[0]["code"], "board_busy")
        self.assertIn("page agent", busy[0]["text"])
        self.agent.gate.set()
        self.wait_idle()

    def test_cancel_stops_the_turn(self):
        self.ask("hold")
        self.commands.dispatch({"type": "board_chat_cancel", "id": "r2"})
        self.wait_idle()
        self.assertTrue(self.of("board_chat_cancelled")[0]["stopped"])
        self.assertFalse(self.commands.chat.busy())

    def test_a_bad_model_is_refused_before_anything_runs(self):
        with self.assertRaises(ValueError):
            self.ask("hi", model="not-a-role")
        self.assertFalse(self.commands.chat.busy())

    def test_a_check_can_be_scoped_to_one_section(self):
        # A card whose front matter is broken lands in check; the section filter keeps it.
        (self.root / "features" / "2026-09-19-bad.md").write_text(
            "---\nid: NOPE\nstatus: inbox\n---\n# Bad\n", encoding="utf-8")
        self.commands.dispatch({"type": "board_check", "id": "r1"})
        self.assertGreater(len(self.of("board_problems")[0]["items"]), 0)
        self.commands.dispatch({"type": "board_check", "id": "r2", "section": "inbox"})
        scoped = self.of("board_problems")[1]
        self.assertEqual(scoped["section"], "inbox")
        self.assertTrue(scoped["items"])
        self.commands.dispatch({"type": "board_check", "id": "r3", "section": "done"})
        self.assertEqual(self.of("board_problems")[2]["items"], [])


class SurveyProtocolTest(ProtocolChatTest):
    def setUp(self):
        super().setUp()
        (self.repo / "TODO.md").write_text(
            "# TODO\n\n- [ ] Fix the flaky test\n- [ ] Ship the board\n", encoding="utf-8")

    def test_a_created_board_is_marked_pending_and_surveys_on_open(self):
        self.commands._board_became_ready()
        self.assertEqual(board_chat.survey_state(B.Board(self.root, self.repo)), "pending")
        self.commands.dispatch({"type": "board_open", "id": "r1"})
        surveys = self.of("board_survey")
        self.assertEqual(len(surveys), 1)
        keys = [p["source_key"] for p in surveys[0]["proposals"]]
        self.assertTrue(any("TODO.md" in k for k in keys), keys)
        self.assertIn("Fix the flaky test", self.agent.prompts[0])
        self.assertTrue(self.commands.chat.readonly)            # the read-only survey turn
        self.agent.gate.set()
        self.wait_idle()
        self.assertEqual(board_chat.survey_state(B.Board(self.root, self.repo)), "done")
        # A second open does not survey again.
        self.commands.chat.drop()
        self.commands.chat._build = self.build
        self.commands.dispatch({"type": "board_open", "id": "r2"})
        self.assertEqual(len(self.of("board_survey")), 1)

    def test_a_board_that_predates_the_survey_is_never_surveyed(self):
        self.commands.dispatch({"type": "board_open", "id": "r1"})
        self.assertEqual(self.of("board_survey"), [])
        self.assertIsNone(self.agent)

    def test_a_github_remote_is_reported_as_a_link_only(self):
        import subprocess
        try:
            subprocess.run(["git", "init", "-q"], cwd=self.repo, check=True,
                           capture_output=True)
            subprocess.run(["git", "remote", "add", "origin",
                            "https://github.com/example/boardly.git"], cwd=self.repo,
                           check=True, capture_output=True)
        except (OSError, subprocess.CalledProcessError):       # pragma: no cover - no git
            self.skipTest("git is unavailable")
        self.commands._board_became_ready()
        self.commands.dispatch({"type": "board_open", "id": "r1"})
        survey = self.of("board_survey")[0]
        self.assertEqual(survey["git"]["owner"], "example")
        self.assertEqual(survey["git"]["repo"], "boardly")
        self.assertEqual(survey["git"]["primary"], "origin")
        self.assertIn("github.com/example/boardly/issues", self.agent.prompts[0])
        self.agent.gate.set()
        self.wait_idle()


class ImportToolTest(ProtocolChatTest):
    def test_the_agent_imports_through_the_same_never_twice_path(self):
        (self.repo / "TODO.md").write_text("# TODO\n\n- [ ] A task\n", encoding="utf-8")
        tools = self.commands.agent_tools(str(self.repo), {})
        proposals = __import__("relay_core.board_import", fromlist=["propose"]).propose(
            self.repo, board=tools.board)
        keys = [p.source_key for p in proposals]
        first = tools.run("board_import_items", {"keys": keys})
        self.assertEqual(first["created"], 1)
        again = tools.run("board_import_items", {"keys": keys})
        self.assertEqual(again["created"], 0)                  # never twice

    def test_the_import_tool_needs_keys(self):
        tools = self.commands.agent_tools(str(self.repo), {})
        result = tools.run("board_import_items", {"keys": []})
        self.assertIn("error", result)

    def test_the_page_scope_allows_merge_and_refuses_the_shell(self):
        tools = self.commands.agent_tools(str(self.repo), {})
        tools.begin_chat_turn()
        scope = tools.card_scope
        self.assertTrue(scope.allows("board_merge_cards"))
        self.assertTrue(scope.allows("read_file"))
        self.assertFalse(scope.allows("run_command"))
        self.assertFalse(scope.allows("write_file"))
        names = {t["function"]["name"] for t in scope.tool_specs([])}
        self.assertIn("board_merge_cards", names)
        self.assertNotIn("run_command", names)
        self.assertNotIn("board_sections", names)
        tools.end_chat_turn()

    def test_a_readonly_turn_refuses_every_write(self):
        tools = self.commands.agent_tools(str(self.repo), {})
        tools.begin_chat_turn(readonly=True)
        result = tools.run("board_comment", {"id": "ABCD", "kind": "note", "text": "no"})
        self.assertEqual(result.get("code"), "board_readonly_turn")
        result = tools.run("board_import_items", {"keys": ["todo-md:TODO.md#0"]})
        self.assertEqual(result.get("code"), "board_readonly_turn")
        tools.end_chat_turn()
        result = tools.run("board_comment", {"id": "ABCD", "kind": "note", "text": "yes"})
        self.assertNotIn("error", result)


class FakeConfig:
    """A provider config, as far as the page agent's rebuild rule is concerned: a model id."""

    def __init__(self, model: str):
        self.model = model


class FakeResolved:
    def __init__(self, model: str):
        self.config = FakeConfig(model)
        self.preset_id = None
        self.effort = None


class FakeRoles:
    """A role table whose `switchboard` row can be repointed, the way the page's picker does."""

    def __init__(self, model: str):
        self.model = model

    def resolve(self, role):
        return FakeResolved(self.model)


class FakeMain:
    """The pane's agent, as `bind_agent` sees it: a config, a role table and a cancel event."""

    def __init__(self, model: str):
        self.config = FakeConfig(model)
        self.roles = FakeRoles(model)
        self.cancel_event = threading.Event()


class ModelNudgeTest(ProtocolChatTest):
    """A `configure` that moves the `switchboard` role reaches a live conversation (19.18).

    The page's model picker does not send `board_chat {model}` — that names another role for this
    conversation alone.  It writes the `switchboard` role itself and reconfigures every board
    worker, so `PageAgent.model` never changes and `_start`'s own rebuild rule never fires.  Left
    at that, the pick would land in the settings, redraw the box, and leave the conversation
    answering on the provider it was built with.
    """

    def setUp(self):
        super().setUp()
        self.main = FakeMain("glm-5.3")
        self.commands.turns.agent = self.main

    def build(self, emit):
        agent, tools = super().build(emit)
        # The agent a build produces answers on whatever the role resolves to right now, which is
        # exactly what `built_model_id` records.
        agent.config = FakeConfig(self.main.roles.model)
        return agent, tools

    def run_turn(self, text):
        self.ask(text)
        agent = self.agent
        agent.gate.set()
        self.wait_idle()
        return agent

    def test_a_configure_that_moves_the_switchboard_role_rebuilds_the_conversation(self):
        first = self.run_turn("first")
        self.assertEqual(self.commands.chat.built_model_id, "glm-5.3")

        # The pick: the role now resolves elsewhere, and `configure` re-binds the pane's agent.
        self.main.roles.model = "kimi-k2.5"
        self.commands.bind_agent(self.main)

        second = self.run_turn("second")
        self.assertIsNot(second, first)
        self.assertEqual(self.commands.chat.built_model_id, "kimi-k2.5")
        # The conversation came across with it (13.5's rule for a pane): the new agent keeps its
        # own system prompt and every other message, and the prompt is not re-seeded.
        self.assertEqual(len(second.messages), 3)               # system + first + second
        self.assertEqual(second.messages[-1]["content"], "second")
        self.assertTrue(second.messages[1]["content"].startswith("[Switchboard page agent]"))

    def test_a_configure_that_changes_nothing_leaves_the_live_agent_alone(self):
        first = self.run_turn("first")
        self.commands.bind_agent(self.main)                     # same resolved model
        second = self.run_turn("second")
        self.assertIs(second, first)
        self.assertEqual(self.commands.chat.built_model_id, "glm-5.3")

    def test_invalidate_before_the_first_turn_is_harmless(self):
        self.commands.chat.invalidate()
        self.assertIsNone(self.commands.chat.agent)
        self.run_turn("first")
        self.assertTrue(self.of("board_chat_started"))
        self.assertEqual(self.commands.chat.built_model_id, "glm-5.3")


if __name__ == "__main__":                                      # pragma: no cover
    unittest.main()
