# SPDX-License-Identifier: GPL-3.0-or-later
"""Worker protocol handlers for the Switchboard pane (docs/AGENT-SESSIONS-PROTOCOL.md section 17).

The GUI never parses a card: it asks for rows and detail and sends back intents.  Every write
goes through `relay_core.board_tools.BoardTools`, the same code the agent tools use, so the
owner's writes and the agent's writes log the same thread events and share the undo snapshots.
The owner's instance simply runs with the rate limit and the duplicate check turned off.

`board_ask` is the Switchboard agent: the worker it runs in is started by the GUI with
`agent_role: "switchboard"`, so its model is the `switchboard` role (protocol 13), which
defaults to the main agent.  The agent is stateless per card: the first question about a card
seeds the conversation from the card file plus the tail of its thread, and any later edit of
the card reseeds it, so the file stays the memory and a collaborator continues the same thread.
"""
from __future__ import annotations

import os
import secrets
from pathlib import Path

from . import board as B
from .board_tools import BoardTools, BoardToolError, ToolContext, cleanup_brief, normalize_id

TYPES = {"board_open", "board_refresh", "board_card_get", "board_create", "board_update",
         "board_move", "board_comment", "board_undo", "board_ask", "board_check",
         "board_cleanup"}

#: How much of a card the Switchboard agent is seeded with (design 5, "Attach").
SEED_BODY_BYTES = 16384
SEED_THREAD_ENTRIES = 10
MAX_ASK_TEXT = 32768

#: `board_cleanup` (protocol 19.9): the roster the brief is sent with, and how much of the
#: user's own extra instruction is carried.  The roster is one line per card, so even a very
#: large board fits; the agent reads the cards it cares about with `board_read`.
MAX_CLEANUP_ROSTER = 400
MAX_CLEANUP_NOTE = 4000


