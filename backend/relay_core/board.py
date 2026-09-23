# SPDX-License-Identifier: AGPL-3.0-or-later
"""Board file format: cards, tasks, threads, ids, ranks (phase 0).

The board *is* a folder in the project -- `board/` on a board created from
2026-09-21 on, `.switchboard/`, `switchboard/` or `issues/` on an older one
(`BOARD_FOLDERS`, in that precedence order): one Markdown
file per card (YAML front matter plus a Markdown body), one append-only thread
per card under `threads/`, and a generated index in `BOARD.md`.  Nothing here
picks the folder; it is given one.  See `docs/BOARD-FORMAT.md`.

This module never calls a model and never talks to the network.  It owns
parsing, ids, ranks, task markers, thread appends, atomic hash-checked writes
and the `check` rules; the GUI and the tools layer build on it.

Only a small, explicit YAML subset is read and written (flat scalars, flow
sequences and flow mappings), so the backend keeps its stdlib-only dependency
list and the emitted bytes stay stable and diff-friendly.
"""
from __future__ import annotations

from . import filelock as fcntl
import hashlib
import os
from .filelock import chmod_fd
import re
import secrets
import stat
import subprocess
import tempfile
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable, Iterable, Mapping, Sequence

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


def relative_name(path: str | os.PathLike, root: Path) -> str:
    """`str(path.relative_to(root))`, as a string slice.

    Everything that walks a board asks this of every file it sees, and `pathlib.relative_to`
    rebuilds both paths from their parsed parts to answer: it was 1.17 s of the 2.26 s
    `relay-board.py index` spends at 3,000 cards, and most of what a fully cached
    `board_refresh` still cost once the parsing was gone (#7M6E).  A path that is not under
    `root` falls through to pathlib, which is the one that raises about it.
    """
    text, base = str(path), str(root)
    if text.startswith(base) and text[len(base):len(base) + 1] == os.sep:
        return text[len(base) + 1:]
    return str(Path(path).relative_to(root))


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

#: Card #X7NB, owner 2026-09-20: there is no `plan` card type. A plan is the `## Plan`
#: section of the work card it plans (protocol 19.10, design 12.4), and plan mode writes
#: its own files under `<root>/.relay/plans`; neither is a card.
CARD_TYPES = ("work", "memory", "alias")

#: status -> state subfolder inside the category folder ("" = the category itself).  The stage
#: statuses of card #3XZV — `planning`, `planned`, `executing`, `needs-verification` — live in the
#: category folder itself, like `inbox`: a stage move is a front-matter change, never a file move.
WORK_STATUS_FOLDER = {
    "inbox": "", "discussing": "", "planning": "", "planned": "", "ready": "",
    "executing": "", "in-progress": "", "needs-verification": "",
    "needs-qa-llm": "needs_qa_llm", "needs-qa-human": "needs_qa_human",
    "needs-review": "needs_review", "needs-labels": "needs_labels", "needs-ab": "needs_ab",
    "deferred": "deferred", "done": "done", "dropped": "done",
}
#: `suggested` and `rejected` (#MEMS) are memory_suggestions' pending and declined facts; neither is loaded.
MEMORY_STATUS_FOLDER = {"active": "", "retired": "archive", "suggested": "suggestions", "rejected": "rejected"}
#: Aliases (issue G8DK): saved commands and prompts, same two states as memory.
ALIAS_STATUS_FOLDER = {"active": "", "retired": "archive"}

STATUS_FOLDER: dict[str, dict[str, str]] = {
    "work": WORK_STATUS_FOLDER, "memory": MEMORY_STATUS_FOLDER, "alias": ALIAS_STATUS_FOLDER,
}

#: Legacy header statuses from the pre-board tracker.
LEGACY_STATUS = {"open": "ready", "needs-qa": "needs-qa-llm"}

MEMORY_FOLDER = "memory"
ALIAS_FOLDER = "aliases"
THREADS_FOLDER = "threads"
PRIVATE_FOLDER = ".private"
BOARD_CONFIG = "board.yaml"
BOARD_INDEX = "BOARD.md"

COMMON_FIELDS = ("id", "type", "status", "priority", "rank", "created", "labels", "assignee",
                 "private", "links", "aliases", "source", "blocked_by", "parent", "waiting_on")
#: `verified_by` is the signature of the model that closed the card out of a QA lane, stamped by
#: the worker exactly as `implemented_by` is (card #T71W): the pair is the audit trail of who
#: wrote a card and who passed it, and the reason a closed card can still be asked "who checked this?".
#: `session` is the pane session token of the terminal pane that holds the card (#R9G7): written
#: by `board_claim` from the pane's own `configure` token, never typed by a model, and read by
#: every other session as "this one is taken". A card in `executing` with a `session` is claimed.
WORK_FIELDS = ("component", "milestone", "workstream", "acceptance", "implemented_by",
               "verified_by", "session", "label_count", "label_output", "codebook",
               # The manual section a card is parked in (#3XZV): the id of a configured column
               # that collects no status. It wins over the status for as long as that column
               # exists, and a move to a status column is what clears it.
               "section")
MEMORY_FIELDS = ("name", "description", "kind", "topic", "scope", "paths", "pinned",
                 "supersedes", "reviewed", "author",
                 "origin", "suggested", "rejected", "reason")
#: An alias card (issue G8DK): `name` is what you type, `kind` is command or prompt,
#: `shell` records which shell an imported command came from. The runnable text and the
#: parameter defaults live in the body, because front matter scalars are single-line and a
#: default may hold any character (see relay_core/aliases.py).
ALIAS_FIELDS = ("name", "kind", "shell")

ALLOWED_FIELDS = {
    "work": set(COMMON_FIELDS) | set(WORK_FIELDS),
    "memory": set(COMMON_FIELDS) | set(MEMORY_FIELDS),
    "alias": set(COMMON_FIELDS) | set(ALIAS_FIELDS),
}
#: Emission order; anything else follows, sorted, so a new key is never dropped.
FIELD_ORDER = ("id", "type", "status", "section", "name", "description", "kind", "topic", "scope",
               "private", "labels", "component", "milestone", "workstream", "assignee",
               "implemented_by", "verified_by", "session", "waiting_on", "parent", "blocked_by",
               "aliases",
               "paths", "pinned", "reviewed", "author", "supersedes",
               "label_count", "label_output", "codebook", "shell", "priority", "rank", "created",
               "acceptance", "source", "links")

TASK_HEADING = {"work": "Tasks", "memory": "Tasks", "alias": "Tasks"}

#: The section holding the user's own words about the card.  It was written `## Request`
#: until 2026-09-18, when the owner asked for the plainer "Issue"; new and edited cards write
#: `## Issue` and both spellings are read as the same section, so no existing card file has to
#: be rewritten (`relay_core.board_tools._section_span`).
ISSUE_HEADING = "Issue"
ISSUE_HEADINGS = ("issue", "request")

#: The work-card body schema (2026-09-20, #Z4HR): one section per workflow stage, in body
#: order, plus the two the merge and split tools write.  A section earns its place by recording
#: what its stage *produced* -- a fact that stays true -- so the body is the record of the
#: workflow and the append-only thread is what these sections digest.  `check` warns on a
#: `## ` heading outside it (`unknown_section`): a warning, never an error, because the board
#: predates the set by hundreds of cards and an error would invalidate it on day one; the
#: warning keeps the backlog visible and countable, and a card converts when it is next
#: touched.  `board_tools.AGENT_SECTIONS` derives the agent-writable set from it.  Memory and
#: alias cards have their own layouts (`## Run`, `## Parameters`) and are not checked.
#:
#: 2026-09-21 (#WC3E): `done means` -- the expectations, written *before* the work, so the
#: verifier has something it did not choose to check against -- plus `human qa`, `try it` and
#: `profile`, which the board had grown and `check` was warning on.  The list is complete: a
#: heading outside it is owner text, and nothing else is a stage.
CARD_SECTIONS = (
    "issue", "decisions", "discussion points", "planning notes", "done means", "plan", "tasks",
    "execution summary", "tests", "profile", "try it", "qa checklist", "human qa",
    "verdict", "resolution",
    "merged in", "split",
)

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
    """One `- [ ]` line of a card's `## Tasks`."""
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


def set_task_blockers(items: Sequence[TaskItem], blockers: Mapping[int, Sequence]) -> list[TaskItem]:
    """Write `blocked_by=` markers on items that are about to be written (format §2.5).

    `blockers` maps a **0-based position in `items`** to what blocks that item.  A reference is
    either an integer — another position in this same list — or a string: `#K7Q2` for a card,
    anything else for an item id already on this card.  Positions exist because the thing that
    blocks a new item is usually another new item, and a new item has no id until it is written;
    ids are assigned here first, so both halves of a freshly imported list can name each other.

    Raises `BoardError` for a reference that names nothing, an item that blocks itself, or a
    cycle — all three are `relay-board.py check` errors, and a write must not create one.
    """
    assign_item_ids(items)
    by_position = list(items)
    known = {i.item_id for i in by_position if i.item_id}
    for position, refs in (blockers or {}).items():
        if not isinstance(position, int) or isinstance(position, bool) \
                or not 0 <= position < len(by_position):
            raise BoardError(f"blocked_by: {position!r} is not one of the {len(by_position)} items")
        item = by_position[position]
        out: list[str] = []
        for ref in refs or ():
            if isinstance(ref, bool):
                raise BoardError("blocked_by: a reference is an item position, an item id or #CARD")
            if isinstance(ref, int):
                if not 0 <= ref < len(by_position):
                    raise BoardError(f"blocked_by: item {ref} is not one of the "
                                     f"{len(by_position)} items")
                if ref == position:
                    raise BoardError(f"blocked_by: item {position} cannot block itself")
                value = by_position[ref].item_id or ""
            else:
                value = str(ref).strip()
                if value.startswith("#"):
                    if not valid_id(value[1:].upper()):
                        raise BoardError(f"blocked_by: {value!r} is not a card id")
                    value = "#" + value[1:].upper()
                else:
                    value = value.lower()
                    if value not in known:
                        raise BoardError(f"blocked_by: {value!r} is not an item of this card")
                    if value == item.item_id:
                        raise BoardError(f"blocked_by: item {value} cannot block itself")
            if value and value not in out:
                out.append(value)
        item.blocked_by = out
    cycle = _task_cycle(by_position)
    if cycle:
        raise BoardError("blocked_by cycle: " + " -> ".join(cycle))
    return by_position


