# SPDX-License-Identifier: AGPL-3.0-or-later
"""Options › Models "test" on a guest row: `test_key {preset: "guest:claude"}` (protocol 29.3).

Nothing here starts a real claude or codex. Each test puts a fake `claude` and `codex` on a
private PATH — a script that prints what the real CLI prints (`claude auth status --json`,
`codex login status`, 2.1.278 / 0.155.1, read off this machine on 2026-09-20) and speaks just
enough stream-json / app-server for one turn — so `keytest.run` is driven end to end, with
`shutil.which` and the adapters' own spawn as the only seams.
"""
from __future__ import annotations

import json
import os
import stat
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

from relay_core import guest_harness_provider as ghp
from relay_core import keytest

CLAUDE_STATUS = {"loggedIn": True, "authMethod": "claude.ai", "apiProvider": "firstParty",
                 "analyticsDisabled": False, "projectsDirectory": "/home/u/.claude/projects",
                 "configDirectory": "/home/u/.claude", "email": "u@example.com",
                 "orgId": "00000000-0000-0000-0000-000000000000", "orgName": "u's Organization",
                 "subscriptionType": "max"}

# The fake claude: `auth status --json` prints the JSON above (with `loggedIn` from the marker
# file), `-p … stream-json` answers the initialize handshake, then the one user message with the
# CLI's `system/init` and a `result`. Every argv it is started with is appended to ARGV_LOG.
FAKE_CLAUDE = r'''#!PYTHON
import json, os, sys, time
here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "argv.log"), "a") as log:
    log.write(json.dumps(sys.argv[1:]) + "\n")
status = json.load(open(os.path.join(here, "claude-status.json")))
if sys.argv[1:3] == ["auth", "status"]:
    print(json.dumps(status, indent=2))
    sys.exit(0 if status["loggedIn"] else 1)
if sys.argv[1] != "-p":
    sys.exit(2)
hang = os.path.exists(os.path.join(here, "hang"))
def out(obj):
    sys.stdout.write(json.dumps(obj) + "\n"); sys.stdout.flush()
for line in sys.stdin:
    msg = json.loads(line)
    if msg.get("type") == "control_request":
        out({"type": "control_response", "response": {
            "subtype": "success", "request_id": msg["request_id"],
            "response": {"account": {"subscriptionType": "max", "apiProvider": "firstParty"}}}})
    elif msg.get("type") == "user":
        out({"type": "system", "subtype": "init", "session_id": "s-fake", "model": "claude-fake-1",
             "tools": [], "permissionMode": "default"})
        if hang:
            time.sleep(30)
        out({"type": "assistant", "message": {"role": "assistant", "model": "claude-fake-1",
             "content": [{"type": "text", "text": "ok"}]}, "session_id": "s-fake"})
        out({"type": "result", "subtype": "success", "is_error": False, "result": "ok",
             "session_id": "s-fake", "usage": {"input_tokens": 3, "output_tokens": 1},
             "total_cost_usd": 0.0001})
'''

