# SPDX-License-Identifier: AGPL-3.0-or-later
"""The `app_*` tools: the agent drives the Relay app (card #FEJQ, protocol §30).

The Switchboard tools (`board_tools.BoardTools`) are the model this copies.  There the agent
changes *files*; here it changes the *app* — the rows of Options, the entries of the actions
palette, which pane is open and what it is zoomed to — and the app lives in the GUI process, so
every write is a round trip: the tool emits an `app_command` and blocks on the GUI's
`app_command_result` (§30.3), exactly as `BoardTools.init` (`BoardInit.ask_and_wait`) blocks on
the "initialize a Switchboard here?" dialog.  A tool result therefore says what *happened*,
never what was asked for.

What the GUI sends and what this module owns:

* the **catalog** — the option rows and the actions, with their current values — arrives in the
  `app` block of `configure` and is replaced by an `app_catalog` message (§30.2), the way the
  keybinding catalog does (`keybindings.KeybindingCatalog.from_request`).  Nothing here reads a
  settings file: the GUI owns the settings and this is its description of them, as fresh as the
  last message.  Nothing is cached across a refresh — a row that is missing from the new catalog
  has gone from the app.  Since #GMCF `app_action_list` answers from the **keybinding** catalog
  too: it gives each action's current keys, and the registry entries that have no palette row,
  because `set_keybinding`'s schema stopped listing them.
* the **policy** is in the catalog too, because it is the GUI's to decide: `writes_enabled`
  (Options › Agent, "Agents may change options and run actions" — owner decision 3 of
  2026-09-20), `settable` per row and `agent_safe` per action (decisions 1 and 2: every value
  row except a secret, and actions that are undoable in one click).
* the **change log** is this worker's own record of what it wrote, so `app_changes` can list it
  and `app_undo` can reverse one.  The GUI keeps the authoritative log and performs the undo
  itself (§30.6), so Undo works with no agent in the room; this is the agent's view of its own
  half of it.

Guardrails, in one place so they can be reviewed:

* **A secret is never read and never written.**  A row marked `secret` arrives with no `value`
  at all (§30.2), is listed without one, and `app_option_set` refuses it in one sentence before
  a request is built.  API keys are the keystore's and the owner's.
* **A row that is not a value is not settable.**  Button, buttons, info and heading rows are in
  the catalog with `settable: false` so the agent can *name* one and open the pane at it; the
  button itself is an action (`app_action_run`).
* **Values are validated against the catalog before the GUI is asked** (§30.3: the worker
  refuses `not_settable`, `secret`, `writes_disabled`, `not_agent_safe` and `invalid_value`
  itself, as 22.3 refuses a command `bash -n` rejects).  A refused value costs no round trip.
* **Only `agent_safe` actions run.**  The catalog marks them; the fence is the GUI's.
* **A pane-scoped action is aimed, not aimed at whatever has the focus.**  `app_action_run`
  takes an optional `pane` (#AG7R group 2, §30.3): with none, the GUI runs it on the pane whose
  worker asked — it knows which, because the command arrives with that pane's session token — and
  a helper, which has no pane of its own, goes on landing where the person is focused, as it
  always did.  `app_panes` is where the ids come from.
* **One agent making another act is announced, attributed and bounded.**  `app_send_prompt`
  submits a prompt in another pane as if the person had pressed Enter there and
  `app_prefill_prompt` only fills that pane's composer (#AG7R group 8, §30.3).  The GUI posts a
  notification for each — a pre-fill is visible where it lands, a send is not — writes the
  sending pane's name into the receiving pane's transcript, and refuses a pane sending to itself
  or a chain of agent-to-agent prompts that runs on past `would_loop`.  The fence is the GUI's,
  because only it can see the other panes.
* **Sessions search never leaves the worker.**  `app_sessions_search` asks the conversation
  index directly (`conv_index.ConversationIndex.search`, the protocol-14 `conversations`
  answer's own source), so asking "which session was that in" costs no GUI round trip.
"""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Callable

from . import guest_sessions

#: How long a tool waits for the GUI's `app_command_result` before giving up (§30.3: "within
#: 20 s or the tool returns `no_reply`" — 22.4's deadline, for a channel with 22.4's shape).
#: The board's own dialog has no timeout because a person is reading it; an `app_command` can
#: reach a GUI with no handler for it (an older build, a pane whose window is gone), and a turn
#: must not park forever on that.
ANSWER_TIMEOUT = 20.0

#: What an option row can be (§30.2).  The first four hold a value the agent may set; the rest
#: are rows it can name and open (a button is run through `app_action_run`, decision 1).
OPTION_KINDS = ("toggle", "choice", "text", "number", "button", "buttons", "info", "heading")
VALUE_KINDS = ("toggle", "choice", "text", "number")

#: What `app_open` can open.  The four panes the card is about; a card id opens the Switchboard,
#: and `conversation` resumes a saved conversation in a pane — the Sessions row's own Enter
#: (`SessionManager::onResume`), which until 2026-09-20 no tool could reach: the helper could
#: search the index and then only open the *list* at the search, so "open a group of previous
#: sessions in new panes" came back as a list the person had to click through (owner's report).
#:
#: The six after them are #AG7R group 8: every other pane Relay has had only an *action*, and two
#: of those actions were among group 1's unreachable twelve.  Each routes to the same window code
#: the action runs (`RelayWindow::openAppTarget`), so there is one way to open a pane and not two,
#: and the pane-scoped ones (`info`, `requests`, `activity`, `subagents`, `files`) follow the
#: `pane` aim of §30.3 rather than landing wherever the focus happens to be.
OPEN_TARGETS = ("options", "actions", "sessions", "switchboard", "conversation",
                "files", "tests", "activity", "info", "requests", "subagents")

#: How many conversations one `app_open {ids}` may open.  A group is a handful of panes, not a
#: window full: each one is a worker of its own.
MAX_OPEN_CONVERSATIONS = 8

#: The commands that go out as `app_command` (§30.3).  `list_panes` is a round trip and not a
#: field of the catalog: panes open and close between two catalogs, and an agent aiming at a pane
#: that has gone is the fault `pane` exists to fix (#AG7R group 2).
COMMANDS = ("open", "set_option", "run_action", "undo", "list_panes",
            "send_prompt", "prefill_prompt", "rename")

#: The `error` vocabulary of `app_command_result` (§30.3).  The worker's own refusals use the
#: same words, so a refusal reads the same whether the catalog caught it or the pane did.
ERRORS = ("unknown_row", "unknown_action", "unknown_target", "unknown_change",
          "unknown_conversation", "unknown_pane", "not_settable",
          "secret", "writes_disabled", "invalid_value", "not_agent_safe", "busy", "would_loop",
          "failed", "no_reply")

#: What each of those means in a sentence, for the tool result when the GUI sends the code alone.
ERROR_TEXT = {
    "unknown_row": "Relay no longer has that option row.",
    "unknown_action": "Relay no longer has that action.",
    "unknown_target": "Relay cannot open that.",
    "unknown_change": "Relay no longer holds that change.",
    "unknown_conversation": "Relay has no saved conversation with that id.",
    "unknown_pane": "Relay has no pane with that id any more; list the panes again.",
    "not_settable": "That row is not one an agent may set.",
    "secret": "That row holds a secret; no agent may set it.",
    "writes_disabled": "Agents may not change options or run actions.",
    "invalid_value": "Relay rejected that value.",
    "not_agent_safe": "That action is not one an agent may run.",
    "busy": "Relay was busy and did not do it.",
    "would_loop": "Relay stopped that prompt: agents may not drive each other in a ring.",
    "failed": "Relay could not do it.",
    "no_reply": "Relay did not answer.",
}

MAX_OPTIONS = 2000
MAX_ACTIONS = 1000
MAX_LABEL = 200
MAX_DETAIL = 1000
MAX_CHOICES = 200
MAX_TEXT_VALUE = 4096
MAX_LIST_ROWS = 60
MAX_SEARCH = 200
MAX_SESSION_ROWS = 25
MAX_PANES = 60
#: How long a prompt one agent sends another may be.  A prompt, not a document: anything longer
#: is a file to point at, and the receiving pane's transcript has to stay readable by the person.
MAX_PROMPT = 4000
MAX_CHANGES = 100


class AppToolError(ValueError):
    """A refusal the model should read and correct.  Carries a machine-readable code.

    Shaped like `board_tools.BoardToolError`: the dispatch turns it into a tool *result*, not an
    exception, so a refused write is one sentence in the transcript and the turn carries on.
    """

    def __init__(self, message: str, code: str = "failed", **extra):
        super().__init__(message)
        self.code = code
        self.extra = extra

    def to_result(self) -> dict:
        return {"error": str(self), "code": self.code, **self.extra}


