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
from .board_tools import (BOARD_STATES, CARD_MODES, PLAN_HEADING, BoardInit,
                          BoardTools, BoardToolError, ToolContext, board_at, board_for,
                          card_brief, cleanup_brief, find_board_root, named_board_root,
                          normalize_id)

TYPES = {"board_open", "board_refresh", "board_card_get", "board_create", "board_update",
         "board_move", "board_comment", "board_undo", "board_ask", "board_check",
         "board_cleanup", "board_init", "board_init_answer"}

#: What every message here says when the pane has no board at all (protocol 19.1).  Both folder
#: names, because a project may carry either and neither is wrong.
NO_BOARD_ERROR = ("This project has no Switchboard (no switchboard/board.yaml, and no "
                  "issues/board.yaml).")

#: The owner-side messages that may be the first thing a project's board ever hears.  Only a
#: create can be: the other three name a card, and an uninitialized board has none.
INIT_WRITES = ("board_create",)

#: How much of a card the Switchboard agent is seeded with (design 5, "Attach").
SEED_BODY_BYTES = 16384
SEED_THREAD_ENTRIES = 10
MAX_ASK_TEXT = 32768

#: `board_cleanup` (protocol 19.9): the roster the brief is sent with, and how much of the
#: user's own extra instruction is carried.  The roster is one line per card, so even a very
#: large board fits; the agent reads the cards it cares about with `board_read`.
MAX_CLEANUP_ROSTER = 400
MAX_CLEANUP_NOTE = 4000


def parse_board(block) -> dict:
    """The `board` block of a `configure` or `set_board`, normalized (protocol 19.1, 19.11 and 19.12).

    Every field is optional and every default is what Relay did before any of them existed, so a
    `configure` that sends no `board` at all behaves exactly as it did in 384fac4: walk up from
    the workspace, use the board if there is one, and have none if there is not.

      `attach`   false means this pane has no board whatever else is here (no tools, no policy
                 block, `board_*` messages answer the usual no-board error).  Default true.
      `dir`      the board folder, or the project that holds one.  Wins over `project`.
      `project`  the project this board belongs to; carried onto the events for the GUI to route
                 by and used as `dir` when no `dir` is given.  No file is ever searched under it.
      `state`    "uninitialized" says the GUI is willing to offer creating a board here, so a
                 project with none still attaches (19.12).  Default "ready": no board, no attach.
      `autonomy`, `limits`  as before.
    """
    if block is None:
        block = {}
    if not isinstance(block, dict):
        raise ValueError("board must be an object, or null to detach.")
    attach = block.get("attach", True)
    if type(attach) is not bool:
        raise ValueError("board.attach must be true or false.")
    out = {"raw": dict(block), "attach": attach, "state": "ready",
           "autonomy": block.get("autonomy"), "limits": block.get("limits")}
    for key in ("dir", "project"):
        value = block.get(key)
        if value is not None and not isinstance(value, str):
            raise ValueError(f"board.{key} must be a path.")
        out[key] = value.strip() if isinstance(value, str) and value.strip() else None
    state = block.get("state")
    if state is not None:
        if state not in BOARD_STATES:
            raise ValueError(f"board.state must be one of {', '.join(BOARD_STATES)}.")
        out["state"] = state
    return out


