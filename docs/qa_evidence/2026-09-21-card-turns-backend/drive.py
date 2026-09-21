#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Drive `backend/worker.py` over NDJSON for card #CTRN's backend steps (1-3).

    python3 drive.py                       # the whole run, against this checkout
    python3 drive.py --tree <dir> --only discuss --out <dir>    # one Discuss, any checkout

No network, no keyring, no provider: the only endpoint that answers is `stub-provider.py` on
127.0.0.1, configured straight into the worker as a plain-HTTP loopback URL (which is a local
model server as far as `provider_config` is concerned, so no key is looked up).

`--tree` is what makes the thread-format check honest: the same driver runs against a clean
export of the commit *before* step 1 and against this checkout, and the two thread files are
normalised (entry ids, timestamps, session and turn ids) and diffed. `--only discuss` is for the
old tree, where a second prompt on a busy card is refused rather than queued.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

BOARD = """version: 1
tabs: [{id: features, folder: features}]
columns: [inbox, discussing, planning, planned, executing, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""

CARD = """---
id: {id}
type: work
status: inbox
labels: [feature]
rank: m
created: '2026-09-21'
source: 'fixture'
links: {{plans: [], commits: [], evidence: [], related: [], github: null}}
---
# {title}

## Issue

{issue}
"""

CARDS = {"CRD1": ("A card turn is an ordinary console turn", "the queue, on a card"),
         "CRD2": ("A Plan turn may not write files", "Execute is where code is written"),
         "CRD3": ("One card is stopped", "and the other goes on running"),
         "CRD4": ("The other card goes on running", "which is what Stop per card means")}


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Worker:
    """One `backend/worker.py` on a pipe, with every event kept as it arrives."""

    def __init__(self, tree: Path, env: dict, log: Path):
        self.events: list[dict] = []
        self.raw = log.open("w", encoding="utf-8")
        self.lock = threading.Condition()
        self.proc = subprocess.Popen(
            [sys.executable, "-u", str(tree / "backend" / "worker.py")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=(log.parent / (log.stem + ".stderr")).open("w"),
            env=env, text=True, bufsize=1)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            line = line.strip()
            if not line:
                continue
            self.raw.write(line + "\n")
            self.raw.flush()
            try:
                event = json.loads(line)
            except ValueError:
                continue
            with self.lock:
                self.events.append(event)
                self.lock.notify_all()

    def send(self, **request):
        self.proc.stdin.write(json.dumps(request) + "\n")
        self.proc.stdin.flush()

    def wait(self, pred, timeout=20.0, what=""):
        deadline = time.monotonic() + timeout
        with self.lock:
            while True:
                for event in self.events:
                    if pred(event):
                        return event
                left = deadline - time.monotonic()
                if left <= 0:
                    raise SystemExit(f"timed out waiting for {what or pred}")
                self.lock.wait(left)

    def of(self, name):
        with self.lock:
            return [e for e in self.events if e.get("event") == name]

    def close(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        self.proc.wait(timeout=10)
        self.raw.close()


def normalise(text: str) -> str:
    """A thread file with everything that is different every run taken out.

    What is left is exactly what the format check is about: the attribute *names*, their order,
    the entry bodies and the absence of a heading line.
    """
    import re
    text = re.sub(r"relay:entry \S+ ", "relay:entry <id> ", text)
    text = re.sub(r"turn=\S+", "turn=<session>/<turn>", text)
    text = re.sub(r"model=\S+", "model=<model>", text)
    return text


def fixture(scratch: Path) -> Path:
    proj = scratch / "proj"
    (proj / "issues" / "features").mkdir(parents=True)
    (proj / "issues" / "board.yaml").write_text(BOARD, encoding="utf-8")
    for card_id, (title, issue) in CARDS.items():
        (proj / "issues" / "features" / f"2026-09-21-{card_id.lower()}.md").write_text(
            CARD.format(id=card_id, title=title, issue=issue), encoding="utf-8")
    return proj


def report(out: Path, name: str, lines: list):
    (out / name).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", default=str(Path(__file__).resolve().parents[3]))
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent / "logs"))
    ap.add_argument("--only", choices=["discuss", "all"], default="all")
    ap.add_argument("--label", default="")
    args = ap.parse_args()
    tree, out = Path(args.tree).resolve(), Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    label = args.label or ("baseline" if args.only == "discuss" else "run")
    scratch = Path(tempfile.mkdtemp(prefix="ctrn-"))
    port = free_port()
    stub = subprocess.Popen([sys.executable, str(Path(__file__).resolve().parent / "stub-provider.py"),
                             str(port)], stderr=(out / f"{label}-stub.stderr").open("w"))
    said = []
    try:
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", port), 0.2).close()
                break
            except OSError:
                time.sleep(0.05)
        proj = fixture(scratch)
        env = dict(os.environ,
                   HOME=str(scratch / "home"), XDG_DATA_HOME=str(scratch / "home" / "data"),
                   XDG_CONFIG_HOME=str(scratch / "home" / "config"),
                   XDG_RUNTIME_DIR=str(scratch / "run"), TMPDIR=str(scratch / "tmp"),
                   RELAY_KEYRING="off", PYTHONPATH=str(tree / "backend"))
        for key in ("home", "run", "tmp"):
            (scratch / key).mkdir(parents=True, exist_ok=True)
        w = Worker(tree, env, out / f"{label}.ndjson")

        # 1. configure: a workspace and a tab id, which is what keys a card's conversation.
        w.send(type="configure", id="c1", workspace=str(proj), tab="tab-smoke",
               base_url=f"http://127.0.0.1:{port}/v1", model="stub-model", api_key="",
               board={"autonomy": "auto"})
        w.wait(lambda e: e.get("event") == "configured", what="configured")
        said.append(f"configured: workspace={proj} tab=tab-smoke model=stub-model")

        # 2. a Discuss on #CRD1, and a second prompt on the same card while it runs.
        w.send(type="board_ask", id="a1", card="CRD1", text="why this card? #CRD1")
        w.wait(lambda e: e.get("event") == "agent_started" and e.get("card_id") == "CRD1",
               what="the first turn on #CRD1")
        if args.only == "all":
            w.send(type="board_ask", id="a2", card="CRD1", text="and what about the queue? #CRD1")
            queued = w.wait(lambda e: e.get("event") == "queue_changed"
                            and e.get("surface") == "card:CRD1"
                            and any("the queue" in (r.get("preview") or "")
                                    for r in e.get("items") or []),
                            what="the second prompt queued on #CRD1")
            said.append("queued on #CRD1: " + json.dumps(
                {"surface": queued["surface"],
                 "items": [{k: r.get(k) for k in ("mode", "card_id", "surface", "preview")}
                           for r in queued["items"]]}))
            said.append("board_busy events: %d (it queued instead)"
                        % len([e for e in w.of("error") if e.get("code") == "board_busy"]))

            # 3. a Plan on #CRD2 while #CRD1 is still running: two cards at once.
            w.send(type="board_ask", id="a3", card="CRD2", mode="plan")
            w.wait(lambda e: e.get("event") == "agent_started" and e.get("card_id") == "CRD2",
                   what="the Plan turn on #CRD2")
            running = {e["card_id"] for e in w.of("agent_started")} - {
                e["card_id"] for e in w.of("agent_finished")}
            said.append("running at once: " + ", ".join(sorted(running)))

            # 4. the Plan calls write_file and is refused, in a sentence that names Execute.
            refused = w.wait(lambda e: e.get("event") == "tool_result"
                             and e.get("card_id") == "CRD2"
                             and (e.get("result") or {}).get("error"),
                             what="the refusal on #CRD2")
            said.append("write_file on a Plan turn: "
                        + " ".join(str(refused["result"]["error"]).split())[:300])

        w.wait(lambda e: e.get("event") == "agent_finished" and e.get("card_id") == "CRD1"
               and e.get("mode") == "discuss", what="the Discuss on #CRD1 to finish")
        w.wait(lambda e: e.get("event") == "board_thread_appended" and e.get("card_id") == "CRD1"
               and e.get("author") == "agent", what="the answer on #CRD1's thread")

        if args.only == "all":
            w.wait(lambda e: e.get("event") == "agent_finished" and e.get("card_id") == "CRD2",
                   what="the Plan on #CRD2 to finish")
            # 5. Stop, addressed to one card: the other goes on running.
            w.send(type="board_ask", id="a4", card="CRD3", mode="plan")
            w.send(type="board_ask", id="a5", card="CRD4", mode="plan")
            for card in ("CRD3", "CRD4"):
                w.wait(lambda e, c=card: e.get("event") == "agent_started" and e.get("card_id") == c,
                       what=f"the Plan on #{card}")
            w.send(type="cancel", id="x1", surface="card:CRD3")
            stopped = w.wait(lambda e: e.get("event") == "agent_finished"
                             and e.get("card_id") == "CRD3", what="#CRD3 to stop")
            said.append(f"cancel surface=card:CRD3 -> #CRD3 {stopped['outcome']}")
            kept = w.wait(lambda e: e.get("event") == "agent_finished" and e.get("card_id") == "CRD4",
                          what="#CRD4 to finish on its own")
            said.append(f"#CRD4 was untouched and ended {kept['outcome']}")
            said.append("#CRD3 wrote to its thread: %s"
                        % bool([e for e in w.of("board_thread_appended")
                                if e.get("card_id") == "CRD3" and e.get("author") == "agent"]))

        time.sleep(0.4)
        w.close()
        # Persistence (question 1 on the card): one conversation file per (tab, card), under
        # the helper store rather than the person's own sessions.
        sys.path.insert(0, str(tree / "backend"))
        from relay_core import agent_context as AC                      # the tree under test
        for card in sorted(CARDS):
            os.environ["XDG_DATA_HOME"] = str(scratch / "home" / "data")
            directory, name = AC.helper_file(str(proj), f"tab-smoke/card:{card}")
            path = Path(directory) / f"{name}.json"
            if path.exists():
                said.append(f"conversation for #{card}: helper-sessions/.../{name}.json "
                            f"({path.stat().st_size} bytes)")
        thread = (proj / "issues" / "threads" / "CRD1.md").read_text(encoding="utf-8")
        (out / f"{label}-thread-CRD1.md").write_text(thread, encoding="utf-8")
        (out / f"{label}-thread-CRD1.normalised.md").write_text(normalise(thread), encoding="utf-8")
        said.append(f"thread written: {label}-thread-CRD1.md ({len(thread)} bytes)")
        report(out, f"{label}-notes.txt", said)
    finally:
        stub.terminate()
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