class BoardCommands:
    """`board_*` protocol messages for one worker."""

    def __init__(self, turns, emit, workspace: str | None = None):
        self.turns = turns
        self.emit = emit
        self.workspace = workspace
        self.tools: BoardTools | None = None
        self.rev = 0
        self._snapshot: dict[str, dict] = {}
        # board_ask state: which card the conversation is seeded from, and the card hash it was
        # seeded at, so an edit to the card reseeds instead of answering from a stale copy.
        self._ask_card: str | None = None
        self._ask_hash: str | None = None
        self._ask_turn: str | None = None
        self._ask_text: list[str] = []
        # board_cleanup state (protocol 19.9): the agent's tools while the run owns them, and
        # the log they write into.  Not None means a cleanup turn is in flight.
        self._cleanup_tools = None
        self._cleanup_log = None
        self._cleanup_id = None

    # ---- wiring ---------------------------------------------------------------
    def configure(self, workspace: str, request: dict | None = None) -> dict | None:
        """Called from `configure`; returns the `board` block for the `configured` event."""
        self.workspace = workspace
        settings = (request or {}).get("board") or {}
        if not isinstance(settings, dict):
            raise ValueError("board must be an object.")
        root = Path(settings.get("dir") or (Path(workspace) / "issues"))
        if not (root / B.BOARD_CONFIG).is_file():
            self.tools = None
            return None
        board = B.Board(root, Path(workspace))
        self.tools = BoardTools(board, emit=self.emit, autonomy=settings.get("autonomy"),
                                limits=settings.get("limits"),
                                context=ToolContext(actor="owner",
                                                    pane=os.environ.get("RELAY_PANE_ID") or None),
                                enforce_limits=False, duplicate_check=False)
        self._snapshot = {}
        return {"dir": str(root), "autonomy": self.tools.autonomy, "limits": dict(self.tools.limits),
                "cards": len(board.card_paths())}

    def agent_tools(self, workspace: str, request: dict | None = None) -> BoardTools | None:
        """The *agent's* instance of the tools for this workspace (guardrails on)."""
        settings = (request or {}).get("board") or {}
        root = Path(settings.get("dir") or (Path(workspace) / "issues"))
        if not (root / B.BOARD_CONFIG).is_file():
            return None
        pane = os.environ.get("RELAY_PANE_ID") or None
        tools = BoardTools(B.Board(root, Path(workspace)), emit=self.emit,
                           autonomy=settings.get("autonomy"), limits=settings.get("limits"),
                           context=ToolContext(actor="agent", pane=pane))
        return None if tools.autonomy == "off" else tools

    def handles(self, kind: str) -> bool:
        return kind in TYPES

    def _need(self) -> BoardTools:
        if self.tools is None:
            raise ValueError("This workspace has no Switchboard (issues/board.yaml is missing).")
        return self.tools

    # ---- rows -----------------------------------------------------------------
    def _rows(self) -> dict[str, dict]:
        tools = self._need()
        counts = tools._thread_counts()
        rows: dict[str, dict] = {}
        for card in tools.board.cards():
            if card.id is None:
                continue
            row = tools._row(card, counts)
            row["created"] = card.front.get("created")
            row["milestone"] = card.front.get("milestone")
            row["component"] = card.front.get("component")
            row["implemented_by"] = card.front.get("implemented_by")
            tasks = card.tasks()
            row["tasks_done"] = sum(1 for t in tasks if t.done)
            row["tasks_total"] = len(tasks)
            rows[card.id] = row
        return rows

    def _config(self) -> dict:
        tools = self._need()
        config = tools.board.config()
        return {"tabs": tools.board.tabs(),
                "columns": [str(c) for c in (config.get("columns") or B.DEFAULT_CONFIG.get("columns") or [])],
                "autonomy": tools.autonomy,
                "statuses": {t: list(B.STATUS_FOLDER[t]) for t in B.CARD_TYPES},
                "column_statuses": COLUMN_STATUSES,
                "labels": sorted({str(l) for row in self._snapshot.values() for l in row.get("labels") or []})}

    def _changed(self, write_id: str | None = None) -> dict:
        """Diff the tree against the last snapshot, so the GUI patches rather than reloads."""
        rows = self._rows()
        upserts = [row for cid, row in rows.items() if self._snapshot.get(cid) != row]
        removed = [cid for cid in self._snapshot if cid not in rows]
        self._snapshot = rows
        self.rev += 1
        event = {"event": "board_changed", "rev": self.rev, "upserts": upserts, "removed": removed,
                 "problems": self._problems()}
        if write_id:
            event["write_id"] = write_id
        return event

    def _problems(self) -> list[dict]:
        try:
            return [{"code": p.code, "path": p.path, "message": p.message, "severity": p.severity}
                    for p in self._need().board.check()][:100]
        except (B.BoardError, OSError) as exc:                  # pragma: no cover - unreadable tree
            return [{"code": "check_failed", "path": "", "message": str(exc), "severity": "error"}]

    # ---- dispatch -------------------------------------------------------------
    def dispatch(self, request: dict) -> bool:
        kind = request.get("type")
        if kind not in TYPES:
            return False
        rid = request.get("id")
        if kind == "board_open":
            tools = self._need()
            self._snapshot = self._rows()
            self.rev += 1
            self.emit({"event": "board", "id": rid, "rev": self.rev,
                       "root": str(tools.board.root), "workspace": str(tools.board.repo),
                       "config": self._config(), "cards": list(self._snapshot.values()),
                       "problems": self._problems()})
        elif kind == "board_refresh":
            self.emit({**self._changed(), "id": rid})
        elif kind == "board_check":
            self.emit({"event": "board_problems", "id": rid, "items": self._problems()})
        elif kind == "board_card_get":
            tools = self._need()
            result = tools.run("board_read", {"id": request.get("card"),
                                              "thread_entries": min(50, int(request.get("thread_entries") or 50))})
            if result.get("error"):
                raise ValueError(result["error"])
            self.emit({"event": "board_card", **result, "card_id": result["id"], "id": rid})
        elif kind in ("board_create", "board_update", "board_move", "board_comment"):
            self._write(kind, request, rid)
        elif kind == "board_undo":
            tools = self._need()
            try:
                result = tools.undo(str(request.get("write_id") or ""))
            except BoardToolError as exc:
                raise ValueError(str(exc)) from exc
            self.emit({"event": "board_undone", **result, "card_id": result["id"], "id": rid})
            self.emit(self._changed())
        elif kind == "board_ask":
            self._ask(request, rid)
        elif kind == "board_cleanup":
            self._cleanup(request, rid)
        return True

    # ---- who may start a turn --------------------------------------------------
    def _busy_error(self, rid, what: str) -> bool:
        """One agent turn at a time in the Switchboard worker.  True when it refused.

        A card's ask and a whole-board cleanup share the one worker and the one conversation,
        so the second of them is refused rather than queued: a cleanup that ran while the user
        was talking to a card would rewrite the card under the conversation.  The GUI shows the
        refusal and offers Stop; `cancel` stops whichever is running.
        """
        busy = bool(getattr(self.turns, "busy", False))
        running = ("a Switchboard cleanup" if self._cleanup_log is not None else
                   f"a question on #{self._ask_card}" if self._ask_card and busy else
                   "an agent turn" if busy else "")
        if not running:
            return False
        self.emit({"event": "error", "id": rid, "code": "board_busy", "agent_busy": True,
                   "cleanup_running": self._cleanup_log is not None,
                   "card_id": self._ask_card if self._cleanup_log is None else None,
                   "text": f"The Switchboard agent is busy with {running}. Stop it first, then "
                           f"start {what}."})
        return True

    def _write(self, kind: str, request: dict, rid) -> None:
        tools = self._need()
        author = str(request.get("author") or "owner")[:64]
        tools.context.actor = author
        try:
            if kind == "board_create":
                text = request.get("text")
                title = request.get("title") or _title_from(text)
                result = tools.run("board_create_card", {
                    "tab": request.get("tab") or "features", "status": request.get("status") or "inbox",
                    "title": title, "request": text if isinstance(text, str) and text.strip() else title,
                    "type": request.get("card_type") or "work",
                    **({"labels": request["labels"]} if request.get("labels") else {}),
                    **({"source": request["source"]} if request.get("source") else {})})
            elif kind == "board_update":
                patch = request.get("patch")
                if not isinstance(patch, dict):
                    raise ValueError("board_update takes a patch object.")
                result = tools.run("board_update_card",
                                   {"id": request.get("card"), "base_hash": request.get("base_hash"), **patch})
            elif kind == "board_move":
                result = tools.run("board_move_card", {
                    "id": request.get("card"), "reason": request.get("reason") or "moved in the Switchboard",
                    **{k: request[k] for k in ("status", "tab", "before", "after", "evidence") if request.get(k)}})
            else:
                result = tools.run("board_comment", {"id": request.get("card"),
                                                     "kind": request.get("kind") or "note",
                                                     "text": request.get("text")})
        finally:
            tools.context.actor = "owner"
        if result.get("error"):
            self.emit({"event": "error", "id": rid, "text": result["error"],
                       "code": result.get("code"), **{k: v for k, v in result.items()
                                                      if k in ("current_hash", "possible_duplicates")}})
            return
        self.emit({"event": "board_written", **result, "card_id": result.get("id"),
                   "id": rid, "kind": kind})
        self.emit(self._changed(result.get("write_id")))

    # ---- the Switchboard agent -------------------------------------------------
    def _ask(self, request: dict, rid) -> None:
        tools = self._need()
        card_id = normalize_id(request.get("card"))
        text = request.get("text")
        if not isinstance(text, str) or not text.strip() or len(text) > MAX_ASK_TEXT:
            raise ValueError(f"board_ask text must be 1-{MAX_ASK_TEXT} characters.")
        # Checked before the question is appended, so a refused ask leaves no trace on the card.
        if self._busy_error(rid, "the question"):
            return
        card = tools.board.card_by_id(card_id)
        if card is None:
            raise ValueError(f"no card #{card_id} on this board.")
        card_hash = B.file_hash(card.path)
        # The owner's message is part of the record before the agent ever sees it.
        entry = tools.board.append_thread(card_id, text, author=str(request.get("author") or "owner"),
                                          kind="comment", private=card.private)
        seeded = self._ask_card == card_id and self._ask_hash == card_hash
        if not seeded:
            self.turns.reset()
            self._ask_card, self._ask_hash = card_id, card_hash
        prompt = text if seeded else seed_block(tools.board, card) + "\n\n" + text
        self._ask_text = []
        self._ask_turn = None
        self.emit({"event": "board_thread_appended", "id": rid, "card_id": card_id,
                   "entry_id": entry.entry_id, "author": "owner", "kind": "comment", "text": text})
        self.turns.submit(prompt, "now", rid, None, None)

    # ---- board_cleanup: one agent turn over the whole board ----------------------
    def _cleanup(self, request: dict, rid) -> None:
        """`board_cleanup`: the agent tidies the whole board in one turn (protocol 19.9).

        The same machinery as `board_ask` — one turn on the Switchboard worker, the ordinary
        turn events, `cancel` to stop it — with three differences: the events carry
        `cleanup: true` and a `run_id` instead of a `card_id`, so the pane draws progress in
        the board's notice area rather than in a card thread; the agent's tools run on the
        cleanup's raised ceilings and gain merge/split/sections; and every write is collected
        into a changelog that the run ends by writing to a dated file and sending as
        `board_cleanup_summary`.
        """
        tools = self._need()
        agent = getattr(self.turns, "agent", None)
        if agent is None:
            raise ValueError("Configure a provider and workspace first.")
        agent_tools = getattr(agent, "board", None)
        if agent_tools is None:
            raise ValueError("The Switchboard agent has no board tools here "
                             "(issues/board.yaml is missing, or its autonomy is off).")
        scope = request.get("scope")
        if scope is not None and (not isinstance(scope, str) or len(scope) > 200):
            raise ValueError("board_cleanup scope must be a string of at most 200 characters.")
        note = request.get("note")
        if note is not None and (not isinstance(note, str) or len(note) > MAX_CLEANUP_NOTE):
            raise ValueError(f"board_cleanup note must be a string of at most {MAX_CLEANUP_NOTE} characters.")
        dry_run = bool(request.get("dry_run"))
        if self._busy_error(rid, "the cleanup"):
            return

        run_id = f"c-{secrets.token_hex(3)}"
        self.turns.reset()                 # a cleanup is its own conversation, not a card's
        self._ask_card = self._ask_hash = self._ask_turn = None
        self._ask_text = []
        log = agent_tools.begin_cleanup(run_id, dry_run=dry_run, scope=scope or None,
                                        note=note or None, limits=request.get("limits"))
        self._cleanup_tools, self._cleanup_log, self._cleanup_id = agent_tools, log, rid
        self.emit({"event": "board_cleanup_started", "id": rid, "run_id": run_id,
                   "dry_run": dry_run, "scope": scope or None, "cards": log.cards_before,
                   "limits": dict(log.limits), "changelog": log.changelog})
        try:
            self.turns.submit(cleanup_prompt(tools, scope or None, note or None, dry_run), "now",
                              rid, None, None)
        except Exception:
            self._finish_cleanup("error")
            raise

    def _finish_cleanup(self, outcome: str) -> None:
        """End the run: write the changelog, send the summary, then the board diff."""
        tools, log, rid = self._cleanup_tools, self._cleanup_log, self._cleanup_id
        self._cleanup_tools = self._cleanup_log = self._cleanup_id = None
        if tools is None or log is None:
            return
        report = "".join(self._ask_text).strip()
        self._ask_text = []
        log = tools.end_cleanup(outcome, report) or log
        if log.changes or log.refusals:
            try:
                log.write(Path(tools.board.repo))
            except OSError as exc:                          # pragma: no cover - unwritable repo
                self.emit({"event": "error", "id": rid, "code": "board_cleanup_changelog",
                           "text": f"The cleanup ran but its changelog could not be written: {exc}"})
        else:
            log.changelog = ""       # a run that changed nothing leaves no file behind
        self.emit({"event": "board_cleanup_summary", "id": rid, **log.summary_event()})
        try:
            self.emit(self._changed())
        except (B.BoardError, OSError):                     # pragma: no cover - unreadable tree
            pass

    def observe(self, event: dict) -> dict:
        """Tag and record the Switchboard agent's turn; called before every emit."""
        if self._cleanup_log is not None:
            return self._observe_cleanup(event)
        if self._ask_card is None:
            return event
        name = event.get("event")
        if name == "turn_started" or (name == "status" and self._ask_turn is None):
            self._ask_turn = event.get("turn_id") or self._ask_turn
        if name in ("delta", "answer") and isinstance(event.get("text"), str):
            self._ask_text.append(event["text"])
        if name in ("delta", "done", "error", "cancelled", "turn_summary", "thinking",
                    "thinking_done", "tool_started", "tool_result", "status"):
            event = {**event, "card_id": self._ask_card}
        if name == "done":
            self._finish_ask(event.get("turn_id"))
        elif name in ("error", "cancelled"):
            self._ask_text = []
        return event

    def _finish_ask(self, turn_id) -> None:
        answer = "".join(self._ask_text).strip()
        self._ask_text = []
        card_id, tools = self._ask_card, self.tools
        if not answer or card_id is None or tools is None:
            return
        card = tools.board.card_by_id(card_id)
        if card is None:                                        # pragma: no cover - deleted mid-turn
            return
        agent = getattr(self.turns, "agent", None)
        tools.board.append_thread(card_id, answer, author="agent", kind="comment",
                                  private=card.private,
                                  model=getattr(getattr(agent, "config", None), "model", None),
                                  turn=f"{getattr(agent, 'session_id', '')}/{turn_id}" if turn_id else None)
        # The card is unchanged, so the seeded conversation stays valid for the next question.
        self.emit({"event": "board_thread_appended", "card_id": card_id, "author": "agent",
                   "kind": "comment", "text": answer, "turn_id": turn_id})

    def _observe_cleanup(self, event: dict) -> dict:
        """Tag a cleanup turn's events so the pane shows them on the board, not on a card."""
        log = self._cleanup_log
        name = event.get("event")
        if name == "turn_started" or (name == "status" and self._ask_turn is None):
            self._ask_turn = event.get("turn_id") or self._ask_turn
        if name in ("delta", "answer") and isinstance(event.get("text"), str):
            self._ask_text.append(event["text"])
        # A cleanup is many steps, and the model says something between most of them. A blank
        # line at each tool call keeps those remarks apart in the report and the changelog,
        # instead of running the whole run's commentary into one paragraph.
        if name == "tool_started" and self._ask_text and not self._ask_text[-1].endswith("\n\n"):
            self._ask_text.append("\n\n")
        if name in ("delta", "answer", "done", "error", "cancelled", "turn_summary", "thinking",
                    "thinking_done", "tool_started", "tool_result", "status", "turn_started"):
            event = {**event, "cleanup": True, "run_id": log.run_id}
        if name in ("done", "error", "cancelled"):
            # `error` before the turn ran (a provider refusal) ends the run just as `done` does;
            # the changelog is written either way, so a half-finished cleanup is still readable.
            self._finish_cleanup({"done": "done"}.get(name, name))
        return event