# The fake codex: `login status` prints the one line the real one prints, `debug models` an
# empty catalogue, and `app-server` answers initialize / thread/start / turn/start and then
# reports one agentMessage and the turn's completion. `thread/start`'s params are logged so the
# test can hold the probe to its posture.
FAKE_CODEX = r'''#!PYTHON
import json, os, sys
here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "argv.log"), "a") as log:
    log.write(json.dumps(sys.argv[1:]) + "\n")
logged_in = os.path.exists(os.path.join(here, "codex-logged-in"))
if sys.argv[1:] == ["login", "status"]:
    if logged_in:
        print("Logged in using ChatGPT"); sys.exit(0)
    sys.stderr.write("Not logged in\n"); sys.exit(1)
if sys.argv[1:] == ["debug", "models"]:
    print(json.dumps({"models": []})); sys.exit(0)
if sys.argv[1:] != ["app-server"]:
    sys.exit(2)
def out(obj):
    sys.stdout.write(json.dumps(obj) + "\n"); sys.stdout.flush()
for line in sys.stdin:
    msg = json.loads(line)
    method, rid = msg.get("method"), msg.get("id")
    if rid is None:
        continue
    if method == "initialize":
        out({"id": rid, "result": {"userAgent": "fake/0.155.1"}})
    elif method == "thread/start":
        with open(os.path.join(here, "thread-start.json"), "w") as f:
            json.dump(msg["params"], f)
        out({"id": rid, "result": {"thread": {"id": "t-fake", "sessionId": "t-fake"},
                                   "model": "gpt-fake-1", "reasoningEffort": "low"}})
    elif method == "turn/start":
        out({"id": rid, "result": {"turn": {"id": "u-1", "status": "inProgress"}}})
        out({"method": "turn/started", "params": {"threadId": "t-fake", "turn": {"id": "u-1"}}})
        out({"method": "item/completed", "params": {
            "threadId": "t-fake", "turnId": "u-1",
            "item": {"type": "agentMessage", "id": "m-1", "text": "ok", "phase": "final_answer"}}})
        out({"method": "turn/completed", "params": {
            "threadId": "t-fake", "turn": {"id": "u-1", "status": "completed", "items": [
                {"type": "agentMessage", "id": "m-1", "text": "ok", "phase": "final_answer"}]}}})
    else:
        out({"id": rid, "error": {"code": -32601, "message": "unknown method " + str(method)}})
'''


class FakePath:
    """A directory holding a fake `claude` and `codex`, and the environment that finds them."""

    def __init__(self, test, *, claude_logged_in=True, codex_logged_in=True, install=("claude",
                                                                                    "codex")):
        self.dir = tempfile.mkdtemp(prefix="relay-keytest-fakes-")
        test.addCleanup(self._cleanup)
        for name, body in (("claude", FAKE_CLAUDE), ("codex", FAKE_CODEX)):
            if name not in install:
                continue
            path = os.path.join(self.dir, name)
            with open(path, "w") as f:
                # The test's PATH holds only the fakes, so the interpreter is named absolutely.
                f.write(body.replace("#!PYTHON", "#!" + sys.executable, 1))
            os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        with open(os.path.join(self.dir, "claude-status.json"), "w") as f:
            json.dump({**CLAUDE_STATUS, "loggedIn": bool(claude_logged_in)}, f)
        if codex_logged_in:
            open(os.path.join(self.dir, "codex-logged-in"), "w").close()
        # Only the fakes: the real CLIs on this machine must never be found by these tests.
        patcher = mock.patch.dict(os.environ, {"PATH": self.dir, "RELAY_CODEX_BIN": ""})
        patcher.start()
        test.addCleanup(patcher.stop)
        ghp.installations(refresh=True)

    def _cleanup(self):
        import shutil
        shutil.rmtree(self.dir, ignore_errors=True)

    def hang(self):
        open(os.path.join(self.dir, "hang"), "w").close()

    def argv_log(self) -> list[list[str]]:
        try:
            with open(os.path.join(self.dir, "argv.log")) as f:
                return [json.loads(line) for line in f if line.strip()]
        except FileNotFoundError:
            return []

    def thread_start(self) -> dict:
        with open(os.path.join(self.dir, "thread-start.json")) as f:
            return json.load(f)


def wait_for(events, n=1, timeout=20.0):
    deadline = time.monotonic() + timeout
    while len(events) < n and time.monotonic() < deadline:
        time.sleep(0.02)
    return list(events)


