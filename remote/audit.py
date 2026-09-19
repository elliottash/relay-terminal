# SPDX-License-Identifier: AGPL-3.0-or-later
"""The local-only audit log for remote sharing (docs/REMOTE-PROTOCOL.md section 10).

Everything a phone can do to a shared pane lands here: pairings, joins, role changes, control
handoffs, prompts, lines and password-field use. It is the record the owner reads after the fact,
so it is written before the action, never uploaded, 0600 inside the 0700 state directory, and
size-capped: a shared pane that runs for a month must not produce a file nobody can open.

What each kind may carry is normative (section 10): prompt and line events record their text,
raw keys record byte counts, and password events record that one happened and nothing else.
"""
from __future__ import annotations

import json
import os
import time
from pathlib import Path

MAX_BYTES = 5 * 1024 * 1024          # rotate the month's file rather than grow past this


def state_dir() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    path = Path(base) / "relay" / "remote"
    path.mkdir(parents=True, exist_ok=True)
    path.chmod(0o700)
    return path


class AuditLog:
    def __init__(self, directory: Path | None = None):
        self.directory = directory or state_dir()

    def path_for(self, stamp: str) -> Path:
        """The month's file, or the first numbered part of it that still has room.

        A month that outgrows one file goes on in ``audit-YYYY-MM.2.jsonl``, ``.3`` and so on.
        Nothing is rotated away: this is the record of what other people did to a pane, so it is
        split, never trimmed. Each part is capped, so the number of files grows with the volume,
        one per MAX_BYTES. (Naming the overflow after the current second, as this first did, made
        one new file per second for the rest of the month once the cap was reached.)
        """
        part = 1
        while True:
            name = f"audit-{stamp}.jsonl" if part == 1 else f"audit-{stamp}.{part}.jsonl"
            path = self.directory / name
            if not path.exists() or path.stat().st_size < MAX_BYTES:
                return path
            part += 1

    def record(self, kind: str, **fields) -> None:
        """Append one line. Content beyond what section 10 allows is a bug in the caller."""
        path = self.path_for(time.strftime("%Y-%m"))
        line = json.dumps({"at": round(time.time(), 3), "kind": kind, **fields},
                          separators=(",", ":"), ensure_ascii=False)
        # 0600 from the moment the file exists, rather than chmod-ed once the first line is in
        # it: between the two the file is whatever the umask says, and the first line of an audit
        # log is a pairing or a knock. `remote/guests.py` opens `guests.json` the same way and
        # for the same reason. The chmod stays for a file this process did not create.
        handle = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
        with os.fdopen(handle, "a", encoding="utf-8") as out:
            out.write(line + "\n")
        path.chmod(0o600)
