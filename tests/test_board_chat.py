# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Board console: one worker, one protocol (cards #AGNT, #FEJQ; protocol 19.18, 33).

Until #AGNT the helper was a second implementation — `board_chat.PageAgent`, its own FIFO, its
own `board_chat*` messages, its own event tagging — and the owner's report was that it did not
work like a terminal pane: *"the queue doesn't work like the main terminal, and the thinking
bubbles don't work the same way. why not just make it feature equal with the terminal agent?"*

So a console is now an ordinary pane worker with a `context`: its queue is the pane's
(`relay_core.queue`, tested in `tests/test_queue.py`), its persistence and its brief are the
context's (`relay_core.agent_context`, tested in `tests/test_agent_context.py`), and what is
left here is the board's own half — the console's tool scope, the seed, the busy guards, the
survey and its marker file — plus one end-to-end run of `backend/worker.py` as a console.

No model, no network, no keyring: the end-to-end run answers out of a loopback stub.
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from relay_core import agent_context as AC
from relay_core import board as B
from relay_core import board_chat
from relay_core import board_protocol as P
from relay_core import board_tools as T

ROOT = Path(__file__).resolve().parents[1]

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
    """`board_protocol`'s supervisor: records what the board submits, and whether it is busy."""

    def __init__(self):
        self.agent = None
        self.busy = False
        self.submitted: list[dict] = []
        self.fail = None

    def submit(self, prompt, when="now", request_id=None, context=None, attachments=None,
               origin="user", requeue=True, ledger_id=None, surface="", screen="",
               readonly=False):
        if self.fail:
            raise ValueError(self.fail)
        self.submitted.append({"prompt": prompt, "when": when, "id": request_id,
                               "surface": surface, "screen": screen, "readonly": readonly})
        return "turn-1"

    def reset(self):
        pass


