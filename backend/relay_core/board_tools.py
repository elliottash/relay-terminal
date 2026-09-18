# SPDX-License-Identifier: GPL-3.0-or-later
"""Switchboard agent tools: the six `board_*` tools and their guardrails (phase 1).

Design: `docs/SWITCHBOARD-DESIGN.md` sections 6.1-6.3 and the owner decisions in 12
(especially 12.3: owner text *may* be rewritten, but every change is logged in the card's
thread holding the old and the new text, so any rewrite can be reverted).

The tools sit on top of `relay_core.board`, which owns the bytes.  This module owns
*policy*: what an agent may change, how often, what has to be recorded, and how to undo
the last write.  It never calls a model and never uses the network.

Guardrails, in one place so they can be reviewed:

* **No delete tool.**  Closing a card is a move to `done` or `dropped` with a reason.
* **Every write appends a thread entry** naming the actor, model, pane and turn.  A change
  to owner-authored text additionally appends a `rewrite` entry carrying the old and the
  new text verbatim (decision 12.3).
* **Immutable fields:** `id`, `type`, `created`, `source`, `rank`, `status`, `private`
  are never writable through `board_update_card` (rank and status move).
* **Limits:** creates and other writes per turn, creates per hour per workspace; over the
  limit the tool returns `{"code": "board_rate_limited"}` instead of writing.
* **Atomic, hash-checked writes** through `board.Board.save`; a stale `base_hash` returns
  `{"code": "board_conflict", "current_hash": ...}` and nothing is overwritten.
* **Undo:** each write records a snapshot (file bytes plus the thread length before it),
  restorable for 30 s from the GUI toast; undoing a creation removes the file only when
  git has never seen it.
"""
from __future__ import annotations

import difflib
import fcntl
import json
import os
import re
import secrets
import subprocess
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Callable, Sequence

from . import board as B

AUTONOMY = ("off", "suggest", "auto")

#: Per-turn and per-hour ceilings (design 6.3).  `board.yaml` may lower the create ceiling.
DEFAULT_LIMITS = {
    "max_creates_per_turn": 5,
    "max_writes_per_turn": 20,
    "max_creates_per_hour": 30,
}

MAX_TITLE = 200
MAX_REQUEST = 8000
MAX_TEXT = 16384
MAX_REASON = 500
MAX_SECTION = 16384
MAX_LIST_LIMIT = 50
MAX_THREAD_ENTRIES = 50
UNDO_SECONDS = 30

#: Fields no agent write may touch.  `status` and `rank` belong to `board_move_card`;
#: `source` is the provenance of the owner's own words; `private` would move the file
#: between the git tree and the private root, which is the owner's decision.
IMMUTABLE_FIELDS = frozenset({"id", "type", "created", "source", "rank", "status", "private"})

#: Sections an agent writes freely.  Anything else in a card body is owner text: it may
#: still be rewritten (decision 12.3) but the old and new text go into the thread.
AGENT_SECTIONS = frozenset({
    "findings", "plan", "options", "implementer check", "qa checklist", "evidence",
    "notes", "tasks", "steps", "verdict", "qa verdict", "resolution", "decisions",
})

#: A card in a QA lane may only be closed by a different model family than the one that
#: implemented it (design 6.4, "the independence rule").
QA_STATUSES = ("needs-qa-llm", "needs-qa-human")

COMMENT_KINDS = ("note", "question", "decision", "evidence", "progress")

_WORD_RE = re.compile(r"[a-z0-9]+")
_QUOTE_RE = re.compile(r"[\"“”‘’']([^\"“”‘’']{3,})[\"“”‘’']")
_SECTION_RE = re.compile(r"^##[ \t]+(?P<heading>.+?)[ \t]*$", re.M)
_H1_RE = re.compile(r"^#[ \t]+(?P<title>.+?)[ \t]*$", re.M)


class BoardToolError(ValueError):
    """A refusal the model should read and correct.  Carries a machine-readable code."""

    def __init__(self, message: str, code: str = "board_refused", **extra):
        super().__init__(message)
        self.code = code
        self.extra = extra

    def to_result(self) -> dict:
        return {"error": str(self), "code": self.code, **self.extra}


def spec(name: str, description: str, properties: dict, required: list[str]) -> dict:
    return {"type": "function", "function": {"name": name, "description": description,
            "parameters": {"type": "object", "properties": properties, "required": required,
                           "additionalProperties": False}}}


_ID_ARG = {"type": "string", "description": "Card id, four characters (e.g. K7Q2); '#K7Q2' is accepted."}

