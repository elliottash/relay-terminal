# SPDX-License-Identifier: AGPL-3.0-or-later
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
import threading
from pathlib import Path

from . import board as B
from . import board_chat
from . import board_import as I
from . import board_turns
from . import forge_github as GH
from . import forge_sync as F
from . import logs
from . import project_probe as PP
from . import roles as model_roles
from .board_tools import (BOARD_STATES, CARD_MODES, PLAN_HEADING, BoardInit,
                          BoardTools, BoardToolError, ToolContext, board_at, board_for,
                          card_brief, cleanup_brief, find_board_root, named_board_root,
                          normalize_id)

TYPES = {"board_open", "board_refresh", "board_card_get", "board_create", "board_update",
         "board_move", "board_comment", "board_undo", "board_ask", "board_cancel", "board_check",
         "board_cleanup", "board_init", "board_init_answer", "board_folder", "board_sections",
         # Initializing a project and importing what is already in it (19.13,
         # docs/PROJECT-INIT-AND-IMPORT.md section 8).
         "project_probe", "board_import_propose", "board_import_apply",
         # Two-way sync with GitHub issues (19.14, docs/GITHUB-SYNC.md section 8).
         "forge_sync_plan", "forge_sync_run",
         # The Switchboard page agent (19.18): a conversation about the whole board.
         "board_chat", "board_chat_cancel", "board_chat_queue_remove", "board_chat_queue_move"}

#: What every message here says when the pane has no board at all (protocol 19.1).  Both folder
#: names, because a project may carry either and neither is wrong.
NO_BOARD_ERROR = ("This project has no Switchboard (no switchboard/board.yaml, and no "
                  "issues/board.yaml).")

#: The owner-side messages that may be the first thing a project's board ever hears.  Only a
#: create can be: the other three name a card, and an uninitialized board has none.
INIT_WRITES = ("board_create",)

#: What an import or a sync says when the pane's board has not been created yet (19.13, 19.14).
#: Both write cards, and a card needs a board.yaml; the GUI's path is `board_init` first.
NOT_INITIALIZED_ERROR = ("This project has no Switchboard yet. Create one first "
                         "(board_init), then import or sync into it.")

#: `board_import_apply`: how many keys one message may carry.  The proposals themselves are
#: capped by `board_import.MAX_PROPOSALS`; this is the wire.
MAX_IMPORT_KEYS = 1000

#: The searchable text one row may carry (19.2 `text`): the card's whole body and thread for
#: the pane's full-text filter.  Capped so `board`, which carries every row at once, stays well
#: inside the worker's 8 MiB line buffer even on a board of long cards.
MAX_ROW_TEXT = 64 * 1024

#: The `code` a failed `forge_sync_*` answers with (19.14), so the GUI can offer the right thing:
#: signing in, waiting until `retry_at`, or just saying what happened.
FORGE_ERROR_CODES = {"ForgeAuthError": "forge_auth", "ForgeRateLimited": "forge_rate_limited",
                     "ForgeUnavailable": "forge_unavailable", "ForgePrivacyError": "forge_privacy",
                     "ForgeError": "forge_failed"}

#: How much of a card the Switchboard agent is seeded with (design 5, "Attach").
SEED_BODY_BYTES = 16384
SEED_THREAD_ENTRIES = 10
MAX_ASK_TEXT = 32768

#: `board_cleanup` (protocol 19.9): the roster the brief is sent with, and how much of the
#: user's own extra instruction is carried.  The roster is one line per card, so even a very
#: large board fits; the agent reads the cards it cares about with `board_read`.
MAX_CLEANUP_ROSTER = 400
MAX_CLEANUP_NOTE = 4000


def _turn_phrase(mode: str | None) -> str:
    """What a running card turn is called in a refusal: "a plan", "a question", "a turn"."""
    return {"plan": "a plan", "discuss": "a question"}.get(mode or "", "a turn")


