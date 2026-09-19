#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write guest-agent events in Relay's private per-session directory (GT7X, protocol 26.3).

Called as `guest-event.py <event> [guest] [sequence]` with the event's JSON on stdin. The
optional arguments are the shim's to pass: `guest` is the guest id ("claude"/"codex") and
`sequence` a uuid4 the shim generated when it must recognize the answer that comes back
(a PreToolUse permission decision); without them the helper picks its own fresh uuid4.

Atomically replaces `guest.json` in the pane's runtime dir with the protocol 26.3 envelope:

    {"token": "<pane token>", "sequence": "<fresh uuid4>", "event": "<name>",
     "guest": "claude|codex", "data": {}}

The pane polls the file exactly as it polls `state.json`: a new inode means a new event,
then a token check and a sequence check. `sequence` is a fresh uuid4 on every write, so two
events never share one.
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
    # The payload is whatever the caller could parse; an empty or malformed stdin is an
    # event with no data, never a crash that would pollute the guest's own output.
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
        # A missing environment means the caller is not in a Relay pane; a vanished runtime
        # dir means the pane closed. Either way: exit quietly, print nothing, write nowhere.
        pass
