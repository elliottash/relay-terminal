# SPDX-License-Identifier: GPL-3.0-or-later
"""Switchboard file format: cards, tasks, threads, ids, ranks (phase 0).

The board *is* `issues/`: one Markdown file per card (YAML front matter plus a
Markdown body), one append-only thread per card under `issues/threads/`, and a
generated index in `issues/BOARD.md`.  See `docs/SWITCHBOARD-FORMAT.md`.

This module never calls a model and never talks to the network.  It owns
parsing, ids, ranks, task markers, thread appends, atomic hash-checked writes
and the `check` rules; the GUI and the tools layer build on it.

Only a small, explicit YAML subset is read and written (flat scalars, flow
sequences and flow mappings), so the backend keeps its stdlib-only dependency
list and the emitted bytes stay stable and diff-friendly.
"""
from __future__ import annotations

import fcntl
import hashlib
import os
import re
import secrets
import stat
import subprocess
import tempfile
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Sequence

# --------------------------------------------------------------------------- ids

#: Crockford base32: no I, L, O or U, so ids cannot be misread or spell words.
ID_ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
ID_LETTERS = "ABCDEFGHJKMNPQRSTVWXYZ"
ID_LENGTH = 4
ITEM_ALPHABET = "0123456789abcdefghjkmnpqrstvwxyz"
ITEM_LETTERS = "abcdefghjkmnpqrstvwxyz"
ITEM_LENGTH = 2

ID_RE = re.compile(r"^[0-9A-HJKMNP-TV-Z]{4}$")
ITEM_ID_RE = re.compile(r"^[0-9a-hjkmnp-tv-z]{2}$")


def valid_id(value: str) -> bool:
    """A card id: four Crockford-base32 characters with at least one letter."""
    return bool(value) and bool(ID_RE.match(value)) and any(c in ID_LETTERS for c in value)


def valid_item_id(value: str) -> bool:
    return bool(value) and bool(ITEM_ID_RE.match(value)) and any(c in ITEM_LETTERS for c in value)


def new_id(taken: Iterable[str] = ()) -> str:
    """Random card id avoiding `taken`.  All-digit ids are rejected so GitHub
    does not autolink `#1234` as one of its own issues."""
    used = {t.upper() for t in taken}
    for _ in range(10000):
        candidate = "".join(secrets.choice(ID_ALPHABET) for _ in range(ID_LENGTH))
        if valid_id(candidate) and candidate not in used:
            return candidate
    raise BoardError("could not allocate a card id")


def new_item_id(taken: Iterable[str] = ()) -> str:
    used = {t.lower() for t in taken}
    for _ in range(10000):
        candidate = "".join(secrets.choice(ITEM_ALPHABET) for _ in range(ITEM_LENGTH))
        if valid_item_id(candidate) and candidate not in used:
            return candidate
    raise BoardError("could not allocate a task item id")


def derived_id(seed: str, taken: Iterable[str] = ()) -> str:
    """Deterministic card id from `seed` (used by `migrate`, which must produce
    the same ids on every run so the commit can be reviewed and repeated)."""
    used = {t.upper() for t in taken}
    for salt in range(10000):
        digest = hashlib.sha256(f"{seed}\x00{salt}".encode("utf-8")).digest()
        candidate = "".join(ID_ALPHABET[b % 32] for b in digest[:ID_LENGTH])
        if valid_id(candidate) and candidate not in used:
            return candidate
    raise BoardError("could not derive a card id")


def derived_item_id(seed: str, taken: Iterable[str] = ()) -> str:
    used = {t.lower() for t in taken}
    for salt in range(10000):
        digest = hashlib.sha256(f"{seed}\x00{salt}".encode("utf-8")).digest()
        candidate = "".join(ITEM_ALPHABET[b % 32] for b in digest[:ITEM_LENGTH])
        if valid_item_id(candidate) and candidate not in used:
            return candidate
    raise BoardError("could not derive a task item id")


# ------------------------------------------------------------------------- ranks

#: Ranks are compared as plain strings, so the alphabet must sort ASCII-wise.
RANK_ALPHABET = "0123456789abcdefghijklmnopqrstuvwxyz"
RANK_BASE = len(RANK_ALPHABET)
RANK_RE = re.compile(r"^[0-9a-z]+$")


def valid_rank(value: str) -> bool:
    return bool(value) and bool(RANK_RE.match(value)) and not value.endswith("0")


def rank_between(before: str | None, after: str | None) -> str:
    """A rank strictly between `before` and `after` (fractional index).

    Either bound may be None (start or end of the list).  Reordering one card
    therefore rewrites one card, never a shared ordered index that every move
    would conflict on.  Ranks never end in '0', so there is always room before
    one (the classic fractional-index invariant).
    """
    if before is not None and after is not None and before >= after:
        raise BoardError(f"ranks out of order: {before!r} >= {after!r}")
    for bound in (before, after):
        if bound is not None and (not RANK_RE.match(bound) or bound.endswith("0")):
            raise BoardError(f"not a rank: {bound!r}")
    return _midpoint(before or "", after)