TOOL_SPECS = [
    spec("board_list",
         "List Switchboard cards (the repository's issues/ tracker). One row per card: id, title, "
         "type, status, tab, labels, assignee, waiting_on and thread size. Search here before "
         "creating a card, so a request that already has one updates it instead.",
         {"tab": {"type": "string", "description": "Tab id from board.yaml, e.g. features, bugs, design, planning."},
          "status": {"type": "string", "description": "Exact status, e.g. inbox, ready, in-progress, needs-qa-llm, done."},
          "type": {"type": "string", "enum": list(B.CARD_TYPES), "description": "work (default view), plan or memory."},
          "labels": {"type": "array", "items": {"type": "string"}, "description": "Every label must be present."},
          "query": {"type": "string", "description": "Case-insensitive text matched against id, title and body."},
          "limit": {"type": "integer", "minimum": 1, "maximum": MAX_LIST_LIMIT}},
         []),
    spec("board_read",
         "Read one card: its front matter, the Markdown body (request, decisions, tasks, QA checklist) "
         "and the tail of its thread. The returned `hash` is what board_update_card needs.",
         {"id": _ID_ARG,
          "thread_entries": {"type": "integer", "minimum": 0, "maximum": MAX_THREAD_ENTRIES,
                             "description": "How many of the newest thread entries to return (default 20)."}},
         ["id"]),
    spec("board_create_card",
         "Create a card for a request that is not finished within this turn. `request` must be the "
         "user's own words, verbatim; the title is yours. Search with board_list first: if a card "
         "already covers the request, update that one instead. A fuzzy duplicate check can refuse the "
         "creation and return possible_duplicates; repeat the call with not_duplicate_of to override it.",
         {"tab": {"type": "string", "description": "Tab id from board.yaml (features, bugs, design, marketing, planning)."},
          "status": {"type": "string", "description": "inbox for raw capture, discussing when you need an answer, ready when agreed."},
          "title": {"type": "string", "description": "One line, your words; becomes the card's `# ` heading."},
          "request": {"type": "string", "description": "The user's words verbatim. Do not paraphrase or tidy them."},
          "type": {"type": "string", "enum": list(B.CARD_TYPES), "description": "work (default), plan or memory."},
          "labels": {"type": "array", "items": {"type": "string"}},
          "source": {"type": "string", "description": "Where the request came from, e.g. 'pane 2, 2026-09-17'."},
          "related": {"type": "array", "items": {"type": "string"}, "description": "Ids of related cards."},
          "not_duplicate_of": {"type": "array", "items": {"type": "string"},
                               "description": "Ids the duplicate check flagged that you have checked and rejected."}},
         ["tab", "status", "title", "request"]),
    spec("board_update_card",
         "Change a card's front matter fields or its body sections. Pass the `hash` from board_read as "
         "base_hash; the write is refused if the file changed meanwhile. id, type, status, rank, created, "
         "source and private are never writable here (status and rank move; the rest are the record). "
         "Rewriting text the user wrote is allowed, and the old and the new text are recorded in the "
         "card's thread so the change can be reverted.",
         {"id": _ID_ARG,
          "base_hash": {"type": "string", "description": "The `hash` returned by board_read."},
          "fields": {"type": "object", "description": "Front matter fields to set; null removes one.",
                     "additionalProperties": True},
          "title": {"type": "string", "description": "Replace the card's `# ` heading."},
          "append_section": {"type": "object", "description": "Append text to a `## ` section, creating it if needed.",
                             "properties": {"heading": {"type": "string"}, "text": {"type": "string"}},
                             "required": ["heading", "text"], "additionalProperties": False},
          "replace_section": {"type": "object", "description": "Replace a `## ` section's body outright.",
                              "properties": {"heading": {"type": "string"}, "text": {"type": "string"}},
                              "required": ["heading", "text"], "additionalProperties": False},
          "tasks": {"type": "array", "description": "Replace the `## Tasks` (or `## Steps`) checklist.",
                    "items": {"type": "object", "properties": {
                        "text": {"type": "string"},
                        "status": {"type": "string", "enum": list(B.ITEM_STATUSES)},
                        "item_id": {"type": "string", "description": "Keep an existing item's id (two characters)."},
                        "card": {"type": "string", "description": "Id of a card that mirrors this item."}},
                        "required": ["text"], "additionalProperties": False}}},
         ["id", "base_hash"]),
    spec("board_move_card",
         "Move a card to another status (column), another tab (category) or another position. `reason` is "
         "required and goes into the thread. Moving into needs-qa-llm or needs-qa-human requires an "
         "evidence path and an implemented_by model. Closing a card that is in a QA lane requires a "
         "verdict section in the body, and the model closing it must not be the family that implemented it.",
         {"id": _ID_ARG,
          "status": {"type": "string", "description": "Target status; the file moves into the matching state folder."},
          "tab": {"type": "string", "description": "Target tab id; the file moves into that category folder."},
          "before": {"type": "string", "description": "Id of the card this one should sit before in the column."},
          "after": {"type": "string", "description": "Id of the card this one should sit after in the column."},
          "reason": {"type": "string", "description": "Why, in one line. Recorded in the thread."},
          "evidence": {"type": "string", "description": "Evidence path, e.g. docs/qa_evidence/2026-09-17-slug/."},
          "implemented_by": {"type": "string", "description": "Model that implemented the change, when the card does not name one."}},
         ["id", "reason"]),
    spec("board_comment",
         "Append one entry to a card's thread: a note, a question for the user, a decision they made, "
         "evidence, or progress. A question is numbered and carries your recommendation. A decision "
         "quotes the user's own words in quotation marks. The thread is the card's discussion history "
         "and is append-only; nothing you write here is ever rewritten.",
         {"id": _ID_ARG,
          "kind": {"type": "string", "enum": list(COMMENT_KINDS)},
          "text": {"type": "string"}},
         ["id", "kind", "text"]),
]

TOOL_NAMES = tuple(s["function"]["name"] for s in TOOL_SPECS)
WRITE_TOOLS = ("board_create_card", "board_update_card", "board_move_card", "board_comment")


# ------------------------------------------------------------------ small helpers

def normalize_id(value, what: str = "id") -> str:
    if not isinstance(value, str):
        raise BoardToolError(f"{what} must be a card id such as K7Q2.")
    text = value.strip().lstrip("#").upper()
    if not B.valid_id(text):
        raise BoardToolError(f"{value!r} is not a card id: four Crockford-base32 characters with a letter, e.g. K7Q2.")
    return text


def model_family(model: str | None) -> str:
    """The vendor-ish family of a model string, for the QA independence rule.

    `anthropic/claude-opus-5` -> `anthropic`; `Claude Opus 5 (pane 2)` -> `claude`;
    `gpt-5-codex` -> `gpt`.  Deliberately coarse: it only has to tell two different
    providers apart, and an unknown string never blocks a move on its own.
    """
    if not isinstance(model, str) or not model.strip():
        return ""
    text = model.strip().lower()
    if "/" in text:
        return text.split("/", 1)[0]
    word = _WORD_RE.search(text)
    return word.group(0) if word else ""


def _words(text: str) -> set[str]:
    return {w for w in _WORD_RE.findall((text or "").lower()) if len(w) > 2}


def similarity(a: str, b: str) -> float:
    """How alike two card titles/requests are.

    Half a character-level ratio, half a word-overlap ratio.  Characters alone call two
    requests that share their boilerplate ("please add a ... to Relay") duplicates; words
    alone miss a reworded title.  Averaging them keeps an exact repeat at 1.0 while
    boilerplate on its own stays well under the threshold.
    """
    left, right = (a or "").strip().lower(), (b or "").strip().lower()
    if not left or not right:
        return 0.0
    chars = difflib.SequenceMatcher(None, left, right).ratio()
    wa, wb = _words(left), _words(right)
    overlap = len(wa & wb) / len(wa | wb) if wa and wb else chars
    return (chars + overlap) / 2


def section_headings(body: str) -> list[str]:
    return [m.group("heading").strip() for m in _SECTION_RE.finditer(body or "")]