class GuestKeyTestTests(unittest.TestCase):
    def setUp(self):
        ghp.reset_catalog()
        ghp._detected = None
        ghp._adapter_cache.clear()
        self.addCleanup(ghp.reset_catalog)
        self.addCleanup(setattr, ghp, "_detected", None)
        self.addCleanup(ghp._adapter_cache.clear)
        self.scratch = tempfile.mkdtemp(prefix="relay-keytest-cwd-")
        self.addCleanup(lambda: __import__("shutil").rmtree(self.scratch, ignore_errors=True))

    def test_claude_installed_and_logged_in_is_one_turn_and_ok(self):
        fakes = FakePath(self)
        events = []
        thread = keytest.run("guest:claude", events.append, "req-1", cwd=self.scratch)
        self.assertIsNotNone(thread)
        thread.join(20)
        self.assertFalse(thread.is_alive())
        (event,) = wait_for(events)
        self.assertEqual(event["event"], "key_tested")
        self.assertEqual(event["id"], "req-1")
        self.assertTrue(event["ok"], event)
        self.assertEqual((event["preset"], event["guest"], event["model"], event["text"]),
                         ("guest:claude", "claude", "claude-fake-1", "ok"))
        self.assertEqual(event["reply_chars"], 2)
        self.assertNotIn("error", event)
        self.assertGreaterEqual(event["elapsed_ms"], 0)
        # The status command first (free, precise), then exactly one headless turn with no
        # tools at all (`--tools ""`), whatever the prompt might have asked for.
        argvs = fakes.argv_log()
        self.assertEqual(argvs[0], ["auth", "status", "--json"])
        turns = [a for a in argvs if a[:1] == ["-p"]]
        self.assertEqual(len(turns), 1)
        self.assertIn("--tools", turns[0])
        self.assertEqual(turns[0][turns[0].index("--tools") + 1], "")
        self.assertIn("--permission-prompts", turns[0])          # the deny posture, not bypass
        self.assertNotIn("--dangerously-skip-permissions", turns[0])
        # And the row now says what the turn proved, without a second status command.
        self.assertIs(ghp.login_status("claude"), True)

    def test_codex_installed_and_logged_in_is_one_turn_and_ok(self):
        fakes = FakePath(self)
        events = []
        thread = keytest.run("guest:codex", events.append, "req-2", cwd=self.scratch)
        thread.join(20)
        (event,) = wait_for(events)
        self.assertTrue(event["ok"], event)
        self.assertEqual((event["preset"], event["guest"], event["model"], event["text"]),
                         ("guest:codex", "codex", "gpt-fake-1", "ok"))
        argvs = fakes.argv_log()
        self.assertEqual(argvs[0], ["login", "status"])
        self.assertEqual([a for a in argvs if a == ["app-server"]], [["app-server"]])
        started = fakes.thread_start()
        # Codex has no "no tools" switch: the probe runs read-only and refuses every ask.
        self.assertEqual((started["approvalPolicy"], started["sandbox"]),
                         ("on-request", "read-only"))
        self.assertEqual(started["cwd"], self.scratch)
        self.assertIs(ghp.login_status("codex"), True)

    def test_a_guest_that_is_not_installed_is_answered_at_once(self):
        FakePath(self, install=())
        events = []
        thread = keytest.run("guest:claude", events.append, "req-3")
        self.assertIsNone(thread)                     # no thread: nothing to run
        self.assertEqual(events, [{"event": "key_tested", "id": "req-3", "preset": "guest:claude",
                                   "guest": "claude", "ok": False, "model": "", "elapsed_ms": 0,
                                   "error": "claude is not installed: no `claude` on PATH"}])
        events.clear()
        keytest.run("guest:codex", events.append, "req-4")
        self.assertEqual(events[0]["error"], "codex is not installed: no `codex` on PATH")

    def test_a_guest_that_is_not_logged_in_never_starts_a_turn(self):
        fakes = FakePath(self, claude_logged_in=False, codex_logged_in=False)
        for preset, name in (("guest:claude", "claude"), ("guest:codex", "codex")):
            events = []
            thread = keytest.run(preset, events.append, "req-5", cwd=self.scratch)
            thread.join(20)
            (event,) = wait_for(events)
            self.assertFalse(event["ok"])
            self.assertEqual(event["error"], f"{name} is not logged in: change login first")
            self.assertEqual(event["model"], "")
            self.assertIs(ghp.login_status(name), False)
        # Only the two status commands ran: no `-p`, no app-server, nothing spent.
        self.assertEqual(fakes.argv_log(), [["auth", "status", "--json"], ["login", "status"]])

    def test_a_guest_that_never_answers_is_timed_out_and_closed(self):
        fakes = FakePath(self)
        fakes.hang()
        events = []
        started = time.monotonic()
        thread = keytest.run("guest:claude", events.append, "req-6", cwd=self.scratch,
                             timeout_s=1.0)
        thread.join(20)
        (event,) = wait_for(events)
        self.assertFalse(event["ok"])
        self.assertEqual(event["error"], "claude timed out after 1 s")
        self.assertLess(time.monotonic() - started, 15)
        # The guest reported its model before it stalled, and that much is still said.
        self.assertEqual(event["model"], "claude-fake-1")

    def test_the_guest_answer_is_one_line_only(self):
        # Whatever the guest prints — a paragraph, a stack trace — the event carries one trimmed
        # line: the pane's status line has room for that and nothing more.
        self.assertEqual(keytest._one_line("\n\n  ok  \nand more\n"), "ok")
        self.assertEqual(keytest._one_line("x" * 500), "x" * 120)
        self.assertEqual(keytest._one_line(None), "")

    def test_an_unknown_guest_preset_is_still_unknown(self):
        with self.assertRaises(ValueError):
            keytest.run("guest:gemini", lambda e: None, "req-7")