def _cards_phrase(cards: list[str]) -> str:
    """"a turn on #A", "turns on #A and #B", "turns on #A, #B and #C"."""
    ids = [f"#{c}" for c in cards]
    if len(ids) == 1:
        return f"a turn on {ids[0]}"
    return "turns on " + (", ".join(ids[:-1]) + " and " + ids[-1] if ids else "the board")


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
      `folder`   the folder a board this pane *creates* would go in: `.switchboard` (the default,
                 hidden since the owner's decision of 2026-09-19) or `switchboard`, which is what
                 the GUI sends when its "Hidden Switchboard folder" option is off.  It never
                 affects reading: a board is found wherever it already is, in `B.BOARD_FOLDERS`
                 order.
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
    folder = block.get("folder")
    if folder is not None and not isinstance(folder, str):
        raise ValueError("board.folder must be a folder name.")
    folder = folder.strip() if isinstance(folder, str) else ""
    if folder and folder not in B.BOARD_FOLDERS:
        raise ValueError(f"board.folder must be one of {', '.join(B.BOARD_FOLDERS)}.")
    out["folder"] = folder or None
    state = block.get("state")
    if state is not None:
        if state not in BOARD_STATES:
            raise ValueError(f"board.state must be one of {', '.join(BOARD_STATES)}.")
        out["state"] = state
    return out


def git_init_project(project: Path) -> str:
    """`git init` in `project` unless it is already inside a repository. One line about what happened.

    The check is git's own (`rev-parse --show-toplevel`), so a linked worktree or a submodule counts
    as "inside a repository" exactly as it does for git. An existing repository is never re-initialised
    and a parent repository is never touched: the directory simply stays a folder of that checkout.
    Git missing, or refusing, is reported rather than raised — the board is created either way.
    """
    try:
        inside = B._git(project, "rev-parse", "--show-toplevel")
    except (OSError, ValueError) as exc:                       # pragma: no cover - git missing
        return f"git init skipped: {exc}"
    if inside.returncode == 0:
        top = inside.stdout.strip()
        try:
            same = Path(top).resolve() == Path(project).resolve()
        except OSError:
            same = top == str(project)
        return ("already a git repository" if same
                else f"inside the git repository at {top}, which was left alone")
    try:
        done = B._git(project, "init", "-q")
    except (OSError, ValueError) as exc:                       # pragma: no cover - git missing
        return f"git init skipped: {exc}"
    if done.returncode != 0:
        return "git init failed: " + (done.stderr.strip().splitlines() or ["unknown error"])[-1]
    return "git repository initialized"


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
        #: What that `board_init`'s `git_init` did, one line, for the same `board_state`.
        self._init_git = None
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
        #: The id of the `forge_sync_*` request whose thread is running, or None (19.14). One
        #: sync at a time: two runs against one repository would post the same comment twice.
        self._forge_run = None
        #: The live card conversations (19.16, `relay_core.board_turns`): one agent, one
        #: conversation and one `CardScope` per card, so Plan on #A and Discuss on #B run at
        #: the same time. Cards were serialized through the worker's single turn runner until
        #: 2026-09-19; the cleanup below still is, because it rewrites the whole board.
        self.cards = board_turns.CardTurns(
            self.emit, lambda card_id, emit: self._build_card_agent(card_id, emit),
            on_answer=self._card_answer)
        #: The page agent (19.18, `relay_core.board_chat`): one conversation about the whole
        #: board, on the Switchboard's main page.  It can write any card, so it is exclusive
        #: with the card turns and the cleanup (see `_busy_error`), and it queues its own
        #: prompts the way a pane's agent does instead of refusing them.
        self.chat = board_chat.PageAgent(
            self.emit, lambda emit: self._build_page_agent(emit),
            on_turn_end=self._chat_turn_ended)

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
        root = (named_board_root(named, settings["folder"]) if named
                else find_board_root(workspace, None, settings["folder"]))
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
        # The card conversations belong to the board we are leaving, and their agents hold that
        # board's tools: stop them and forget them rather than let them write into it (19.16).
        self.cards.drop()
        self.chat.drop()
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

    def _build_card_agent(self, card_id: str, emit):
        """Build the agent one card's turns run on (19.16): the pane's provider, its own rest.

        The provider config, the skills, the roles chain and the failover switches are the pane
        agent's, so a card turn answers on the model the Switchboard is configured with and
        fails over the way the pane does.  Everything that carries state is this card's own: its
        conversation, its `cancel_event` (so Stop on one card cannot stop another) and — the
        point of the exercise — its own `BoardTools`, which is where `card_scope` lives.  That
        is why enforcing what a Plan may touch needed no change in `board_tools.py`.

        There is no request ledger, no todo tool and no completion check: a card turn is one
        prompt answered into a card thread, not a pane's unit of work.
        """
        from .agent import Agent          # late: agent.py pulls in the whole tool executor
        from .tools import Workspace
        main = self._agent()
        if main is None:
            raise ValueError("Configure a provider and workspace first.")
        tools = self.agent_tools(self.workspace, {"board": self.settings["raw"]})
        if tools is None:
            raise ValueError("The Switchboard agent has no board tools here "
                             "(this project has no board.yaml, or its autonomy is off).")
        workspace = str(tools.board.repo)
        agent = Agent(main.config, workspace, emit,
                      max_steps=main.max_steps, max_tool_calls=main.max_tool_calls,
                      skills=getattr(main.executor, "skills", None),
                      preset_id=main.preset.id if main.preset else None,
                      roles=main.roles, board=tools, effort=getattr(main, "effort", None),
                      track_requests=False, todo_tool=False, completion_check=False,
                      stall_timeout_s=main.stall_timeout_s,
                      first_token_timeout_s=getattr(main, "first_token_timeout_s", 0.0),
                      failover=getattr(main, "failover", True),
                      failover_hosted=getattr(main, "failover_hosted", False))
        # Options › Security (#3KB7): a card turn reads the repository, so the owner's extra
        # readable roots and extra secret patterns are its rules too.
        policy = getattr(main.executor, "policy", None)
        if policy is not None:
            agent.executor.policy = policy
            agent.executor.workspace = Workspace(workspace, policy)
        if getattr(main, "instructions", None) is not None:
            agent.set_instructions(main.instructions)
        return agent, tools

    def _build_page_agent(self, emit):
        """Build the page agent (19.18): this worker's provider — the `switchboard` role, which
        is what the GUI starts a board worker on (`agent_role`) — its own conversation and its
        own `BoardTools`, whose `ChatScope` offers the board tools with merge and split.

        The model the page's picker named, when there is one, resolves through the same role
        table, so a page on the Flash agent is a role choice and not a second provider setup.
        """
        from .agent import Agent          # late: agent.py pulls in the whole tool executor
        from .tools import Workspace
        main = self._agent()
        if main is None:
            raise ValueError("Configure a provider and workspace first.")
        tools = self.agent_tools(self.workspace, {"board": self.settings["raw"]})
        if tools is None:
            raise ValueError("The Switchboard page agent has no board tools here "
                             "(this project has no board.yaml, or its autonomy is off).")
        workspace = str(tools.board.repo)
        config = main.config
        preset_id = main.preset.id if main.preset else None
        effort = getattr(main, "effort", None)
        role = self.chat.model or "switchboard"
        resolved = main.roles.resolve(role) if (main.roles is not None and role != "main") else None
        if resolved is not None:
            config, preset_id = resolved.config, resolved.preset_id
            effort = resolved.effort or effort
        agent = Agent(config, workspace, emit,
                      max_steps=main.max_steps, max_tool_calls=main.max_tool_calls,
                      skills=getattr(main.executor, "skills", None),
                      preset_id=preset_id, roles=main.roles, board=tools, effort=effort,
                      track_requests=False, todo_tool=False, completion_check=False,
                      stall_timeout_s=main.stall_timeout_s,
                      first_token_timeout_s=getattr(main, "first_token_timeout_s", 0.0),
                      failover=getattr(main, "failover", True),
                      failover_hosted=getattr(main, "failover_hosted", False))
        policy = getattr(main.executor, "policy", None)
        if policy is not None:
            agent.executor.policy = policy
            agent.executor.workspace = Workspace(workspace, policy)
        if getattr(main, "instructions", None) is not None:
            agent.set_instructions(main.instructions)
        return agent, tools

    def _chat_turn_ended(self, turn_id: str, survey: bool, outcome: str) -> None:
        """A page-agent turn ended: settle the survey state file (19.18).

        `done`, `cancelled` or `error` all count: the survey ran, and asking again on every open
        of the page would be the thing nobody wanted.  The owner can still import later — the
        proposals are in the conversation and the import tool is one call away.
        """
        if survey and self.tools is not None:
            board_chat.mark_survey(self.tools.board, "done", note=f"turn {outcome}")

    def _card_answer(self, session, turn_id, answer: str) -> None:
        """A card turn finished with something to say: it goes on that card's thread (19.10)."""
        tools = self.tools
        if tools is None:
            return
        card = tools.board.card_by_id(session.card_id)
        if card is None:                                        # deleted while the turn ran
            return
        agent = session.agent
        tools.board.append_thread(session.card_id, answer, author="agent", kind="comment",
                                  private=card.private, mode=session.mode,
                                  model=getattr(getattr(agent, "config", None), "model", None),
                                  turn=f"{getattr(agent, 'session_id', '')}/{turn_id}" if turn_id else None)
        # A Discuss that edited the card, or a Plan that wrote its `## Plan`, changed the file:
        # the next question on it reseeds from that version (the seed hash no longer matches),
        # so the conversation never argues with a stale copy.
        self._send({"event": "board_thread_appended", "card_id": session.card_id, "author": "agent",
                    "kind": "comment", "text": answer, "turn_id": turn_id, "mode": session.mode})

    def _board_became_ready(self) -> None:
        """A board was created: both halves of the tools and the system prompt catch up at once."""
        agent = self._agent()
        # A fresh board is owed the survey (19.18, owner decision 2026-09-19: it is the page
        # agent's opening turn, so the picker path that skips the import offer gets one too).
        # The marker is what makes it fresh; whichever worker created the board writes it and
        # the worker serving the page reads it on `board_open`.
        if self.tools is not None and board_chat.survey_state(self.tools.board) is None:
            board_chat.mark_survey(self.tools.board, "pending", note="created by board_init")
        for tools in (self.tools, getattr(agent, "board", None)):
            if tools is not None:
                tools.state = "ready"
        if agent is not None and hasattr(agent, "refresh_system_prompt"):
            agent.refresh_system_prompt()
        self.init.declined = False
        # `board_init` answers the message that asked for it; a board created on the way through a
        # write (either half of 19.12) is announced on its own.
        rid, self._init_rid = self._init_rid, None
        git, self._init_git = self._init_git, None
        event = {"event": "board_state", "board": self.state_block(), "applies": "now"}
        if git:                                   # what `board_init {git_init: true}` did (19.12)
            event["git"] = git
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
        git_init = request.get("git_init", False)
        if type(git_init) is not bool:
            raise ValueError("board_init git_init must be true or false.")
        root = (named_board_root(named, self.settings.get("folder")) if named
                else (self.tools.board.root if self.tools else None))
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
            # The project picker's "Initialize new project here" (#916B) asks for a repository as
            # well: `git init` when the directory is not inside one already, and nothing at all —
            # not a re-init, not a parent repository's config — when it is. Reported on the
            # `board_state` that answers `rid`, so the pane can say what happened.
            self._init_rid = rid
            self._init_git = git_init_project(tools.board.repo) if git_init else None
            try:
                tools.create_board()      # board_created, then the board_state that answers `rid`
            finally:
                self._init_rid = None
                self._init_git = None
            return
        self._send({"event": "board_state", "id": rid, "board": self.state_block(), "applies": "now"})

    def _init_answer(self, request: dict, rid) -> None:
        """`board_init_answer {id, accept}`: the user's yes or no to a `board_init_request`."""
        self.init.answer(request)

    # ---- hiding and showing the board's folder (protocol 19.17) ----------------
    def _folder(self, request: dict, rid) -> None:
        """`board_folder {hidden}`: rename this board's folder to `.switchboard/` or `switchboard/`.

        The one thing that moves an existing board, and only because the user asked for it: reading
        is tolerant in both directions (`B.BOARD_FOLDERS`) and nothing migrates by itself.  `git mv`
        in a checkout, a plain rename outside one, and a refusal — with the reason, having changed
        nothing — for an `issues/` board, an existing target or uncommitted changes.

        A turn must not be running: the agent holds card paths under the old folder, and the
        Switchboard pane's watcher is on it.  Afterwards the worker is re-pointed at the new root,
        so the tools, the agent's tools and the GUI all move together.
        """
        hidden = request.get("hidden")
        if type(hidden) is not bool:
            raise ValueError("board_folder needs `hidden`: true to hide the folder, false to show it.")
        tools = self._need_ready()
        if getattr(self.turns, "busy", False):
            raise ValueError("Stop the active agent turn before moving this board's folder.")
        try:
            move = B.rename_board_folder(tools.board, hidden)
        except (B.BoardError, OSError) as exc:
            raise ValueError(str(exc)) from exc
        settings = parse_board({**self.settings["raw"], "dir": move.root,
                                "folder": move.new if move.new in B.BOARD_FOLDERS else None})
        self._point(self.workspace, settings)
        self._send({"event": "board_folder_changed", "id": rid, "board": self.state_block(),
                    "old": move.old, "new": move.new, "root": move.root, "hidden": move.hidden,
                    "method": move.method, "files": list(move.files), "summary": move.summary()})

    # ---- the section list, from the gear beside the section checkboxes ---------
    def _sections(self, request: dict, rid) -> None:
        """`board_sections {columns, column_statuses, column_titles}`: rewrite the section list.

        The same tool the Switchboard agent calls (`board_sections`), so the gear, a cleanup run
        and a person editing `board.yaml` by hand all go through one validator and one writer,
        and the edit lands in the write log where `board_undo` can take it back.  No card is
        touched: sections are a view of the statuses, which is why this is a config write and
        not a move.
        """
        tools = self._need_ready()
        args = {k: request[k] for k in ("columns", "column_statuses", "column_titles", "tabs")
                if request.get(k) is not None}
        if not args:
            raise ValueError("board_sections needs columns, column_statuses or column_titles.")
        args["reason"] = str(request.get("reason") or "edited in the Switchboard")[:200]
        tools.context.actor = str(request.get("author") or "owner")[:64]
        try:
            result = tools.run("board_sections", args, by_owner=True)
        finally:
            tools.context.actor = "owner"
        if result.get("error"):
            self.emit({"event": "error", "id": rid, "text": result["error"],
                       "code": result.get("code")})
            return
        # `board_state` carries the config block, so every pane on this board redraws its
        # sections from the file that was just written rather than from what the gear sent.
        self._send({"event": "board_written", **result, "id": rid, "kind": "board_sections",
                    "board": self.state_block()})
        self.emit(self._changed(result.get("write_id")))

    def handles(self, kind: str) -> bool:
        return kind in TYPES

    def _need(self) -> BoardTools:
        if self.tools is None:
            raise ValueError(NO_BOARD_ERROR)
        return self.tools

    def _need_ready(self) -> BoardTools:
        """The tools, for a message that writes cards: an uninitialized board is not one.

        `board_create` may create the board on the way through (19.12) because the user typed
        one card and can be asked about it.  An import of thirty items and a sync with a public
        repository are not that: the GUI initializes the board first and sends this after.
        """
        tools = self._need()
        if tools.state != "ready" or not tools.exists():
            raise ValueError(NOT_INITIALIZED_ERROR)
        return tools

    # ---- initializing a project: the probe and the importer (19.13) -------------
    def _project(self, request: dict, what: str) -> Path:
        """The project a `project_probe`/`board_import_*` names.

        Required, absolute and a directory.  **Never the worker's own cwd**: the process runs in
        whatever directory the GUI happened to start it in, and probing that instead of the
        project the user is looking at would read a tree nobody asked about (and, for an import,
        write cards from it).  A missing, empty or relative `project` is an error, not a default.
        """
        value = request.get("project")
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f"{what} needs `project`: the absolute path of the project "
                             "directory. There is no default.")
        path = Path(value.strip()).expanduser()
        if not path.is_absolute():
            raise ValueError(f"{what} project must be an absolute path; got {value.strip()!r}.")
        if not path.is_dir():
            raise ValueError(f"{what}: {path} is not a directory.")
        return Path(os.path.abspath(path))

    def _kinds(self, request: dict):
        kinds = request.get("kinds")
        if kinds is None:
            return None
        if not isinstance(kinds, list) or not all(isinstance(k, str) for k in kinds):
            raise ValueError("kinds must be a list of tracker kinds, or omitted for all of them.")
        unknown = sorted(set(kinds) - set(PP.TRACKER_KINDS))
        if unknown:
            raise ValueError(f"unknown tracker kind(s): {', '.join(unknown)}; known: "
                             f"{', '.join(PP.TRACKER_KINDS)}.")
        return list(kinds)

    def _board_for_project(self, project: Path) -> B.Board | None:
        """The board this project already has: the pane's own when it is that project's."""
        if self.tools is not None and self.tools.exists() and Path(self.tools.board.repo) == project:
            return self.tools.board
        root = B.board_folder(project)
        return B.Board(root, project) if root is not None else None

    def _project_probe(self, request: dict, rid) -> None:
        """`project_probe`: what is in a project, read-only and offline (19.13).

        Needs no board — it is what the GUI asks *before* there is one — and writes nothing, so
        it is safe to send for a project that already has a board and safe to send twice.
        """
        project = self._project(request, "project_probe")
        result = PP.probe(project, kinds=self._kinds(request))
        event = {"event": "project_probe_result", "id": rid, **result}
        board = self._board_for_project(project)
        if board is not None:
            event["root"] = str(board.root)
        self.emit(event)

    def _import_propose(self, request: dict, rid) -> None:
        """`board_import_propose`: the cards an import would create.  Writes nothing."""
        project = self._project(request, "board_import_propose")
        kinds = self._kinds(request)
        board = self._board_for_project(project)
        try:
            proposals = I.propose(project, kinds, board=board)
            skipped = len(I.skipped_keys(project, kinds, board=board))
        except (I.ImportError_, PP.ProbeError, B.BoardError, OSError) as exc:
            raise ValueError(str(exc)) from exc
        event = {"event": "board_import_proposals", "id": rid, "project": str(project),
                 "proposals": [p.to_dict() for p in proposals], "skipped": skipped}
        if board is not None:
            event["root"] = str(board.root)
        self.emit(event)

    def _import_apply(self, request: dict, rid) -> None:
        """`board_import_apply`: create cards for the keys the user ticked (19.13).

        The keys are re-derived here rather than trusted: the GUI sends back the *keys* of the
        last `board_import_proposals`, and the proposals themselves are read again from the
        project, so nothing a client sent becomes a card body.
        """
        tools = self._need_ready()
        project = (self._project(request, "board_import_apply") if request.get("project")
                   else Path(tools.board.repo))
        if Path(tools.board.repo) != project:
            raise ValueError(f"This pane's Switchboard is {tools.board.root}, which is not in "
                             f"{project}. Point the pane at that project first (set_board).")
        keys = request.get("keys")
        if not isinstance(keys, list) or not keys or not all(isinstance(k, str) for k in keys):
            raise ValueError("board_import_apply needs `keys`: the source keys the user ticked.")
        if len(keys) > MAX_IMPORT_KEYS:
            raise ValueError(f"board_import_apply takes at most {MAX_IMPORT_KEYS} keys.")
        wanted = list(dict.fromkeys(keys))
        if self._forge_busy(rid, "import into this board"):
            return
        if self._busy_error(rid, "the import"):
            return
        tab = request.get("tab")
        if tab is not None and not (isinstance(tab, str) and tab.strip()):
            raise ValueError("board_import_apply tab must be a tab id.")
        tools.context.actor = "import"
        try:
            proposals = I.propose(project, self._kinds(request), board=tools.board)
            chosen = [p for p in proposals if p.source_key in set(wanted)]
            created = I.apply(tools, chosen, tab=tab.strip() if tab else None, actor="import",
                              emit=self.emit)
        except (I.ImportError_, BoardToolError, PP.ProbeError, B.BoardError, OSError) as exc:
            raise ValueError(str(exc)) from exc
        finally:
            tools.context.actor = "owner"
        cards = []
        for card_id in created:
            card = tools.board.card_by_id(card_id)
            if card is None:                              # pragma: no cover - deleted mid-import
                continue
            cards.append({"id": card_id, "source_key": I.source_key_of(card),
                          "path": str(card.path.relative_to(tools.board.repo)),
                          "status": card.status, "tab": B.tab_of(tools.board, card)})
        done = {c["source_key"] for c in cards}
        self._send({"event": "board_imported", "id": rid, "project": str(project),
                    "cards": cards, "skipped": [k for k in wanted if k not in done]})
        self.emit(self._changed())

    # ---- GitHub sync (19.14) ---------------------------------------------------
    def _forge_busy(self, rid, what: str) -> bool:
        """True when a sync is running, and the caller was told.  One writer at a time.

        A sync writes card files from its own thread, so an import (or a second sync) alongside
        it would have two writers on one card.  Both are user actions with a button behind them,
        so the second is refused rather than queued.
        """
        if self._forge_run is None:
            return False
        self.emit({"event": "error", "id": rid, "code": "forge_busy",
                   "text": f"A sync with this board's repository is running. Wait for it to "
                           f"finish, then {what}."})
        return True

    def _forge_sync(self, kind: str, request: dict, rid) -> None:
        """`forge_sync_plan` / `forge_sync_run`: the Switchboard against its repository.

        The network part runs on a thread, like `hosted_quota` (13.9), so the message loop never
        waits on GitHub; **exactly one** terminal event follows either way —
        `forge_sync_planned`, `forge_sync_done`, or `error`. The error text is scrubbed and names
        the exception rather than carrying a traceback, and no credential is ever in it: the
        provider holds the token and never hands it over (`GitHubProvider._safe`).
        """
        tools = self._need_ready()
        if self._forge_busy(rid, "start another sync"):
            return
        if self._busy_error(rid, "the sync"):
            return
        board = tools.board
        try:
            config = F.board_config(board)
        except (F.ForgeError, B.BoardError, OSError) as exc:
            raise ValueError(str(exc)) from exc
        repo = request.get("repo") or config["repo"]
        base_url = request.get("base_url") or config["base_url"]
        for name, value in (("repo", repo), ("base_url", base_url)):
            if value is not None and not isinstance(value, str):
                raise ValueError(f"{name} must be a string.")
        if not repo:
            raise ValueError("This board has no repository: put `github: {repo: owner/name}` in "
                             f"{board.root.name}/board.yaml, or send `repo` with the message.")
        dry_run = kind == "forge_sync_plan"
        confirm_bulk = bool(request.get("confirm_bulk"))
        root = str(board.root)
        self._forge_run = rid

        def progress(row: dict, rid=rid, root=root) -> None:
            self.emit({"event": "forge_sync_progress", "id": rid, "root": root, **row})

        def work() -> None:
            try:
                provider = GH.provider_for(
                    str(repo).strip(),
                    base_url=str(base_url).strip() if base_url else None)
                engine = F.ForgeSync(board, provider, login_map=config["login_map"],
                                     create_cap=config["create_cap"],
                                     default_tab=config["default_tab"],
                                     comment_kinds=config["comment_kinds"],
                                     on_progress=None if dry_run else progress)
                result = (engine.plan() if dry_run else engine.run(confirm_bulk=confirm_bulk)).to_dict()
            except Exception as exc:
                # One terminal event, whatever went wrong: an unreachable forge, a refused
                # token, a board pointed at a second repository, a bug here.  A `ForgeError`
                # says something the user can act on, so it is passed through (scrubbed); any
                # other exception is a bug in this code and says only what it was.
                forge = isinstance(exc, F.ForgeError)
                event = {"event": "error", "id": rid, "root": root,
                         "code": FORGE_ERROR_CODES.get(type(exc).__name__, "forge_sync_failed"),
                         "text": logs.scrub(str(exc))[:2000] if forge and str(exc).strip()
                                 else f"The sync failed ({type(exc).__name__})."}
                retry_at = getattr(exc, "retry_at", None)
                if retry_at:
                    event["retry_at"] = int(retry_at)
                    event["retry_at_text"] = exc.retry_at_text
                if not forge:
                    logs.event(logs.get("board"), "forge_sync_crashed", level_name="error",
                               error=type(exc).__name__, msg=str(exc)[:300])
                self.emit(event)
                return
            finally:
                self._forge_run = None
            name = "forge_sync_planned" if dry_run else "forge_sync_done"
            self.emit({"event": name, "id": rid, "root": root, **result})
            if not dry_run:
                # A sync writes card files outside BoardTools, so the pane is told the same way
                # any other write tells it.
                try:
                    self.emit(self._changed())
                except (B.BoardError, OSError):            # pragma: no cover - unreadable tree
                    pass

        try:
            threading.Thread(target=work, name="relay-forge-sync", daemon=True).start()
        except RuntimeError as exc:
            # The thread never started, so nothing will ever clear the flag: without this, one
            # failure to spawn would answer `forge_busy` to every sync and import from here on.
            self._forge_run = None
            raise ValueError(f"The sync could not be started ({type(exc).__name__}).") from exc

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
    @staticmethod
    def _search_text(card: B.Card, entries: list[B.ThreadEntry]) -> str:
        """The card's whole text as one searchable string: its body, then each thread entry's
        author, kind and words (protocol 19.2 `text`).

        The entries' header metadata (timestamps, turn and pane ids) is left out on purpose:
        `pane=switchboard` sits in every header, so a search for "switchboard" would match
        every card that has a thread."""
        parts = [card.body.strip()]
        parts.extend(f"{entry.author} {entry.kind} {entry.text}".strip() for entry in entries)
        return "\n\n".join(part for part in parts if part)

    def _rows(self) -> dict[str, dict]:
        tools = self._need()
        threads = tools._threads()
        counts = {card_id: len(entries) for card_id, entries in threads.items()}
        rows: dict[str, dict] = {}
        for card in tools.board.cards():
            if card.id is None:
                continue
            row = tools._row(card, counts)
            row["created"] = card.front.get("created")
            row["milestone"] = card.front.get("milestone")
            row["component"] = card.front.get("component")
            row["implemented_by"] = card.front.get("implemented_by")
            # The signatures travel on every row; the `qa` recommendation does not — it is per card
            # and costs a PATH and keyring probe, so it rides on `board_card_get` (19.15) instead.
            row["verified_by"] = card.front.get("verified_by")
            # The whole card, so the pane's filter bar is full-text search (owner, 2026-09-19):
            # body and thread together, capped at MAX_ROW_TEXT.  Only this row carries it —
            # `board_list`'s rows go to an agent's tool result and stay light.
            row["text"] = self._search_text(card, threads.get(card.id, []))[:MAX_ROW_TEXT]
            tasks = card.tasks()
            row["tasks_done"] = sum(1 for t in tasks if t.done)
            row["tasks_total"] = len(tasks)
            rows[card.id] = row
        return rows

    def _config(self) -> dict:
        tools = self._need()
        config = tools.board.config()
        columns = [str(c) for c in (config.get("columns") or B.DEFAULT_CONFIG.get("columns") or [])]
        # This board's own sections, not the defaults: a merged or invented section exists only in
        # `column_statuses:`, and a renamed one only in `column_titles:`, so a GUI sent the bare
        # defaults would draw the section list of a board nobody has.
        return {"tabs": tools.board.tabs(),
                "columns": columns,
                "autonomy": tools.autonomy,
                "statuses": {t: list(B.STATUS_FOLDER[t]) for t in B.CARD_TYPES},
                "column_statuses": {c: B.column_statuses_of(config, c) for c in columns},
                # Every renamed section, not only the configured ones: a section that exists
                # because a card has that status (a plan's Draft, Deferred) is renamed the same way.
                "column_titles": {str(k): str(v) for k, v in (config.get("column_titles") or {}).items()
                                  if isinstance(v, str) and v.strip()},
                "all_statuses": list(B.ALL_STATUSES),
                "labels": sorted({str(l) for row in self._snapshot.values() for l in row.get("labels") or []})}

    def _changed(self, write_id: str | None = None) -> dict:
        """Diff the tree against the last snapshot, so the GUI patches rather than reloads."""
        rows = self._rows()
        upserts = [row for cid, row in rows.items() if self._snapshot.get(cid) != row]
        removed = [cid for cid in self._snapshot if cid not in rows]
        self._snapshot = rows
        self.rev += 1
        # The config travels with every change, not only with the full `board` event: the gear
        # rewrites the section list without touching a card, and a pane that only ever learned
        # the sections at open would keep drawing the old ones until it was reopened.
        event = self._tag({"event": "board_changed", "rev": self.rev, "upserts": upserts,
                           "removed": removed, "problems": self._problems(),
                           "config": self._config()})
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
                        "problems": self._problems(), "chat": self.chat.state()})
            # A board `board_init` just created gets the survey as the page agent's opening turn
            # (19.18): the marker file is what makes it fresh, so an old board is never surveyed.
            self._maybe_survey(tools)
        elif kind == "board_refresh":
            self.emit({**self._changed(), "id": rid})
        elif kind == "board_check":
            section = request.get("section")
            if section is not None and not isinstance(section, str):
                raise ValueError("board_check section must be a column id.")
            items = self._problems()
            if section:
                items = [p for p in items if self._problem_section(p) == section]
            self._send({"event": "board_problems", "id": rid, "items": items,
                        "section": section or None})
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
        elif kind == "board_chat":
            self._chat(request, rid)
        elif kind == "board_chat_cancel":
            stopped = self.chat.stop()
            self._send({"event": "board_chat_cancelled", "id": rid, "stopped": stopped,
                        "chat": self.chat.state()})
        elif kind == "board_chat_queue_remove":
            item = request.get("item")
            if not isinstance(item, str) or not self.chat.remove(item):
                raise ValueError("That prompt is not queued (it may already have started).")
        elif kind == "board_chat_queue_move":
            item, to = request.get("item"), request.get("to")
            if not isinstance(item, str) or not isinstance(to, int) or not self.chat.move(item, to):
                raise ValueError("That prompt is not queued (it may already have started).")
        elif kind == "board_cancel":
            self._cancel_card(request, rid)
        elif kind == "board_cleanup":
            self._cleanup(request, rid)
        elif kind == "board_init":
            self._init(request, rid)
        elif kind == "board_init_answer":
            self._init_answer(request, rid)
        elif kind == "board_folder":
            self._folder(request, rid)
        elif kind == "board_sections":
            self._sections(request, rid)
        elif kind == "project_probe":
            self._project_probe(request, rid)
        elif kind == "board_import_propose":
            self._import_propose(request, rid)
        elif kind == "board_import_apply":
            self._import_apply(request, rid)
        elif kind in ("forge_sync_plan", "forge_sync_run"):
            self._forge_sync(kind, request, rid)
        return True

    # ---- who may start a turn --------------------------------------------------
    def _busy_error(self, rid, what: str, card_id: str | None = None) -> bool:
        """What may start now (19.16).  True when it refused, with `board_busy` sent.

        Turns on **different cards run at the same time**, each on its own agent and
        conversation (`relay_core.board_turns`) — as many as the owner clicks; the concurrent
        cap was removed the day after it landed (owner, 2026-09-19).  Two things are still
        refused rather than queued:

        * a **second turn on the same card** — two agents writing one card's `## Plan` would
          each undo the other, and the thread would interleave two answers;
        * **anything while a cleanup runs**, and a cleanup while anything runs — a cleanup
          merges, splits and moves cards across the whole board, including the ones being
          talked about;

        The GUI shows the refusal and offers Stop; `board_cancel {card}` stops one card's turn.
        """
        cleanup = self._cleanup_log is not None
        running_cards = self.cards.running_cards()
        if cleanup:
            running, busy_card = "a Switchboard cleanup", None
        elif self.chat.busy():
            # The page agent's own prompts never reach here (they queue, 19.18); everything else
            # waits, because it can write any card on the board.
            running, busy_card = "the Switchboard page agent's turn", None
        elif card_id is not None and card_id in running_cards:
            running, busy_card = f"{_turn_phrase(self.cards.mode_of(card_id))} on #{card_id}", card_id
        elif card_id is None and (running_cards or bool(getattr(self.turns, "busy", False))):
            # A cleanup wants the board to itself.
            running = _cards_phrase(running_cards) if running_cards else "an agent turn"
            busy_card = running_cards[0] if running_cards else None
        else:
            return False
        self.emit({"event": "error", "id": rid, "code": "board_busy", "agent_busy": True,
                   "cleanup_running": cleanup, "card_id": busy_card,
                   "cards": running_cards,
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
        if self._busy_error(rid, "the plan" if mode == "plan" else "the question", card_id=card_id):
            return
        card = tools.board.card_by_id(card_id)
        if card is None:
            raise ValueError(f"no card #{card_id} on this board.")
        card_hash = B.file_hash(card.path)
        # The owner's message is part of the record before the agent ever sees it. The mode goes
        # with it, so the thread reads "Plan ·" / "Discuss ·" in Relay and `mode=plan` in the file.
        said = text or "Plan this card."
        entry = tools.board.append_thread(card_id, said, author=str(request.get("author") or "owner"),
                                          kind="comment", private=card.private, mode=mode)
        # This card's own conversation (19.16). It is seeded from the card file the first time
        # and whenever the file has changed since; a second question on an unchanged card
        # continues where it left off, which the single shared conversation could only do for
        # whichever card was asked last.
        session = self.cards.session(card_id)
        seeded = session is not None and session.seed_hash == card_hash
        prompt = (text if mode == "discuss" and seeded and session.brief_mode == "discuss"
                  else mode_prompt(mode, card_id, text))
        if not seeded:
            self.cards.forget(card_id)
            prompt = seed_block(tools.board, card) + "\n\n" + prompt
        # Which card was asked last, for the messages that name one.
        self._ask_card, self._ask_hash, self._ask_mode = card_id, card_hash, mode
        self._send({"event": "board_thread_appended", "id": rid, "card_id": card_id,
                    "entry_id": entry.entry_id, "author": "owner", "kind": "comment", "text": said,
                    "mode": mode})
        # What the mode may touch is enforced for the length of the turn by this card's own
        # tools, not only asked for in the brief (protocol 19.10): `CardTurns.start` opens the
        # scope on them and closes it when the turn's thread unwinds.
        self.cards.start(card_id, mode, prompt, rid, seed_hash=card_hash)

    def _cancel_card(self, request: dict, rid) -> None:
        """`board_cancel {card}`: stop one card's turn (19.16), or every card turn without one.

        The worker-wide `cancel` stops the pane agent's turn — here, a cleanup. A card turn runs
        on its own agent, so stopping it needs to name the card; stopping a plan on #A must not
        stop the one on #B.
        """
        card_id = normalize_id(request.get("card")) if request.get("card") else ""
        stopped = self.cards.stop(card_id) if card_id else bool(self.cards.stop_all())
        self._send({"event": "board_cancelled", "id": rid, "card_id": card_id or None,
                    "stopped": stopped, "cards": self.cards.running_cards()})

    # ---- board_chat: the page agent (protocol 19.18) -----------------------------
    def _chat(self, request: dict, rid) -> None:
        """`board_chat`: a prompt for the page agent — a turn, or a place in the queue.

        The page agent's own prompts never refuse on `board_busy`: the queue is the answer
        (#N8VK's rule, the same as a pane's).  Everything else the board can run still refuses
        while it is turning, because it can write any card.
        """
        self._need()
        text = request.get("text")
        if not isinstance(text, str) or not text.strip() or len(text) > MAX_ASK_TEXT:
            raise ValueError(f"board_chat text must be 1-{MAX_ASK_TEXT} characters.")
        model = request.get("model")
        if model is not None:
            if not isinstance(model, str) or (model and model not in model_roles.SETTABLE):
                raise ValueError("board_chat model must be a role id, or empty for the "
                                 "Switchboard role.")
            self.chat.model = model or None
        if request.get("survey"):
            return self._maybe_survey(self._need(), rid)   # one path: the survey is a chat turn
        if not self.chat.busy() and self._busy_error(rid, "the page agent's prompt"):
            return
        what, ident = self.chat.ask(text.strip(), rid)
        if what == "turn":
            self._send({"event": "board_chat_started", "id": rid, "turn_id": ident,
                        "model": self.chat.model or "switchboard", "chat": self.chat.state()})

    def _problem_section(self, problem: dict) -> str | None:
        """Which section a problem belongs to, for `board_check {section}` (a triage button).

        Problems about the board itself — a thread file with no card, a duplicate id — belong to
        no one section, and a scoped check leaves them out.
        """
        tools = self._need()
        for card in tools.board.cards():
            if card.id is not None and str(card.path.relative_to(tools.board.root)) == problem.get("path"):
                config = tools.board.config()
                for column in self._config()["columns"]:
                    if card.status in B.column_statuses_of(config, column):
                        return column
        return None

    def _maybe_survey(self, tools, rid=None) -> None:
        """The survey (19.18): the page agent's opening turn on a board `board_init` just created.

        `project_probe` and `board_import.propose` do the finding, offline and read-only; the
        agent narrates what they found under a read-only scope, so nothing is written until the
        owner answers.  The board's `survey-state.json` is what makes a board fresh: a board from
        before the survey existed has no file and is never surveyed.
        """
        if tools is None or self.chat.surveyed or self.chat.busy():
            return
        if board_chat.survey_state(tools.board) != "pending":
            return
        board_chat.mark_survey(tools.board, "running")
        repo = tools.board.repo
        try:
            probe = PP.probe(repo)
        except PP.ProbeError as exc:                        # pragma: no cover - unreadable tree
            board_chat.mark_survey(tools.board, "done", note=f"probe failed: {exc}")
            return
        proposals: list[dict] = []
        try:
            proposals = [p.to_dict() for p in I.propose(repo, board=tools.board)]
        except (I.ImportError_, PP.ProbeError, B.BoardError, OSError):
            proposals = []                                  # the survey still runs; nothing to offer
        git = probe.get("git") or {}
        primary_name = git.get("primary")
        row = next((r for r in git.get("remotes") or [] if r.get("name") == primary_name), {})
        self._send({"event": "board_survey", "id": rid, "root": str(tools.board.root),
                    "project": str(repo), "hints": probe.get("hints") or [],
                    "counts": probe.get("counts") or {}, "proposals": proposals[:MAX_IMPORT_KEYS],
                    "git": {"is_repo": bool(git.get("is_repo")), "primary": primary_name,
                            "primary_reason": git.get("primary_reason"), "url": row.get("url"),
                            "forge": row.get("forge"), "owner": row.get("owner"),
                            "repo": row.get("repo")}})
        if self._busy_error(rid, "the survey"):
            board_chat.mark_survey(tools.board, "pending")  # card turns are running; next open
            return
        prompt = board_chat.survey_prompt(tools.board.root, repo, probe, proposals[:MAX_IMPORT_KEYS])
        self.chat.ask("Survey the project: what is already tracked here, and what should become "
                      "cards?", rid, prompt=prompt, readonly=True, survey=True)

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
        # A card turn is no longer one of *this* agent's turns (19.16): it runs on the card's own
        # agent, and `relay_core.board_turns` tags and collects it there. Only a cleanup still
        # comes through here.
        return event

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


#: Which statuses each configurable column collects by default (design 3, "Tabs and columns").
#: It lives in `board` beside `COLUMN_IDS` because the section editor validates against it and
#: the agent's `board_sections` tool writes what overrides it; this name is kept because the
#: protocol tests and the GUI's config block have always read it here.
COLUMN_STATUSES = B.COLUMN_STATUSES


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