def spec(name: str, description: str, properties: dict, required: list[str]) -> dict:
    return {"type": "function", "function": {"name": name, "description": description,
            "parameters": {"type": "object", "properties": properties, "required": required,
                           "additionalProperties": False}}}


def _short(value, limit: int = 80) -> str:
    text = "on" if value is True else "off" if value is False else "" if value is None else str(value)
    return text if len(text) <= limit else text[:limit - 1] + "…"


# ------------------------------------------------------------------ the catalog

def _text(value, what: str, maximum: int = MAX_LABEL, *, required: bool = False) -> str:
    if value is None:
        if required:
            raise AppToolError(f"{what} is required.", code="catalog")
        return ""
    if not isinstance(value, str):
        raise AppToolError(f"{what} must be text.", code="catalog")
    return value[:maximum]


@dataclass
class OptionRow:
    """One row of Options, as the GUI describes it (`settingsSections()` in RelayWindow, §30.2)."""

    id: str
    section: str = ""
    section_label: str = ""
    label: str = ""
    detail: str = ""
    kind: str = "info"
    value: object = None
    has_value: bool = False
    choices: list = field(default_factory=list)
    min: object = None
    max: object = None
    settable: bool = False
    secret: bool = False

    @property
    def path(self) -> str:
        """How a row is named in prose: "Agent › Agents may change options"."""
        head = self.section_label or self.section
        return f"{head} › {self.label}" if head else (self.label or self.id)

    def row(self) -> dict:
        """The listing shape.  A secret row is a row with no `value` at all (§30.2)."""
        out = {"id": self.id, "section": self.section, "label": self.label, "kind": self.kind,
               "settable": self.settable}
        if self.has_value:
            out["value"] = self.value
        if self.section_label:
            out["section_label"] = self.section_label
        if self.secret:
            out["secret"] = True
        return out

    def detail_row(self) -> dict:
        """`app_option_get`: the listing shape plus what a person would read beside the row."""
        out = self.row()
        if self.detail:
            out["detail"] = self.detail
        if self.kind == "choice":
            out["choices"] = [dict(c) for c in self.choices]
        if self.kind == "number":
            if self.min is not None:
                out["min"] = self.min
            if self.max is not None:
                out["max"] = self.max
        return out


@dataclass
class ActionRow:
    """One entry of the actions palette (`searchableActions()` in RelayWindow, §30.2)."""

    key: str
    section: str = ""
    label: str = ""
    detail: str = ""
    agent_safe: bool = False

    def row(self) -> dict:
        out = {"key": self.key, "section": self.section, "label": self.label,
               "agent_safe": self.agent_safe}
        if self.detail:
            out["detail"] = self.detail
        return out


def _choices(value, row_id: str) -> list[dict]:
    if value is None:
        return []
    if not isinstance(value, list) or len(value) > MAX_CHOICES:
        raise AppToolError(f"choices for {row_id} must be a list of at most {MAX_CHOICES} entries.",
                           code="catalog")
    out = []
    for entry in value:
        if not isinstance(entry, dict) or "value" not in entry:
            raise AppToolError(f"each choice for {row_id} must be an object with a value.",
                               code="catalog")
        out.append({"value": entry["value"], "label": _text(entry.get("label"), "choice label")})
    return out


class AppCatalog:
    """The GUI's description of Options and the actions palette, and what may be done with them.

    Parsed once per `configure` / `app_catalog` message and then read-only, except for the one
    value a successful `set_option` writes back: the catalog is the agent's picture of the app,
    and a picture that still showed the old value after the write landed would make the next
    `app_option_get` contradict the tool result the model has just read.
    """

    def __init__(self, value: dict):
        if not isinstance(value, dict):
            raise AppToolError("app must be an object with options and actions.", code="catalog")
        #: The tab's persistent id (§30.2): what keys the helper worker, and what tells a pane
        #: agent and the helper that they mean the same Options pane.
        self.tab = _text(value.get("tab"), "app.tab")
        self.writes_enabled = value.get("writes_enabled") is True
        self.options: dict[str, OptionRow] = {}
        self.actions: dict[str, ActionRow] = {}
        self._lock = threading.Lock()
        rows = value.get("options") or []
        if not isinstance(rows, list) or len(rows) > MAX_OPTIONS:
            raise AppToolError(f"app.options must be a list of at most {MAX_OPTIONS} rows.",
                               code="catalog")
        for entry in rows:
            row = self._option(entry)
            self.options[row.id] = row
        actions = value.get("actions") or []
        if not isinstance(actions, list) or len(actions) > MAX_ACTIONS:
            raise AppToolError(f"app.actions must be a list of at most {MAX_ACTIONS} entries.",
                               code="catalog")
        for entry in actions:
            action = self._action(entry)
            self.actions[action.key] = action

    # ---- parsing ---------------------------------------------------------------
    @staticmethod
    def _option(entry) -> OptionRow:
        if not isinstance(entry, dict):
            raise AppToolError("each app option must be an object.", code="catalog")
        row_id = _text(entry.get("id"), "option id", required=True)
        if not row_id:
            raise AppToolError("each app option needs an id.", code="catalog")
        kind = entry.get("kind")
        if kind not in OPTION_KINDS:
            raise AppToolError(f"option {row_id}: kind must be one of {', '.join(OPTION_KINDS)}.",
                               code="catalog")
        secret = entry.get("secret") is True
        # A secret row arrives with no value; it is given none here either, so nothing downstream
        # can leak one a future GUI sends by mistake.
        has_value = "value" in entry and not secret
        return OptionRow(
            id=row_id, section=_text(entry.get("section"), "section"),
            section_label=_text(entry.get("section_label"), "section_label"),
            label=_text(entry.get("label"), "label"),
            detail=_text(entry.get("detail"), "detail", MAX_DETAIL),
            kind=kind, value=entry.get("value") if has_value else None, has_value=has_value,
            choices=_choices(entry.get("choices"), row_id) if kind == "choice" else [],
            min=entry.get("min"), max=entry.get("max"),
            settable=entry.get("settable") is True and kind in VALUE_KINDS and not secret,
            secret=secret)

    @staticmethod
    def _action(entry) -> ActionRow:
        if not isinstance(entry, dict):
            raise AppToolError("each app action must be an object.", code="catalog")
        key = _text(entry.get("key"), "action key", required=True)
        if not key:
            raise AppToolError("each app action needs a key.", code="catalog")
        return ActionRow(key=key, section=_text(entry.get("section"), "section"),
                         label=_text(entry.get("label"), "label"),
                         detail=_text(entry.get("detail"), "detail", MAX_DETAIL),
                         agent_safe=entry.get("agent_safe") is True)

    @classmethod
    def from_request(cls, value) -> "AppCatalog | None":
        """The `app` block of `configure`, or of an `app_catalog` message.  None when absent.

        A worker that gets no block has no app tools at all, which is what every worker did
        before §30 (the section's "all additive").
        """
        if value is None:
            return None
        return cls(value)

    # ---- reading ---------------------------------------------------------------
    def sections(self) -> list[dict]:
        seen: dict[str, str] = {}
        for row in self.options.values():
            if row.section and row.section not in seen:
                seen[row.section] = row.section_label or row.section
        return [{"section": key, "label": label} for key, label in seen.items()]

    def find(self, row_id) -> OptionRow:
        if not isinstance(row_id, str) or not row_id.strip():
            raise AppToolError("id must be the option row's id, as app_option_list gives it.",
                               code="unknown_row")
        row = self.options.get(row_id.strip())
        if row is None:
            near = [r.id for r in self.options.values() if row_id.strip().lower() in r.id.lower()][:5]
            raise AppToolError(
                f"Relay has no option row {row_id!r}. Find it with app_option_list"
                + (f"; did you mean {', '.join(near)}?" if near else "."), code="unknown_row")
        return row

    def action(self, key) -> ActionRow:
        if not isinstance(key, str) or not key.strip():
            raise AppToolError("key must be the action's key, as app_action_list gives it.",
                               code="unknown_action")
        found = self.actions.get(key.strip())
        if found is None:
            near = [a.key for a in self.actions.values() if key.strip().lower() in a.key.lower()][:5]
            raise AppToolError(
                f"Relay has no action {key!r}. Find it with app_action_list"
                + (f"; did you mean {', '.join(near)}?" if near else "."), code="unknown_action")
        return found

    def rows(self, section=None, search=None, limit: int = MAX_LIST_ROWS) -> list[OptionRow]:
        section = (section or "").strip().lower()
        needle = (search or "").strip().lower()[:MAX_SEARCH]
        out = []
        for row in self.options.values():
            if section and section not in (row.section.lower(), (row.section_label or "").lower()):
                continue
            if needle and needle not in " ".join(
                    (row.id, row.label, row.section, row.section_label, row.detail)).lower():
                continue
            out.append(row)
            if len(out) >= limit:
                break
        return out

    def matching_actions(self, search=None, limit: int = MAX_LIST_ROWS) -> list[ActionRow]:
        needle = (search or "").strip().lower()[:MAX_SEARCH]
        out = []
        for action in self.actions.values():
            if needle and needle not in " ".join(
                    (action.key, action.label, action.section, action.detail)).lower():
                continue
            out.append(action)
            if len(out) >= limit:
                break
        return out

    def note_value(self, row_id: str, value) -> None:
        """A `set_option` landed: the catalog now shows what the app shows."""
        with self._lock:
            row = self.options.get(row_id)
            if row is not None and not row.secret:
                row.value, row.has_value = value, True

    # ---- validation ------------------------------------------------------------
    def check_value(self, row: OptionRow, value):
        """The value `set_option` would carry, or a refusal (§30.3: the worker's own check)."""
        if row.kind == "toggle":
            if type(value) is not bool:
                raise AppToolError(f"{row.path} is a switch: value must be true or false.",
                                   code="invalid_value")
            return value
        if row.kind == "choice":
            allowed = [c["value"] for c in row.choices]
            if value in allowed:
                return value
            # A model that read the labels rather than the values is corrected, not refused.
            for choice in row.choices:
                if isinstance(value, str) and value.strip().lower() in (
                        str(choice["value"]).lower(), str(choice.get("label") or "").lower()):
                    return choice["value"]
            names = ", ".join(repr(c) for c in allowed[:20]) or "(none offered)"
            raise AppToolError(f"{row.path} takes one of: {names}.", code="invalid_value")
        if row.kind == "number":
            if type(value) is bool or not isinstance(value, (int, float)):
                raise AppToolError(f"{row.path} is a number: value must be a number.",
                                   code="invalid_value")
            if isinstance(row.min, (int, float)) and value < row.min:
                raise AppToolError(f"{row.path} cannot go below {row.min}.", code="invalid_value")
            if isinstance(row.max, (int, float)) and value > row.max:
                raise AppToolError(f"{row.path} cannot go above {row.max}.", code="invalid_value")
            return value
        if row.kind == "text":
            if not isinstance(value, str):
                raise AppToolError(f"{row.path} is a text field: value must be text.",
                                   code="invalid_value")
            if len(value) > MAX_TEXT_VALUE:
                raise AppToolError(f"{row.path}: text is limited to {MAX_TEXT_VALUE} characters.",
                                   code="invalid_value")
            # §30.4: "a string with no control characters". A newline or an escape in a settings
            # field is a line the person never typed and cannot see they now have.
            if any(ch < " " or ch == "\x7f" for ch in value):
                raise AppToolError(f"{row.path} is a single-line field: the value may not contain "
                                   "control characters or line breaks.", code="invalid_value")
            return value
        raise AppToolError(f"{row.path} is not a value row.", code="not_settable")


