# SPDX-License-Identifier: AGPL-3.0-or-later
"""Worker protocol handlers for the Board pane (docs/AGENT-SESSIONS-PROTOCOL.md section 17).

The GUI never parses a card: it asks for rows and detail and sends back intents.  Every write
goes through `relay_core.board_tools.BoardTools`, the same code the agent tools use, so the
owner's writes and the agent's writes log the same thread events and share the undo snapshots.
The owner's instance simply runs with the rate limit and the duplicate check turned off.

`board_ask` is the Board agent: the worker it runs in is started by the GUI with
`agent_role: "switchboard"`, so its model is the `switchboard` role (protocol 13), which
defaults to the main agent.  The agent is stateless per card: the first question about a card
seeds the conversation from the card file plus the tail of its thread, and any later edit of
the card reseeds it, so the file stays the memory and a collaborator continues the same thread.
"""
from __future__ import annotations

import copy
import json
import os
import secrets
import threading
from pathlib import Path

from . import agent_context
from . import board as B
from . import board_chat
from . import board_links as BL
from . import board_turns
from . import guest_harness_provider as GHP
from . import logs
from . import roles as model_roles
# `board_import`, `forge_github`, `forge_sync` and `project_probe` are imported inside the
# five on-demand handlers that use them (`project_probe`, `board_import_*`, `forge_sync_*`
# and the new-board survey), not here: importing them at module level cost every worker
# 5.5 ms of start-up for code most workers never reach (#TZWF item 4). `sys.modules` makes
# every call after the first a dict lookup.
from .board_tools import (BOARD_STATES, CARD_MODE_TITLES, CARD_MODES, DONE_MEANS_HEADING, PLAN_HEADING, BoardInit,
                          BoardTools, BoardToolError, ToolContext, board_at, board_for,
                          card_brief, check_pane_token, cleanup_brief, find_board_root,
                          named_board_root, normalize_id, section_headings)

#: The `tests_*` requests of protocol section 31, spelled out here rather than imported from
#: `tests_protocol`: that module pulls in `test_probe` and `jobs`, and a pane that never opens
#: the Test suites pane should not pay for them at start-up (#TZWF item 4). It is the same set
#: as `tests_protocol.TYPES`, and `tests/test_tests_protocol.py` fails if the two drift.
TESTS_TYPES = ("tests_list", "tests_run", "tests_stop", "tests_history", "tests_check",
               "tests_suggest",
               # #PR4Q: "Use this existing result" — a run already in the store, accepted as
               # this card's evidence for this revision.
               "tests_accept",
               # Protocol 32 (#AQ6X): the signals are folded out of the same run history and
               # driven from the same handler, so they arrive through the same door.
               "signals_list", "signals_claim", "signals_release", "signals_dismiss",
               "signals_promote", "signals_config")

#: The Profile button's two requests (section 31.9, #7BM4 phase 5), spelled out here for the same
#: reason: `profile_protocol` pulls in `jobs`, and a worker whose owner never presses Profile
#: should not pay for it at start-up. It is the same set as `profile_protocol.TYPES`, and
#: `tests/test_profile_protocol.py` fails if the two drift.
PROFILE_TYPES = ("profile_run", "profile_stop")

#: Try it's three requests (section 31.10, #JNYN), spelled out here for the same reason: a worker
#: whose owner never presses Try it should not import `tryit_protocol` at start-up. It is the same
#: set as `tryit_protocol.TYPES`, and `tests/test_tryit_protocol.py` fails if the two drift.
TRYIT_TYPES = ("try_run", "try_stop", "try_answer")

#: The Check gate's two ends (#7BM4), spelled out here for the same reason `TESTS_TYPES` is: the
#: move path must not import `tests_protocol` — and through it `test_probe` and `jobs` — to decide
#: that an ordinary move is not a landing. `tests/test_tests_protocol.py` fails if they drift from
#: `tests_protocol.GATE_FROM_STATUS` / `GATE_TO_STATUSES`.
TP_GATE_FROM = "needs-verification"
TP_GATE_TO = ("needs-qa", "needs-qa-llm", "needs-qa-human", "needs-review", "done", "verified")

TYPES = {"board_open", "board_refresh", "board_card_get", "board_triage", "board_create", "board_update",
         "board_move", "board_priority", "board_delete", "board_comment", "board_undo", "board_ask",
         # Stop and go, per card: `board_cancel` pauses that card's queue, `board_resume` runs it
         # again — `cancel` and `resume_queue` (12.5) by the road a device can reach (#7JD1).
         "board_cancel", "board_resume", "board_check",
         # The pane's filter bar, answered here over the text the rows stopped carrying (#7M6E).
         "board_search",
         # The computed link index, both directions, per address (19.25, #EE42).
         "board_links",
         # Execute's hand-off in one message (19.19, #R9G7): the three writes it used to send
         # by hand, plus the pane session token the card records as `session`.
         "board_claim",
         "board_cleanup", "board_init", "board_init_answer", "board_folder", "board_sections",
         # Initializing a project and importing what is already in it (19.13,
         # docs/PROJECT-INIT-AND-IMPORT.md section 8).
         "project_probe", "board_import_propose", "board_import_apply",
         # Two-way sync with GitHub issues (19.14, docs/GITHUB-SYNC.md section 8).
         "forge_sync_plan", "forge_sync_run",
         # Retired by #AGNT and answered with one sentence for a release (see RETIRED_CHAT).
         "board_chat", "board_chat_cancel", "board_chat_queue_remove", "board_chat_queue_move",
         # The Test suites pane and a card's Check (section 31, #7BM4). Answered by
         # `tests_protocol.TestsCommands`, which this class holds one of per board.
         *TESTS_TYPES,
         # The Profile button (section 31.9, #7BM4), answered by `profile_protocol.ProfileCommands`
         # the same way: one per board, made on first use.
         *PROFILE_TYPES,
         # Try it (section 31.10, #JNYN): one bounded agent turn that opens the card's situation
         # for a person, answered by `tryit_protocol.TryItCommands` — one per worker, because it
         # runs on *this* worker's turn supervisor, as a cleanup does.
         *TRYIT_TYPES}

#: What every message here says when the pane has no board at all (protocol 19.1).  The current
#: folder and the original one, because a project may carry either and neither is wrong.
NO_BOARD_ERROR = ("This project has no board (no .board/board.yaml, and no "
                  "issues/board.yaml).")

#: What a *board* ask gets when this tab has no board (protocol 30.7).  The console itself
#: still runs — Options, Actions and Sessions are about the app, not about a board — so this is
#: only for the one surface whose whole subject is the cards.
NO_BOARD_CHAT_ERROR = ("This tab has no board, so there is nothing for the Board "
                       "pane to talk about. Attach a project to the tab, or ask from Options, "
                       "Actions or Sessions.")

#: `board_chat` and its three queue messages are retired (card #AGNT): a console is an ordinary
#: pane worker with a context, so its turn is `ask` and its queue is the pane's.  They are still
#: *recognised* for a release, and answered with this rather than "Unknown protocol message" —
#: a GUI one version behind must be told what to send, not handed a traceback.
RETIRED_CHAT = ("board_chat and its queue messages retired with card #AGNT: send `ask` with a "
                "`surface`, after a `configure` carrying a `context` block, and use `cancel`, "
                "`queue_remove` and `queue_move` — the console's queue is the pane's queue now.")

#: The owner-side messages that may be the first thing a project's board ever hears.  Only a
#: create can be: the other three name a card, and an uninitialized board has none.
INIT_WRITES = ("board_create",)

#: What an import or a sync says when the pane's board has not been created yet (19.13, 19.14).
#: Both write cards, and a card needs a board.yaml; the GUI's path is `board_init` first.
NOT_INITIALIZED_ERROR = ("This project has no Board yet. Create one first "
                         "(board_init), then import or sync into it.")

#: `board_import_apply`: how many keys one message may carry.  The proposals themselves are
#: capped by `board_import.MAX_PROPOSALS`; this is the wire.
MAX_IMPORT_KEYS = 1000

#: How much of one card is searchable: its whole body and thread, capped.  Until 2026-09-20 this
#: text travelled on every row (19.2 `text`) for one substring test in the GUI; it was 92.6 % of
#: `board`'s bytes, and past about 1,160 cards the event overflowed the GUI's 8 MiB read buffer
#: and killed the worker (#7M6E).  It stays here now and `board_search` answers over it, so the
#: cap bounds the worker's own memory rather than a message.
MAX_ROW_TEXT = 64 * 1024

#: How many rows one message may carry, and how many bytes of them (#7M6E).  `board` was one line
#: of every card; the rows travel in batches now (`board_cards`), which the pane patches in
#: exactly as it patches a `board_changed` upsert, so no board size can reach the buffer cap.
#: Both limits apply — the count keeps a normal board to one or two messages, the byte budget
#: keeps a board of unusually long titles inside the cap whatever the count says.
MAX_ROWS_PER_MESSAGE = 400
MAX_ROW_BYTES_PER_MESSAGE = 512 * 1024

#: The `code` a failed `forge_sync_*` answers with (19.14), so the GUI can offer the right thing:
#: signing in, waiting until `retry_at`, or just saying what happened.
FORGE_ERROR_CODES = {"ForgeAuthError": "forge_auth", "ForgeRateLimited": "forge_rate_limited",
                     "ForgeUnavailable": "forge_unavailable", "ForgePrivacyError": "forge_privacy",
                     "ForgeError": "forge_failed"}

#: `board_links` (19.25, #EE42): how many addresses one request may ask about, and how many
#: dangling edges one answer carries.
MAX_LINK_ADDRESSES = 200
MAX_DANGLING = 500

#: How much of a card the Board agent is seeded with (design 5, "Attach").
SEED_BODY_BYTES = 16384
SEED_THREAD_ENTRIES = 10
MAX_ASK_TEXT = 32768

#: `board_cleanup` (protocol 19.9): the roster the brief is sent with, and how much of the
#: user's own extra instruction is carried.  The roster is one line per card, so even a very
#: large board fits; the agent reads the cards it cares about with `board_read`.
MAX_CLEANUP_ROSTER = 400
MAX_CLEANUP_NOTE = 4000


