# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Switchboard *page* agent: one conversation about the whole board (protocol 19.18).

The board worker already runs three kinds of agent turn: a pane's own turns, a card's Discuss
or Plan (19.16, one agent per card) and the whole-board cleanup (19.9).  This is the fourth:
the agent that lives on the Switchboard's main page, with every card as its context by default,
for the questions that are about the board rather than about one card — reorganizing it,
merging duplicates, moving cards between sections, explaining what is where.

It is a conversation, not a run.  It persists across turns like a card session does, and a
second prompt typed while it is answering **queues** (#N8VK's FIFO semantics, the same rule the
terminal panes use): the queue is worker-side and authoritative, the page shows it, and the
next prompt starts when the turn ends.  It never refuses a prompt of its own the way a cleanup
does; other board work (a cleanup, a card turn) still refuses *it*, because it can write any
card.

Its model is the `switchboard` role (13.1), resolved when its agent is built; the page's model
picker can name another one for later turns without losing the conversation.

The **survey** is its opening turn on a freshly created board (owner decision, 2026-09-19): what
`project_probe` found offline, what `board_import.propose` would turn into cards, the probe's
`hints` as the things Relay leaves alone — narrated by the agent under a read-only scope, so
nothing is written until the owner answers.  Whether a board is fresh is `<board
folder>/survey-state.json`, which `board_init` leaves `pending` and the first survey turn
settles: boards created before this existed have no file and are never surveyed.
"""
from __future__ import annotations

import json
import os
import secrets
import threading
import time
from pathlib import Path

from . import board as B

#: Prompts waiting for the page agent, past which a message is refused rather than queued. The
#: pane's queue has no cap because a person is watching it fill; a board page can be left open.
MAX_QUEUE = 20

#: How many cards the opening roster lists (the cleanup's cap, for the same reason).
MAX_ROSTER = 400

#: Conversation entries kept for a pane that opens mid-conversation. The agent itself holds the
#: real messages; this is what a reopened page is shown.
MAX_HISTORY = 60

#: Turn events tagged with `chat: true` so the page routes them to its own panel (the idiom
#: `cleanup: true` and `card_id` already use). `context` rides along so the page's context-left
#: chip follows the conversation like a pane's does.
CHAT_TAGGED = ("delta", "done", "error", "cancelled", "turn_summary", "turn_started", "thinking",
               "thinking_delta", "thinking_done", "tool_started", "tool_result", "status",
               "context")

TERMINAL = ("done", "error", "cancelled")

#: The survey's state file, beside `import-state.json` in the board folder. Absent means a board
#: from before the survey existed (or one whose survey ran): never surveyed again. Only
#: `board_init` writes `pending`.
SURVEY_FILE = "survey-state.json"


def survey_path(board: B.Board) -> Path:
    return board.root / SURVEY_FILE


def survey_state(board: B.Board) -> str | None:
    """`pending`, `done` or None (no file: a board that predates the survey)."""
    try:
        data = json.loads(survey_path(board).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    state = data.get("state") if isinstance(data, dict) else None
    return state if state in ("pending", "running", "done") else None


def mark_survey(board: B.Board, state: str, *, note: str = "") -> None:
    """Write the survey state. Best effort: an unwritable board folder must not break a turn."""
    payload = {"version": 1, "state": state, "changed": time.strftime("%Y-%m-%dT%H:%M:%S")}
    if note:
        payload["note"] = note[:400]
    try:
        survey_path(board).write_text(json.dumps(payload, indent=1) + "\n", encoding="utf-8")
    except OSError:
        pass


def chat_brief() -> str:
    """The page agent's brief (`board_chat_brief.md`), beside the policy and the other briefs."""
    path = Path(__file__).resolve().parent / "board_chat_brief.md"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:                                        # pragma: no cover - packaging slip
        return ""
    import re
    return re.sub(r"<!--.*?-->", "", text, flags=re.S).strip()


