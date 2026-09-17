# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal session todo list maintained by the model through `update_todos`.

Each call replaces the whole list (opencode `todowrite` style). Items may link to request ledger ids
(`R<n>`); linked requests take their status from the todos (requests.RequestLedger.apply_todos).
Stored in the session and emitted as a `todos` event. Deliberately small: the user-facing task list is
designed separately (Switchboard integration).
"""
from __future__ import annotations

import copy
import re

STATUSES = ("pending", "in_progress", "completed", "cancelled", "deferred", "blocked")
OPEN = ("pending", "in_progress")
NEEDS_NOTE = ("cancelled", "deferred", "blocked")
MAX_ITEMS = 50
MAX_TEXT = 500
MAX_NOTE = 500
MAX_LINKS = 20
TODO_ID = re.compile(r"^T[1-9][0-9]{0,5}$")

SPEC = {"type": "function", "function": {
    "name": "update_todos",
    "description": ("Replace your todo list for this conversation (send the complete list every time). Use it when a "
                    "message contains more than one ask or a message arrives while you work. Link each todo to the "
                    "request ids (R<n>) it serves. Exactly one item in_progress while working. Mark completed only "
                    "after the work is actually done; cancelled, deferred and blocked need a note with the reason."),
    "parameters": {"type": "object", "properties": {
        "items": {"type": "array", "maxItems": MAX_ITEMS, "items": {"type": "object", "properties": {
            "id": {"type": "string", "description": "Existing todo id (T<n>) to keep; omit for a new todo."},
            "text": {"type": "string", "description": "The ask, quoted or closely paraphrased."},
            "status": {"type": "string", "enum": list(STATUSES)},
            "request_ids": {"type": "array", "items": {"type": "string"}, "description": "Linked requests, e.g. [\"R3\"]. Omit to link a new todo to the current turn's message. Listing several means one piece of work serves them all: add a new id here when a later message refines an ask this todo already covers."},
            "note": {"type": "string", "description": "Reason; required for cancelled, deferred and blocked."}},
            "required": ["text", "status"], "additionalProperties": False}}},
        "required": ["items"], "additionalProperties": False}}}

RULES = """

Requests and todos: Relay records every message the user sends as a request with an id (R<n>). Messages that arrive while you work are labelled with their id. When a message contains more than one ask, or a new message arrives while you are working, call update_todos before continuing: one todo per ask, with the ask quoted, linked with request_ids (todos you add without request_ids are linked to the message that started the current turn; the tool result shows the links). When a message changes, narrows or corrects an ask you already have a todo for, add its request id to that todo's request_ids instead of adding a todo; add a todo only for work that is genuinely new. Keep exactly one todo in_progress, mark each completed as soon as it is actually done, and keep going until every todo of the current turn's requests is completed, or cancelled, deferred or blocked with a reason. Do not end your turn with pending todos for those requests. Todos of an earlier request whose turn was stopped may stay pending until the user asks to continue it; do not cancel them on your own. Skip the list for a single simple ask."""


def validate(raw, known_request_ids, existing: list[dict], next_id: int,
             default_links: list[str] = ()) -> tuple[list[dict], int]:
    """Validated replacement list and the next free id number. Raises ValueError with a model-readable message."""
    if not isinstance(raw, dict) or set(raw) - {"items"} or not isinstance(raw.get("items"), list):
        raise ValueError("update_todos takes {items: [...]}.")
    items = raw["items"]
    if len(items) > MAX_ITEMS:
        raise ValueError(f"At most {MAX_ITEMS} todos.")
    existing_ids = {t["id"] for t in existing}
    out, seen, in_progress = [], set(), 0
    for index, item in enumerate(items, 1):
        where = f"Todo {index}"
        if not isinstance(item, dict) or set(item) - {"id", "text", "status", "request_ids", "note"}:
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
        if isinstance(todo_id, str) and TODO_ID.match(todo_id) and todo_id in existing_ids and todo_id not in seen:
            if not links:  # a resent todo without links keeps its earlier ones
                links = next(t["request_ids"] for t in existing if t["id"] == todo_id)
        else:
            if not links:  # a new todo without links belongs to the request that started this turn
                links = list(default_links)
            todo_id = f"T{next_id}"
            next_id += 1
        seen.add(todo_id)
        in_progress += status == "in_progress"
        out.append({"id": todo_id, "text": text.strip(), "status": status,
                    "request_ids": list(dict.fromkeys(links)), "note": note})
    if in_progress > 1:
        raise ValueError("Only one todo may be in_progress at a time.")
    return out, next_id


class TodoList:
    def __init__(self):
        self.items: list[dict] = []
        self.next_id = 1
        self.turn_id: str | None = None   # turn of the latest update

    def replace(self, raw, known_request_ids, turn_id: str | None, default_links: list[str] = ()) -> list[dict]:
        items, next_id = validate(raw, known_request_ids, self.items, self.next_id, default_links)
        self.items, self.next_id, self.turn_id = items, next_id, turn_id
        return self.items

    def open_items(self) -> list[dict]:
        return [t for t in self.items if t["status"] in OPEN]

    def snapshot(self) -> list[dict]:
        return copy.deepcopy(self.items)

    def restore(self, items) -> None:
        self.load_json({"items": items or [], "next_id": self.next_id})

    def event(self, turn_id: str | None = None) -> dict:
        return {"event": "todos", "turn_id": turn_id, "items": copy.deepcopy(self.items),
                "open": len(self.open_items())}

    def to_json(self) -> dict:
        return {"next_id": self.next_id, "turn_id": self.turn_id, "items": copy.deepcopy(self.items)}

    def load_json(self, data) -> None:
        if not isinstance(data, dict) or not isinstance(data.get("items"), list):
            raise ValueError("Invalid todo list.")
        items = []
        for raw in data["items"][:MAX_ITEMS]:
            if not isinstance(raw, dict) or not isinstance(raw.get("id"), str) or not TODO_ID.match(raw["id"]) \
                    or raw.get("status") not in STATUSES or not isinstance(raw.get("text"), str):
                raise ValueError("Invalid todo entry.")
            links = raw.get("request_ids") if isinstance(raw.get("request_ids"), list) else []
            items.append({"id": raw["id"], "text": raw["text"], "status": raw["status"],
                          "request_ids": [r for r in links if isinstance(r, str)],
                          "note": raw.get("note") if isinstance(raw.get("note"), str) else None})
        highest = max((int(t["id"][1:]) for t in items), default=0)
        nxt = data.get("next_id")
        self.items = items
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
