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
from datetime import datetime, timezone
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
          "labels": {"type": "array", "items": {"type": "string"},
                     "description": "Every label must be present, e.g. ['bug'] for the fault list."},
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
          "labels": {"type": "array", "items": {"type": "string"},
                     "description": "You choose these, not the user. Always exactly one of 'bug' "
                                    "(something built behaves wrongly) or 'feature' (something new "
                                    "or changed is asked for), decided from your understanding of "
                                    "the request, plus any obvious area labels."},
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

#: The three tools a whole-board cleanup needs and an ordinary turn does not (protocol 19.9).
#: They are offered only while `board_cleanup` is running, so a pane agent's every turn does
#: not carry three more tool schemas — and cannot merge the user's cards on a whim.
CLEANUP_TOOL_SPECS = [
    spec("board_merge_cards",
         "Fold one or more redundant cards into one surviving card. Nothing is deleted: each "
         "merged card keeps its file and its id, gains a `## Resolution` naming the survivor and "
         "is closed as `dropped`, its text is copied into the survivor under `## Merged in`, and "
         "its thread is carried over. Read every card first; merge only cards that are genuinely "
         "the same request.",
         {"into": {**_ID_ARG, "description": "The card that survives and keeps the work."},
          "cards": {"type": "array", "items": {"type": "string"}, "minItems": 1, "maxItems": 10,
                    "description": "Ids of the cards folded into it."},
          "reason": {"type": "string", "description": "Why they are the same request, in one line."}},
         ["into", "cards", "reason"]),
    spec("board_split_card",
         "Split a card that mixes unrelated work into one card per piece. Each part's `request` is "
         "the user's own words for that piece, quoted verbatim from the original; the title is "
         "yours. The original stays as the record, gains a `## Split` section naming the new cards, "
         "and is closed only when `close` is true, which is right when every piece moved out.",
         {"id": _ID_ARG,
          "parts": {"type": "array", "minItems": 2, "maxItems": 10,
                    "description": "One entry per piece of work.",
                    "items": {"type": "object", "properties": {
                        "title": {"type": "string", "description": "One line, your words."},
                        "request": {"type": "string",
                                    "description": "The user's words for this piece, verbatim from the original card."},
                        "status": {"type": "string", "description": "Defaults to the original's status."},
                        "tab": {"type": "string", "description": "Defaults to the original's category folder."},
                        "labels": {"type": "array", "items": {"type": "string"}}},
                        "required": ["title", "request"], "additionalProperties": False}},
          "reason": {"type": "string", "description": "Why the card mixes unrelated work, in one line."},
          "close": {"type": "boolean",
                    "description": "Close the original as `dropped` because every piece moved out."}},
         ["id", "parts", "reason"]),
    spec("board_sections",
         "Change the board's own structure in issues/board.yaml: which sections (columns) the one "
         "list is divided into and in what order, and which category folders (tabs) a card's file "
         "can live in. Dropping a column does not hide its cards — a status no column collects gets "
         "a section of its own — but a tab whose folder still holds cards cannot be dropped. Use "
         "this sparingly: it changes the board for everyone.",
         {"columns": {"type": "array", "items": {"type": "string"},
                      "description": f"The whole ordered column list, from: {', '.join(B.COLUMN_IDS)}."},
          "tabs": {"type": "array", "description": "The whole tab list, each {id, folder} or {id, filter}.",
                   "items": {"type": "object", "properties": {
                       "id": {"type": "string"}, "folder": {"type": "string"},
                       "filter": {"type": "string"}},
                       "required": ["id"], "additionalProperties": False}},
          "reason": {"type": "string", "description": "Why the structure no longer fits, in one line."}},
         ["reason"]),
]

#: The tools of every turn.  The cleanup-only three are in `CLEANUP_TOOL_NAMES`; `ALL_TOOL_NAMES`
#: is what `handles` answers to, since a cleanup call still arrives through the same dispatch.
TOOL_NAMES = tuple(s["function"]["name"] for s in TOOL_SPECS)
CLEANUP_TOOL_NAMES = tuple(s["function"]["name"] for s in CLEANUP_TOOL_SPECS)
ALL_TOOL_NAMES = TOOL_NAMES + CLEANUP_TOOL_NAMES
WRITE_TOOLS = ("board_create_card", "board_update_card", "board_move_card", "board_comment",
               "board_merge_cards", "board_split_card", "board_sections")


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


def _heading_matches(found: str, wanted: str) -> bool:
    """One `## ` heading names the same section as another.

    `## Request` and `## Issue` are the same section under two names: cards written before
    2026-09-18 say Request, new and edited ones say Issue (`board.ISSUE_HEADINGS`).
    """
    found, wanted = found.strip().lower(), wanted.strip().lower()
    if found == wanted:
        return True
    return found in B.ISSUE_HEADINGS and wanted in B.ISSUE_HEADINGS


def _section_span(body: str, heading: str) -> tuple[int, int, int] | None:
    """(start of the heading line, start of the section text, end of the section)."""
    wanted = heading.strip()
    for match in _SECTION_RE.finditer(body):
        if not _heading_matches(match.group("heading"), wanted):
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
    """A section the user wrote (so a rewrite has to be logged).  `## Issue` always is."""
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


# --------------------------------------------------------------- whole-board cleanup

#: A cleanup rewrites many cards in one turn, so it runs on its own ceilings rather than a
#: pane turn's five creates and twenty writes.  They are still ceilings: a run that wants
#: more than this has lost the plot and should stop and report.
CLEANUP_LIMITS = {
    "max_creates_per_turn": 60,
    "max_writes_per_turn": 400,
    "max_creates_per_hour": 200,
}

