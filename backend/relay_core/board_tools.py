# SPDX-License-Identifier: AGPL-3.0-or-later
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
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Sequence

from . import board as B
from . import qa_verifiers as QA
from .provider import Cancelled

AUTONOMY = ("off", "suggest", "auto")

#: The folders a project may keep its Switchboard in, in precedence order (`board.BOARD_FOLDERS`):
#: `.switchboard/`, `switchboard/`, `issues/`.
BOARD_FOLDERS = B.BOARD_FOLDERS

#: The folder a *new* board is created in: `<project>/.switchboard/board.yaml`, hidden since the
#: owner's decision of 2026-09-19.  A `configure` whose `board.folder` says otherwise overrides it
#: for that pane (protocol 19.1), which is how the "Hidden Switchboard folder" option reaches here.
BOARD_FOLDER = B.DEFAULT_BOARD_FOLDER

#: What a pane's board is, at any moment (protocol 19.12):
#:   "ready"          - the board exists; the full tools and the full policy block.
#:   "uninitialized"  - the project has no board and the GUI says one may be offered: the agent
#:                      gets `board_create_card` alone and a one-line note, and the first card
#:                      asks the user "Initialize a project and create a Switchboard here?".
#: A pane with no board at all has no `BoardTools` and neither state.
BOARD_STATES = ("ready", "uninitialized")

#: Why the worker is asking to initialize a project (`board_init_request.reason`).
INIT_REASONS = ("agent-card", "card-command")

#: What the agent is told when there is no Switchboard and the user has said not to make one.
#: A tool result, not an exception: the turn carries on without the board.
NO_BOARD_TEXT = ("This project has no Switchboard and the user declined to create one. Do not "
                 "call the board tools again in this conversation; say what you would have "
                 "filed, in your reply, and carry on with the work.")

#: The one-line note an uninitialized board puts in the system prompt, in place of the policy.
UNINITIALIZED_NOTE = ("\n\nSwitchboard: this project has no Switchboard yet; creating a card with "
                      "board_create_card will ask the user to initialize one.\n")

#: What an uninitialized board offers: creating a card, and nothing else.  There is nothing to
#: read, move or comment on until the first card exists.
UNINITIALIZED_TOOLS = ("board_create_card",)

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

#: Fields no agent write may touch.  `status`, `rank` and `section` belong to `board_move_card`
#: (the last is where a card is parked, #3XZV); `source` is the provenance of the owner's own
#: words; `private` would move the file between the git tree and the private root, which is the
#: owner's decision.
IMMUTABLE_FIELDS = frozenset({"id", "type", "created", "source", "rank", "status", "private",
                              "section"})

#: Sections an agent writes freely.  Anything else in a card body is owner text: it may
#: still be rewritten (decision 12.3) but the old and new text go into the thread.
AGENT_SECTIONS = frozenset({
    "findings", "plan", "options", "implementer check", "qa checklist", "evidence",
    "notes", "tasks", "steps", "verdict", "qa verdict", "resolution", "decisions",
})

