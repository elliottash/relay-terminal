# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal session todo list maintained by the model through `update_todos`.

Each call replaces the whole list (opencode `todowrite` style). Items may link to request ledger ids
(`R<n>`); linked requests take their status from the todos (requests.RequestLedger.apply_todos).
Stored in the session and emitted as a `todos` event. Deliberately small: the user-facing task list is
designed separately (Switchboard integration).

Todos and subagents (card #QHR1): a todo can be handed to a subagent, by the model (`agent` with
`todo_id`) or by the user (the `todo_subagent` command). The todo then records the subagent's id in
`subagent` and takes its status from it while it runs: in_progress while it runs, completed when it
finishes, blocked (with the error) when it fails, pending again (with a note) when it is stopped. A todo
a subagent is running is *delegated*: it does not count against the one-in_progress rule, the model's
update_todos cannot change its status, and the completion check does not hold the main turn open for it.
"""
from __future__ import annotations

import copy
import re
import threading

STATUSES = ("pending", "in_progress", "completed", "cancelled", "deferred", "blocked")
OPEN = ("pending", "in_progress")
NEEDS_NOTE = ("cancelled", "deferred", "blocked")
# What a subagent may take: open work, and work put off or blocked (a retry after a failed subagent).
DELEGABLE = ("pending", "in_progress", "deferred", "blocked")
MAX_ITEMS = 50
MAX_TEXT = 500
MAX_NOTE = 500
MAX_LINKS = 20
TODO_ID = re.compile(r"^T[1-9][0-9]{0,5}$")
SUBAGENT_ID = re.compile(r"^a[1-9][0-9]{0,5}$")

SPEC = {"type": "function", "function": {
    "name": "update_todos",
    "description": ("Replace your todo list for this conversation (send the complete list every time). Use it when a "
                    "message contains more than one ask or a message arrives while you work. Link each todo to the "
                    "request ids (R<n>) it serves. Exactly one item in_progress while working; a todo a subagent is working "
                    "on (it shows a subagent id) does not count and keeps the status Relay gives it until that subagent "
                    "ends. Mark completed only after the work is actually done; cancelled, deferred and blocked need a "
                    "note with the reason."),
    "parameters": {"type": "object", "properties": {
        "items": {"type": "array", "maxItems": MAX_ITEMS, "items": {"type": "object", "properties": {
            "id": {"type": "string", "description": "Existing todo id (T<n>) to keep; omit for a new todo."},
            "text": {"type": "string", "description": "The ask, quoted or closely paraphrased."},
            "status": {"type": "string", "enum": list(STATUSES)},
            "request_ids": {"type": "array", "items": {"type": "string"}, "description": "Linked requests, e.g. [\"R3\"]. Omit to link a new todo to the current turn's message. Listing several means one piece of work serves them all: add a new id here when a later message refines an ask this todo already covers."},
            "note": {"type": "string", "description": "Reason; required for cancelled, deferred and blocked."},
            "subagent": {"type": ["string", "null"], "description": "Read-only: the subagent working on this todo, as shown in the list. Set it by starting the subagent with agent(todo_id=...)."}},
            "required": ["text", "status"], "additionalProperties": False}}},
        "required": ["items"], "additionalProperties": False}}}

RULES = """

Requests and todos: Relay records every message the user sends as a request with an id (R<n>). Messages that arrive while you work are labelled with their id. When a message contains more than one ask, or a new message arrives while you are working, call update_todos before continuing: one todo per ask, with the ask quoted, linked with request_ids (todos you add without request_ids are linked to the message that started the current turn; the tool result shows the links). When a message changes, narrows or corrects an ask you already have a todo for, add its request id to that todo's request_ids instead of adding a todo; add a todo only for work that is genuinely new. Keep exactly one todo in_progress, mark each completed as soon as it is actually done, and keep going until every todo of the current turn's requests is completed, or cancelled, deferred or blocked with a reason. Do not end your turn with pending todos for those requests. Todos of an earlier request whose turn was stopped may stay pending until the user asks to continue it; do not cancel them on your own. To hand a todo to a subagent, start it with agent(todo_id="T<n>"): Relay then keeps that todo's status in step with the subagent (in_progress while it runs, completed or blocked when it ends), it does not count as your one in_progress todo, and you do not need to finish it yourself before ending your turn. Skip the list for a single simple ask."""


