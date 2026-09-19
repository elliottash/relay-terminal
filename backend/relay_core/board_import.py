# SPDX-License-Identifier: GPL-3.0-or-later
"""Turning what `project_probe` found into cards — proposed first, written only on a yes.

Two steps, deliberately apart:

* `propose(project, finding_kinds)` reads the project (offline, read-only) and returns a
  `Proposal` per importable item, already mapped onto the card format: a title, the body, a
  status, labels, `## Tasks` items and where it came from.  It proposes nothing that has been
  imported before.
* `apply(board, proposals)` writes the accepted ones **through `BoardTools`**, the same code
  the Switchboard pane writes with, so every card gets an id, a rank, a thread and an undo
  record.  It touches no file the import came from: a `TODO.md` is byte-identical afterwards.

Neither calls a model and neither uses the network.

## Never twice: `source_key` and `switchboard/import-state.json`

Every item carries a `source_key` (`<kind>:<path>#<id>`) that is stable across edits to the
file around it.  After `apply`, each key is recorded in two places:

* the card's own `source` front matter field, as `relay-import: <key> (<where>)`.  `source`
  is a legal field for every card type (`board.COMMON_FIELDS`) and `board_update_card` treats
  it as immutable provenance, so the record cannot be edited away by an agent; and
* `<board>/import-state.json`, a small committed map of key → card id, so a second clone
  and a second session agree about what has been imported without reading every card.

A re-run skips a key found in either, so deleting the state file does not duplicate cards and
deleting a card does not silently re-import it.  To import something again on purpose, remove
its key from `import-state.json` *and* the card that holds it.

No new front matter field is invented: `board.check` rejects a key that is not in
`board.ALLOWED_FIELDS`, which is why the state lives in a file of its own rather than on the
card, and why that file is not a `*.md` in a category folder — `Board.card_paths` would then
read it as a card.
"""
from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Sequence

from . import board as B
from . import board_tools as T
from . import project_probe as P

#: The file inside the board directory that remembers what has been imported.
STATE_FILE = "import-state.json"
STATE_VERSION = 1

#: The marker that makes a card's `source` field machine-readable without making it ugly.
#: The key is JSON-quoted because it carries a path, and a path may hold spaces — a
#: Backlog.md task file is called `task-1 - Its Title.md`, so a bare-word key would be read
#: back as `backlog-md:backlog/tasks/task-1` and the same task would be imported again on
#: every run.  JSON quoting also survives a quote or a backslash in a file name.
SOURCE_PREFIX = "relay-import:"
_SOURCE_QUOTED_RE = re.compile(r'^relay-import:\s*("(?:[^"\\]|\\.)*")')
_SOURCE_BARE_RE = re.compile(r"^relay-import:\s*(\S+)")

#: Nothing bigger goes in at once: an import is a one-time act the user is watching.
MAX_PROPOSALS = 500

#: Two labels per imported card, so a board can be filtered back to what came from where.
IMPORT_LABEL = "imported"

#: Tracker status -> work-card status.  Anything a tracker calls "not started" lands in
#: **Inbox**, because an untriaged item is exactly what Inbox is for; a tracker that says
#: more than that (in progress, done, dropped) is taken at its word.
STATUS_MAP = {
    "open": "inbox", "todo": "inbox", "to do": "inbox", "pending": "inbox", "new": "inbox",
    "backlog": "inbox", "ready": "inbox", "draft": "inbox", "proposed": "inbox",
    "in-progress": "in-progress", "in_progress": "in-progress", "in progress": "in-progress",
    "doing": "in-progress", "started": "in-progress", "active": "in-progress",
    "blocked": "inbox",                      # there is no blocked column; `blocked_by` says it
    "review": "needs-review", "in-review": "needs-review", "needs-review": "needs-review",
    "deferred": "deferred", "on-hold": "deferred", "on hold": "deferred", "paused": "deferred",
    "done": "done", "closed": "done", "complete": "done", "completed": "done", "resolved": "done",
    "dropped": "dropped", "cancelled": "dropped", "canceled": "dropped", "wontfix": "dropped",
    "duplicate": "dropped", "archived": "dropped",
}

#: Priority -> sort key.  Cards are created in this order, and `BoardTools` appends each one
#: to the end of its column, so the column ends up in priority order.  (Ranks are fractional
#: indices, so nothing has to be renumbered afterwards — `board.rank_between`.)
PRIORITY_ORDER = {"critical": 0, "urgent": 0, "highest": 1, "high": 1, "p0": 0, "p1": 1,
                  "medium": 2, "normal": 2, "p2": 2, "default": 2,
                  "low": 3, "p3": 3, "lowest": 4, "trivial": 4, "p4": 4}
DEFAULT_PRIORITY_ORDER = 2