def cleanup_prompt(tools: BoardTools, scope: str | None = None, note: str | None = None,
                   dry_run: bool = False) -> str:
    """The whole-board cleanup turn's prompt: the brief, the board's shape, and the roster.

    The brief is `relay_core/board_cleanup_brief.md` — text beside `board_policy.md`, not code —
    and the roster is one line per card so even a large board fits in the prompt; the agent reads
    the cards it means to touch with `board_read`.
    """
    config = tools.board.config()
    counts: dict[str, int] = {}
    lines: list[str] = []
    for card in sorted(tools.board.cards(), key=lambda c: (B._status_order(c), c.rank or "zzzz")):
        if card.id is None:
            continue
        counts[card.status] = counts.get(card.status, 0) + 1
        if len(lines) >= MAX_CLEANUP_ROSTER:
            continue
        labels = ",".join(str(l) for l in (card.front.get("labels") or [])) or "-"
        lines.append(f"#{card.id} [{card.status}] {card.title} · labels {labels} · "
                     f"{card.path.relative_to(tools.board.root)}")
    head = ["[Switchboard cleanup]",
            f"Board: {tools.board.root} ({len(lines)} of {sum(counts.values())} cards listed below).",
            "Sections (board.yaml columns): " + ", ".join(str(c) for c in config.get("columns") or []),
            "Category folders (board.yaml tabs): "
            + ", ".join(f"{t.get('id')}={t.get('folder') or t.get('filter')}" for t in tools.board.tabs()),
            "Cards per status: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())),
            ""]
    if dry_run:
        head += ["THIS IS A DRY RUN. Every write tool will refuse with `board_cleanup_dry_run` and "
                 "record what you asked for as a proposal. Call them exactly as you would for a real "
                 "run, then give the user the plan in your reply.", ""]
    if scope:
        head += [f"The user narrowed this run to: {scope}. Leave everything else alone.", ""]
    if note:
        head += ["The user added, verbatim:", note.strip(), ""]
    return "\n".join(head + [cleanup_brief(), "", "--- the board today ---"] + lines
                     + ["--- end of the board ---"])


