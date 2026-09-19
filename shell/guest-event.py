#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one writer on Relay's guest event channel (GT7X, protocol 26.3).

Every surface that has something to tell a pane about its guest — the hooks and statusline shim
(`relay_core.guest_hook`), the Claude IDE bridge sidecar, the codex rollout tail — writes through
this file, so the spool's naming, its atomicity and its size cap exist once.

The channel is a **spool directory**, not a slot: `$RELAY_GUEST_EVENT` (or `guest-events/` under
`$RELAY_RUNTIME_DIR`), mode 0700, holding one file per event named

    <time_ns, 20 digits, zero-padded>-<pid>-<counter>.json

written with `mkstemp` in that directory and moved into place with `os.replace`. The names sort
into the order the events were written; the pane lists the directory each tick, handles the files
in that order and deletes each one. The single `guest.json` this replaced lost a permission
question to the statusline tick behind it and stranded the first of two parallel tool calls.

Each file holds one envelope:

    {"token": "<pane token>", "sequence": "<uuid4>", "event": "<name>",
     "guest": "claude|codex", "data": {}}

Two ways in, one implementation:

* **Imported** (`write_event`), which is what `relay_core.guest_hook` does — the shim runs twice
  per statusline tick, and spawning a second interpreter to write a small JSON file was half of
  its cost.
* **Run** as `guest-event.py <event> [guest] [sequence]` with the event's JSON on stdin, for a
  caller that is not Python. Without `sequence` the writer picks a fresh uuid4; a caller that must
  recognize the answer coming back (a permission question) passes its own.

The pane deletes an event file over 256 KiB unread, so an envelope that would exceed that is
written with its `data` replaced by `{"relay_truncated": true}`: a question the user can still
answer beats a file the pane refuses. A caller with a large payload should cut it down itself
first, with something more useful to show (`guest_hook._capped` does).

Nothing here ever raises at a caller: a missing environment means this is not a Relay pane, and a
vanished directory means the pane closed. Either way the guest's own run is untouched.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import time
import uuid

# The pane refuses an event file larger than this (Pane::kGuestEventMax).
MAX_EVENT_BYTES = 256 * 1024
EVENTS_DIR_NAME = "guest-events"

_counter = 0   # two events written in the same nanosecond still sort in the order they were made


def events_dir() -> Path | None:
    """Where this pane's spool is, or None when there is no pane around this process.

    `RELAY_GUEST_EVENT` is the spool directory; `RELAY_RUNTIME_DIR` is the fallback, and the
    spool is `guest-events/` inside it.
    """
    exported = os.environ.get("RELAY_GUEST_EVENT", "")
    if exported:
        return Path(exported)
    runtime = os.environ.get("RELAY_RUNTIME_DIR", "")
    return Path(runtime) / EVENTS_DIR_NAME if runtime else None


def write_event(event: str, guest: str = "", data=None, sequence: str | None = None,
                directory=None, token: str | None = None) -> str | None:
    """Put one event on the spool. Returns the envelope's sequence, or None when nothing was
    written — no pane, no token, or a directory that cannot be written."""
    global _counter
    spool = Path(directory) if directory is not None else events_dir()
    if token is None:
        token = os.environ.get("RELAY_SESSION_TOKEN", "")
    if spool is None or not token:
        return None
    sequence = (sequence or "").strip() or str(uuid.uuid4())
    if not isinstance(data, dict):
        data = {}
    envelope = {"token": token, "sequence": sequence, "event": event,
                "guest": guest, "data": data}
    body = _serialised(envelope)
    _counter += 1
    name = "%020d-%d-%d.json" % (time.time_ns(), os.getpid(), _counter)
    try:
        spool.mkdir(mode=0o700, parents=True, exist_ok=True)
        handle, temporary = tempfile.mkstemp(prefix=".relay-", dir=spool)
        try:
            with os.fdopen(handle, "w", encoding="utf-8") as output:
                output.write(body)
            os.replace(temporary, spool / name)
            temporary = ""
        finally:
            if temporary and os.path.exists(temporary):
                os.unlink(temporary)
    except (OSError, ValueError):
        return None
    return sequence


def _serialised(envelope: dict) -> str:
    """The envelope as the file holds it, small enough for the pane to read (26.3)."""
    try:
        body = json.dumps(envelope, ensure_ascii=False)
    except (TypeError, ValueError):
        body = ""
    if body and len(body.encode("utf-8")) <= MAX_EVENT_BYTES:
        return body
    envelope = dict(envelope, data={"relay_truncated": True})
    return json.dumps(envelope, ensure_ascii=False)


def main(argv=None) -> int:
    """Exit 0 when the event was written *or* when there is no pane to write it to; 1 when a pane
    was named and the write failed.

    Both halves of that are contracts. The no-op invariant (26.3) is the first: a shim with no
    pane around it exits 0, prints nothing and writes nowhere, because hooks installed in a user's
    global settings run in every other terminal too. The second is the bridge's (26.5): "a helper
    that is missing, fails, or has no runtime dir to write is a failed emit — the event is not sent,
    and `openDiff` answers `DIFF_REJECTED`". The caller can only tell those apart from the exit
    code, and this returned 0 either way — so a spool that could not be written (a full disk, a
    runtime dir whose mode changed) read as a delivered event and left the guest blocked on a
    decision no pane would ever be shown.
    """
    args = list(sys.argv[1:] if argv is None else argv)
    if not args:
        return 0
    # No pane around this process: not a failure, the whole point of the invariant. Asked before
    # stdin is read, so a hook in a plain terminal does not even block on a pipe nobody fills.
    if events_dir() is None or not os.environ.get("RELAY_SESSION_TOKEN", ""):
        return 0
    # The payload is whatever the caller could parse; an empty or malformed stdin is an event
    # with no data, never a crash that would pollute the guest's own output. More than the pane
    # will read is an event that says so, rather than a silently empty one.
    raw = sys.stdin.read(MAX_EVENT_BYTES + 1)
    try:
        data = json.loads(raw or "null")
    except ValueError:
        data = {"relay_truncated": True} if len(raw) > MAX_EVENT_BYTES else None
    written = write_event(args[0], args[1] if len(args) > 1 else "", data,
                          args[2] if len(args) > 2 else None)
    return 0 if written else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, IndexError):
        # A missing environment means the caller is not in a Relay pane; a vanished runtime
        # dir means the pane closed. Either way: exit quietly, print nothing, write nowhere.
        sys.exit(0)
