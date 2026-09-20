#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""#AQ6X phase 3, live: a failing test nobody is on works itself, and the check has the last word.

Nothing here is stubbed except, optionally, the model.  The pieces are Relay's own:

  * a real board (`.switchboard/board.yaml`, with `signals: {auto_work: true}`),
  * a real run history written by `relay_core.test_history`,
  * a real `SubagentManager` + `SubagentFactory`, on the `signal` definition from
    `agents_defs.BUILTINS`, with the real `Agent`, the real tool executor and the real workspace
    guard,
  * a real `TestsCommands`, folding and picking up exactly as the board worker does — the spawn it
    is handed is the body of `board_protocol._spawn_signal_thread`, the same call,
  * a real test run: `verify_signal` drives `relay_core.junit_runner` over the project's own
    `test_width.py`, which is why the key is a `unittest:` one — it is the runner the worker can
    drive in a plain directory, with no cmake and no build tree.

    docs/qa_evidence/2026-09-20-signal-threads/loop.py [seconds-to-wait]   # the local model
    docs/qa_evidence/2026-09-20-signal-threads/loop.py --stub              # a scripted agent

`--stub` replaces the *model* and nothing else, with four scripted steps through the real Agent and
the real tools: look, fix, run the test, report.  It is there because the model served on this
machine is a reasoning model at about half a token a second on this host — a live agentic fix takes
four or five minutes, which is longer than a QA run should be, and the thing being proved is the
pickup rather than the model.  Both modes ran; see README.md.
"""
import json
import os
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import agents_defs, signal_threads as ST, signals as S      # noqa: E402
from relay_core import test_history as H, tests_protocol as TP              # noqa: E402
from relay_core.provider import ProviderConfig                              # noqa: E402
from relay_core.subagents import SubagentFactory, SubagentManager           # noqa: E402

# `TestsCommands` runs a unittest key as `python3 -m relay_core.junit_runner`, and puts
# `<project>/backend` on the child's PYTHONPATH — which a throwaway project does not have. Relay's
# own backend goes on this process's, and the run inherits it.
os.environ["PYTHONPATH"] = str(ROOT / "backend") + (
    ":" + os.environ["PYTHONPATH"] if os.environ.get("PYTHONPATH") else "")

STUB = "--stub" in sys.argv[1:]
WAIT = next((float(a) for a in sys.argv[1:] if not a.startswith("-")), 360.0)
LOCAL = "http://127.0.0.1:8080/v1"

#: The key the whole run is about. A `unittest:` key on purpose: it is the runner the worker can
#: drive in a plain directory, so `verify_signal` really runs the check rather than being skipped —
#: which is half of what this run exists to prove.
KEY = "unittest:test_width.WidthTests.test_three"

BOARD_YAML = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, executing, needs-verification, done]
agent: {autonomy: auto}
signals: {auto_work: true}
"""

TEST_FILE = '''\
# The project's one test. It passes when width.txt says 3.
import unittest
from pathlib import Path


class WidthTests(unittest.TestCase):
    def test_three(self):
        got = (Path(__file__).parent / "width.txt").read_text().strip()
        self.assertEqual(got, "3", "width.txt says %r, expected '3'" % got)
'''

FAILURE = "AssertionError: width.txt says '7', expected '3'"