def validate(raw, known_request_ids, existing: list[dict], next_id: int,
             default_links: list[str] = (), delegated=frozenset()) -> tuple[list[dict], int]:
    """Validated replacement list and the next free id number. Raises ValueError with a model-readable message.

    `delegated`: ids of todos a subagent is running. A resent delegated todo keeps its status (and note);
    every resent todo keeps its subagent link. Neither counts towards the one-in_progress rule."""
    if not isinstance(raw, dict) or set(raw) - {"items"} or not isinstance(raw.get("items"), list):
        raise ValueError("update_todos takes {items: [...]}.")
    items = raw["items"]
    if len(items) > MAX_ITEMS:
        raise ValueError(f"At most {MAX_ITEMS} todos.")
    existing_ids = {t["id"] for t in existing}
    out, seen, in_progress = [], set(), 0
    for index, item in enumerate(items, 1):
        where = f"Todo {index}"
        if not isinstance(item, dict) or set(item) - {"id", "text", "status", "request_ids", "note", "subagent"}:
            raise ValueError(f"{where}: allowed fields are id, text, status, request_ids, note.")
        text = item.get("text")
        if not isinstance(text, str) or not text.strip() or len(text) > MAX_TEXT:
            raise ValueError(f"{where}: text must be 1-{MAX_TEXT} characters.")
        status = item.get("status")
        if status not in STATUSES:
            raise ValueError(f"{where}: status must be one of {', '.join(STATUSES)}.")
        note = item.get("note")
        if note is not None and (not isinstance(note, str) or len(note) > MAX_NOTE):
            raise ValueError(f"{where}: note must be text of at most {MAX_NOTE} characters.")
        note = note.strip() if isinstance(note, str) and note.strip() else None
        if status in NEEDS_NOTE and not note:
            raise ValueError(f"{where}: a {status} todo needs a note with the reason.")
        links = item.get("request_ids") or []
        if not isinstance(links, list) or len(links) > MAX_LINKS or not all(isinstance(r, str) for r in links):
            raise ValueError(f"{where}: request_ids must be a list of request ids such as \"R3\".")
        unknown = [r for r in links if r not in known_request_ids]
        if unknown:
            raise ValueError(f"{where}: unknown request id(s) {', '.join(unknown[:5])}.")
        todo_id = item.get("id")
        subagent = None   # the link is Relay's: only `agent(todo_id=...)` or the user sets it
        if isinstance(todo_id, str) and TODO_ID.match(todo_id) and todo_id in existing_ids and todo_id not in seen:
            before = next(t for t in existing if t["id"] == todo_id)
            if not links:  # a resent todo without links keeps its earlier ones
                links = before["request_ids"]
            subagent = before.get("subagent")
            if todo_id in delegated:   # its subagent owns the status until it ends
                status, note = before["status"], before.get("note")
        else:
            if not links:  # a new todo without links belongs to the request that started this turn
                links = list(default_links)
            todo_id = f"T{next_id}"
            next_id += 1
        seen.add(todo_id)
        in_progress += status == "in_progress" and todo_id not in delegated
        out.append({"id": todo_id, "text": text.strip(), "status": status,
                    "request_ids": list(dict.fromkeys(links)), "note": note, "subagent": subagent})
    if in_progress > 1:
        raise ValueError("Only one todo may be in_progress at a time.")
    return out, next_id