def _midpoint(low: str, high: str | None) -> str:
    if high is not None:
        shared = 0
        while (low[shared] if shared < len(low) else "0") == (high[shared] if shared < len(high) else ""):
            shared += 1
        if shared > 0:
            return high[:shared] + _midpoint(low[shared:], high[shared:])
    first = RANK_ALPHABET.index(low[0]) if low else 0
    last = RANK_ALPHABET.index(high[0]) if high else RANK_BASE
    if last - first > 1:
        return RANK_ALPHABET[(first + last + 1) // 2]
    if high and len(high) > 1:
        return high[:1]
    # The digits are consecutive: keep this one and go deeper.
    return RANK_ALPHABET[first] + _midpoint(low[1:], None)


def initial_ranks(count: int) -> list[str]:
    """`count` evenly spaced ranks, for migration and for seeding a column."""
    if count <= 0:
        return []
    width = 2 if count < RANK_BASE * RANK_BASE - 2 else 3
    span = RANK_BASE ** width
    out = []
    for i in range(count):
        n = (i + 1) * span // (count + 1)
        if n % RANK_BASE == 0:  # ranks never end in '0' (see rank_between)
            n += 1
        digits = []
        for _ in range(width):
            digits.append(RANK_ALPHABET[n % RANK_BASE])
            n //= RANK_BASE
        out.append("".join(reversed(digits)))
    if out != sorted(set(out)):  # pragma: no cover - only for absurd counts
        raise BoardError("too many cards for the rank width")
    return out


# ------------------------------------------------------------- statuses and paths

CARD_TYPES = ("work", "plan", "memory")

#: status -> state subfolder inside the category folder ("" = the category itself)
WORK_STATUS_FOLDER = {
    "inbox": "", "discussing": "", "ready": "", "in-progress": "",
    "needs-qa-llm": "needs_qa_llm", "needs-qa-human": "needs_qa_human",
    "needs-review": "needs_review", "needs-labels": "needs_labels", "needs-ab": "needs_ab",
    "deferred": "deferred", "done": "done", "dropped": "done",
}
PLAN_STATUS_FOLDER = {
    "draft": "", "approved": "", "executing": "", "done": "done", "dropped": "done",
}
MEMORY_STATUS_FOLDER = {"active": "", "retired": "archive"}

STATUS_FOLDER: dict[str, dict[str, str]] = {
    "work": WORK_STATUS_FOLDER, "plan": PLAN_STATUS_FOLDER, "memory": MEMORY_STATUS_FOLDER,
}

#: Legacy header statuses from the pre-board tracker.
LEGACY_STATUS = {"open": "ready", "needs-qa": "needs-qa-llm"}

PLAN_FOLDER = "planning"
MEMORY_FOLDER = "memory"
THREADS_FOLDER = "threads"
PRIVATE_FOLDER = ".private"
BOARD_CONFIG = "board.yaml"
BOARD_INDEX = "BOARD.md"

COMMON_FIELDS = ("id", "type", "status", "rank", "created", "labels", "assignee", "private",
                 "links", "aliases", "source", "blocked_by", "parent", "waiting_on")
WORK_FIELDS = ("component", "milestone", "workstream", "acceptance", "implemented_by",
               "label_count", "label_output", "codebook")
PLAN_FIELDS = ("approved_by", "goal")
MEMORY_FIELDS = ("name", "description", "kind", "topic", "scope", "paths", "pinned",
                 "supersedes", "reviewed", "author")

ALLOWED_FIELDS = {
    "work": set(COMMON_FIELDS) | set(WORK_FIELDS),
    "plan": set(COMMON_FIELDS) | set(PLAN_FIELDS),
    "memory": set(COMMON_FIELDS) | set(MEMORY_FIELDS),
}
#: Emission order; anything else follows, sorted, so a new key is never dropped.
FIELD_ORDER = ("id", "type", "status", "name", "description", "kind", "topic", "scope",
               "private", "labels", "component", "milestone", "workstream", "assignee",
               "implemented_by", "waiting_on", "parent", "blocked_by", "aliases", "paths",
               "pinned", "reviewed", "author", "supersedes", "approved_by", "goal",
               "label_count", "label_output", "codebook", "rank", "created", "acceptance",
               "source", "links")

TASK_HEADING = {"work": "Tasks", "plan": "Steps", "memory": "Tasks"}

ITEM_STATUSES = ("open", "in-progress", "blocked", "deferred", "done", "dropped")
CLOSED_ITEM_STATUSES = ("done", "dropped")


class BoardError(Exception):
    """A malformed card, thread or rank."""


class BoardConflict(BoardError):
    """A hash-checked write lost a race; nothing was overwritten."""

    def __init__(self, path: Path, current_hash: str):
        super().__init__(f"{path} changed since it was read (now {current_hash[:12]})")
        self.path = path
        self.current_hash = current_hash


# ------------------------------------------------------------------ YAML subset

_PLAIN_UNSAFE = set("-?:,[]{}#&*!|>'\"%@`")
_YAML_CONSTANTS = {"true": True, "false": False, "yes": True, "no": False,
                   "on": True, "off": False, "null": None, "~": None, "": None}
_INT_RE = re.compile(r"^[-+]?\d+$")
_FLOAT_RE = re.compile(r"^[-+]?(\d+\.\d*|\.\d+)([eE][-+]?\d+)?$")
#: Plain scalars a full YAML reader would turn into something other than a string
#: (dates, times, hex, underscored ints, infinities).  We quote those instead.
_TYPED_RE = re.compile(r"""^(
      \d{4}-\d{1,2}-\d{1,2}([Tt ].*)?        # date / timestamp
    | \d{1,2}:\d{2}(:\d{2})?                 # sexagesimal
    | [-+]?0[xXbB][0-9a-fA-F_]+              # hex / binary
    | [-+]?[\d_]*\d[\d_]*                    # underscored int
    | [-+]?\.(inf|Inf|INF|nan|NaN|NAN)
    )$""", re.X)


def _quote(text: str) -> str:
    return "'" + text.replace("'", "''") + "'"


def _needs_quote(text: str) -> bool:
    if text == "" or text != text.strip():
        return True
    if text[0] in _PLAIN_UNSAFE:
        return True
    if ": " in text or text.endswith(":") or " #" in text:
        return True
    if text.lower() in _YAML_CONSTANTS or _INT_RE.match(text) or _FLOAT_RE.match(text):
        return True
    return bool(_TYPED_RE.match(text))


def yaml_scalar(value) -> str:
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, (int, float)):
        return repr(value)
    text = str(value)
    if "\n" in text:  # front matter scalars stay on one line; bodies hold prose
        text = " ".join(text.split())
    return _quote(text) if _needs_quote(text) else text


def yaml_value(value) -> str:
    if isinstance(value, (list, tuple)):
        return "[" + ", ".join(yaml_value(v) for v in value) + "]"
    if isinstance(value, dict):
        return "{" + ", ".join(f"{yaml_scalar(k)}: {yaml_value(v)}" for k, v in value.items()) + "}"
    return yaml_scalar(value)


def dump_front_matter(front: dict, order: Sequence[str] = FIELD_ORDER) -> str:
    """Canonical front matter body (without the `---` fences)."""
    keys = [k for k in order if k in front] + sorted(k for k in front if k not in order)
    return "".join(f"{k}: {yaml_value(front[k])}\n" for k in keys)


def _parse_scalar(text: str):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        inner = text[1:-1]
        return inner.replace("''", "'") if text[0] == "'" else inner.replace('\\"', '"')
    low = text.lower()
    if low in _YAML_CONSTANTS:
        return _YAML_CONSTANTS[low]
    if _INT_RE.match(text):
        return int(text)
    if _FLOAT_RE.match(text):
        return float(text)
    return text


def _split_flow(text: str) -> list[str]:
    parts, depth, quote, buf = [], 0, "", []
    for ch in text:
        if quote:
            buf.append(ch)
            if ch == quote:
                quote = ""
            continue
        if ch in "'\"":
            quote = ch
            buf.append(ch)
            continue
        if ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(buf))
            buf = []
            continue
        buf.append(ch)
    tail = "".join(buf).strip()
    if tail:
        parts.append(tail)
    return [p.strip() for p in parts if p.strip()]


def _split_key(text: str) -> tuple[str, str] | None:
    depth, quote = 0, ""
    for i, ch in enumerate(text):
        if quote:
            if ch == quote:
                quote = ""
            continue
        if ch in "'\"":
            quote = ch
        elif ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
        elif ch == ":" and depth == 0 and (i + 1 == len(text) or text[i + 1] in " \t"):
            return text[:i].strip(), text[i + 1:].strip()
    return None


def parse_yaml_value(text: str):
    text = text.strip()
    if text.startswith("[") and text.endswith("]"):
        return [parse_yaml_value(p) for p in _split_flow(text[1:-1])]
    if text.startswith("{") and text.endswith("}"):
        out = {}
        for part in _split_flow(text[1:-1]):
            split = _split_key(part)
            if split is None:
                raise BoardError(f"bad mapping entry: {part!r}")
            out[str(_parse_scalar(split[0]))] = parse_yaml_value(split[1])
        return out
    return _parse_scalar(text)


def _strip_comment(text: str) -> str:
    """Drop a trailing YAML comment (` #…` outside quotes and flow collections)."""
    depth, quote = 0, ""
    for i, ch in enumerate(text):
        if quote:
            if ch == quote:
                quote = ""
            continue
        if ch in "'\"":
            quote = ch
        elif ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
        elif ch == "#" and depth == 0 and i > 0 and text[i - 1] in " \t":
            return text[:i].rstrip()
    return text