#: Which task-item statuses the board understands (`board.ITEM_STATUSES`).
ITEM_STATUS_MAP = {"open": "open", "in-progress": "in-progress", "blocked": "blocked",
                   "deferred": "deferred", "done": "done", "dropped": "dropped"}


class ImportError_(Exception):
    """An import could not be written.  Named with a trailing underscore: `ImportError`
    is a builtin and shadowing it inside this package would be a trap."""


# ------------------------------------------------------------------------- proposals

@dataclass
class Proposal:
    """One card Relay offers to create, and everything needed to create it."""
    source_key: str
    kind: str
    title: str
    body: str = ""
    status: str = "inbox"
    labels: list[str] = field(default_factory=list)
    tasks: list[dict] = field(default_factory=list)       # {text, status}
    source: dict = field(default_factory=dict)            # {kind, path, group, line|id}
    depends_on: list[str] = field(default_factory=list)   # other proposals' source_keys
    parent_key: str | None = None                         # another proposal's source_key
    #: Front matter the board already knows for a work card (`assignee`, `milestone`),
    #: written in the second pass.  A key outside `board.ALLOWED_FIELDS` is dropped there
    #: rather than making `relay-board.py check` fail on `unknown_field`.
    fields: dict = field(default_factory=dict)
    #: Discussion the source carried, as `(author, text)` — appended to the card's thread.
    comments: list[tuple[str, str]] = field(default_factory=list)
    priority: str | None = None
    order: int = 0

    @property
    def source_line(self) -> str:
        """The single line that goes into the card's `source` front matter field.

        The key already names the tracker, the file and the item, so the line says it once
        and adds only the line number, which the key deliberately does not carry.
        """
        line = f" line {self.source['line']}" if self.source.get("line") else ""
        return f"{SOURCE_PREFIX} {json.dumps(self.source_key, ensure_ascii=False)}{line}"

    def to_dict(self) -> dict:
        out = {"source_key": self.source_key, "kind": self.kind, "title": self.title,
               "status": self.status, "source": dict(self.source), "order": self.order}
        if self.body:
            out["body"] = self.body
        if self.labels:
            out["labels"] = list(self.labels)
        if self.tasks:
            out["tasks"] = [dict(t) for t in self.tasks]
        if self.depends_on:
            out["depends_on"] = list(self.depends_on)
        if self.parent_key:
            out["parent_key"] = self.parent_key
        if self.fields:
            out["fields"] = {k: self.fields[k] for k in sorted(self.fields)}
        if self.comments:
            out["comments"] = [{"author": a, "text": t} for a, t in self.comments]
        if self.priority:
            out["priority"] = self.priority
        return out


def source_key_of(card: B.Card) -> str | None:
    """The import key a card records in its `source` field, or None if it was not imported."""
    text = str(card.front.get("source") or "").strip()
    match = _SOURCE_QUOTED_RE.match(text)
    if match:
        try:
            return json.loads(match.group(1))
        except ValueError:                               # pragma: no cover - the regex guards it
            return None
    # A key somebody unquoted by hand: read it as far as the first space rather than not
    # at all, so a hand-edited card still counts as imported.
    bare = _SOURCE_BARE_RE.match(text)
    return bare.group(1).strip('"') if bare else None


# ----------------------------------------------------------------------- state file

def state_path(board: B.Board) -> Path:
    return Path(board.root) / STATE_FILE


def read_state(board: B.Board) -> dict:
    """`{key: {card, at}}` — what has already been imported into this board."""
    path = state_path(board)
    try:
        if not path.is_file() or path.stat().st_size > 8 * 1024 * 1024:
            return {}
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    imported = data.get("imported") if isinstance(data, dict) else None
    return {str(k): v for k, v in imported.items()} if isinstance(imported, dict) else {}