class _CardEntry:
    """One card file in the Board's parse cache (#7M6E): what it was when we read it, and
    everything derived from it.  `key` is its (mtime_ns, size) and `thread_key` its thread file's,
    because the row's entry count and the searchable text come from both; either moving re-reads
    the card.  `row` is None for a file that would not parse, and `problems` then holds the
    `bad_card` that `check()` would report for it."""
    __slots__ = ("key", "thread_key", "card_id", "rel", "row", "search", "problems")

    def __init__(self, key, thread_key, card_id, rel, row, search, problems):
        self.key = key
        self.thread_key = thread_key
        self.card_id = card_id
        self.rel = rel
        self.row = row
        self.search = search
        self.problems = problems


class _ThreadEntryCache:
    """One thread file in the same cache: how many entries it holds (the row's `thread_entries`),
    its `check_thread` problems and its folded searchable text, all keyed on its (mtime_ns, size).
    The two problems that depend on which cards exist are not here — see `check_thread_name`."""
    __slots__ = ("key", "count", "problems", "search")

    def __init__(self, key, count, problems, search):
        self.key = key
        self.count = count
        self.problems = problems
        self.search = search


def _stat_key(path: Path) -> tuple | None:
    """What says a file has not changed: its mtime in nanoseconds and its size.  None when it is
    not there, which is a change like any other."""
    try:
        st = path.stat()
    except OSError:
        return None
    return (st.st_mtime_ns, st.st_size)


def _search_fields(row: dict) -> str:
    """The row's own searchable fields, in the order `BoardModel.cpp`'s filter puts them: a plain
    word in the filter bar matches these or the card's text, and both are folded together here so
    `board_search` answers exactly what the GUI used to answer for itself (#7M6E)."""
    return " ".join(str(part) for part in
                    (row.get("id") or "", row.get("title") or "",
                     " ".join(str(l) for l in (row.get("labels") or [])),
                     row.get("assignee") or "", row.get("milestone") or ""))


def _fold(*parts: str) -> str:
    """One case-folded string to search.  Folded once here rather than per keystroke: the filter
    has always been case-insensitive, and nothing ever displays this copy."""
    return "\n".join(parts).lower()


def _row_batches(rows: list[dict]) -> list[list[dict]]:
    """The rows cut into messages small enough that no board size reaches the GUI's read buffer
    (#7M6E).  A batch ends at MAX_ROWS_PER_MESSAGE rows or MAX_ROW_BYTES_PER_MESSAGE of them,
    whichever comes first; a single oversized row still gets a batch of its own rather than being
    dropped, because the pane must be able to draw every card it has."""
    batches: list[list[dict]] = []
    current: list[dict] = []
    size = 0
    for row in rows:
        width = len(json.dumps(row, default=str))
        if current and (len(current) >= MAX_ROWS_PER_MESSAGE
                        or size + width > MAX_ROW_BYTES_PER_MESSAGE):
            batches.append(current)
            current, size = [], 0
        current.append(row)
        size += width
    if current:
        batches.append(current)
    return batches


def _turn_phrase(mode: str | None) -> str:
    """What a running card turn is called in a refusal: "a plan", "a question", "a turn"."""
    return {"plan": "a plan", "discuss": "a question", "refine": "a refinement"}.get(mode or "", "a turn")


def _cards_phrase(cards: list[str]) -> str:
    """"a turn on #A", "turns on #A and #B", "turns on #A, #B and #C"."""
    ids = [f"#{c}" for c in cards]
    if len(ids) == 1:
        return f"a turn on {ids[0]}"
    return "turns on " + (", ".join(ids[:-1]) + " and " + ids[-1] if ids else "the board")