def _balanced(text: str) -> bool:
    depth, quote = 0, ""
    for ch in text:
        if quote:
            if ch == quote:
                quote = ""
            continue
        if ch in "'\"":
            quote = ch
        elif ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
    return depth <= 0


def parse_yaml(text: str) -> dict:
    """Read the supported subset: `key: scalar`, flow `[...]`/`{...}` (which may
    wrap across lines), block sequences of scalars, and one level of block map."""
    out: dict = {}
    lines = [ln for ln in text.splitlines() if ln.strip() and not ln.lstrip().startswith("#")]
    i = 0
    while i < len(lines):
        line = lines[i]
        if line[:1] in (" ", "\t", "-"):
            raise BoardError(f"unsupported YAML line: {line!r}")
        split = _split_key(line)
        if split is None:
            raise BoardError(f"unsupported YAML line: {line!r}")
        key, value = split
        i += 1
        while value and not _balanced(value) and i < len(lines):
            value = value + " " + lines[i].strip()
            i += 1
        value = _strip_comment(value)
        if value == "":
            block: list = []
            mapping: dict = {}
            while i < len(lines) and lines[i][:1] in (" ", "\t"):
                child = lines[i].strip()
                i += 1
                if child.startswith("- "):
                    block.append(parse_yaml_value(child[2:]))
                else:
                    pair = _split_key(child)
                    if pair is None:
                        raise BoardError(f"unsupported YAML line: {child!r}")
                    mapping[str(_parse_scalar(pair[0]))] = parse_yaml_value(pair[1])
            out[str(_parse_scalar(key))] = block if block else (mapping if mapping else None)
            continue
        out[str(_parse_scalar(key))] = parse_yaml_value(value)
    return out


# -------------------------------------------------------------------- task items

_ITEM_RE = re.compile(
    r"^(?P<indent>[ \t]*)(?P<bullet>[-*+]) \[(?P<box>[ xX])\](?P<space> +)(?P<rest>.*?)[ \t]*$")
_MARKER_RE = re.compile(r"<!--\s*t:(?P<id>[0-9A-Za-z]{1,4})(?P<attrs>(?:\s+[a-z_]+=[^\s>]+)*)\s*-->$")
_ATTR_RE = re.compile(r"([a-z_]+)=([^\s>]+)")


@dataclass
class TaskItem:
    """One `- [ ]` line of a card's `## Tasks` (or a plan's `## Steps`)."""
    item_id: str | None = None
    text: str = ""
    status: str = "open"
    depth: int = 0
    card: str | None = None
    blocked_by: list[str] = field(default_factory=list)
    line: int | None = None          # index in the card body, when parsed
    box_wins: bool = False           # the checkbox disagreed with the marker
    missing_marker: bool = False     # no `<!-- t:.. -->` yet (GitHub or hand edit)
    bullet: str = "-"

    @property
    def done(self) -> bool:
        return self.status in CLOSED_ITEM_STATUSES

    @property
    def ref(self) -> str:
        return f".{self.item_id}" if self.item_id else ""

    def render(self) -> str:
        if not self.item_id:
            raise BoardError("task item has no id; call assign_item_ids() first")
        text = self.text.strip()
        if self.status == "dropped" and not text.startswith("~~"):
            text = f"~~{text}~~"
        if self.status != "dropped" and text.startswith("~~") and text.endswith("~~"):
            text = text[2:-2].strip()
        attrs = ""
        implied = "done" if self.done else "open"
        if self.status != implied:
            attrs += f" s={self.status}"
        if self.card:
            attrs += f" card={self.card}"
        if self.blocked_by:
            attrs += " blocked_by=" + ",".join(self.blocked_by)
        box = "x" if self.done else " "
        return f"{'  ' * self.depth}{self.bullet} [{box}] {text} <!-- t:{self.item_id}{attrs} -->"


def parse_task_line(line: str, index: int | None = None) -> TaskItem | None:
    match = _ITEM_RE.match(line)
    if not match:
        return None
    rest = match.group("rest")
    item = TaskItem(line=index, bullet=match.group("bullet"))
    indent = match.group("indent").replace("\t", "  ")
    item.depth = len(indent) // 2
    marker = _MARKER_RE.search(rest)
    attrs: dict[str, str] = {}
    if marker:
        item.item_id = marker.group("id").lower()
        attrs = dict(_ATTR_RE.findall(marker.group("attrs") or ""))
        rest = rest[:marker.start()].rstrip()
    else:
        item.missing_marker = True
    item.text = rest.strip()
    checked = match.group("box").lower() == "x"
    marked = attrs.get("s", "").strip()
    if marked and marked not in ITEM_STATUSES:
        raise BoardError(f"unknown task status {marked!r}")
    status = marked or ("done" if checked else "open")
    # The checkbox is authoritative for open/closed; the marker only refines it,
    # so a box ticked in GitHub's web UI wins over a stale `s=in-progress`.
    if checked and status not in CLOSED_ITEM_STATUSES:
        status, item.box_wins = "done", True
    elif not checked and status in CLOSED_ITEM_STATUSES:
        status, item.box_wins = "open", True
    item.status = status
    if item.status == "dropped" and item.text.startswith("~~") and item.text.endswith("~~"):
        item.text = item.text[2:-2].strip()
    item.card = attrs.get("card")
    item.blocked_by = [p for p in attrs.get("blocked_by", "").split(",") if p]
    return item


def assign_item_ids(items: Sequence[TaskItem], seed: str | None = None) -> list[TaskItem]:
    """Give every unmarked item an id, unique within the card."""
    taken = {i.item_id for i in items if i.item_id}
    for position, item in enumerate(items):
        if not item.item_id:
            item.item_id = (derived_item_id(f"{seed}\x00{position}\x00{item.text}", taken)
                            if seed is not None else new_item_id(taken))
            taken.add(item.item_id)
    return list(items)


# ------------------------------------------------------------------------- cards

_FRONT_RE = re.compile(r"\A---\r?\n(.*?)(?:\r?\n)---[ \t]*\r?\n", re.S)
_H1_RE = re.compile(r"^#[ \t]+(.*?)[ \t]*$", re.M)
_MERGE_MARKER_RE = re.compile(r"^(<{7}|={7}|>{7})[ \t]*(\S.*)?$", re.M)