# ------------------------------------------------------------------------- cards

#: The card's priority flag (card #VKFV): −1…+3, 0 the default. 0 is never written — a card
#: with no flag carries no `priority` key, so the front matter of an unranked board stays clean.
PRIORITY_MIN, PRIORITY_MAX = -1, 3


def clamp_priority(value) -> int:
    """`value` as a priority flag, clamped into −1…+3. Raises BoardError on a non-integer."""
    if isinstance(value, bool) or not isinstance(value, int):
        try:
            value = int(str(value).strip())
        except (TypeError, ValueError):
            raise BoardError(f"priority must be an integer from {PRIORITY_MIN} to {PRIORITY_MAX}, "
                             f"not {value!r}") from None
    return max(PRIORITY_MIN, min(PRIORITY_MAX, value))


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
    def priority(self) -> int:
        """The row's flag (#VKFV): −1…+3, 0 when unset or unparsable — a broken value in the
        file must not take the board down, it reads as unflagged."""
        try:
            return clamp_priority(self.front.get("priority", 0))
        except BoardError:
            return 0

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
        if self.type == "memory":
            category = MEMORY_FOLDER
        elif self.type == "alias":
            category = ALIAS_FOLDER
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


#: Entry-id suffixes are read back as base 36; the alphabet sorts ASCII-wise, like the ids.
BASE36 = "0123456789abcdefghijklmnopqrstuvwxyz"


def next_entry_id(after: str | None, when: datetime | None = None) -> str:
    """A fresh entry id that sorts strictly after `after`.

    Entry ids are second-resolution, so two appends in the same second would otherwise land in
    random order and `check` would report the file as unsorted.  Within a second the two-character
    suffix is incremented in base 36; when it runs out the timestamp moves on by a second.
    """
    candidate = new_entry_id(when)
    if not after or candidate > after:
        return candidate
    stamp, _, suffix = after.partition("-")
    value = int(suffix, 36) + 1 if ENTRY_ID_RE.match(after) else 36 * 36
    if value < 36 * 36:
        return f"{stamp}-{BASE36[value // 36]}{BASE36[value % 36]}"
    later = datetime.strptime(stamp, "%Y%m%dT%H%M%SZ").replace(tzinfo=timezone.utc) + timedelta(seconds=1)
    return new_entry_id(later)


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

def atomic_write(path: Path, text: str, mode: int | None = None) -> str:
    """Public name for the hash-returning atomic write (used by relay_core/aliases.py)."""
    return _atomic_write(path, text, mode)