def write_state(board: B.Board, imported: dict) -> Path:
    """Rewrite the state file, sorted, so two imports produce a reviewable diff."""
    path = state_path(board)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {"version": STATE_VERSION,
               "imported": {k: imported[k] for k in sorted(imported)}}
    B.atomic_write(path, json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n")
    return path


def imported_keys(board: B.Board | None) -> set[str]:
    """Every key this board has already taken in, from the state file *and* the cards.

    Both, so that neither losing the state file nor deleting a card can cause the same item
    to be proposed twice.
    """
    if board is None:
        return set()
    keys = set(read_state(board))
    try:
        cards = board.cards()
    except (OSError, B.BoardError):                       # pragma: no cover - unreadable board
        return keys
    for card in cards:
        key = source_key_of(card)
        if key:
            keys.add(key)
    return keys


# ------------------------------------------------------------------------- proposing

def _status_for(item: P.TrackerItem) -> str:
    return STATUS_MAP.get(str(item.status or "").strip().lower(), "inbox")


def _priority_rank(priority: str | None) -> int:
    return PRIORITY_ORDER.get(str(priority or "").strip().lower(), DEFAULT_PRIORITY_ORDER)


def _labels_for(item: P.TrackerItem) -> list[str]:
    labels = [IMPORT_LABEL, item.kind]
    for label in item.labels:
        clean = " ".join(str(label).split())[:40]
        if clean and clean not in labels:
            labels.append(clean)
    return labels[:12]


def skipped_keys(project: str | os.PathLike, finding_kinds: Sequence[str] | None = None,
                 *, board: B.Board | None = None) -> list[str]:
    """The keys this project holds that are already on the board — what `propose` left out.

    The preview says "23 of 30, 7 already imported", and this is the 7 without the caller
    having to re-derive `imported_keys` and intersect it by hand.
    """
    project = Path(project).expanduser()
    if board is None:
        root = B.board_folder(project)
        board = B.Board(root, project) if root is not None else None
    done = imported_keys(board)
    return sorted({i.source_key for i in P.items_for(project, finding_kinds)} & done)


def propose(project: str | os.PathLike, finding_kinds: Sequence[str] | None = None,
            *, board: B.Board | None = None) -> list[Proposal]:
    """Cards Relay would create from what is already in `project`.  Writes nothing.

    `finding_kinds` limits the run to some of `project_probe.TRACKER_KINDS`; None means all
    of them.  `board` is the board that would receive them — passed when it exists, so keys
    imported before are skipped.  When it is None and the project has a board, it is found.
    """
    project = Path(project).expanduser()
    if board is None:
        root = B.board_folder(project)
        board = B.Board(root, project) if root is not None else None
    done = imported_keys(board)
    items = P.items_for(project, finding_kinds)
    proposals: list[Proposal] = []
    for item in items:
        if item.source_key in done:
            continue
        source = {"kind": item.kind, "path": item.path, "id": item.source_id,
                  "group": item.group or item.path}
        if item.line is not None:
            source["line"] = item.line
        tasks = [{"text": text[:300], "status": ITEM_STATUS_MAP.get(status, "open")}
                 for text, status in item.tasks][:100]
        proposals.append(Proposal(
            source_key=item.source_key, kind=item.kind, title=item.title.strip()[:200] or "(untitled)",
            body=item.body, status=_status_for(item), labels=_labels_for(item), tasks=tasks,
            source=source, depends_on=list(item.depends_on), parent_key=item.parent,
            fields=dict(item.fields), comments=list(item.comments), priority=item.priority,
            order=item.order))
    # Priority first; then, inside one tracker, the order that tracker itself had.  `group`
    # comes before `path` because Backlog.md and Beads number across many files, and sorting
    # on the path would scatter their own ordering alphabetically.
    proposals.sort(key=lambda p: (_priority_rank(p.priority), p.kind, p.source.get("group", ""),
                                  p.order, p.source["path"], p.source_key))
    # `depends_on` and `parent` name source ids inside one tracker's own numbering (its
    # `group`); turn them into the keys of the proposals that are actually in this run, and
    # drop the rest.  A `blocked_by` pointing at a card that was never created is worse than
    # no `blocked_by` at all, and `parent` naming nothing would read as a lost card.
    by_source_id = {(p.kind, p.source.get("group"), p.source["id"]): p.source_key
                    for p in proposals}
    for proposal in proposals:
        group = proposal.source.get("group")
        resolved = []
        for dep in proposal.depends_on:
            key = by_source_id.get((proposal.kind, group, dep))
            if key and key != proposal.source_key:
                resolved.append(key)
        proposal.depends_on = sorted(set(resolved))
        if proposal.parent_key:
            key = by_source_id.get((proposal.kind, group, proposal.parent_key))
            proposal.parent_key = key if key and key != proposal.source_key else None
    return proposals[:MAX_PROPOSALS]


# --------------------------------------------------------------------------- applying

def default_tab(board: B.Board) -> str:
    """The tab imported cards land in: `features` when the board has it, else the first
    folder-backed tab.  A filter tab (`deferred`, `done`) is not a folder and cannot hold a
    new card, which is what `BoardTools._category_for_tab` refuses."""
    tabs = [str(t.get("id")) for t in board.tabs() if t.get("folder") and t.get("id")]
    if "features" in tabs:
        return "features"
    if tabs:
        return tabs[0]
    raise ImportError_("this board's board.yaml has no tab with a folder to put a card in")


def _tools_for(board, *, actor: str, emit=None) -> T.BoardTools:
    """`BoardTools` set up for an import.

    The per-turn and per-hour ceilings and the duplicate check are the agent's guardrails:
    they exist to stop a model filing forty cards nobody asked for.  An import is the user's
    own act — they saw the list and ticked the boxes — so it runs the way the Switchboard
    pane's own writes do, with them off (`BoardTools.__init__`, "the owner's own writes").
    """
    if isinstance(board, T.BoardTools):
        return board
    return T.BoardTools(board, emit=emit, enforce_limits=False, duplicate_check=False,
                        context=T.ToolContext(actor=actor))


def _fail(result: dict, what: str) -> None:
    if isinstance(result, dict) and result.get("error"):
        raise ImportError_(f"{what}: {result['error']}")


def apply(board, proposals: Sequence[Proposal], *, tab: str | None = None,
          actor: str = "import", emit=None, when: datetime | None = None) -> list[str]:
    """Write the accepted proposals as cards.  Returns the new card ids, in creation order.

    Every write goes through `BoardTools`, so each card gets an id, a rank at the end of its
    column, a thread, an undo record and a `board_changed` event — exactly as if the user had
    typed it into the Switchboard pane.  **Nothing the import read is touched**: a `TODO.md`,
    a `tasks.json` and a `backlog/` are byte-identical afterwards, which is the owner's rule
    that converting must never mutate the source (`docs/SWITCHBOARD-DESIGN.md` §7).

    Three passes, because a card cannot be blocked by one that does not exist yet:

    1. create every card, in the order `propose` sorted them, so each column ends up in the
       tracker's own priority order;
    2. one update per card that needs `## Tasks`, `blocked_by`, `parent` or a field;
    3. one thread entry per card saying where it came from, plus any discussion the source
       carried, and then the state file.
    """
    tools = _tools_for(board, actor=actor, emit=emit)
    real = tools.board
    if not tools.exists():
        raise ImportError_(f"{real.root} has no board.yaml: initialize the Switchboard first")
    proposals = list(proposals)[:MAX_PROPOSALS]
    if not proposals:
        return []
    # The tab is checked before anything is written, and before the "nothing left to do"
    # shortcut above would hide the mistake: a run that names a tab this board does not have
    # should say so, not quietly succeed because everything was imported already.
    tab = str(tab) if tab else default_tab(real)
    folders = {str(t.get("id")): t.get("folder") for t in real.tabs() if t.get("id")}
    if not folders.get(tab):
        known = ", ".join(sorted(k for k, v in folders.items() if v)) or "(none)"
        raise ImportError_(f"unknown tab {tab!r} for this board; it has: {known}")
    already = imported_keys(real)
    proposals = [p for p in proposals if p.source_key not in already]
    if not proposals:
        return []

    created: dict[str, dict] = {}                        # source_key -> board_create result
    order: list[str] = []
    for proposal in proposals:
        args = {"tab": tab, "status": proposal.status, "title": proposal.title,
                "request": proposal.body.strip() or proposal.title,
                "labels": proposal.labels[:20], "source": proposal.source_line}
        result = tools.run("board_create_card", args)
        _fail(result, f"creating {proposal.source_key}")
        created[proposal.source_key] = result
        order.append(proposal.source_key)

    for proposal in proposals:
        result = created[proposal.source_key]
        fields: dict = {}
        blockers = [created[key]["id"] for key in proposal.depends_on if key in created]
        if blockers:
            fields["blocked_by"] = sorted(set(blockers))
        if proposal.parent_key and proposal.parent_key in created:
            fields["parent"] = created[proposal.parent_key]["id"]
        for key, value in (proposal.fields or {}).items():
            if key in B.ALLOWED_FIELDS["work"] - T.IMMUTABLE_FIELDS:
                fields[key] = value
        if not fields and not proposal.tasks:
            continue
        args = {"id": result["id"], "base_hash": result["hash"]}
        if fields:
            args["fields"] = fields
        if proposal.tasks:
            args["tasks"] = [{"text": t["text"], "status": t["status"]} for t in proposal.tasks]
        update = tools.run("board_update_card", args)
        _fail(update, f"filling in {proposal.source_key}")
        result["hash"] = update.get("hash", result["hash"])

    imported = read_state(real)
    stamp = (when or datetime.now()).strftime("%Y-%m-%d")
    for proposal in proposals:
        card_id = created[proposal.source_key]["id"]
        where = proposal.source.get("path", "")
        if proposal.source.get("line"):
            where = f"{where} line {proposal.source['line']}"
        elif proposal.source.get("id"):
            where = f"{where}, {proposal.source['id']}"
        note = (f"- ✦ imported from {proposal.kind} · {where}\n"
                f"  The original file is unchanged. Import key `{proposal.source_key}`.")
        _fail(tools.run("board_comment", {"id": card_id, "kind": "note", "text": note}),
              f"recording the source of {proposal.source_key}")
        for author, text in proposal.comments[:20]:
            real.append_thread(card_id, text, author=str(author)[:60] or "import", kind="comment")
        imported[proposal.source_key] = {"card": card_id, "at": stamp, "kind": proposal.kind,
                                         "path": proposal.source.get("path", "")}
    write_state(real, imported)
    return [created[key]["id"] for key in order]