@dataclass
class Card:
    """A card file: YAML front matter plus the Markdown body (first `# ` = title)."""
    front: dict = field(default_factory=dict)
    body: str = ""
    path: Path | None = None
    front_raw: str | None = None     # bytes as read, kept while the front matter is untouched
    dirty: bool = False

    # ---- parse / render
    @classmethod
    def parse(cls, text: str, path: Path | None = None) -> "Card":
        match = _FRONT_RE.match(text)
        if not match:
            return cls(front={}, body=text, path=path, front_raw=None)
        raw = match.group(1)
        try:
            front = parse_yaml(raw)
        except BoardError as exc:
            raise BoardError(f"{path or '<card>'}: bad front matter: {exc}") from exc
        return cls(front=front, body=text[match.end():], path=path, front_raw=raw + "\n")

    @classmethod
    def load(cls, path: Path) -> "Card":
        return cls.parse(path.read_text(encoding="utf-8"), path=path)

    def to_text(self) -> str:
        if not self.front:
            return self.body
        raw = dump_front_matter(self.front) if (self.dirty or self.front_raw is None) else self.front_raw
        return f"---\n{raw}---\n{self.body}"

    def sha256(self) -> str:
        return hashlib.sha256(self.to_text().encode("utf-8")).hexdigest()

    # ---- fields
    def set(self, key: str, value) -> None:
        if self.front.get(key) != value or key not in self.front:
            self.front[key] = value
            self.dirty = True

    def drop(self, key: str) -> None:
        if key in self.front:
            del self.front[key]
            self.dirty = True

    @property
    def id(self) -> str | None:
        value = self.front.get("id")
        return str(value).upper() if value is not None else None

    @property
    def type(self) -> str:
        return str(self.front.get("type") or "work")

    @property
    def status(self) -> str:
        return str(self.front.get("status") or "")

    @property
    def rank(self) -> str:
        return str(self.front.get("rank") or "")

    @property
    def private(self) -> bool:
        return bool(self.front.get("private"))

    @property
    def title(self) -> str:
        match = _H1_RE.search(self.body)
        if match:
            return match.group(1).strip()
        return str(self.front.get("name") or self.front.get("description") or "")

    @property
    def task_heading(self) -> str:
        return TASK_HEADING[self.type]

    # ---- tasks
    def _section_bounds(self, heading: str) -> tuple[int, int] | None:
        lines = self.body.splitlines()
        wanted = f"## {heading}"
        for i, line in enumerate(lines):
            if line.strip() == wanted:
                for j in range(i + 1, len(lines)):
                    if lines[j].startswith("## ") or lines[j].startswith("# "):
                        return i + 1, j
                return i + 1, len(lines)
        return None

    def tasks(self) -> list[TaskItem]:
        bounds = self._section_bounds(self.task_heading)
        if bounds is None:
            return []
        start, end = bounds
        lines = self.body.splitlines()
        items = []
        for index in range(start, end):
            item = parse_task_line(lines[index], index)
            if item is not None:
                items.append(item)
        return items

    def write_tasks(self, items: Sequence[TaskItem]) -> None:
        """Rewrite the task lines in place; every other byte of the body is kept.

        Items carrying a `line` replace that line; new items are appended after
        the last existing item (or at the end of the section).
        """
        heading = self.task_heading
        bounds = self._section_bounds(heading)
        newline = "\n"
        lines = self.body.splitlines()
        trailing = self.body.endswith("\n")
        if bounds is None:
            block = [f"## {heading}", ""] + [item.render() for item in assign_item_ids(list(items))]
            body = self.body.rstrip("\n")
            self.body = (body + "\n\n" if body else "") + newline.join(block) + "\n"
            return
        start, end = bounds
        assign_item_ids(list(items))
        by_line = {item.line: item for item in items if item.line is not None}
        fresh = [item for item in items if item.line is None]
        out = lines[:start]
        last_item = start
        for index in range(start, end):
            item = by_line.get(index)
            if item is None:
                if parse_task_line(lines[index]) is not None:
                    continue  # an item that was removed from the list
                out.append(lines[index])
                continue
            out.append(item.render())
            last_item = len(out)
        for item in fresh:
            out.insert(last_item, item.render())
            last_item += 1
        out.extend(lines[end:])
        self.body = newline.join(out) + ("\n" if trailing else "")

    def add_task(self, text: str, status: str = "open", depth: int = 0) -> TaskItem:
        items = self.tasks()
        item = TaskItem(text=text, status=status, depth=depth)
        items.append(item)
        self.write_tasks(items)
        return next(i for i in self.tasks() if i.item_id == item.item_id)

    def normalize_tasks(self) -> int:
        """Give unmarked items ids and reconcile boxes with markers.  Returns the
        number of lines changed."""
        items = self.tasks()
        if not items:
            return 0
        changed = sum(1 for i in items if i.missing_marker or i.box_wins)
        before = self.body
        self.write_tasks(items)
        return changed if self.body != before else 0

    # ---- placement
    def expected_folder(self, category: str | None = None) -> str:
        table = STATUS_FOLDER[self.type]
        if self.status not in table:
            raise BoardError(f"unknown {self.type} status {self.status!r}")
        if self.type == "plan":
            category = PLAN_FOLDER
        elif self.type == "memory":
            category = MEMORY_FOLDER
        elif category is None:
            raise BoardError("a work card needs its category folder")
        sub = table[self.status]
        return f"{category}/{sub}" if sub else category


# ------------------------------------------------------------------------ threads

_ENTRY_RE = re.compile(r"^<!--\s*relay:entry\s+(?P<id>\S+)(?P<attrs>[^>]*?)-->[ \t]*$")
ENTRY_ID_RE = re.compile(r"^\d{8}T\d{6}Z-[0-9a-z]{2}$")
ENTRY_KINDS = ("comment", "question", "decision", "evidence", "progress", "note",
               "event", "task", "plan", "rewrite")


@dataclass
class ThreadEntry:
    entry_id: str
    attrs: dict[str, str] = field(default_factory=dict)
    text: str = ""

    @property
    def author(self) -> str:
        return self.attrs.get("author", "")

    @property
    def kind(self) -> str:
        return self.attrs.get("kind", "comment")

    def render(self) -> str:
        head = " ".join(f"{k}={_attr_value(v)}" for k, v in self.attrs.items())
        header = f"<!-- relay:entry {self.entry_id}{' ' + head if head else ''} -->"
        body = self.text.strip("\n")
        return f"{header}\n{body}\n" if body else f"{header}\n"


def _attr_value(value) -> str:
    text = str(value)
    if not text or re.search(r"[\s>\"]", text):
        return '"' + re.sub(r'[\s>"]+', " ", text).strip() + '"'
    return text