#: Which statuses each configurable column collects (design 3, "Tabs and columns").
COLUMN_STATUSES = {
    "inbox": ["inbox"], "discussing": ["discussing"], "ready": ["ready"],
    "in-progress": ["in-progress"],
    "waiting": ["needs-review", "needs-labels", "needs-ab"],
    "needs-qa": ["needs-qa-llm", "needs-qa-human"],
    "done": ["done", "dropped"], "deferred": ["deferred"],
    # plan and memory columns
    "draft": ["draft"], "approved": ["approved"], "executing": ["executing"],
    "active": ["active"], "retired": ["retired"],
}


def _title_from(text) -> str:
    """A title for a quick-add: the first line, trimmed.  The text itself stays verbatim."""
    if not isinstance(text, str) or not text.strip():
        raise ValueError("board_create needs text.")
    first = next((line.strip() for line in text.splitlines() if line.strip()), "")
    return (first[:80].rstrip() + "…") if len(first) > 80 else first


def seed_block(board: B.Board, card: B.Card) -> str:
    """The card, as the Switchboard agent sees it at the start of a conversation."""
    entries = sorted(board.thread(card.id, card.private), key=lambda e: e.entry_id)
    tail = entries[-SEED_THREAD_ENTRIES:]
    body = card.body[:SEED_BODY_BYTES]
    lines = [f"[Switchboard card #{card.id} — {card.path.name}]",
             "You are Relay's Switchboard agent. You are talking to the user about this one card. "
             "Answer about the card, use the board_* tools to change it, and keep your reply short. "
             "The card file and its thread are the shared record; this conversation is not.",
             "", "--- card front matter ---", B.dump_front_matter(dict(card.front)).rstrip(),
             "--- card body ---", body.rstrip()]
    if len(card.body) > SEED_BODY_BYTES:
        lines.append(f"[body truncated at {SEED_BODY_BYTES} bytes]")
    if tail:
        lines += ["--- thread (last %d of %d entries) ---" % (len(tail), len(entries))]
        for entry in tail:
            lines.append(f"[{entry.entry_id} {entry.author} {entry.kind}] {entry.text.strip()}")
    lines.append("--- end of card ---")
    return "\n".join(lines)