# ------------------------------------------------------------------ the round trip

class AppBridge:
    """`app_command` out, `app_command_result` back — the one round trip (§30.3).

    `BoardInit` is the shape: the request goes out as an event with an id, the GUI answers on
    the protocol thread, and the turn thread that is waiting is woken.  Two differences, both
    because this is not a dialog a person is reading: there is a deadline (`ANSWER_TIMEOUT`),
    and Stop still ends it — the agent's `cancel_event` is watched exactly as `ask_and_wait`
    watches it.

    The command carries no tab or pane field: it travels down the asking worker's own pipe, so
    a pane agent's commands come out of that pane's worker and the helper's out of its tab's.
    """

    def __init__(self, emit: Callable[[dict], None], cancel: threading.Event | None = None,
                 timeout: float = ANSWER_TIMEOUT, clock: Callable[[], float] = time.monotonic):
        self.emit = emit
        #: The agent's `cancel_event`, set by the worker once there is an agent.
        self.cancel = cancel
        self.timeout = timeout
        self.clock = clock
        self._lock = threading.Lock()
        self._pending: dict[str, list] = {}
        self._next = 0

    def send(self, command: str, fields: dict) -> dict:
        """Emit one `app_command` and block until the GUI answers, or the deadline passes.

        Returns the GUI's result dict (`{ok, error?, previous?, value?, change_id?}`); raises
        `AppToolError(no_reply)` when nothing answered, so the caller reports it as a tool error.
        """
        with self._lock:
            self._next += 1
            command_id = f"ac-{self._next}"
            done = threading.Event()
            self._pending[command_id] = [done, None]
        # The command's own fields go in first: `id` on the event is the *request* id, the one
        # the result is matched by, and nothing a command carries may take that name from it.
        # (§30.1 writes the row id as `id` too, which no JSON object can hold twice; the row id
        # travels as `row`, which is what the GUI's executor reads — src/AppCommands.cpp.)
        self.emit({**fields, "event": "app_command", "id": command_id, "command": command})
        deadline = self.clock() + self.timeout
        while not done.wait(0.05):
            if self.cancel is not None and self.cancel.is_set():
                self._take(command_id)
                from .provider import Cancelled
                raise Cancelled("Stopped.")
            if self.clock() >= deadline:
                self._take(command_id)
                raise AppToolError(
                    f"Relay did not answer the {command} request within {int(self.timeout)}s, so "
                    "nothing was changed. Tell the user what you were trying to do and let them "
                    "do it.", code="no_reply")
        result = self._take(command_id)
        return result if isinstance(result, dict) else {"ok": False, "error": "failed"}

    def _take(self, command_id: str):
        with self._lock:
            entry = self._pending.pop(command_id, None)
        return entry[1] if entry else None

    def answer(self, reply: dict) -> dict:
        """`app_command_result {id, ok, error?, previous?, value?, change_id?}` from the GUI."""
        if not isinstance(reply, dict):
            raise ValueError("app_command_result must be an object.")
        command_id = reply.get("id")
        if not isinstance(command_id, str) or not command_id:
            raise ValueError("app_command_result needs the id of the app_command it answers.")
        if "ok" in reply and type(reply.get("ok")) is not bool:
            raise ValueError("app_command_result ok must be true or false.")
        with self._lock:
            entry = self._pending.get(command_id)
            if entry is None:
                # Late, or the turn was stopped: nothing is waiting for it.
                return {"id": command_id, "pending": False}
            entry[1] = dict(reply)
            entry[0].set()
        return {"id": command_id, "pending": True}

    def fail_pending(self) -> None:
        """Answer everything still waiting with a no (the worker is going, or was repointed)."""
        with self._lock:
            entries = list(self._pending.values())
            self._pending.clear()
        for entry in entries:
            entry[1] = {"ok": False, "error": "no_reply"}
            entry[0].set()


def _refused(result: dict, what: str) -> AppToolError:
    """The GUI said no: its code from the §30.3 vocabulary, plus whatever sentence it sent."""
    code = result.get("error")
    code = code if isinstance(code, str) and code in ERRORS else "failed"
    sentence = ""
    for key in ("text", "detail", "message"):
        if isinstance(result.get(key), str) and result[key].strip():
            sentence = result[key].strip()[:MAX_DETAIL]
            break
    if not sentence and isinstance(result.get("error"), str) and result["error"] not in ERRORS:
        sentence = result["error"][:MAX_DETAIL]
    return AppToolError(f"Relay refused {what}: {ERROR_TEXT.get(code, ERROR_TEXT['failed'])}"
                        + (f" {sentence}" if sentence else ""), code=code)


# ------------------------------------------------------------------ the tools

_ROW_ARG = {"type": "string", "description": "The row's id, exactly as app_option_list gives it."}