def _parse_attrs(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for key, quoted, bare in re.findall(r'([a-z_]+)=(?:"([^"]*)"|([^\s>]+))', text):
        out[key] = quoted if quoted else bare
    return out


def new_entry_id(when: datetime | None = None, taken: Iterable[str] = ()) -> str:
    when = when or datetime.now(timezone.utc)
    stamp = when.astimezone(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    used = set(taken)
    for _ in range(10000):
        candidate = f"{stamp}-{secrets.choice(ITEM_ALPHABET)}{secrets.choice(ITEM_ALPHABET)}"
        if candidate not in used:
            return candidate
    raise BoardError("could not allocate a thread entry id")  # pragma: no cover


def parse_thread(text: str) -> list[ThreadEntry]:
    entries: list[ThreadEntry] = []
    current: ThreadEntry | None = None
    buffer: list[str] = []
    for line in text.splitlines():
        match = _ENTRY_RE.match(line)
        if match:
            if current is not None:
                current.text = "\n".join(buffer).strip("\n")
                entries.append(current)
            current = ThreadEntry(match.group("id"), _parse_attrs(match.group("attrs") or ""))
            buffer = []
        elif current is not None:
            buffer.append(line)
    if current is not None:
        current.text = "\n".join(buffer).strip("\n")
        entries.append(current)
    return entries


def render_thread(entries: Sequence[ThreadEntry]) -> str:
    return "\n".join(entry.render() for entry in entries)


# -------------------------------------------------------------------- filesystem

def _atomic_write(path: Path, text: str, mode: int | None = None) -> str:
    data = text.encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    if mode is None:
        mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    fd, temp = tempfile.mkstemp(prefix=".relay-board-", dir=str(path.parent))
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
    return hashlib.sha256(data).hexdigest()


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else ""


@dataclass
class Problem:
    code: str
    path: str
    message: str
    severity: str = "error"
    fixable: bool = False

    def __str__(self) -> str:
        return f"{self.severity}: {self.path}: {self.code}: {self.message}"


DEFAULT_CONFIG = {
    "version": 1,
    "tabs": [{"id": "features", "folder": "features"}, {"id": "bugs", "folder": "changes"},
             {"id": "design", "folder": "design"}, {"id": "marketing", "folder": "marketing"},
             {"id": "planning", "folder": "planning"},
             {"id": "deferred", "filter": "status:deferred"},
             {"id": "done", "filter": "status:done,dropped"}],
    "columns": ["inbox", "discussing", "ready", "in-progress", "waiting", "needs-qa", "done"],
    "agent": {"autonomy": "auto", "max_creates_per_turn": 5},
    "memory": {"autonomy": "auto"},
}

CONFIG_TEXT = """\
# Switchboard configuration. Format: docs/SWITCHBOARD-FORMAT.md
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: design, folder: design}, {id: marketing, folder: marketing},
  {id: planning, folder: planning},
  {id: deferred, filter: "status:deferred"}, {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
memory: {autonomy: auto}
"""

GITATTRIBUTES_LINE = "issues/threads/*.md merge=union"
GITIGNORE_TEXT = "# Private cards, plans, threads and memory (Switchboard private root).\n.private/\n"


class Board:
    """The `issues/` tree: cards, threads, config and the check rules."""

    def __init__(self, root: str | os.PathLike, repo: str | os.PathLike | None = None):
        self.root = Path(root)
        self.repo = Path(repo) if repo is not None else self.root.parent

    # ---- config
    @property
    def config_path(self) -> Path:
        return self.root / BOARD_CONFIG

    def config(self) -> dict:
        if not self.config_path.exists():
            return dict(DEFAULT_CONFIG)
        return parse_yaml(self.config_path.read_text(encoding="utf-8"))

    def tabs(self) -> list[dict]:
        return [t for t in self.config().get("tabs") or [] if isinstance(t, dict)]

    def category_folders(self) -> list[str]:
        folders = [str(t["folder"]) for t in self.tabs() if t.get("folder")]
        for extra in (PLAN_FOLDER, MEMORY_FOLDER):
            if extra not in folders:
                folders.append(extra)
        return folders

    # ---- paths
    def private_root(self) -> Path:
        return self.root / PRIVATE_FOLDER

    def base_for(self, private: bool) -> Path:
        return self.private_root() if private else self.root

    def threads_dir(self, private: bool = False) -> Path:
        return self.base_for(private) / THREADS_FOLDER

    def thread_path(self, card_id: str, private: bool = False) -> Path:
        return self.threads_dir(private) / f"{card_id.upper()}.md"

    def card_paths(self, include_private: bool = True) -> list[Path]:
        out: list[Path] = []
        for base in ([self.root] + ([self.private_root()] if include_private else [])):
            if not base.is_dir():
                continue
            for path in sorted(base.rglob("*.md")):
                rel = path.relative_to(self.root)
                parts = rel.parts
                if parts[0] == PRIVATE_FOLDER:
                    if not include_private:
                        continue
                    parts = parts[1:]
                if not parts or parts[0] in (THREADS_FOLDER,) or path.name == BOARD_INDEX:
                    continue
                if len(parts) < 2 or path.name.upper() == "README.MD":
                    continue
                out.append(path)
        return sorted(set(out))

    def category_of(self, path: Path) -> str:
        parts = Path(path).relative_to(self.root).parts
        if parts and parts[0] == PRIVATE_FOLDER:
            parts = parts[1:]
        return parts[0] if parts else ""

    def cards(self, include_private: bool = True) -> list[Card]:
        return [Card.load(p) for p in self.card_paths(include_private)]

    def card_by_id(self, card_id: str) -> Card | None:
        for card in self.cards():
            if card.id == card_id.upper():
                return card
        return None

    # ---- writes
    def save(self, card: Card, base_hash: str | None = None) -> str:
        if card.path is None:
            raise BoardError("card has no path")
        current = file_hash(card.path)
        if base_hash is not None and current != base_hash:
            raise BoardConflict(card.path, current)
        return _atomic_write(card.path, card.to_text())

    def append_thread(self, card_id: str, text: str, author: str = "owner",
                      kind: str = "comment", private: bool = False,
                      when: datetime | None = None, **attrs) -> ThreadEntry:
        """Append one self-contained entry.  `O_APPEND` under `flock`, so two
        processes (and a `merge=union` git merge) both keep every entry."""
        if kind not in ENTRY_KINDS:
            raise BoardError(f"unknown thread entry kind {kind!r}")
        path = self.thread_path(card_id, private)
        path.parent.mkdir(parents=True, exist_ok=True)
        entry = ThreadEntry(new_entry_id(when), {"author": author, "kind": kind,
                                                 **{k: str(v) for k, v in attrs.items() if v is not None}}, text)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX)
            size = os.lseek(fd, 0, os.SEEK_END)
            prefix = ""
            if size:
                with open(path, "rb") as check:
                    check.seek(max(0, size - 2))
                    tail = check.read()
                prefix = "\n\n" if not tail.endswith(b"\n") else ("\n" if not tail.endswith(b"\n\n") else "")
            os.write(fd, (prefix + entry.render()).encode("utf-8"))
            os.fsync(fd)
        finally:
            fcntl.flock(fd, fcntl.LOCK_UN)
            os.close(fd)
        return entry

    def thread(self, card_id: str, private: bool = False) -> list[ThreadEntry]:
        path = self.thread_path(card_id, private)
        if not path.exists():
            return []
        return parse_thread(path.read_text(encoding="utf-8"))

    # ---- ranks
    def next_rank(self, cards: Sequence[Card]) -> str:
        ranks = sorted(c.rank for c in cards if c.rank)
        return rank_between(ranks[-1] if ranks else None, None)

    # ---- check
    def check(self, fix: bool = False) -> list[Problem]:
        problems: list[Problem] = []
        by_id: dict[str, list[str]] = {}
        seen_ids: set[str] = set()
        for path in self.card_paths():
            rel = str(path.relative_to(self.root))
            try:
                card = Card.load(path)
            except (BoardError, UnicodeDecodeError) as exc:
                problems.append(Problem("bad_card", rel, str(exc)))
                continue
            problems.extend(self._check_card(card, rel, fix))
            if card.id:
                by_id.setdefault(card.id, []).append(rel)
                seen_ids.add(card.id)
        for card_id, paths in sorted(by_id.items()):
            if len(paths) > 1:
                problems.append(Problem("duplicate_id", paths[0],
                                        f"id {card_id} is used by {len(paths)} cards: {', '.join(paths)}"))
        problems.extend(self._check_threads(seen_ids, fix))
        problems.extend(self._check_private())
        return sorted(problems, key=lambda p: (p.path, p.code))

    def _check_card(self, card: Card, rel: str, fix: bool) -> list[Problem]:
        problems: list[Problem] = []
        if not card.front:
            problems.append(Problem("no_front_matter", rel, "card has no YAML front matter"))
            return problems
        if _MERGE_MARKER_RE.search(card.body) or (card.front_raw and _MERGE_MARKER_RE.search(card.front_raw)):
            problems.append(Problem("merge_markers", rel, "unresolved git conflict markers"))
        if card.type not in CARD_TYPES:
            problems.append(Problem("unknown_type", rel, f"type {card.type!r} is not one of {CARD_TYPES}"))
            return problems
        if not card.id:
            problems.append(Problem("missing_id", rel, "no id in front matter"))
        elif not valid_id(card.id):
            problems.append(Problem("bad_id", rel, f"id {card.id!r} is not 4 Crockford-base32 characters with a letter"))
        table = STATUS_FOLDER[card.type]
        if card.status not in table:
            problems.append(Problem("bad_status", rel,
                                    f"status {card.status!r} is not one of {sorted(table)}"))
        else:
            want = card.expected_folder(self.category_of(card.path))
            have = str(Path(rel).parent)
            if have.startswith(PRIVATE_FOLDER + "/"):
                have = have[len(PRIVATE_FOLDER) + 1:]
            if have != want:
                problems.append(Problem("folder_status_mismatch", rel,
                                        f"status {card.status!r} belongs in {want}/, file is in {have}/"))
        if card.rank and not valid_rank(card.rank):
            problems.append(Problem("bad_rank", rel, f"rank {card.rank!r} must be [0-9a-z]+ not ending in 0"))
        if not card.rank:
            problems.append(Problem("missing_rank", rel, "no rank in front matter", "warning"))
        if card.private != str(card.path).startswith(str(self.private_root())):
            problems.append(Problem("private_flag_mismatch", rel,
                                    "private: true belongs under .private/ and nowhere else"))
        unknown = sorted(set(card.front) - ALLOWED_FIELDS[card.type])
        if unknown:
            problems.append(Problem("unknown_field", rel,
                                    f"unknown front matter field(s) for a {card.type} card: {', '.join(unknown)}"))
        if not card.title:
            problems.append(Problem("missing_title", rel, "no '# ' heading in the body"))
        problems.extend(self._check_tasks(card, rel, fix))
        return problems

    def _check_tasks(self, card: Card, rel: str, fix: bool) -> list[Problem]:
        problems: list[Problem] = []
        try:
            items = card.tasks()
        except BoardError as exc:
            return [Problem("bad_task_marker", rel, str(exc))]
        seen: dict[str, TaskItem] = {}
        for item in items:
            if item.item_id and not valid_item_id(item.item_id):
                problems.append(Problem("bad_task_marker", rel,
                                        f"task id {item.item_id!r} is not 2 Crockford-base32 characters"))
            if item.item_id and item.item_id in seen:
                problems.append(Problem("duplicate_task_id", rel, f"two items share the id {item.item_id}"))
            if item.item_id:
                seen[item.item_id] = item
            if item.depth > 1:
                problems.append(Problem("task_too_deep", rel,
                                        f"item {item.item_id or item.text[:20]!r} nests deeper than one level",
                                        "warning"))
            if item.card and not valid_id(item.card.upper()):
                problems.append(Problem("bad_task_marker", rel, f"card={item.card} is not a card id"))
        for item in items:
            for blocker in item.blocked_by:
                if blocker.startswith("#"):
                    continue
                if blocker not in seen:
                    problems.append(Problem("unknown_blocker", rel,
                                            f"item {item.item_id} is blocked_by {blocker}, which is not an item here"))
        cycle = _task_cycle(items)
        if cycle:
            problems.append(Problem("task_cycle", rel, "blocked_by cycle: " + " -> ".join(cycle)))
        unmarked = [i for i in items if i.missing_marker]
        mismatched = [i for i in items if i.box_wins]
        if unmarked or mismatched:
            if fix:
                card.normalize_tasks()
                self.save(card)
            else:
                if unmarked:
                    problems.append(Problem("task_missing_marker", rel,
                                            f"{len(unmarked)} item(s) have no <!-- t:id --> marker",
                                            "warning", fixable=True))
                if mismatched:
                    problems.append(Problem("task_marker_stale", rel,
                                            f"{len(mismatched)} item(s) have a checkbox the marker disagrees with",
                                            "warning", fixable=True))
        return problems

    def _check_threads(self, card_ids: set[str], fix: bool) -> list[Problem]:
        problems: list[Problem] = []
        for private in (False, True):
            directory = self.threads_dir(private)
            if not directory.is_dir():
                continue
            for path in sorted(directory.glob("*.md")):
                rel = str(path.relative_to(self.root))
                entries = parse_thread(path.read_text(encoding="utf-8"))
                card_id = path.stem.upper()
                if not valid_id(card_id):
                    problems.append(Problem("bad_thread_name", rel, "thread file is not named <ID>.md"))
                elif card_id not in card_ids:
                    problems.append(Problem("orphan_thread", rel, f"no card has id {card_id}", "warning"))
                ids = [e.entry_id for e in entries]
                duplicates = sorted({i for i in ids if ids.count(i) > 1})
                if duplicates:
                    problems.append(Problem("duplicate_thread_entry", rel,
                                            f"repeated entry id(s): {', '.join(duplicates)}"))
                bad = [i for i in ids if not ENTRY_ID_RE.match(i)]
                if bad:
                    problems.append(Problem("bad_thread_entry", rel,
                                            f"entry id(s) not sortable timestamps: {', '.join(bad[:3])}"))
                for entry in entries:
                    if entry.kind not in ENTRY_KINDS:
                        problems.append(Problem("bad_thread_entry", rel,
                                                f"entry {entry.entry_id} has unknown kind {entry.kind!r}"))
                if ids != sorted(ids):
                    if fix:
                        _atomic_write(path, render_thread(sorted(entries, key=lambda e: e.entry_id)))
                    else:
                        problems.append(Problem("thread_unsorted", rel,
                                                "entries are not in id order (a union merge interleaved them)",
                                                "warning", fixable=True))
        return problems

    def _check_private(self) -> list[Problem]:
        private = self.private_root()
        if not private.exists():
            return []
        try:
            tracked = subprocess.run(["git", "-C", str(self.repo), "ls-files", "--", str(private)],
                                     capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.SubprocessError):  # pragma: no cover - git missing
            return []
        if tracked.returncode != 0:
            return []
        files = [line for line in tracked.stdout.splitlines() if line.strip()]
        if not files:
            return []
        return [Problem("private_tracked", f"{PRIVATE_FOLDER}/",
                        f"{len(files)} private file(s) are tracked by git: {', '.join(files[:3])}"
                        f"{'…' if len(files) > 3 else ''}; add .private/ to issues/.gitignore and "
                        "`git rm --cached` them")]

    # ---- index
    def index_markdown(self, include_private: bool = False) -> str:
        cards = []
        for path in self.card_paths(include_private=include_private):
            try:
                card = Card.load(path)
            except BoardError:
                continue
            if card.front:
                cards.append(card)
        lines = ["<!-- Generated by scripts/relay-board.py index. Do not edit by hand;",
                 "     edit the card files and regenerate. -->",
                 "# Board", "",
                 f"{len(cards)} cards. Format: [docs/SWITCHBOARD-FORMAT.md](../docs/SWITCHBOARD-FORMAT.md).", ""]
        tabs = [t for t in self.tabs() if t.get("folder")]
        known = {str(t["folder"]) for t in tabs}
        for folder in self.category_folders():
            if folder not in known:
                tabs.append({"id": folder, "folder": folder})
                known.add(folder)
        for tab in tabs:
            folder = str(tab["folder"])
            group = [c for c in cards if self.category_of(c.path) == folder]
            if not group:
                continue
            title = str(tab.get("title") or tab["id"]).replace("-", " ").title()
            lines.append(f"## {title} ({len(group)})")
            lines.append("")
            lines.append("| Card | Title | Status | Assignee | Tasks | Thread |")
            lines.append("|---|---|---|---|---|---|")
            for card in sorted(group, key=lambda c: (_status_order(c), c.rank, str(c.path))):
                rel = str(card.path.relative_to(self.root))
                items = card.tasks()
                done = sum(1 for i in items if i.done)
                tasks = f"{done}/{len(items)}" if items else ""
                thread = self.thread_path(card.id or "", card.private) if card.id else None
                link = (f"[{len(self.thread(card.id, card.private))}]"
                        f"({THREADS_FOLDER}/{card.id}.md)") if thread and thread.exists() else ""
                lines.append(f"| `#{card.id or '????'}` | [{_escape_cell(card.title)}]({rel}) "
                             f"| {card.status} | {_escape_cell(str(card.front.get('assignee') or ''))} "
                             f"| {tasks} | {link} |")
            lines.append("")
        return "\n".join(lines).rstrip("\n") + "\n"

    def write_index(self, include_private: bool = False) -> Path:
        path = self.root / BOARD_INDEX
        _atomic_write(path, self.index_markdown(include_private))
        return path


_STATUS_ORDER = ["inbox", "discussing", "ready", "draft", "approved", "executing", "in-progress",
                 "needs-review", "needs-labels", "needs-ab", "needs-qa-llm", "needs-qa-human",
                 "active", "deferred", "retired", "done", "dropped"]


def _status_order(card: Card) -> int:
    try:
        return _STATUS_ORDER.index(card.status)
    except ValueError:
        return len(_STATUS_ORDER)


def _escape_cell(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ")


def _task_cycle(items: Sequence[TaskItem]) -> list[str] | None:
    graph = {i.item_id: [b for b in i.blocked_by if not b.startswith("#")]
             for i in items if i.item_id}
    state: dict[str, int] = {}
    stack: list[str] = []

    def walk(node: str) -> list[str] | None:
        state[node] = 1
        stack.append(node)
        for nxt in graph.get(node, []):
            if nxt not in graph:
                continue
            if state.get(nxt) == 1:
                return stack[stack.index(nxt):] + [nxt]
            if state.get(nxt, 0) == 0:
                found = walk(nxt)
                if found:
                    return found
        state[node] = 2
        stack.pop()
        return None

    for node in graph:
        if state.get(node, 0) == 0:
            found = walk(node)
            if found:
                return found
    return None


# ----------------------------------------------------------------- card creation

def card_filename(title: str, when: datetime | None = None) -> str:
    when = when or datetime.now()
    slug = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:48].strip("-") or "card"
    return f"{when.strftime('%Y-%m-%d')}-{slug}.md"


def new_card(card_type: str, title: str, status: str, *, card_id: str | None = None,
             rank: str | None = None, request: str | None = None, created: str | None = None,
             private: bool = False, **fields) -> Card:
    """Build a card in canonical form (not written to disk)."""
    if card_type not in CARD_TYPES:
        raise BoardError(f"unknown card type {card_type!r}")
    if status not in STATUS_FOLDER[card_type]:
        raise BoardError(f"unknown {card_type} status {status!r}")
    front: dict = {"id": card_id or new_id(), "type": card_type, "status": status}
    front.update({k: v for k, v in fields.items() if v is not None})
    if private:
        front["private"] = True
    front["rank"] = rank or initial_ranks(1)[0]
    front["created"] = created or datetime.now().strftime("%Y-%m-%d")
    front.setdefault("links", {"plans": [], "commits": [], "evidence": [], "related": [], "github": None})
    body = f"# {title}\n"
    if request:
        body += f"\n## Request\n{request.rstrip()}\n"
    card = Card(front=front, body=body, dirty=True)
    return card


# --------------------------------------------------------------------- migration

#: `- **Header**: value` names of the pre-board tracker -> front matter keys.
LEGACY_HEADERS = {
    "status": "status", "component": "component", "milestone": "milestone",
    "workstream": "workstream", "acceptance evidence": "acceptance", "assignee": "assignee",
    "source": "source", "label count": "label_count", "label output": "label_output",
    "codebook": "codebook",
}
LIST_FIELDS = ("component", "labels", "aliases", "paths", "supersedes", "blocked_by")
_HEADER_RE = re.compile(r"^-[ \t]+\*\*(?P<name>[^*]+?)\*\*:[ \t]*(?P<value>.*)$")
_DATE_PREFIX_RE = re.compile(r"^(\d{4}-\d{2}-\d{2})-")


@dataclass
class LegacyCard:
    title: str
    headers: dict[str, str]
    body: str
    extra_headers: list[str] = field(default_factory=list)
    repeated_headers: list[str] = field(default_factory=list)


def parse_legacy(text: str) -> LegacyCard:
    """Parse `# Title` + the `- **Field**: value` header block of a pre-board issue.

    The body keeps the H1 and every following byte; only the header block (and
    the blank line that separated it from the H1) is removed.
    """
    lines = text.split("\n")
    h1 = next((i for i, line in enumerate(lines) if line.startswith("# ")), None)
    if h1 is None:
        raise BoardError("no '# ' title")
    start = h1 + 1
    while start < len(lines) and not lines[start].strip():
        start += 1
    if start >= len(lines) or not _HEADER_RE.match(lines[start]):
        raise BoardError("no '- **Field**: value' header block after the title")
    headers: dict[str, str] = {}
    extra: list[str] = []
    repeated: list[str] = []
    index = start
    current: str | None = None
    while index < len(lines):
        line = lines[index]
        match = _HEADER_RE.match(line)
        if match:
            name = " ".join(match.group("name").split()).lower()
            key = LEGACY_HEADERS.get(name)
            if key is None:
                key = re.sub(r"[^a-z0-9]+", "_", name).strip("_")
                extra.append(name)
            if key in headers:
                repeated.append(name)
                headers[key] = headers[key] + " " + match.group("value").strip()
            else:
                headers[key] = match.group("value").strip()
            current = key
            index += 1
            continue
        if current and line[:1] in (" ", "\t") and line.strip():
            headers[current] = (headers[current] + " " + line.strip()).strip()
            index += 1
            continue
        break
    if "status" not in headers:
        raise BoardError("header block has no Status")
    body_head = lines[:start - 1] if start > 0 and not lines[start - 1].strip() else lines[:start]
    body = "\n".join(body_head + lines[index:])
    return LegacyCard(lines[h1][2:].strip(), headers, body, extra, repeated)


@dataclass
class Migration:
    """What `migrate` did (or would do) to one file."""
    path: str
    card_id: str = ""
    status: str = ""
    rank: str = ""
    action: str = "convert"
    note: str = ""


@dataclass
class MigrationReport:
    migrations: list[Migration] = field(default_factory=list)
    skipped: list[Migration] = field(default_factory=list)
    created: list[str] = field(default_factory=list)
    applied: bool = False

    @property
    def converted(self) -> list[Migration]:
        return [m for m in self.migrations if m.action == "convert"]

    def summary(self) -> str:
        out = [f"{'migrated' if self.applied else 'would migrate'}: {len(self.converted)} card(s)"]
        statuses: dict[str, int] = {}
        for item in self.converted:
            statuses[item.status] = statuses.get(item.status, 0) + 1
        for status, count in sorted(statuses.items()):
            out.append(f"  {status}: {count}")
        already = [m for m in self.migrations if m.action == "already"]
        if already:
            out.append(f"already in board format: {len(already)}")
        if self.skipped:
            out.append(f"could not parse: {len(self.skipped)}")
            out.extend(f"  {m.path}: {m.note}" for m in self.skipped)
        notes = [m for m in self.converted if m.note]
        if notes:
            out.append(f"notes: {len(notes)}")
            out.extend(f"  {m.path}: {m.note}" for m in notes)
        if self.created:
            out.append(f"{'created' if self.applied else 'would create'}: {', '.join(self.created)}")
        return "\n".join(out)


def migrate(issues_dir: str | os.PathLike, apply: bool = False,
            repo: str | os.PathLike | None = None) -> MigrationReport:
    """Convert a pre-board `issues/` tree to the card format.

    Deterministic: ids come from a hash of the file's path, ranks from the file
    order inside each category, so two runs produce identical bytes and the
    commit can be reviewed line by line.  Bodies are kept byte-for-byte apart
    from the removed header block.
    """
    board = Board(issues_dir, repo)
    root = board.root
    report = MigrationReport(applied=apply)
    paths = board.card_paths()
    taken: set[str] = set()

    by_category: dict[str, list[Path]] = {}
    parsed: dict[Path, tuple[Card | None, LegacyCard | None, str]] = {}
    for path in paths:
        rel = str(path.relative_to(root))
        text = path.read_text(encoding="utf-8")
        card = Card.parse(text, path)
        if card.front:
            parsed[path] = (card, None, "already")
            if card.id:
                taken.add(card.id)
            continue
        try:
            legacy = parse_legacy(text)
        except BoardError as exc:
            report.skipped.append(Migration(rel, action="skip", note=str(exc)))
            continue
        parsed[path] = (None, legacy, "convert")
        by_category.setdefault(board.category_of(path), []).append(path)

    ranks: dict[Path, str] = {}
    for category, group in sorted(by_category.items()):
        group = sorted(group)
        for path, rank in zip(group, initial_ranks(len(group))):
            ranks[path] = rank

    writes: list[tuple[Path, str]] = []
    for path in paths:
        if path not in parsed:
            continue
        card, legacy, action = parsed[path]
        rel = str(path.relative_to(root))
        if action == "already":
            report.migrations.append(Migration(rel, card.id or "", card.status, card.rank, "already"))
            continue
        assert legacy is not None
        card_id = derived_id(rel, taken)
        taken.add(card_id)
        front: dict = {"id": card_id, "type": "work"}
        status = str(legacy.headers.get("status", "")).strip().lower()
        status = LEGACY_STATUS.get(status, status)
        folder_status = _status_from_folder(board, path)
        note = ""
        if folder_status and folder_status != status:
            note = f"header said {status!r}; folder says {folder_status!r} (folder wins)"
            status = folder_status
        front["status"] = status
        for key, value in legacy.headers.items():
            if key == "status":
                continue
            value = value.strip()
            if key == "assignee" and value.lower() in ("unassigned", "none", "-", ""):
                continue
            if not value:
                continue
            front[key] = [v.strip() for v in value.split(",") if v.strip()] if key in LIST_FIELDS else value
        front["rank"] = ranks[path]
        date = _DATE_PREFIX_RE.match(path.name)
        if date:
            front["created"] = date.group(1)
        front["links"] = {"plans": [], "commits": [], "evidence": [], "related": [], "github": None}
        new = Card(front=front, body=legacy.body, path=path, dirty=True)
        if legacy.extra_headers:
            note = (note + "; " if note else "") + "unknown header(s): " + ", ".join(legacy.extra_headers)
        if legacy.repeated_headers:
            note = (note + "; " if note else "") + "repeated header(s) joined: " + ", ".join(legacy.repeated_headers)
        report.migrations.append(Migration(rel, card_id, status, ranks[path], "convert", note))
        writes.append((path, new.to_text()))

    scaffold = _scaffold(board)
    for target, _ in scaffold:
        try:
            report.created.append(str(Path(target).relative_to(board.repo)))
        except ValueError:  # pragma: no cover - repo outside the issues dir
            report.created.append(target)
    if apply:
        for path, text in writes:
            _atomic_write(path, text)
        for target, text in scaffold:
            _atomic_write(Path(target), text)
        (root / THREADS_FOLDER).mkdir(parents=True, exist_ok=True)
        board.write_index()
    return report


def _status_from_folder(board: Board, path: Path) -> str:
    parts = Path(path).relative_to(board.root).parts
    if parts and parts[0] == PRIVATE_FOLDER:
        parts = parts[1:]
    if len(parts) < 2:
        return ""
    sub = parts[1] if len(parts) > 2 else ""
    if parts[0] == PLAN_FOLDER:
        table = PLAN_STATUS_FOLDER
    elif parts[0] == MEMORY_FOLDER:
        table = MEMORY_STATUS_FOLDER
    else:
        table = WORK_STATUS_FOLDER
    if not sub:
        return ""
    for status, folder in table.items():
        if folder == sub and status not in ("dropped", "retired"):
            return status
    return ""


def _scaffold(board: Board) -> list[tuple[str, str]]:
    """The files `migrate` adds beside the cards, as (path, content) pairs."""
    out: list[tuple[str, str]] = []
    if not board.config_path.exists():
        out.append((str(board.config_path), CONFIG_TEXT))
    gitignore = board.root / ".gitignore"
    if not gitignore.exists():
        out.append((str(gitignore), GITIGNORE_TEXT))
    elif PRIVATE_FOLDER + "/" not in gitignore.read_text(encoding="utf-8"):
        out.append((str(gitignore), gitignore.read_text(encoding="utf-8").rstrip("\n") + "\n" + GITIGNORE_TEXT))
    keep = board.root / THREADS_FOLDER / ".gitkeep"
    if not keep.exists():
        out.append((str(keep), ""))
    attributes = board.repo / ".gitattributes"
    existing = attributes.read_text(encoding="utf-8") if attributes.exists() else ""
    if GITATTRIBUTES_LINE not in existing:
        header = "# Card threads are append-only; a union merge keeps both sides' entries.\n"
        out.append((str(attributes), (existing.rstrip("\n") + "\n\n" if existing.strip() else "")
                    + header + GITATTRIBUTES_LINE + "\n"))
    return out