#: A card in a QA lane is closed with a verdict section in the body; any pane may flip it once
#: the verdict is there (owner, 2026-09-20, card #76DJ: the verdict is the gate, not the
#: closer's model family). Relay Free still may not verify (owner, 2026-09-19).
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
         "List Switchboard cards: the project's own tracker, in its Switchboard folder "
         "(`.switchboard/`, which a ripgrep search of the project skips, so this tool — not `rg` — "
         "is how you find cards). One row per card: id, title, type, status, tab, labels, "
         "assignee, waiting_on and thread size. Search here before creating a card, so a request "
         "that already has one updates it instead.",
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
          "section": {"type": "string", "description": "Park the new card in this manual section (a "
                                                    "column that collects nothing), instead of its "
                                                    "status's own section."},
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
                        "card": {"type": "string", "description": "Id of a card that mirrors this item."},
                        "blocked_by": {"type": "array", "description": "What has to happen first: the "
                                       "1-based number of another task in this same list, an existing "
                                       "item id, or #CARD. Replaces this item's markers.",
                                       "items": {"type": ["integer", "string"]}}},
                        "required": ["text"], "additionalProperties": False}}},
         ["id", "base_hash"]),
    spec("board_move_card",
         "Move a card to another status (column), another tab (category) or another position. `reason` is "
         "required and goes into the thread. Moving into needs-qa-llm or needs-qa-human requires an "
         "evidence path. Relay stamps `implemented_by` and `verified_by` with the pane's own "
         "provider/model, so you do not type them. Closing a card that is in a QA lane requires a "
         "verdict section in the body — any pane may flip it once the verdict is there; Relay Free still may not.",
         {"id": _ID_ARG,
          "status": {"type": "string", "description": "Target status; the file moves into the matching state folder."},
          "section": {"type": "string", "description": "Park the card in this manual section (a column "
                                                      "that collects nothing) and leave its status "
                                                      "alone; an empty string takes it out. The "
                                                      "board's stage moves never touch it."},
          "tab": {"type": "string", "description": "Target tab id; the file moves into that category folder."},
          "before": {"type": "string", "description": "Id of the card this one should sit before in the column."},
          "after": {"type": "string", "description": "Id of the card this one should sit after in the column."},
          "reason": {"type": "string", "description": "Why, in one line. Recorded in the thread."},
          "evidence": {"type": "string", "description": "Evidence path, e.g. docs/qa_evidence/2026-09-17-slug/."},
          "implemented_by": {"type": "string",
                             "description": "Only when Relay cannot know it (a guest CLI writing "
                                            "through the bridge): the model that implemented the "
                                            "change. Relay's own stamp wins."}},
         ["id", "reason"]),
    spec("board_import_items",
         "Create Switchboard cards from tracking the project already has — a TODO.md, a backlog/ "
         "folder, a GitHub-style issues list, spec files — through the same import the Switchboard "
         "page uses, so every card carries a `source` key and is never imported twice. Call it "
         "only after the owner said yes: it writes. Returns what each key became.",
         {"keys": {"type": "array", "items": {"type": "string"}, "minItems": 1, "maxItems": 1000,
                    "description": "Source keys of the items to import, from `board_import_propose` "
                                   "or the survey's proposals."},
          "tab": {"type": "string", "description": "Tab id the cards land in; default features."}},
         ["keys"]),
    spec("board_comment",
         "Append one entry to a card's thread: a note, a question for the user, a decision they made, "
         "evidence, or progress. A question is numbered and carries your recommendation. A decision "
         "quotes the user's own words in quotation marks. The thread is the card's discussion history "
         "and is append-only; nothing you write here is ever rewritten.",
         {"id": _ID_ARG,
          "kind": {"type": "string", "enum": list(COMMENT_KINDS)},
          "text": {"type": "string"},
          "pane_token": {"type": "string",
                         "description": "The pane's session token, when this entry records a "
                                        "hand-off to a terminal pane (Execute, #HKAP): the thread "
                                        "draws it as a link that reveals that pane. At most 64 "
                                        "characters, no whitespace or '>'."}},
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
         "Change the board's own structure in the board's board.yaml: which sections (columns) the one "
         "list is divided into and in what order, what each of them collects, what they are called, "
         "and which category folders (tabs) a card's file can live in. Add, remove, merge and rename "
         "a section are all this one call. No card moves and no status changes: dropping a section "
         "does not hide its cards — a status no section collects gets a section of its own — but a "
         "tab whose folder still holds cards cannot be dropped. Use this sparingly: it changes the "
         "board for everyone.",
         {"columns": {"type": "array", "items": {"type": "string"},
                      "description": f"The whole ordered section list, from: {', '.join(B.COLUMN_IDS)}, "
                                     "or an id of your own that column_statuses gives statuses to."},
          "column_statuses": {"type": "object",
                              "description": "Which statuses each section collects, e.g. "
                                             "{\"needs-qa\": [\"needs-qa-llm\", \"needs-qa-human\"]}. "
                                             "Merging two sections is one section with both sets, the "
                                             "other left out of columns. One status, one section. "
                                             "An explicit empty list is a manual section, one you "
                                             "fill by hand with board_move_card's `section`."},
          "column_titles": {"type": "object",
                            "description": "What a section is called, over an id that does not change, "
                                           "e.g. {\"ready\": \"Up next\"}. \"\" puts the name back."},
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

#: Cleanup-only tools the owner may also reach directly, through a control in the GUI rather than
#: through a model: the section editor behind the gear on the Switchboard's list of sections. The
#: fence is on the agent's autonomy, and this is the owner asking for it by hand.
OWNER_TOOLS = ("board_sections",)
ALL_TOOL_NAMES = TOOL_NAMES + CLEANUP_TOOL_NAMES
WRITE_TOOLS = ("board_create_card", "board_update_card", "board_move_card", "board_comment",
               "board_merge_cards", "board_split_card", "board_sections", "board_import_items")


# ------------------------------------------------------------------ small helpers

def normalize_id(value, what: str = "id") -> str:
    if not isinstance(value, str):
        raise BoardToolError(f"{what} must be a card id such as K7Q2.")
    text = value.strip().lstrip("#").upper()
    if not B.valid_id(text):
        raise BoardToolError(f"{value!r} is not a card id: four Crockford-base32 characters with a letter, e.g. K7Q2.")
    return text


def model_family(model: str | None) -> str:
    """The vendor family of a model string, for the `qa` recommendation block.

    One line since card #T71W: `relay_core.qa_verifiers.family` is the single table, so the
    signature form and the free-text form of the same model land on the same family
    (`anthropic/claude-opus-5` and `Claude Opus 5 (pane 2)` are both `anthropic`) and an
    aggregator's route is read as the model's vendor (`openrouter/deepseek-…` is `deepseek`).
    Before that this split on the slash and took the first word, so `Claude Opus 5` was
    `claude` and `anthropic/claude-opus-5` was `anthropic` — two names for one lab, and the
    independence rule let each close what the other wrote.
    """
    return QA.family(model)


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

#: The page agent's board tools (protocol 19.18): the ordinary set plus merge and split —
#: merging duplicates is that conversation's headline job — plus the import. `board_sections`
#: stays with a cleanup: restructuring the whole board is a run with a preview of its own.
CHAT_BOARD_TOOLS = ("board_list", "board_read", "board_create_card", "board_update_card",
                    "board_move_card", "board_comment", "board_merge_cards",
                    "board_split_card", "board_import_items")

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
    "Read-only. Use it to find where something is defined before reading the file. When the workspace "
    "is the home directory or /, pass `path`: crawling all of it is refused as too slow.",
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
class ChatScope:
    """The Switchboard page agent's turn (protocol 19.18): read the repository, write the board.

    It rides in the `card_scope` slot — that is where the agent looks for a turn's scope — so it
    answers the same three questions `CardScope` does. `chat` marks it apart for the cleanup-only
    fence in `run`.
    """
    chat: bool = True
    mode: str = "chat"

    def allows(self, name: str) -> bool:
        return name in CARD_READ_TOOLS or name == "search_files" or name in CHAT_BOARD_TOOLS

    def tool_specs(self, executor_specs: list[dict]) -> list[dict]:
        """The turn's tool list: the executor's read-only tools, search_files, the board tools."""
        keep = [t for t in executor_specs if t["function"]["name"] in CARD_READ_TOOLS]
        board = [dict(s) for s in TOOL_SPECS if s["function"]["name"] in CHAT_BOARD_TOOLS]
        cleanup = [dict(s) for s in CLEANUP_TOOL_SPECS
                   if s["function"]["name"] in CHAT_BOARD_TOOLS]
        return keep + [dict(SEARCH_SPEC)] + board + cleanup

    def refusal(self, name: str) -> str:
        return (f"{name} is not available to the Switchboard page agent: it reads the repository "
                "(read_file, list_directory, search_files) and changes the board through the board "
                "tools. Writing code is a card's Execute, not this conversation.")


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
    from .tools import Workspace, wide_root            # late: tools imports nothing of ours
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
    # Card #2Y96, the same cost guard run_command has: a card turn whose workspace is the home
    # directory or `/` must be given a path before it crawls. The ceilings below still apply.
    if why := wide_root(start):
        raise BoardToolError(f"Searching all of {start} ({why}) would take minutes and return little, "
                             f"so Relay refuses it — this is a cost limit, not a permission one. Pass "
                             f"`path` naming the directory to search, e.g. {start / '<subdirectory>'}.")
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
    """Who is writing, for the thread entries and the activity events.

    `preset` and `model` together are what the worker knows about *itself*, and `signature()`
    turns them into the canonical `provider/model` a card records as its `implemented_by` or
    `verified_by` (card #T71W).  Both are set per turn by the agent (`Agent.ask`); a guest
    writing through the bridge sets neither, and then the agent's own `implemented_by`
    argument is what the card gets.
    """
    actor: str = "agent"
    model: str | None = None
    preset: str | None = None
    pane: str | None = None
    turn_id: str | None = None
    session_id: str | None = None

    def signature(self) -> str:
        """This worker's own `provider/model`, or "" when it cannot know it."""
        return QA.signature(self.preset, self.model)

    def attrs(self) -> dict:
        out = {}
        if self.model:
            out["model"] = self.model
        if self.pane:
            out["pane"] = str(self.pane)
        if self.turn_id:
            out["turn"] = f"{self.session_id}/{self.turn_id}" if self.session_id else str(self.turn_id)
        return out


def find_board_root(workspace: str | os.PathLike | None,
                    explicit_dir: str | os.PathLike | None = None,
                    folder: str | None = None) -> Path | None:
    """The board directory that governs `workspace`, or None when there is none.

    The one place the backend decides which board a message is about, so the worker, the agent's
    tools and `#K7Q2` attachments all land on the same tree, and so does the GUI, which has always
    walked up to the nearest ancestor holding a board.  Before 2026-09-18 the backend took
    `<workspace>/issues` literally, so a pane opened in a subdirectory of a project saw no board
    at all while the window's Switchboard showed one.

    **The rule, and the C++ `relay::boardRootFor` must match it exactly:** an explicit `board.dir`
    (protocol 19.1) always wins.  Otherwise the walk starts at the resolved workspace and climbs to
    the filesystem root; at each directory the candidates are tried in `B.BOARD_FOLDERS` order —
    `.switchboard/board.yaml`, then `switchboard/board.yaml`, then `issues/board.yaml` — and the
    first hit wins.  So the **nearest ancestor** wins over a further one whatever its spelling, and
    a single directory holding more than one of them is read as the first in that order.

    `folder` is the folder a board **would** go in, for an `explicit_dir` that names a project with
    no board yet; it never affects the walk, which only ever finds folders that exist.

    **An absent or empty workspace has no board**: the process's cwd, the environment and this
    file's location are never consulted.  A worker started from the directory Relay was launched in
    must not adopt *that* project's board because the `configure` it was sent named no workspace —
    which is exactly what `workspace: ""` used to do, quietly opening the launch directory's 186
    cards in a window that pointed somewhere else.
    """
    if explicit_dir is not None and str(explicit_dir).strip():
        root = named_board_root(explicit_dir, folder)
        return root if (root / B.BOARD_CONFIG).is_file() else None
    if workspace is None or not str(workspace).strip():
        return None
    try:
        here = Path(workspace).expanduser().resolve()
    except OSError:                                     # pragma: no cover - unreadable path
        return None
    for directory in (here, *here.parents):
        root = B.board_folder(directory)
        if root is not None:
            return root
    return None


def named_board_root(explicit_dir: str | os.PathLike, folder: str | None = None) -> Path:
    """The board directory a `board.dir` (or a `project`) names, whether or not it exists yet.

    It may name the board folder itself or the project that holds one, because the GUI has both in
    hand and should not have to guess which spelling the worker wants.  An existing `board.yaml`
    decides it — the folder's own, then `B.BOARD_FOLDERS` in order.  With none of them present the
    name decides: a directory already called `.switchboard`, `switchboard` or `issues` is taken as
    the board folder, and anything else is a project, whose board **would** go in `folder` — the
    `board.folder` of the `configure` that pointed this worker, which is the "Hidden Switchboard
    folder" option, defaulting to `.switchboard`.  Nothing is created here either way.

    Resolved, like the walk's answer in `find_board_root`, because `root` is what a GUI routes
    events by: two spellings of one directory must not look like two boards.
    """
    here = Path(explicit_dir).expanduser().resolve()
    if (here / B.BOARD_CONFIG).is_file():
        return here
    found = B.board_folder(here)
    if found is not None:
        return found
    if here.name in B.BOARD_FOLDERS:
        return here
    return here / (folder if folder in B.BOARD_FOLDERS else B.DEFAULT_BOARD_FOLDER)


def board_at(root: str | os.PathLike) -> B.Board:
    """A `Board` on `root`, with `repo` the project directory that holds it.

    The repo is where `.relay/board-rate.json`, the cleanup changelogs and every path in a
    `board_activity` are relative to, so it is the project root rather than whichever subdirectory
    the pane happens to be open in.
    """
    root = Path(root)
    return B.Board(root, root.parent)


def board_for(workspace: str | os.PathLike | None,
              explicit_dir: str | os.PathLike | None = None,
              folder: str | None = None) -> B.Board | None:
    """`find_board_root`, as a `Board`.  None when no board exists for this workspace."""
    root = find_board_root(workspace, explicit_dir, folder)
    return None if root is None else board_at(root)


class BoardInit:
    """"Initialize a project and create a Switchboard here?" — the one round trip (protocol 19.12).

    One instance per worker, shared by the owner's tools and the agent's, so a yes or a no is the
    pane's and not one instance's.  The shape is `terminal_handoff.TerminalHandoff`'s, for the same
    reason: the request goes out as an event, the GUI answers on the protocol thread, and whoever
    is waiting is woken.  Two callers use it differently and both are here so the difference is
    visible:

    * the **agent's** tool call runs on the turn thread, so `ask_and_wait` blocks it until the
      answer arrives.  There is no timeout — the pane owns the dialog and always answers it — and
      Stop raises `Cancelled` out of the tool, which is how every other blocking tool ends;
    * the **owner's** `board_create` arrives on the protocol thread, which is the very thread that
      would have to read the answer, so it cannot block.  `ask` parks a callback instead and
      `board_protocol` replays the write when the answer comes.
    """

    def __init__(self, emit: Callable[[dict], None], cancel: threading.Event | None = None):
        self.emit = emit
        #: The agent's `cancel_event`, set by the worker once there is an agent.  Stop while the
        #: dialog is open must end the turn, not leave a thread parked on it.
        self.cancel = cancel
        #: The user said no: nothing asks again until the board is re-pointed or initialized.
        self.declined = False
        self._lock = threading.Lock()
        self._pending: dict[str, list] = {}
        self._next = 0

    def ask(self, *, project: str, directory: str | os.PathLike, reason: str,
            title: str = "", request_id=None, callback: Callable[[bool], None] | None = None) -> str:
        """Emit `board_init_request` and return its id.  `callback(accepted)` runs when answered."""
        if reason not in INIT_REASONS:                   # pragma: no cover - callers pass a constant
            reason = "agent-card"
        with self._lock:
            self._next += 1
            init_id = f"bi-{self._next}"
            self._pending[init_id] = [threading.Event(), None, callback]
        event = {"event": "board_init_request", "id": init_id, "root": str(directory),
                 "project": str(project), "dir": str(directory), "reason": reason}
        if title:
            event["title"] = str(title)[:200]
        if request_id is not None:
            event["request_id"] = request_id
        self.emit(event)
        return init_id

    def ask_and_wait(self, *, project: str, directory: str | os.PathLike, reason: str,
                     title: str = "") -> bool:
        """`ask`, then block this (turn) thread until the user answers or stops the turn."""
        init_id = self.ask(project=project, directory=directory, reason=reason, title=title)
        done = self._pending[init_id][0]
        while not done.wait(0.05):
            if self.cancel is not None and self.cancel.is_set():
                self._take(init_id)
                raise Cancelled("Stopped.")
        return bool(self._take(init_id))

    def _take(self, init_id: str) -> bool:
        with self._lock:
            entry = self._pending.pop(init_id, None)
        return bool(entry and entry[1])

    def answer(self, reply: dict) -> dict:
        """`board_init_answer {id, accept}` from the GUI, on the protocol thread."""
        if not isinstance(reply, dict):                  # pragma: no cover - dispatch checks first
            raise ValueError("board_init_answer must be an object.")
        init_id = reply.get("id")
        accept = reply.get("accept")
        if type(accept) is not bool:
            raise ValueError("board_init_answer needs accept: true or false.")
        with self._lock:
            entry = self._pending.get(init_id) if isinstance(init_id, str) else None
            if entry is None:
                # The turn was stopped, or the answer is late: nothing is waiting for it. The no
                # is still remembered, so a second card does not reopen the dialog.
                if not accept:
                    self.declined = True
                return {"id": init_id, "accept": accept, "pending": False}
            entry[1] = accept
            callback = entry[2]
            if callback is not None:
                self._pending.pop(init_id, None)
            entry[0].set()
        if not accept:
            self.declined = True
        if callback is not None:
            callback(accept)                              # the owner's parked write, replayed
        return {"id": init_id, "accept": accept, "pending": True}

    def fail_pending(self) -> None:
        """Answer everything still waiting with a no (the turn ended, or the pane went away)."""
        with self._lock:
            entries = list(self._pending.values())
            self._pending.clear()
        for entry in entries:
            entry[1] = False
            entry[0].set()


class BoardTools:
    """The `board_*` tools for one pane (or for the Switchboard worker)."""

    def __init__(self, board: B.Board, *, emit: Callable[[dict], None] | None = None,
                 autonomy: str | None = None, limits: dict | None = None,
                 context: ToolContext | None = None, state_path: Path | str | None = None,
                 clock: Callable[[], float] = time.time, enforce_limits: bool = True,
                 duplicate_check: bool = True, state: str = "ready", project: str | None = None,
                 init=None):
        self.board = board
        #: "ready" or "uninitialized" (protocol 19.12).  An uninitialized board is not on disk:
        #: these tools offer `board_create_card` alone, and creating a card asks the user first.
        self.state = state if state in BOARD_STATES else "ready"
        #: The project this board belongs to, carried through from `configure`/`set_board` onto
        #: the events.  It is a label for the GUI: no file is ever looked for under it.
        self.project = str(project) if project else str(board.repo)
        #: The shared `BoardInit` round trip, or None when nothing may be initialized here.
        self.init = init
        #: Called after this board is created, so the worker can re-point and the agent's system
        #: prompt can go from the one-line note to the full policy block.
        self.on_created: Callable[[], None] | None = None
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
        #: mode offers, and the card a Plan turn may write to. None for a pane's own turns. The
        #: page agent's turns set it to a `ChatScope` (19.18), which is why the type is loose.
        self.card_scope: CardScope | ChatScope | None = None
        #: Set while a turn that writes nothing runs (the survey of a fresh board, 19.18): every
        #: write tool refuses, so what the agent offers stays an offer until the owner says yes.
        self.readonly = False

    # ---- events ---------------------------------------------------------------
    def _emit_board(self, event: dict) -> None:
        """Send a board event naming the board it happened on (protocol 19.2).

        One window can have several projects open, so every `board_*` event carries `root` — the
        `issues/` directory it is about — and a GUI routes by it instead of assuming the events it
        receives belong to whatever board it asked about last.
        """
        self.emit({"root": str(self.board.root), **event})

    # ---- initialization, with the user's consent (protocol 19.12) --------------
    def exists(self) -> bool:
        """Whether this board is on disk yet.  An `uninitialized` one is not."""
        return self.board.config_path.is_file()

    def create_board(self) -> list[str]:
        """Scaffold this board and announce it.  The caller has the user's yes.

        Nothing else writes `board.yaml`: the owner's rule of 2026-09-18 is that a project gets a
        Switchboard only when the user answers "Initialize a project and create a Switchboard
        here?", so every path to this method runs through `board_init` (the GUI asked) or a
        `board_init_request` the user accepted.
        """
        files = B.scaffold(self.board)
        self.state = "ready"
        self._emit_board({"event": "board_created", "workspace": str(self.board.repo),
                          "project": self.project, "files": files})
        if self.on_created is not None:
            self.on_created()
        return files

    def ensure_board(self, *, title: str = "", reason: str = "agent-card") -> bool:
        """Before a write: make sure there is a board, asking the user once.  True if it created one.

        The agent's instance runs this on the turn thread, so it blocks on the round trip exactly
        as `terminal_handoff` does — the user's answer arrives on the protocol thread, a Stop
        raises `Cancelled`, and no timeout is needed because the pane always answers a dialog it
        opened.  The owner's instance never reaches here in the uninitialized state: a
        `board_create` message is parked by `board_protocol` instead, because the protocol thread
        is the one that would have to read the answer.
        """
        if self.state != "uninitialized" or self.exists():
            return False
        if self.init is None:                            # pragma: no cover - always wired in the worker
            raise BoardToolError(NO_BOARD_TEXT, code="board_not_initialized")
        if self.init.declined:
            raise BoardToolError(NO_BOARD_TEXT, code="board_not_initialized")
        accepted = self.init.ask_and_wait(project=self.project, directory=self.board.root,
                                          reason=reason, title=title)
        if not accepted:
            raise BoardToolError(NO_BOARD_TEXT, code="board_not_initialized")
        self.create_board()
        return True

    # ---- lifecycle ------------------------------------------------------------
    @classmethod
    def for_workspace(cls, workspace: str | os.PathLike, **kwargs) -> "BoardTools | None":
        """The tools for a workspace, or None when it has no Switchboard or autonomy is off."""
        board = board_for(workspace)
        if board is None:
            return None
        tools = cls(board, **kwargs)
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
        if self.state == "uninitialized":
            # Nothing to read, move or comment on until a board exists; the one tool that can
            # bring one into being is offered, and calling it asks the user (protocol 19.12).
            return [dict(s) for s in TOOL_SPECS
                    if s["function"]["name"] in UNINITIALIZED_TOOLS]
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

    def begin_chat_turn(self, *, readonly: bool = False) -> ChatScope:
        """The page agent's turn starts (protocol 19.18): board tools, merge and split included.

        `readonly` is the survey's opening turn: nothing is written until the owner confirms, and
        that is enforced here rather than asked for in the brief.
        """
        self.card_scope = ChatScope()
        self.readonly = bool(readonly)
        return self.card_scope

    def end_chat_turn(self) -> None:
        self.card_scope = None
        self.readonly = False

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

    def run(self, name: str, args: dict, *, by_owner: bool = False) -> dict:
        """Run one Switchboard tool.  `by_owner` is the GUI acting for the person at the keyboard.

        The cleanup-only tools are fenced off because an *agent* must not restructure the board
        in the middle of an ordinary turn — not because the structure is off limits.  The owner
        editing the section list in the gear is the case the fence was never about, so
        `OWNER_TOOLS` names the ones a direct request may reach, and only when it says so.
        """
        if not self.handles(name):
            raise BoardToolError(f"unknown Switchboard tool {name!r}")
        if not isinstance(args, dict):
            raise BoardToolError("Tool arguments must be an object.")
        try:
            if (name in CLEANUP_TOOL_NAMES and self.cleanup is None
                    and not (by_owner and name in OWNER_TOOLS)
                    and not getattr(self.card_scope, "chat", False)):
                raise BoardToolError(f"{name} is only available during a Switchboard cleanup.",
                                     code="board_refused")
            self._check_card_scope(name, args)
            if self.readonly and name in WRITE_TOOLS:
                raise BoardToolError(
                    "This turn writes nothing by design — the owner has not confirmed anything "
                    "yet. Say what you would do; the write happens once the owner answers.",
                    code="board_readonly_turn")
            if name == "search_files":
                return search_workspace(Path(self.board.repo), dict(args))
            if self.state == "uninitialized" and name not in UNINITIALIZED_TOOLS:
                raise BoardToolError(
                    "This project has no Switchboard yet, so there is nothing to read or change. "
                    "board_create_card is the only board tool here: calling it asks the user "
                    "whether to create one.", code="board_not_initialized")
            if name in WRITE_TOOLS:
                self._check_write_budget(name, args)
                # Last, and only once the budget and the dry run have had their say: a refused
                # call must not open a dialog, and a dry run must not create anything.
                self.ensure_board(title=str(args.get("title") or "")[:200])
            handler = {"board_list": self._list, "board_read": self._read,
                       "board_create_card": self._create, "board_update_card": self._update,
                       "board_move_card": self._move, "board_comment": self._comment,
                       "board_merge_cards": self._merge, "board_split_card": self._split,
                       "board_sections": self._sections,
                       "board_import_items": self._import_items}[name]
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

    def _import_items(self, args: dict) -> dict:
        """`board_import_items`: cards from tracking the project already has (protocol 19.18).

        The same import the page's survey uses (`board_import.propose` + `apply`), so every card
        carries its `source` key and nothing is imported twice.  The keys are re-derived from the
        project here rather than trusted from the caller, exactly as `board_import_apply` does.
        """
        from . import board_import as I
        keys = args.get("keys")
        if (not isinstance(keys, list) or not keys
                or not all(isinstance(k, str) and k.strip() for k in keys)):
            raise BoardToolError("board_import_items needs `keys`: the source keys to import.")
        if len(keys) > 1000:
            raise BoardToolError("board_import_items takes at most 1000 keys.")
        tab = args.get("tab")
        if tab is not None and not (isinstance(tab, str) and tab.strip()):
            raise BoardToolError("board_import_items tab must be a tab id.")
        wanted = list(dict.fromkeys(k.strip() for k in keys))
        try:
            proposals = I.propose(self.board.repo, board=self.board)
            chosen = [p for p in proposals if p.source_key in set(wanted)]
            created = I.apply(self, chosen, tab=tab.strip() if tab else None, actor="agent",
                              emit=self.emit)
        except (I.ImportError_, BoardError, OSError) as exc:
            raise BoardToolError(f"The import could not run: {exc}") from exc
        cards = []
        for card_id in created:
            card = self.board.card_by_id(card_id)
            if card is None:                              # pragma: no cover - deleted mid-import
                continue
            cards.append({"id": card_id, "title": card.title,
                          "source_key": I.source_key_of(card),
                          "path": str(card.path.relative_to(self.board.repo))})
        return {"created": len(cards), "cards": cards,
                "skipped": [k for k in wanted if k not in {c["source_key"] for c in cards}]}

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
        # `board.category_for_tab` is the one copy of this: the sync engine writes cards too and
        # has to agree about which folder a tab means. Only the exception type is ours.
        try:
            return B.category_for_tab(self.board, tab, strict=True)
        except B.BoardError as exc:
            raise BoardToolError(str(exc)) from exc

    def _tab_of(self, card: B.Card) -> str:
        return B.tab_of(self.board, card)

    def _require_manual_section(self, section: str) -> None:
        """Refuse unless `section` names a configured column that collects nothing (#3XZV).

        Parking is for the sections a person fills by hand; a column with statuses of its own
        is reached through `status`, and an unconfigured id would park the card nowhere.
        """
        config = self.board.config()
        columns = [str(c) for c in (config.get("columns") or [])]
        if section not in columns:
            raise BoardToolError(
                f"section {section!r} is not one of this board's sections: "
                f"{', '.join(columns) or '(none configured)'}.")
        if B.column_statuses_of(config, section):
            raise BoardToolError(
                f"the {section} section collects statuses, so a card is moved into it with "
                "`status`; `section` parks a card in a section that collects nothing.")

    def _updated_at(self, card: B.Card) -> str | None:
        """When this card last changed on disk: the card file's mtime, or its thread file's if
        that is later (protocol 19.2 ``updated``, 2026-09-19). A git checkout moves mtimes too —
        this says "recently touched", not "recently written by a person", which is what the
        pane's Recently updated sort wants."""
        newest = None
        paths = [card.path] if card.path else []
        if card.id:
            paths.append(self.board.thread_path(card.id, card.private))
        for path in paths:
            try:
                mtime = path.stat().st_mtime
            except OSError:
                continue
            if newest is None or mtime > newest:
                newest = mtime
        if newest is None:
            return None
        return datetime.fromtimestamp(newest, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    def _row(self, card: B.Card, thread_counts: dict[str, int]) -> dict:
        # The full row of protocol 19.2. `created`, the task counts and `milestone` were promised
        # there but never sent, so the pane's age and `☑ done/total` badges had nothing to draw
        # (found while rebuilding the pane as rows, 2026-09-18).
        tasks = card.tasks()
        return {"id": card.id, "title": card.title, "type": card.type, "status": card.status,
                "section": card.front.get("section"),
                "tab": self._tab_of(card), "labels": list(card.front.get("labels") or []),
                "assignee": card.front.get("assignee"), "waiting_on": card.front.get("waiting_on"),
                "rank": card.rank, "private": card.private, "priority": card.priority,
                "path": str(card.path.relative_to(self.board.repo)) if card.path else None,
                "thread_entries": thread_counts.get(card.id or "", 0),
                "created": str(card.front.get("created") or ""),
                "updated": self._updated_at(card),
                "milestone": card.front.get("milestone"),
                "topic": card.front.get("topic"),
                "implemented_by": card.front.get("implemented_by"),
                # Who closed it out of the QA lane (#T71W). The row stays light on purpose: the
                # `qa` recommendation is computed per card in `board_read`, not for every row.
                "verified_by": card.front.get("verified_by"),
                "tasks_total": len(tasks),
                "tasks_done": sum(1 for task in tasks if task.done)}

    def _threads(self) -> dict[str, list[B.ThreadEntry]]:
        """Every card's thread entries, read once: the count on a row comes from here, and so
        does the searchable text the protocol's own rows carry (19.2 `text`)."""
        out: dict[str, list[B.ThreadEntry]] = {}
        for private in (False, True):
            folder = self.board.threads_dir(private)
            if not folder.is_dir():
                continue
            for path in folder.glob("*.md"):
                try:
                    entries = B.parse_thread(path.read_text(encoding="utf-8"))
                except (OSError, UnicodeDecodeError):
                    continue
                if entries:
                    out[path.stem.upper()] = entries
        return out

    def _thread_counts(self) -> dict[str, int]:
        return {card_id: len(entries) for card_id, entries in self._threads().items()}

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
        qa = self._qa_block(card)
        return {**({"qa": qa} if qa else {}),
                "id": card.id, "hash": B.file_hash(card.path), "type": card.type,
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
                           "done": t.done, "depth": t.depth, "card": t.card,
                           "blocked_by": list(t.blocked_by)}
                          for t in card.tasks()],
                "thread_total": len(entries),
                "thread": [{"entry_id": e.entry_id, "author": e.author, "kind": e.kind,
                            "attrs": dict(e.attrs), "text": e.text} for e in tail]}

    def _qa_block(self, card: B.Card) -> dict | None:
        """The `qa` object of protocol 19.15: who should verify this card, and what its commits say.

        Only on a work card that names an implementer — with nothing to be independent *of* there is
        nothing to recommend. Availability is this machine's (`qa_verifiers.availability`, cached a
        minute because it shells out per stored key), never the GUI's, and the commit trailers are
        read from `links.commits` plus `git log --grep '#ID'`. Advisory throughout: a missing git, a
        bad hash or an unreadable keyring answers with fewer rows, never an error.
        """
        if card.type != "work":
            return None
        implementer = str(card.front.get("implemented_by") or "").strip()
        if not implementer:
            return None
        block = QA.recommend_here(implementer)
        block["commits"] = QA.card_commits(self.board.repo, card.id or "",
                                           card.front.get("links"), implementer)
        verified = str(card.front.get("verified_by") or "").strip()
        if verified:
            block["verified_by"] = verified
            block["verifier_family"] = QA.family(verified)
        return block

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
        self._emit_board(activity)
        self._emit_board({"event": "board_changed", "upserts": [card_id] if card_id else [],
                          "removed": [], "write_id": write_id})
        return write_id

    def _thread_size(self, card: B.Card) -> int:
        path = self.board.thread_path(card.id or "", card.private)
        try:
            return path.stat().st_size
        except OSError:
            return 0

    def _append(self, card: B.Card, text: str, kind: str = "event", **attrs) -> B.ThreadEntry:
        return self.board.append_thread(card.id, text, author=self.context.actor, kind=kind,
                                        private=card.private, **self.context.attrs(), **attrs)

    def _create(self, args: dict) -> dict:
        allowed = {"tab", "status", "section", "title", "request", "type", "labels", "source",
                   "related", "not_duplicate_of"}
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
        section = str(args.get("section") or "").strip().lower()
        if section:
            self._require_manual_section(section)

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
        if section:
            card.set("section", section)
        try:
            path = B.write_new_card(self.board, card, category)
        except B.BoardError as exc:
            raise BoardToolError(str(exc)) from exc

        self.creates_this_turn += 1
        self.writes_this_turn += 1
        rel = str(path.relative_to(self.board.repo))
        size = self._thread_size(card)
        self._append(card, f"- ✦ {self.context.actor} created this card in "
                           f"{_column_label(status)} · {rel}", kind="event")
        summary = f"created · {_column_label(status)}"
        write_id = self._record("create", card, summary, None, size)
        return {"id": card.id, "path": rel, "status": status, "section": section or None,
                "tab": self._tab_of(card),
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
                        "user's own provenance, private moves the file, and status, rank and "
                        "section are board_move_card's job.", code="board_refused", field=key)
                if key not in writable:
                    raise BoardToolError(f"{key} is not a field of a {card.type} card; allowed: "
                                         f"{', '.join(sorted(writable))}.", code="board_refused", field=key)
                if key == "priority" and value is not None:
                    # One clamped int (#VKFV): a bad value is refused rather than guessed at,
                    # and 0 means "no flag", which is the key's absence in the file.
                    try:
                        value = B.clamp_priority(value)
                    except B.BoardError as exc:
                        raise BoardToolError(str(exc), code="board_refused", field=key) from exc
                    if value == 0:
                        value = None
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
        allowed = {"id", "status", "section", "tab", "before", "after", "reason", "evidence",
                   "implemented_by"}
        if set(args) - allowed:
            raise BoardToolError(f"board_move_card takes {', '.join(sorted(allowed))}.")
        card_id = normalize_id(args.get("id"))
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        card = self._card(card_id)
        before_bytes = card.path.read_bytes()
        base_hash = B.file_hash(card.path)
        old_status, old_tab = card.status, self._tab_of(card)
        old_section = str(card.front.get("section") or "")
        status = str(args.get("status")).strip().lower() if args.get("status") else old_status
        if status not in B.STATUS_FOLDER[card.type]:
            raise BoardToolError(f"unknown {card.type} status {args.get('status')!r}; use one of "
                                 f"{', '.join(B.STATUS_FOLDER[card.type])}.")
        # `section` parks the card in a manual section — a column that collects nothing — and an
        # empty string takes it out (#3XZV). Its status is left alone either way.
        section_arg = args.get("section")
        if section_arg is not None:
            if not isinstance(section_arg, str):
                raise BoardToolError("section must be a section id, or an empty string to take "
                                     "the card out of one.")
            if section_arg.strip():
                self._require_manual_section(section_arg.strip().lower())
                section_arg = section_arg.strip().lower()
            else:
                section_arg = ""
        new_section = old_section if section_arg is None else section_arg
        tab = str(args.get("tab")).strip().lower() if args.get("tab") else old_tab
        category = (B.PLAN_FOLDER if card.type == "plan" else B.MEMORY_FOLDER if card.type == "memory"
                    else self._category_for_tab(tab))

        evidence = args.get("evidence")
        # Card #T71W: the worker knows its own preset and model, so the signature is stamped, never
        # typed. It wins over the agent's `implemented_by` argument, which stays for the one case
        # the worker cannot know — a guest CLI writing through the bridge.
        mine = self.context.signature()
        stamped = ""
        if mine and (status in ("in-progress", "executing", "needs-verification")
                     or (status in QA_STATUSES and old_status not in QA_STATUSES)):
            stamped = mine
            card.set("implemented_by", mine)
        if status in QA_STATUSES and old_status not in QA_STATUSES:
            if not isinstance(evidence, str) or not evidence.strip():
                raise BoardToolError(
                    "Moving a card into a QA lane needs `evidence`: the path of the evidence folder "
                    "(docs/qa_evidence/<date>-<slug>/) recorded with the change.",
                    code="board_refused", requires="evidence")
            implemented_by = stamped or args.get("implemented_by") or card.front.get("implemented_by")
            if not implemented_by:
                raise BoardToolError(
                    "Moving a card into a QA lane needs `implemented_by`: the model that implemented "
                    "it, so QA can be run by a different one.",
                    code="board_refused", requires="implemented_by")
            card.set("implemented_by", implemented_by)
        verified = ""
        if old_status in QA_STATUSES and status in ("done", "dropped"):
            if not any(h.lower() in ("verdict", "qa verdict", "qa result", "resolution")
                       for h in section_headings(card.body)):
                raise BoardToolError(
                    "A card in a QA lane is closed with a verdict: add a `## Verdict` (or "
                    "`## Resolution`) section to the body first, then move it.",
                    code="board_refused", requires="verdict")
            if QA.is_relay_free(mine):
                raise BoardToolError(
                    "Verifying is not available on Relay Free (owner's decision, 2026-09-19): "
                    "run QA on a provider key, on Codex or on Claude Code, and close it from there.",
                    code="board_refused", requires="independent_model")
            # Any pane may close it once the verdict is there (owner, 2026-09-20, card #76DJ):
            # the gate is the verifier's verdict on the card, not the closer's model family.
            # Who passed it, in the same canonical form as `implemented_by`.
            if status == "done" and mine:
                verified = mine
                card.set("verified_by", mine)

        if args.get("evidence"):
            links = dict(card.front.get("links") or {})
            paths = list(links.get("evidence") or [])
            if evidence not in paths:
                paths.append(evidence)
            links["evidence"] = paths
            card.set("links", links)
        if args.get("implemented_by") and not stamped:
            card.set("implemented_by", args["implemented_by"])
        if section_arg == "":
            card.drop("section")
        elif section_arg is not None:
            card.set("section", section_arg)

        card.set("status", status)
        rank = self._rank_for(card, status, args.get("before"), args.get("after"))
        if rank is not None:
            card.set("rank", rank)

        target = B.card_target_path(self.board, card, category)
        moved_from = card.path if target is not None else None
        if target is not None and target.exists():
            raise BoardToolError(f"a different file already sits at {target.relative_to(self.board.repo)}.")
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        if target is not None:
            B.move_card_file(card, target)

        self.writes_this_turn += 1
        parts = []
        if status != old_status:
            parts.append(f"{_column_label(old_status)} → {_column_label(status)}")
        if new_section != old_section:
            parts.append(f"parked in {_column_label(new_section)}" if new_section
                         else f"out of {_column_label(old_section)}")
        if tab != old_tab:
            parts.append(f"tab {old_tab} → {tab}")
        if rank is not None and not parts:
            parts.append("reordered")
        summary = ", ".join(parts) or "unchanged"
        line = f"- ✦ {self.context.actor} moved this card · {summary} · {reason}"
        if args.get("evidence"):
            line += f" · evidence {evidence}"
        if stamped:
            line += f" · implemented_by {stamped}"
        if verified:
            line += f" · verified_by {verified}"
        self._append(card, line, kind="event")
        write_id = self._record("move", card, summary, before_bytes, size, moved_from)
        return {"id": card.id, "status": status, "section": new_section or None, "tab": tab,
                "rank": card.rank,
                "path": str(card.path.relative_to(self.board.repo)),
                "hash": B.file_hash(card.path), "moved": moved_from is not None,
                "write_id": write_id, "summary": summary}

    def set_priority(self, card_id: str, priority) -> dict:
        """The pane's flag click (protocol 19.3 ``board_priority``, card #VKFV).

        Not a `run()` tool: it is the owner at the keyboard, not an agent turn, so it takes no
        `base_hash` — the whole patch is one clamped integer, like a drag's rank — and it is not
        offered to the agent, which sets the same field through `board_update_card`. Undo, the
        write record and the thread entry are the ordinary ones, so a misclick is Ctrl+Z like
        any other move.
        """
        card_id = normalize_id(card_id)
        priority = B.clamp_priority(priority)   # raises BoardError -> BoardToolError below
        card = self._card(card_id)
        before = card.path.read_bytes()
        base_hash = B.file_hash(card.path)
        old_priority = card.priority
        if priority:
            card.set("priority", priority)
        else:
            card.drop("priority")
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        self.writes_this_turn += 1
        flag = ("-" if priority < 0 else "+" if priority else "") + str(abs(priority))
        line = (f"- ✦ {self.context.actor} flagged this card · priority {flag}"
                if priority else f"- ✦ {self.context.actor} cleared this card's priority flag")
        self._append(card, line, kind="event")
        summary = f"priority {old_priority} → {priority}"
        write_id = self._record("priority", card, summary, before, size)
        return {"id": card.id, "priority": priority, "hash": B.file_hash(card.path),
                "write_id": write_id, "summary": summary}

    def stage_advance(self, card_id: str, event: str) -> dict | None:
        """Move a work card one step along its stage lifecycle (#3XZV).

        This is the board itself acting — the same standing as `set_priority`, not a tool the
        model calls: it takes no `base_hash`, is not offered in `tool_specs`, and never refuses
        loudly. A card that is not in the event's starting statuses (further along, closed, or
        another type) is left exactly where it is and None comes back, so a caller can fire the
        event on every path that meets it. A manual `section:` is never touched: a card parked
        by hand stays parked while its stage moves underneath it.
        """
        step = STAGE_MOVES.get(event)
        if step is None:
            raise BoardToolError(f"unknown stage event {event!r}; use one of {', '.join(sorted(STAGE_MOVES))}.")
        froms, to, why = step
        card = self.board.card_by_id(normalize_id(card_id))
        if card is None or card.type != "work" or card.status not in froms or not card.path:
            return None
        if event == "discussed":
            # Only on the thread's first non-event entry; the event kinds a write itself makes
            # (an event) never count.
            if not any(e.kind != "event" for e in self.board.thread(card.id, card.private)):
                return None
        if event == "plan-written":
            if not any(_heading_matches(h, PLAN_HEADING) for h in section_headings(card.body)):
                return None
        before_bytes = card.path.read_bytes()
        base_hash = B.file_hash(card.path)
        old_status = card.status
        card.set("status", to)
        rank = self._rank_for(card, to, None, None)
        if rank is not None:
            card.set("rank", rank)
        category = self.board.category_of(card.path)
        target = B.card_target_path(self.board, card, category)
        moved_from = card.path if target is not None else None
        if target is not None and target.exists():
            raise BoardToolError(f"a different file already sits at {target.relative_to(self.board.repo)}.")
        size = self._thread_size(card)
        self.board.save(card, base_hash=base_hash)
        if target is not None:
            B.move_card_file(card, target)
        self.writes_this_turn += 1
        summary = f"{_column_label(old_status)} → {_column_label(to)} · {why}"
        self._append(card, f"- ✦ {self.context.actor} moved this card · {summary}", kind="event")
        write_id = self._record("move", card, summary, before_bytes, size, moved_from)
        return {"id": card.id, "status": to, "hash": B.file_hash(card.path), "write_id": write_id,
                "summary": summary}

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
        if set(args) - {"id", "kind", "text", "pane_token"}:
            raise BoardToolError("board_comment takes id, kind, text and pane_token.")
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
        # The pane an Execute hand-off landed in (#HKAP): carried in the entry's attrs so the
        # GUI can draw the entry as a link that reveals that pane. Kept to what the entry
        # marker can hold — short, no whitespace, no '>' closing it early.
        pane_token = str(args.get("pane_token") or "").strip()
        if len(pane_token) > 64 or re.search(r"[\s>]", pane_token):
            raise BoardToolError("pane_token is at most 64 characters, with no whitespace or '>'.")
        size = self._thread_size(card)
        before = card.path.read_bytes()
        entry = self._append(card, text, kind=kind, **({"pane_token": pane_token} if pane_token else {}))
        self.writes_this_turn += 1
        write_id = self._record("comment", card, f"{kind}: {text.splitlines()[0][:120]}", before, size)
        # The stage move the comment makes (#3XZV): the thread's first non-event entry moves an
        # inbox card to discussing. Relay's own move — inside, it is another write like this one.
        self.stage_advance(card.id, "discussed")
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
        """`board_sections`: the board's own columns and category folders (protocol 19.9).

        The four things a person can do to the section list — add, remove, merge, rename — are
        all this one call, because they are all one rewrite of `board.yaml`: `columns` is the
        ordered list (add and remove), `column_statuses` says what each one collects (merge), and
        `column_titles` is the name over an id that does not change (rename).  **No card moves
        and no status changes**, whichever of them you do: a section is a view of the statuses,
        so a card that was in a dropped section comes back in a section of its own rather than
        disappearing (`B.column_statuses_of`, and `Model::sections()` on the GUI side).
        """
        allowed = {"columns", "column_statuses", "column_titles", "tabs", "reason"}
        if set(args) - allowed:
            raise BoardToolError(f"board_sections takes {', '.join(sorted(allowed))}.")
        reason = _one_line(args.get("reason"), "reason", MAX_REASON)
        if all(args.get(k) is None for k in ("columns", "column_statuses", "column_titles", "tabs")):
            raise BoardToolError("board_sections takes columns, column_statuses, column_titles, "
                                 "tabs, or any of them together.")
        config = self.board.config()
        before_bytes = (self.board.config_path.read_bytes()
                        if self.board.config_path.exists() else b"")
        changes: list[str] = []

        # The statuses first: `columns` is checked against them, so a section invented in this
        # same call is a known section by the time the list is read.
        statuses = config.get("column_statuses") if isinstance(config.get("column_statuses"), dict) else {}
        statuses = {str(k): [str(s) for s in v] for k, v in statuses.items() if isinstance(v, list)}
        if args.get("column_statuses") is not None:
            statuses = _column_statuses(args.get("column_statuses"))

        if args.get("columns") is not None:
            columns = _string_list(args.get("columns"), "columns")
            if not columns:
                raise BoardToolError("columns must name at least one section.")
            # An id outside the known list is allowed only when this board says what it collects:
            # that is what makes an invented section possible without making a typo silent. An
            # explicit empty entry is a manual section — one that collects nothing on purpose
            # (#3XZV) — and counts as said just the same.
            unknown = [c for c in columns if c not in B.COLUMN_IDS and c not in statuses]
            if unknown:
                raise BoardToolError(
                    f"unknown section(s) {', '.join(unknown)}: either name one of "
                    f"{', '.join(B.COLUMN_IDS)}, or give it statuses of its own in column_statuses.")
            bad = [c for c in columns if not _SECTION_ID_RE.fullmatch(c)]
            if bad:
                raise BoardToolError(f"section id {bad[0]!r} must be lower-case letters, digits, - and _.")
            if len(set(columns)) != len(columns):
                raise BoardToolError("columns lists the same section twice.")
            if list(config.get("columns") or []) != columns:
                changes.append(f"columns: {', '.join(str(c) for c in config.get('columns') or [])} "
                               f"→ {', '.join(columns)}")
                config["columns"] = columns

        if args.get("column_statuses") is not None:
            listed = [str(c) for c in (config.get("columns") or [])]
            stray = sorted(set(statuses) - set(listed))
            if stray:
                raise BoardToolError(f"column_statuses names {', '.join(stray)}, which is not a "
                                     "section in columns. Add it to columns in the same call.")
            # One status, one section. Two sections collecting it would draw the same card twice,
            # and a drop on either would be a move to whichever the list happened to read first.
            seen: dict[str, str] = {}
            for column in listed:
                for status in B.column_statuses_of({**config, "column_statuses": statuses}, column):
                    if status in seen:
                        raise BoardToolError(f"both {seen[status]} and {column} collect "
                                             f"{status!r}; a status belongs to one section.")
                    seen[status] = column
            if statuses != (config.get("column_statuses") or {}):
                changes.append("column_statuses: " + ("; ".join(
                    f"{c} = {', '.join(statuses[c])}" for c in sorted(statuses)) or "cleared"))
                if statuses:
                    config["column_statuses"] = statuses
                else:
                    config.pop("column_statuses", None)

        if args.get("column_titles") is not None:
            titles = _column_titles(args.get("column_titles"))
            if titles != (config.get("column_titles") or {}):
                changes.append("column_titles: " + ("; ".join(
                    f"{c} → {titles[c]}" for c in sorted(titles)) or "cleared"))
                if titles:
                    config["column_titles"] = titles
                else:
                    config.pop("column_titles", None)

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
        self._emit_board({"event": "board_changed", "upserts": [] if removed else [record.card_id],
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


#: A section id: the same shape as a tab id, because both name a thing in `board.yaml`.
_SECTION_ID_RE = re.compile(r"[a-z0-9][a-z0-9_-]*")
MAX_SECTIONS = 20
MAX_SECTION_TITLE = 40


def _column_statuses(value) -> dict[str, list[str]]:
    """`{section: [status, ...]}`, checked: known statuses, nothing invented.

    Merging two sections is writing one of them with both sets of statuses and dropping the
    other from `columns`, so this is the one place that decides what a section may collect.
    An explicit empty list is a section that collects nothing — one a person fills by hand,
    parking cards in it with `board_move_card {section}` (#3XZV) — and it is kept as written.
    """
    if not isinstance(value, dict):
        raise BoardToolError("column_statuses must be an object of section -> statuses.")
    if len(value) > MAX_SECTIONS:
        raise BoardToolError(f"column_statuses takes at most {MAX_SECTIONS} sections.")
    out: dict[str, list[str]] = {}
    for key, listed in value.items():
        column = _one_line(key, "a column_statuses key", 40).lower()
        if not _SECTION_ID_RE.fullmatch(column):
            raise BoardToolError(f"section id {column!r} must be lower-case letters, digits, - and _.")
        if not isinstance(listed, list) or not all(isinstance(s, str) for s in listed):
            raise BoardToolError(f"column_statuses[{column}] must be an array of statuses.")
        statuses = [s.strip() for s in listed if s.strip()]
        if not listed:
            out[column] = []          # a manual section: it collects nothing on purpose
            continue
        unknown = [s for s in statuses if s not in B.ALL_STATUSES]
        if unknown:
            raise BoardToolError(f"unknown status(es) {', '.join(unknown)} in section {column!r}; "
                                 f"a section collects from: {', '.join(B.ALL_STATUSES)}.")
        if len(set(statuses)) != len(statuses):
            raise BoardToolError(f"section {column!r} lists the same status twice.")
        out[column] = statuses
    return out


def _column_titles(value) -> dict[str, str]:
    """`{section: "Name"}`: what the sections are called, over ids that do not change."""
    if not isinstance(value, dict):
        raise BoardToolError("column_titles must be an object of section -> name.")
    if len(value) > MAX_SECTIONS:
        raise BoardToolError(f"column_titles takes at most {MAX_SECTIONS} sections.")
    out: dict[str, str] = {}
    for key, title in value.items():
        column = _one_line(key, "a column_titles key", 40).lower()
        if not _SECTION_ID_RE.fullmatch(column):
            raise BoardToolError(f"section id {column!r} must be lower-case letters, digits, - and _.")
        if isinstance(title, str) and not title.strip():
            continue          # cleared: the section goes back to the name Relay gives it
        out[column] = _one_line(title, f"the name of section {column!r}", MAX_SECTION_TITLE)
    return out


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
    #: `ready` reads as "ready to ship" on its own (owner, 2026-09-19), so the label the pane
    #: shows is "Ready to start" and a thread line says the same words as the section header.
    #: The status id is unchanged, here and on disk.
    return {"inbox": "Inbox", "discussing": "Discussing", "planning": "Planning",
            "planned": "Planned", "ready": "Ready to start",
            "in-progress": "In progress", "needs-verification": "Needs verification",
            "needs-qa-llm": "Needs QA (LLM)",
            "needs-qa-human": "Needs QA (human)", "needs-review": "Needs review",
            "needs-labels": "Needs labels", "needs-ab": "Needs A/B", "deferred": "Deferred",
            "done": "Done", "dropped": "Dropped", "draft": "Draft", "approved": "Approved",
            "executing": "Executing", "active": "Active", "retired": "Retired"}.get(status, status)


#: The deterministic stage moves (#3XZV): what each stage event does to a work card's status.
#: Relay makes the move at the event itself, not on a model's judgment, and each one writes an
#: event entry to the thread saying why. The statuses a move starts *from* are listed — anywhere
#: else the card is further along (or closed, or not a work card) and the event leaves it be.
STAGE_MOVES = {
    # the thread's first non-event entry: the owner asked, or an agent commented
    "discussed": (("inbox",), "discussing", "the discussion started"),
    # a Plan turn was started on the card
    "plan-started": (("inbox", "discussing", "planning", "planned"), "planning",
                     "a Plan turn started"),
    # a Plan turn finished and left its `## Plan` on the card
    "plan-written": (("inbox", "discussing", "planning"), "planned", "the plan is on the card"),
}


def _task_items(raw, card: B.Card) -> list[B.TaskItem]:
    if not isinstance(raw, list) or len(raw) > 100:
        raise BoardToolError("tasks must be an array of at most 100 items.")
    existing = {i.item_id: i for i in card.tasks() if i.item_id}
    out: list[B.TaskItem] = []
    #: Position in `out` -> what blocks it, for the items that named something. An item that
    #: says nothing about `blocked_by` keeps whatever marker its line already carried.
    blockers: dict[int, list] = {}
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
        if entry.get("blocked_by") is not None:
            refs = entry["blocked_by"]
            if not isinstance(refs, list) or len(refs) > 20:
                raise BoardToolError(f"task {index}: blocked_by must be an array of at most "
                                     "20 references.")
            # A number is the 1-based position of another task in this same array (1-based
            # here, as `task {index}` is in every message of this file; 0-based inside
            # `board.set_task_blockers`). A string is an item id on this card, or `#K7Q2`.
            resolved = []
            for ref in refs:
                if isinstance(ref, bool) or not isinstance(ref, (int, str)):
                    raise BoardToolError(f"task {index}: blocked_by takes the number of another "
                                         "task in this list, an item id, or #CARD.")
                if isinstance(ref, int):
                    if not 1 <= ref <= len(raw):
                        raise BoardToolError(f"task {index}: blocked_by {ref} is not one of the "
                                             f"{len(raw)} tasks you sent.")
                    if ref == index:
                        raise BoardToolError(f"task {index} cannot block itself.")
                    ref -= 1
                resolved.append(ref)
            blockers[len(out)] = resolved
        out.append(item)
    if blockers:
        try:
            B.set_task_blockers(out, blockers)
        except B.BoardError as exc:
            raise BoardToolError(str(exc)) from exc
    return out


def _tracked_by_git(repo: Path, path: Path) -> bool:
    if not B.in_git_checkout(repo):
        # A board in Relay's data directory has no repository around it: nothing there is
        # committed, and running git would answer about an unrelated ancestor checkout.
        return False
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
    if tools.state == "uninitialized":
        return UNINITIALIZED_NOTE
    tabs = ", ".join(t for t in tools._tab_map())
    folder = tools.board.root.name
    header = (f"\n\nSwitchboard: this project has one ({folder}/board.yaml). Tabs: {tabs}. "
              f"Autonomy: {tools.autonomy}"
              + (" — your card writes are proposals the user accepts in the Switchboard pane."
                 if tools.autonomy == "suggest" else "") + "\n")
    return header + text