def card_attachments(workspace: str | os.PathLike, cards, board: B.Board | None = None) -> list[dict]:
    """`ask {cards: [{id}]}`: the labelled blocks a pane agent gets for a `#K7Q2` reference.

    Same shape as `relay_core.attachments.load` returns, so the turn formats them the same way.
    """
    if not cards:
        return []
    if not isinstance(cards, list) or len(cards) > 10:
        raise ValueError("cards must be a list of at most 10 {id} objects.")
    board = board or B.Board(Path(workspace) / "issues", Path(workspace))
    if not board.config_path.is_file():
        raise ValueError("This workspace has no Switchboard (issues/board.yaml is missing).")
    by_id = {c.id: c for c in board.cards() if c.id}
    out = []
    for item in cards:
        if isinstance(item, str):
            item = {"id": item}
        if not isinstance(item, dict) or not isinstance(item.get("id"), str):
            raise ValueError("Each card must be {id}.")
        card_id = normalize_id(item["id"])
        card = by_id.get(card_id)
        if card is None:
            raise ValueError(f"No card #{card_id} on this board.")
        content = seed_block(board, card)
        out.append({"path": str(card.path.relative_to(board.repo)), "kind": "card",
                    "content": content, "bytes": len(content.encode("utf-8")),
                    "truncated": len(card.body) > SEED_BODY_BYTES,
                    "label": f"Switchboard card #{card_id}, referenced by the user as #{card_id}"})
    return out