def _section_span(body: str, heading: str) -> tuple[int, int, int] | None:
    """(start of the heading line, start of the section text, end of the section)."""
    wanted = heading.strip().lower()
    for match in _SECTION_RE.finditer(body):
        if match.group("heading").strip().lower() != wanted:
            continue
        text_start = match.end()
        if body[text_start:text_start + 1] == "\n":
            text_start += 1
        following = _SECTION_RE.search(body, match.end())
        top = _H1_RE.search(body, match.end())
        end = len(body)
        for candidate in (following, top):
            if candidate is not None:
                end = min(end, candidate.start())
        return match.start(), text_start, end
    return None


def is_owner_section(heading: str) -> bool:
    """A section the user wrote (so a rewrite has to be logged).  `## Request` always is."""
    return heading.strip().lower() not in AGENT_SECTIONS


# ------------------------------------------------------------------- rate limiting

class RateState:
    """Creates per hour, per workspace, shared by every pane of that workspace.

    Kept in one small JSON file under `.relay/` and updated under `flock`, so two panes
    writing at the same moment cannot both slip past the ceiling.
    """

    def __init__(self, path: Path | None, clock: Callable[[], float] = time.time):
        self.path = Path(path) if path is not None else None
        self.clock = clock
        self._memory: list[float] = []

    def _load(self) -> list[float]:
        if self.path is None:
            return list(self._memory)
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return []
        stamps = data.get("creates") if isinstance(data, dict) else None
        return [float(s) for s in stamps] if isinstance(stamps, list) else []

    def _store(self, stamps: list[float]) -> None:
        if self.path is None:
            self._memory = list(stamps)
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_name(self.path.name + f".{os.getpid()}.tmp")
        tmp.write_text(json.dumps({"creates": stamps}), encoding="utf-8")
        os.replace(tmp, self.path)

    def recent(self) -> int:
        cutoff = self.clock() - 3600
        return sum(1 for s in self._load() if s >= cutoff)

    def claim(self, ceiling: int) -> bool:
        """Record one creation, or return False when the hour's ceiling is already used."""
        lock = None
        if self.path is not None:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            lock = os.open(str(self.path) + ".lock", os.O_WRONLY | os.O_CREAT, 0o600)
            fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            now = self.clock()
            stamps = [s for s in self._load() if s >= now - 3600]
            if len(stamps) >= ceiling:
                return False
            stamps.append(now)
            self._store(stamps)
            return True
        finally:
            if lock is not None:
                fcntl.flock(lock, fcntl.LOCK_UN)
                os.close(lock)


# ------------------------------------------------------------------------- writes

@dataclass
class WriteRecord:
    """One reversible agent write (design 6.3, "Undo")."""
    write_id: str
    action: str
    card_id: str
    path: Path
    summary: str
    at: float
    before: bytes | None = None        # None = the file did not exist (a creation)
    thread_path: Path | None = None
    thread_size: int = 0
    moved_from: Path | None = None
    undone: bool = False


@dataclass
class ToolContext:
    """Who is writing, for the thread entries and the activity events."""
    actor: str = "agent"
    model: str | None = None
    pane: str | None = None
    turn_id: str | None = None
    session_id: str | None = None

    def attrs(self) -> dict:
        out = {}
        if self.model:
            out["model"] = self.model
        if self.pane:
            out["pane"] = str(self.pane)
        if self.turn_id:
            out["turn"] = f"{self.session_id}/{self.turn_id}" if self.session_id else str(self.turn_id)
        return out