def _call(name, args, cid):
    return {"id": cid, "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}


class ScriptedProvider:
    """Four steps through the real tools: look at the fault, fix it, run the test, report.

    A provider needs exactly `complete(messages, tools, emit, cancel)` and `cancel()`. Everything
    else about this run — the Agent, the tool executor, the workspace guard, the thread file — is
    Relay's own.
    """

    def __init__(self):
        self.calls = 0

    def cancel(self):
        pass

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        if self.calls == 1:
            return {"role": "assistant", "content": "",
                    "tool_calls": [_call("run_command",
                                         {"command": "cat width.txt test_width.py"}, "c1")]}
        if self.calls == 2:
            return {"role": "assistant", "content": "The test wants 3 and the file says 7.",
                    "tool_calls": [_call("write_file",
                                         {"path": "width.txt", "content": "3\n"}, "c2")]}
        if self.calls == 3:
            return {"role": "assistant", "content": "",
                    "tool_calls": [_call("run_command",
                                         {"command": "python3 -m unittest test_width 2>&1 | tail -2"},
                                         "c3")]}
        emit({"event": "delta", "text": "Fixed: width.txt said 7, the test wants 3."})
        return {"role": "assistant", "content": "Fixed: width.txt said 7, the test wants 3."}


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="aq6x-loop-", dir="/tmp"))
    project = tmp / "proj"
    board = project / ".switchboard"
    for name in ("changes", "threads", "features"):
        (board / name).mkdir(parents=True)
    (board / "board.yaml").write_text(BOARD_YAML, encoding="utf-8")
    (project / "test_width.py").write_text(TEST_FILE, encoding="utf-8")
    (project / "width.txt").write_text("7\n", encoding="utf-8")        # the fault

    events: list[dict] = []

    def emit(event: dict) -> None:
        events.append(event)
        if event.get("event") in ("signal_thread", "signals_written"):
            print("EVENT " + json.dumps(event, sort_keys=True), flush=True)

    commands = TP.TestsCommands(project, board, emit)

    config = ProviderConfig(base_url=LOCAL, api_key="", model="bonsai-2-27b")
    manager = SubagentManager(emit)
    scripted = ScriptedProvider() if STUB else None
    manager.configure(agents_defs.load_catalog(str(project)),
                      SubagentFactory(config, str(project),
                                      provider_factory=(lambda c: scripted) if STUB else None))

    def spawn(task: str, description: str):
        # `board_protocol._spawn_signal_thread`, verbatim.
        sub = manager.spawn({"subagent_type": ST.THREAD_AGENT_TYPE, "background": False,
                             "description": description, "prompt": task}, signal=description)
        return sub.thread_id, sub.id, sub.owner_session or "", sub.done

    commands.spawn_agent = spawn

    # Two failing executions of one key: the fold opens the signal (decision 3).
    stamp = lambda ago: time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() - ago))  # noqa: E731
    H.append([H.Execution(ts=stamp(600), id=KEY, result="fail", run_id="r1", runner="unittest",
                          duration=0.01, message=FAILURE,
                          excerpt=FAILURE + "\nThe project's one test is test_width.py."),
              H.Execution(ts=stamp(300), id=KEY, result="fail", run_id="r2", runner="unittest",
                          duration=0.01, message=FAILURE, excerpt=FAILURE)],
             commands.store_path())

    print("== fold 1: the signal opens, and the pane whose run opened it has this fold to claim it")
    state = commands.fold_signals()
    one = state[KEY]
    print(f"   {one.key} {one.state} {one.kind} x{one.count} session={one.session!r}")
    threads = commands.signal_threads()
    print(f"   threads running: {threads.count()}")

    print("== fold 2: nobody claimed it, so Relay does")
    commands.fold_signals()
    print(f"   threads running: {threads.count()}")
    if threads.count() == 0:
        print("   NO THREAD STARTED — is the model at %s answering?" % LOCAL)
        print(json.dumps(events[-3:], indent=2))
        return 1
    thread = threads.running()[0]
    state = commands.signal_state()
    print(f"   claim: {state[KEY].session!r} (the thread's own id)")
    print(f"   thread_id={thread.thread_id} agent={thread.agent_id} owner={thread.session_id!r}")
    print("== the task it was started with")
    print(ST.task_text(state[KEY], project=str(project), board_folder=".switchboard",
                       thread_id=thread.thread_id))

    print(f"== waiting up to {WAIT:.0f}s for the thread's agent to stop")
    started = time.time()
    sub = next(s for s in manager._agents.values() if s.thread_id == thread.thread_id)
    while not sub.done.is_set() and time.time() - started < WAIT:
        time.sleep(2.0)
    if not sub.done.is_set():
        print("   the wait expired with it still working: stopping it, as the worker does at "
              "shutdown")
        manager.stop(sub.id)
        sub.done.wait(30)
    print(f"   subagent status={sub.status} after {time.time() - started:.0f}s")
    print(f"   its report: {(sub.result or '')[:300]!r}")
    print(f"   width.txt is now {(project / 'width.txt').read_text().strip()!r}")

    # Nothing the thread ran was recorded: it was a subprocess in its own shell, and the history has
    # seen nothing since the failure that opened the signal. So the *worker* runs the key — that is
    # `verify_signal`, and it is the half of this the first run of this script found missing — and
    # it does it by itself, on the watcher thread `_spawn_signal_thread` armed on `sub.done`.
    # Nothing below asks for it.
    print("== the worker finishes the thread by itself: it runs the check, and the check decides")
    for _ in range(60):
        if any(e.get("event") == "signal_thread" and e.get("state") == "finished" for e in events):
            break
        time.sleep(1.0)
    for row in H.read(commands.store_path()):
        if row.id == KEY:
            print(f"     {row.run_id:30} {row.result}")
    final = commands.signal_state().get(KEY)
    print(f"   signal is now {final.state} card={final.card!r} session={final.session!r} "
          f"green_streak={final.green_streak}")

    print("== the signal log")
    for line in S.read_events(commands.signals_path()):
        print("   " + json.dumps(line, sort_keys=True))
    print("== the thread file (what the Sessions manager and the ⓘ view read)")
    data = manager.thread_data(sub)
    print("   " + json.dumps({k: data[k] for k in ("id", "type", "title", "description", "signal",
                                                   "status", "model", "runs") if k in data},
                             sort_keys=True))
    print("== every signal_thread event, in order")
    for event in events:
        if event.get("event") == "signal_thread":
            print("   " + json.dumps(event, sort_keys=True))
    print("== the board's bugs tab")
    cards = sorted((board / "changes").glob("*.md"))
    print(f"   {len(cards)} card(s): {[c.name for c in cards]}")

    print(f"\nagent: {'scripted provider (4 steps, real tools)' if STUB else 'bonsai-2-27b at ' + LOCAL}")
    print(f"project kept at {project}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