TOOL_SPECS = [
    spec("app_option_list",
         "List the rows of Relay's own Options, with their current values: the same rows the "
         "person sees in the Options pane. Use it to answer \"what is this set to\" and to find "
         "the id app_option_set needs. API keys and anything else the keyring holds are marked "
         "secret and are listed with no value at all.",
         {"section": {"type": "string", "description": "One section only, e.g. agent, appearance, privacy."},
          "search": {"type": "string", "description": "Case-insensitive text matched against the id, label, section and description."}},
         []),
    spec("app_option_get",
         "Read one option row in full: its value, what it is for, and — for a choice row — the "
         "values it accepts.",
         {"id": _ROW_ARG}, ["id"]),
    spec("app_option_set",
         "Change one option in Relay, as though the person had clicked it. The change is shown "
         "to them at once as \"Agent changed <row>: <before> → <after> · Undo\", so say what you "
         "changed and why in your reply rather than changing things quietly. Secrets (API keys) "
         "and rows that are not values cannot be set here; a button row is run with "
         "app_action_run.",
         {"id": _ROW_ARG,
          "value": {"description": "true/false for a switch, one of the row's choices for a "
                                   "choice, a number inside its range, or a single line of text."}},
         ["id", "value"]),
    spec("app_action_list",
         "List the actions Relay offers — the entries of its actions palette, and the shortcuts "
         "it can bind. `agent_safe` says whether you may run one: only the actions the person can "
         "undo in a click are, and the rest are listed so you can say where the button is. `keys` "
         "is the shortcut the action is on now, and the key set_keybinding takes.",
         {"search": {"type": "string", "description": "Case-insensitive text matched against the key, label and section."}},
         []),
    spec("app_action_run",
         "Run one of Relay's actions, as though the person had chosen it in the palette. Only "
         "actions app_action_list marks agent_safe can be run. An action that acts on one pane — "
         "its model, its reasoning effort, its input mode, plan mode, its own views — runs on "
         "your own pane unless you name another with `pane`.",
         {"key": {"type": "string", "description": "The action's key, exactly as app_action_list gives it."},
          "pane": {"type": "string",
                   "description": "The pane to act on, as app_panes gives it. Your own pane by "
                                  "default; the pane the person is looking at when you have none "
                                  "of your own."}},
         ["key"]),
    spec("app_panes",
         "List the panes of the Relay window you are in: the id `pane` takes, the title the "
         "person sees, the directory, the tab, the model and whether that pane's agent is busy. "
         "Your own pane is marked `you`. Read it before aiming an action at a pane that is not "
         "yours — the list is the only place the ids are.",
         {}, []),
    spec("app_send_prompt",
         "Send a prompt to another pane's agent, exactly as if the person had typed it there and "
         "pressed Enter. The pane's transcript shows it came from you, and the person is told. "
         "Use it to hand work to a pane that is already in the right directory or on the right "
         "model; say in your reply which pane you sent it to and why. If that pane's agent is "
         "mid-turn the prompt waits in its queue, as a second prompt of the person's own does. "
         "You cannot send to your own pane, and a chain of agents prompting each other is cut "
         "off, so do not use this to loop.",
         {"pane": {"type": "string", "description": "The pane to send to, as app_panes gives it."},
          "text": {"type": "string",
                   "description": "The prompt, as you would type it into that pane."}},
         ["pane", "text"]),
    spec("app_prefill_prompt",
         "Put a prompt into another pane's composer and leave it there, unsent, for the person to "
         "read, edit and send themselves. The gentler half of app_send_prompt: use it whenever "
         "the person should see the words before the other agent acts on them. It is refused if "
         "that composer already has something in it — the person's draft is never overwritten.",
         {"pane": {"type": "string", "description": "The pane whose composer to fill, as app_panes gives it."},
          "text": {"type": "string", "description": "The text to leave in the composer."}},
         ["pane", "text"]),
    spec("app_rename",
         "Name a pane or the tab it is in, the way /rename and /rename-tab do for the person — "
         "who can type those and you cannot. A name helps them find the pane again: \"deploy\", "
         "\"the failing test\". An empty name puts it back to the automatic one, which is also "
         "how a rename is undone.",
         {"what": {"type": "string", "enum": ["pane", "tab"],
                   "description": "pane renames the pane; tab renames the tab it sits in."},
          "name": {"type": "string",
                   "description": "The new name, or empty to go back to the automatic one."},
          "pane": {"type": "string",
                   "description": "Which pane, as app_panes gives it. Your own by default."}},
         ["what", "name"]),
    spec("app_sessions_search",
         "Search the person's past Relay conversations — the same index the Sessions pane uses. "
         "One row per conversation: id, title, when, model, workspace and the turns that matched. "
         "The `id` is what app_open {target: \"conversation\"} takes, so a search and an open "
         "are the two halves of \"open the sessions about X\". Answered inside Relay; nothing "
         "is sent anywhere.",
         {"query": {"type": "string", "description": "Words to look for. The Sessions pane's operators work here too: project:, file:, model:, branch:, before:, after:, is:, -word."},
          "limit": {"type": "integer", "minimum": 1, "maximum": MAX_SESSION_ROWS,
                    "description": f"How many conversations to return (default 10, at most {MAX_SESSION_ROWS})."}},
         ["query"]),
    spec("app_open",
         "Open one of Relay's panes for the person and zoom it to what you are talking about: "
         "Options or the actions palette at a section or a row, Sessions at a search, the "
         "Switchboard at a card, the file explorer, Test suites, Activity, \u24d8 (conversation "
         "info), the request ledger or the subagents of a pane — or, with target "
         "`conversation`, open a past conversation "
         "itself, which is what pressing Enter on a Sessions row does. `id` is a conversation's "
         "id from app_sessions_search; `ids` opens a group, each in its own pane, in the order "
         "given, and the result says what happened to each one. `new_pane` is true by default "
         "for `ids` and whenever the person said \"in new panes\"; pass false to load the "
         "conversation into the pane they are in, which replaces what that pane is holding. Use "
         "it instead of describing where a setting lives. It returns once the pane is open.",
         {"target": {"type": "string", "enum": list(OPEN_TARGETS),
                     "description": "options, actions, sessions, switchboard, conversation, "
                                    "files, tests, activity, info, requests or subagents."},
          "section": {"type": "string", "description": "Options/actions: the section to open at."},
          "row": {"type": "string", "description": "Options: the row id to reveal and highlight."},
          "query": {"type": "string", "description": "Sessions or actions: the search to open with."},
          "card": {"type": "string", "description": "Switchboard: a card id such as K7Q2 to open."},
          "id": {"type": "string",
                 "description": "conversation: the id of one conversation to open, as "
                                "app_sessions_search gives it."},
          "ids": {"type": "array", "items": {"type": "string"},
                  "description": f"conversation: up to {MAX_OPEN_CONVERSATIONS} conversation ids "
                                 "to open, each in a pane of its own, in this order."},
          "new_pane": {"type": "boolean",
                       "description": "conversation: open in a new pane (the default) rather "
                                      "than loading it into the pane the person is in."},
          "pane": {"type": "string",
                   "description": "The pane to open in or beside, as app_panes gives it. Your own "
                                  "by default \u2014 so for a conversation new_pane false means "
                                  "\"here\", and \u24d8, Activity, requests, subagents and the "
                                  "explorer open on the pane you name."}},
         ["target"]),
    spec("app_changes",
         "List the changes you have made to Relay in this session — each with the row, what it "
         "was, what it is now, and the change id app_undo takes.",
         {}, []),
    spec("app_undo",
         "Undo one of your own changes, by the change id app_changes (or the result of "
         "app_option_set) gives. The person can do the same from the notification Relay showed "
         "them.",
         {"change_id": {"type": "string", "description": "The change to reverse."}}, ["change_id"]),
]

TOOL_NAMES = tuple(s["function"]["name"] for s in TOOL_SPECS)

#: The two tools `writes_enabled` gates.  `app_open` is not one of them (§30.4: it is not a
#: write), and neither is `app_undo` (§30.4: putting a setting back is not a new write).
#: `app_send_prompt`, `app_prefill_prompt` and `app_rename` are writes too (#AG7R group 8): the
#: first two make another agent act or put words in front of the person, and a rename changes what
#: they see in the header — reversible by renaming back, which is why it is allowed at all.
WRITE_TOOLS = ("app_option_set", "app_action_run", "app_send_prompt", "app_prefill_prompt",
               "app_rename")