def append_to_thread(path: Path, add: Callable[[bytes], tuple[str, object]]) -> object:
    """Add to a thread file **by replacing it**, under the threads directory's own lock.

    `add` is handed the file's current bytes (empty for a file that is not there yet) and returns
    the text to add and whatever the caller wants back; it runs under the lock, so it can choose
    entry ids against what is actually on disk.

    This was an `O_APPEND` write under a lock on the file itself until 2026-09-20.  That kept
    every entry, including across a `merge=union` git merge — but a `QFileSystemWatcher`
    **directory** watch, which is what the board pane holds, does not fire when an existing
    file grows.  About two of every three thread writes therefore never reached the pane: 21 of 60
    in a one-write-a-second storm (#N5JJ).  Writing a temporary file and `os.replace`-ing it in
    creates and renames a directory entry, which the watch does see, and is what every other
    writer in this file already does (`_atomic_write`).

    The lock is on the threads **directory**, not on the file: `os.replace` gives the path a new
    inode, so a lock held on the old one would stop excluding anybody the moment the first writer
    landed its rename, and two appends could then each drop the other's entry.  A directory is one
    inode that no writer here replaces.  It is one lock for all of a board's threads, which costs
    nothing: an append is a read, a render and a rename.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    lock = fcntl.open_directory_lock(path.parent)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX)
        body = path.read_bytes() if path.exists() else b""
        text, result = add(body)
        prefix = ""
        if body:
            tail = body[-2:]
            prefix = "\n\n" if not tail.endswith(b"\n") else ("\n" if not tail.endswith(b"\n\n") else "")
        _atomic_write(path, body.decode("utf-8", "replace") + prefix + text)
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        os.close(lock)
    return result


def _atomic_write(path: Path, text: str, mode: int | None = None) -> str:
    data = text.encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    if mode is None:
        mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    fd, temp = tempfile.mkstemp(prefix=".relay-board-", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as out:
            chmod_fd(out.fileno(), mode)
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
    "columns": ["inbox", "discussing", "planning", "planned", "executing", "needs-verification",
                "needs-qa", "done"],
    "agent": {"autonomy": "auto", "max_creates_per_turn": 5},
    "memory": {"autonomy": "auto"},
    # Signals (#AQ6X decision 9): `auto_work` is whether a failing check nobody is on starts its
    # own agent thread.  The owner's "yes by default, but its optional", so a board that predates
    # the key behaves as if it were true (`relay_core.signal_threads.AUTO_WORK_DEFAULT`).
    "signals": {"auto_work": True},
}

CONFIG_TEXT = """\
# Board configuration. Format: docs/BOARD-FORMAT.md
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: design, folder: design}, {id: marketing, folder: marketing},
  {id: planning, folder: planning},
  {id: deferred, filter: "status:deferred"}, {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, planning, planned, executing, needs-verification, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
memory: {autonomy: auto}
signals: {auto_work: true}
"""

#: The folder a board is kept in, newest spelling first.  Owner's decision, 2026-09-21 (#1CXD):
#: a new board is created as `<project>/board/` — plain and visible, so an agent reaching for the
#: cards with a bare `rg` finds them instead of concluding the project has no board.  The other
#: half of that trade is taught rather than enforced: the generated pointer block and `POLICY.md`
#: say to exclude the folder from code searches (`rg -g '!board/'`), which is the job the hidden
#: spelling used to do by itself.  `.switchboard/` is what Relay created between 2026-09-19 and
#: that decision, `switchboard/` between 2026-09-18 and 2026-09-19, and `issues/` is the original
#: spelling — including this repository's own.  **Nothing moves a board that already exists.**
NEW_BOARD_FOLDER = "board"
HIDDEN_BOARD_FOLDER = ".switchboard"
VISIBLE_BOARD_FOLDER = "switchboard"
LEGACY_BOARD_FOLDER = "issues"

#: Where a project keeps its board, in precedence order: `board/board.yaml` first, then
#: `.switchboard/board.yaml`, then `switchboard/board.yaml`, then `issues/board.yaml`, and the
#: **first that exists wins**.
#: Reading is always tolerant — a board is used wherever it is found and nothing moves by itself —
#: so this one list, walked in this one order, is the only definition of "which folder is the
#: board" in the backend.  The C++ side repeats it once, in `relay::projects::boardFolders()`
#: (src/Projects.h), and `tests/projects_test.cpp` pins the two to the same order.
BOARD_FOLDERS = (NEW_BOARD_FOLDER, HIDDEN_BOARD_FOLDER, VISIBLE_BOARD_FOLDER, LEGACY_BOARD_FOLDER)

#: The folder a *new* board is created in.  A `configure` may still name one of the older
#: spellings in `board.folder` (protocol 19.1) for a project that wants it; nothing creates
#: `issues/` any more.
DEFAULT_BOARD_FOLDER = NEW_BOARD_FOLDER


def new_board_folder() -> str:
    """The folder name a board created now gets: `board`."""
    return NEW_BOARD_FOLDER


def board_folder(directory: str | os.PathLike) -> Path | None:
    """The board directory inside `directory`, or None.

    `BOARD_FOLDERS` order: `board/board.yaml`, then `.switchboard/board.yaml`, then
    `switchboard/board.yaml`, then `issues/board.yaml`.  The first that exists wins, so a project
    that somehow has two is read as the first of them and the others are left where they are.
    """
    here = Path(directory)
    for name in BOARD_FOLDERS:
        if (here / name / BOARD_CONFIG).is_file():
            return here / name
    return None


def gitattributes_line(folder: str = DEFAULT_BOARD_FOLDER) -> str:
    """The union-merge rule for a board's threads, written against its own folder name."""
    return f"{folder}/threads/*.md merge=union"


#: The `issues/` spelling, kept because it is what the repositories that already have a board
#: carry in their `.gitattributes`.
GITATTRIBUTES_LINE = gitattributes_line("issues")
GITIGNORE_TEXT = "# Private cards, plans, threads and memory (Board private root).\n.private/\n"


class Board:
    """The board tree (`board/`, or the older `.switchboard/`, `switchboard/` or `issues/` on a
    board that was filed before 2026-09-21): cards, threads, config and the check rules.  `repo`
    is the project that holds it."""

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
        for extra in (MEMORY_FOLDER, ALIAS_FOLDER):
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
                parts = tuple(relative_name(path, self.root).split(os.sep))
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
        parts = relative_name(path, self.root).split(os.sep)
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
        """Append one self-contained entry, atomically, so two processes (and a `merge=union`
        git merge) all keep every entry — see `append_to_thread`."""
        if kind not in ENTRY_KINDS:
            raise BoardError(f"unknown thread entry kind {kind!r}")
        path = self.thread_path(card_id, private)

        def add(body: bytes) -> tuple[str, ThreadEntry]:
            ids = [e.entry_id for e in parse_thread(body.decode("utf-8", "replace"))]
            # Under the lock, so the id is chosen against what is actually on disk.
            attributes = {"author": author, "kind": kind,
                          **{k: str(v) for k, v in attrs.items() if v is not None}}
            entry = ThreadEntry(next_entry_id(max(ids) if ids else None, when), attributes, text)
            return entry.render(), entry

        return append_to_thread(path, add)

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
    #
    # `check()` is the whole walk; the four pieces under it are the same work cut along the lines
    # of what invalidates each answer, so a caller that has already parsed the board can reuse
    # what it has (#7M6E).  One card's problems depend only on that file and on board.yaml; one
    # thread's depend only on that file, except the two that ask which cards exist; the rest are
    # about the board as a whole.  The board worker caches the first two per file against
    # the file's mtime and size, which is why `board_refresh` no longer parses the tree twice.
    def check(self, fix: bool = False) -> list[Problem]:
        problems: list[Problem] = []
        by_id: dict[str, list[str]] = {}
        seen_ids: set[str] = set()
        for path in self.card_paths():
            rel = relative_name(path, self.root)
            try:
                card = Card.load(path)
            except (BoardError, UnicodeDecodeError) as exc:
                problems.append(Problem("bad_card", rel, str(exc)))
                continue
            problems.extend(self.check_card(card, rel, fix))
            if card.id:
                by_id.setdefault(card.id, []).append(rel)
                seen_ids.add(card.id)
        for private in (False, True):
            directory = self.threads_dir(private)
            if not directory.is_dir():
                continue
            for path in sorted(directory.glob("*.md")):
                problems.extend(self.check_thread_name(path, seen_ids))
                problems.extend(self.check_thread(path, None, fix))
        problems.extend(self.check_whole_board(by_id))
        return sorted(problems, key=lambda p: (p.path, p.code))

    def check_card(self, card: Card, rel: str, fix: bool = False) -> list[Problem]:
        """One card file's problems, exactly as `check()` finds them.  They depend on the file
        and on board.yaml (the category folder a status belongs in, and `columns:` for a parked
        card's section) and on nothing else, so a caller may cache them per file."""
        return self._check_card(card, rel, fix)

    def check_thread_name(self, path: Path, card_ids: set[str]) -> list[Problem]:
        """The two thread problems that depend on what cards exist, so the rest can be cached
        while cards come and go: a file not named `<ID>.md`, and a thread no card owns."""
        rel = relative_name(path, self.root)
        card_id = path.stem.upper()
        if not valid_id(card_id):
            return [Problem("bad_thread_name", rel, "thread file is not named <ID>.md")]
        if card_id not in card_ids:
            return [Problem("orphan_thread", rel, f"no card has id {card_id}", "warning")]
        return []

    def check_thread(self, path: Path, entries: Sequence[ThreadEntry] | None,
                     fix: bool = False) -> list[Problem]:
        """One thread file's own problems: repeated or unsortable entry ids, an unknown kind, and
        entries a union merge left out of order.  `entries` is the already-parsed file when the
        caller has it; None reads and parses it here."""
        rel = relative_name(path, self.root)
        if entries is None:
            entries = parse_thread(path.read_text(encoding="utf-8"))
        problems: list[Problem] = []
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

    def check_whole_board(self, by_id: dict[str, list[str]]) -> list[Problem]:
        """What only the whole board can see: one id on two cards, and private files git tracks.
        `by_id` maps each card id to the paths (relative to the board root) that carry it."""
        problems: list[Problem] = []
        for card_id, paths in sorted(by_id.items()):
            if len(paths) > 1:
                problems.append(Problem("duplicate_id", paths[0],
                                        f"id {card_id} is used by {len(paths)} cards: {', '.join(paths)}"))
        problems.extend(self._check_private())
        return problems

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
        if "section" in card.front:
            section = card.front.get("section")
            if not isinstance(section, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", section):
                problems.append(Problem("bad_section", rel,
                                        f"section {section!r} must be a section id: lower-case letters, "
                                        "digits, - and _"))
            elif section not in [str(c) for c in (self.config().get("columns") or [])]:
                # A removed manual section leaves its parked cards behind; they fall back to
                # their status section until they are moved, and the warning says where.
                problems.append(Problem("dangling_section", rel,
                                        f"section {section!r} is not one of this board's columns; the "
                                        "card shows in its status section", "warning"))
        if not card.title:
            problems.append(Problem("missing_title", rel, "no '# ' heading in the body"))
        if card.type == "work":
            problems.extend(self._check_sections(card, rel))
        problems.extend(self._check_tasks(card, rel, fix))
        return problems

    def _check_sections(self, card: Card, rel: str) -> list[Problem]:
        """`## ` headings outside the stage schema (#Z4HR, CARD_SECTIONS).  A warning, never an
        error: the board predates the set by hundreds of cards, so an error would invalidate it
        on day one.  The warning keeps the backlog visible and countable; a card converts when
        it is next touched.  A parenthesized suffix (`## Decisions (owner, 2026-09-20)`) still
        names its section.  Only work cards are checked -- memory and alias cards have their
        own layouts."""
        known = set(CARD_SECTIONS) | set(ISSUE_HEADINGS)
        problems: list[Problem] = []
        for match in _SECTION_HEADING_RE.finditer(card.body):
            heading = match.group("heading").strip()
            base = heading.split(" (", 1)[0].strip().lower()
            if heading.lower() in known or base in known:
                continue
            problems.append(Problem("unknown_section", rel,
                                    f"`## {heading}` is not one of the card body sections "
                                    "(docs/BOARD-FORMAT.md, card file)", "warning"))
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

    def _check_private(self) -> list[Problem]:
        private = self.private_root()
        if not private.exists():
            return []
        if not in_git_checkout(self.repo):
            # A board in Relay's data directory has no repository to be tracked by; asking git
            # here would answer about whatever checkout the data directory happens to sit under.
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
                        f"{'…' if len(files) > 3 else ''}; add .private/ to {self.root.name}/.gitignore and "
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
                 f"{len(cards)} cards. Format: [docs/BOARD-FORMAT.md](../docs/BOARD-FORMAT.md).", ""]
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
        # Both files in the board are generated, so regenerating one refreshes the other -- but
        # only when it is already there: creating `POLICY.md` writes a project's instruction files
        # too (`policy_files`), which is the scaffold's job and needs the user's yes behind it.
        policy = self.root / POLICY_FILE
        if policy.is_file():
            want = policy_text(self)
            try:
                stale = policy.read_text(encoding="utf-8") != want
            except (OSError, UnicodeDecodeError):          # pragma: no cover - unreadable file
                stale = False
            if stale:
                _atomic_write(policy, want)
        return path


_STATUS_ORDER = ["inbox", "discussing", "planning", "planned", "ready", "draft", "approved",
                 "executing", "in-progress", "needs-verification", "needs-review", "needs-labels",
                 "needs-ab", "needs-qa-llm", "needs-qa-human", "active", "deferred", "retired",
                 "done", "dropped"]


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
        body += f"\n## {ISSUE_HEADING}\n{request.rstrip()}\n"
    card = Card(front=front, body=body, dirty=True)
    return card


# ------------------------------------------------------- where a card file belongs
#
# One copy of the three things everything that writes a card has to agree on: which tab a
# card is in, which folder a tab means, and where its file goes when it is created or when
# its status or tab changes.  `board_tools.BoardTools` and `forge_sync.ForgeSync` both write
# cards, and each used to carry its own version of all three; the sync's copy silently
# disagreed about plan and memory cards, which is the kind of drift this section exists to
# stop.  Tools that want their own error text catch `BoardError` and re-raise.

def tab_folders(board: "Board") -> dict[str, str]:
    """`{tab id: category folder}` for the tabs that are folders (not filters)."""
    return {str(t["id"]): str(t["folder"]) for t in board.tabs()
            if t.get("id") and t.get("folder")}


def tab_of(board: "Board", card: Card) -> str:
    """Which tab a card sits in.  Memories are their own tab, whatever the config."""
    if card.type == "memory":
        return "memory"
    category = board.category_of(card.path) if card.path else ""
    for tab_id, folder in tab_folders(board).items():
        if folder == category:
            return tab_id
    return category


def category_for_tab(board: "Board", tab, *, strict: bool = False) -> str:
    """The category folder a tab id means.

    `strict` is for the callers that are about to write a card: an unknown tab, or a tab that
    is a filter across categories rather than a folder, raises `BoardError` instead of being
    taken at face value.  Readers pass it through unchanged, as they always did.
    """
    folders = tab_folders(board)
    name = str(tab).strip().lower() if tab is not None else ""
    if not strict:
        return folders.get(name, name)
    if name in folders:
        return folders[name]
    known = ", ".join(sorted(folders)) or "(none)"
    if any(str(t.get("id")) == name for t in board.tabs()):
        raise BoardError(f"tab {tab!r} is a filter across categories, not a folder; "
                         "pick a category tab for the card.")
    raise BoardError(f"unknown tab {tab!r}; this board has: {known}")


def free_card_path(folder: Path, title: str) -> Path:
    """A path in `folder` that no file is using yet, for a card titled `title`."""
    path = folder / card_filename(title)
    for n in range(2, 60):
        if not path.exists():
            return path
        path = folder / card_filename(f"{title}-{n}")
    raise BoardError("could not find a free file name for the card.")


def write_new_card(board: "Board", card: Card, category: str) -> Path:
    """Write a brand-new card into its category folder and return the path it landed at.

    The card's `path` is set to it.  Nothing else about the card is touched: the caller has
    already built the front matter and the body.
    """
    folder = board.base_for(card.private) / card.expected_folder(category)
    folder.mkdir(parents=True, exist_ok=True)
    card.path = free_card_path(folder, card.title)
    _atomic_write(card.path, card.to_text())
    return card.path


def card_target_path(board: "Board", card: Card, category: str) -> Path | None:
    """Where this card's file belongs now, or None when it is already there.

    Called *before* the card is saved, with the status already set on it: the answer is what
    the new status and category ask for, and the caller decides what an occupied path means.
    """
    target = board.base_for(card.private) / card.expected_folder(category) / card.path.name
    return None if target == card.path else target


def move_card_file(card: Card, target: Path) -> Path:
    """Move a saved card's file to `target` and point the card at it."""
    target.parent.mkdir(parents=True, exist_ok=True)
    os.replace(card.path, target)
    card.path = target
    return target


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

    scaffold = scaffold_files(board)
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
    if parts[0] == MEMORY_FOLDER:
        table = MEMORY_STATUS_FOLDER
    elif parts[0] == ALIAS_FOLDER:
        table = ALIAS_STATUS_FOLDER
    else:
        table = WORK_STATUS_FOLDER
    if not sub:
        return ""
    for status, folder in table.items():
        if folder == sub and status not in ("dropped", "retired"):
            return status
    return ""


def in_git_checkout(path: str | os.PathLike) -> bool:
    """Whether `path` is inside a git working tree, decided without running git.

    A project that is not a checkout has nothing for `.gitattributes` to configure and nothing for
    "is this card already committed?" to be true of, and running git there answers about whatever
    repository happens to be an ancestor of it.  Everything git-specific asks this first.
    """
    here = Path(path)
    for directory in (here, *here.parents):
        if (directory / ".git").exists():
            return True
    return False


def _git_env() -> dict:
    """The environment git is run in: this process's, minus the three variables that would point it
    at another repository's index.  A worker started from a git hook inherits `GIT_INDEX_FILE`,
    `GIT_DIR` and `GIT_WORK_TREE`, and a `git mv` run with them writes the hook's index instead of
    the project's.
    """
    env = dict(os.environ)
    for name in ("GIT_INDEX_FILE", "GIT_DIR", "GIT_WORK_TREE"):
        env.pop(name, None)
    return env


def _git(repo: Path, *args: str, check: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                          timeout=30, check=check, env=_git_env())


@dataclass
class FolderMove:
    """What `rename_board_folder` did, for the event the GUI shows."""
    old: str                       # the old folder name, e.g. ".switchboard"
    new: str                       # the new folder name, e.g. "board"
    root: str                      # the board's new absolute path
    method: str                    # "git mv" or "rename"
    files: list[str] = field(default_factory=list)   # what else changed, relative to the project

    @property
    def hidden(self) -> bool:
        """Whether the board is hidden now.  Carried for `board_folder_changed`, whose `hidden`
        field is what a phone or a GUI one release behind still reads (protocol 19.17)."""
        return self.new.startswith(".")

    def summary(self) -> str:
        return f"{self.old}/ is now {self.new}/ ({self.method})"


def rename_board_folder(board: Board, to: str = DEFAULT_BOARD_FOLDER) -> FolderMove:
    """Move a board to `board/` (owner, 2026-09-21, #1CXD), from any of the older spellings.

    The one explicit action that moves an existing board: reading is tolerant and nothing moves by
    itself, so this runs only when the user asks for it.  `git mv` in a checkout, a plain rename
    elsewhere, and the project's `.gitattributes` line is rewritten against the new name so the
    threads keep their union merge.

    `to` is the folder to move to, and defaults to the one a new board would get.  The older
    spellings are still accepted as a target — `rename_board_folder(board, ".switchboard")` is
    what the retired hide/show action did (protocol 19.17's `{hidden}` shape, which
    `board_protocol` still maps for one release) — but `issues/` is never a target.

    Refused, with a `BoardError` that says why and changes nothing:

      * the board is already in that folder;
      * the board folder is `issues/` — the original spelling, which whole repositories refer to
        by name in their own instructions, scripts and hooks (this one does).  Relay never moves
        it; a project that wants the new spelling moves it by hand;
      * the target folder already exists;
      * a card under the board folder has uncommitted text, staged or not.  `git mv` could carry
        it, but the file would change path underneath whatever diff the user is reading; committing
        first makes the move one clean rename.  A folder that has only been *moved* before, and not
        edited, passes -- otherwise moving a board and moving it back needed a commit in between.
    """
    old = board.root.name
    target = to
    if target == LEGACY_BOARD_FOLDER or target not in BOARD_FOLDERS:
        raise BoardError(f"{target}/ is not a board folder Relay creates. The folders are "
                         + ", ".join(f"{f}/" for f in BOARD_FOLDERS if f != LEGACY_BOARD_FOLDER)
                         + ".")
    if old == LEGACY_BOARD_FOLDER:
        raise BoardError(
            f"This board is in {LEGACY_BOARD_FOLDER}/, the original spelling. Relay never renames "
            f"it: a repository's own instructions, scripts and hooks name that folder. Move it by "
            f"hand if you want {target}/.")
    if old == target:
        raise BoardError(f"This board's folder is already {target}/.")
    if old not in BOARD_FOLDERS:
        raise BoardError(f"This board is in {old}/, which is not one of Relay's board folders ("
                         + ", ".join(f"{f}/" for f in BOARD_FOLDERS) + "). Nothing was moved.")
    destination = board.root.parent / target
    if destination.exists():
        raise BoardError(f"{target}/ already exists in {board.root.parent}. Nothing was moved.")

    repo = Path(board.repo)
    git = in_git_checkout(board.root)
    tracked = False
    if git:
        listed = _git(repo, "ls-files", "-z", "--", old)
        tracked = listed.returncode == 0 and bool(listed.stdout.strip("\0").strip())
        if tracked:
            dirty = _git(repo, "status", "--porcelain", "--untracked-files=no", "--", old)
            if dirty.returncode == 0:
                # `git mv` can carry any of these, so the refusal is a promise rather than a
                # limitation: a card whose text is uncommitted must not change path underneath the
                # diff the user is reading.  Content changes refuse -- unstaged in the work tree
                # (the second column) or staged (`M` in the first).  A staged add, rename or
                # deletion passes: that is the structure of a previous hide or show of this same
                # folder, which git moves again without losing anything.
                changed = [line[3:].split(" -> ")[-1] for line in dirty.stdout.splitlines()
                           if len(line) > 3 and (line[1] != " " or line[0] == "M")]
                if changed:
                    raise BoardError(
                        "This board has uncommitted card changes, so Relay will not move it: "
                        + ", ".join(changed[:5])
                        + ". Commit them first and the move is one clean rename.")

    method = "rename"
    if git and tracked:
        moved = _git(repo, "mv", "--", old, target)
        if moved.returncode != 0:
            raise BoardError("git mv refused to move the board: "
                             + (moved.stderr.strip() or moved.stdout.strip() or "no reason given"))
        method = "git mv"
    else:
        # Not a checkout, or a board that has never been committed: there is nothing for git to
        # record, so the folder is renamed in place.
        try:
            board.root.rename(destination)
        except OSError as exc:
            raise BoardError(f"Could not rename {old}/ to {target}/: {exc}") from exc

    files: list[str] = []
    attributes = repo / ".gitattributes"
    if attributes.exists():
        before = attributes.read_text(encoding="utf-8")
        after = before.replace(gitattributes_line(old), gitattributes_line(target))
        if after != before:
            _atomic_write(attributes, after)
            files.append(".gitattributes")
    board.root = destination
    return FolderMove(old=old, new=target, root=str(destination), method=method, files=files)


# ------------------------------------------------------------------- the policy file
#
# A guest agent -- Claude Code or Codex started in a Relay pane -- never sees the worker's system
# prompt; managed harnesses have a limited board MCP bridge (#4NXH). What every guest reads
# is the project's own instruction files.  `<board>/POLICY.md` is the board's rules written
# out as a file in the board, and a marked block in `CLAUDE.md` / `AGENTS.md` is the pointer at it
# (card #R9G7; the owner's words: "tell claude md and agents md to read the relay system prompt").
# Both are generated: `policy_text` is the only author, `scaffold_files` rewrites a copy that has
# gone stale exactly as it would a missing one, and `scripts/relay-board.py policy` regenerates
# them on a board that already exists.

POLICY_FILE = "POLICY.md"
#: The rules, the same bytes the worker puts in its own system prompt (`board_tools.policy_text`).
POLICY_SOURCE = "board_policy.md"
#: The procedure rule 1 points at, as the bundled skill a pane agent loads with `/deliver`.
DELIVER_SKILL = "deliver"

#: The instruction-file block's fences.  What is between them is generated and replaced in place;
#: what is outside them is the project's own text and is never read, moved or rewritten.
POINTER_START = "<!-- relay:switchboard-policy start -->"
POINTER_END = "<!-- relay:switchboard-policy end -->"
#: Where the pointer goes: the two instruction files the guest CLIs read.  `AGENTS.md` is created
#: when it is missing (`_new_agents_text`); `CLAUDE.md` is only ever appended to.  **WARP.md is
#: never touched.**  It is first in `instructions.PROJECT_ORDER`, so it is the file Relay's own
#: agent loads -- and that agent already has the policy in its system prompt and the tools to go
#: with it, so a block there would be the one edit that changes what Relay itself reads.
POINTER_TARGETS = ("CLAUDE.md", "AGENTS.md")


def _package_text(name: str) -> str:
    return (Path(__file__).resolve().parent / name).read_text(encoding="utf-8")


def _bundled_skill_text(name: str) -> str:
    """A skill Relay ships, through `relay_core.skills` so there is one definition of where they live."""
    try:
        from . import skills
        root = skills.bundled_dir()
    except Exception:                                      # pragma: no cover - packaging slip
        root = Path(__file__).resolve().parent / "skills_bundled"
    return (Path(root) / name / "SKILL.md").read_text(encoding="utf-8")


def _without_frontmatter(text: str) -> str:
    lines = text.splitlines()
    if not lines or lines[0].strip() != "---":
        return text.strip("\n")
    for index in range(1, len(lines)):
        if lines[index].strip() in ("---", "..."):
            return "\n".join(lines[index + 1:]).strip("\n")
    return text.strip("\n")                                # pragma: no cover - unterminated


def _strip_html_comments(text: str) -> str:
    return re.sub(r"<!--.*?-->", "", text, flags=re.S).strip("\n").strip()


def _demote_headings(text: str) -> str:
    """Every ATX heading one level deeper, so a whole document nests under a `##` of ours.

    Fenced blocks are left alone -- a `# comment` line in a shell example is not a heading.
    """
    out, fenced = [], False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
        elif not fenced and re.match(r"^#{1,5} ", line):
            line = "#" + line
        out.append(line)
    return "\n".join(out)


def _in_this_board(text: str, folder: str) -> str:
    """The policy is written against `issues/`; a board may be `board/` or an older spelling."""
    return text if folder == LEGACY_BOARD_FOLDER else text.replace(LEGACY_BOARD_FOLDER + "/", folder + "/")


def _lane_folders() -> list[tuple[str, str]]:
    """`(status, folder)` for the work statuses whose file lives in a state subfolder."""
    return [(s, f) for s, f in WORK_STATUS_FOLDER.items() if f]


def _stage_statuses() -> list[str]:
    return [s for s, f in WORK_STATUS_FOLDER.items() if not f]


def _wrap(text: str, indent: str = "  ", width: int = 96) -> str:
    """One paragraph of generated prose, wrapped like hand-written Markdown.

    The appendix interpolates lists of statuses, folders and kinds into its sentences; without
    this the file would carry 400-character lines, which is not what anyone reading it in a
    terminal wants.
    """
    import textwrap
    return textwrap.fill(" ".join(text.split()), width=width, initial_indent="",
                         subsequent_indent=indent, break_long_words=False, break_on_hyphens=False)


def _relay_script(board: "Board") -> str:
    """How to run `relay-board.py` from this project, as far as this project can know.

    In Relay's own checkout the script is right there; in any other project it is not, and saying
    "run it from a Relay checkout, or skip it" is more use than a path that does not exist.
    """
    folder = board.root.name
    script = board.repo / "scripts" / "relay-board.py"
    arg = "" if folder == LEGACY_BOARD_FOLDER else f" --board {folder}"
    if script.is_file():
        return (_wrap(f"- `python3 scripts/relay-board.py{arg} check` validates every card, task "
                      "marker and thread in this board: folder against status, the front matter "
                      "fields, the ids, the ranks, the entry ids. Run it before you commit, and "
                      "`--fix` repairs what it can.") + "\n"
                + _wrap(f"- `python3 scripts/relay-board.py{arg} index` regenerates "
                        f"`{folder}/BOARD.md` after you add or move a card.") + "\n")
    return (_wrap("- `scripts/relay-board.py` ships with Relay, not with this project. From a "
                  "Relay checkout: `python3 <relay>/scripts/relay-board.py --board <this "
                  f"project>/{folder} check` validates every card, task marker and thread in this "
                  "board, and `index` regenerates its `BOARD.md`.") + "\n"
            + _wrap("- Without a Relay checkout, skip it and follow the format above by hand: "
                    "Relay revalidates the board and regenerates the index the next time it opens "
                    "the project.") + "\n")


def _format_reference(board: "Board") -> str:
    doc = "docs/BOARD-FORMAT.md"
    if (board.repo / doc).is_file():
        return f"`{doc}`"
    return f"`{doc}` in Relay's own repository"


def _appendix(board: "Board") -> str:
    folder = board.root.name
    tabs = tab_folders(board)
    tab_list = ", ".join(f"{tab} → `{name}/`" for tab, name in tabs.items()) or "none configured"
    lanes = ", ".join(f"`{f}/`" for f in dict.fromkeys(f for _, f in _lane_folders()))
    lane_pairs = ", ".join(f"`{s}` → `{f}/`" for s, f in _lane_folders())
    stages = ", ".join(f"`{s}`" for s in _stage_statuses())
    kinds = ", ".join(f"`{k}`" for k in ENTRY_KINDS)
    hidden = ""
    if folder.startswith("."):
        hidden = _wrap(f"- **`{folder}/` is a hidden folder and `rg` skips hidden folders by "
                       "default**, so a bare `rg` over this project finds no cards at all and it "
                       "is easy to conclude there is no board. Use `rg --hidden`, or `grep -r`, "
                       "or read the files by path.") + "\n"
    tab_bullet = _wrap(f"- This board's tabs are {tab_list}; memory cards are in `memory/`, "
                       "alias cards in `aliases/`.")
    id_bullet = _wrap(f"- `id`: four characters of `{ID_ALPHABET}` with at least one letter, and "
                      f"not one this board already uses — check `{folder}/BOARD.md`.")
    lane_bullet = _wrap("4. Move the file only if the status you are moving to has a folder of its "
                        f"own ({lane_pairs}). The stage statuses — {stages} — live in the tab "
                        "folder itself, so a claim moves no file.", indent="   ")
    entry_id_bullet = _wrap("- The entry id is `YYYYMMDDTHHMMSSZ-xx`: UTC to the second, then two "
                            f"characters of `{ITEM_ALPHABET}`. It must sort after every id already "
                            "in the file.")
    kinds_bullet = _wrap(f"- `kind` is one of {kinds}. A question for the user is `kind=question` "
                         "— numbered, each with your recommendation — and the card goes to "
                         "`status: discussing` with `waiting_on: owner` (rule 3). A decision the "
                         "user made is `kind=decision`, quoting their own words (rule 4).")
    move_paragraph = _wrap(f"Set `status:` and put the file where that status belongs ({lane_pairs}"
                           "; every other status stays in the tab folder). Landing work means "
                           "the status `needs-verification`, the evidence path in `links.evidence` "
                           "and the tests in `## Tests` — in the same commit as the change (rule "
                           "5), and no `## QA checklist`: that section is the verifier's record of "
                           "what it checked, and you are not the verifier. Nothing is ever "
                           "deleted: a card is closed by moving it to `done` or `dropped` (both in "
                           "`done/`) with the reason in the thread.", indent="")
    signals_paragraph = "\n".join([
        _wrap("A **signal** is one keyed item per failing check — a test, a build, a `check` "
              "problem — that a machine opens on its second consecutive failing execution and "
              "closes only when that check passes again (card #AQ6X). It is not a card and not in "
              f"git: it is folded out of `{folder}/.private/`, which is local to this machine, so "
              "there is nothing here to edit by hand and no file of yours to commit. You cannot "
              "mark one fixed; you run the check.", indent=""), "",
        _wrap("`python3 scripts/relay-board.py signals` lists the open ones. `signals claim <key> "
              "--as <your name>` says you are on one, and a signal somebody else holds is refused "
              "— read it, say what you are doing instead, and take it over only when the user says "
              "to. `signals release <key> --reason gave-up` files it as a bug card when you could "
              "not fix it; any other reason simply frees it. `signals dismiss <key> --reason "
              "environmental|flaky-known --comment '…' --until YYYY-MM-DD` hides one you have "
              "*shown* is not the code's fault, for at most a week: `wont-fix`, `expected` and a "
              "longer expiry are the user's, and every dismissal expires. `signals promote <key>` "
              "files the bug card by hand.", indent=""), "",
        _wrap("Two rules to know before you land work. A card cannot leave `needs-verification` "
              "while a signal it is answerable for is open, and closing the card does not close "
              "the signal — the machine's state wins. And a card a signal was promoted from "
              "carries a `## Signal` section that Relay rewrites in place: leave it alone, like "
              "`implemented_by`.", indent="")])
    format_paragraph = _wrap("Everything above names Relay's `board_*` tools. When a tool is unavailable "
                             "— including create/claim outside the five-tool bridge — reach the board by "
                             "editing files. Here is each call as a file edit. The bytes are "
                             f"specified in {_format_reference(board)}; invent no field and no "
                             "heading that is not there.", indent="")
    return f"""## Without the board tools

{format_paragraph}

### Read the board — `board_list`, `board_read`, `board_card_get`

- `{folder}/BOARD.md` is the generated index: one table per tab, ordered by status then rank, with
  each card's id, its title linked to its file, status, assignee, task progress and thread link.
{hidden}- A card is one file, `{folder}/<tab>/[<lane>/]<YYYY-MM-DD-slug>.md`, and its conversation and
  audit trail is `{folder}/threads/<ID>.md`. Read both before you decide a card is the one you
  want — a title match is not a match.
{tab_bullet}

### File a card — `board_create_card`

A new file in the tab's folder, named `YYYY-MM-DD-<slug>.md`:

```markdown
---
id: K7Q2
type: work
status: inbox
labels: [feature, switchboard]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'Claude Code in a Relay pane, 2026-09-20'
links: {{plans: [], commits: [], evidence: [], related: [], github: null}}
---
# A title of your own

## Issue
the user's words, verbatim
```

{id_bullet}
- `labels`: exactly one of `bug` or `feature`, plus the obvious area labels.
- `rank`: a string of `[0-9a-z]` that does not end in `0`, ordering the card in its column; `m`
  is the middle and anything the column is not using will do.
- `created` is today's date, single-quoted as in the example; `source` says where the request
  came from.
- `## Issue` is the user's request **verbatim** (rule 2). The `# ` title is yours, and there is
  exactly one of them.
- `{folder}/BOARD.md` is stale the moment you write the file: regenerate it (below), or leave it
  to Relay.

### Claim it — `board_claim`

1. Read the card first. A card in `executing` or `in-progress` that carries a `session:` is held
   by a Relay pane, and one with someone else in `assignee` is their work: comment on it, and take
   it over only when the user says to.
2. In the front matter set `status: executing` and `assignee: <your name>` — `claude-code`,
   `codex`, whatever names you in the thread.
3. Do **not** write `session:`. That is Relay's *pane* session token, written only by the pane that
   holds the card; it is immutable, so a patch naming one is refused. A guest has no pane token and
   does not need one: your `assignee` and your claim entry in the thread are what tell the next
   agent the card is taken.
{lane_bullet}
5. Append a `progress` entry to `{folder}/threads/<ID>.md` saying what you are about to do.

### Comment — `board_comment`, and every write you make

Append one self-contained entry to `{folder}/threads/<ID>.md`, never rewriting an earlier one:

```markdown
<!-- relay:entry 20260920T141203Z-c3 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 14:12
claimed this card; starting on backend/relay_core/board.py
```

{entry_id_bullet}
{kinds_bullet}
- The file is append-only and merges with `merge=union`: add at the end, and never reflow or
  re-sort what is there.
- Every card write gets an entry. The card body is the document a verifier reads; the thread is
  the record of who did what.

### Change a card — `board_update_card`

Edit the file: a front-matter value, or the `## Heading` section in the body the change belongs in
(a plan goes in `## Plan`, the expectations in `## Done means`, a verifier's record in
`## QA checklist`, tasks in `## Tasks` as
`- [ ] text <!-- t:xx -->`). Leave every other byte alone, and append a thread entry saying what
you changed. Never touch `implemented_by`, `verified_by` or `session`: Relay stamps all three, and
a value typed by hand is what makes the audit trail a lie.

### Move it — `board_move_card`

{move_paragraph}

### Signals — the faults the machine is tracking

{signals_paragraph}

### Check what you wrote

{_relay_script(board)}
"""


def policy_text(board: "Board") -> str:
    """`<board>/POLICY.md`: the board's rules for an agent that has no `board_*` tools.

    Generated, like `BOARD.md`, and from three sources: `board_policy.md` (the block the worker
    puts in its own system prompt, so a guest and a pane agent are told the same thing), the
    bundled `deliver` skill (the procedure rule 1 points at) and an appendix that maps each tool
    the two of them name onto the file edit that does the same job.  The board's own folder name is
    substituted throughout, because the policy is written against `issues/`.
    """
    folder = board.root.name
    rules = _in_this_board(_strip_html_comments(_package_text(POLICY_SOURCE)), folder)
    skill = _in_this_board(_demote_headings(_without_frontmatter(_bundled_skill_text(DELIVER_SKILL))),
                           folder)
    what = _wrap("These are the rules Relay gives its own terminal-pane agents in their system "
                 "prompt, written out for an agent working this project **without** Relay's "
                 "`board_*` tools: Claude Code, Codex, or anyone reading the repository. They "
                 "apply to you. The board is this project's record of what was asked and what was "
                 "done, so work goes through a card.", indent="")
    # The visible `board/` is in ordinary `rg` range (owner, 2026-09-21), which is how an agent
    # finds the cards at all -- and why it has to be told, once, to leave them out of a code
    # search.  A hidden board has the opposite problem, and the appendix covers that one.
    search = ("" if folder.startswith(".") else
              f" A code search over this project matches card text too, so leave the board out of "
              f"one: `rg -g '!{folder}/'`.")
    where = _wrap(f"The board is `{folder}/`: plain Markdown in git, one file per card, one "
                  f"append-only thread per card under `{folder}/threads/`, and a generated index "
                  f"in `{folder}/BOARD.md`.{search} Below are the rules; then the procedure they point at; "
                  "then an appendix that says how to make each `board_*` call by editing files, "
                  "which is the fallback for unavailable tools. When the `relay_board` MCP server "
                  "is connected, prefer the Board tools it actually exposes, including card creation "
                  "and claiming. Relay owns their identity and guardrails. Use the file fallback "
                  "only when the bridge or the needed tool is unavailable.", indent="")
    return f"""<!-- Generated by relay_core.board.policy_text (`relay-board.py policy`). Never hand-edited:
     every board scaffold rewrites it from backend/relay_core/board_policy.md and the bundled
     `deliver` skill, the way {folder}/BOARD.md is regenerated from the cards. Change those. -->
# Board policy — `{folder}/`

{what}

{where}

## The rules

{rules}

{skill}

{_appendix(board)}"""


def pointer_text(board: "Board") -> str:
    """The marked block that points an instruction file at `POLICY.md`."""
    folder = board.root.name
    return f"""{POINTER_START}
## Board (Relay)

This project has a Relay board in `{folder}/`: its cards are the record of what was asked and
what was done, in plain Markdown in git. Read them there or in `{folder}/BOARD.md`, and leave the
folder out of code searches with `rg -g '!{folder}/'`.

**Before doing work, read `{folder}/POLICY.md`** and follow it: check whether the request is already
done, find the card that asks for it or file one, claim it, plan on it if it needs a plan, do the
work, then land it in needs-verification with its evidence. The policy is the same one Relay's own
agents get in their system prompt; `{folder}/POLICY.md` also says how to do each of their `board_*`
tool calls by editing files, which is what you have.

<!-- Generated by Relay (relay_core.board.pointer_text): this block is replaced whenever the
     board scaffold runs. Edit around it, not inside it. -->
{POINTER_END}"""


def _with_pointer(text: str, block: str) -> str | None:
    """`text` with the pointer block appended or replaced in place.

    None when the file is already right, and None when it holds half a block -- one marker without
    the other is somebody's hand edit, and guessing which of their lines the block replaces is how
    a generator eats a file.
    """
    start, end = text.find(POINTER_START), text.find(POINTER_END)
    if start >= 0 and end > start:
        new = text[:start] + block + text[end + len(POINTER_END):]
    elif start >= 0 or end >= 0:
        return None
    else:
        new = (text.rstrip("\n") + "\n\n" if text.strip() else "") + block + "\n"
    return None if new == text else new


def _new_agents_text(board: "Board", block: str) -> str:
    """A project's first `AGENTS.md`: the pointer, over an import of what it must not shadow.

    `instructions.py` loads the **first** hit per directory in `PROJECT_ORDER` (WARP.md,
    AGENTS.override.md, AGENTS.md, CLAUDE.md, ...), so an `AGENTS.md` created beside a project's
    `CLAUDE.md` would stop Relay's own agent reading that CLAUDE.md.  A CLAUDE-style `@path` import
    on the first line is the fix: Relay resolves it (`instructions._imports`), and so do Claude Code
    and Codex, so the project's instructions still reach every prompt.
    """
    imports = [name for name in ("CLAUDE.md", "CLAUDE.local.md") if (board.repo / name).is_file()]
    lines = ["# Agent instructions", ""]
    if imports:
        lines += [f"@{name}" for name in imports]
        lines += ["",
                  _wrap("<!-- The import(s) above are this project's own instructions. They are "
                        "imported rather than repeated because a tool that reads AGENTS.md instead "
                        "of CLAUDE.md would otherwise miss them: Relay takes the first instruction "
                        "file it finds per directory (relay_core.instructions.PROJECT_ORDER), and "
                        "so do the CLIs. -->", indent="     "), ""]
    else:
        lines += ["Instructions for any coding agent working in this project.", ""]
    rules = sorted(p.name for p in (board.repo / ".claude" / "rules").glob("*.md")) \
        if (board.repo / ".claude" / "rules").is_dir() else []
    if rules:
        lines += ["Also read " + ", ".join(f"`.claude/rules/{name}`" for name in rules) + ".", ""]
    lines += [block, ""]
    return "\n".join(lines)


def pointer_files(board: "Board") -> list[tuple[str, str]]:
    """The instruction files that need the pointer block, as (path, whole new content) pairs.

    `CLAUDE.md` and `AGENTS.md` get the block when they exist, and `AGENTS.md` is created when it
    does not -- a project with only a `CLAUDE.md` gets one too, because the owner's decision is
    that both files point at the policy (#R9G7).  Nothing outside the markers is changed, and
    `WARP.md` is never written (`POINTER_TARGETS`).
    """
    block = pointer_text(board)
    out: list[tuple[str, str]] = []
    for name in POINTER_TARGETS:
        path = board.repo / name
        if not path.is_file():
            continue
        try:
            new = _with_pointer(path.read_text(encoding="utf-8"), block)
        except (OSError, UnicodeDecodeError):              # pragma: no cover - unreadable file
            continue
        if new is not None:
            out.append((str(path), new))
    agents = board.repo / "AGENTS.md"
    if not agents.exists():
        out.append((str(agents), _new_agents_text(board, block)))
    return out


def policy_files(board: "Board") -> list[tuple[str, str]]:
    """`POLICY.md` when it is missing or stale, plus the instruction files that need the pointer."""
    out: list[tuple[str, str]] = []
    path = board.root / POLICY_FILE
    want = policy_text(board)
    try:
        current = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        current = None
    if current != want:
        out.append((str(path), want))
    out.extend(pointer_files(board))
    return out


def write_policy(board: "Board") -> list[str]:
    """Regenerate `POLICY.md` and the instruction-file pointers; the paths written, repo-relative."""
    files = policy_files(board)
    for target, text in files:
        _atomic_write(Path(target), text)
    return [_repo_relative(board, target) for target, _ in files]


def _repo_relative(board: "Board", target: str) -> str:
    try:
        return str(Path(target).relative_to(board.repo))
    except ValueError:                                     # pragma: no cover - outside the project
        return target


def scaffold_files(board: Board) -> list[tuple[str, str]]:
    """The files a new board needs beside its cards, as (path, content) pairs.

    `.gitignore` goes inside the board folder; `.gitattributes` goes beside it in the project and
    names this board's own folder (`board/`, or an older `.switchboard/`, `switchboard/` or
    `issues/`), so a board gets the union-merge rule for the folder it actually has -- and keeps
    it when `rename_board_folder()` moves that folder.

    Then `POLICY.md` and the instruction-file pointers (`policy_files`), which are the only entries
    here that are rewritten rather than merely created: both are generated, so a stale copy is a
    missing one.  That is how a board gets the rules for the agents that have no `board_*` tools --
    every new board, since `board_tools.create_board` scaffolds -- and how
    `relay-board.py policy` refreshes an old one.
    """
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
    line = gitattributes_line(board.root.name)
    attributes = board.repo / ".gitattributes"
    existing = attributes.read_text(encoding="utf-8") if attributes.exists() else ""
    if line not in existing:
        header = "# Card threads are append-only; a union merge keeps both sides' entries.\n"
        out.append((str(attributes), (existing.rstrip("\n") + "\n\n" if existing.strip() else "")
                    + header + line + "\n"))
    out.extend(policy_files(board))
    return out


def scaffold(board: Board) -> list[str]:
    """Create an empty board on disk and return the files it wrote, relative to `board.repo`.

    Nothing calls this on its own: a project gets a board only after the user has said yes
    (protocol 19.12), either through `board_init` — the GUI already asked — or through the
    `board_init_request` round trip that the first card raises.  Reading a project that has no
    board creates nothing at all, so opening the board never leaves a folder behind.
    Safe to call on a board that already exists: it writes only what is missing.
    """
    files = scaffold_files(board)
    for target, text in files:
        _atomic_write(Path(target), text)
    (board.root / THREADS_FOLDER).mkdir(parents=True, exist_ok=True)
    out = []
    for target, _ in files:
        try:
            out.append(str(Path(target).relative_to(board.repo)))
        except ValueError:                          # pragma: no cover - repo outside the board dir
            out.append(target)
    return out


# ------------------------------------------------- whole-board cleanup operations
#
# The file half of `board_cleanup` (docs/AGENT-SESSIONS-PROTOCOL.md 19.9): merging two
# cards into one, splitting one card into several, and changing the board's own sections
# in `board.yaml`.  Everything here is deliberately *non-destructive*: no card file is ever
# removed, a merged card stays on disk as a `dropped` card that names its survivor, and the
# text it held is copied into the survivor before it is closed.  `relay_core.board_tools`
# wraps these as the agent tools and owns the thread events, the undo snapshots and the
# changelog; this module owns the bytes.

#: Where a survivor records the cards folded into it, and where a split card records the
#: cards that came out of it.
MERGED_HEADING = "Merged in"
SPLIT_HEADING = "Split"
RESOLUTION_HEADING = "Resolution"

#: How much of a merged card's body is copied into the survivor.  The whole card stays on
#: disk either way, so the copy is a convenience, not the record.
MERGE_BODY_CAP = 8000

#: The column ids a board may list in `board.yaml`'s `columns:` (the sections of the one
#: list the pane draws).  Anything else is a typo, and a typo would silently hide a lane.
COLUMN_IDS = ("inbox", "discussing", "planning", "planned", "ready", "in-progress", "waiting",
              "needs-verification", "needs-qa", "deferred", "done", "draft", "approved",
              "executing", "active", "retired")

#: Which statuses each of those columns collects when `board.yaml` does not say (design 3,
#: "Tabs and columns").  A board that merges two sections, or invents one, overrides this per
#: column in `column_statuses:`; `column_statuses_of()` is the one reader of both.
COLUMN_STATUSES = {
    "inbox": ["inbox"], "discussing": ["discussing"], "planning": ["planning"],
    "planned": ["planned"], "ready": ["ready"], "in-progress": ["in-progress"],
    "waiting": ["needs-review", "needs-labels", "needs-ab"],
    "needs-verification": ["needs-verification"],
    "needs-qa": ["needs-qa-llm", "needs-qa-human"],
    "done": ["done", "dropped"], "deferred": ["deferred"],
    # memory columns; `draft` and `approved` are only a board.yaml written before #X7NB
    # dropped the plan card type, and no card can carry either status now.
    "draft": ["draft"], "approved": ["approved"], "executing": ["executing"],
    "active": ["active"], "retired": ["retired"],
}

#: Every status a card of any type may carry: what a section is allowed to collect.
ALL_STATUSES = tuple(dict.fromkeys(s for folders in STATUS_FOLDER.values() for s in folders))


def column_statuses_of(config: dict, column: str) -> list[str]:
    """Which statuses `column` collects on a board configured this way.

    `column_statuses:` in `board.yaml` first — that is how a board merges two sections into one
    or invents a section of its own — then the default map, and finally the column id read as a
    status of its own, which is what makes `columns: [inbox, ready]` mean what it looks like.
    An explicit empty list is not "no entry": it is a section that collects nothing, one a
    person fills by hand (#3XZV), and it comes back exactly that way.
    """
    configured = (config or {}).get("column_statuses") or {}
    listed = configured.get(column) if isinstance(configured, dict) else None
    if isinstance(listed, list):
        return [str(s) for s in listed]
    return list(COLUMN_STATUSES.get(column) or [column])


def column_title_of(config: dict, column: str) -> str | None:
    """The name this board gives `column`, or None to let the reader use its own wording.

    A section is renamed for the people reading it, never by moving cards: `column_titles:` is a
    display name over a column id that stays exactly what it was.
    """
    titles = (config or {}).get("column_titles") or {}
    if not isinstance(titles, dict):
        return None
    title = titles.get(column)
    return str(title) if isinstance(title, str) and title.strip() else None

_HEADING_LINE_RE = re.compile(r"^(#{1,5})[ \t]+", re.M)
_SECTION_HEADING_RE = re.compile(r"^##[ \t]+(?P<heading>.+?)[ \t]*$", re.M)


def section_span(body: str, heading: str) -> tuple[int, int] | None:
    """(start of the section's text, end of the section) for a `## ` heading, or None."""
    wanted = heading.strip().lower()
    aliases = set(ISSUE_HEADINGS) if wanted in ISSUE_HEADINGS else {wanted}
    pattern = re.compile(r"^##[ \t]+(?P<heading>.+?)[ \t]*$", re.M)
    for match in pattern.finditer(body or ""):
        if match.group("heading").strip().lower() not in aliases:
            continue
        start = match.end() + (1 if body[match.end():match.end() + 1] == "\n" else 0)
        end = len(body)
        following = pattern.search(body, match.end())
        top = _H1_RE.search(body, match.end())
        for candidate in (following, top):
            if candidate is not None:
                end = min(end, candidate.start())
        return start, end
    return None


def section_text(body: str, heading: str) -> str:
    span = section_span(body, heading)
    return body[span[0]:span[1]].strip("\n") if span else ""


def append_body_section(body: str, heading: str, text: str) -> str:
    """Append `text` to a `## ` section, creating the section at the end when it is missing."""
    block = text.rstrip("\n") + "\n"
    span = section_span(body, heading)
    if span is None:
        prefix = body if body.endswith("\n") else body + "\n"
        return f"{prefix}\n## {heading}\n{block}"
    start, end = span
    kept = body[start:end].rstrip("\n")
    return body[:start] + (kept + "\n\n" if kept else "") + block + "\n" + body[end:]


def demote_headings(text: str, levels: int = 2) -> str:
    """Push every Markdown heading down, so a card's body can be quoted inside a section
    without its `## Issue` ending the section it was quoted into."""
    return _HEADING_LINE_RE.sub(lambda m: "#" * min(6, len(m.group(1)) + levels) + " ", text or "")


def merged_into(card: Card) -> str | None:
    """The id this card was merged into, when it was."""
    links = card.front.get("links")
    value = links.get("merged_into") if isinstance(links, dict) else None
    return str(value).upper() if value else None


def _link_list(card: Card, key: str) -> list[str]:
    links = card.front.get("links")
    value = links.get(key) if isinstance(links, dict) else None
    return [str(v).upper() for v in value] if isinstance(value, list) else []


def _set_link(card: Card, key: str, value) -> None:
    links = dict(card.front.get("links") or {})
    links[key] = value
    card.set("links", links)


def carry_thread(board: "Board", src_id: str, dst_id: str, *, src_private: bool = False,
                 dst_private: bool = False) -> int:
    """Copy a merged card's conversation into the survivor's thread, in one locked append.

    The carried entries keep their order, their text and their authorship, and each gains
    `from=<source card>` and `orig=<its id there>`, so the survivor's thread says where the
    words came from and the copy can be matched back to the original.  They are given *fresh*
    ids after the survivor's last one rather than their own: the file stays sorted (`relay-board
    check` reports an unsorted thread) and the two conversations read as "…and then this one was
    folded in".  Returns how many were copied.
    """
    entries = sorted(board.thread(src_id, src_private), key=lambda e: e.entry_id)
    if not entries:
        return 0
    def add(body: bytes) -> tuple[str, int]:
        ids = [e.entry_id for e in parse_thread(body.decode("utf-8", "replace"))]
        last = max(ids) if ids else None
        blocks = []
        for entry in entries:
            last = next_entry_id(last)
            attrs = {**entry.attrs, "from": src_id.upper(), "orig": entry.entry_id}
            blocks.append(ThreadEntry(last, attrs, entry.text).render())
        return "\n".join(blocks), len(entries)

    return append_to_thread(board.thread_path(dst_id, dst_private), add)


def merge_cards(board: "Board", into: Card, sources: Sequence[Card], *, reason: str,
                category_of: Callable[[Card], str] | None = None) -> dict:
    """Fold `sources` into `into` without deleting anything.

    The survivor keeps its own text and gains a `## Merged in` section holding each source's
    body (headings demoted, capped at `MERGE_BODY_CAP`), the union of the labels and
    `links.merged_from`.  Each source is rewritten with `links.merged_into`, a `## Resolution`
    naming the survivor, status `dropped`, and its file moves into the category's `done/`
    folder — so `#OLD` still resolves, to a card that says where the work went.  The source's
    thread is copied into the survivor's and then closed with an event.

    Returns `{into, into_path, into_hash, merged: [{id, title, path, was, entries}], others}`
    where `others` is the (path, bytes-before, original-path) triples the undo needs.
    """
    if into.id is None or into.path is None:
        raise BoardError("the surviving card needs an id and a path")
    if merged_into(into):
        raise BoardError(f"#{into.id} was itself merged into #{merged_into(into)}; merge into that one")
    category_of = category_of or (lambda card: board.category_of(card.path))
    others: list[tuple[Path, bytes | None, Path | None]] = []
    merged: list[dict] = []
    labels = [str(l) for l in (into.front.get("labels") or [])]
    related = _link_list(into, "related")
    from_ids = _link_list(into, "merged_from")
    today = datetime.now().strftime("%Y-%m-%d")

    for src in sources:
        if src.id is None or src.path is None:
            raise BoardError("a merged card needs an id and a path")
        if src.id == into.id:
            raise BoardError(f"#{into.id} cannot be merged into itself")
        if merged_into(src):
            raise BoardError(f"#{src.id} was already merged into #{merged_into(src)}")
        # The `# Title` heading is already the block's own header here, so it is dropped
        # rather than demoted; every other byte of the body is kept.
        body = _H1_RE.sub("", src.body, count=1).strip("\n")
        quoted = demote_headings(body)
        if len(quoted) > MERGE_BODY_CAP:
            quoted = quoted[:MERGE_BODY_CAP].rstrip() + f"\n\n[…truncated; the whole card is kept at {src.path.name}]"
        was = str(src.path.relative_to(board.repo))
        into.body = append_body_section(
            into.body, MERGED_HEADING,
            f"### #{src.id} — {src.title} ({today})\n\n"
            f"Merged from `{was}` ({src.status}): {reason}\n\n{quoted}")
        into.dirty = True
        for label in (src.front.get("labels") or []):
            if str(label) not in labels:
                labels.append(str(label))
        for other in _link_list(src, "related"):
            if other not in related and other != into.id:
                related.append(other)
        if src.id not in from_ids:
            from_ids.append(src.id)
        merged.append({"id": src.id, "title": src.title, "was": was, "src": src})

    if labels:
        into.set("labels", labels)
    if related:
        _set_link(into, "related", related)
    _set_link(into, "merged_from", from_ids)
    into_before = into.path.read_bytes()
    _atomic_write(into.path, into.to_text())

    for item in merged:
        src: Card = item.pop("src")
        before = src.path.read_bytes()
        _set_link(src, "merged_into", into.id)
        src.set("status", "dropped")
        target_dir = board.base_for(src.private) / src.expected_folder(category_of(src))
        target = target_dir / src.path.name
        if target != src.path and target.exists():
            raise BoardError(f"a different file already sits at {target.relative_to(board.repo)}")
        # The link is written for where the file ends up, not where it is now.
        src.body = append_body_section(
            src.body, RESOLUTION_HEADING,
            f"Merged into [#{into.id}]({_relative_link(target, into.path)}) on {today}: {reason}\n\n"
            f"Nothing was thrown away: the text above is also kept on #{into.id} under "
            f"`## {MERGED_HEADING}`, and this card stays here so `#{src.id}` keeps resolving.")
        src.dirty = True
        _atomic_write(src.path, src.to_text())
        moved_from = None
        if target != src.path:
            target_dir.mkdir(parents=True, exist_ok=True)
            os.replace(src.path, target)
            moved_from, src.path = src.path, target
        item["path"] = str(src.path.relative_to(board.repo))
        item["entries"] = carry_thread(board, src.id, into.id, src_private=src.private,
                                       dst_private=into.private)
        others.append((src.path, before, moved_from))

    return {"into": into.id, "into_path": str(into.path.relative_to(board.repo)),
            "into_before": into_before, "into_hash": file_hash(into.path),
            "merged": merged, "others": others}


def split_card(board: "Board", card: Card, parts: Sequence[dict], *, reason: str,
               category: str, close: bool = False,
               tab_category: Callable[[str], str] | None = None) -> dict:
    """Split one card into several, leaving the original in place as the record.

    Each part becomes a new card whose `## Issue` is the part's own verbatim excerpt of the
    original request, with `parent` set to the original.  The original gains a `## Split`
    section naming the children and `links.split_into`; with `close` it is additionally moved
    to `dropped` with a resolution, for a card whose every piece moved out.

    Returns `{id, path, hash, children: [{id, title, path}], others, closed}`.
    """
    if card.id is None or card.path is None:
        raise BoardError("the card being split needs an id and a path")
    if not 2 <= len(parts) <= 10:
        raise BoardError("a split makes between 2 and 10 cards")
    existing = board.cards()
    taken = [c.id for c in existing if c.id]
    today = datetime.now().strftime("%Y-%m-%d")
    others: list[tuple[Path, bytes | None, Path | None]] = []
    children: list[dict] = []
    before = card.path.read_bytes()

    for part in parts:
        title = str(part.get("title") or "").strip()
        request = str(part.get("request") or "").strip()
        if not title or not request:
            raise BoardError("every part of a split needs a title and a request")
        status = str(part.get("status") or card.status)
        if status not in STATUS_FOLDER[card.type]:
            raise BoardError(f"unknown {card.type} status {status!r}")
        part_category = category
        if part.get("tab") and tab_category is not None:
            part_category = tab_category(str(part["tab"]))
        labels = [str(l) for l in (part.get("labels") or card.front.get("labels") or [])]
        child_id = new_id(taken)
        taken.append(child_id)
        child = new_card(card.type, title, status, card_id=child_id, request=request,
                         rank=board.next_rank([c for c in existing if c.status == status]),
                         labels=labels or None, private=card.private,
                         parent=card.id, source=card.front.get("source") or None)
        links = dict(child.front.get("links") or {})
        links["split_from"] = card.id
        child.set("links", links)
        folder = board.base_for(card.private) / child.expected_folder(part_category)
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / card_filename(title)
        for n in range(2, 60):
            if not path.exists():
                break
            path = folder / card_filename(f"{title}-{n}")
        if path.exists():
            raise BoardError("could not find a free file name for a split card")
        child.path = path
        _atomic_write(path, child.to_text())
        existing.append(child)
        others.append((path, None, None))
        children.append({"id": child_id, "title": title, "status": status,
                         "path": str(path.relative_to(board.repo))})

    # Where the original ends up, so the links it writes point at the children from there.
    target = card.path
    if close:
        card.set("status", "dropped")
        target = board.base_for(card.private) / card.expected_folder(category) / card.path.name
        if target != card.path and target.exists():
            raise BoardError(f"a different file already sits at {target.relative_to(board.repo)}")
    listing = "\n".join(f"- [#{c['id']}]({_relative_link(target, board.repo / c['path'])}) — "
                        f"{c['title']} ({c['status']})" for c in children)
    card.body = append_body_section(card.body, SPLIT_HEADING,
                                    f"{today}: {reason}\n\n{listing}")
    _set_link(card, "split_into", [c["id"] for c in children])
    moved_from = None
    if close:
        card.body = append_body_section(
            card.body, RESOLUTION_HEADING,
            f"Split on {today} into {', '.join('#' + c['id'] for c in children)}: {reason}. "
            "The text above is kept here, and each piece is now its own card.")
    card.dirty = True
    _atomic_write(card.path, card.to_text())
    if target != card.path:
        target.parent.mkdir(parents=True, exist_ok=True)
        os.replace(card.path, target)
        moved_from, card.path = card.path, target
    return {"id": card.id, "path": str(card.path.relative_to(board.repo)),
            "hash": file_hash(card.path), "before": before, "moved_from": moved_from,
            "children": children, "others": others, "closed": bool(close)}


def _relative_link(from_path: Path, to_path: Path) -> str:
    """A Markdown link target from one card file to another, so the file reads on GitHub."""
    return os.path.relpath(str(to_path), str(Path(from_path).parent))


CONFIG_KEY_ORDER = ("version", "tabs", "columns", "column_statuses", "column_titles", "labels",
                    "agent", "memory", "signals")
CONFIG_HEADER = "# Board configuration. Format: docs/BOARD-FORMAT.md\n"

_FLOW_UNSAFE_RE = re.compile(r"[,:\[\]{}]")


def flow_value(value) -> str:
    """A value inside a flow sequence or mapping.

    `yaml_value` is written for front matter, where a comma or a colon inside a scalar is
    usually already quoted for another reason.  `board.yaml` has `filter: status:done,dropped`,
    which has to come back as one scalar, so anything with flow punctuation in it is quoted.
    """
    if isinstance(value, (list, tuple)):
        return "[" + ", ".join(flow_value(v) for v in value) + "]"
    if isinstance(value, dict):
        return "{" + ", ".join(f"{flow_value(k)}: {flow_value(v)}" for k, v in value.items()) + "}"
    if isinstance(value, str) and _FLOW_UNSAFE_RE.search(value):
        return _quote(value)
    return yaml_scalar(value)


def render_config(config: dict) -> str:
    """`board.yaml` as text: the known keys in a stable order, then anything else, sorted."""
    keys = [k for k in CONFIG_KEY_ORDER if k in config]
    keys += sorted(str(k) for k in config if k not in CONFIG_KEY_ORDER)
    return CONFIG_HEADER + "".join(f"{yaml_scalar(k)}: {flow_value(config[k])}\n" for k in keys)


def write_config(board: "Board", config: dict) -> str:
    """Replace `board.yaml` atomically.  Returns the new file hash."""
    text = render_config(config)
    parse_yaml(text)          # never leave a board.yaml the board cannot read back
    return _atomic_write(board.config_path, text)