def board_block(request: dict | None) -> dict | None:
    """The `board` block of a `configure` request with its top-level `qa` folded in (#C3Q2),
    so the QA switch travels with the board settings through every rebuild of the tools — the
    owner's half in `configure`, the agent's in `agent_tools`, and every re-point after."""
    request = request or {}
    block, qa = request.get("board"), request.get("qa")
    if isinstance(qa, dict) and (block is None or isinstance(block, dict)):
        return {**(block or {}), "qa": qa}
    return block


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
      `folder`   the folder a board this pane *creates* would go in: `board` (the default,
                 visible since the owner's decision of 2026-09-21) or one of the older spellings
                 a project may ask for by name.  It never affects reading: a board is found
                 wherever it already is, in `B.BOARD_FOLDERS` order.
      `autonomy`, `limits`  as before.
      `qa`       the QA policy floor's global layer (#C3Q2): `{verification: ask|automatic}`
                 from Options › Agent › QA.  `configure` copies its top-level `qa` in here so
                 the block travels with the board settings; `board.yaml qa:` overrides it.
    """
    if block is None:
        block = {}
    if not isinstance(block, dict):
        raise ValueError("board must be an object, or null to detach.")
    attach = block.get("attach", True)
    if type(attach) is not bool:
        raise ValueError("board.attach must be true or false.")
    out = {"raw": dict(block), "attach": attach, "state": "ready",
           "autonomy": block.get("autonomy"), "limits": block.get("limits"),
           "qa": block.get("qa") if isinstance(block.get("qa"), dict) else None}
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
        #: This pane's session token, from `configure {pane_token}` (protocol 19.19). The board
        #: tools carry it so `board_claim` can write it onto a card as `session` and onto the
        #: claim's thread entry as `pane_token`. None for the Board worker, which has no
        #: terminal pane of its own, and for a GUI too old to send one.
        self.pane_token: str | None = None
        #: "Initialize a project and create a Board here?" (protocol 19.12), shared by the
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
        #: The searchable text the snapshot's rows were taken with, so `_changed` notices a body
        #: edit the row itself does not show (#7M6E).
        self._snapshot_search: dict[str, str] = {}
        #: The parse cache behind `_rows()` and `_problems()` (#7M6E): one entry per card file and
        #: one per thread file, keyed on that file's (mtime_ns, size), so a `board_refresh` after a
        #: one-card write re-reads one card and one thread rather than the whole tree twice.  It is
        #: dropped whole when board.yaml changes — a row's `tab` and a parked card's section come
        #: from it — and per file whenever the file's stat moves.  `set_board` clears it below.
        self._card_cache: dict[str, _CardEntry] = {}
        self._thread_cache: dict[str, _ThreadEntryCache] = {}
        self._cache_config: tuple | None = None
        #: What `_rows()` left for `_problems()`: the per-file problems it collected, the ids it
        #: saw and where, and the thread files it walked.  None before the first `_rows()`, and
        #: then `_problems()` falls back to a full `board.check()`.
        self._check_state: tuple | None = None
        #: Each card's searchable text, case-folded once, for `board_search` (19.2, #7M6E): its
        #: row fields and body, then its thread, held apart because they are cached against two
        #: different files.  The rows no longer carry this text — it was 92.6 % of `board`'s
        #: bytes, and past about 1,160 cards the event overflowed the GUI's buffer.
        self._search_index: dict[str, tuple[str, str]] = {}
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
        #: The live card consoles (19.16, `relay_core.board_turns`): one agent, one conversation
        #: and one `TurnSupervisor` per card, so Plan on #A and Discuss on #B run at the same
        #: time and a second prompt on #A queues behind the first (#CTRN). Cards were serialized
        #: through the worker's single turn runner until 2026-09-19; the cleanup below still is,
        #: because it rewrites the whole board.
        self.cards = board_turns.CardTurns(
            self.emit, lambda card_id, emit: self._build_card_console(card_id, emit),
            on_answer=self._card_answer)
        #: Whether this worker's agent is an agent **console** rather than a terminal pane's own
        #: (`configure {context: {scope: "console"}}`, protocol 33).  Set by `worker.py`. It is
        #: what makes the board offer a console's tool set, what makes the survey a turn of this
        #: worker's own conversation, and what makes a card turn wait for it.
        self.console = False
        #: Whether the console's conversation has been seeded with the board (19.18): the roster
        #: goes in front of the first question and never again, the conversation being the
        #: context from then on.  `False` again whenever the worker is pointed elsewhere.
        self.seeded = False
        #: A survey has run on this worker, so `board_open` does not offer a second one.
        self.surveyed = False
        #: The tab this worker is the console of (`configure {tab}`, protocol 30.7), or "" from
        #: a GUI that sends none.  The context's `persist.key` is the same id, and `worker.py`
        #: passes whichever it was sent.
        self.tab = ""
        #: The (workspace, tab) the live conversation belongs to, so a `configure` that moves
        #: either is known to be a different console.
        self._helper_key: tuple[str, str] | None = None
        #: This worker's `SubagentManager`, set by `backend/worker.py` (#AQ6X step 7b). It is what
        #: a **signal thread** runs on: a failing check nobody is on becomes a subagent of this
        #: worker, notified and listed in Sessions, rather than a turn inside the user's pane.
        #: None in a test and in any worker that has no subagents — no manager, no pickup.
        self.subagents = None

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
        self.pane_token = check_pane_token((request or {}).get("pane_token"))
        return self._point(workspace, parse_board(board_block(request)), agent=False)

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
            self.tools = self._build(board, state, settings, actor="owner", workspace=workspace)
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

    def _build(self, board: B.Board, state: str, settings: dict, *, actor: str,
               workspace: str | None = None) -> BoardTools:
        tools = BoardTools(board, emit=self.emit, autonomy=settings.get("autonomy"),
                           limits=settings.get("limits"), qa=settings.get("qa"), workspace=workspace,
                           context=ToolContext(actor=actor,
                                               pane=os.environ.get("RELAY_PANE_ID") or None),
                           enforce_limits=actor != "owner", duplicate_check=actor != "owner",
                           state=state, project=settings["project"], init=self.init,
                           pane_token=self.pane_token)
        tools.on_created = self._board_became_ready
        return tools

    def release_claims(self, reason: str = "the pane closed") -> list[str]:
        """Drop this pane's claims on the board it is on (19.19, #R9G7; owner 2026-09-20).

        The worker's `shutdown` calls this: the pane that held the cards has closed, and a pane
        token is a fresh uuid per pane that no session resume brings back, so a `session` left on
        a card would tell the next pane it is taken by something that no longer exists. `_repoint`
        calls it for the same reason when this worker is pointed at another board.

        The owner-side tools are the ones asked, because they exist whenever this pane has a board
        at all, while the agent's are rebuilt by every `configure`; both halves carry the same
        `pane_token`, so it is the same set of cards either way. Never raises — a shutdown must not
        fail over the board — and does nothing for a worker with no board or no pane token.
        """
        tools = self.tools
        if tools is None:
            return []
        try:
            return tools.release_claims(reason)
        except Exception:
            return []

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
        "initialize a Board here?" belongs to the old project too, so a new one may ask.
        """
        current = self.tools.board.root if self.tools is not None else None
        if root is not None and root == current:
            return
        # The claims this pane holds on the board it is leaving (19.19, #R9G7). `session` says a
        # live pane is working on the card; this pane is about to be somewhere else, and the tools
        # that hold the claim are thrown away below, so they are dropped here while they still can
        # be. The cards stay in Executing: the work is in flight, only the claim is stale.
        self.release_claims("the pane moved to another project")
        # The card conversations belong to the board we are leaving, and their agents hold that
        # board's tools: stop them and forget them rather than let them write into it (19.16).
        self.cards.drop()
        # The roster this worker's console was seeded with was the old board's (19.18): the next
        # question seeds again. The conversation itself is the agent's and `set_board` keeps it
        # on purpose — that is the whole of 19.11.
        self.seeded = self.surveyed = False
        self._snapshot = {}
        self._snapshot_search = {}
        self._drop_parse_cache()
        self._ask_card = self._ask_hash = self._ask_turn = None
        self._ask_text = []
        self._ask_mode = "discuss"
        self._brief_mode = None
        self.init.declined = False
        self.init.fail_pending()
        self._parked.clear()

    def agent_tools(self, workspace: str | None, request: dict | None = None) -> BoardTools | None:
        """The *agent's* instance of the tools for this workspace (guardrails on)."""
        settings = parse_board(board_block(request))
        board, state = self._resolve(workspace, settings)
        if board is None:
            return None
        tools = self._build(board, state, settings, actor="agent", workspace=workspace)
        if tools.autonomy == "off":
            return None
        if self.console:
            # An agent console's board tools offer a console's set for as long as the console
            # exists — merge, split, the import and `search_files` — rather than for one turn
            # the way a card's stage scope does (#AGNT, `board_tools.ConsoleScope`).
            tools.begin_console()
        return tools

    # ---- the agent's half, swapped without losing the conversation -------------
    def _agent(self):
        return getattr(self.turns, "agent", None)

    def bind_agent(self, agent) -> None:
        """`configure` built a new Agent: the init dialog follows its Stop.

        A user who presses Stop while "Initialize a project and create a Board here?" is
        open must not leave the turn thread parked on it, so the round trip watches the agent's
        own `cancel_event` — the same event every other blocking tool watches.
        """
        cancel = getattr(agent, "cancel_event", None)
        if cancel is not None:
            self.init.cancel = cancel

    def set_tab(self, tab) -> str:
        """Which tab this worker is the helper of (`configure {tab}`, protocol 30.7).

        The tab's persistent id — the one that restores its panes — keys the helper's
        conversation together with the workspace, so the same tab comes back with its own
        history and two tabs on one project keep two.  `worker.py` calls it straight after
        `configure`, which is where the workspace is settled.

        A worker re-pointed at another tab lets the live conversation go, so the first tab's
        messages are never carried into the second tab's file; a `configure` that moves neither
        — a model swap, a keybinding reload — leaves it exactly where it was.
        """
        tab = board_chat.validate_tab(tab)
        key = (self.workspace or "", tab)
        self.tab = tab
        if self._helper_key is not None and self._helper_key != key:
            # Another tab's console: its roster and its survey were the last tab's. The
            # conversation is `Agent.adopt_session`'s, keyed by (workspace, tab) in
            # `agent_context` — `configure` has already pointed it at this tab's own file.
            self.seeded = self.surveyed = False
        self._helper_key = key
        return tab

    def _attach_agent_tools(self, workspace: str | None, settings: dict) -> None:
        """Give the pane's live agent the board these settings name, keeping its conversation.

        `configure` cannot do this — it builds a new `Agent` and with it a new conversation — so
        attaching a project to a tab that is already talking goes through here: the agent object,
        its messages and its session id are untouched; only `agent.board` and the system prompt's
        Board block change.  `Agent.tools()` is read per step, so the tool list follows by
        itself; `refresh_system_prompt` rewrites `messages[0]` in place.
        """
        agent = self._agent()
        if agent is None or not hasattr(agent, "refresh_system_prompt"):
            return
        agent.board = self.agent_tools(workspace, {"board": settings["raw"]})
        if self.console and agent.board is not None:
            # A console's board tools are a console's, whenever they arrive (#AGNT): merge,
            # split, the import and `search_files`. `configure` calls `begin_console` on the
            # tools it builds; a project attached to a tab that is already talking came through
            # here and got a pane's set instead, which is the tool list changing under a
            # conversation that this card is otherwise about not doing (#CTRN).
            agent.board.begin_console()
        cancel = getattr(agent, "cancel_event", None)
        if cancel is not None:
            self.init.cancel = cancel
        agent.refresh_system_prompt()

    def refuse_model_selection(self, request):
        """Refuse a helper model change only while card turns might be mid-flight (#BMS1).

        A guest pick needs no refusal since #E34S: the guest process is the helper's own agent,
        and a card console follows its helper's *rest* of the table through
        `_console_model`. What a card console cannot do is swap mid-turn, so a change while the
        board runs is the one refusal left.
        """
        if not self.console:
            return False
        main = self._agent()
        if main is None:
            return False
        reason = ("" if self.cards.idle() else
                  "Wait for the board's running and queued card turns to finish before changing models.")
        if not reason:
            return False
        self.emit({"event": "model_switch_refused", "id": request.get("id"),
                   "at": "request", "model": request.get("model", ""),
                   "current_model": main.config.model,
                   "preset": main.preset.id if main.preset else None,
                   "context_window": main.context.window, "effort": main.effort,
                   "reason": reason, "code": "board_model_unavailable"})
        return True

    def _sync_card_model(self, session):
        """Keep a cached idle conversation on the helper's selected provider (#BMS1)."""
        main = self._agent()
        if not self.console or main is None or session is None or not session.idle:
            return
        config, preset, effort = self._console_model(main)
        agent = session.agent
        old_preset = agent.preset.id if agent.preset else None
        if agent.config != config or old_preset != preset or agent.effort != effort:
            # set_model adapts history and token accounting while retaining the conversation.
            # Copy: effort and temporary Plan routing must not mutate the tab's config.
            agent.effort = effort
            agent.set_model(copy.deepcopy(config), preset, main.context.window)
        agent.roles = main.roles

    def _console_model(self, main):
        """(config, preset_id, effort) a card console runs on, or the sentence saying why there is none.

        A guest harness is not an endpoint, and since #E34S the guest it names is the *helper's
        own* agent — one process, one agent — so a card console, another agent in this same
        worker, cannot also use its ``harness://`` config. Building one anyway is what the owner
        saw on 2026-09-20 — "The Board agent could not answer: Base URL must be an HTTPS URL
        without credentials, query, or fragment", raised five frames down in
        ``ProviderConfig.validate`` (card #GH5T). That console's model is the first usable entry
        of the Options › Models priority list (`resolver.leave_guest`); when the list holds
        nothing, this raises the one sentence a turn then answers with instead.
        """
        name = GHP.guest_name(main.config)
        if not name:
            return main.config, (main.preset.id if main.preset else None), main.effort
        spare = main.roles.leave_guest() if main.roles is not None else None
        if spare is None:
            raise ValueError(GHP.helper_refusal(name))
        return spare.config, spare.preset_id, spare.effort

    def _build_card_console(self, card_id: str, emit):
        """Build the console one card's turns run on (19.16, card #CTRN): an ordinary console.

        The provider config, the skills, the roles chain and the failover switches are the pane
        agent's, so a card turn answers on the model the Board is configured with and
        fails over the way the pane does — unless that model is the helper's own guest harness
        (#E34S), when `_console_model` takes the console to the priority list's first usable
        entry instead: a guest is one process running one agent, and that agent is the helper's. Everything that carries state is this card's own: its
        conversation, its `cancel_event` (so Stop on one card cannot stop another) and its own
        `BoardTools`, which is where `card_scope` lives — which is why enforcing what a Plan may
        touch needed no change in `board_tools.py`.

        **What #CTRN changed is everything else.**  It was built `tool_scope="card"`, with no
        request ledger, no todo tool and no completion check, because a card turn was "one
        prompt answered into a card thread, not a pane's unit of work".  A card turn is now an
        ordinary supervised turn on an ordinary console, so it is built as one: the console
        scope, and the ledger, todos and completion check the pane agent has.  What a Discuss or
        a Plan may touch is the *turn's* constraint (`Agent.set_card_turn`), not the agent's.

        Its conversation is persisted per **(tab, card)** — `persist {scope: "helper", key:
        "<tab id>/card:<ID>"}` — so a card remembers its earlier turns across a restart. The tab
        is in the key because a tab owns one worker and that worker owns its conversation files:
        keyed by the card alone, two tabs on one project would adopt one file from two workers
        and the last to save would win.  A worker that has not been told its tab keeps the
        ephemeral conversation it had before this card.
        """
        from .agent import Agent          # late: agent.py pulls in the whole tool executor
        from .tools import Workspace
        main = self._agent()
        if main is None:
            raise ValueError("Configure a provider and workspace first.")
        tools = self.agent_tools(self.workspace, {"board": self.settings["raw"]})
        if tools is None:
            raise ValueError("The Board agent has no board tools here "
                             "(this project has no board.yaml, or its autonomy is off).")
        workspace = str(tools.board.repo)
        session_dir, session_id = self._card_session_file(card_id)
        config, preset_id, effort = self._console_model(main)
        agent = Agent(copy.deepcopy(config), workspace, emit,
                      max_steps=main.max_steps, max_tool_calls=main.max_tool_calls,
                      skills=getattr(main.executor, "skills", None),
                      preset_id=preset_id,
                      roles=main.roles, board=tools, effort=effort,
                      # Protocol 33: the named scope. A card console is a console — one tool
                      # list, offered to every turn, with the stage's rule refused at call time
                      # (owner decision 3 on #CTRN). `card` is what this said before that.
                      tool_scope="console",
                      # Protocol 30.4: one tool set. A card turn drives the app through the same
                      # `AppTools` the pane agent holds, so the change log is the worker's.
                      app=getattr(main, "app", None),
                      track_requests=getattr(main, "track_requests", True),
                      todo_tool=getattr(main, "todo_tool", True),
                      completion_check=getattr(main, "completion_check", True),
                      session_dir=session_dir,
                      stall_timeout_s=main.stall_timeout_s,
                      first_token_timeout_s=getattr(main, "first_token_timeout_s", 0.0),
                      failover=getattr(main, "failover", True),
                      failover_hosted=getattr(main, "failover_hosted", False))
        # A console's board tools offer the console's set (#AGNT); a card's are a console's.
        tools.begin_console()
        if session_id:
            # The conversation this card had last time, loaded now that the agent exists; a card
            # nothing has been said about yet starts empty under that id, and the end-of-turn
            # autosave keeps it from then on (30.7). Never fatal: a card whose history cannot be
            # read is a card with no history, not a card that cannot be discussed.
            try:
                agent.adopt_session(session_id)
            except (ValueError, OSError):
                logs.event(logs.get("board"), "card_session_unreadable",
                           level_name="warning", card=card_id)
        # Options › Security (#3KB7): a card turn reads the repository, so the owner's extra
        # readable roots and extra secret patterns are its rules too.
        policy = getattr(main.executor, "policy", None)
        if policy is not None:
            agent.executor.policy = policy
            agent.executor.workspace = Workspace(workspace, policy)
        if getattr(main, "instructions", None) is not None:
            agent.set_instructions(main.instructions)
        return agent, tools

    def _card_session_file(self, card_id: str) -> tuple[str | None, str | None]:
        """(session_dir, session_id) for one card's conversation, or (None, None).

        `agent_context.helper_file` decides *where*, exactly as it does for a tab's console: the
        directory is the workspace's and the name is the key's, and a conversation that already
        exists under any workspace digest for this key is the one that is opened — so a tab that
        gained its project later keeps what was said in it (#AGNT's live drive).
        """
        if not self.tab or not self.workspace:
            return None, None
        directory, name = agent_context.helper_file(
            self.workspace, f"{self.tab}/{board_turns.surface_of(card_id)}")
        return str(directory), name

    def console_seed(self, agent) -> str:
        """The board a console's first question is seeded with (19.18), or "".

        Read by `worker.py` on every `ask`: the roster goes in front of the first prompt of a
        conversation and never again. It is checked against the agent's own message list rather
        than a flag alone, so a console whose saved conversation came back from disk (30.7) is
        not re-seeded with a board it has already been told about.
        """
        if not self.console or self.tools is None:
            return ""
        if self.seeded or len(getattr(agent, "messages", [None])) > 1:
            self.seeded = True
            return ""
        self.seeded = True
        return board_chat.board_seed(self.tools)

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
        # The turn is the event (#3XZV): a Plan that finished and left its `## Plan` on the card
        # has planned it. Swallowed on refusal for the same reason as the ask's own move.
        if session.mode == "plan":
            try:
                tools.stage_advance(session.card_id, "plan-written")
            except (BoardToolError, B.BoardError, OSError):
                pass
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
    def set_keybindings(self, catalog) -> None:
        """A `keybindings` message reached the worker: hand the board's own agents the catalogue.

        `worker.py` replaces the catalogue on this worker's agent, which since #AGNT is the
        console itself.  What it misses is the **card** conversations (19.16), each a second
        `Agent` with its own `ToolExecutor`; a card turn still refuses `set_keybinding` by scope,
        so this is about the keys a card turn *reports*, not the ones it writes.  Replaced in
        place, like the pane's: the conversation and the tool list are the same afterwards,
        because the schema no longer carries the keys.
        """
        for session in list(getattr(self.cards, "_sessions", {}).values()):
            executor = getattr(getattr(session, "agent", None), "executor", None)
            if executor is not None:
                executor.keybindings = catalog

    def set_board(self, request: dict) -> None:
        """`set_board`: point this pane at another board (or none) without ending its conversation.

        Mid-turn it is deferred to the end of the turn, exactly as `set_agent_role` defers a model
        switch, so a running turn keeps the tool set it started with.  Both cases answer
        `board_state`; the one that says `applies: "now"` is the one in force.
        """
        if "board" not in request:
            raise ValueError("set_board needs a board object, or null to detach.")
        # A pane that re-points itself may also (re)state its token; without one the pane keeps
        # the token its `configure` gave it, since the pane itself has not changed.
        if "pane_token" in request:
            self.pane_token = check_pane_token(request.get("pane_token"))
        block = request["board"]
        # A re-point keeps the QA switch its `configure` carried (#C3Q2): the pane changed
        # boards, not its Options.
        previous = getattr(self, "settings", None) or {}
        if isinstance(block, dict) and "qa" not in block and isinstance(previous.get("qa"), dict):
            block = {**block, "qa": previous["qa"]}
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
            raise ValueError("board_init needs a project to create the Board in.")
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

    # ---- moving the board's folder (protocol 19.17) ----------------------------
    def _folder(self, request: dict, rid) -> None:
        """`board_folder {folder}`: move this board to `.board/`, from any older spelling.

        The one thing that moves an existing board, and only because the user asked for it: reading
        is tolerant of every spelling (`B.BOARD_FOLDERS`) and nothing migrates by itself.  `git mv`
        in a checkout, a plain rename outside one, and a refusal — with the reason, having changed
        nothing — for an `issues/` board, an existing target or uncommitted changes.

        `{hidden: true|false}` is the retired shape of this message (the "Hidden Board
        folder" toggle, 2026-09-19 to 2026-09-21).  It is still accepted for one release, as
        `.switchboard` and `switchboard`, so a phone paired to an older build is not broken.

        A turn must not be running: the agent holds card paths under the old folder, and the board
        pane's watcher is on it.  Afterwards the worker is re-pointed at the new root, so the
        tools, the agent's tools and the GUI all move together.
        """
        folder = request.get("folder")
        if folder is None and type(request.get("hidden")) is bool:
            folder = B.HIDDEN_BOARD_FOLDER if request["hidden"] else B.VISIBLE_BOARD_FOLDER
        if not isinstance(folder, str) or not folder.strip():
            raise ValueError("board_folder needs `folder`: the folder to move this board to, one "
                             "of " + ", ".join(f for f in B.BOARD_FOLDERS
                                               if f != B.LEGACY_BOARD_FOLDER) + ".")
        folder = folder.strip()
        tools = self._need_ready()
        if getattr(self.turns, "busy", False):
            raise ValueError("Stop the active agent turn before moving this board's folder.")
        try:
            move = B.rename_board_folder(tools.board, folder)
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

        The same tool the board agent calls (`board_sections`), so the gear, a cleanup run
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
        args["reason"] = str(request.get("reason") or "edited in the Board")[:200]
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
        self._emit_changed(result.get("write_id"))

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
        from . import project_probe as PP
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
        from . import project_probe as PP
        project = self._project(request, "project_probe")
        result = PP.probe(project, kinds=self._kinds(request))
        event = {"event": "project_probe_result", "id": rid, **result}
        board = self._board_for_project(project)
        if board is not None:
            event["root"] = str(board.root)
        self.emit(event)

    def _import_propose(self, request: dict, rid) -> None:
        """`board_import_propose`: the cards an import would create.  Writes nothing."""
        from . import board_import as I
        from . import project_probe as PP
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
        from . import board_import as I
        from . import project_probe as PP
        tools = self._need_ready()
        project = (self._project(request, "board_import_apply") if request.get("project")
                   else Path(tools.board.repo))
        if Path(tools.board.repo) != project:
            raise ValueError(f"This pane's Board is {tools.board.root}, which is not in "
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
        self._emit_changed()

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
        """`forge_sync_plan` / `forge_sync_run`: the Board against its repository.

        The network part runs on a thread, like `hosted_quota` (13.9), so the message loop never
        waits on GitHub; **exactly one** terminal event follows either way —
        `forge_sync_planned`, `forge_sync_done`, or `error`. The error text is scrubbed and names
        the exception rather than carrying a traceback, and no credential is ever in it: the
        provider holds the token and never hands it over (`GitHubProvider._safe`).
        """
        from . import forge_github as GH
        from . import forge_sync as F
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
                    self._emit_changed()
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
    def _thread_search_text(entries: list[B.ThreadEntry]) -> str:
        """A thread as one searchable string: each entry's author, kind and words.

        The entries' header metadata (timestamps, turn and pane ids) is left out on purpose:
        `pane=switchboard` sits in every header, so a search for "switchboard" would match
        every card that has a thread.  A card's body is the other half, and the two are held
        apart so a card edit does not re-parse its thread, nor an append its card."""
        return "\n\n".join(part for part in
                           (f"{entry.author} {entry.kind} {entry.text}".strip()
                            for entry in entries) if part)

    def _drop_parse_cache(self) -> None:
        self._card_cache = {}
        self._thread_cache = {}
        self._cache_config = None
        self._check_state = None
        self._search_index = {}

    def _rows(self) -> dict[str, dict]:
        """Every card's row, re-parsing only the files whose mtime or size moved (#7M6E).

        This used to read and parse the whole tree, and `_problems()` then read and parsed it a
        second time through `board.check()`: 179 ms at 337 cards and 1,596 ms at 3,000, the same
        whether one card had changed or none, on every watcher tick.  Now each card file and each
        thread file carries a cache entry keyed on its own (mtime_ns, size) holding its row, its
        folded search text and its `check_card` problems, so an unchanged file costs one `stat`.
        board.yaml decides a row's `tab` and a parked card's section, so a change to it drops the
        whole cache; `set_board` drops it too, because the next board is a different tree.
        """
        tools = self._need()
        board = tools.board
        config_key = _stat_key(board.config_path)
        if config_key != self._cache_config:
            self._drop_parse_cache()
            self._cache_config = config_key

        # The thread files first: a row's entry count and half of its searchable text come from
        # them, so the card loop below needs them settled.  Each is parsed only when its own stat
        # moved — which is what keeps a card edit from re-reading that card's thread as well.
        problems: list[B.Problem] = []
        threads: dict[str, _ThreadEntryCache] = {}   # thread file -> its cache entry
        thread_paths: dict[str, str] = {}            # CARD ID -> thread file
        for private in (False, True):
            folder = board.threads_dir(private)
            if not folder.is_dir():
                continue
            for path in folder.glob("*.md"):
                name = str(path)
                key = _stat_key(path)
                if key is None:
                    continue                        # removed between the listing and the stat
                held = self._thread_cache.get(name)
                if held is None or held.key != key:
                    try:
                        entries = B.parse_thread(path.read_text(encoding="utf-8"))
                    except (OSError, UnicodeDecodeError):
                        entries = []
                    # The two problems that depend on which cards exist (`bad_thread_name`,
                    # `orphan_thread`) are not here: `_problems()` recomputes them, so a card
                    # appearing or going does not invalidate every thread's entry.
                    held = _ThreadEntryCache(
                        key, len(entries), board.check_thread(path, entries),
                        _fold(self._thread_search_text(entries)[:MAX_ROW_TEXT]))
                threads[name] = held
                thread_paths[path.stem.upper()] = name
                problems.extend(held.problems)
        self._thread_cache = threads

        rows: dict[str, dict] = {}
        search: dict[str, tuple[str, str]] = {}
        by_id: dict[str, list[str]] = {}
        cache: dict[str, _CardEntry] = {}
        for path in board.card_paths():
            name = str(path)
            key = _stat_key(path)
            if key is None:
                continue
            entry = self._card_cache.get(name)
            thread = thread_paths.get((entry.card_id if entry else None) or "", "")
            held = threads.get(thread)
            thread_key = held.key if held else None
            if entry is None or entry.key != key or entry.thread_key != thread_key:
                rel = B.relative_name(path, board.root)
                try:
                    card = B.Card.load(path)
                except (B.BoardError, UnicodeDecodeError, OSError) as exc:
                    # The same problem `check()` reports for an unreadable card, and no row: the
                    # pane cannot draw a card nothing could parse.
                    entry = _CardEntry(key, None, None, rel, None, "",
                                       [B.Problem("bad_card", rel, str(exc))])
                    cache[name] = entry
                    problems.extend(entry.problems)
                    continue
                thread = thread_paths.get(card.id or "", "")
                held = threads.get(thread)
                row = tools._row(card, {card.id or "": held.count if held else 0})
                row["created"] = card.front.get("created")
                row["milestone"] = card.front.get("milestone")
                row["component"] = card.front.get("component")
                row["implemented_by"] = card.front.get("implemented_by")
                # The signatures travel on every row; the `qa` recommendation does not — it is per
                # card and costs a PATH and keyring probe, so it rides on `board_card_get` (19.15).
                row["verified_by"] = card.front.get("verified_by")
                # The card's whole text does **not**: it was 92.6 % of `board`'s bytes for one
                # substring test in the GUI, and the pane's filter is still full-text search
                # (owner, 2026-09-19) — `board_search` answers it here instead, over the folded
                # copy below.  `_row` already counted the tasks, so they are not recounted.
                entry = _CardEntry(key, held.key if held else None, card.id, rel, row,
                                   _fold(_search_fields(row), card.body.strip()[:MAX_ROW_TEXT]),
                                   board.check_card(card, rel))
            cache[name] = entry
            problems.extend(entry.problems)
            if entry.card_id:
                by_id.setdefault(entry.card_id, []).append(entry.rel)
            if entry.row is not None and entry.card_id:
                rows[entry.card_id] = entry.row
                search[entry.card_id] = (entry.search, held.search if held else "")
        self._card_cache = cache

        self._search_index = search
        self._check_state = (problems, by_id, sorted(threads))
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
        search = self._search_index
        # The searchable text is part of "changed" although it no longer travels (#7M6E): an edit
        # to a card's body within the same second as the last one moves no field on the row —
        # `updated` is an ISO timestamp — and the pane would not re-read the open card for it.
        upserts = [row for cid, row in rows.items()
                   if self._snapshot.get(cid) != row
                   or self._snapshot_search.get(cid) != search.get(cid)]
        removed = [cid for cid in self._snapshot if cid not in rows]
        self._snapshot = rows
        self._snapshot_search = dict(search)
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

    def _emit_changed(self, write_id: str | None = None, rid=None) -> None:
        """Send a `board_changed`, in batches when its upserts do not fit one message (#7M6E).

        A cleanup, a `git pull` or an import can move every card on the board at once, and one
        line of all of them is exactly what overflowed the GUI's read buffer on `board_open`.
        The first batch rides on the `board_changed` itself — so `removed`, `problems` and
        `config` are never delayed — and the rest follow as `board_cards`, which the pane patches
        in the same way.
        """
        event = self._changed(write_id)
        if rid is not None:
            event["id"] = rid
        batches = _row_batches(event.get("upserts") or [])
        if len(batches) <= 1:
            self.emit(event)
            return
        self.emit({**event, "upserts": batches[0], "more": True})
        for index, batch in enumerate(batches[1:], 1):
            self._send({"event": "board_cards", "id": event.get("id"), "rev": event.get("rev"),
                        "cards": batch, "more": index < len(batches) - 1})

    def _problems(self) -> list[dict]:
        """The board's format problems, from what `_rows()` already parsed (#7M6E).

        This ran a second full `board.check()` — 72 of the 179 ms a `board_refresh` cost at 337
        cards, and every millisecond of it a re-read of files `_rows()` had just read.  The
        per-file half comes out of the parse cache now; only the two answers that depend on the
        board as a whole are recomputed, and neither reads a file.  With no cache yet (nobody has
        asked for rows) it falls back to the full walk, which is what `check()` has always been.
        """
        try:
            board = self._need().board
            if self._check_state is None:
                problems = board.check()
            else:
                cached, by_id, thread_paths = self._check_state
                ids = set(by_id)
                problems = list(cached)
                for name in thread_paths:
                    problems.extend(board.check_thread_name(Path(name), ids))
                problems.extend(board.check_whole_board(by_id))
                problems.sort(key=lambda p: (p.path, p.code))
            return [{"code": p.code, "path": p.path, "message": p.message, "severity": p.severity}
                    for p in problems][:100]
        except (B.BoardError, OSError) as exc:                  # pragma: no cover - unreadable tree
            return [{"code": "check_failed", "path": "", "message": str(exc), "severity": "error"}]

    def _search(self, request: dict, rid) -> None:
        """The pane's filter bar, answered over the snapshot this worker already holds (#7M6E).

        The owner's rule stands (2026-09-19: "switchboard filter bar should be full text
        search"), and so do its semantics — every word must match, case does not matter, and a
        word matches the row's own fields *or* the card's body and thread.  What changed is where
        it runs: each card's text used to ride on every row for this one substring test, which
        was 92.6 % of `board`'s bytes and 30–80 ms of GUI thread per keystroke.  Only the words
        come here and only the matching ids go back; the pane still decides `status:`, `label:`,
        `folder:`, `@` and `#` itself, from the row, so those cost nothing and never wait.
        """
        query = request.get("query")
        if query is not None and not isinstance(query, str):
            raise ValueError("board_search query must be text.")
        terms = str(query or "").split()
        # Link terms (#EE42, PROJECT-BOARD-DESIGN §4.3): `links:#ID`, `skill:x`, `case:c-…`,
        # `run:r-…` select through the link index, not the text. The pane sends them here as
        # plain words already (`board::Model::plainTerms` scopes only label:/status:/…).
        linked: set[str] | None = None
        words = []
        for term in terms:
            chosen = (self._link_index().matching(term)
                      if term.startswith(BL.FILTER_PREFIXES) else None)
            if chosen is None:
                words.append(term.lower())
            else:
                linked = chosen if linked is None else linked & chosen
        ids = [card_id for card_id, parts in self._search_index.items()
               if all(any(term in part for part in parts) for term in words)
               and (linked is None or card_id in linked)]
        self._send({"event": "board_search", "id": rid, "query": query or "", "ids": sorted(ids)})

    def _link_index(self) -> "BL.LinkIndex":
        """The link index for the filter box, rebuilt when the snapshot moved (`rev`). The
        `board_links` request itself always rebuilds it from the files."""
        cached = getattr(self, "_link_cache", None)
        if cached is None or cached[0] != self.rev:
            cached = (self.rev, BL.build(self._need().board))
            self._link_cache = cached
        return cached[1]

    def _links(self, request: dict, rid) -> None:
        """`board_links` (protocol 19.25, #EE42): both directions of every link an address
        takes part in, from an index computed from the board's files on this call and written
        nowhere. `address` or `addresses` (≤ MAX_LINK_ADDRESSES) ask per address; `dangling:
        true`, or no address at all, adds every edge whose target resolves to nothing."""
        addresses = request.get("addresses")
        if request.get("address") is not None:
            addresses = [request.get("address")]
        if addresses is None:
            addresses = []
        if (not isinstance(addresses, list) or len(addresses) > MAX_LINK_ADDRESSES
                or not all(isinstance(a, str) and a.strip() for a in addresses)):
            raise ValueError(f"board_links takes `address`, or `addresses`: at most "
                             f"{MAX_LINK_ADDRESSES} non-empty strings.")
        wanted = []
        for raw in addresses:
            address = BL.normalize(raw)
            if address is None:
                raise ValueError(f"board_links: {raw!r} is not an address (#ID, skill:<id>, "
                                 "case:<id>, run:<id>, <path>@<sha>, a commit or a path).")
            wanted.append(address)
        index = BL.build(self._need().board)
        self._link_cache = (self.rev, index)
        event = {"event": "board_links", "id": rid, "edges": len(index.edges),
                 "items": [index.query(address) for address in dict.fromkeys(wanted)]}
        if request.get("dangling") or not wanted:
            event["dangling"] = index.dangling()[:MAX_DANGLING]
        self._send(event)

    def _triage(self, request: dict, rid) -> None:
        """Local suggestions now; an optional chores-role answer later, never a write."""
        text = request.get("text")
        if not isinstance(text, str) or not text.strip() or len(text) > 3000:
            raise ValueError("board_triage needs 1–3000 characters of draft text.")
        title = request.get("title", text)
        if not isinstance(title, str) or not title.strip() or len(title) > 200:
            raise ValueError("board_triage title must be 1–200 characters.")
        semantic = request.get("semantic", False)
        if type(semantic) is not bool:
            raise ValueError("board_triage semantic must be true or false.")
        tools = self._need_ready()
        cards = [card for card in tools.board.cards()
                 if card.id and card.type == "work" and card.status not in ("done", "dropped")]
        local = tools.duplicates(title, text, cards, threshold=0.40)
        self._send({"event": "board_triage", "id": rid, "phase": "local",
                    "duplicates": [item for item in local if item["score"] >= 0.66],
                    "related": [item for item in local if item["score"] < 0.66]})
        if not semantic:
            return
        from . import board_triage
        agent = getattr(self.turns, "agent", None)
        if agent is None:
            self._send({"event": "board_triage", "id": rid, "phase": "semantic",
                        "duplicates": [], "related": [], "tab": "", "labels": []})
            return
        try:
            provider = agent.side_provider(cheap=True, role="chores",
                                           max_tokens=board_triage.MAX_TOKENS)
        except Exception:
            self._send({"event": "board_triage", "id": rid, "phase": "semantic",
                        "duplicates": [], "related": [], "tab": "", "labels": []})
            return
        # The model sees a bounded set: nearest local matches first, then the remaining open
        # titles. It can only cite those IDs, and the GUI still asks the owner to select links.
        by_id = {card.id: card for card in cards}
        ordered = [by_id[item["id"]] for item in local if item["id"] in by_id]
        local_ids = {card.id for card in ordered}
        ordered.extend(card for card in sorted(cards, key=lambda item: str(item.front.get("created") or ""),
                                                reverse=True) if card.id not in local_ids)
        summaries = [{"id": card.id, "title": card.title[:120]}
                     for card in ordered[:board_triage.MAX_CARDS]]
        tabs = [str(tab["id"]) for tab in tools.board.tabs() if tab.get("folder")]
        labels = sorted({"feature", "bug"}
                        | {label for card in cards for label in (card.front.get("labels") or [])})
        root = str(tools.board.root)
        threading.Thread(target=board_triage.run,
                         args=(provider, rid, f"Title: {title}\nIssue: {text}", summaries, tabs, labels,
                               lambda event: self.emit({"root": root, **event})),
                         name="relay-board-triage", daemon=True).start()

    # ---- the tests (protocol section 31) --------------------------------------
    def _tests(self):
        """The `tests_*` handlers for the board this worker is pointed at (#7BM4).

        Made on first use and cached against (project, board root), so a `set_board` that moves
        this worker to another project answers about that project's tests; held as one attribute
        rather than two fields in `__init__` for the same reason `observe_protocol` caches its
        router that way — this is the only code that touches it. The emit is `_send`, so every
        event carries the `root` a GUI routes by; the job table is the tests' own, so stopping a
        run cannot reach a command the agent left running.
        """
        tools = self._need()
        key = (str(tools.board.repo), str(tools.board.root))
        cached = getattr(self, "_tests_cache", None)
        if cached is None or cached[0] != key:
            from . import tests_protocol as TP
            commands = TP.TestsCommands(tools.board.repo, tools.board.root, self._send)
            # #AQ6X step 7b: how this project's signal threads are started. Only the *worker's*
            # instance gets it — the agent's own (`board_tools._tests`) is built elsewhere and
            # must not start background agents from inside a tool call.
            commands.spawn_agent = self._spawn_signal_thread
            cached = (key, commands)
            self._tests_cache = cached
        return cached[1]

    def _spawn_signal_thread(self, task: str, description: str):
        """Start one signal thread on this worker's subagents (#AQ6X step 7b).

        `(thread_id, agent_id, session_id, done_event)`, or None when this worker cannot run one —
        no manager, subagents not configured yet, or the live ceiling reached. Each of those is a
        reason to leave the signal unclaimed for the next fold, not an error: `SignalThreads`
        treats None exactly that way.

        `background=False` is deliberate and is the whole difference from an `agent` tool call.
        A background subagent's result is handed to the main agent and can wake a turn
        (`subagents._handoff_locked`); a signal thread reports to nobody — it fixed the test or it
        did not, and the *check* says which. So it runs with no waiter and no handoff, and what
        the person sees is the notification and the Sessions row, which is decision 9's "visible".
        """
        manager = self.subagents
        if manager is None:
            return None
        from . import signal_threads as ST
        try:
            sub = manager.spawn({"subagent_type": ST.THREAD_AGENT_TYPE, "background": False,
                                 "description": description, "prompt": task},
                                signal=description)
        except (ValueError, TypeError, OSError):
            return None                      # not configured, or too many already live
        return sub.thread_id, sub.id, sub.owner_session or "", sub.done

    def _tests_gate(self, request: dict, rid) -> bool:
        """The Check gate on leaving `needs-verification` (#7BM4, #PR4Q).  True when it refused.

        The owner's decision, 2026-09-20: Check "is a gate on leaving `needs-verification`, with
        a recorded override".  What it blocks on is `tests_protocol.gate_move`'s answer: a
        listed check whose status for **this revision** is `failed` or `missing-evidence`, with
        no live override for that (check, revision) pair.

        Two holes and one question.  An `override` string lets the move through and is quoted
        onto the thread by `_record_override`, which also records the markers that stop the same
        override being asked for twice.  Any failure of the check itself — no discovery, no
        build directory, an unreadable store — lets the move through: a gate that fires when its
        own evidence is missing is a gate that stops work for reasons nobody can act on.  And a
        card with **no `## Tests` section** is asked, once, which checks prove it: the refusal
        carries `code: "tests_none"` and a second attempt goes through (#PR4Q; Codex's review
        §C: "a card without `## Tests` is ungated. That rewards omitting evidence").
        """
        self._override_note = None
        status = request.get("status")
        if not isinstance(status, str) or status not in TP_GATE_TO:
            return False
        override = request.get("override")
        has_override = isinstance(override, str) and bool(override.strip())
        try:
            card_id = normalize_id(request.get("card") or "")
            card = self._need().board.card_by_id(card_id)
            if card is None or card.status != TP_GATE_FROM:
                return False
            blocked = self._tests().gate_move(card_id, status)
        except Exception:                                    # the check's own trouble, not the card's
            return False
        if not blocked:
            return False
        if has_override:
            # The move goes through, and what it waived is recorded per check and revision so
            # the same override is never asked for again while it holds.
            self._override_note = {"card": blocked["card"], "tests": blocked.get("tests") or [],
                                   "revision": blocked.get("revision") or ""}
            return False
        self.emit({"event": "error", "id": rid,
                   "code": blocked.get("code") or "tests_gate",
                   "text": blocked["message"], "card": blocked["card"],
                   "card_id": blocked["card"], "status": status,
                   "tests": blocked["tests"], "findings": blocked["findings"],
                   "statuses": blocked.get("statuses") or [],
                   "revision": blocked.get("revision") or ""})
        return True

    def _record_override(self, request: dict, card_id, author: str) -> None:
        """A move that carried an `override` is a decision, so the thread quotes it verbatim.

        It also carries the markers `tests_protocol.live_overrides` reads: one per check the
        gate named, scoped to the revision the card is at and expiring in
        `tests_protocol.OVERRIDE_DAYS` days.  Nothing is recorded when the gate did not refuse —
        an override on a move that would have gone through anyway waives nothing.
        """
        override = request.get("override")
        if not isinstance(override, str) or not override.strip() or not card_id:
            return
        reason = override.strip()[:2000]
        note = getattr(self, "_override_note", None)
        note = note if isinstance(note, dict) else None
        self._override_note = None
        status = str(request.get("status") or "")
        try:
            if note and normalize_id(str(note.get("card") or "")) == normalize_id(str(card_id)):
                from . import tests_protocol as TP
                text = TP.override_entry_text(note.get("tests") or [],
                                              str(note.get("revision") or ""), reason, status)
            else:
                text = (f'Moved to `{status}` with the Check gate overridden: "{reason}"')
            self._need().board.append_thread(str(card_id), text,
                                             author=author or "owner", kind="decision")
        except Exception:                                    # pragma: no cover - defensive
            pass

    # ---- the profile (protocol section 31.9) ----------------------------------
    def _profile(self):
        """The `profile_*` handlers for the project this worker is pointed at (#7BM4 phase 5).

        Cached against the project exactly as `_tests` is, and for the same reason: a `set_board`
        that moves this worker profiles the project it moved to. Its job table is its own, so
        stopping a profile cannot reach a test run or a command the agent left going.
        """
        tools = self._need()
        key = str(tools.board.repo)
        cached = getattr(self, "_profile_cache", None)
        if cached is None or cached[0] != key:
            from . import profile_protocol as PP
            cached = (key, PP.ProfileCommands(tools.board.repo, self._send))
            self._profile_cache = cached
        return cached[1]

    # ---- Try it (protocol section 31.10) --------------------------------------
    def _tryit(self):
        """The `try_*` handlers (#JNYN): one bounded agent turn that opens a card's situation.

        Unlike `_profile` this is **not** cached against the project, and it holds no board of
        its own: it is handed `self._need`, so a `set_board` moves it with the worker, and
        `self.turns`, because Try it is an agent turn on this worker exactly as a cleanup is.
        """
        commands = getattr(self, "_tryit_commands", None)
        if commands is None:
            from . import tryit_protocol as TI
            commands = TI.TryItCommands(self._need, self.turns, self._send)
            self._tryit_commands = commands
        return commands

    # ---- dispatch -------------------------------------------------------------
    def dispatch(self, request: dict) -> bool:
        kind = request.get("type")
        if kind not in TYPES:
            return False
        rid = request.get("id")
        if kind == "board_open":
            tools = self._need()
            self._snapshot = self._rows()
            self._snapshot_search = dict(self._search_index)
            self.rev += 1
            # The rows in batches, the first with the `board` event itself (#7M6E): one line of
            # every card overflowed the GUI's 8 MiB read buffer at about 1,160 cards and killed
            # the worker, and the pane showed "Loading the Board…" for ever.
            batches = _row_batches(list(self._snapshot.values()))
            self._send({"event": "board", "id": rid, "rev": self.rev,
                        "root": str(tools.board.root), "workspace": str(tools.board.repo),
                        "project": tools.project, "state": tools.state, "exists": tools.exists(),
                        "config": self._config(), "cards": batches[0] if batches else [],
                        "cards_total": len(self._snapshot), "more": len(batches) > 1,
                        "problems": self._problems()})
            for index, batch in enumerate(batches[1:], 1):
                self._send({"event": "board_cards", "id": rid, "rev": self.rev,
                            "cards": batch, "more": index < len(batches) - 1})
            # A board `board_init` just created gets the survey as the page agent's opening turn
            # (19.18): the marker file is what makes it fresh, so an old board is never surveyed.
            self._maybe_survey(tools)
        elif kind == "board_refresh":
            self._emit_changed(rid=rid)
        elif kind == "board_search":
            self._search(request, rid)
        elif kind == "board_links":
            self._links(request, rid)
        elif kind == "board_triage":
            self._triage(request, rid)
        elif kind == "board_check":
            section = request.get("section")
            if section is not None and not isinstance(section, str):
                raise ValueError("board_check section must be a column id.")
            # Against the tree as it is now: `_problems` reads what `_rows` leaves behind, and a
            # Check pressed long after the last refresh must not answer from a stale parse.
            self._rows()
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
            # Desktop rendering needs the whole document: Tests and Try it may follow a
            # long plan. The agent-facing board_read keeps its separate context limit.
            if result.get("body_truncated"):
                card = tools.board.card_by_id(result["id"])
                if card is not None:
                    result["body"] = card.body
                    result["body_truncated"] = False
            self._send({"event": "board_card", **result, "card_id": result["id"], "id": rid})
        elif kind in ("board_create", "board_update", "board_move", "board_priority",
                      "board_delete", "board_comment", "board_claim"):
            self._write(kind, request, rid)
        elif kind == "board_undo":
            tools = self._need()
            try:
                result = tools.undo(str(request.get("write_id") or ""))
            except BoardToolError as exc:
                raise ValueError(str(exc)) from exc
            self._send({"event": "board_undone", **result, "card_id": result["id"], "id": rid})
            self._emit_changed()
        elif kind == "board_ask":
            self._ask(request, rid)
        elif kind in ("board_chat", "board_chat_cancel", "board_chat_queue_remove",
                      "board_chat_queue_move"):
            # Retired by #AGNT. Still recognised for a release so a GUI one version behind is
            # told what to send instead of being handed "Unknown protocol message".
            raise ValueError(RETIRED_CHAT)
        elif kind == "board_cancel":
            self._cancel_card(request, rid)
        elif kind == "board_resume":
            self._resume_card(request, rid)
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
        elif kind in TESTS_TYPES:
            self._tests().dispatch(request)
        elif kind in PROFILE_TYPES:
            self._profile().dispatch(request)
        elif kind in TRYIT_TYPES:
            # A Try it turn owns this worker's turn supervisor for as long as it runs, exactly as
            # a cleanup does, so it waits for whatever is already on it. `try_stop` and
            # `try_answer` never wait: one ends a run and the other only writes a card.
            if kind == "try_run" and not self._tryit().running() \
                    and self._busy_error(rid, "Try it"):
                return True
            self._tryit().dispatch(request)
        return True

    # ---- who may start a turn --------------------------------------------------
    def _busy_error(self, rid, what: str, card_id: str | None = None, *,
                    queues: bool = False) -> bool:
        """What may start now (19.16).  True when it refused, with `board_busy` sent.

        Turns on **different cards run at the same time**, each on its own agent and
        conversation (`relay_core.board_turns`) — as many as the owner clicks; the concurrent
        cap was removed the day after it landed (owner, 2026-09-19).  `queues=True` is the
        caller — `board_ask`, and only it — that has a queue to put this on: since card #CTRN a
        card's second prompt waits in the §12 strip, can be steered, withdrawn and reordered,
        and runs when the first finishes, instead of being refused.  Everything else is refused
        rather than queued:

        * a **write to a card a turn is running on** — a delete or a priority change under a
          running writer would race it, and there is no queue for a file write;
        * **anything while a cleanup runs**, and a cleanup while anything runs — a cleanup
          merges, splits and moves cards across the whole board, including the ones being
          talked about;
        * **anything while the *console's* own turn runs** — that conversation can write any
          card (19.18).  A console's own prompts never reach here: they queue.

        The GUI shows the refusal and offers Stop; `board_cancel {card}` stops one card's turn.
        """
        cleanup = self._cleanup_log is not None
        # What has work on it, not only what is mid-turn: a card whose prompt was accepted a
        # heartbeat ago has no running turn yet, and a delete must not slip through that gap.
        running_cards = self.cards.working_cards()
        if cleanup:
            running, busy_card = "a Board cleanup", None
        elif self.console and bool(getattr(self.turns, "busy", False)):
            # The console's own prompts never reach here — they queue, which is what a pane's
            # queue is for. Everything else waits, because the console can write any card.
            running, busy_card = "the Board console's turn", None
        elif card_id is not None and card_id in running_cards and not queues:
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
                   "text": f"The Board agent is busy with {running}. Stop it first, then "
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
                    **({"related": request["related"]} if request.get("related") else {}),
                    **({"not_duplicate_of": request["not_duplicate_of"]}
                       if request.get("not_duplicate_of") else {}),
                    **({"source": request["source"]} if request.get("source") else {}),
                    **({"section": request["section"]} if request.get("section") else {})})
            elif kind == "board_update":
                patch = request.get("patch")
                if not isinstance(patch, dict):
                    raise ValueError("board_update takes a patch object.")
                result = tools.run("board_update_card",
                                   {"id": request.get("card"), "base_hash": request.get("base_hash"), **patch})
            elif kind == "board_move":
                # The Check gate (#7BM4): a card does not leave `needs-verification` while the
                # tests it names are gone, have never run, or last failed — unless the move
                # carries an `override` that says why, which is then quoted on the thread.
                if self._tests_gate(request, rid):
                    return
                result = tools.run("board_move_card", {
                    "id": request.get("card"), "reason": request.get("reason") or "moved in the Board",
                    # `section` rides along even when it is the empty string: that is how a drop
                    # on a status column takes a card out of the manual section it was parked in.
                    **{k: request[k] for k in ("status", "tab", "before", "after", "evidence", "section")
                       if request.get(k) is not None}})
            elif kind == "board_priority":
                # The flag click on a row (#VKFV): the owner at the keyboard, so no base_hash and
                # no run() budget — BoardTools.set_priority is that path, with undo of its own.
                try:
                    result = tools.set_priority(str(request.get("card") or ""),
                                                request.get("priority"))
                except (BoardToolError, B.BoardError) as exc:
                    result = {"error": str(exc), "code": getattr(exc, "code", "board_refused")}
            elif kind == "board_delete":
                # The trash button and the Delete key (card #CYM9): the owner's confirmed delete,
                # with the same standing as the flag click — no base_hash, no run() budget, and
                # never offered to the agent (board_policy.md, "Nothing is deleted"). Refused while anything
                # that could be writing this card runs: a turn on it, a cleanup, the page agent,
                # a sync — deleting under a running writer would race it.
                try:
                    card_id = normalize_id(request.get("card") or "")
                    if self._forge_busy(rid, "the delete") or \
                            self._busy_error(rid, "the delete", card_id):
                        return
                    result = tools.delete_card(card_id, request.get("reason") or "")
                except (BoardToolError, B.BoardError) as exc:
                    result = {"error": str(exc), "code": getattr(exc, "code", "board_refused")}
            elif kind == "board_claim":
                # Execute (19.10, 19.19): the card goes to the terminal pane whose token this
                # message carries, and the claim is the three writes Execute used to send by
                # hand. That token is the pane's, not this worker's — the Board worker has
                # no pane of its own — so it overrides the one `configure` gave the tools for as
                # long as this one call runs.
                note = request.get("text")
                held, tools.pane_token = tools.pane_token, (
                    check_pane_token(request.get("pane_token")) or tools.pane_token)
                try:
                    result = tools.run("board_claim", {
                        "id": request.get("card"),
                        **({"note": note} if isinstance(note, str) and note.strip() else {}),
                        **({"force": True} if request.get("force") else {})})
                finally:
                    tools.pane_token = held
                # The whole card comes back for the *model's* benefit (it is the block a turn
                # would otherwise read); the GUI re-reads the card from `board_changed`, so the
                # block is not sent down the pipe with the reply.
                result.pop("card", None)
                # Execute on a card with no `## Done means` (#WC3E): a one-sentence notice on the
                # board, and the claim goes through. The expectations belong on the card *before*
                # the work, so that a verifying session checks something it did not choose itself
                # -- but a card handed to a pane without them is still worth doing, and the owner
                # decided Execute warns rather than refuses.
                if notice := self._done_means_notice(result.get("id")):
                    result["notice"] = notice
            else:
                # `pane_token` (#HKAP): the pane Execute handed the card to, so the thread entry
                # can link back to it. Absent (or empty) on every other comment.
                result = tools.run("board_comment", {"id": request.get("card"),
                                                     "kind": request.get("kind") or "note",
                                                     "text": request.get("text"),
                                                     **{k: request[k] for k in ("pane_token",)
                                                        if request.get(k)}})
        finally:
            tools.context.actor = "owner"
        if result.get("error"):
            self.emit({"event": "error", "id": rid, "text": result["error"],
                       "code": result.get("code"), **{k: v for k, v in result.items()
                                                      if k in ("current_hash", "possible_duplicates")}})
            return
        if kind == "board_move":
            self._record_override(request, result.get("id"), author)
        self._send({"event": "board_written", **result, "card_id": result.get("id"),
                    "id": rid, "kind": kind})
        self._emit_changed(result.get("write_id"))

    def _done_means_notice(self, card_id) -> str:
        """The Execute warning for a card with no `## Done means` (#WC3E), or "".

        Read from the card as it is *after* the claim, so a card that gained the section in the
        same breath does not warn. Any trouble reading it is no notice at all: this is a nudge,
        and a nudge that fires on its own broken plumbing teaches people to ignore it.
        """
        try:
            card = self._need().board.card_by_id(normalize_id(str(card_id or "")))
        except Exception:                                    # pragma: no cover - defensive
            return ""
        if card is None or card.type != "work":
            return ""
        wanted = DONE_MEANS_HEADING.strip().lower()
        if any(h.split(" (", 1)[0].strip().lower() == wanted for h in section_headings(card.body)):
            return ""
        return (f"#{card.id} has no Done means; the verifier will have nothing to check "
                "against.")

    def _ask_to_initialize(self, kind: str, request: dict, rid) -> None:
        """A write reached a project with no Board: ask the user, and park the write.

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

    # ---- the Board agent -------------------------------------------------
    def _ask(self, request: dict, rid) -> None:
        tools = self._need()
        card_id = normalize_id(request.get("card"))
        mode = request.get("mode") or "discuss"
        if mode not in CARD_MODES:
            raise ValueError(f"board_ask mode must be one of {', '.join(CARD_MODES)}.")
        text = request.get("text")
        if text is None and mode in ("plan", "refine"):
            text = ""                       # Plan and Refine need no words: the card is the brief
        if not isinstance(text, str) or len(text) > MAX_ASK_TEXT or (mode == "discuss" and not text.strip()):
            raise ValueError(f"board_ask text must be 1-{MAX_ASK_TEXT} characters"
                             + (" (it may be empty for a plan or a refine)." if mode == "discuss" else "."))
        text = text.strip()
        # Checked before the question is appended, so a refused ask leaves no trace on the card.
        if self._busy_error(rid, {"plan": "the plan", "refine": "the refinement"}.get(mode, "the question"),
                            card_id=card_id, queues=True):
            return
        card = tools.board.card_by_id(card_id)
        if card is None:
            raise ValueError(f"no card #{card_id} on this board.")
        card_hash = B.file_hash(card.path)
        # The owner's message is part of the record before the agent ever sees it. The mode goes
        # with it, so the thread reads "Plan ·" / "Discuss ·" in Relay and `mode=plan` in the file.
        said = text or ("Refine this card." if mode == "refine" else "Plan this card.")
        entry = tools.board.append_thread(card_id, said, author=str(request.get("author") or "owner"),
                                          kind="comment", private=card.private, mode=mode)
        # The stage move the event makes (#3XZV): starting a Plan is `planning`, and any other
        # first thread entry is `discussing`. The board's own bookkeeping must never block the
        # ask, so a refusal here is swallowed and the turn runs from wherever the card is.
        # Refine makes no move at all (#6W9X): it checks the request before anything is decided,
        # so an inbox card stays in the inbox.
        try:
            if mode != "refine":
                tools.stage_advance(card_id, "plan-started" if mode == "plan" else "discussed")
        except (BoardToolError, B.BoardError, OSError):
            pass
        card = tools.board.card_by_id(card_id) or card
        card_hash = B.file_hash(card.path) if card.path else card_hash
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
        # The card's own queue (19.16, card #CTRN). A prompt sent while this card is busy waits
        # in the §12 strip instead of being refused; what the mode may touch is enforced for the
        # length of the turn by `Agent.set_card_turn`, which opens the `CardScope` on this card's
        # own tools when the turn starts and closes it when it ends.
        self._sync_card_model(self.cards.session(card_id))
        self.cards.submit(card_id, mode, prompt, rid, seed_hash=card_hash, preview=said)

    def card_queue(self, surface):
        """The card queue a `surface` names (`card:<ID>`), or None (protocol 33, card #CTRN).

        `worker.py` asks this of every queue op and of `cancel`: a card turn is an ordinary
        supervised turn, so the strip drawn on a card operates that card's queue and not the
        tab's. A surface that names no card, or a card with no live session, is None and the
        worker's own supervisor answers — which is every message sent before this card.
        """
        card_id = board_turns.card_of_surface(surface)
        return self.cards.queue_of(normalize_id(card_id)) if card_id else None

    def _cancel_card(self, request: dict, rid) -> None:
        """`board_cancel {card}`: stop one card's turn (19.16), or every card turn without one.

        The worker-wide `cancel` stops the pane agent's turn — here, a cleanup. A card turn runs
        on its own agent, so stopping it needs to name the card; stopping a plan on #A must not
        stop the one on #B.
        """
        card_id = normalize_id(request.get("card")) if request.get("card") else ""
        stopped = self.cards.stop(card_id) if card_id else bool(self.cards.stop_all())
        # `cards` is what is *still* running (19.16), and a turn that was just told to stop is
        # not: it stays `active` for the moment its thread takes to unwind, and this answer goes
        # out inside that moment. A phone is sent none of a card turn's own events (remote
        # protocol 17.4), so this list is the only thing that puts its lamp out — it stayed lit
        # for good on the first Stop of #SWPH's hosted drive.
        running = self.cards.running_cards()
        if stopped:
            running = [card for card in running if card != card_id] if card_id else []
        self._send({"event": "board_cancelled", "id": rid, "card_id": card_id or None,
                    "stopped": stopped, "cards": running})

    def _resume_card(self, request: dict, rid) -> None:
        """`board_resume {card}`: run that card's queue again after a Stop (#7JD1).

        `resume_queue` (12.5) by the road a device can reach, the way `board_cancel` is `cancel`'s:
        a device's requests carry no `surface`, so the op that names a queue names it as a card.
        Enter on an empty prompt box is what sends it — at the desk, on a card's console and on a
        phone alike (owner, 2026-09-21: "why don't we just copy the functionality and have enter
        resume") — and a device's next `board_ask` resumes the same queue without it.

        `resumed` says whether there was a pause to lift, because a phone asks blind: it is sent
        none of a queue's state (remote protocol 17.4), so it cannot know before it sends and has
        to be told after.
        """
        card_id = normalize_id(request.get("card")) if request.get("card") else ""
        if not card_id:
            raise ValueError("board_resume names the card whose queue to run again.")
        queue = self.cards.queue_of(card_id)
        resumed = bool(queue is not None and queue.resume())
        self._send({"event": "board_resumed", "id": rid, "card_id": card_id, "resumed": resumed})

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
        if tools is None or not self.console or self.surveyed:
            return
        if self.turns is None or bool(getattr(self.turns, "busy", False)):
            return
        from . import board_import as I
        from . import project_probe as PP
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
        # The survey is an ordinary turn of this console's own conversation since #AGNT: it
        # queues behind whatever is running, it is stopped by `cancel`, and its events are the
        # pane's. `readonly` is the one thing that makes it different, and it is a flag on the
        # turn rather than a second code path — nothing is written until the owner answers.
        self.surveyed = True
        self.seeded = True          # the survey carries the board's shape itself
        try:
            self.turns.submit(prompt, "queue", rid, surface="switchboard", readonly=True)
        except ValueError as exc:
            board_chat.mark_survey(tools.board, "pending")   # try again on the next open
            self.surveyed = False
            self.emit({"event": "error", "id": rid, "text": str(exc)[:2000]})
            return
        board_chat.mark_survey(tools.board, "done", note="survey turn queued")

    # ---- board_cleanup: one agent turn over the whole board ----------------------
    def _cleanup(self, request: dict, rid) -> None:
        """`board_cleanup`: the agent tidies the whole board in one turn (protocol 19.9).

        The same machinery as `board_ask` — one turn on the Board worker, the ordinary
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
            raise ValueError("The Board agent has no board tools here "
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
            self._emit_changed()
        except (B.BoardError, OSError):                     # pragma: no cover - unreadable tree
            pass

    def observe(self, event: dict) -> dict:
        """Tag and record the Board agent's turn; called before every emit."""
        if event.get("event") in ("done", "error", "cancelled") and self._pending_board is not None:
            # A `set_board` that arrived mid-turn lands here, once the turn that kept the old
            # tool set has finished with it.
            self._settle_pending_board()
        if self._cleanup_log is not None:
            return self._observe_cleanup(event)
        # A Try it turn (31.10) runs on this worker too, and its events belong to the run rather
        # than to the card's chat: `TryItCommands.observe` tags them and streams the progress.
        tryit = getattr(self, "_tryit_commands", None)
        if tryit is not None and tryit.running():
            return tryit.observe(event)
        # A card turn is no longer one of *this* agent's turns (19.16): it runs on the card's own
        # agent, and `relay_core.board_turns` tags and collects it there. Only a cleanup still
        # comes through here.
        return event

    def _observe_cleanup(self, event: dict) -> dict:
        """Tag a cleanup turn's events so the pane shows them on the board, not on a card."""
        log = self._cleanup_log
        name = event.get("event")
        # The run's turn id, from the first event that carries one. It used to read
        # `turn_started` first; nothing emits that event, so the `status` branch was always the
        # one doing the work and the name is gone with #AGNT.
        if name == "status" and self._ask_turn is None:
            self._ask_turn = event.get("turn_id") or self._ask_turn
        if name in ("delta", "answer") and isinstance(event.get("text"), str):
            self._ask_text.append(event["text"])
        # A cleanup is many steps, and the model says something between most of them. A blank
        # line at each tool call keeps those remarks apart in the report and the changelog,
        # instead of running the whole run's commentary into one paragraph.
        if name == "tool_started" and self._ask_text and not self._ask_text[-1].endswith("\n\n"):
            self._ask_text.append("\n\n")
        if name in ("delta", "answer", "done", "error", "cancelled", "turn_summary", "thinking",
                    "thinking_done", "tool_started", "tool_result", "status"):
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
    head = ["[Board cleanup]",
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
    label = CARD_MODE_TITLES.get(mode, "Discuss")
    head = f"[{label} · #{card_id}]\n{brief}"
    if mode in ("plan", "refine"):
        return head + ("\n\nThe owner adds, verbatim:\n" + text if text else "")
    return head + "\n\nThe owner says:\n" + text


def seed_block(board: B.Board, card: B.Card) -> str:
    """The card, as the Board agent sees it at the start of a conversation."""
    entries = sorted(board.thread(card.id, card.private), key=lambda e: e.entry_id)
    tail = entries[-SEED_THREAD_ENTRIES:]
    body = card.body[:SEED_BODY_BYTES]
    lines = [f"[Board card #{card.id} — {card.path.name}]",
             "You are Relay's Board agent. You are talking to the user about this one card. "
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
                    "label": f"Board card #{card_id}, referenced by the user as #{card_id}"})
    return out
