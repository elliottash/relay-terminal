#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The #H6VQ fault and its fix, through the real worker, with a GUI made of ten lines (§30.3).

    python3 repro.py before                # the GUI as it was: it matched `type`, so nothing
                                           # answered and the tool sat out its 20-second deadline
    python3 repro.py before --tab-close    # …and the owner's window: the tab closed at 5 s, so
                                           # the turn never answered at all
    python3 repro.py after                 # the GUI as it is: it matches `event`

Relay's GUI answers a helper's `app_command` in `RelayWindow::boardWorker`'s `onEvent`.  A worker
names its events in **`event`**; that read said **`type`**, which is what the GUI calls the
messages it sends *down*.  So the branch never matched and no `app_command` from a tab's helper
was ever answered — the owner's report, 2026-09-20: "sessions helper didn't do anything when I
asked to open a group of previous sessions in new panes."

Everything here is real except the window: the worker is `backend/worker.py`, the provider is the
loopback stub beside this file, the conversation index is the isolated HOME's own.  The two modes
differ in exactly one line — which key the fake GUI reads the event's name out of — so the
difference in the output is the bug and nothing else.

`before` also does what the owner's window did next: with the call still hanging, the tab closed,
which stopped the worker (`shutdown`).  That is the `ok=False ms=5358` beside `worker_stop` in
his worker.log, and it is why the turn never answered at all.
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PORT = int(os.environ.get("RELAY_QA_PORT", "8843"))

SESSIONS = [("11111111111111111111111111111111", "Splitting the pane layout"),
            ("22222222222222222222222222222222", "The pane header and its labels"),
            ("33333333333333333333333333333333", "Pane drag and the equalize key")]


def seed(home: Path) -> Path:
    """Three saved conversations in the isolated data root, as the pane's store writes them.

    Under the workspace-digest folder `SessionStore` uses — `sessions/<digest>/<id>.json` — which
    is the only shape `conv_index.reconcile()` walks (`session_folders`).
    """
    sys.path.insert(0, str(ROOT / "backend"))
    os.environ["XDG_DATA_HOME"] = str(home / ".local/share")
    from relay_core import sessions as S
    directory = S.default_session_dir(home / "project")
    directory.mkdir(parents=True, exist_ok=True)
    for index, (session_id, title) in enumerate(SESSIONS):
        data = {"version": 1, "kind": "relay_session", "id": session_id, "title": title,
                "created": 1000.0, "updated": time.time() - 60 + index,
                "workspace": str(home / "project"),
                "model": "stub", "preset": "local:stub", "effort": "high", "mode": "build",
                "turns": 1, "epoch": 0,
                "messages": [{"role": "user", "content": f"about the pane: {title}"},
                             {"role": "assistant", "content": "answered"}],
                "snapshots": {}, "checkpoints": {"items": [
                    {"turn": 1, "prompt": f"about the pane: {title}", "prompt_preview": "about",
                     "time": 1000.0, "locations": {"0": 1}, "files": {}}]},
                "requests": {"items": []}, "todos": {"items": []}, "plan_path": None,
                "open_requests": 0}
        (directory / f"{session_id}.json").write_text(json.dumps(data), encoding="utf-8")
    return directory


APP_BLOCK = {"tab": "tab-1", "writes_enabled": True,
             "options": [{"id": "terminal.copy_on_select", "section": "terminal",
                          "section_label": "Terminal", "label": "Copy on select",
                          "kind": "toggle", "value": False, "settable": True}],
             "actions": [{"key": "app.settings", "section": "Relay", "label": "Open settings",
                          "agent_safe": True}]}


