#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The guest event channel helper, as `tests/test_guest_codex.py` needs it (GT7X).

This is a fixture, not the shipped helper: `shell/guest-event.py` belongs to the hooks phase and
is written by the GUI. It exists here so the Codex phase's tests can exercise the channel end to
end — a real child process, a real file, a real token check — without depending on another
phase's branch. `tests/test_guest_codex.py` prefers the real `shell/guest-event.py` as soon as it
is in the tree, so this copy stops being used (and can be deleted) the moment that lands.

Called as `guest-event.py <event> [guest] [sequence]` with the event's JSON on stdin, it
atomically replaces `guest.json` in the pane's runtime dir with the protocol 26.3 envelope:

    {"token": "<pane token>", "sequence": "<fresh uuid4>", "event": "<name>",
     "guest": "claude|codex", "data": {}}
"""
import json
import os
from pathlib import Path
import sys
import tempfile
import uuid


def main():
    runtime = Path(os.environ["RELAY_RUNTIME_DIR"])
    event_name = sys.argv[1]
    guest = sys.argv[2] if len(sys.argv) > 2 else ""
    sequence = (sys.argv[3] if len(sys.argv) > 3 else "").strip() or str(uuid.uuid4())
    try:
        data = json.loads(sys.stdin.read(1024 * 1024) or "null")
    except ValueError:
        data = None
    if not isinstance(data, dict):
        data = {}
    event = {"token": os.environ["RELAY_SESSION_TOKEN"], "sequence": sequence,
             "event": event_name, "guest": guest, "data": data}
    fd, temporary = tempfile.mkstemp(prefix="guest-", dir=runtime)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            json.dump(event, output, ensure_ascii=False)
        os.replace(temporary, runtime / "guest.json")
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, IndexError):
        # No pane environment, or a runtime dir that vanished with the pane: exit quietly.
        pass