class TodoList:
    def __init__(self):
        self.items: list[dict] = []
        self.next_id = 1
        self.turn_id: str | None = None   # turn of the latest update
        # Todos a subagent is running: todo id -> subagent id. Live state of this worker, never saved.
        self.delegated: dict[str, str] = {}
        # Subagents update the list from their own threads; the list is replaced, never mutated in place.
        self._lock = threading.RLock()

    def replace(self, raw, known_request_ids, turn_id: str | None, default_links: list[str] = ()) -> list[dict]:
        with self._lock:
            items, next_id = validate(raw, known_request_ids, self.items, self.next_id, default_links,
                                      frozenset(self.delegated))
            self.items, self.next_id, self.turn_id = items, next_id, turn_id
            kept = {t["id"] for t in items}
            self.delegated = {k: v for k, v in self.delegated.items() if k in kept}
            return self.items

    def open_items(self, include_delegated: bool = True) -> list[dict]:
        return [t for t in self.items if t["status"] in OPEN and (include_delegated or t["id"] not in self.delegated)]

    # ----- subagents (card #QHR1) ---------------------------------------------------------------
    def check_delegable(self, todo_id) -> dict:
        """The todo a subagent may take, or ValueError with a model-readable reason."""
        with self._lock:
            if not isinstance(todo_id, str) or not TODO_ID.match(todo_id):
                raise ValueError("todo_id must be a todo id such as \"T3\".")
            todo = next((t for t in self.items if t["id"] == todo_id), None)
            if todo is None:
                raise ValueError(f"No todo {todo_id} in the list.")
            if todo_id in self.delegated:
                raise ValueError(f"Todo {todo_id} is already being worked on by subagent {self.delegated[todo_id]}.")
            if todo["status"] not in DELEGABLE:
                raise ValueError(f"Todo {todo_id} is {todo['status']}; a completed or cancelled todo cannot go to a subagent.")
            return dict(todo)

    def _set(self, todo_id: str, **fields) -> bool:
        items, changed = [], False
        for todo in self.items:
            if todo["id"] == todo_id and any(todo.get(k) != v for k, v in fields.items()):
                todo = {**todo, **fields}
                changed = True
            items.append(todo)
        self.items = items
        return changed

    def subagent_started(self, todo_id: str, agent_id: str) -> bool:
        """A subagent starts (or resumes) work on a todo: link it and mark it in_progress."""
        with self._lock:
            if not any(t["id"] == todo_id for t in self.items):
                return False
            if self.delegated.get(todo_id, agent_id) != agent_id:
                return False   # another subagent has it now (a resumed one does not take it back)
            self.delegated[todo_id] = agent_id
            return self._set(todo_id, status="in_progress", subagent=agent_id, note=None)

    def subagent_finished(self, agent_id: str, outcome: str, error: str | None = None) -> bool:
        """The subagent's run ended: its todo takes the outcome. False when no todo follows it (any more)."""
        with self._lock:
            todo_id = next((k for k, v in self.delegated.items() if v == agent_id), None)
            if todo_id is None:
                return False
            del self.delegated[todo_id]
            if outcome == "done":
                fields = {"status": "completed", "note": None}
            elif outcome == "failed":
                reason = " ".join(str(error or "no reason given").split())
                fields = {"status": "blocked", "note": f"Subagent {agent_id} failed: {reason}"[:MAX_NOTE]}
            else:   # stopped: nobody did the work, so it is open again
                fields = {"status": "pending", "note": f"Subagent {agent_id} was stopped before it finished."}
            return self._set(todo_id, **fields)

    def snapshot(self) -> list[dict]:
        with self._lock:
            return copy.deepcopy(self.items)

    def restore(self, items) -> None:
        """Rewind to an earlier list. Subagents still running keep the todos they are linked to."""
        with self._lock:
            delegated = dict(self.delegated)
            self.load_json({"items": items or [], "next_id": self.next_id})
            for todo_id, agent_id in delegated.items():
                if any(t["id"] == todo_id and t.get("subagent") == agent_id for t in self.items):
                    self.delegated[todo_id] = agent_id
                    self._set(todo_id, status="in_progress", note=None)

    def event(self, turn_id: str | None = None) -> dict:
        with self._lock:
            items = [{**t, "subagent_running": t["id"] in self.delegated} for t in self.items]
            return {"event": "todos", "turn_id": turn_id, "items": copy.deepcopy(items),
                    "open": len(self.open_items())}

    def to_json(self) -> dict:
        with self._lock:
            return {"next_id": self.next_id, "turn_id": self.turn_id, "items": copy.deepcopy(self.items)}

    def load_json(self, data) -> None:
        """Load a saved list. No subagent of a saved list is running: a todo one was still working on when
        the list was saved becomes pending again (restore() re-links the ones still running), link kept."""
        if not isinstance(data, dict) or not isinstance(data.get("items"), list):
            raise ValueError("Invalid todo list.")
        items = []
        for raw in data["items"][:MAX_ITEMS]:
            if not isinstance(raw, dict) or not isinstance(raw.get("id"), str) or not TODO_ID.match(raw["id"]) \
                    or raw.get("status") not in STATUSES or not isinstance(raw.get("text"), str):
                raise ValueError("Invalid todo entry.")
            links = raw.get("request_ids") if isinstance(raw.get("request_ids"), list) else []
            subagent = raw.get("subagent") if isinstance(raw.get("subagent"), str) and SUBAGENT_ID.match(raw["subagent"]) else None
            status, note = raw["status"], raw.get("note") if isinstance(raw.get("note"), str) else None
            if subagent and status == "in_progress":
                status, note = "pending", f"Subagent {subagent} is not running any more and had not finished this todo."
            items.append({"id": raw["id"], "text": raw["text"], "status": status,
                          "request_ids": [r for r in links if isinstance(r, str)],
                          "note": note, "subagent": subagent})
        highest = max((int(t["id"][1:]) for t in items), default=0)
        nxt = data.get("next_id")
        with self._lock:
            self.items = items
            self.delegated = {}
            self.next_id = max(nxt if type(nxt) is int and nxt > 0 else 1, highest + 1)
            self.turn_id = data.get("turn_id") if isinstance(data.get("turn_id"), str) else None


def no_list_reminder_text(tool_calls: int) -> str:
    """Nudge for a turn that has done real work without ever calling update_todos.

    `reminder_text` below only fires while todos are open, so a model that never starts a list gets
    no nudge at all. Nothing else covers that: the end-of-turn completion check counts a request as
    open only when an open todo points at it (agent._open_items), and a request with no todos is
    marked done because its turn ended normally (requests.RequestLedger.finish_turn). So a multi-part
    ask answered without a list is never checked. Card D8VN; measured on glm-5.3, which skipped the
    list on 2 of 12 runs of the same five-ask prompt.
    """
    return (f"[Relay reminder: this turn has made {tool_calls} tool calls and update_todos has not been "
            "used. If this request has several parts, call it now with one todo per part so the parts "
            "are tracked and none is missed; ignore this if it is a single simple ask.]")


def reminder_text(open_todos: list[dict], steps: int) -> str:
    listed = "; ".join(f'{t["id"]} "{t["text"][:80]}" ({t["status"]})' for t in open_todos[:8])
    return (f"[Relay reminder: update_todos has not been used for {steps} steps while todos are open: {listed}. "
            "If the list is out of date, update it; ignore this if it is current.]")
