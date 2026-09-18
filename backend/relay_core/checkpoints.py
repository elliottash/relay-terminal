# SPDX-License-Identifier: GPL-3.0-or-later
"""Per-turn checkpoints: conversation positions and file pre-images for agent writes.

A checkpoint is recorded when a user turn starts. Before the agent's first write to a path in
that turn, the file's previous bytes (or "absent") are stored content-addressed; after each
successful write the new SHA-256 is recorded. Restoring walks turns newest to oldest and only
touches a file whose current hash still equals what the agent wrote, so edits made since by the
user, the shell or another agent are reported as conflicts instead of being overwritten.

Conversation positions are stored per compaction epoch (`locations: {epoch: message_index}`),
because compaction rewrites the message list; see Agent.compact.
Shell side effects (run_command) are never recorded or undone.
"""
from __future__ import annotations

import hashlib
import os
import stat
import tempfile
import time
from pathlib import Path

PREVIEW = 120
MAX_PROMPT_STORED = 16384


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha(path: Path) -> str | None:
    try:
        if path.is_symlink() or not path.is_file():
            return None if not path.exists() and not path.is_symlink() else "not-a-regular-file"
        with open(path, "rb") as handle:
            return hashlib.file_digest(handle, "sha256").hexdigest()
    except FileNotFoundError:
        return None


class CheckpointStore:
    def __init__(self, blob_dir: str | Path | None = None):
        self.blob_dir = Path(blob_dir) if blob_dir else None
        self._memory: dict[str, bytes] = {}
        self.items: list[dict] = []
        self.next_turn = 1

    # ----- recording --------------------------------------------------------
    def begin_turn(self, prompt: str, message_index: int, epoch: int) -> dict:
        item = {"turn": self.next_turn, "prompt": prompt[:MAX_PROMPT_STORED], "prompt_preview": prompt[:PREVIEW],
                "time": time.time(), "locations": {str(epoch): message_index}, "files": {}}
        self.next_turn += 1
        self.items.append(item)
        return item

    def end_turn(self, item: dict | None) -> None:
        """Stamp a turn's wall-clock end.  `time` is when the turn started; a recap has to state
        the span it covers (owner request, 2026-09-17) and the turn record's `elapsed_ms` is
        monotonic, which cannot be turned back into a clock time.  First stamp wins, so a rerun
        through the end path never moves an already-closed turn.
        """
        if item is not None and item.get("ended") is None:
            item["ended"] = time.time()

    def record_before(self, item: dict, path: Path, data: bytes | None) -> None:
        key = str(path)
        if key in item["files"]:
            return
        blob = None
        if data is not None:
            blob = sha256(data)
            self._put(blob, data)
        item["files"][key] = {"before": blob, "after": None}

    def record_after(self, item: dict, path: Path, new_sha: str) -> None:
        record = item["files"].get(str(path))
        if record is not None:
            record["after"] = new_sha

    def _put(self, blob: str, data: bytes) -> None:
        if self.blob_dir is None:
            self._memory[blob] = data
            return
        self.blob_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        target = self.blob_dir / blob
        if target.exists():
            return
        fd, temp = tempfile.mkstemp(dir=self.blob_dir, prefix=".blob-")
        with os.fdopen(fd, "wb") as out:
            out.write(data)
        os.replace(temp, target)

    def _get(self, blob: str) -> bytes:
        if self.blob_dir is None:
            return self._memory[blob]
        data = (self.blob_dir / blob).read_bytes()
        if sha256(data) != blob:
            raise ValueError("Checkpoint data is corrupted.")
        return data

    # ----- queries ----------------------------------------------------------
    def get(self, turn) -> dict:
        for item in self.items:
            if item["turn"] == turn:
                return item
        raise ValueError("No checkpoint for that turn.")

    def listing(self, available=None) -> list[dict]:
        out = []
        for item in self.items:
            entry = {"turn": item["turn"], "prompt_preview": item["prompt_preview"], "time": item["time"],
                     "files": sorted(p for p, r in item["files"].items() if r.get("after"))}
            if available is not None:
                entry["conversation"] = available(item)
            out.append(entry)
        return out

    # ----- restore ----------------------------------------------------------
    def restore_files(self, turn: int, root: Path) -> tuple[list[str], list[str]]:
        """Restore files to their state before `turn`. Returns (restored, conflicts)."""
        self.get(turn)
        root = Path(root).resolve()
        blocked: set[str] = set()
        restored: list[str] = []
        conflicts: list[str] = []
        for item in reversed([i for i in self.items if i["turn"] >= turn]):
            for key in list(item["files"]):
                record = item["files"][key]
                if key in blocked or not record.get("after"):
                    continue
                path = Path(key)
                if not _safe_target(path, root):
                    blocked.add(key)
                    conflicts.append(key)
                    continue
                if file_sha(path) != record["after"]:
                    blocked.add(key)
                    if key not in conflicts:
                        conflicts.append(key)
                    continue
                if record["before"] is None:
                    path.unlink()
                else:
                    _atomic_write(path, self._get(record["before"]))
                del item["files"][key]
                if key not in restored:
                    restored.append(key)
        return restored, conflicts

    def truncate(self, turn: int) -> None:
        """Forget checkpoints from `turn` on (after a conversation rewind)."""
        self.items = [i for i in self.items if i["turn"] < turn]

    # ----- persistence --------------------------------------------------------
    def to_json(self) -> dict:
        return {"next_turn": self.next_turn, "items": self.items}

    def load_json(self, data: dict) -> None:
        if not isinstance(data, dict) or not isinstance(data.get("items"), list):
            raise ValueError("Invalid checkpoint data.")
        items = []
        for item in data["items"]:
            if not isinstance(item, dict) or type(item.get("turn")) is not int or not isinstance(item.get("locations"), dict):
                raise ValueError("Invalid checkpoint entry.")
            item.setdefault("files", {})
            item.setdefault("prompt", item.get("prompt_preview", ""))
            items.append(item)
        self.items = items
        nxt = data.get("next_turn")
        self.next_turn = nxt if type(nxt) is int and nxt > 0 else (max((i["turn"] for i in items), default=0) + 1)


def span(items: list[dict]) -> tuple[float, float] | None:
    """(start, end) epoch seconds covered by `items`, or None when no turn carries a stamp.

    The start is the earliest turn start; the end is the latest of the recorded turn ends and
    turn starts, so a turn still running (or one saved before `ended` existed) contributes its
    start rather than dropping out of the span.  Sessions written before this version have no
    `ended` at all, which is why the fallback is a whole span and not just a missing last turn.
    """
    starts = [item["time"] for item in items if _is_stamp(item.get("time"))]
    if not starts:
        return None
    ends = [item["ended"] for item in items if _is_stamp(item.get("ended"))]
    return min(starts), max(starts + ends)


def _is_stamp(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _safe_target(path: Path, root: Path) -> bool:
    if not path.is_absolute() or ".." in path.parts:
        return False
    try:
        relative = path.relative_to(root)
    except ValueError:
        return False
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            return False
    return path.parent.is_dir()


def _atomic_write(path: Path, data: bytes) -> None:
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    fd, temp = tempfile.mkstemp(prefix=".relay-restore-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as out:
            os.fchmod(out.fileno(), mode)
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