class BoardConsoleTest(unittest.TestCase):
    """`BoardCommands` with `console` on, which is what `worker.py` sets from the context."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name).resolve()
        self.root = self.repo / "issues"
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        write_card(self.root, "2026-09-19-one.md", "ABCD", "One card")
        write_card(self.root, "2026-09-19-two.md", "EFGH", "Two card", "ready")
        self.events: list[dict] = []
        self.turns = StubTurns()
        self.commands = P.BoardCommands(self.turns, self.events.append)
        self.commands.console = True
        self.commands.configure(str(self.repo), {})

    def of(self, name):
        return [e for e in self.events if e.get("event") == name]

    def dispatch(self, **request):
        self.commands.dispatch(request)


class RetiredMessageTest(BoardConsoleTest):
    """`board_chat` and its three queue messages are gone, and say so in a sentence."""

    def test_each_retired_message_names_what_to_send_instead(self):
        for kind in ("board_chat", "board_chat_cancel", "board_chat_queue_remove",
                     "board_chat_queue_move"):
            with self.assertRaises(ValueError) as caught:
                self.dispatch(type=kind, id="r1", text="merge the two voice cards")
            text = str(caught.exception)
            self.assertIn("#AGNT", text)
            self.assertIn("ask", text)
            self.assertIn("queue_move", text)

    def test_the_board_event_no_longer_carries_a_chat_block(self):
        # `chat` was overloaded four ways — true on turn events, an object on `board`,
        # `board_chat_started`, `board_chat_state` — and there is one queue now.
        self.dispatch(type="board_open", id="r1")
        self.assertNotIn("chat", self.of("board")[0])


class ConsoleSeedTest(BoardConsoleTest):
    """The board goes in front of the first question of a conversation, and never again (19.18)."""

    class FakeAgent:
        def __init__(self, messages=1):
            self.messages = [{"role": "system", "content": "s"}] * messages

    def test_the_first_question_carries_the_roster_and_the_second_does_not(self):
        seed = self.commands.console_seed(self.FakeAgent())
        self.assertIn("#ABCD", seed)
        self.assertIn("#EFGH", seed)
        self.assertIn("the board today", seed)
        self.assertEqual(self.commands.console_seed(self.FakeAgent()), "")

    def test_a_conversation_that_came_back_from_disk_is_not_seeded_again(self):
        # 30.7: a restart brings the tab's console back with its history. It has been told what
        # the board is; telling it again would put a stale roster above a live conversation.
        self.assertEqual(self.commands.console_seed(self.FakeAgent(messages=5)), "")

    def test_a_terminal_pane_is_never_seeded(self):
        self.commands.console = False
        self.assertEqual(self.commands.console_seed(self.FakeAgent()), "")

    def test_moving_the_worker_to_another_tab_seeds_again(self):
        self.commands.set_tab("t0123456789ab")
        self.assertTrue(self.commands.console_seed(self.FakeAgent()))
        self.commands.set_tab("tfedcba987654")
        self.assertTrue(self.commands.console_seed(self.FakeAgent()))


class BusyTest(BoardConsoleTest):
    """A console can write any card, so card work waits for it — and it never waits for itself."""

    def test_a_card_turn_is_refused_while_the_console_turns(self):
        self.turns.busy = True
        self.dispatch(type="board_ask", id="r1", card="ABCD", text="what is this?")
        error = self.of("error")[0]
        self.assertEqual(error["code"], "board_busy")
        self.assertIn("console", error["text"])

    def test_nothing_is_refused_while_a_terminal_panes_own_turn_runs(self):
        # A pane's agent does not hold the board the way a console does: 19.16 is unchanged, and
        # a card turn runs on its own agent beside the pane's.
        self.commands.console = False
        self.turns.busy = True
        self.assertFalse(self.commands._busy_error("r1", "the question", card_id="ABCD"))
        self.assertFalse([e for e in self.of("error") if e.get("code") == "board_busy"])

    def test_a_cleanup_still_waits_for_whatever_is_running(self):
        self.commands.console = False
        self.turns.busy = True
        self.assertTrue(self.commands._busy_error("r1", "the cleanup"))


class SurveyTest(BoardConsoleTest):
    """The survey is an ordinary read-only turn of the console's own conversation (19.18)."""

    def pending(self):
        board_chat.mark_survey(self.commands.tools.board, "pending", note="created by board_init")

    def test_a_pending_board_surveys_on_open_as_a_readonly_queued_turn(self):
        self.pending()
        self.dispatch(type="board_open", id="r1")
        offer = self.of("board_survey")
        self.assertEqual(len(offer), 1)
        self.assertEqual(offer[0]["project"], str(self.repo))
        turn = self.turns.submitted[-1]
        self.assertTrue(turn["readonly"])
        self.assertEqual(turn["surface"], "switchboard")
        self.assertEqual(turn["when"], "queue")     # it takes its place in the queue like any ask
        self.assertIn("Board survey", turn["prompt"])
        self.assertEqual(board_chat.survey_state(self.commands.tools.board), "done")

    def test_a_board_that_predates_the_survey_is_never_surveyed(self):
        self.dispatch(type="board_open", id="r1")
        self.assertFalse(self.of("board_survey"))
        self.assertFalse(self.turns.submitted)

    def test_a_survey_runs_once_per_worker(self):
        self.pending()
        self.dispatch(type="board_open", id="r1")
        self.dispatch(type="board_open", id="r2")
        self.assertEqual(len(self.of("board_survey")), 1)

    def test_a_terminal_pane_worker_never_surveys_into_the_persons_own_pane(self):
        # The one thing a survey must not do is start a turn in a terminal pane the person is
        # working in: `board_open` happens whenever a Board pane is opened.
        self.commands.console = False
        self.pending()
        self.dispatch(type="board_open", id="r1")
        self.assertFalse(self.turns.submitted)
        self.assertEqual(board_chat.survey_state(self.commands.tools.board), "pending")

    def test_a_queue_that_refuses_the_turn_leaves_the_marker_pending(self):
        self.pending()
        self.turns.fail = "Queue is full (32 prompts)."
        self.dispatch(type="board_open", id="r1")
        self.assertEqual(board_chat.survey_state(self.commands.tools.board), "pending")
        self.assertIn("Queue is full", self.of("error")[-1]["text"])

    def test_the_survey_prompt_keeps_its_question_and_drops_the_duplicated_brief(self):
        prompt = board_chat.survey_prompt(self.root, self.repo, {"trackers": [], "hints": []}, [])
        self.assertIn("Then ask which parts", prompt)
        # The brief is in the system prompt now, so repeating it here would be the one thing
        # this card took `board_chat`'s prompt building apart for.
        self.assertNotIn(board_chat.chat_brief(), prompt)