class BoardCommands:
    """`board_*` protocol messages for one worker."""

    def __init__(self, turns, emit, workspace: str | None = None):
        self.turns = turns
        self.emit = emit
        self.workspace = workspace
        self.tools: BoardTools | None = None
        #: The `board` block this worker was last pointed with, normalized (`parse_board`).
        self.settings: dict = parse_board(None)
        #: "Initialize a project and create a Switchboard here?" (protocol 19.12), shared by the
        #: owner's tools and the agent's so one yes or no is the pane's.
        self.init = BoardInit(emit)
        #: A `set_board` that arrived mid-turn: applied when the turn ends, so the running turn
        #: keeps the tool set it started with (`set_agent_role` defers the same way).
        self._pending_board: tuple | None = None
        #: Owner-side writes parked on a `board_init_request`, by its id.
        self._parked: dict[str, tuple] = {}
        #: The `board_init` being served, so its `board_state` carries the request id.
        self._init_rid = None
        self.rev = 0
        self._snapshot: dict[str, dict] = {}
        # board_ask state: which card the conversation is seeded from, and the card hash it was
        # seeded at, so an edit to the card reseeds instead of answering from a stale copy.
        self._ask_card: str | None = None
        self._ask_hash: str | None = None
        self._ask_turn: str | None = None
        self._ask_text: list[str] = []
        self._ask_mode: str = "discuss"      # protocol 19.10: discuss | plan
        # The mode whose brief this card's conversation last carried. A Discuss straight after a
        # Discuss sends the owner's words alone; a change of mode, or a Plan, sends the brief.
        self._brief_mode: str | None = None
        # board_cleanup state (protocol 19.9): the agent's tools while the run owns them, and
        # the log they write into.  Not None means a cleanup turn is in flight.
        self._cleanup_tools = None
        self._cleanup_log = None
        self._cleanup_id = None

    # ---- wiring ---------------------------------------------------------------
    def configure(self, workspace: str | None, request: dict | None = None) -> dict | None:
        """Called from `configure`; returns the `board` block for the `configured` event.

        `workspace` is the workspace the GUI named, already resolved, or None when it named
        none: a worker with no workspace has no board, never the board of the directory the
        process happens to be running in.

        The agent does not exist yet when this runs (the board is set up before the provider is
        resolved, 19.1), so `configure` points the owner's half only; `worker.py` asks for the
        agent's half with `agent_tools` and hands it to the `Agent` it then builds.
        """
        self.workspace = str(workspace) if workspace else None
        return self._point(workspace, parse_board((request or {}).get("board")), agent=False)

    def _point(self, workspace: str | None, settings: dict, *, agent: bool = True) -> dict | None:
        """Point this worker at the board these settings name.  The body `set_board` shares.

        Returns the block that goes on `configured` as `board` and on `board_state` as `board`,
        or None when this pane has no board: `attach: false`, or nothing found and nothing the
        GUI said may be created.
        """
        board, state = self._resolve(workspace, settings)
        self.settings = settings
        self._repoint(None if board is None else board.root)
        if board is None:
            self.tools = None
        else:
            self.tools = self._build(board, state, settings, actor="owner")
        if agent:
            self._attach_agent_tools(workspace, settings)
        return self.state_block()

    def _resolve(self, workspace: str | None, settings: dict) -> tuple[B.Board | None, str | None]:
        """Which board these settings mean, and whether it exists yet.

        `dir` wins over `project` when both are given and disagree; with neither, the worker walks
        up from `workspace` as it has since 384fac4 and **searches nowhere else**.  A directory
        that holds no board is a board only when the GUI said so with `state: "uninitialized"`;
        otherwise this pane has none, which is exactly what every `configure` meant before today.
        """
        if not settings["attach"]:
            return None, None
        named = settings["dir"] or settings["project"]
        root = named_board_root(named) if named else find_board_root(workspace)
        if root is None:
            return None, None
        board = board_at(root)
        if board.config_path.is_file():
            return board, "ready"
        if settings["state"] == "uninitialized":
            return board, "uninitialized"
        return None, None

    def _build(self, board: B.Board, state: str, settings: dict, *, actor: str) -> BoardTools:
        tools = BoardTools(board, emit=self.emit, autonomy=settings.get("autonomy"),
                           limits=settings.get("limits"),
                           context=ToolContext(actor=actor,
                                               pane=os.environ.get("RELAY_PANE_ID") or None),
                           enforce_limits=actor != "owner", duplicate_check=actor != "owner",
                           state=state, project=settings["project"], init=self.init)
        tools.on_created = self._board_became_ready
        return tools

    def state_block(self) -> dict | None:
        """The `board` block: what `configured`, `board_state` and `board_init` all answer with."""
        if self.tools is None:
            return None
        board = self.tools.board
        return {"dir": str(board.root), "root": str(board.root), "workspace": str(board.repo),
                "project": self.tools.project, "folder": board.root.name,
                "state": self.tools.state, "exists": self.tools.exists(),
                "autonomy": self.tools.autonomy, "limits": dict(self.tools.limits),
                "cards": len(board.card_paths())}

    def _repoint(self, root: Path | None) -> None:
        """Forget the board we were on when this worker is pointed at another one.

        A re-pointed worker must not answer for the project it left: the seeded card, the hash
        it was seeded at, the turn's collected text and the mode its last brief was sent in all
        belong to the old board, and diffing the new board against the old board's snapshot
        would report every card of one project as an upsert of the other.  The user's answer to
        "initialize a Switchboard here?" belongs to the old project too, so a new one may ask.
        """
        current = self.tools.board.root if self.tools is not None else None
        if root is not None and root == current:
            return
        self._snapshot = {}
        self._ask_card = self._ask_hash = self._ask_turn = None
        self._ask_text = []
        self._ask_mode = "discuss"
        self._brief_mode = None
        self.init.declined = False
        self.init.fail_pending()
        self._parked.clear()

    def agent_tools(self, workspace: str | None, request: dict | None = None) -> BoardTools | None:
        """The *agent's* instance of the tools for this workspace (guardrails on)."""
        settings = parse_board((request or {}).get("board"))
        board, state = self._resolve(workspace, settings)
        if board is None:
            return None
        tools = self._build(board, state, settings, actor="agent")
        return None if tools.autonomy == "off" else tools

    # ---- the agent's half, swapped without losing the conversation -------------
    def _agent(self):
        return getattr(self.turns, "agent", None)

    def bind_agent(self, agent) -> None:
        """`configure` built a new Agent: the init dialog follows its Stop.

        A user who presses Stop while "Initialize a project and create a Switchboard here?" is
        open must not leave the turn thread parked on it, so the round trip watches the agent's
        own `cancel_event` — the same event every other blocking tool watches.
        """
        cancel = getattr(agent, "cancel_event", None)
        if cancel is not None:
            self.init.cancel = cancel

    def _attach_agent_tools(self, workspace: str | None, settings: dict) -> None:
        """Give the pane's live agent the board these settings name, keeping its conversation.

        `configure` cannot do this — it builds a new `Agent` and with it a new conversation — so
        attaching a project to a tab that is already talking goes through here: the agent object,
        its messages and its session id are untouched; only `agent.board` and the system prompt's
        Switchboard block change.  `Agent.tools()` is read per step, so the tool list follows by
        itself; `refresh_system_prompt` rewrites `messages[0]` in place.
        """
        agent = self._agent()
        if agent is None or not hasattr(agent, "refresh_system_prompt"):
            return
        agent.board = self.agent_tools(workspace, {"board": settings["raw"]})
        cancel = getattr(agent, "cancel_event", None)
        if cancel is not None:
            self.init.cancel = cancel
        agent.refresh_system_prompt()

    def _board_became_ready(self) -> None:
        """A board was created: both halves of the tools and the system prompt catch up at once."""
        agent = self._agent()
        for tools in (self.tools, getattr(agent, "board", None)):
            if tools is not None:
                tools.state = "ready"
        if agent is not None and hasattr(agent, "refresh_system_prompt"):
            agent.refresh_system_prompt()
        self.init.declined = False
        # `board_init` answers the message that asked for it; a board created on the way through a
        # write (either half of 19.12) is announced on its own.
        rid, self._init_rid = self._init_rid, None
        event = {"event": "board_state", "board": self.state_block(), "applies": "now"}
        self._send({**event, "id": rid} if rid is not None else event)

    # ---- set_board -------------------------------------------------------------
    def set_board(self, request: dict) -> None:
        """`set_board`: point this pane at another board (or none) without ending its conversation.

        Mid-turn it is deferred to the end of the turn, exactly as `set_agent_role` defers a model
        switch, so a running turn keeps the tool set it started with.  Both cases answer
        `board_state`; the one that says `applies: "now"` is the one in force.
        """
        if "board" not in request:
            raise ValueError("set_board needs a board object, or null to detach.")
        block = request["board"]
        # `board: null` detaches. It is not the same as a `configure` with no board block at all,
        # which still walks up from the workspace; an explicit null says "this pane has none".
        settings = parse_board(block if block is not None else {"attach": False})
        rid = request.get("id")

        def apply_now():
            self._pending_board = None
            self._point(self.workspace, settings)
            self._send({"event": "board_state", "id": rid, "board": self.state_block(),
                        "applies": "now"})

        def later():
            self._pending_board = (rid, settings)
            board, state = self._resolve(self.workspace, settings)
            self.emit({"event": "board_state", "id": rid, "applies": "turn_end",
                       "board": None if board is None else
                       {"dir": str(board.root), "root": str(board.root),
                        "workspace": str(board.repo), "project": settings["project"] or str(board.repo),
                        "folder": board.root.name, "state": state,
                        "exists": board.config_path.is_file()}})

        decide = getattr(self.turns, "now_or_later", None)
        if decide is None:                               # pragma: no cover - a stub supervisor
            apply_now()
        else:
            decide(apply_now, later)

    def _settle_pending_board(self) -> None:
        """Apply a `set_board` that arrived while a turn was running, now that it has ended."""
        pending, self._pending_board = self._pending_board, None
        if pending is None:
            return
        rid, settings = pending
        self._point(self.workspace, settings)
        self._send({"event": "board_state", "id": rid, "board": self.state_block(),
                    "applies": "now", "at": "turn_end"})

    # ---- initializing a project (protocol 19.12) -------------------------------
    def _init(self, request: dict, rid) -> None:
        """`board_init`: create the board for a project the GUI has already asked the user about."""
        named = request.get("dir") or request.get("project")
        if named is not None and not (isinstance(named, str) and named.strip()):
            raise ValueError("board_init project must be a path.")
        root = named_board_root(named) if named else (self.tools.board.root if self.tools else None)
        if root is None:
            raise ValueError("board_init needs a project to create the Switchboard in.")
        if self.tools is not None and root != self.tools.board.root and getattr(self.turns, "busy", False):
            raise ValueError("Stop the active agent turn before pointing this pane at another board.")
        settings = dict(self.settings)
        if root != (self.tools.board.root if self.tools is not None else None):
            settings = parse_board({**settings["raw"], "dir": str(root), "state": "uninitialized",
                                    **({"project": request["project"]} if request.get("project") else {})})
            self._point(self.workspace, settings)
        tools = self._need()
        if not tools.exists():
            self._init_rid = rid
            try:
                tools.create_board()      # board_created, then the board_state that answers `rid`
            finally:
                self._init_rid = None
            return
        self._send({"event": "board_state", "id": rid, "board": self.state_block(), "applies": "now"})

    def _init_answer(self, request: dict, rid) -> None:
        """`board_init_answer {id, accept}`: the user's yes or no to a `board_init_request`."""
        self.init.answer(request)

    def handles(self, kind: str) -> bool:
        return kind in TYPES

    def _need(self) -> BoardTools:
        if self.tools is None:
            raise ValueError(NO_BOARD_ERROR)
        return self.tools

    # ---- events ----------------------------------------------------------------
    def _tag(self, event: dict) -> dict:
        """Name the board an event is about (protocol 19.2), once, for every board event.

        One window may have several projects open at the same time; `root` is what a GUI routes
        by, so it belongs on every `board_*` event and not only on `board`.
        """
        if self.tools is not None and "root" not in event:
            return {"root": str(self.tools.board.root), **event}
        return event

    def _send(self, event: dict) -> None:
        self.emit(self._tag(event))

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
        event = self._tag({"event": "board_changed", "rev": self.rev, "upserts": upserts,
                           "removed": removed, "problems": self._problems()})
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
            self._send({"event": "board", "id": rid, "rev": self.rev,
                        "root": str(tools.board.root), "workspace": str(tools.board.repo),
                        "project": tools.project, "state": tools.state, "exists": tools.exists(),
                        "config": self._config(), "cards": list(self._snapshot.values()),
                        "problems": self._problems()})
        elif kind == "board_refresh":
            self.emit({**self._changed(), "id": rid})
        elif kind == "board_check":
            self._send({"event": "board_problems", "id": rid, "items": self._problems()})
        elif kind == "board_card_get":
            tools = self._need()
            result = tools.run("board_read", {"id": request.get("card"),
                                              "thread_entries": min(50, int(request.get("thread_entries") or 50))})
            if result.get("error"):
                raise ValueError(result["error"])
            self._send({"event": "board_card", **result, "card_id": result["id"], "id": rid})
        elif kind in ("board_create", "board_update", "board_move", "board_comment"):
            self._write(kind, request, rid)
        elif kind == "board_undo":
            tools = self._need()
            try:
                result = tools.undo(str(request.get("write_id") or ""))
            except BoardToolError as exc:
                raise ValueError(str(exc)) from exc
            self._send({"event": "board_undone", **result, "card_id": result["id"], "id": rid})
            self.emit(self._changed())
        elif kind == "board_ask":
            self._ask(request, rid)
        elif kind == "board_cleanup":
            self._cleanup(request, rid)
        elif kind == "board_init":
            self._init(request, rid)
        elif kind == "board_init_answer":
            self._init_answer(request, rid)
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
        if tools.state == "uninitialized" and not tools.exists():
            self._ask_to_initialize(kind, request, rid)
            return
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
        self._send({"event": "board_written", **result, "card_id": result.get("id"),
                    "id": rid, "kind": kind})
        self.emit(self._changed(result.get("write_id")))

    def _ask_to_initialize(self, kind: str, request: dict, rid) -> None:
        """A write reached a project with no Switchboard: ask the user, and park the write.

        The agent's tool call blocks its turn thread on the same question (`BoardTools.ensure_board`);
        an owner-side message cannot, because it arrives on the protocol thread and that is the
        thread the answer has to come in on.  So the write is parked and replayed on a yes — the
        card the user typed is never lost — and answered with the plain no-board error on a no.
        """
        tools = self._need()
        if kind not in INIT_WRITES:
            # board_update, board_move and board_comment all name a card, and a project with no
            # board has none: there is nothing here that creating a board would let through.
            raise ValueError(NO_BOARD_ERROR)
        if self.init.declined:
            # The user already said no for this project. Answered rather than raised, so the GUI
            # gets the same `board_not_initialized` it gets the first time and can grey the action.
            self.emit({"event": "error", "id": rid, "code": "board_not_initialized",
                       "text": NO_BOARD_ERROR})
            return
        title = request.get("title") or _title_from(request.get("text"))

        def answered(accepted: bool, kind=kind, request=request, rid=rid) -> None:
            self._parked.pop(init_id, None)
            if not accepted:
                self.emit({"event": "error", "id": rid, "code": "board_not_initialized",
                           "text": NO_BOARD_ERROR})
                return
            tools.create_board()
            self._write(kind, request, rid)

        init_id = self.init.ask(project=tools.project, directory=tools.board.root,
                                reason="card-command", title=title, request_id=rid,
                                callback=answered)
        self._parked[init_id] = (kind, request, rid)

    # ---- the Switchboard agent -------------------------------------------------
    def _ask(self, request: dict, rid) -> None:
        tools = self._need()
        card_id = normalize_id(request.get("card"))
        mode = request.get("mode") or "discuss"
        if mode not in CARD_MODES:
            raise ValueError(f"board_ask mode must be one of {', '.join(CARD_MODES)}.")
        text = request.get("text")
        if text is None and mode == "plan":
            text = ""                       # Plan needs no words: the card is the brief
        if not isinstance(text, str) or len(text) > MAX_ASK_TEXT or (mode == "discuss" and not text.strip()):
            raise ValueError(f"board_ask text must be 1-{MAX_ASK_TEXT} characters"
                             + (" (it may be empty for a plan)." if mode == "discuss" else "."))
        text = text.strip()
        # Checked before the question is appended, so a refused ask leaves no trace on the card.
        if self._busy_error(rid, "the plan" if mode == "plan" else "the question"):
            return
        card = tools.board.card_by_id(card_id)
        if card is None:
            raise ValueError(f"no card #{card_id} on this board.")
        agent_tools = getattr(getattr(self.turns, "agent", None), "board", None)
        card_hash = B.file_hash(card.path)
        # The owner's message is part of the record before the agent ever sees it. The mode goes
        # with it, so the thread reads "Plan ·" / "Discuss ·" in Relay and `mode=plan` in the file.
        said = text or "Plan this card."
        entry = tools.board.append_thread(card_id, said, author=str(request.get("author") or "owner"),
                                          kind="comment", private=card.private, mode=mode)
        seeded = self._ask_card == card_id and self._ask_hash == card_hash
        if not seeded:
            self.turns.reset()
            self._ask_card, self._ask_hash = card_id, card_hash
            self._brief_mode = None
        prompt = (text if mode == "discuss" and self._brief_mode == "discuss"
                  else mode_prompt(mode, card_id, text))
        self._brief_mode = mode
        if not seeded:
            prompt = seed_block(tools.board, card) + "\n\n" + prompt
        self._ask_text = []
        self._ask_turn = None
        self._ask_mode = mode
        self._send({"event": "board_thread_appended", "id": rid, "card_id": card_id,
                    "entry_id": entry.entry_id, "author": "owner", "kind": "comment", "text": said,
                    "mode": mode})
        # What the mode may touch is enforced by the agent's tools for the length of the turn,
        # not only asked for in the brief (protocol 19.10).
        if agent_tools is not None:
            agent_tools.begin_card_turn(mode, card_id)
        try:
            self.turns.submit(prompt, "now", rid, None, None)
        except Exception:
            self._end_card_turn()
            raise

    def _end_card_turn(self) -> None:
        agent_tools = getattr(getattr(self.turns, "agent", None), "board", None)
        if agent_tools is not None:
            agent_tools.end_card_turn()

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
                             "(this project has no board.yaml, or its autonomy is off).")
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
        self._send({"event": "board_cleanup_started", "id": rid, "run_id": run_id,
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
        self._send({"event": "board_cleanup_summary", "id": rid, **log.summary_event()})
        try:
            self.emit(self._changed())
        except (B.BoardError, OSError):                     # pragma: no cover - unreadable tree
            pass

    def observe(self, event: dict) -> dict:
        """Tag and record the Switchboard agent's turn; called before every emit."""
        if event.get("event") in ("done", "error", "cancelled") and self._pending_board is not None:
            # A `set_board` that arrived mid-turn lands here, once the turn that kept the old
            # tool set has finished with it.
            self._settle_pending_board()
        if self._cleanup_log is not None:
            return self._observe_cleanup(event)
        if self._ask_card is None:
            return event
        name = event.get("event")
        if name == "turn_started" or (name == "status" and self._ask_turn is None):
            self._ask_turn = event.get("turn_id") or self._ask_turn
        if name in ("delta", "answer") and isinstance(event.get("text"), str):
            self._ask_text.append(event["text"])
        # What the model says before a tool call and after it are two paragraphs, not one run-on
        # line ("…Writing the plan.The plan is on #ZW95", live Plan turn, 2026-09-18).
        if name == "tool_started" and self._ask_text and not self._ask_text[-1].endswith("\n\n"):
            self._ask_text.append("\n\n")
        if name in ("delta", "done", "error", "cancelled", "turn_summary", "thinking",
                    "thinking_done", "tool_started", "tool_result", "status"):
            event = {**event, "card_id": self._ask_card}
        if name in ("delta", "done", "error", "cancelled", "turn_summary", "turn_started"):
            event = {**event, "mode": self._ask_mode}
        if name == "done":
            self._end_card_turn()
            self._finish_ask(event.get("turn_id"))
        elif name in ("error", "cancelled"):
            self._end_card_turn()
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
                                  private=card.private, mode=self._ask_mode,
                                  model=getattr(getattr(agent, "config", None), "model", None),
                                  turn=f"{getattr(agent, 'session_id', '')}/{turn_id}" if turn_id else None)
        # A Discuss that edited the card, or a Plan that wrote its `## Plan`, changed the file:
        # the next question reseeds from it, so the conversation never argues with a stale copy.
        # An answer that changed nothing keeps the seeded conversation.
        self._send({"event": "board_thread_appended", "card_id": card_id, "author": "agent",
                    "kind": "comment", "text": answer, "turn_id": turn_id, "mode": self._ask_mode})

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


def mode_prompt(mode: str, card_id: str, text: str) -> str:
    """One card turn's prompt: the mode's brief, then what the owner typed (protocol 19.10).

    The brief belongs to the turn rather than the seed, because one card's conversation may go
    Discuss, Plan, Discuss: the mode is the turn's, not the conversation's. It is sent when the
    mode changes and on every Plan; a Discuss after a Discuss is the owner's words alone.
    """
    brief = card_brief(mode).replace("{card}", card_id).replace("{plan_heading}", PLAN_HEADING)
    label = "Plan" if mode == "plan" else "Discuss"
    head = f"[{label} · #{card_id}]\n{brief}"
    if mode == "plan":
        return head + ("\n\nThe owner adds, verbatim:\n" + text if text else "")
    return head + "\n\nThe owner says:\n" + text


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
    board = board or board_for(workspace)
    if board is None or not board.config_path.is_file():
        raise ValueError(NO_BOARD_ERROR)
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