#: Where a run's changelog is written.  `issues/` holds cards and threads and nothing else,
#: so the closest fit in `issues/README.md`'s conventions is a dated evidence folder — the
#: same place an implementer's evidence for a change goes (`docs/qa_evidence/<date>-<slug>/`).
CLEANUP_EVIDENCE_SLUG = "switchboard-cleanup"

#: How many changes the summary event carries before it says "truncated"; the changelog file
#: always holds every one of them.
MAX_SUMMARY_CHANGES = 200


@dataclass
class CleanupChange:
    """One write a cleanup made (or, on a dry run, one it wanted to make)."""
    action: str
    card_id: str
    summary: str
    path: str = ""
    write_id: str = ""
    cards: list[str] = field(default_factory=list)
    proposed: bool = False

    def to_dict(self) -> dict:
        out = {"action": self.action, "card_id": self.card_id, "summary": self.summary,
               "path": self.path, "write_id": self.write_id}
        if self.cards:
            out["cards"] = list(self.cards)
        if self.proposed:
            out["proposed"] = True
        return out


@dataclass
class CleanupLog:
    """The record of one `board_cleanup` run: what it changed, and the file that says so.

    A cleanup rewrites the user's own files, so it is only as good as its changelog.  Every
    write goes through `BoardTools._record_path`, which appends here; the run ends by writing
    the whole thing to a dated file under `docs/qa_evidence/` and sending it as
    `board_cleanup_summary`, so the change can be read, judged and reverted with git.
    """
    run_id: str
    started: float = 0.0
    dry_run: bool = False
    scope: str | None = None
    note: str | None = None
    limits: dict = field(default_factory=lambda: dict(CLEANUP_LIMITS))
    changes: list[CleanupChange] = field(default_factory=list)
    refusals: list[dict] = field(default_factory=list)
    outcome: str = "running"
    report: str = ""
    cards_before: int = 0
    cards_after: int = 0
    config_before: dict = field(default_factory=dict)
    config_after: dict = field(default_factory=dict)
    finished: float = 0.0
    model: str | None = None
    changelog: str = ""

    def record(self, change: CleanupChange) -> None:
        self.changes.append(change)

    def counts(self) -> dict:
        out = {"writes": sum(1 for c in self.changes if not c.proposed),
               "proposed": sum(1 for c in self.changes if c.proposed),
               "cards_touched": len({c.card_id for c in self.changes if c.card_id})}
        for change in self.changes:
            out[change.action] = out.get(change.action, 0) + 1
        return out

    def seconds(self) -> float:
        return round(max(0.0, (self.finished or self.started) - self.started), 1)

    def summary_event(self) -> dict:
        shown = self.changes[:MAX_SUMMARY_CHANGES]
        return {"run_id": self.run_id, "outcome": self.outcome, "dry_run": self.dry_run,
                "scope": self.scope, "seconds": self.seconds(), "counts": self.counts(),
                "changes": [c.to_dict() for c in shown],
                "truncated": len(self.changes) > len(shown),
                "refusals": self.refusals[:50],
                "cards_before": self.cards_before, "cards_after": self.cards_after,
                "sections": self.sections_delta(), "changelog": self.changelog,
                "report": self.report[:4000]}

    def sections_delta(self) -> dict | None:
        if not self.config_after or self.config_before == self.config_after:
            return None
        return {"columns_before": list(self.config_before.get("columns") or []),
                "columns_after": list(self.config_after.get("columns") or []),
                "tabs_before": [str(t.get("id")) for t in (self.config_before.get("tabs") or [])],
                "tabs_after": [str(t.get("id")) for t in (self.config_after.get("tabs") or [])]}

    # ---- the changelog file ----------------------------------------------------
    def stamp(self) -> str:
        return datetime.fromtimestamp(self.started or time.time(), timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    def relative_path(self) -> str:
        day = datetime.fromtimestamp(self.started or time.time(), timezone.utc).strftime("%Y-%m-%d")
        name = f"cleanup-{self.stamp()}" + ("-dry-run" if self.dry_run else "") + ".md"
        return f"docs/qa_evidence/{day}-{CLEANUP_EVIDENCE_SLUG}/{name}"

    def markdown(self) -> str:
        counts = self.counts()
        head = [f"# Switchboard cleanup {self.run_id}",
                "",
                f"- **Run**: {self.run_id} ({'dry run, nothing written' if self.dry_run else 'applied'})",
                f"- **Finished**: {datetime.fromtimestamp(self.finished or time.time(), timezone.utc).isoformat(timespec='seconds')}"
                f" after {self.seconds()} s, outcome `{self.outcome}`",
                f"- **Model**: {self.model or 'unknown'}",
                f"- **Cards**: {self.cards_before} before, {self.cards_after} after",
                f"- **Writes**: {counts.get('writes', 0)}"
                + (f", proposed {counts['proposed']}" if counts.get("proposed") else ""),
                f"- **Scope**: {self.scope or 'the whole board'}"]
        if self.note:
            head.append(f"- **Note from the user**: {self.note}")
        sections = self.sections_delta()
        if sections:
            head += ["", "## Sections",
                     f"- columns: `{', '.join(sections['columns_before'])}` → `{', '.join(sections['columns_after'])}`",
                     f"- tabs: `{', '.join(sections['tabs_before'])}` → `{', '.join(sections['tabs_after'])}`"]
        head += ["", "## Changes", ""]
        if self.changes:
            head += ["| # | Action | Card | Summary | File |", "|---|---|---|---|---|"]
            for index, change in enumerate(self.changes, 1):
                cards = (" ← " + " ".join("#" + c for c in change.cards)) if change.cards else ""
                mark = "*(proposed)* " if change.proposed else ""
                head.append(f"| {index} | {change.action} | #{change.card_id or '—'}{cards} | "
                            f"{mark}{_cell(change.summary)} | `{change.path}` |")
        else:
            head.append("Nothing was changed.")
        if self.refusals:
            head += ["", "## Refused", ""]
            head += [f"- `{r.get('tool')}`: {_cell(str(r.get('error', '')))}" for r in self.refusals[:50]]
        head += ["", "## What the agent said", "", self.report.strip() or "(no closing message)", ""]
        return "\n".join(head)

    def write(self, repo: Path) -> str:
        """Write the changelog under the repository and return its relative path."""
        rel = self.relative_path()
        path = Path(repo) / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        B.atomic_write(path, self.markdown())
        self.changelog = rel
        return rel


def _cell(text: str) -> str:
    return " ".join(str(text).split()).replace("|", "\\|")[:300]


def _without_own_heading(text: str, heading: str) -> str:
    """A section's text with a leading copy of its own heading dropped.

    Models writing `replace_section {heading: "Plan", text: "## Plan\\n…"}` repeat the heading
    they were given, and the card then said `## Plan` twice (live Plan turn, 2026-09-18).
    """
    lines = text.lstrip("\n").split("\n")
    first = lines[0].strip() if lines else ""
    if first.startswith("#") and first.lstrip("#").strip().lower() == heading.strip().lower():
        return "\n".join(lines[1:]).lstrip("\n")
    return text


def cleanup_brief() -> str:
    """The whole-board cleanup brief, versioned beside `board_policy.md` so evals can pin it."""
    path = Path(__file__).resolve().parent / "board_cleanup_brief.md"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:                                        # pragma: no cover - packaging slip
        return ""
    return re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()


# ------------------------------------------------------------ card turns (protocol 19.10)
#
# A card's "Ask the agent" became Discuss / Plan / Execute (#XS6Q, owner 2026-09-18). Discuss
# and Plan are one `board_ask` turn each, told apart by `mode`; Execute hands the card to a
# terminal pane and is not a turn here at all. What a mode may touch is enforced below, not
# left to the brief: a card turn runs on the Switchboard worker, whose executor would otherwise
# offer the whole pane tool set (commands, file writes, subagents).

CARD_MODES = ("discuss", "plan")

#: The worker's own tools a card turn keeps: reading, never writing or running anything.
CARD_READ_TOOLS = ("read_file", "list_directory", "load_skill", "read_skill_file")

#: The board tools each mode offers. Plan writes only its own card's `## Plan` (and may
#: comment on that card); Discuss keeps the whole ordinary set, cleanup-only tools aside.
CARD_MODE_BOARD_TOOLS = {
    "discuss": ("board_list", "board_read", "board_create_card", "board_update_card",
                "board_move_card", "board_comment"),
    "plan": ("board_list", "board_read", "board_update_card", "board_comment"),
}

#: Where a Plan turn writes. SWITCHBOARD-DESIGN 12.4: plan mode writes the plan onto the card.
PLAN_HEADING = "Plan"

MAX_SEARCH_MATCHES = 80
MAX_SEARCH_FILES = 20000
MAX_SEARCH_FILE_BYTES = 1 << 20
_SEARCH_SKIP_DIRS = frozenset({".git", ".hg", ".svn", "node_modules", "__pycache__", ".venv",
                               "venv", ".mypy_cache", ".pytest_cache", ".relay", ".ssh", ".gnupg",
                               "dist", ".cache"})

SEARCH_SPEC = spec(
    "search_files",
    "Search the workspace's text files for a regular expression (Python syntax, case-insensitive "
    "unless it has an uppercase letter). Returns up to 80 matching lines as path:line: text. "
    "Read-only. Use it to find where something is defined before reading the file.",
    {"pattern": {"type": "string", "description": "Regular expression, e.g. 'def board_ask|board_ask\\('."},
     "path": {"type": "string", "description": "Workspace-relative directory or file to search; default '.'."},
     "glob": {"type": "string", "description": "Only files whose name matches, e.g. '*.py' or '*.cpp'."}},
    ["pattern"])


def card_brief(mode: str) -> str:
    """The Discuss or Plan brief (`board_discuss_brief.md`, `board_plan_brief.md`), beside the policy."""
    path = Path(__file__).resolve().parent / f"board_{mode}_brief.md"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:                                        # pragma: no cover - packaging slip
        return ""
    return re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()


@dataclass
class CardScope:
    """One Discuss or Plan turn on one card: the tools it may call and the card it is about."""
    mode: str
    card_id: str

    def allows(self, name: str) -> bool:
        return (name in CARD_READ_TOOLS or name == "search_files"
                or name in CARD_MODE_BOARD_TOOLS.get(self.mode, ()))

    def tool_specs(self, executor_specs: list[dict]) -> list[dict]:
        """The turn's tool list: the executor's read-only tools, search_files, the mode's board tools."""
        keep = [t for t in executor_specs if t["function"]["name"] in CARD_READ_TOOLS]
        board = [dict(s) for s in TOOL_SPECS if s["function"]["name"] in CARD_MODE_BOARD_TOOLS[self.mode]]
        return keep + [dict(SEARCH_SPEC)] + board

    def refusal(self, name: str) -> str:
        what = "Plan" if self.mode == "plan" else "Discuss"
        return (f"{name} is not available in a {what} turn on #{self.card_id}: it reads the "
                "repository (read_file, list_directory, search_files) and writes only through the "
                "board tools. Writing code is Execute's job — the owner hands the card to a "
                "terminal pane for that.")


def search_workspace(root: Path, args: dict) -> dict:
    """`search_files`: a read-only grep over the workspace, with the file tools' secret guard."""
    from .tools import Workspace                       # late: tools imports nothing of ours
    if set(args) - {"pattern", "path", "glob"}:
        raise BoardToolError("search_files takes pattern, path and glob.")
    pattern = args.get("pattern")
    if not isinstance(pattern, str) or not pattern or len(pattern) > 500:
        raise BoardToolError("pattern must be a regular expression of 1-500 characters.")
    try:
        regex = re.compile(pattern, 0 if any(c.isupper() for c in pattern) else re.I)
    except re.error as exc:
        raise BoardToolError(f"pattern is not a valid regular expression: {exc}") from exc
    glob = args.get("glob")
    if glob is not None and (not isinstance(glob, str) or len(glob) > 100):
        raise BoardToolError("glob must be a short file-name pattern such as '*.py'.")
    workspace = Workspace(root)
    rel = args.get("path") or "."
    try:
        start = workspace.root if rel in (".", "./") else workspace.resolve(str(rel))
    except (ValueError, OSError) as exc:
        raise BoardToolError(str(exc)) from exc
    import fnmatch

    def secret(parts) -> bool:
        return any(p == ".env" or p.startswith(".env.") or p in {"id_rsa", "id_ed25519"}
                   or p.endswith((".pem", ".key")) for p in parts)

    matches: list[str] = []
    scanned = 0
    truncated = False
    walker = [(start.parent, [], [start.name])] if start.is_file() else os.walk(start)
    for folder, dirs, names in walker:
        folder = Path(folder)
        dirs[:] = sorted(d for d in dirs if d not in _SEARCH_SKIP_DIRS
                         and not (folder / d).is_symlink())
        for name in sorted(names):
            if glob and not fnmatch.fnmatch(name, glob):
                continue
            path = folder / name
            relative = path.relative_to(workspace.root)
            if path.is_symlink() or secret(relative.parts):
                continue
            scanned += 1
            if scanned > MAX_SEARCH_FILES:
                truncated = True
                break
            try:
                if path.stat().st_size > MAX_SEARCH_FILE_BYTES:
                    continue
                data = path.read_bytes()
            except OSError:
                continue
            if b"\0" in data[:4096]:
                continue                                   # binary
            for number, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
                if regex.search(line):
                    matches.append(f"{relative}:{number}: {line.strip()[:200]}")
                    if len(matches) >= MAX_SEARCH_MATCHES:
                        truncated = True
                        break
            if truncated:
                break
        if truncated:
            break
    return {"matches": matches, "count": len(matches), "truncated": truncated,
            "files_scanned": min(scanned, MAX_SEARCH_FILES)}


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
    #: Files beyond the primary card this one write also touched — a merge rewrites and
    #: closes every card it folded in, a split creates one file per piece.  Each entry is
    #: (path now, bytes before or None for a file this write created, path it was moved from).
    others: list[tuple[Path, bytes | None, Path | None]] = field(default_factory=list)


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
        #: Set while a `board_cleanup` turn runs (protocol 19.9): it raises the per-turn
        #: ceilings, offers the merge/split/sections tools, and records every write.
        self.cleanup: CleanupLog | None = None
        #: Set while a card's Discuss or Plan turn runs (protocol 19.10, #XS6Q): the tools that
        #: mode offers, and the card a Plan turn may write to. None for a pane's own turns.
        self.card_scope: CardScope | None = None

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

    def begin_cleanup(self, run_id: str, *, dry_run: bool = False, scope: str | None = None,
                      note: str | None = None, limits: dict | None = None) -> CleanupLog:
        """Enter cleanup mode: raised ceilings, the merge/split/sections tools, a changelog."""
        ceilings = dict(CLEANUP_LIMITS)
        for key, value in (limits or {}).items():
            if key in ceilings and isinstance(value, int):
                ceilings[key] = max(0, int(value))
        log = CleanupLog(run_id=run_id, started=self.clock(), dry_run=bool(dry_run),
                         scope=scope, note=note, limits=ceilings,
                         model=self.context.model,
                         cards_before=len(self.board.card_paths()),
                         config_before=self.board.config())
        log.changelog = log.relative_path()
        self.cleanup = log
        return log

    def end_cleanup(self, outcome: str, report: str = "") -> CleanupLog | None:
        """Leave cleanup mode and finish the changelog.  Returns the log, or None."""
        log, self.cleanup = self.cleanup, None
        if log is None:
            return None
        log.outcome = outcome
        log.report = report or ""
        log.finished = self.clock()
        log.cards_after = len(self.board.card_paths())
        log.config_after = self.board.config()
        log.model = log.model or self.context.model
        return log

    def limit(self, key: str) -> int:
        """The ceiling in force: a cleanup's raised one, or the pane turn's."""
        if self.cleanup is not None:
            return int(self.cleanup.limits.get(key, self.limits[key]))
        return int(self.limits[key])

    def tool_specs(self) -> list[dict]:
        specs = list(TOOL_SPECS) + (list(CLEANUP_TOOL_SPECS) if self.cleanup is not None else [])
        return [dict(s) for s in specs]

    def handles(self, name: str) -> bool:
        return name in ALL_TOOL_NAMES or (name == "search_files" and self.card_scope is not None)

    def begin_card_turn(self, mode: str, card_id: str) -> CardScope:
        """A Discuss or Plan turn on one card starts: narrow the tools to what the mode offers."""
        if mode not in CARD_MODES:
            raise BoardToolError(f"mode must be one of {', '.join(CARD_MODES)}.")
        self.card_scope = CardScope(mode, normalize_id(card_id))
        return self.card_scope

    def end_card_turn(self) -> None:
        self.card_scope = None

    def _check_card_scope(self, name: str, args: dict) -> None:
        """A Plan turn writes its own card's `## Plan` and nothing else; Discuss has no extra rule."""
        scope = self.card_scope
        if scope is None:
            return
        if not scope.allows(name):
            raise BoardToolError(scope.refusal(name), code="board_mode_refused", mode=scope.mode)
        if scope.mode != "plan" or name not in WRITE_TOOLS:
            return
        target = normalize_id(args.get("id")) if args.get("id") else ""
        if target != scope.card_id:
            raise BoardToolError(
                f"A Plan turn writes only to #{scope.card_id}, the card being planned. Mention "
                f"#{target or '?'} in the plan instead of changing it.",
                code="board_mode_refused", mode="plan")
        if name == "board_update_card":
            extra = set(args) - {"id", "base_hash", "replace_section", "append_section"}
            blocks = [args.get(k) for k in ("replace_section", "append_section") if args.get(k) is not None]
            headings = {str(b.get("heading") or "").strip().lstrip("#").strip().lower()
                        for b in blocks if isinstance(b, dict)}
            if extra or not blocks or headings != {PLAN_HEADING.lower()}:
                raise BoardToolError(
                    f"A Plan turn writes the card's `## {PLAN_HEADING}` section and nothing else: "
                    f"call board_update_card with replace_section {{heading: \"{PLAN_HEADING}\", "
                    "text}. The title, the issue, labels and status are Discuss's to change.",
                    code="board_mode_refused", mode="plan")

    # ---- dispatch -------------------------------------------------------------
    def preview(self, name: str, args: dict) -> str:
        """One human-readable block for the pane's tool line (no side effects)."""
        if not isinstance(args, dict):
            return name.upper().replace("_", " ")
        head = name.replace("board_", "").replace("_", " ").upper()
        bits = []
        for key in ("id", "tab", "status", "title", "kind", "query", "reason", "pattern", "glob"):
            if args.get(key):
                bits.append(f"{key}: {str(args[key])[:120]}")
        return f"SWITCHBOARD {head}\n\n" + ("\n".join(bits) or "(no arguments)")

    def run(self, name: str, args: dict) -> dict:
        if not self.handles(name):
            raise BoardToolError(f"unknown Switchboard tool {name!r}")
        if not isinstance(args, dict):
            raise BoardToolError("Tool arguments must be an object.")
        try:
            if name in CLEANUP_TOOL_NAMES and self.cleanup is None:
                raise BoardToolError(f"{name} is only available during a Switchboard cleanup.",
                                     code="board_refused")
            self._check_card_scope(name, args)
            if name == "search_files":
                return search_workspace(Path(self.board.repo), dict(args))
            if name in WRITE_TOOLS:
                self._check_write_budget(name, args)
            handler = {"board_list": self._list, "board_read": self._read,
                       "board_create_card": self._create, "board_update_card": self._update,
                       "board_move_card": self._move, "board_comment": self._comment,
                       "board_merge_cards": self._merge, "board_split_card": self._split,
                       "board_sections": self._sections}[name]
            return handler(dict(args))
        except BoardToolError as exc:
            # A dry run's refusals are the plan, not a problem: they are already recorded as
            # proposed changes, so they do not also go into the refusal list.
            if self.cleanup is not None and exc.code != "board_cleanup_dry_run":
                self.cleanup.refusals.append({"tool": name, "error": str(exc), "code": exc.code})
            return exc.to_result()
        except B.BoardConflict as exc:
            return {"error": str(exc), "code": "board_conflict", "current_hash": exc.current_hash}
        except B.BoardError as exc:
            if self.cleanup is not None:
                self.cleanup.refusals.append({"tool": name, "error": str(exc), "code": "board_error"})
            return {"error": str(exc), "code": "board_error"}

    # ---- limits ---------------------------------------------------------------
    def _check_write_budget(self, name: str, args: dict | None = None) -> None:
        if self.cleanup is not None and self.cleanup.dry_run:
            # A preview run: the plan is recorded, nothing is written.
            self.cleanup.record(CleanupChange(
                action=name.replace("board_", "").replace("_card", "").replace("_cards", ""),
                card_id=str((args or {}).get("id") or (args or {}).get("into") or "").lstrip("#").upper(),
                summary=preview_line(name, args or {}), proposed=True))
            raise BoardToolError(
                "This is a dry run of the Switchboard cleanup: nothing is written. Carry on "
                "reading the board and call the write tools as you would — each call is recorded "
                "as a proposal — then summarize the plan in your reply.",
                code="board_cleanup_dry_run")
        if not self.enforce_limits:
            return
        if self.autonomy == "off":
            raise BoardToolError("Switchboard writes are turned off for this workspace (autonomy: off).",
                                 code="board_autonomy_off")
        if name == "board_create_card":
            if self.creates_this_turn >= self.limit("max_creates_per_turn"):
                raise BoardToolError(
                    f"Switchboard limit: {self.limit('max_creates_per_turn')} new cards per turn. "
                    "Summarize the remaining requests in your reply instead of creating more.",
                    code="board_rate_limited", scope="turn", limit=self.limit("max_creates_per_turn"))
        elif self.writes_this_turn >= self.limit("max_writes_per_turn"):
            raise BoardToolError(
                f"Switchboard limit: {self.limit('max_writes_per_turn')} card writes per turn. "
                "Summarize the rest in your reply.",
                code="board_rate_limited", scope="turn", limit=self.limit("max_writes_per_turn"))

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
        # The full row of protocol 19.2. `created`, the task counts and `milestone` were promised
        # there but never sent, so the pane's age and `☑ done/total` badges had nothing to draw
        # (found while rebuilding the pane as rows, 2026-09-18).
        tasks = card.tasks()
        return {"id": card.id, "title": card.title, "type": card.type, "status": card.status,
                "tab": self._tab_of(card), "labels": list(card.front.get("labels") or []),
                "assignee": card.front.get("assignee"), "waiting_on": card.front.get("waiting_on"),
                "rank": card.rank, "private": card.private,
                "path": str(card.path.relative_to(self.board.repo)) if card.path else None,
                "thread_entries": thread_counts.get(card.id or "", 0),
                "created": str(card.front.get("created") or ""),
                "milestone": card.front.get("milestone"),
                "topic": card.front.get("topic"),
                "implemented_by": card.front.get("implemented_by"),
                "tasks_total": len(tasks),
                "tasks_done": sum(1 for task in tasks if task.done)}

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
                # The user's own words, so the pane can offer them for editing without parsing
                # Markdown itself.  `issue_heading` is the spelling this card uses today
                # (`Request` on a card written before 2026-09-18); a write settles it on `Issue`.
                "issue": _section_text(card.body, B.ISSUE_HEADING).strip("\n"),
                "issue_heading": next((h for h in section_headings(card.body)
                                       if _heading_matches(h, B.ISSUE_HEADING)), B.ISSUE_HEADING),
                "tasks": [{"item_id": t.item_id, "text": t.text, "status": t.status,
                           "done": t.done, "depth": t.depth, "card": t.card}
                          for t in card.tasks()],
                "thread_total": len(entries),
                "thread": [{"entry_id": e.entry_id, "author": e.author, "kind": e.kind,
                            "attrs": dict(e.attrs), "text": e.text} for e in tail]}

    # ---- writes ---------------------------------------------------------------
    def _record(self, action: str, card: B.Card, summary: str, before: bytes | None,
                thread_size: int, moved_from: Path | None = None,
                others: Sequence[tuple[Path, bytes | None, Path | None]] = (),
                cards: Sequence[str] = ()) -> str:
        return self._record_path(action, card.path, card.id or "", summary, before,
                                 self.board.thread_path(card.id or "", card.private),
                                 thread_size, moved_from, others, cards)

    def _record_path(self, action: str, path: Path, card_id: str, summary: str,
                     before: bytes | None, thread_path: Path | None, thread_size: int,
                     moved_from: Path | None = None,
                     others: Sequence[tuple[Path, bytes | None, Path | None]] = (),
                     cards: Sequence[str] = ()) -> str:
        """Record one undoable write, announce it, and (in a cleanup) log it for the changelog."""
        write_id = f"w-{int(self.clock() * 1000):x}-{secrets.token_hex(2)}"
        record = WriteRecord(write_id=write_id, action=action, card_id=card_id,
                             path=path, summary=summary, at=self.clock(), before=before,
                             thread_path=thread_path, thread_size=thread_size,
                             moved_from=moved_from, others=list(others))
        self.writes[write_id] = record
        self._order.append(write_id)
        while len(self._order) > 50:
            self.writes.pop(self._order.pop(0), None)
        rel = str(path.relative_to(self.board.repo))
        activity = {"event": "board_activity", "write_id": write_id, "id": card_id or None,
                    "action": action, "actor": self.context.actor, "model": self.context.model,
                    "pane": self.context.pane, "turn_id": self.context.turn_id,
                    "summary": summary, "path": rel, "undo_seconds": UNDO_SECONDS}
        if self.cleanup is not None:
            activity["cleanup"] = True
            activity["run_id"] = self.cleanup.run_id
            self.cleanup.record(CleanupChange(action=action, card_id=card_id, summary=summary,
                                              path=rel, write_id=write_id, cards=list(cards)))
        self.emit(activity)
        self.emit({"event": "board_changed", "upserts": [card_id] if card_id else [],
                   "removed": [], "write_id": write_id})
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

        if self.enforce_limits and not self.rate.claim(self.limit("max_creates_per_hour")):
            raise BoardToolError(
                f"Switchboard limit: {self.limit('max_creates_per_hour')} new cards per hour for this "
                "workspace. Summarize the remaining requests in your reply.",
                code="board_rate_limited", scope="hour", limit=self.limit("max_creates_per_hour"))

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
            span = _section_span(card.body, B.ISSUE_HEADING)
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
            # The hash is named in the message as well as the fields: a model that mistyped or
            # invented one repeated the same wrong hash four times in a live cleanup, because the
            # sentence did not say what the right one was (2026-09-18).
            raise BoardToolError(f"#{card_id} does not have the base_hash you sent; it is now "
                                 f"{current}. Read it again and reapply your change to what you "
                                 "read back.", code="board_conflict", current_hash=current, id=card_id)

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
            # One spelling for the section that holds the user's own words, whichever the caller
            # used: a card written as `## Request` comes out saying `## Issue` once it is edited.
            if heading.strip().lower() in B.ISSUE_HEADINGS:
                heading = B.ISSUE_HEADING
            text = _without_own_heading(_text(block.get("text"), "text", MAX_SECTION), heading)
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

    # ---- cleanup-only writes ----------------------------------------------------
    def _merge(self, args: dict) -> dict:
        """`board_merge_cards`: fold redundant cards into one, losing nothing (protocol 19.9)."""
        allowed = {"into", "cards", "reason"}
        if set(args) - allowed:
            raise BoardToolError(f"board_merge_cards takes {', '.join(sorted(allowed))}.")
        into_id = normalize_id(args.get("into"), "into")
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        source_ids = [normalize_id(c, "cards") for c in _string_list(args.get("cards"), "cards")]
        if not source_ids:
            raise BoardToolError("cards must name at least one card to merge in.")
        if len(source_ids) > 10:
            raise BoardToolError("merge at most 10 cards into one at a time.")
        if into_id in source_ids:
            raise BoardToolError(f"#{into_id} cannot be merged into itself.")
        if len(set(source_ids)) != len(source_ids):
            raise BoardToolError("cards lists the same card twice.")
        into = self._card(into_id)
        if into.status in ("done", "dropped"):
            raise BoardToolError(f"#{into_id} is closed ({into.status}); merge into an open card.")
        sources = [self._card(cid) for cid in source_ids]
        for src in sources:
            if src.type != into.type:
                raise BoardToolError(f"#{src.id} is a {src.type} card and #{into_id} is a "
                                     f"{into.type} card; merge like with like.")

        size = self._thread_size(into)
        result = B.merge_cards(self.board, into, sources, reason=reason,
                               category_of=lambda card: self.board.category_of(card.path))
        self.writes_this_turn += 1
        titles = ", ".join(f"#{m['id']} {m['title']}" for m in result["merged"])[:300]
        summary = f"merged {len(sources)} card(s) in: {titles}"
        self._append(into, f"- ✦ {self.context.actor} merged "
                           f"{', '.join('#' + m['id'] for m in result['merged'])} into this card · {reason}",
                     kind="event")
        for merged, src in zip(result["merged"], sources):
            self._append(src, f"- ✦ {self.context.actor} merged this card into #{into_id} · {reason} · "
                              "its text is kept here and copied there; this card stays as the record",
                         kind="event")
        write_id = self._record("merge", into, summary, result["into_before"], size,
                                others=result["others"],
                                cards=[m["id"] for m in result["merged"]])
        return {"id": into.id, "path": result["into_path"], "hash": result["into_hash"],
                "merged": [{k: v for k, v in m.items() if k != "src"} for m in result["merged"]],
                "write_id": write_id, "summary": summary}

    def _split(self, args: dict) -> dict:
        """`board_split_card`: one card per piece of unrelated work (protocol 19.9)."""
        allowed = {"id", "parts", "reason", "close"}
        if set(args) - allowed:
            raise BoardToolError(f"board_split_card takes {', '.join(sorted(allowed))}.")
        card_id = normalize_id(args.get("id"))
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        parts = args.get("parts")
        if not isinstance(parts, list) or not 2 <= len(parts) <= 10:
            raise BoardToolError("parts must be a list of 2 to 10 pieces.")
        card = self._card(card_id)
        if card.status in ("done", "dropped"):
            raise BoardToolError(f"#{card_id} is closed ({card.status}); there is nothing to split.")
        clean: list[dict] = []
        for index, part in enumerate(parts, 1):
            if not isinstance(part, dict) or set(part) - {"title", "request", "status", "tab", "labels"}:
                raise BoardToolError(f"part {index} takes title, request, status, tab and labels.")
            clean.append({"title": _one_line(part.get("title"), f"part {index} title", MAX_TITLE),
                          "request": _text(part.get("request"), f"part {index} request", MAX_REQUEST),
                          "status": str(part.get("status") or card.status).strip().lower(),
                          "tab": part.get("tab"),
                          "labels": _string_list(part.get("labels"), f"part {index} labels")})
        if self.enforce_limits:
            for _ in clean:
                if not self.rate.claim(self.limit("max_creates_per_hour")):
                    raise BoardToolError(
                        f"Switchboard limit: {self.limit('max_creates_per_hour')} new cards per hour "
                        "for this workspace. Summarize the rest of the split in your reply.",
                        code="board_rate_limited", scope="hour")
        category = (B.PLAN_FOLDER if card.type == "plan" else B.MEMORY_FOLDER if card.type == "memory"
                    else self.board.category_of(card.path))
        size = self._thread_size(card)
        result = B.split_card(self.board, card, clean, reason=reason, category=category,
                              close=bool(args.get("close")), tab_category=self._category_for_tab)
        self.creates_this_turn += len(clean)
        self.writes_this_turn += 1
        summary = ("split into " + ", ".join(f"#{c['id']} {c['title']}" for c in result["children"]))[:400]
        self._append(card, f"- ✦ {self.context.actor} {summary} · {reason}"
                           + (" · this card is closed; every piece moved out" if result["closed"] else ""),
                     kind="event")
        for child in result["children"]:
            self.board.append_thread(child["id"], f"- ✦ {self.context.actor} split this card out of "
                                                  f"#{card_id} · {reason}",
                                     author=self.context.actor, kind="event", private=card.private,
                                     **self.context.attrs())
        write_id = self._record("split", card, summary, result["before"], size,
                                moved_from=result["moved_from"], others=result["others"],
                                cards=[c["id"] for c in result["children"]])
        return {"id": card.id, "path": result["path"], "hash": result["hash"],
                "children": result["children"], "closed": result["closed"],
                "write_id": write_id, "summary": summary}

    def _sections(self, args: dict) -> dict:
        """`board_sections`: the board's own columns and category folders (protocol 19.9)."""
        allowed = {"columns", "tabs", "reason"}
        if set(args) - allowed:
            raise BoardToolError(f"board_sections takes {', '.join(sorted(allowed))}.")
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        if args.get("columns") is None and args.get("tabs") is None:
            raise BoardToolError("board_sections takes columns, tabs, or both.")
        config = self.board.config()
        before_bytes = (self.board.config_path.read_bytes()
                        if self.board.config_path.exists() else b"")
        changes: list[str] = []

        if args.get("columns") is not None:
            columns = _string_list(args.get("columns"), "columns")
            if not columns:
                raise BoardToolError("columns must name at least one section.")
            unknown = [c for c in columns if c not in B.COLUMN_IDS]
            if unknown:
                raise BoardToolError(f"unknown column(s) {', '.join(unknown)}; the board's sections "
                                     f"come from: {', '.join(B.COLUMN_IDS)}.")
            if len(set(columns)) != len(columns):
                raise BoardToolError("columns lists the same section twice.")
            if list(config.get("columns") or []) != columns:
                changes.append(f"columns: {', '.join(str(c) for c in config.get('columns') or [])} "
                               f"→ {', '.join(columns)}")
                config["columns"] = columns

        if args.get("tabs") is not None:
            tabs = args.get("tabs")
            if not isinstance(tabs, list) or not 1 <= len(tabs) <= 20:
                raise BoardToolError("tabs must be a list of 1 to 20 entries.")
            clean: list[dict] = []
            seen: set[str] = set()
            for index, tab in enumerate(tabs, 1):
                if not isinstance(tab, dict) or set(tab) - {"id", "folder", "filter"}:
                    raise BoardToolError(f"tab {index} takes id and either folder or filter.")
                tab_id = _one_line(tab.get("id"), f"tab {index} id", 40).lower()
                if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", tab_id):
                    raise BoardToolError(f"tab id {tab_id!r} must be lower-case letters, digits, - and _.")
                if tab_id in seen:
                    raise BoardToolError(f"tab {tab_id!r} is listed twice.")
                seen.add(tab_id)
                entry: dict = {"id": tab_id}
                if tab.get("folder"):
                    folder = _one_line(tab["folder"], f"tab {index} folder", 60)
                    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", folder):
                        raise BoardToolError(f"tab folder {folder!r} must be one plain directory name.")
                    entry["folder"] = folder
                elif tab.get("filter"):
                    entry["filter"] = _one_line(tab["filter"], f"tab {index} filter", 120)
                else:
                    raise BoardToolError(f"tab {tab_id!r} needs a folder or a filter.")
                clean.append(entry)
            folders = {t["folder"] for t in clean if t.get("folder")}
            orphaned = sorted({self.board.category_of(c.path) for c in self.board.cards()
                               if c.type == "work" and c.path} - folders)
            if orphaned:
                raise BoardToolError(
                    f"these folders still hold cards and no tab names them: {', '.join(orphaned)}. "
                    "Move those cards with board_move_card first, then drop the tab.",
                    code="board_refused", requires="empty_folder")
            if config.get("tabs") != clean:
                changes.append(f"tabs: {', '.join(str(t.get('id')) for t in config.get('tabs') or [])} "
                               f"→ {', '.join(t['id'] for t in clean)}")
                config["tabs"] = clean

        if not changes:
            raise BoardToolError("nothing to change: the board already has these sections.")
        B.write_config(self.board, config)
        self.writes_this_turn += 1
        summary = "; ".join(changes)[:400] + f" · {reason}"
        write_id = self._record_path("sections", self.board.config_path, "", summary,
                                     before_bytes or None, None, 0)
        return {"path": str(self.board.config_path.relative_to(self.board.repo)),
                "columns": list(config.get("columns") or []),
                "tabs": list(config.get("tabs") or []),
                "changes": changes, "write_id": write_id, "summary": summary}

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
        # A merge or a split touched more than the card it was recorded against: put the
        # cards it folded in back where they were, and remove the cards it created.
        for path, before, moved_from in record.others:
            here = path
            if moved_from is not None and here.exists():
                moved_from.parent.mkdir(parents=True, exist_ok=True)
                os.replace(here, moved_from)
                here = moved_from
            if before is None:
                if _tracked_by_git(self.board.repo, here):
                    continue
                if here.exists():
                    here.unlink()
            else:
                B._atomic_write(here, before.decode("utf-8"))
        record.undone = True
        self.emit({"event": "board_changed", "upserts": [] if removed else [record.card_id],
                   "removed": removed, "write_id": write_id, "undo_of": write_id})
        return {"undone": write_id, "id": record.card_id, "action": record.action,
                "removed": bool(removed), "also_restored": len(record.others)}

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

def preview_line(name: str, args: dict) -> str:
    """One line describing a write a tool call would make (the dry-run changelog)."""
    head = name.replace("board_", "").replace("_", " ")
    bits = [f"{key}: {_short(args[key], 160)}" for key in
            ("into", "cards", "title", "status", "tab", "kind", "columns", "tabs", "reason")
            if args.get(key)]
    if isinstance(args.get("parts"), list):
        bits.append("parts: " + ", ".join(_short(p.get("title"), 60) for p in args["parts"]
                                          if isinstance(p, dict)))
    if isinstance(args.get("fields"), dict):
        bits.append("fields: " + ", ".join(sorted(args["fields"])))
    return f"{head} — " + ("; ".join(bits) or "(no arguments)")


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
    head, start, end = span
    existing = body[start:end]
    if replace:
        # A replace also settles the heading's spelling, which is how a card that still says
        # `## Request` comes out saying `## Issue` once its text is edited.
        line = f"## {heading}"
        if body[head:start].strip() != line:
            return body[:head] + line + "\n" + block + "\n" * _trailing_blanks(existing) + body[end:]
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