class SurveyFileTest(BoardConsoleTest):
    def test_mark_and_read_the_state_file(self):
        board = self.commands.tools.board
        self.assertIsNone(board_chat.survey_state(board))
        board_chat.mark_survey(board, "pending", note="created by board_init")
        self.assertEqual(board_chat.survey_state(board), "pending")
        board_chat.mark_survey(board, "done", note="turn done")
        self.assertEqual(board_chat.survey_state(board), "done")

    def test_a_corrupt_file_reads_as_absent(self):
        board = self.commands.tools.board
        board_chat.survey_path(board).write_text("{not json", encoding="utf-8")
        self.assertIsNone(board_chat.survey_state(board))


class BriefTest(unittest.TestCase):
    def test_the_brief_teaches_the_question_move(self):
        text = board_chat.chat_brief()
        self.assertIn("card", text.lower())
        self.assertNotIn("<!--", text)

    def test_the_brief_is_the_context_registrys_own(self):
        # One text, two names: the board side has always called it `chat_brief`, and the context
        # reaches it by `brief.key`. They may not drift apart.
        self.assertEqual(board_chat.chat_brief(), AC.brief_body("switchboard"))


class ConsoleScopeTest(BoardConsoleTest):
    """What a console's board tools offer — and the two things that are still withheld."""

    def tools(self):
        return self.commands.agent_tools(str(self.repo), {})

    def test_a_console_gets_merge_and_the_shell_and_not_board_sections(self):
        """#AGNT, owner 2026-09-20: a context specialises an agent, it does not fence it.

        §19.18's "No shell, no file writes: code is a card's Execute" is gone — a board-less
        console got both by accident anyway, because the scope hung off the board. What is left
        is not a fence: `board_sections` restructures the whole board and stays with a cleanup,
        and `board_claim` records a terminal pane a console has not got.
        """
        tools = self.tools()
        scope = tools.card_scope
        self.assertIsInstance(scope, T.ConsoleScope)   # set by `agent_tools`, not per turn
        self.assertTrue(scope.allows("board_merge_cards"))
        self.assertTrue(scope.allows("run_command"))
        self.assertTrue(scope.allows("write_file"))
        names = {t["function"]["name"] for t in tools.tool_specs()}
        self.assertIn("board_merge_cards", names)
        self.assertIn("board_import_items", names)
        self.assertIn("search_files", names)
        self.assertNotIn("board_sections", names)
        self.assertNotIn("board_claim", names)

    def test_a_terminal_panes_board_tools_are_untouched(self):
        self.commands.console = False
        names = {t["function"]["name"] for t in self.tools().tool_specs()}
        self.assertIn("board_claim", names)
        self.assertNotIn("board_merge_cards", names)   # merge and split stay with a cleanup

    def test_the_import_tool_runs_through_the_same_never_twice_path(self):
        (self.repo / "TODO.md").write_text("- [ ] one thing\n", encoding="utf-8")
        tools = self.tools()
        import relay_core.board_import as I
        keys = [p.to_dict()["source_key"] for p in I.propose(self.repo, board=tools.board)]
        self.assertTrue(keys)
        self.assertEqual(tools.run("board_import_items", {"keys": keys})["created"], 1)
        self.assertEqual(tools.run("board_import_items", {"keys": keys})["created"], 0)

    def test_a_readonly_turn_refuses_every_board_write(self):
        tools = self.tools()
        tools.readonly = True                          # what `Agent.set_readonly` does
        result = tools.run("board_comment", {"id": "ABCD", "kind": "note", "text": "no"})
        self.assertEqual(result.get("code"), "board_readonly_turn")
        tools.readonly = False
        self.assertNotIn("error", tools.run("board_comment", {"id": "ABCD", "kind": "note",
                                                              "text": "yes"}))


