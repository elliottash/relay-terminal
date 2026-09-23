#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write atomic shell-state events in Relay's private per-session directory."""
import base64
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time


def main():
    runtime = Path(os.environ["RELAY_RUNTIME_DIR"])
    stage = sys.argv[1]
    status = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    cwd = sys.argv[3] if len(sys.argv) > 3 else os.getcwd()
    pid = int(sys.argv[4]) if len(sys.argv) > 4 else os.getppid()
    event = {"token": os.environ["RELAY_SESSION_TOKEN"], "sequence": str(time.time_ns()),
             "event": stage, "status": status, "cwd": cwd, "shell_pid": pid}
    if stage == "running":
        # Text comes from shell acceptance/preexec, never BASH_COMMAND or history.
        command = sys.argv[5] if len(sys.argv) > 5 else ""
        encode = lambda value: base64.b64encode(value.encode("utf-8")).decode("ascii")
        sys.stdout.write(f"\033]777;notify;relay-command;{event['token']};{encode(command)};{encode(cwd)}\007")
        sys.stdout.flush()
    if stage == "ready":
        event["known_commands"] = [s for s in sys.stdin.read(131072).splitlines() if not s.startswith("__relay_")][:20000]
        event["path"] = os.environ.get("PATH", os.defpath)
    elif stage == "loaded":
        data = (runtime / "input.txt").read_bytes()
        event["input_sha256"] = hashlib.sha256(data).hexdigest()
    fd, temporary = tempfile.mkstemp(prefix="state-", dir=runtime)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            json.dump(event, output, ensure_ascii=False)
        os.replace(temporary, runtime / "state.json")
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError):
        # Closing the window removes the runtime directory. Never pollute the PTY.
        pass