def roster(tools) -> list[str]:
    """One line per card, the cleanup's roster shape: the board fits in the opening prompt and
    the agent reads the cards it means to touch with `board_read`."""
    lines: list[str] = []
    for card in sorted(tools.board.cards(), key=lambda c: (B._status_order(c), c.rank or "zzzz")):
        if card.id is None:
            continue
        if len(lines) >= MAX_ROSTER:
            lines.append(f"… {MAX_ROSTER} cards listed; read the rest with board_list.")
            break
        labels = ",".join(str(l) for l in (card.front.get("labels") or [])) or "-"
        lines.append(f"#{card.id} [{card.status}] {card.title} · labels {labels} · "
                     f"{card.path.relative_to(tools.board.root)}")
    return lines


def chat_prompt(tools, text: str) -> str:
    """The opening prompt: the brief, the board's shape, the roster, and the owner's words."""
    config = tools.board.config()
    head = ["[Switchboard page agent]",
            f"Board: {tools.board.root} — every card is listed below; this conversation is "
            f"about the board as a whole.",
            "Sections (board.yaml columns): " + ", ".join(str(c) for c in config.get("columns") or []),
            "Category folders (board.yaml tabs): "
            + ", ".join(f"{t.get('id')}={t.get('folder') or t.get('filter')}" for t in tools.board.tabs()),
            "",
            chat_brief(),
            "",
            "--- the board today ---"]
    return "\n".join(head + roster(tools) + ["--- end of the board ---", "", text.strip()])


def survey_prompt(root: Path, project: Path, probe: dict, proposals: list[dict]) -> str:
    """The survey's opening prompt: what the probe found, what an import would create, the hints.

    All of it is data the worker gathered offline; the agent's job is to present it, say what
    Relay leaves alone, and ask — never to write, which its read-only scope enforces anyway.
    """
    git = probe.get("git") or {}
    trackers = probe.get("trackers") or []
    lines = ["[Switchboard survey]",
             f"A Switchboard was just created for {project} (board folder: {root}).",
             "This is the board's opening turn: survey what the project already tracks, offer to "
             "convert it into cards, and write nothing until the owner says yes.",
             ""]
    if trackers:
        lines.append(f"Found {probe.get('counts', {}).get('items', 0)} item(s) in "
                     f"{len(trackers)} tracker(s):")
        for finding in trackers[:20]:
            lines.append(f"- {finding.get('kind')}: {finding.get('path')} "
                         f"({finding.get('count', len(finding.get('items') or []))} item(s))"
                         + (" — truncated" if finding.get("truncated") else ""))
    else:
        lines.append("No existing tracker was found (no TODO.md, backlog/, issues list, specs…). "
                     "Say so in a sentence; an empty board is a fine answer.")
    if proposals:
        lines.append("")
        lines.append(f"An import would create {len(proposals)} card(s):")
        for item in proposals[:50]:
            lines.append(f"- {item.get('title')} — from {item.get('source', {}).get('kind')}: "
                         f"{item.get('source', {}).get('path')}")
    hints = probe.get("hints") or []
    if hints:
        lines.append("")
        lines.append("Relay deliberately leaves these alone (report them as such):")
        for hint in hints[:10]:
            lines.append(f"- {hint.get('message') or hint.get('kind') or hint}")
    primary_name = git.get("primary")
    row = next((r for r in git.get("remotes") or [] if r.get("name") == primary_name), {})
    if git.get("is_repo") and row:
        why = f" ({git.get('primary_reason')})" if git.get("primary_reason") else ""
        lines.append("")
        lines.append(f"The project is a git repository; its primary remote{why} is "
                     f"{row.get('name')}: {row.get('url')}.")
        if str(row.get("forge") or "") == "github" and row.get("owner") and row.get("repo"):
            lines.append(f"Its issues are on GitHub: https://github.com/{row['owner']}/{row['repo']}/issues "
                         "— offer to look there for an issues corpus, but only as a link for now: "
                         "two-way sync is not built, so nothing is fetched or synced without the "
                         "owner asking for it in some other way.")
    lines += ["", chat_brief(), "",
              "Present the survey in a few lines: what was found, what an import would create, "
              "what Relay leaves alone, and the GitHub link if there is one. Then ask which parts "
              "to convert. This turn writes nothing — the read-only scope refuses every write "
              "tool — and the owner's answer is the confirmation."]
    return "\n".join(lines)