class BoardTools:
    """The `board_*` tools for one pane (or for the Switchboard worker)."""

    def __init__(self, board: B.Board, *, emit: Callable[[dict], None] | None = None,
                 autonomy: str | None = None, limits: dict | None = None,
                 context: ToolContext | None = None, state_path: Path | str | None = None,
                 clock: Callable[[], float] = time.time, enforce_limits: bool = True,
                 duplicate_check: bool = True):
        self.board = board
        self.emit = emit or (lambda event: None)
        self.clock = clock
        self.context = context or ToolContext()
        config = board.config()
        agent_config = config.get("agent") if isinstance(config.get("agent"), dict) else {}
        # `autonomy: off` in board.yaml reads back as the YAML boolean false, so map it here
        # rather than making every board.yaml quote the word.
        raw = agent_config.get("autonomy")
        if raw is False:
            raw = "off"
        elif raw is True:
            raw = "auto"
        self.autonomy = autonomy or str(raw or "suggest")
        if self.autonomy not in AUTONOMY:
            self.autonomy = "suggest"
        self.limits = dict(DEFAULT_LIMITS)
        for key in DEFAULT_LIMITS:
            if isinstance(agent_config.get(key), int):
                self.limits[key] = max(0, int(agent_config[key]))
        for key, value in (limits or {}).items():
            if key in self.limits and isinstance(value, int):
                self.limits[key] = max(0, int(value))
        if state_path is None:
            state_path = Path(board.repo) / ".relay" / "board-rate.json"
        self.rate = RateState(state_path, clock)
        # The guardrails exist to keep an *agent* honest; the owner's own writes from the
        # Switchboard pane go through the same code with them turned off.
        self.enforce_limits = enforce_limits
        self.duplicate_check = duplicate_check
        self.writes: dict[str, WriteRecord] = {}
        self._order: list[str] = []
        self.creates_this_turn = 0
        self.writes_this_turn = 0

    # ---- lifecycle ------------------------------------------------------------
    @classmethod
    def for_workspace(cls, workspace: str | os.PathLike, **kwargs) -> "BoardTools | None":
        """The tools for a workspace, or None when it has no Switchboard or autonomy is off."""
        root = Path(workspace) / "issues"
        if not (root / B.BOARD_CONFIG).is_file():
            return None
        tools = cls(B.Board(root, Path(workspace)), **kwargs)
        return None if tools.autonomy == "off" else tools

    def begin_turn(self, turn_id: str | None = None) -> None:
        self.creates_this_turn = 0
        self.writes_this_turn = 0
        self.context.turn_id = turn_id

    def tool_specs(self) -> list[dict]:
        return [dict(s) for s in TOOL_SPECS]

    def handles(self, name: str) -> bool:
        return name in TOOL_NAMES

    # ---- dispatch -------------------------------------------------------------
    def preview(self, name: str, args: dict) -> str:
        """One human-readable block for the pane's tool line (no side effects)."""
        if not isinstance(args, dict):
            return name.upper().replace("_", " ")
        head = name.replace("board_", "").replace("_", " ").upper()
        bits = []
        for key in ("id", "tab", "status", "title", "kind", "query", "reason"):
            if args.get(key):
                bits.append(f"{key}: {str(args[key])[:120]}")
        return f"SWITCHBOARD {head}\n\n" + ("\n".join(bits) or "(no arguments)")

    def run(self, name: str, args: dict) -> dict:
        if not self.handles(name):
            raise BoardToolError(f"unknown Switchboard tool {name!r}")
        if not isinstance(args, dict):
            raise BoardToolError("Tool arguments must be an object.")
        try:
            if name in WRITE_TOOLS:
                self._check_write_budget(name)
            handler = {"board_list": self._list, "board_read": self._read,
                       "board_create_card": self._create, "board_update_card": self._update,
                       "board_move_card": self._move, "board_comment": self._comment}[name]
            return handler(dict(args))
        except BoardToolError as exc:
            return exc.to_result()
        except B.BoardConflict as exc:
            return {"error": str(exc), "code": "board_conflict", "current_hash": exc.current_hash}
        except B.BoardError as exc:
            return {"error": str(exc), "code": "board_error"}

    # ---- limits ---------------------------------------------------------------
    def _check_write_budget(self, name: str) -> None:
        if not self.enforce_limits:
            return
        if self.autonomy == "off":
            raise BoardToolError("Switchboard writes are turned off for this workspace (autonomy: off).",
                                 code="board_autonomy_off")
        if name == "board_create_card":
            if self.creates_this_turn >= self.limits["max_creates_per_turn"]:
                raise BoardToolError(
                    f"Switchboard limit: {self.limits['max_creates_per_turn']} new cards per turn. "
                    "Summarize the remaining requests in your reply instead of creating more.",
                    code="board_rate_limited", scope="turn", limit=self.limits["max_creates_per_turn"])
        elif self.writes_this_turn >= self.limits["max_writes_per_turn"]:
            raise BoardToolError(
                f"Switchboard limit: {self.limits['max_writes_per_turn']} card writes per turn. "
                "Summarize the rest in your reply.",
                code="board_rate_limited", scope="turn", limit=self.limits["max_writes_per_turn"])

    # ---- reads ----------------------------------------------------------------
    def _tab_map(self) -> dict[str, dict]:
        return {str(t.get("id")): t for t in self.board.tabs() if t.get("id")}

    def _category_for_tab(self, tab: str) -> str:
        tabs = self._tab_map()
        entry = tabs.get(str(tab).strip().lower())
        if entry is None:
            known = ", ".join(sorted(k for k, v in tabs.items() if v.get("folder"))) or "(none)"
            raise BoardToolError(f"unknown tab {tab!r}; this board has: {known}")
        folder = entry.get("folder")
        if not folder:
            raise BoardToolError(f"tab {tab!r} is a filter across categories, not a folder; "
                                 "pick a category tab for the card.")
        return str(folder)

    def _tab_of(self, card: B.Card) -> str:
        if card.type == "plan":
            return "planning"
        if card.type == "memory":
            return "memory"
        category = self.board.category_of(card.path) if card.path else ""
        for tab_id, entry in self._tab_map().items():
            if entry.get("folder") == category:
                return tab_id
        return category

    def _row(self, card: B.Card, thread_counts: dict[str, int]) -> dict:
        return {"id": card.id, "title": card.title, "type": card.type, "status": card.status,
                "tab": self._tab_of(card), "labels": list(card.front.get("labels") or []),
                "assignee": card.front.get("assignee"), "waiting_on": card.front.get("waiting_on"),
                "rank": card.rank, "private": card.private,
                "path": str(card.path.relative_to(self.board.repo)) if card.path else None,
                "thread_entries": thread_counts.get(card.id or "", 0)}

    def _thread_counts(self) -> dict[str, int]:
        counts: dict[str, int] = {}
        for private in (False, True):
            folder = self.board.threads_dir(private)
            if not folder.is_dir():
                continue
            for path in folder.glob("*.md"):
                try:
                    counts[path.stem.upper()] = len(B.parse_thread(path.read_text(encoding="utf-8")))
                except (OSError, UnicodeDecodeError):
                    continue
        return counts

    def _list(self, args: dict) -> dict:
        allowed = {"tab", "status", "type", "labels", "query", "limit"}
        if set(args) - allowed:
            raise BoardToolError(f"board_list takes {', '.join(sorted(allowed))}.")
        limit = args.get("limit", 25)
        if not isinstance(limit, int) or not 1 <= limit <= MAX_LIST_LIMIT:
            raise BoardToolError(f"limit must be an integer from 1 to {MAX_LIST_LIMIT}.")
        want_type = args.get("type")
        if want_type is not None and want_type not in B.CARD_TYPES:
            raise BoardToolError(f"type must be one of {', '.join(B.CARD_TYPES)}.")
        want_labels = {str(l).lower() for l in (args.get("labels") or [])}
        query = str(args.get("query") or "").strip().lower()
        want_tab = str(args.get("tab")).strip().lower() if args.get("tab") else None
        want_status = str(args.get("status")).strip().lower() if args.get("status") else None
        if want_tab:
            entry = self._tab_map().get(want_tab)
            if entry is None:
                raise BoardToolError(f"unknown tab {args['tab']!r}.")
        counts = self._thread_counts()
        rows = []
        for card in self.board.cards():
            if card.id is None:
                continue
            if want_type is not None and card.type != want_type:
                continue
            if want_type is None and args.get("tab") is None and card.type != "work":
                continue
            if want_status and card.status != want_status:
                continue
            if want_tab and self._tab_of(card) != want_tab:
                continue
            if want_labels and not want_labels <= {str(l).lower() for l in (card.front.get("labels") or [])}:
                continue
            if query and query not in f"{card.id} {card.title} {card.body}".lower():
                continue
            rows.append(card)
        rows.sort(key=lambda c: (B._status_order(c), c.rank or "zzzz", str(c.path)))
        out = [self._row(c, counts) for c in rows[:limit]]
        return {"cards": out, "total": len(rows), "truncated": len(rows) > limit,
                "tabs": [t for t in self._tab_map()], "autonomy": self.autonomy}

    def _card(self, card_id: str) -> B.Card:
        card = self.board.card_by_id(card_id)
        if card is None:
            raise BoardToolError(f"no card #{card_id} on this board.", code="board_not_found")
        return card

    def _read(self, args: dict) -> dict:
        if set(args) - {"id", "thread_entries"}:
            raise BoardToolError("board_read takes id and thread_entries.")
        card_id = normalize_id(args.get("id"))
        count = args.get("thread_entries", 20)
        if not isinstance(count, int) or not 0 <= count <= MAX_THREAD_ENTRIES:
            raise BoardToolError(f"thread_entries must be an integer from 0 to {MAX_THREAD_ENTRIES}.")
        card = self._card(card_id)
        entries = self.board.thread(card_id, card.private)
        entries.sort(key=lambda e: e.entry_id)
        tail = entries[len(entries) - count:] if count else []
        return {"id": card.id, "hash": B.file_hash(card.path), "type": card.type,
                "tab": self._tab_of(card), "status": card.status,
                "path": str(card.path.relative_to(self.board.repo)),
                "front": dict(card.front), "title": card.title, "body": card.body[:MAX_TEXT],
                "body_truncated": len(card.body) > MAX_TEXT,
                "sections": section_headings(card.body),
                "tasks": [{"item_id": t.item_id, "text": t.text, "status": t.status,
                           "done": t.done, "depth": t.depth, "card": t.card}
                          for t in card.tasks()],
                "thread_total": len(entries),
                "thread": [{"entry_id": e.entry_id, "author": e.author, "kind": e.kind,
                            "attrs": dict(e.attrs), "text": e.text} for e in tail]}

    # ---- writes ---------------------------------------------------------------
    def _record(self, action: str, card: B.Card, summary: str, before: bytes | None,
                thread_size: int, moved_from: Path | None = None) -> str:
        write_id = f"w-{int(self.clock() * 1000):x}-{secrets.token_hex(2)}"
        record = WriteRecord(write_id=write_id, action=action, card_id=card.id or "",
                             path=card.path, summary=summary, at=self.clock(), before=before,
                             thread_path=self.board.thread_path(card.id or "", card.private),
                             thread_size=thread_size, moved_from=moved_from)
        self.writes[write_id] = record
        self._order.append(write_id)
        while len(self._order) > 50:
            self.writes.pop(self._order.pop(0), None)
        self.emit({"event": "board_activity", "write_id": write_id, "id": card.id, "action": action,
                   "actor": self.context.actor, "model": self.context.model, "pane": self.context.pane,
                   "turn_id": self.context.turn_id, "summary": summary,
                   "path": str(card.path.relative_to(self.board.repo)), "undo_seconds": UNDO_SECONDS})
        self.emit({"event": "board_changed", "upserts": [card.id], "removed": [], "write_id": write_id})
        return write_id

    def _thread_size(self, card: B.Card) -> int:
        path = self.board.thread_path(card.id or "", card.private)
        try:
            return path.stat().st_size
        except OSError:
            return 0

    def _append(self, card: B.Card, text: str, kind: str = "event") -> B.ThreadEntry:
        return self.board.append_thread(card.id, text, author=self.context.actor, kind=kind,
                                        private=card.private, **self.context.attrs())

    def _create(self, args: dict) -> dict:
        allowed = {"tab", "status", "title", "request", "type", "labels", "source", "related",
                   "not_duplicate_of"}
        if set(args) - allowed:
            raise BoardToolError(f"board_create_card takes {', '.join(sorted(allowed))}.")
        card_type = args.get("type") or "work"
        if card_type not in B.CARD_TYPES:
            raise BoardToolError(f"type must be one of {', '.join(B.CARD_TYPES)}.")
        title = _one_line(args.get("title"), "title", MAX_TITLE)
        request = _text(args.get("request"), "request", MAX_REQUEST)
        status = str(args.get("status") or "").strip().lower()
        if status not in B.STATUS_FOLDER[card_type]:
            raise BoardToolError(f"unknown {card_type} status {args.get('status')!r}; use one of "
                                 f"{', '.join(B.STATUS_FOLDER[card_type])}.")
        category = (B.PLAN_FOLDER if card_type == "plan" else
                    B.MEMORY_FOLDER if card_type == "memory" else self._category_for_tab(args.get("tab")))
        labels = _string_list(args.get("labels"), "labels")
        related = [normalize_id(r, "related") for r in _string_list(args.get("related"), "related")]

        cards = self.board.cards()
        excused = {normalize_id(r, "not_duplicate_of") for r in _string_list(args.get("not_duplicate_of"), "not_duplicate_of")}
        duplicates = self.duplicates(title, request, cards, excused) if self.duplicate_check else []
        if duplicates:
            raise BoardToolError(
                "This looks like a card that already exists. Read it and update it instead, or repeat "
                "the call with not_duplicate_of listing the ids you checked.",
                code="board_possible_duplicate", possible_duplicates=duplicates)

        if self.enforce_limits and not self.rate.claim(self.limits["max_creates_per_hour"]):
            raise BoardToolError(
                f"Switchboard limit: {self.limits['max_creates_per_hour']} new cards per hour for this "
                "workspace. Summarize the remaining requests in your reply.",
                code="board_rate_limited", scope="hour", limit=self.limits["max_creates_per_hour"])

        taken = [c.id for c in cards if c.id]
        card = B.new_card(card_type, title, status, card_id=B.new_id(taken), request=request,
                          rank=self.board.next_rank([c for c in cards if c.status == status]),
                          labels=labels or None, source=args.get("source") or None)
        if related:
            links = dict(card.front.get("links") or {})
            links["related"] = related
            card.set("links", links)
        folder = self.board.root / card.expected_folder(category)
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / B.card_filename(title)
        for n in range(2, 60):
            if not path.exists():
                break
            path = folder / B.card_filename(f"{title}-{n}")
        if path.exists():
            raise BoardToolError("could not find a free file name for the card.")
        card.path = path
        B._atomic_write(path, card.to_text())

        self.creates_this_turn += 1
        self.writes_this_turn += 1
        rel = str(path.relative_to(self.board.repo))
        size = self._thread_size(card)
        self._append(card, f"- ✦ {self.context.actor} created this card in "
                           f"{_column_label(status)} · {rel}", kind="event")
        summary = f"created · {_column_label(status)}"
        write_id = self._record("create", card, summary, None, size)
        return {"id": card.id, "path": rel, "status": status, "tab": self._tab_of(card),
                "hash": B.file_hash(path), "write_id": write_id, "created": True}

    def duplicates(self, title: str, request: str, cards: Sequence[B.Card] | None = None,
                   excused: set[str] = frozenset(), threshold: float = 0.66) -> list[dict]:
        """Cards whose title or request looks like this one (open cards only)."""
        out = []
        for card in (cards if cards is not None else self.board.cards()):
            if card.id is None or card.id in excused or card.status in ("done", "dropped"):
                continue
            body_request = ""
            span = _section_span(card.body, "Request")
            if span:
                body_request = card.body[span[1]:span[2]]
            score = max(similarity(title, card.title), similarity(request, body_request),
                        similarity(title, body_request))
            if score >= threshold:
                out.append({"id": card.id, "title": card.title, "status": card.status,
                            "score": round(score, 3),
                            "path": str(card.path.relative_to(self.board.repo)) if card.path else None})
        return sorted(out, key=lambda d: -d["score"])[:5]

    def _update(self, args: dict) -> dict:
        allowed = {"id", "base_hash", "fields", "title", "append_section", "replace_section",
                   "replace_agent_section", "tasks"}
        if set(args) - allowed:
            raise BoardToolError(f"board_update_card takes {', '.join(sorted(allowed))}.")
        card_id = normalize_id(args.get("id"))
        base_hash = args.get("base_hash")
        if not isinstance(base_hash, str) or len(base_hash) != 64:
            raise BoardToolError("base_hash must be the 64-character `hash` returned by board_read.")
        card = self._card(card_id)
        before = card.path.read_bytes()
        current = B.file_hash(card.path)
        if current != base_hash:
            raise BoardToolError(f"#{card_id} changed since you read it; read it again and reapply "
                                 "your change.", code="board_conflict", current_hash=current, id=card_id)

        changes: list[str] = []
        rewrites: list[tuple[str, str, str]] = []   # (what, old, new)

        fields = args.get("fields")
        if fields is not None:
            if not isinstance(fields, dict):
                raise BoardToolError("fields must be an object of front matter keys.")
            writable = B.ALLOWED_FIELDS[card.type] - IMMUTABLE_FIELDS
            for key, value in fields.items():
                if key in IMMUTABLE_FIELDS:
                    raise BoardToolError(
                        f"{key} is not writable: id, type and created are the record, source is the "
                        "user's own provenance, private moves the file, and status and rank are "
                        "board_move_card's job.", code="board_refused", field=key)
                if key not in writable:
                    raise BoardToolError(f"{key} is not a field of a {card.type} card; allowed: "
                                         f"{', '.join(sorted(writable))}.", code="board_refused", field=key)
                old = card.front.get(key)
                if value is None:
                    card.drop(key)
                else:
                    card.set(key, value)
                if old != card.front.get(key):
                    changes.append(f"{key}: {_short(old)} → {_short(card.front.get(key))}")

        if args.get("title") is not None:
            new_title = _one_line(args.get("title"), "title", MAX_TITLE)
            old_title = card.title
            if new_title != old_title:
                match = _H1_RE.search(card.body)
                if match is None:
                    raise BoardToolError("this card has no `# ` heading to replace.")
                card.body = card.body[:match.start()] + f"# {new_title}" + card.body[match.end():]
                changes.append(f"title: {_short(old_title)} → {_short(new_title)}")
                rewrites.append(("title", old_title, new_title))

        for key in ("append_section", "replace_section", "replace_agent_section"):
            block = args.get(key)
            if block is None:
                continue
            if not isinstance(block, dict) or set(block) - {"heading", "text"}:
                raise BoardToolError(f"{key} takes {{heading, text}}.")
            heading = _one_line(block.get("heading"), "heading", 120)
            text = _text(block.get("text"), "text", MAX_SECTION)
            replace = key != "append_section"
            old_text = _section_text(card.body, heading)
            card.body = _write_section(card.body, heading, text, replace=replace)
            card.dirty = True
            verb = "replaced" if replace else "appended to"
            changes.append(f"{verb} `## {heading}`")
            if replace and old_text.strip() and is_owner_section(heading):
                rewrites.append((f"## {heading}", old_text, text))

        if args.get("tasks") is not None:
            items = _task_items(args["tasks"], card)
            card.write_tasks(items)
            card.dirty = True
            changes.append(f"tasks: {sum(1 for i in items if i.done)}/{len(items)} done")

        if not changes:
            raise BoardToolError("nothing to change: pass fields, title, append_section, "
                                 "replace_section or tasks.")

        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        self.writes_this_turn += 1
        summary = "; ".join(changes)[:400]
        self._append(card, f"- ✦ {self.context.actor} updated this card · {summary}", kind="event")
        # Decision 12.3: a rewrite of the user's own text is allowed, and the thread keeps the old
        # and the new text so the discussion history shows it and it can be put back.
        for what, old, new in rewrites:
            self._append(card, _rewrite_entry(what, old, new), kind="rewrite")
        write_id = self._record("update", card, summary, before, size)
        return {"id": card.id, "hash": B.file_hash(card.path), "changes": changes,
                "write_id": write_id, "logged_rewrites": [w for w, _, _ in rewrites]}

    def _move(self, args: dict) -> dict:
        allowed = {"id", "status", "tab", "before", "after", "reason", "evidence", "implemented_by"}
        if set(args) - allowed:
            raise BoardToolError(f"board_move_card takes {', '.join(sorted(allowed))}.")
        card_id = normalize_id(args.get("id"))
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        card = self._card(card_id)
        before_bytes = card.path.read_bytes()
        base_hash = B.file_hash(card.path)
        old_status, old_tab = card.status, self._tab_of(card)
        status = str(args.get("status")).strip().lower() if args.get("status") else old_status
        if status not in B.STATUS_FOLDER[card.type]:
            raise BoardToolError(f"unknown {card.type} status {args.get('status')!r}; use one of "
                                 f"{', '.join(B.STATUS_FOLDER[card.type])}.")
        tab = str(args.get("tab")).strip().lower() if args.get("tab") else old_tab
        category = (B.PLAN_FOLDER if card.type == "plan" else B.MEMORY_FOLDER if card.type == "memory"
                    else self._category_for_tab(tab))

        evidence = args.get("evidence")
        if status in QA_STATUSES and old_status not in QA_STATUSES:
            if not isinstance(evidence, str) or not evidence.strip():
                raise BoardToolError(
                    "Moving a card into a QA lane needs `evidence`: the path of the evidence folder "
                    "(docs/qa_evidence/<date>-<slug>/) recorded with the change.",
                    code="board_refused", requires="evidence")
            implemented_by = args.get("implemented_by") or card.front.get("implemented_by")
            if not implemented_by:
                raise BoardToolError(
                    "Moving a card into a QA lane needs `implemented_by`: the model that implemented "
                    "it, so QA can be run by a different one.",
                    code="board_refused", requires="implemented_by")
            card.set("implemented_by", implemented_by)
        if old_status in QA_STATUSES and status in ("done", "dropped"):
            if not any(h.lower() in ("verdict", "qa verdict", "qa result", "resolution")
                       for h in section_headings(card.body)):
                raise BoardToolError(
                    "A card in a QA lane is closed with a verdict: add a `## Verdict` (or "
                    "`## Resolution`) section to the body first, then move it.",
                    code="board_refused", requires="verdict")
            mine = model_family(self.context.model)
            theirs = model_family(card.front.get("implemented_by"))
            if mine and theirs and mine == theirs:
                raise BoardToolError(
                    f"QA independence: {card.front.get('implemented_by')} implemented this card, and "
                    f"you are the same model family ({mine}). A different model has to close it.",
                    code="board_refused", requires="independent_model")

        if args.get("evidence"):
            links = dict(card.front.get("links") or {})
            paths = list(links.get("evidence") or [])
            if evidence not in paths:
                paths.append(evidence)
            links["evidence"] = paths
            card.set("links", links)
        if args.get("implemented_by"):
            card.set("implemented_by", args["implemented_by"])

        card.set("status", status)
        rank = self._rank_for(card, status, args.get("before"), args.get("after"))
        if rank is not None:
            card.set("rank", rank)

        target_dir = self.board.base_for(card.private) / card.expected_folder(category)
        target = target_dir / card.path.name
        moved_from = card.path if target != card.path else None
        if target != card.path and target.exists():
            raise BoardToolError(f"a different file already sits at {target.relative_to(self.board.repo)}.")
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        if moved_from is not None:
            target_dir.mkdir(parents=True, exist_ok=True)
            os.replace(card.path, target)
            card.path = target

        self.writes_this_turn += 1
        parts = []
        if status != old_status:
            parts.append(f"{_column_label(old_status)} → {_column_label(status)}")
        if tab != old_tab:
            parts.append(f"tab {old_tab} → {tab}")
        if rank is not None and not parts:
            parts.append("reordered")
        summary = ", ".join(parts) or "unchanged"
        line = f"- ✦ {self.context.actor} moved this card · {summary} · {reason}"
        if args.get("evidence"):
            line += f" · evidence {evidence}"
        self._append(card, line, kind="event")
        write_id = self._record("move", card, summary, before_bytes, size, moved_from)
        return {"id": card.id, "status": status, "tab": tab, "rank": card.rank,
                "path": str(card.path.relative_to(self.board.repo)),
                "hash": B.file_hash(card.path), "moved": moved_from is not None,
                "write_id": write_id, "summary": summary}

    def _rank_for(self, card: B.Card, status: str, before, after) -> str | None:
        if before is None and after is None:
            return None if card.status == status and card.rank else self.board.next_rank(
                [c for c in self.board.cards() if c.status == status and c.id != card.id])
        column = sorted((c for c in self.board.cards()
                         if c.status == status and c.id != card.id and c.rank),
                        key=lambda c: c.rank)
        by_id = {c.id: c for c in column}

        def rank_of(value, what):
            key = normalize_id(value, what)
            neighbour = by_id.get(key)
            if neighbour is None:
                raise BoardToolError(f"#{key} is not a card in the {_column_label(status)} column.")
            return neighbour.rank

        low = high = None
        if after is not None:
            low = rank_of(after, "after")
            later = [c.rank for c in column if c.rank > low]
            high = later[0] if later else None
        if before is not None:
            high = rank_of(before, "before")
            earlier = [c.rank for c in column if c.rank < high]
            if after is None:
                low = earlier[-1] if earlier else None
        return B.rank_between(low, high)

    def _comment(self, args: dict) -> dict:
        if set(args) - {"id", "kind", "text"}:
            raise BoardToolError("board_comment takes id, kind and text.")
        card_id = normalize_id(args.get("id"))
        kind = str(args.get("kind") or "").strip().lower()
        if kind not in COMMENT_KINDS:
            raise BoardToolError(f"kind must be one of {', '.join(COMMENT_KINDS)}.")
        text = _text(args.get("text"), "text", MAX_TEXT)
        card = self._card(card_id)
        if kind == "decision" and not _QUOTE_RE.search(text):
            raise BoardToolError(
                'A decision entry quotes the user\'s own words in quotation marks, e.g. '
                '2026-09-17, owner: "cloud is fine" → default to the cloud model. Quote them, '
                "then repeat the call.", code="board_refused", requires="verbatim_quote")
        size = self._thread_size(card)
        before = card.path.read_bytes()
        entry = self._append(card, text, kind=kind)
        self.writes_this_turn += 1
        write_id = self._record("comment", card, f"{kind}: {text.splitlines()[0][:120]}", before, size)
        return {"id": card.id, "entry_id": entry.entry_id, "kind": kind, "write_id": write_id}

    # ---- undo ------------------------------------------------------------------
    def undo(self, write_id: str) -> dict:
        record = self.writes.get(write_id)
        if record is None:
            raise BoardToolError(f"no undoable Switchboard write {write_id!r}.", code="board_not_found")
        if record.undone:
            raise BoardToolError("that write was already undone.")
        current = record.path
        if record.moved_from is not None and current.exists():
            record.moved_from.parent.mkdir(parents=True, exist_ok=True)
            os.replace(current, record.moved_from)
            current = record.moved_from
        removed = []
        if record.before is None:
            if _tracked_by_git(self.board.repo, current):
                raise BoardToolError("that card is already committed; close it with board_move_card "
                                     "(done or dropped) instead of undoing the creation.")
            if current.exists():
                current.unlink()
            removed.append(record.card_id)
            if record.thread_path is not None and record.thread_path.exists():
                record.thread_path.unlink()
        else:
            B._atomic_write(current, record.before.decode("utf-8"))
            self._truncate_thread(record)
        record.undone = True
        self.emit({"event": "board_changed", "upserts": [] if removed else [record.card_id],
                   "removed": removed, "write_id": write_id, "undo_of": write_id})
        return {"undone": write_id, "id": record.card_id, "action": record.action,
                "removed": bool(removed)}

    def _truncate_thread(self, record: WriteRecord) -> None:
        """Drop the entries this write appended, then record the undo itself."""
        path = record.thread_path
        if path is None or not path.exists():
            return
        with open(path, "r+b") as handle:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
            try:
                if handle.seek(0, os.SEEK_END) > record.thread_size:
                    handle.truncate(record.thread_size)
            finally:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
        card = self.board.card_by_id(record.card_id)
        if card is not None:
            self._append(card, f"- ↩ {self.context.actor} undid: {record.summary}", kind="event")