class LoggedInRowTests(unittest.TestCase):
    """`logged_in` on the guest rows: the status commands run once, on the background scan."""

    def setUp(self):
        ghp.reset_catalog()
        ghp._detected = None
        self.addCleanup(ghp.reset_catalog)
        self.addCleanup(setattr, ghp, "_detected", None)

    def test_rows_say_null_until_the_scan_lands_then_what_each_cli_printed(self):
        fakes = FakePath(self, claude_logged_in=True, codex_logged_in=False)
        pushes = []
        ghp.set_catalog_listener(lambda: pushes.append("landed"))
        self.addCleanup(ghp.set_catalog_listener, None)
        real_read, threads = ghp._read_login_status, []

        def on_a_thread(guest_id, binary):
            threads.append(threading.current_thread().name)
            return real_read(guest_id, binary)

        with mock.patch.object(ghp, "_read_login_status", on_a_thread):
            rows = {row["id"]: row for row in ghp.preset_rows()}
            # Answered before either status command has run: `presets` never waits for one.
            self.assertEqual({k: v["logged_in"] for k, v in rows.items()},
                             {"guest:claude": None, "guest:codex": None})
            self.assertTrue(ghp.catalog_ready.wait(20))
        # Both ran on the one background scan, never on the thread that answered `presets`.
        self.assertEqual(threads, ["relay-guest-scan", "relay-guest-scan"])
        rows = {row["id"]: row for row in ghp.preset_rows()}
        self.assertIs(rows["guest:claude"]["logged_in"], True)
        self.assertIs(rows["guest:codex"]["logged_in"], False)
        self.assertEqual(pushes, ["landed"])
        # Once per worker process: another `presets` does not run the status commands again.
        ghp.preset_rows()
        statuses = [a for a in fakes.argv_log() if a in (["auth", "status", "--json"],
                                                          ["login", "status"])]
        self.assertEqual(sorted(map(tuple, statuses)),
                         [("auth", "status", "--json"), ("login", "status")])

    def test_a_guest_that_is_not_installed_stays_null(self):
        FakePath(self, install=("codex",))
        ghp.preset_rows()
        self.assertTrue(ghp.catalog_ready.wait(20))
        rows = {row["id"]: row for row in ghp.preset_rows()}
        self.assertIsNone(rows["guest:claude"]["logged_in"])
        self.assertIs(rows["guest:codex"]["logged_in"], True)


if __name__ == "__main__":
    unittest.main()