def main(mode: str, tab_close: bool = False) -> int:
    if mode not in ("before", "after"):
        print(__doc__)
        return 2
    home = Path(tempfile.mkdtemp(prefix="h6vq-"))
    seed(home)
    project = home / "project"
    project.mkdir(exist_ok=True)
    env = {**os.environ, "HOME": str(home), "XDG_DATA_HOME": str(home / ".local/share"),
           "XDG_CONFIG_HOME": str(home / ".config"), "XDG_CACHE_HOME": str(home / ".cache"),
           "TMPDIR": str(home / "tmp"), "RELAY_KEYRING": "off",
           "PYTHONPATH": str(ROOT / "backend"), "RELAY_PANE_ID": "switchboard"}
    (home / "tmp").mkdir(exist_ok=True)

    stub = subprocess.Popen([sys.executable, str(HERE / "stub-provider.py"), str(PORT),
                             *[s[0] for s in SESSIONS]],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    worker = subprocess.Popen([sys.executable, "-u", str(ROOT / "backend/worker.py")],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env, text=True)

    lock = threading.Lock()

    def send(message):
        with lock:
            worker.stdin.write(json.dumps(message) + "\n")
            worker.stdin.flush()

    answered, opened, finished = [], [], threading.Event()
    started = time.time()

    def read():
        for line in worker.stdout:
            try:
                event = json.loads(line)
            except ValueError:
                continue
            name = event.get("event")
            # ---- the window, in one branch ------------------------------------------------
            # `before` is the shipped read: the event's name out of `type`, which a worker never
            # sets. `after` is the fix: out of `event`.
            key = "type" if mode == "before" else "event"
            if event.get(key) == "app_command":
                answered.append(event)
                print(f"  [gui] answering {event['command']} "
                      f"{event.get('target', '')} {event.get('conversation', '')}")
                if event.get("target") == "conversation":
                    opened.append(event.get("conversation"))
                send({"type": "app_command_result", "id": event["id"], "ok": True})
                continue
            if name == "tool_result" and str(event.get("tool", "")).startswith("app_"):
                result = event.get("result") or {}
                ok = "error" not in result
                print(f"  [tool] {event['tool']} ok={ok} "
                      f"{result.get('text') or result.get('error')}")
            if name == "delta" and event.get("chat"):
                print(f"  [panel] {event.get('text')!r}")
            if name in ("done", "error", "cancelled") and event.get("chat"):
                print(f"  [turn ] {name} after {time.time() - started:.1f}s")
                finished.set()

    threading.Thread(target=read, daemon=True).start()

    send({"type": "configure", "workspace": str(project), "agent_role": "switchboard",
          "use_stored_key": False, "api_key": "stub", "tab": "tab-1",
          "base_url": f"http://127.0.0.1:{PORT}/v1", "model": "stub", "app": APP_BLOCK})
    time.sleep(2)
    # What the Sessions pane itself sends when it opens (protocol 14). It is also what brings the
    # seeded files into the shared conversation index — `session_protocol.index()` reconciles on
    # first use — so the helper's search and open see the same three rows a person would.
    send({"type": "conversations", "id": "conv-list", "query": "", "workspace": str(project),
          "scope": "all"})
    time.sleep(2)
    print(f"--- {mode}: the Sessions helper is asked to open three conversations in new panes")
    send({"type": "board_chat", "id": "r1", "pane": "sessions",
          "text": "open the three sessions about panes in new panes"})

    if mode == "before" and tab_close:
        # The owner's window, five seconds later: the tab the helper was asked in closed, which
        # is what stops a tab's helper (§30.7). The call is still hanging, and the turn never
        # answers at all — `app_open … ok=False ms=5358` in the same millisecond as `worker_stop`.
        time.sleep(6)
        print("  [gui] the tab closed: the helper worker is stopped with the call still in flight")
        send({"type": "shutdown"})
    finished.wait(40)
    time.sleep(1)
    try:
        worker.stdin.close()
    except OSError:
        pass
    worker.wait(10)
    stub.terminate()
    print(f"--- {mode}: app_commands answered = {len(answered)}, "
          f"conversations opened = {opened}")
    # The worker's own log, which is where the owner's report was read from: the same two lines
    # in the same millisecond when the call was still in flight as the worker went.
    log = home / ".local/share/relay/logs/worker.log"
    if log.is_file():
        print("--- worker.log")
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            if "app_open" in line or "worker_stop" in line or "app_sessions_search" in line:
                print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "", "--tab-close" in sys.argv))