# ------------------------------------------------------------------ text plumbing

def _short(value, limit: int = 80) -> str:
    text = "(unset)" if value is None else (value if isinstance(value, str) else json.dumps(value, ensure_ascii=False))
    text = " ".join(str(text).split())
    return text[:limit] + ("…" if len(text) > limit else "")


def _one_line(value, what: str, maximum: int) -> str:
    if not isinstance(value, str) or not value.strip():
        raise BoardToolError(f"{what} must be a non-empty string.")
    text = " ".join(value.split())
    if len(text) > maximum:
        raise BoardToolError(f"{what} must be at most {maximum} characters.")
    return text


def _text(value, what: str, maximum: int) -> str:
    if not isinstance(value, str) or not value.strip():
        raise BoardToolError(f"{what} must be a non-empty string.")
    if len(value) > maximum:
        raise BoardToolError(f"{what} must be at most {maximum} characters.")
    return value.replace("\r\n", "\n").rstrip()


def _string_list(value, what: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
        raise BoardToolError(f"{what} must be an array of strings.")
    if len(value) > 20:
        raise BoardToolError(f"{what} takes at most 20 entries.")
    return [v.strip() for v in value if v.strip()]


def _section_text(body: str, heading: str) -> str:
    span = _section_span(body, heading)
    return body[span[1]:span[2]] if span else ""


def _write_section(body: str, heading: str, text: str, *, replace: bool) -> str:
    """Replace or extend one `## ` section, leaving every other byte of the body alone."""
    block = text.rstrip("\n") + "\n"
    span = _section_span(body, heading)
    if span is None:
        prefix = body if body.endswith("\n") else body + "\n"
        return f"{prefix}\n## {heading}\n{block}"
    _, start, end = span
    existing = body[start:end]
    if replace:
        return body[:start] + block + "\n" * _trailing_blanks(existing) + body[end:]
    kept = existing.rstrip("\n")
    joined = (kept + "\n" if kept else "") + block
    return body[:start] + joined + "\n" * _trailing_blanks(existing) + body[end:]


def _trailing_blanks(text: str) -> int:
    return len(text) - len(text.rstrip("\n")) - (1 if text.endswith("\n") else 0) if text.strip() else 1


def _rewrite_entry(what: str, old: str, new: str) -> str:
    """The thread block that makes a rewrite of the user's own text reversible (12.3)."""
    return (f"- ✦ rewrote {what}\n\n"
            f"<details><summary>before</summary>\n\n```\n{old.strip()}\n```\n\n</details>\n\n"
            f"<details><summary>after</summary>\n\n```\n{new.strip()}\n```\n\n</details>")


def _column_label(status: str) -> str:
    return {"inbox": "Inbox", "discussing": "Discussing", "ready": "Ready",
            "in-progress": "In progress", "needs-qa-llm": "Needs QA (LLM)",
            "needs-qa-human": "Needs QA (human)", "needs-review": "Needs review",
            "needs-labels": "Needs labels", "needs-ab": "Needs A/B", "deferred": "Deferred",
            "done": "Done", "dropped": "Dropped", "draft": "Draft", "approved": "Approved",
            "executing": "Executing", "active": "Active", "retired": "Retired"}.get(status, status)


def _task_items(raw, card: B.Card) -> list[B.TaskItem]:
    if not isinstance(raw, list) or len(raw) > 100:
        raise BoardToolError("tasks must be an array of at most 100 items.")
    existing = {i.item_id: i for i in card.tasks() if i.item_id}
    out: list[B.TaskItem] = []
    for index, entry in enumerate(raw, 1):
        if not isinstance(entry, dict):
            raise BoardToolError(f"task {index} must be an object.")
        text = _one_line(entry.get("text"), f"task {index} text", 300)
        status = str(entry.get("status") or "open").lower()
        if status not in B.ITEM_STATUSES:
            raise BoardToolError(f"task {index}: status must be one of {', '.join(B.ITEM_STATUSES)}.")
        item_id = entry.get("item_id")
        keep = existing.get(str(item_id).lower()) if item_id else None
        item = B.TaskItem(text=text, status=status,
                          item_id=keep.item_id if keep else None,
                          line=keep.line if keep else None,
                          depth=keep.depth if keep else 0,
                          card=keep.card if keep else None,
                          blocked_by=list(keep.blocked_by) if keep else [])
        if entry.get("card"):
            item.card = normalize_id(entry["card"], f"task {index} card")
        out.append(item)
    return out


def _tracked_by_git(repo: Path, path: Path) -> bool:
    try:
        result = subprocess.run(["git", "-C", str(repo), "ls-files", "--error-unmatch", str(path)],
                                capture_output=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0


# ------------------------------------------------------------------------- policy

def policy_text() -> str:
    """The system-prompt block, versioned in `board_policy.md` so evals can pin it."""
    path = Path(__file__).resolve().parent / "board_policy.md"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:                                        # pragma: no cover - packaging slip
        return ""
    # The file's own provenance comment is for readers of the repository, not for the model.
    return re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()


def prompt_section(tools: "BoardTools | None") -> str:
    """What `Agent.system_prompt` appends when this workspace has a Switchboard."""
    if tools is None or tools.autonomy == "off":
        return ""
    text = policy_text()
    if not text:
        return ""
    tabs = ", ".join(t for t in tools._tab_map())
    header = (f"\n\nSwitchboard: this repository has one (issues/board.yaml). Tabs: {tabs}. "
              f"Autonomy: {tools.autonomy}"
              + (" — your card writes are proposals the user accepts in the Switchboard pane."
                 if tools.autonomy == "suggest" else "") + "\n")
    return header + text