class PageAgent:
    """One board's page-agent conversation: its agent, its queue, and the thread of its turn."""

    def __init__(self, emit, build, on_turn_end=None):
        #: The worker's raw emit: these events are tagged here and are not the pane agent's
        #: turns, exactly like a card turn's.
        self._emit = emit
        #: `build(emit) -> (agent, tools)`; the protocol object owns what an agent needs.
        self._build = build
        #: Called when a turn ends (`turn_id`, `survey`, `outcome`), for the survey state file.
        self._on_turn_end = on_turn_end
        self._lock = threading.RLock()
        self.agent = None
        self.tools = None
        #: The model the picker named: None (the `switchboard` role) or a preset id.
        self.model = None
        self._built_model = object()          # what the agent above was built on
        #: The provider model id the live agent was actually built on (`config.model`), as
        #: opposed to the *role* above.  A `configure` that repoints the `switchboard` role
        #: leaves `self.model` alone — the role is still the same one — so this is what tells
        #: the worker that the conversation is running on a model the owner has moved off
        #: (`invalidate()`, 19.18).
        self.built_model_id = None
        self.turn_id = None
        self.request_id = None
        self.thread = None
        self.active = False
        self.ended = False
        self.started = 0.0
        self.seeded = False                   # the roster went in with the first turn
        self.readonly = False                 # the survey turn: every write tool refuses
        self.survey = False                   # the turn now running is the survey
        self.surveyed = False                 # one survey per worker per board
        self.queue: list[dict] = []
        self.history: list[dict] = []
        self._counter = 0

    # ---- state -----------------------------------------------------------------
    def busy(self) -> bool:
        return self.active

    def seconds(self) -> float:
        return max(0.0, time.time() - self.started) if self.started else 0.0

    def _next_id(self) -> str:
        self._counter += 1
        return f"c{self._counter}"

    def state(self) -> dict:
        """What a page needs to draw the panel: the turn, the queue, the recent conversation."""
        with self._lock:
            return {"running": self.active, "turn_id": self.turn_id, "model": self.model,
                    "survey": self.survey and self.active, "seconds": round(self.seconds(), 1),
                    "queue": [{"id": item["id"], "text": item["text"]} for item in self.queue],
                    "history": list(self.history[-MAX_HISTORY:])}

    def _announce_state(self, **extra) -> None:
        self._emit({"event": "board_chat_state", "chat": self.state(), **extra})

    # ---- prompts -----------------------------------------------------------------
    def ask(self, text: str, request_id=None, *, prompt: str | None = None,
            readonly: bool = False, survey: bool = False) -> str:
        """Send `text`. Starts a turn, or joins the back of the queue while one runs.

        Returns the turn id when it started, or the queued item's id. Raises when the queue is
        full — the pane's queue never fills because a person watches it; this page can be left
        open, so a runaway sender is stopped rather than parked forever.
        """
        self._await_unwinding()
        with self._lock:
            if self.active:
                if len(self.queue) >= MAX_QUEUE:
                    raise ValueError(f"The page agent's queue already holds {MAX_QUEUE} prompts.")
                item = {"id": self._next_id(), "text": text, "request_id": request_id}
                self.queue.append(item)
                self._emit({"event": "board_chat_queued", "id": item["id"],
                            "position": len(self.queue), "text": text,
                            "request_id": request_id, "chat": True})
                self._announce_state()
                return "queued", item["id"]
            return "turn", self._start(text, request_id, prompt=prompt, readonly=readonly,
                                       survey=survey)

    def _start(self, text: str, request_id, *, prompt: str | None, readonly: bool,
               survey: bool) -> str:
        """Begin a turn now. The caller holds the lock and has checked `active`."""
        if self.agent is None or self._built_model != self.model:
            messages = None
            if self.agent is not None:
                # A model switch keeps the conversation (13.5's rule for a pane): the new agent
                # keeps its own system prompt and takes every other message across.
                messages = list(self.agent.messages[1:])
                try:
                    self.agent.stop()
                except Exception:                             # pragma: no cover - defensive
                    pass
            self.agent, self.tools = self._build(self.wrap(self._emit))
            if messages:
                self.agent.messages.extend(messages)
            self._built_model = self.model
            self.built_model_id = getattr(getattr(self.agent, "config", None), "model", None)
        # The opening turn carries the board; every later one is the owner's words alone, the
        # conversation being the context (the card sessions' seeding rule).
        if prompt is None:
            prompt = text if self.seeded else chat_prompt(self.tools, text)
        turn_id = f"chat-{secrets.token_hex(3)}"
        self.turn_id, self.request_id = turn_id, request_id
        self.active, self.ended = True, False
        self.started = time.time()
        self.readonly, self.survey = readonly, survey
        agent, tools = self.agent, self.tools
        tools.begin_turn(turn_id)
        tools.begin_chat_turn(readonly=readonly)
        self.history.append({"role": "owner", "text": text[:4000], "time": time.time()})
        self.thread = threading.Thread(target=self._run, args=(prompt, turn_id, agent, tools),
                                       name="relay-board-chat", daemon=True)
        self.thread.start()
        return turn_id

    def stop(self) -> bool:
        """Stop the running turn (the page's Stop button). The queue survives it."""
        with self._lock:
            agent = self.agent if self.active else None
        if agent is not None and getattr(agent, "stop", None) is not None:
            agent.stop()
            return True
        return False

    def invalidate(self) -> None:
        """Rebuild the agent on the next turn, keeping every message of the conversation.

        The picker on the page writes the `switchboard` role and reconfigures the worker
        (`onModelPick`), which never touches `self.model` — the conversation is still on the same
        *role*.  Without this the live agent would keep answering on the provider it was built
        with and the pick would look like it had done nothing.  `_start` already carries the
        messages across when it rebuilds, so a switch mid-conversation costs nothing but the
        system prompt.
        """
        with self._lock:
            self._built_model = object()

    def drop(self, wait: float = 2.0) -> None:
        """Forget the conversation: the worker was repointed, or is shutting down."""
        with self._lock:
            agent, thread = self.agent, self.thread
            self.agent = self.tools = None
            self.queue = []
            self.history = []
            self.seeded = self.surveyed = False
            self.active = False
            self._built_model = object()
            self.built_model_id = None
        if agent is not None and getattr(agent, "stop", None) is not None:
            agent.stop()
        if thread is not None and thread.is_alive():
            thread.join(wait)

    # ---- the queue ----------------------------------------------------------------
    def remove(self, item_id: str) -> bool:
        with self._lock:
            for index, item in enumerate(self.queue):
                if item["id"] == item_id:
                    del self.queue[index]
                    self._announce_state(removed=item_id)
                    return True
            return False

    def move(self, item_id: str, to: int) -> bool:
        with self._lock:
            index = next((i for i, item in enumerate(self.queue) if item["id"] == item_id), -1)
            if index < 0 or not isinstance(to, int):
                return False
            to = max(0, min(len(self.queue) - 1, to))
            self.queue.insert(to, self.queue.pop(index))
            self._announce_state(moved=item_id)
            return True

    # ---- internals -----------------------------------------------------------------
    def _await_unwinding(self, wait: float = 2.0) -> None:
        """A turn whose terminal event went out but whose thread has not unwound yet (the pane
        sends the next prompt the moment it sees `done`)."""
        with self._lock:
            thread = self.thread if self.active and self.ended else None
        if thread is not None and thread.is_alive():
            thread.join(wait)

    def _run(self, prompt: str | None, turn_id: str, agent, tools) -> None:
        outcome = "done"
        try:
            agent.cancel_event.clear()
            agent.ask(prompt, reset_cancellation=False, turn_id=turn_id)
        except Exception as exc:                            # ask() reports its own; defensive
            outcome = "error"
            self._emit({"event": "error", "chat": True, "turn_id": turn_id,
                        "text": f"The Switchboard page agent's turn failed ({type(exc).__name__})."})
        finally:
            try:
                tools.end_chat_turn()
            finally:
                with self._lock:
                    survey = self.survey
                    if self.survey:
                        self.surveyed = True
                # The turn's own state settles *before* it reads as idle. `_on_turn_end` is what
                # writes `survey-state.json` to `done`, and a page — or a test — that has just
                # seen the turn finish must not find the marker still saying `running`: it would
                # survey the board again on the next open, which is the one thing the marker
                # exists to prevent. It is a small file write, and the turn is over either way.
                if self._on_turn_end is not None:
                    try:
                        self._on_turn_end(turn_id, survey, outcome)
                    except Exception:                       # pragma: no cover - state file
                        pass
                with self._lock:
                    self.active = False
                    self.thread = None
                    self.seeded = True
                    self._announce_state(turn_id=turn_id)
                self._drain()

    def _drain(self) -> None:
        """The turn ended: start the next queued prompt, if any (#N8VK's FIFO)."""
        with self._lock:
            if self.active or not self.queue or self.agent is None:
                return
            item = self.queue.pop(0)
            text = item["text"]
            prompt = text if self.seeded else None
        try:
            self._start(text, item.get("request_id"), prompt=prompt, readonly=False, survey=False)
        except Exception as exc:                            # pragma: no cover - build failure
            self._emit({"event": "error", "chat": True, "text":
                        f"The queued prompt could not start ({type(exc).__name__})."})
            self._drain()
            return
        # Say so. The turn that just started has no `board_chat_started` of its own — that answers
        # a `board_chat` message, and this one came off the queue — so without this the page would
        # go on listing the running prompt as though it were still waiting its turn, right up
        # until the turn ended. Announced after `_start`, so the state carries the new turn id.
        self._announce_state()

    def wrap(self, emit):
        """The emit the agent's turn reports through: every event tagged with its turn."""
        def tagged(event: dict, emit=emit) -> None:
            name = event.get("event")
            if name in CHAT_TAGGED:
                event = {**event, "chat": True}
                if self.turn_id:
                    event = {**event, "turn_id": self.turn_id}
                if name in ("delta", "answer") and isinstance(event.get("text"), str):
                    self._collect(event["text"])
                if name in TERMINAL:
                    self._finish(event)
            emit(event)
        return tagged

    # The answer as it streams, appended to the history when the turn ends. Same shape as a card
    # turn's collection, for the same reason: the page can be closed while the agent answers.
    def _collect(self, text: str) -> None:
        with self._lock:
            if self.history and self.history[-1]["role"] == "agent" and self.history[-1].get("turn") == self.turn_id:
                self.history[-1]["text"] = (self.history[-1]["text"] + text)[:8000]
            else:
                self.history.append({"role": "agent", "text": text[:8000],
                                     "time": time.time(), "turn": self.turn_id})

    def _finish(self, event: dict) -> None:
        with self._lock:
            self.ended = True
            if event.get("event") != "done":
                if self.history and self.history[-1]["role"] == "agent":
                    self.history[-1]["text"] += f"  ({event.get('event')})"
