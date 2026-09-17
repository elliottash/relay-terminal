#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in live scenarios for the request ledger (docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md section 7).

NOT run by ./scripts/test.sh or CI: it calls a real provider with a key from the keyring and costs money.
Deterministic stub-provider regressions live in tests/test_requests.py.

    scripts/eval-requests.py --preset openrouter --scenarios 1,2,3 --out /tmp/eval
    scripts/eval-requests.py --preset kimi --no-todos            # compare without the todo tool

Each scenario runs in a fresh temporary workspace through the real TurnSupervisor and Agent, with scripted
user actions (queue, steer, interrupt). Checks are deterministic (files on disk, ledger statuses). Output:
<out>/<preset>-scenario<N>.jsonl (worker events, no deltas) and <out>/<preset>-summary.json.
Keys are read from the keyring inside the provider config and never printed or written.
"""
from __future__ import annotations

import argparse
import json
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import keystore  # noqa: E402
from relay_core.agent import Agent  # noqa: E402
from relay_core.presets import PRESETS  # noqa: E402
from relay_core.provider import ProviderConfig  # noqa: E402
from relay_core.queue import TurnSupervisor  # noqa: E402

SKIP_EVENTS = {"delta", "thinking_delta", "tool_output", "context", "status"}


class Run:
    def __init__(self, preset_id: str, workspace: Path, log: Path, todo_tool: bool, **agent_kw):
        preset = PRESETS[preset_id]
        key = keystore.lookup(preset_id)
        if not key:
            raise SystemExit(f"No stored key for preset {preset_id}.")
        self.cond = threading.Condition()
        self.events: list[dict] = []
        self.log = open(log, "w", encoding="utf-8")
        self.sup = TurnSupervisor(self.emit)
        config = ProviderConfig(preset.base_url, preset.model, key, dict(preset.extra), 8192)
        self.agent = Agent(config, str(workspace), self.sup.agent_emit, preset_id=preset_id, todo_tool=todo_tool,
                           session_dir=str(workspace.parent / "sessions"), **agent_kw)
        self.sup.set_agent(self.agent)

    def emit(self, event: dict) -> None:
        with self.cond:
            self.events.append(event)
            self.cond.notify_all()
        if event.get("event") not in SKIP_EVENTS:
            self.log.write(json.dumps({"t": round(time.time(), 3), **event}, ensure_ascii=False) + "\n")
            self.log.flush()

    def wait(self, pred, timeout: float) -> dict:
        deadline = time.monotonic() + timeout
        with self.cond:
            while True:
                for event in self.events:
                    if pred(event):
                        return event
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError("scenario timed out")
                self.cond.wait(left)

    def idle(self, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            time.sleep(0.5)
            with self.sup._lock:
                if self.sup._running is None and not self.sup._queue and not self.sup._steer:
                    return
        raise TimeoutError("scenario timed out")

    def close(self):
        self.sup.shutdown(timeout=2)
        self.log.close()


def file_is(ws: Path, name: str, text: str) -> bool:
    path = ws / name
    return path.is_file() and path.read_text(errors="replace").strip() == text


def scenario1(run: Run, ws: Path, timeout: float) -> dict:
    run.sup.submit("Please do all five: 1) create a.txt containing alpha 2) create b.txt containing bravo "
                   "3) create c.txt containing charlie 4) create d.txt containing delta 5) create e.txt containing echo. "
                   "Each file holds only that word.", "now")
    run.idle(timeout)
    checks = {n: file_is(ws, f"{n}.txt", w) for n, w in zip("abcde", ["alpha", "bravo", "charlie", "delta", "echo"])}
    return {"asks": 5, "completed": sum(checks.values()), "checks": checks}


def scenario2(run: Run, ws: Path, timeout: float) -> dict:
    (ws / "old.txt").write_text("rename me\n")
    (ws / "README.md").write_text("# Demo\n")
    run.sup.submit("create fix.txt containing fixed, and also rename old.txt to new.txt, oh and add a line "
                   "saying updated at the end of README.md", "now")
    run.idle(timeout)
    checks = {"fix.txt": file_is(ws, "fix.txt", "fixed"),
              "renamed": (ws / "new.txt").is_file() and not (ws / "old.txt").exists(),
              "readme": (ws / "README.md").read_text().rstrip().endswith("updated")}
    return {"asks": 3, "completed": sum(checks.values()), "checks": checks}


def scenario3(run: Run, ws: Path, timeout: float) -> dict:
    run.sup.submit("Run the command `sleep 12 && echo finished > slow.txt` (timeout 60 seconds), then tell me "
                   "what slow.txt contains.", "now")
    run.wait(lambda e: e.get("event") == "tool_started", timeout)
    for n, word in ((1, "one"), (2, "two"), (3, "three")):
        run.sup.submit(f"also create s{n}.txt containing {word}", "steer")
        time.sleep(1.0)
    run.idle(timeout)
    checks = {"slow.txt": file_is(ws, "slow.txt", "finished"), **{f"s{n}.txt": file_is(ws, f"s{n}.txt", w)
              for n, w in ((1, "one"), (2, "two"), (3, "three"))}}
    return {"asks": 4, "completed": sum(checks.values()), "checks": checks}


def scenario4(run: Run, ws: Path, timeout: float) -> dict:
    run.sup.submit("First run `sleep 15` (timeout 60 seconds). After that, create long.txt with the numbers "
                   "1 to 5, one per line.", "now")
    run.wait(lambda e: e.get("event") == "tool_started", timeout)
    run.sup.submit("create quick.txt containing quick", "interrupt")
    run.sup.submit("now continue the earlier request that I interrupted (you may skip the sleep)", "queue")
    run.idle(timeout)
    long_ok = (ws / "long.txt").is_file() and (ws / "long.txt").read_text().split() == ["1", "2", "3", "4", "5"]
    checks = {"quick.txt": file_is(ws, "quick.txt", "quick"), "long.txt": long_ok}
    return {"asks": 2, "completed": sum(checks.values()), "checks": checks}


def scenario6(run: Run, ws: Path, timeout: float) -> dict:
    (ws / "tests").mkdir()
    (ws / "tests" / "keep.txt").write_text("do not touch\n")
    run.sup.submit("Constraint for this whole conversation: never edit or create anything under tests/. "
                   "Acknowledge briefly.", "now")
    for i in range(1, 9):
        run.sup.submit(f"Create note{i}.txt containing the output of `seq 1 400 | tr '\\n' ' '` (write it with "
                       "a shell command), then show me the first 3 lines of README-style summary of what you did.",
                       "queue")
    run.sup.submit("Create tests/extra.txt containing hi if the rules allow it; otherwise explain.", "queue")
    run.idle(timeout * 4)
    compactions = sum(1 for e in run.events if e.get("event") == "compacted")
    checks = {"constraint_kept": not (ws / "tests" / "extra.txt").exists()
              and (ws / "tests" / "keep.txt").read_text() == "do not touch\n",
              **{f"note{i}.txt": (ws / f"note{i}.txt").is_file() for i in range(1, 9)}}
    return {"asks": 9, "completed": sum(checks.values()), "checks": checks, "compactions": compactions}


SCENARIOS = {1: scenario1, 2: scenario2, 3: scenario3, 4: scenario4, 6: scenario6}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", default="openrouter", choices=sorted(PRESETS))
    parser.add_argument("--scenarios", default="1,2,3,4")
    parser.add_argument("--out", default=None)
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--no-todos", action="store_true")
    args = parser.parse_args()
    out = Path(args.out or tempfile.mkdtemp(prefix="relay-eval-"))
    out.mkdir(parents=True, exist_ok=True)
    summary = {"preset": args.preset, "model": PRESETS[args.preset].model, "todo_tool": not args.no_todos,
               "scenarios": {}}
    for number in [int(n) for n in args.scenarios.split(",")]:
        with tempfile.TemporaryDirectory(prefix="relay-eval-ws-") as temp:
            ws = Path(temp) / "ws"
            ws.mkdir()
            kw = {"context_window": 16_000} if number == 6 else {}
            run = Run(args.preset, ws, out / f"{args.preset}-scenario{number}.jsonl", not args.no_todos, **kw)
            started = time.monotonic()
            try:
                result = SCENARIOS[number](run, ws, args.timeout)
            except TimeoutError as exc:
                result = {"error": str(exc)}
            ledger = run.agent.requests.to_json()["items"]
            result.update({"elapsed_s": round(time.monotonic() - started, 1),
                           "ledger": [{"id": i["id"], "source": i["source"], "status": i["status"],
                                       "reason": i["reason"], "text": i["text"][:120]} for i in ledger],
                           "todos": run.agent.todos.items,
                           "completion_checks": sum(1 for e in run.events if e.get("event") == "completion_check"),
                           "limits": sum(1 for e in run.events if e.get("stop_reason") == "limit" and e.get("event") == "done"),
                           "silent_drops": sum(1 for i in ledger if i["status"] == "open" and i["requires_completion"])})
            run.close()
            summary["scenarios"][number] = result
            print(json.dumps({"scenario": number, **{k: v for k, v in result.items() if k != "ledger"}}, ensure_ascii=False))
    (out / f"{args.preset}-summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False))
    print(f"Wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
