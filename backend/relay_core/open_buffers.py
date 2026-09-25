# SPDX-License-Identifier: AGPL-3.0-or-later
"""Files the user has open in Relay's editor, and the round trip that edits them there (card #F8R7).

The native agent's `read_file`, `write_file` and `edit_file` used to work on the disk alone. When
the same file is open in a preview pane, a write behind the editor's back is at best a reload the
user did not ask for and at worst a conflict bar over their unsaved typing. So the GUI tells this
worker which files are open, and a write to one of them goes *through* the editor: the GUI applies
it to the buffer as one named undo step, three-way merges it with unsaved edits
(src/TextMerge.h), and refuses — with the lines in question — when the two overlap.

What the GUI sends (protocol §35):

* `open_buffers {files: [{path, sha256, dirty}]}` — every text file open in the window this
  worker's pane is in, replacing the last list. `path` is an absolute local path or
  `ssh://host/abs/path`; `sha256` is the hex hash of the revision the buffer was loaded from, or
  last merged or saved against; `dirty` says it has unsaved edits. Having sent one at all is what
  tells this worker the GUI answers `buffer_request`.
* `buffer_result {id, ok, …}` — the answer to one `buffer_request`.

What this worker sends: `buffer_request {id, op, path, …}` (op `read` or `patch`), and it waits
`LOCAL_TIMEOUT` seconds for the answer (`REMOTE_TIMEOUT` for a patch to a file on an ssh host,
whose save the answer waits for). No answer, or `not_open`, means the tool does what it always
did: the disk, guarded by the revision it read.
"""
from __future__ import annotations

import os
import posixpath
import threading
import time
from typing import Callable

#: A local round trip: the GUI answers from memory, so a few seconds means it is not going to.
LOCAL_TIMEOUT = 3.0
#: A patch to a clean buffer on an ssh host is answered once the save to the host has landed or
#: failed, and the GUI's own ssh deadline is 30 s (src/RemoteFiles.cpp).
REMOTE_TIMEOUT = 40.0
#: More than any window has open; a longer list is cut rather than trusted.
MAX_FILES = 500


def local_key(path: str | os.PathLike) -> str:
    """How a local path is looked up: absolute, symlinks resolved, on both sides of the match."""
    return os.path.realpath(os.fspath(path))


def remote_key(host: str, path: str, cwd: str | None = None) -> str | None:
    """`ssh://host/abs/path` for a path on the ssh host, or None when it cannot be made absolute
    (a relative path and no known remote directory) — such a write simply goes to the host."""
    if not host or not isinstance(path, str) or not path:
        return None
    if not path.startswith("/"):
        if not cwd or not cwd.startswith("/"):
            return None
        path = posixpath.join(cwd, path)
    return f"ssh://{host}{posixpath.normpath(path)}"