class KeybindingTest(BoardConsoleTest):
    """A `keybindings` reload reaches the card conversations, which are the second agents left."""

    class FakeExecutor:
        keybindings = None

    class FakeAgent:
        def __init__(self):
            self.executor = KeybindingTest.FakeExecutor()

    def test_a_reload_reaches_every_live_card_conversation(self):
        from relay_core.board_turns import CardSession
        session = CardSession(card_id="ABCD", agent=self.FakeAgent(), tools=None)
        self.commands.cards._sessions["ABCD"] = session
        catalog = object()
        self.commands.set_keybindings(catalog)
        self.assertIs(session.agent.executor.keybindings, catalog)

    def test_a_worker_with_no_card_conversation_is_not_an_error(self):
        self.commands.set_keybindings(object())


# ---------------------------------------------------------------------------------------------
# End to end: `backend/worker.py` driven as a console over NDJSON, against a loopback stub.

class _StubHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    reply = "Noted."

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
        self.server.seen.append(body)
        payload = {"id": "stub", "object": "chat.completion", "created": int(time.time()),
                   "model": body.get("model", "stub"),
                   "choices": [{"index": 0, "finish_reason": "stop",
                                "message": {"role": "assistant", "content": self.reply}}],
                   "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


class WorkerConsoleTest(unittest.TestCase):
    """One `configure` with a context, one `ask` with a surface, through the real worker.

    The whole of #AGNT's step 4 on the wire: no `board_chat`, no second agent, no second queue.
    """

    def setUp(self):
        from unittest import mock
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        # The worker writes under this data root; the test reads the same paths back, so it has
        # to resolve them against the same root rather than the person's own.
        patch = mock.patch.dict(os.environ, {"XDG_DATA_HOME": str(self.home / ".local/share")})
        patch.start()
        self.addCleanup(patch.stop)
        self.repo = self.home / "project"
        self.root = self.repo / "issues"
        self.root.mkdir(parents=True)
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        write_card(self.root, "2026-09-19-one.md", "FX01", "A fixture card")
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), _StubHandler)
        self.server.seen = []
        self.addCleanup(self.server.server_close)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.shutdown)
        self.base = f"http://127.0.0.1:{self.server.server_address[1]}/v1"

    def context(self, name="switchboard", key="t0123456789ab", scope=None):
        block = {"name": name, "agent_role": "switchboard",
                 "workspace": str(self.repo),
                 "brief": {"key": name, "title": "Board agent"},
                 "shell": False, "routing": "agent"}
        if key:
            block["persist"] = {"scope": "helper", "key": key}
        if scope:
            block["scope"] = scope
        return block

    #: The smallest `app` block that makes the app tools exist (§30.2), so this proves a console
    #: is offered them rather than that this worker happens to have none.
    APP = {"tab": "t0123456789ab",
           "options": [{"id": "appearance.copy_on_select", "section": "appearance",
                        "label": "Copy on select", "kind": "toggle", "value": True,
                        "settable": True}],
           "actions": [{"key": "pane.split_right", "label": "Split right", "agent_safe": True}]}

    def configure(self, **extra):
        return {"type": "configure", "id": "c1", "base_url": self.base, "model": "stub",
                "api_key": "", "workspace": str(self.repo), "app": self.APP,
                "board": {"autonomy": "auto"}, **extra}

    def run_worker(self, messages, until=("agent_finished",), timeout=60):
        """Drive the real worker over its pipe, and shut it down once the turn has landed.

        A `shutdown` in the same batch would break the read loop while the turn was still in the
        queue — `shutdown` is answered the moment it is read — so the turn's own end is what
        this waits for, and the errors that end an ask count as an end too.
        """
        env = {**os.environ, "XDG_DATA_HOME": str(self.home / ".local/share"),
               "RELAY_KEYRING": "off", "PYTHONPATH": str(ROOT / "backend")}
        messages = [m for m in messages if m.get("type") != "shutdown"]
        proc = subprocess.Popen([sys.executable, "-S", str(ROOT / "backend/worker.py")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env)
        self.addCleanup(proc.kill)
        for pipe in (proc.stdin, proc.stdout, proc.stderr):
            self.addCleanup(pipe.close)
        # A readline() on a worker waiting on stdin never returns on its own; killing it closes
        # the pipe, so a broken run fails this one test instead of hanging the suite.
        watchdog = threading.Timer(timeout, proc.kill)
        watchdog.start()
        self.addCleanup(watchdog.cancel)
        proc.stdin.write("".join(json.dumps(m) + "\n" for m in messages))
        proc.stdin.flush()
        asked = {m.get("id") for m in messages if m.get("type") in ("ask", "board_chat")}
        events = []
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            events.append(json.loads(line))
            name = events[-1].get("event")
            if name in until or (name == "error" and events[-1].get("id") in asked):
                break
        proc.stdin.write(json.dumps({"type": "shutdown"}) + "\n")
        proc.stdin.flush()
        proc.stdin.close()
        events += [json.loads(line) for line in proc.stdout.read().splitlines() if line.strip()]
        stderr = proc.stderr.read()
        self.assertEqual(proc.wait(timeout=20), 0, stderr)
        self.assertTrue(events, "the worker said nothing: " + stderr[-2000:])
        return events

    @staticmethod
    def of(events, name):
        return [e for e in events if e.get("event") == name]

    def test_a_console_configures_asks_and_answers_with_its_surface_on_every_event(self):
        events = self.run_worker([
            self.configure(context=self.context()),
            {"type": "ask", "id": "a1", "text": "which cards are on the board?",
             "surface": "switchboard", "screen": "the Inbox column"},
            {"type": "shutdown"}])
        configured = self.of(events, "configured")[0]
        self.assertEqual(configured["context"]["scope"], "console")
        self.assertEqual(configured["context"]["name"], "switchboard")
        self.assertEqual(configured["context"]["agent_role"], "switchboard")
        # Every event of the turn is addressed to the console that asked (owner decision 1:
        # one conversation, drawn everywhere).
        for name in ("agent_started", "delta", "done", "agent_finished"):
            got = self.of(events, name)
            self.assertTrue(got, name)
            self.assertEqual(got[0].get("surface"), "switchboard", name)
        # The board was seeded into the first prompt, and the screen hint reached the model.
        prompt = [m["content"] for m in self.server.seen[0]["messages"] if m["role"] == "user"][0]
        self.assertIn("#FX01", prompt)
        self.assertIn("On screen now: the Inbox column", prompt)
        # The brief is in the system prompt, once, not in front of the prompt.
        system = self.server.seen[0]["messages"][0]["content"]
        self.assertIn("[Board agent]", system)
        self.assertNotIn("[Board agent]", prompt)

    def test_the_tools_a_console_is_offered_include_the_shell_the_board_set_and_the_panes(self):
        self.run_worker([self.configure(context=self.context()),
                         {"type": "ask", "id": "a1", "text": "tidy the board", "surface": "sb"},
                         {"type": "shutdown"}])
        specs = {t["function"]["name"]: t["function"] for t in self.server.seen[0]["tools"]}
        self.assertLessEqual({"run_command", "write_file", "board_merge_cards", "search_files",
                              "app_option_list", "session_info"}, set(specs))
        self.assertNotIn("load_tools", specs)      # a console defers nothing (#GMCF decision 9)
        # #AG7R group 8, "the helper cannot read the pane it is helping": it can. `app_panes`
        # says which panes there are and whether each is busy, and `pane` on the two session
        # reads says the same about one of them rather than about the console's own turns.
        self.assertIn("app_panes", specs)
        for name in ("session_info", "activity"):
            self.assertIn("pane", specs[name]["parameters"]["properties"], name)
        system = self.server.seen[0]["messages"][0]["content"]
        self.assertIn("app_panes", system)

    def test_the_same_tab_gets_its_conversation_back_and_another_tab_does_not(self):
        script = [self.configure(context=self.context()),
                  {"type": "ask", "id": "a1", "text": "remember the teapot", "surface": "sb"},
                  {"type": "shutdown"}]
        self.run_worker(script)
        directory = AC.helper_dir(str(self.repo))
        saved = sorted(p.name for p in directory.glob("*.json"))
        self.assertEqual(saved[0], AC.helper_session_id("t0123456789ab") + ".json")
        # A second worker on the same (project, tab) picks the conversation up …
        self.server.seen.clear()
        self.run_worker([self.configure(context=self.context()),
                         {"type": "ask", "id": "a2", "text": "and now?", "surface": "sb"},
                         {"type": "shutdown"}])
        said = [m["content"] for m in self.server.seen[0]["messages"] if m["role"] == "user"]
        self.assertTrue(any("teapot" in t for t in said), said)
        # … and it is not seeded with the board a second time.
        self.assertNotIn("the board today", said[-1])
        # … while another tab of the same project starts empty.
        self.server.seen.clear()
        self.run_worker([self.configure(context=self.context(key="tfedcba987654")),
                         {"type": "ask", "id": "a3", "text": "and here?", "surface": "sb"},
                         {"type": "shutdown"}])
        said = [m["content"] for m in self.server.seen[0]["messages"] if m["role"] == "user"]
        self.assertFalse(any("teapot" in t for t in said), said)

    def test_a_switchboard_ask_in_a_tab_with_no_board_is_one_sentence(self):
        events = self.run_worker([
            {"type": "configure", "id": "c1", "base_url": self.base, "model": "stub",
             "api_key": "", "workspace": "", "app": self.APP, "context": self.context()},
            {"type": "ask", "id": "a1", "text": "what is on the board?", "surface": "switchboard"},
            {"type": "shutdown"}])
        error = [e for e in self.of(events, "error") if e.get("id") == "a1"][0]
        self.assertIn("no board", error["text"])
        self.assertFalse(self.of(events, "agent_started"))

    def test_the_other_surfaces_answer_without_a_board(self):
        events = self.run_worker([
            {"type": "configure", "id": "c1", "base_url": self.base, "model": "stub",
             "api_key": "", "workspace": "", "app": self.APP,
             "context": {**self.context(name="options"), "brief": {"key": "options",
                                                                   "title": "Options helper"}}},
            {"type": "ask", "id": "a1", "text": "what is copy on select?", "surface": "options"},
            {"type": "shutdown"}])
        self.assertTrue(self.of(events, "done"))
        self.assertFalse([e for e in self.of(events, "error") if e.get("id") == "a1"])
        system = self.server.seen[0]["messages"][0]["content"]
        self.assertIn("[Options helper]", system)
        self.assertIn("app_option_list", system)
        # No board, so no board tools — the one constraint that is a constraint (#AGNT).
        names = {t["function"]["name"] for t in self.server.seen[0]["tools"]}
        self.assertFalse({n for n in names if n.startswith("board_")})
        self.assertIn("run_command", names)

    def test_a_terminal_pane_configure_is_byte_for_byte_what_it_was(self):
        events = self.run_worker([
            {"type": "configure", "id": "c1", "base_url": self.base, "model": "stub",
             "api_key": "", "workspace": str(self.repo), "app": self.APP},
            {"type": "ask", "id": "a1", "text": "ls"},
            {"type": "shutdown"}])
        self.assertNotIn("context", self.of(events, "configured")[0])
        self.assertFalse([e for e in events if "surface" in e])
        names = {t["function"]["name"] for t in self.server.seen[0]["tools"]}
        self.assertIn("load_tools", names)         # a pane still defers (#GMCF decision 9)

    def test_board_chat_is_answered_with_the_message_to_send_instead(self):
        events = self.run_worker([self.configure(context=self.context()),
                                  {"type": "board_chat", "id": "b1", "text": "merge them"},
                                  {"type": "shutdown"}])
        error = [e for e in self.of(events, "error") if e.get("id") == "b1"][0]
        self.assertIn("#AGNT", error["text"])
        self.assertNotIn("Protocol error", error["text"])


if __name__ == "__main__":
    unittest.main()
