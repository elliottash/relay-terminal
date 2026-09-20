#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Drive the real worker over the protocol against stub-provider.py (#2CZP).

    python3 drive-worker.py            # writes logs/worker-*.txt beside this file

No GUI and no network: the provider is stub-provider.py on 127.0.0.1, the worker runs as the
subprocess the pane would start, and the workspace, HOME and config are a throwaway directory.
RELAY_KEYRING=off, so the owner's real identity key is never touched.

Three runs:
  options   configure with no max_steps/max_tool_calls, and read what `configured` reports
            (agent.options() rides on that event, session_protocol.configured_fields)
  sweep     30 distinct reads in one turn - past the old 256/150 caps' reach for tool calls and
            past the RECITE_STEPS mark; expects recitation events and no stop
  loop      the same failing read for ever; expects loop_detected x3 and
            done {stop_reason: "limit", limit: {which: "loop"}}
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
LOGS = HERE / "logs"


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def run(workspace: Path, messages: list[dict], until=None, timeout: float = 240) -> list[dict]:
    """Send `messages` to a worker process and read its events until `until` says stop.

    Streaming rather than a batch of stdin: `ask` only queues the turn, so a `shutdown` written in
    the same breath would end the worker before the turn had taken a single step.
    """
    env = dict(os.environ, RELAY_KEYRING="off", HOME=str(workspace / "home"),
               XDG_CONFIG_HOME=str(workspace / "home/.config"),
               XDG_DATA_HOME=str(workspace / "home/.local/share"),
               RELAY_INDEX=str(workspace / "index.sqlite"))
    proc = subprocess.Popen([sys.executable, "-S", "-u", str(ROOT / "backend/worker.py")],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, cwd=str(workspace), env=env)
    events: list[dict] = []
    try:
        for message in messages:
            proc.stdin.write(json.dumps(message) + "\n")
        proc.stdin.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = proc.stdout.readline()
            if not line:
                break
            if not line.strip():
                continue
            event = json.loads(line)
            events.append(event)
            if until is None or until(event):
                break
        proc.stdin.write(json.dumps({"type": "shutdown"}) + "\n")
        proc.stdin.flush()
        proc.stdin.close()
        proc.wait(timeout=30)
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.stderr.read()
    return events


def configure(port: int, workspace: Path, **extra) -> dict:
    return {"type": "configure", "base_url": f"http://127.0.0.1:{port}/v1", "model": "stub",
            "api_key": "stub-key", "workspace": str(workspace), **extra}


def main() -> int:
    LOGS.mkdir(parents=True, exist_ok=True)
    port = free_port()
    stub = subprocess.Popen([sys.executable, str(HERE / "stub-provider.py"), str(port)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.0)
        with tempfile.TemporaryDirectory(prefix="relay-2czp-") as temp:
            workspace = Path(temp)
            (workspace / "home/.config").mkdir(parents=True)
            for i in range(40):
                (workspace / f"f{i}.txt").write_text(f"file {i}\n")

            # --- 1. the defaults the worker reports when the GUI stores nothing ----------------
            events = run(workspace, [configure(port, workspace)],
                         until=lambda e: e["event"] == "configured")
            options = next(e for e in events if e["event"] == "configured")
            (LOGS / "worker-options.txt").write_text(
                "configure with no max_steps / max_tool_calls in the request:\n\n"
                + json.dumps({k: options[k] for k in ("max_steps", "max_tool_calls")}, indent=2)
                + "\n\nfull configured event:\n" + json.dumps(options, indent=2) + "\n")
            print("options:", options["max_steps"], options["max_tool_calls"])

            # --- 2. a long turn: 30 distinct reads, a cadence recitation, no stop --------------
            events = run(workspace, [configure(port, workspace),
                                     {"type": "ask", "id": "a1",
                                      "text": "sweep please: read every file and tell me what changed"}],
                         until=lambda e: e["event"] in ("done", "error", "cancelled"))
            report(events, LOGS / "worker-sweep.txt", "sweep: 30 distinct reads in one turn")
            print("sweep: recitations =", sum(1 for e in events if e["event"] == "recitation"),
                  "tool results =", sum(1 for e in events if e["event"] == "tool_result"),
                  "loops =", sum(1 for e in events if e["event"] == "loop_detected"))

            # --- 3. the same failing call for ever: nudge, nudge, stop ------------------------
            events = run(workspace, [configure(port, workspace),
                                     {"type": "ask", "id": "a1", "text": "loop please: read that file"}],
                         until=lambda e: e["event"] in ("done", "error", "cancelled"))
            report(events, LOGS / "worker-loop.txt", "loop: the same failing read, for ever")
            print("loop: nudges =", [e["nudge"] for e in events if e["event"] == "loop_detected"],
                  "· stop =", json.dumps(next(e for e in events if e["event"] == "done").get("limit")))
    finally:
        stub.terminate()
    return 0


def report(events: list[dict], path: Path, title: str) -> None:
    """The events this card is about, plus every Relay note the turn put in the conversation."""
    keep = ("loop_detected", "loop_check", "recitation", "completion_check", "done", "error")
    lines = [title, "=" * len(title), ""]
    lines.append(f"tool_result events: {sum(1 for e in events if e['event'] == 'tool_result')}")
    steps = [e["text"] for e in events if e["event"] == "status" and e["text"].startswith("Requesting model")]
    lines.append(f"model steps: {len(steps)}  (last status: {steps[-1] if steps else '-'})")
    lines += ["", "events:"]
    for e in events:
        if e["event"] in keep:
            lines.append("  " + json.dumps({k: v for k, v in e.items() if k != "open_items"})[:600])
    path.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