class OpenBuffers:
    """One per worker: the open-file list and the `buffer_request` round trip.

    `app_tools.AppBridge` is the shape — a request with an id goes out as an event, the protocol
    thread hands the answer in, and the turn thread that is waiting wakes. Stop ends the wait.
    """

    KINDS = frozenset({"open_buffers", "buffer_result"})

    def __init__(self, emit: Callable[[dict], None], cancel: threading.Event | None = None,
                 clock: Callable[[], float] = time.monotonic,
                 local_timeout: float = LOCAL_TIMEOUT, remote_timeout: float = REMOTE_TIMEOUT):
        self.emit = emit
        self.cancel = cancel
        self.clock = clock
        self.local_timeout = local_timeout
        self.remote_timeout = remote_timeout
        self._lock = threading.Lock()
        self._files: dict[str, dict] = {}
        self._pending: dict[str, list] = {}
        self._next = 0

    @staticmethod
    def handles(kind) -> bool:
        return kind in OpenBuffers.KINDS

    def dispatch(self, request: dict) -> None:
        kind = request.get("type")
        if kind == "open_buffers":
            self.update(request.get("files"))
        elif kind == "buffer_result":
            self.answer(request)

    # ----- the list -----------------------------------------------------------------------

    def update(self, files) -> int:
        """Replace the list. Entries that are not well formed are dropped, never guessed at."""
        if not isinstance(files, list):
            raise ValueError("open_buffers needs a files list.")
        table: dict[str, dict] = {}
        for item in files[:MAX_FILES]:
            if not isinstance(item, dict):
                continue
            path, sha = item.get("path"), item.get("sha256")
            if not isinstance(path, str) or not path or not isinstance(sha, str):
                continue
            if path.startswith("ssh://"):
                key = path
            elif os.path.isabs(path):
                key = local_key(path)
            else:
                continue
            table[key] = {"path": path, "sha256": sha, "dirty": item.get("dirty") is True}
        with self._lock:
            self._files = table
        return len(table)

    def entry(self, key: str | None) -> dict | None:
        """The open file under this key (`local_key` / `remote_key`), or None."""
        if not key:
            return None
        with self._lock:
            found = self._files.get(key)
            return dict(found) if found else None

    # ----- the round trip ------------------------------------------------------------------

    def request(self, fields: dict, *, remote: bool = False) -> dict | None:
        """Emit one `buffer_request` and wait for its `buffer_result`.

        Returns the GUI's answer, or None when nothing answered in time — the caller then does
        what it would have done with no editor open. Stop raises `Cancelled`, as every wait does.
        """
        with self._lock:
            self._next += 1
            request_id = f"br-{self._next}"
            done = threading.Event()
            self._pending[request_id] = [done, None]
        self.emit({**fields, "event": "buffer_request", "id": request_id})
        timeout = self.remote_timeout if remote else self.local_timeout
        deadline = self.clock() + timeout
        while not done.wait(0.02):
            if self.cancel is not None and self.cancel.is_set():
                self._take(request_id)
                from .provider import Cancelled
                raise Cancelled("Stopped.")
            if self.clock() >= deadline:
                self._take(request_id)
                return None
        result = self._take(request_id)
        return result if isinstance(result, dict) else None

    def _take(self, request_id: str):
        with self._lock:
            entry = self._pending.pop(request_id, None)
        return entry[1] if entry else None

    def answer(self, reply: dict) -> dict:
        if not isinstance(reply, dict):
            raise ValueError("buffer_result must be an object.")
        request_id = reply.get("id")
        if not isinstance(request_id, str) or not request_id:
            raise ValueError("buffer_result needs the id of the buffer_request it answers.")
        with self._lock:
            entry = self._pending.get(request_id)
            if entry is None:
                return {"id": request_id, "pending": False}   # late, or the turn was stopped
            entry[1] = dict(reply)
            entry[0].set()
        return {"id": request_id, "pending": True}

    def fail_pending(self) -> None:
        """Wake everything still waiting with no answer (the worker is going, or reconfigured)."""
        with self._lock:
            entries = list(self._pending.values())
            self._pending.clear()
        for entry in entries:
            entry[1] = None
            entry[0].set()


def conflict_text(reply: dict) -> str:
    """The refusal the model reads when its patch overlaps the user's unsaved edits: what to do,
    then each contested region as it is in the editor now, so it can retry against that text."""
    path = reply.get("path") or "the file"
    lines = [reply.get("message") if isinstance(reply.get("message"), str) and reply.get("message") else
             f"{path} is open in Relay and the user has unsaved edits on the same lines, so nothing was "
             "changed."]
    for item in (reply.get("conflicts") or [])[:5]:
        if not isinstance(item, dict):
            continue
        line = item.get("line")
        where = f" at line {line}" if isinstance(line, int) and line > 0 else ""
        lines.append(f"\nIn the editor now{where}:\n{str(item.get('buffer', ''))[:2000]}")
        if item.get("agent") is not None:
            lines.append(f"Your version:\n{str(item.get('agent'))[:2000]}")
    lines.append("\nRead the file again (read_file shows the editor's text) and make the edit against "
                 "that, or leave these lines to the user.")
    return "\n".join(lines)