class AppTools:
    """The `app_*` tools for one worker: a console's and every pane agent's alike (§30.4).

    One instance per worker, shared by the worker's own agent and by the card conversations the
    board builds beside it (19.16), so `app_changes` is "what this worker changed" and not "what
    this one conversation changed".  What differs between those agents is the brief they are
    given, not the tools — #FEJQ's "one tool set" decision, and since #AGNT (§33.3) the rule for
    every agent: a context specialises an agent, it does not fence it.
    """

    def __init__(self, catalog: AppCatalog | None, bridge: AppBridge, *,
                 sessions: Callable[[], object] | None = None, workspace: str | None = None,
                 keybindings: Callable[[], object] | None = None,
                 clock: Callable[[], float] = time.time):
        self.catalog = catalog
        self.bridge = bridge
        #: Returns the live `keybindings.KeybindingCatalog`, or None where there is none.  Since
        #: #GMCF `set_keybinding`'s schema no longer lists the actions, so `app_action_list` is
        #: where an id and its current keys are found; it is read through a callable because a
        #: `keybindings` message replaces the catalog without replacing these tools.
        self.keybindings = keybindings
        #: Returns the shared `conv_index.ConversationIndex`, or None where there is none (a bare
        #: Agent in a test, `RELAY_INDEX=off`).
        self.sessions = sessions
        self.workspace = workspace
        self.clock = clock
        self._lock = threading.Lock()
        self.changes: list[dict] = []
        self._next_change = 0

    # ---- catalog ---------------------------------------------------------------
    def set_catalog(self, catalog: AppCatalog | None) -> None:
        """An `app_catalog` message (§30.2): the app's rows changed (a value, a row, the gate).

        The old catalog is dropped whole — nothing is carried across — so a row the GUI no longer
        sends is gone from the agent's view of the app, which is what it is.
        """
        self.catalog = catalog

    @property
    def tab(self) -> str:
        return self.catalog.tab if self.catalog is not None else ""

    def _need_catalog(self) -> AppCatalog:
        if self.catalog is None:
            raise AppToolError(
                "This pane cannot see Relay's own settings, so the app tools are not available "
                "here. Tell the user what to change and where.", code="failed")
        return self.catalog

    def _need_writes(self, what: str) -> AppCatalog:
        catalog = self._need_catalog()
        if not catalog.writes_enabled:
            raise AppToolError(
                f"\"Agents may change options and run actions\" is off in Options › Agent, so "
                f"{what} is refused. Say what you would change and let the user turn it on.",
                code="writes_disabled")
        return catalog

    # ---- dispatch, the BoardTools shape ----------------------------------------
    def tool_specs(self) -> list[dict]:
        return [dict(s) for s in TOOL_SPECS]

    def handles(self, name: str) -> bool:
        return name in TOOL_NAMES

    def preview(self, name: str, args: dict) -> str:
        """One human-readable block for the pane's tool line (no side effects)."""
        head = name.replace("app_", "").replace("_", " ").upper()
        if not isinstance(args, dict):
            return f"RELAY {head}"
        bits = [f"{key}: {_short(args[key], 120)}"
                for key in ("id", "value", "key", "pane", "target", "section", "row", "query",
                            "card", "search", "change_id", "what", "name", "text")
                if args.get(key) is not None]
        return f"RELAY {head}\n\n" + ("\n".join(bits) or "(no arguments)")

    def run(self, name: str, args: dict) -> dict:
        if not self.handles(name):
            raise AppToolError(f"unknown Relay app tool {name!r}")
        if not isinstance(args, dict):
            raise AppToolError("Tool arguments must be an object.")
        handler = {"app_option_list": self._option_list, "app_option_get": self._option_get,
                   "app_option_set": self._option_set, "app_action_list": self._action_list,
                   "app_action_run": self._action_run, "app_sessions_search": self._sessions_search,
                   "app_panes": self._panes,
                   "app_send_prompt": self._send_prompt,
                   "app_prefill_prompt": self._prefill_prompt, "app_rename": self._rename,
                   "app_open": self._open, "app_changes": self._changes, "app_undo": self._undo}[name]
        try:
            return handler(dict(args))
        except AppToolError as exc:
            return exc.to_result()

    # ---- options ---------------------------------------------------------------
    def _option_list(self, args: dict) -> dict:
        catalog = self._need_catalog()
        rows = catalog.rows(args.get("section"), args.get("search"))
        return {"rows": [row.row() for row in rows], "count": len(rows),
                "total": len(catalog.options), "sections": catalog.sections(),
                "writes_enabled": catalog.writes_enabled}

    def _option_get(self, args: dict) -> dict:
        return self._need_catalog().find(args.get("id")).detail_row()

    def _option_set(self, args: dict) -> dict:
        catalog = self._need_writes("changing an option")
        row = catalog.find(args.get("id"))
        if row.secret:
            raise AppToolError(
                f"{row.path} holds a secret. No agent may read or change it — the person sets it "
                "themselves in Options.", code="secret")
        if not row.settable:
            raise AppToolError(
                f"{row.path} is a {row.kind} row, not a value an agent may set"
                + ("; run it with app_action_run, or open it with app_open."
                   if row.kind in ("button", "buttons") else "."),
                code="not_settable")
        value = catalog.check_value(row, args.get("value"))
        result = self.bridge.send("set_option", {"row": row.id, "value": value})
        if not result.get("ok"):
            raise _refused(result, f"changing {row.path}")
        # §30.3: `previous` and `value` are read back from the row by the GUI, not echoed from
        # the request, so a writer that normalises what it is given (a path that gets expanded,
        # a number that gets clamped) is reported as what is now in force. The catalog's own
        # values are the fallback for a GUI that answered without them.
        after = result["value"] if "value" in result else value
        before = result["previous"] if "previous" in result else row.value
        catalog.note_value(row.id, after)
        change_id = self._record(row, before, after, result.get("change_id"))
        return {"ok": True, "id": row.id, "row": row.path, "previous": before, "value": after,
                "before": _short(before), "after": _short(after), "change_id": change_id,
                "text": f"Changed {row.path}: {_short(before)} → {_short(after)}. "
                        f"Relay showed the user the change with an Undo button; you can also "
                        f"reverse it with app_undo change_id {change_id}."}

    # ---- actions ---------------------------------------------------------------
    def _action_list(self, args: dict) -> dict:
        catalog = self._need_catalog()
        bindable = self._bindable()
        # #GMCF: `set_keybinding` no longer lists the action registry in its schema, so this is
        # where an id and its keys are found. Of the 92 registry entries a real window sends, 31
        # have no palette row at all — the focus moves, the window cycle, the shortcuts overlay —
        # so they are listed here too, and room is kept for them in a listing that has no search.
        extra = self._shortcut_rows(catalog, bindable, args.get("search"))
        limit = MAX_LIST_ROWS - min(len(extra), MAX_LIST_ROWS // 3)
        rows = [a.row() for a in catalog.matching_actions(args.get("search"), limit=limit)]
        if bindable is not None:
            for row in rows:
                action = bindable.actions.get(row["key"])
                if action is not None:
                    row["keys"] = list(action.keys)
            rows += extra[:MAX_LIST_ROWS - len(rows)]
        total = len(catalog.actions) + sum(1 for action_id in (bindable.actions if bindable else ())
                                           if action_id not in catalog.actions)
        result = {"actions": rows, "count": len(rows), "total": total,
                  "runnable": sum(1 for row in rows if row["agent_safe"]),
                  "writes_enabled": catalog.writes_enabled}
        if len(rows) < total:
            result["note"] = f"{len(rows)} of {total} actions; pass a search to see the rest."
        return result

    def _bindable(self):
        """The live keybinding catalog (`keybindings.KeybindingCatalog`), or None (#GMCF)."""
        return self.keybindings() if self.keybindings is not None else None

    @staticmethod
    def _shortcut_rows(catalog: AppCatalog, bindable, search) -> list[dict]:
        """The bindable actions the palette has no row for, matched the way the palette rows are.

        No `agent_safe`: there is no palette entry to run, only a shortcut to rebind (#GMCF).
        """
        if bindable is None:
            return []
        needle = (search or "").strip().lower()[:MAX_SEARCH] if isinstance(search, str) else ""
        return [{"key": action.id, "section": "Shortcuts", "label": action.description,
                 "agent_safe": False, "keys": list(action.keys)}
                for action in bindable.actions.values()
                if action.id not in catalog.actions
                and (not needle or needle in f"{action.id} {action.description}".lower())]

    def _action_run(self, args: dict) -> dict:
        catalog = self._need_writes("running an action")
        action = catalog.action(args.get("key"))
        if not action.agent_safe:
            raise AppToolError(
                f"\"{action.label or action.key}\" is not one of the actions an agent may run "
                "(only the ones the person can undo in a click are). Tell them where it is and "
                "let them run it.", code="not_agent_safe")
        fields = {"key": action.key}
        # The pane the action is aimed at (§30.3, #AG7R group 2).  Sent only when the model named
        # one: with no `pane` the GUI aims a pane-scoped action at the asking agent's own pane —
        # it knows which that is, because the command arrives with the pane's session token — and
        # a helper, which has no pane of its own, goes on landing where the person is focused.
        pane = args.get("pane")
        if pane is not None:
            if not isinstance(pane, str) or not pane.strip():
                raise AppToolError("pane must be a pane id from app_panes.", code="invalid_value")
            fields["pane"] = pane.strip()
        result = self.bridge.send("run_action", fields)
        if not result.get("ok"):
            raise _refused(result, f"running \"{action.label or action.key}\"")
        change_id = None
        if result.get("change_id"):
            # An action the GUI logged as an undoable change — it says so by answering with a
            # change_id — goes into app_changes like a set does.
            change_id = self._record_action(action, result.get("change_id"))
        out = {"ok": True, "key": action.key, "action": action.label or action.key,
               "text": f"Ran \"{action.label or action.key}\"."}
        # Which pane it landed on, when that was a choice at all: the GUI answers with it, so the
        # model reports where the change went instead of assuming.
        if isinstance(result.get("pane"), str) and result["pane"]:
            out["pane"] = result["pane"]
        if change_id:
            out["change_id"] = change_id
            out["text"] += f" Undo it with app_undo change_id {change_id}."
        if isinstance(result.get("detail"), str) and result["detail"].strip():
            # What the action actually did, when it has something to report ("3 servers found").
            out["detail"] = result["detail"].strip()[:MAX_DETAIL]
            out["text"] += " " + out["detail"]
        return out

    # ---- panes -----------------------------------------------------------------
    def _panes(self, args: dict) -> dict:
        """`list_panes` (§30.3): the panes of this window, and which one is the agent's own.

        A pane could not be named before #AG7R group 2: the catalog carries the *tab*,
        `session_info` is about this conversation, and `app_sessions_search` is about saved ones —
        so an agent could aim at no pane but the one it was sitting in.  The ids are the panes'
        session tokens, which is what the GUI's change log already calls them by.
        """
        result = self.bridge.send("list_panes", {})
        if not result.get("ok"):
            raise _refused(result, "listing the panes")
        panes = result.get("panes")
        panes = [p for p in panes if isinstance(p, dict)][:MAX_PANES] if isinstance(panes, list) else []
        mine = next((p for p in panes if p.get("you")), None)
        text = f"{len(panes)} pane{'' if len(panes) == 1 else 's'} in this window"
        if mine is not None:
            text += f"; yours is \"{mine.get('title') or mine.get('id')}\""
        return {"ok": True, "panes": panes, "count": len(panes), "text": text + "."}

    # ---- one pane talking to another (#AG7R group 8, §30.3) ---------------------
    #
    # "8 allow sending messages and pre-filling messages across panes" (owner, 2026-09-20).  Until
    # this, no agent could put a prompt anywhere but its own pane: the Sessions helper could open
    # three conversations and then not say a word to any of them, and `run_in_terminal` /
    # `type_into_program` reach only the worker's own pane.
    #
    # Two acts, two tools, on purpose.  A **send** submits the prompt in that pane as if the
    # person had pressed Enter — the other agent starts working — and a **pre-fill** leaves it in
    # the composer for them to read and send.  A boolean on one tool would be one wrong default
    # away from making an agent act when the person was meant to.
    #
    # The worker checks the shape and nothing else: whether a pane exists, whether it is the
    # asking agent's own, and how long the chain of agent-to-agent prompts already is are all
    # questions only the GUI can answer, so they are refused there (`unknown_pane`, `would_loop`).
    def _send_prompt(self, args: dict) -> dict:
        return self._into_pane(args, send=True)

    def _prefill_prompt(self, args: dict) -> dict:
        return self._into_pane(args, send=False)

    def _into_pane(self, args: dict, *, send: bool) -> dict:
        what = "sending a prompt to another pane" if send else "pre-filling another pane's composer"
        self._need_writes(what)
        pane = args.get("pane")
        if not isinstance(pane, str) or not pane.strip():
            raise AppToolError("Name the pane with `pane`; app_panes lists the ids.",
                               code="invalid_value")
        text = args.get("text")
        if not isinstance(text, str) or not text.strip():
            raise AppToolError("Give the prompt as `text`.", code="invalid_value")
        if len(text) > MAX_PROMPT:
            raise AppToolError(f"A prompt sent to another pane is at most {MAX_PROMPT} characters; "
                               "point at a file instead of pasting one.", code="invalid_value")
        result = self.bridge.send("send_prompt" if send else "prefill_prompt",
                                  {"pane": pane.strip(), "text": text})
        if not result.get("ok"):
            raise _refused(result, what)
        where = result.get("pane_title") or pane.strip()
        if not send:
            return {"ok": True, "pane": pane.strip(), "sent": False,
                    "text": f"Left the prompt in {where}'s composer, unsent. The user reads it and "
                            "presses Enter."}
        # Queued or started: a tool result says what happened, and "it is waiting behind the turn
        # running there" is part of what happened (§30.3).
        queued = bool(result.get("queued"))
        return {"ok": True, "pane": pane.strip(), "sent": True, "queued": queued,
                "text": f"Sent the prompt to {where}; "
                        + ("it is queued behind what that pane is doing."
                           if queued else "its agent has started on it.")}

    # `/rename` and `/rename-tab` are slash commands typed into a composer, and no agent can type
    # into one — so "call this pane «deploy»" could not be asked of an agent at all (#AG7R group
    # 8).  It is a command rather than a palette action because it takes an argument and an
    # `ActionItem` takes none; the safe table could only ever have offered "open the rename
    # editor", which is a modal in front of the person (group 4's own problem).
    def _rename(self, args: dict) -> dict:
        self._need_writes("renaming a pane or a tab")
        what = args.get("what")
        if what not in ("pane", "tab"):
            raise AppToolError("what must be \"pane\" or \"tab\".", code="invalid_value")
        name = args.get("name")
        if name is None:
            name = ""
        if not isinstance(name, str):
            raise AppToolError("name must be text.", code="invalid_value")
        name = name.strip()
        if len(name) > MAX_LABEL:
            raise AppToolError(f"A name is at most {MAX_LABEL} characters.", code="invalid_value")
        for character in name:
            if ord(character) < 32:
                raise AppToolError("A name is one line of text.", code="invalid_value")
        fields = {"what": what, "name": name}
        pane = args.get("pane")
        if pane is not None:
            if not isinstance(pane, str) or not pane.strip():
                raise AppToolError("pane must be a pane id from app_panes.", code="invalid_value")
            fields["pane"] = pane.strip()
        result = self.bridge.send("rename", fields)
        if not result.get("ok"):
            raise _refused(result, f"renaming the {what}")
        previous = result.get("previous") if isinstance(result.get("previous"), str) else ""
        out = {"ok": True, "what": what, "name": name}
        if previous:
            out["previous"] = previous
        if name:
            out["text"] = (f"Renamed the {what}" + (f" \"{previous}\"" if previous else "")
                           + f" to \"{name}\". Renaming it back is the undo.")
        else:
            out["text"] = f"The {what} is back to its automatic name."
        return out

    # ---- sessions --------------------------------------------------------------
    def _sessions_search(self, args: dict) -> dict:
        """The Sessions pane's own search, worker-side (§30.4, protocol 14): no round trip."""
        query = args.get("query")
        if not isinstance(query, str) or not query.strip():
            raise AppToolError("app_sessions_search needs a query.", code="invalid_value")
        limit = args.get("limit")
        if limit is not None and type(limit) is not int:
            raise AppToolError("limit must be an integer.", code="invalid_value")
        limit = max(1, min(int(limit or 10), MAX_SESSION_ROWS))
        if self.sessions is None:
            raise AppToolError("The conversation index is not available in this pane.",
                               code="failed")
        try:
            index = self.sessions()
            # `scope="all"`: the person asking "which session was that in" means their sessions,
            # not this workspace's. The query language's `project:` narrows it (14.2).
            found = index.search(query.strip(), scope="all", workspace=self.workspace or None,
                                 limit=limit, matches_per_item=3)
        except (ValueError, OSError) as exc:
            raise AppToolError(f"Session search failed: {str(exc)[:300]}", code="failed") from None
        items = []
        for item in (found.get("items") or [])[:limit]:
            if not isinstance(item, dict):
                continue
            row = {key: item.get(key) for key in
                   ("id", "title", "updated", "model", "workspace", "turns", "source")
                   if item.get(key) is not None}
            # The index calls it `session_id`; every row had *no* id at all until 2026-09-20,
            # because this list asked for "id" and nothing answered to that name. The agent
            # could then name a conversation only by its title — and `app_open {target:
            # "conversation"}` takes the id, so the search has to give it.
            if not row.get("id") and item.get("session_id"):
                row["id"] = item["session_id"]
            matches = [str(m.get("text") or "")[:200] for m in (item.get("matches") or [])[:3]
                       if isinstance(m, dict)]
            if matches:
                row["matches"] = matches
            items.append(row)
        return {"items": items, "count": len(items), "total": found.get("total"),
                "query": query.strip()}

    # ---- navigation ------------------------------------------------------------
    def _open(self, args: dict) -> dict:
        target = args.get("target")
        if target not in OPEN_TARGETS:
            raise AppToolError(f"target must be one of {', '.join(OPEN_TARGETS)}.",
                               code="unknown_target")
        if target == "conversation":
            return self._open_conversations(args)
        fields = {"target": target}
        for key in ("section", "row", "query", "card"):
            value = args.get(key)
            if value is None or value == "":
                continue
            if not isinstance(value, str):
                raise AppToolError(f"{key} must be text.", code="invalid_value")
            fields[key] = value[:MAX_SEARCH]
        if "row" in fields and self.catalog is not None:
            # Reveal a row that exists: a misremembered id would open Options at nothing and read
            # to the user as a bug in Relay rather than a bad guess.
            fields["row"] = self.catalog.find(fields["row"]).id
        if "card" in fields:
            fields["card"] = fields["card"].strip().lstrip("#").upper()
        # The pane it opens in or beside (§30.3).  It matters for the six targets #AG7R group
        # 8 added — ⓘ, Activity, requests, subagents and the explorer are *that pane's*
        # views, and until they could be named the only way to open one was a key that landed
        # wherever the person was focused.  Unsent unless the model named one: the GUI resolves a
        # pane agent's own pane from the token the command arrives with.
        pane = args.get("pane")
        if pane is not None:
            if not isinstance(pane, str) or not pane.strip():
                raise AppToolError("pane must be a pane id from app_panes.", code="invalid_value")
            fields["pane"] = pane.strip()
        result = self.bridge.send("open", fields)
        if not result.get("ok"):
            raise _refused(result, f"opening {target}")
        where = ", ".join(f"{k} {v}" for k, v in fields.items() if k != "target")
        return {"ok": True, **fields,
                "text": f"Opened {target}" + (f" at {where}." if where else " for the user.")}

    # `app_open {target: "conversation"}` (§30.4).  The Sessions pane's row already does this —
    # `SessionManager::onResume` → `Pane::openSavedSession`, in the pane or in a new one — and the
    # helper had no way to ask for it: it could search the index and open the *list*, which is how
    # "open a group of previous sessions in new panes" ended as a list of titles (owner, 2026-09-20,
    # "sessions helper didn't do anything when I asked to open a group of previous sessions in new
    # panes").
    #
    # The ids are resolved **here**, against the same index `app_sessions_search` answers from, and
    # the GUI is handed the whole row: it holds no conversation index of its own, and a resume
    # needs the session's directory, its title and — for a claude or codex row — the argv that
    # respawns it (protocol 26.7).  One round trip per id, in the order given, so a group answers
    # per id and one unknown id does not lose the rest.
    def _open_conversations(self, args: dict) -> dict:
        ids = args.get("ids")
        if ids is None:
            ids = [args.get("id")] if args.get("id") not in (None, "") else []
        if not isinstance(ids, list):
            raise AppToolError("ids must be a list of conversation ids.", code="invalid_value")
        wanted = []
        for value in ids:
            if not isinstance(value, str) or not value.strip():
                raise AppToolError("Every conversation id must be text.", code="invalid_value")
            if value.strip() not in wanted:
                wanted.append(value.strip())
        if not wanted:
            raise AppToolError("app_open with target conversation needs id or ids.",
                               code="invalid_value")
        if len(wanted) > MAX_OPEN_CONVERSATIONS:
            raise AppToolError(f"app_open opens at most {MAX_OPEN_CONVERSATIONS} conversations at "
                               "once.", code="invalid_value")
        # A group is panes: they cannot all be the one pane the person is looking at, so `ids`
        # means new panes unless the caller says otherwise.  One id follows the same default —
        # "open it" should not take away what the pane is already holding — and `new_pane: false`
        # is how the agent says "here, in this pane".
        new_pane = args.get("new_pane")
        if new_pane is None:
            new_pane = True
        if not isinstance(new_pane, bool):
            raise AppToolError("new_pane must be true or false.", code="invalid_value")
        # The pane it opens from (§30.3, #AG7R group 2): with `new_pane: false` that is the pane
        # the conversation is loaded into, and "here" means the agent's own pane rather than
        # whichever one has the focus.  Unsent unless the model named one; the GUI resolves the
        # asking pane itself.
        pane = args.get("pane")
        if pane is not None and (not isinstance(pane, str) or not pane.strip()):
            raise AppToolError("pane must be a pane id from app_panes.", code="invalid_value")
        pane = pane.strip() if isinstance(pane, str) else None
        if self.sessions is None:
            raise AppToolError("The conversation index is not available in this pane.",
                               code="failed")
        results, opened = [], []
        for session_id in wanted:
            try:
                item = self._conversation_row(session_id)
            except AppToolError as exc:
                results.append({"id": session_id, "ok": False, "error": exc.code,
                                "text": str(exc)})
                continue
            title = str(item.get("title") or "Untitled")
            # `conversation`, not `id`: on an `app_command` the top-level `id` is the request id
            # the result is matched by (AppBridge.send), exactly as §30.1's row id travels as
            # `row`. The whole row goes as `item` — the GUI has no conversation index to look one
            # up in, and the resume path reads the row the Sessions list would have handed it.
            try:
                fields = {"target": "conversation", "conversation": session_id,
                          "new_pane": new_pane, "item": item}
                if pane:
                    fields["pane"] = pane
                result = self.bridge.send("open", fields)
            except AppToolError as exc:
                # The GUI stopped answering (a window that has gone, a worker being shut down).
                # Nothing is gained by spending another deadline per remaining id.
                results.append({"id": session_id, "ok": False, "error": exc.code,
                                "title": title, "text": str(exc)})
                break
            if result.get("ok"):
                results.append({"id": session_id, "ok": True, "title": title})
                opened.append(title)
            else:
                code = result.get("error") if result.get("error") in ERRORS else "failed"
                results.append({"id": session_id, "ok": False, "error": code,
                                "title": title,
                                "text": result.get("message") or ERROR_TEXT.get(code, "")})
        # A `break` above leaves the rest untried; they are still answered, so the model never
        # has to guess which of the ids it named were even attempted.
        answered = {row["id"] for row in results}
        for session_id in wanted:
            if session_id not in answered:
                results.append({"id": session_id, "ok": False, "error": "no_reply",
                                "text": "Not attempted: Relay stopped answering."})
        where = "in a new pane" if new_pane else "in this pane"
        if len(opened) > 1:
            where = "in new panes" if new_pane else "in this pane"
        if not opened:
            first = next((r for r in results if not r["ok"]), {})
            raise AppToolError(first.get("text") or "Relay opened none of them.",
                               code=first.get("error") or "failed")
        text = f"Opened {len(opened)} conversation{'' if len(opened) == 1 else 's'} {where}: " \
               + ", ".join(opened) + "."
        missed = [r for r in results if not r["ok"]]
        if missed:
            text += " Not opened: " + ", ".join(r.get("title") or r["id"] for r in missed) + "."
        return {"ok": True, "target": "conversation", "new_pane": new_pane,
                "opened": len(opened), "results": results, "text": text}

    def _conversation_row(self, session_id: str) -> dict:
        """The saved conversation `session_id`, as the GUI's resume path needs it.

        Only the fields that identify and reopen it: the transcript itself is the pane's business
        once it is loaded, and `conversation()` would otherwise carry a whole history through a
        tool result.
        """
        try:
            index = self.sessions()
            header = index.conversation(session_id, limit=1)
        except ValueError:
            raise AppToolError(f"No saved conversation with the id {session_id[:80]}.",
                               code="unknown_conversation") from None
        except OSError as exc:
            raise AppToolError(f"Reading the conversation failed: {str(exc)[:200]}",
                               code="failed") from None
        header.pop("items", None)
        header.pop("overview", None)
        # A claude or codex row is not a Relay conversation: it reopens by running the guest's own
        # resume command in the directory it was recorded in (26.7), and `annotate_items` is what
        # puts that argv on the row — the same call the `conversations` answer makes.
        rows = guest_sessions.annotate_items([header])
        row = rows[0] if rows else header
        keep = ("session_id", "session_dir", "title", "source", "workspace", "raw_cwd", "model",
                "updated", "turns", "id", "mtime", "message_count", "resume_command", "resume_cwd")
        return {key: row[key] for key in keep if row.get(key) not in (None, "")}

    # ---- the change log --------------------------------------------------------
    def _record(self, row: OptionRow, previous, value, gui_change_id) -> str:
        with self._lock:
            self._next_change += 1
            change_id = str(gui_change_id) if gui_change_id else f"ch-{self._next_change}"
            self.changes.append({"change_id": change_id, "kind": "option", "id": row.id,
                                 "label": row.path, "previous": previous, "value": value,
                                 "at": self.clock(), "undone": False})
            del self.changes[:-MAX_CHANGES]
        return change_id

    def _record_action(self, action: ActionRow, gui_change_id) -> str:
        with self._lock:
            self._next_change += 1
            change_id = str(gui_change_id) if gui_change_id else f"ch-{self._next_change}"
            self.changes.append({"change_id": change_id, "kind": "action", "key": action.key,
                                 "label": action.label or action.key, "at": self.clock(),
                                 "undone": False})
            del self.changes[:-MAX_CHANGES]
        return change_id

    def _changes(self, args: dict) -> dict:
        """This worker's own writes, newest first (§30.4): built from the results it received."""
        with self._lock:
            items = [dict(c) for c in reversed(self.changes)]
        now = self.clock()
        for item in items:
            item["when"] = round(max(0.0, now - item.pop("at")), 1)
        return {"changes": items, "count": len(items)}

    def _find_change(self, change_id: str) -> dict:
        with self._lock:
            found = next((c for c in self.changes if c["change_id"] == change_id), None)
        if found is None:
            raise AppToolError(
                f"You made no change {change_id!r} in this session. app_changes lists what you "
                "have changed; older changes are the person's to undo from Relay itself.",
                code="unknown_change")
        if found["undone"]:
            raise AppToolError(f"Change {change_id} has already been undone.",
                               code="unknown_change")
        return found

    def _undo(self, args: dict) -> dict:
        change_id = args.get("change_id")
        if not isinstance(change_id, str) or not change_id.strip():
            raise AppToolError("app_undo needs the change_id of the change to reverse.",
                               code="unknown_change")
        found = self._find_change(change_id.strip())
        # Deliberately not gated on `writes_enabled` (§30.4): it can only revert a change this
        # worker itself made, and putting a setting back is not a new write.
        result = self.bridge.send("undo", {"change_id": found["change_id"]})
        if not result.get("ok"):
            raise _refused(result, f"undoing {found['change_id']}")
        with self._lock:
            found["undone"] = True
        if found["kind"] == "option" and self.catalog is not None:
            self.catalog.note_value(found["id"], result.get("value", found.get("previous")))
        back = _short(result["value"] if "value" in result else found.get("previous"))
        return {"ok": True, "change_id": found["change_id"], "row": found["label"],
                "text": f"Undid the change to {found['label']}"
                        + (f" — it is {back} again." if back else ".")}


# ------------------------------------------------------------------ the worker's half

class AppCommands:
    """What `backend/worker.py` holds: the catalog, the round trip, and the tools it hands out.

    `board_protocol.BoardCommands` is the shape — one object per worker, created before the
    first `configure`, asked for the agent's tools while an `Agent` is being built, and given
    the protocol messages that belong to it.  The `AppTools` instance it makes **outlives** each
    `configure`, so the change log (§30.4: "the writes this worker has made") survives the
    rebuild of the agent that a model switch or a workspace change causes.
    """

    #: The protocol messages this object owns (§30.2, §30.3).
    KINDS = frozenset({"app_catalog", "app_command_result"})

    def __init__(self, emit: Callable[[dict], None], *,
                 sessions: Callable[[], object] | None = None,
                 agent: Callable[[], object] | None = None):
        self.emit = emit
        self.bridge = AppBridge(emit)
        #: The live `AppTools`, or None while this worker has been sent no `app` block.
        self.tools: AppTools | None = None
        self._sessions = sessions
        self._agent = agent

    @staticmethod
    def handles(kind) -> bool:
        return kind in AppCommands.KINDS

    def configure(self, request: dict | None, workspace: str | None = None) -> "AppTools | None":
        """`configure`'s `app` block (§30.2): the tools the new `Agent` is built with, or None.

        A `configure` replaces the agent, so anything still waiting on the old one's round trip
        is answered rather than left parked on a pane that has gone.
        """
        self.bridge.fail_pending()
        catalog = AppCatalog.from_request((request or {}).get("app"))
        return self._apply(catalog, workspace)

    def _apply(self, catalog: AppCatalog | None, workspace: str | None = None) -> "AppTools | None":
        if catalog is None:
            self.tools = None
            return None
        if self.tools is None:
            self.tools = AppTools(catalog, self.bridge, sessions=self._sessions,
                                  workspace=workspace, keybindings=self._keybindings)
        else:
            self.tools.set_catalog(catalog)
            if workspace is not None:
                self.tools.workspace = workspace
        return self.tools

    def _keybindings(self):
        """The worker's keybinding catalog, read through the live agent (#GMCF).

        Through the agent and not a captured object, because a `keybindings` message replaces
        `executor.keybindings` in place — `app_action_list` has to show the keys as they are now,
        since `set_keybinding`'s schema no longer shows them at all.
        """
        agent = self._agent() if self._agent is not None else None
        return getattr(getattr(agent, "executor", None), "keybindings", None)

    def bind_agent(self, agent) -> None:
        """`configure` built a new Agent: its Stop ends a command that is waiting for the GUI."""
        cancel = getattr(agent, "cancel_event", None)
        if cancel is not None:
            self.bridge.cancel = cancel

    def dispatch(self, request: dict) -> None:
        kind = request.get("type")
        if kind == "app_catalog":
            self._apply(AppCatalog.from_request(request.get("app")))
            agent = self._agent() if self._agent is not None else None
            if agent is not None and getattr(agent, "app", None) is not self.tools:
                # The worker was configured before the GUI had a catalog (or has just lost one):
                # the live agent's tool list and system prompt change without a new conversation,
                # exactly as `set_board` re-points the board tools (19.11).
                agent.app = self.tools
                agent.refresh_system_prompt()
            self.emit({"event": "app_catalog_updated", "id": request.get("id"),
                       "options": len(self.tools.catalog.options) if self.tools else 0,
                       "actions": len(self.tools.catalog.actions) if self.tools else 0,
                       "writes_enabled": bool(self.tools and self.tools.catalog.writes_enabled)})
        elif kind == "app_command_result":
            self.bridge.answer(request)

    def shutdown(self) -> None:
        self.bridge.fail_pending()


# ------------------------------------------------------------------ the brief

def prompt_section(tools: "AppTools | None") -> str:
    """What `Agent.system_prompt` appends when this pane can drive the app (§30.4).

    The board's `prompt_section` is the model: a short header naming what is there, then the
    rules.  It is kept to a paragraph because every pane agent carries it on every turn.
    """
    catalog = getattr(tools, "catalog", None) if tools is not None else None
    if catalog is None:
        return ""
    safe = sum(1 for a in catalog.actions.values() if a.agent_safe)
    lines = [
        "",
        "Relay itself: you can drive the app the user is sitting in. app_option_list and "
        "app_option_get read its Options "
        f"({len(catalog.options)} rows), app_action_list the actions ({safe} of "
        f"{len(catalog.actions)} are ones you may run), app_sessions_search their past "
        "conversations, and app_open puts any of Options, the actions palette, Sessions, the "
        "Switchboard, the file explorer, Test suites, Activity, \u24d8, requests or subagents on "
        "screen zoomed to the row, search or card you are talking about — do that instead of "
        "describing where a setting lives.",
        # The owner, 2026-09-20: "it also needs to reply in text that it is doing it." A turn that
        # only calls tools draws nothing in a helper panel — the panel shows the agent's text, not
        # its calls — so the app tools are the one place where narrating is not optional.
        "app_open {target: \"conversation\"} opens a past conversation itself, the way Enter on "
        "a Sessions row does: app_sessions_search gives the ids, `id` opens one and `ids` opens a "
        "group, each in a pane of its own. new_pane is true unless you say otherwise; \"in new "
        "panes\" means true, and only \"here\" or \"in this pane\" means false, which replaces "
        "what that pane is holding. When neither is said and the conversation is not yours to "
        "disturb, open it in a new pane.",
        # `app_panes` is named here and not in the writes branch below (#AG7R, 2026-09-20): it is
        # a *read*, answered whatever the toggle says (§30.4), and it is the only place a pane id
        # can be found — so an agent that has the tool and no sentence about it cannot aim
        # anything, least of all a prompt at another pane. `activity_tools.prompt_section` names
        # its own read tools unconditionally for the same reason (c33df71b).
        "app_panes lists the panes of this window — their ids, titles, directories, models and "
        "whether each one's agent is busy — and `pane` on the tools that take it aims at one of "
        "them; your own pane is marked `you`.",
        "Say what you are doing, in words, whenever you use one of these tools: name the panes, "
        "rows or conversations before or as you act (\"Opening 3 sessions in new panes: A, B, "
        "C.\", \"Turned Copy on select on — Undo is in the notification.\") and never end a turn "
        "with an empty message after a tool call. The user sees your text, not your tool calls.",
    ]
    if catalog.writes_enabled:
        lines.append(
            "app_option_set and app_action_run change the app for real. Every change is shown to "
            "the user at once as \"Agent changed <row>: <before> → <after> · Undo\", so change "
            "only what was asked for, say in your reply what you changed, and use app_changes "
            "and app_undo to reverse your own. Ask first when a change reaches beyond the "
            "request. An action that acts on one pane — its model, reasoning effort, input mode "
            "or plan mode — runs on your own pane; `pane` aims it at another.")
        lines.append(
            "You can also talk to another pane: app_send_prompt submits a prompt in it as if the "
            "user had typed it there, app_prefill_prompt leaves it in that pane's composer for "
            "them to send, and app_rename names a pane or its tab. Sending makes another agent "
            "act, so name the pane and say why in your reply; the user is shown every send. You "
            "may not send to your own pane, and Relay cuts off a chain of agents prompting each "
            "other — if you find yourself answering a prompt by sending another one, stop and "
            "write the answer instead.")
    else:
        lines.append(
            "Changing options and running actions is switched off for agents in Options › Agent, "
            "so app_option_set and app_action_run refuse: read, explain, open the row for them, "
            "and let them make the change.")
    lines.append(
        "API keys and everything else the keyring holds are secret: their values never reach you "
        "and no agent may set them.")
    return "\n".join(lines) + "\n"
