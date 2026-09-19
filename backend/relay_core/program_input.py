# SPDX-License-Identifier: AGPL-3.0-or-later
"""`type_into_program`: the agent types into the program in the user's *visible* terminal pane.

The worker cannot reach a terminal. The pane owns it, so this tool is a round trip: the worker
emits `program_input`, the GUI performs the write (or refuses), and answers
`program_input_result`. The turn thread blocks on that reply, which is safe because the GUI's
protocol loop and the turn loop are different threads (`backend/worker.py`).

Every rule that protects the user is enforced here **and** again in the pane, because only the
pane knows the state at the instant of the write:

* **Consent is per turn.** `begin_turn` reads `context.program_control` off the user's prompt;
  the GUI only sets it when the user handed the program over (the delegate action, the banner
  button, or "let the agent answer this"). Without it the tool is not even in the tool list.
* **Never a password.** A masked prompt is refused without asking the GUI, and the GUI refuses
  again. No password prompt is ever typed into by a model.
* **The user wins.** `program_state {granted: false}` (take-over: Ctrl+H, the button, or simply
  typing) revokes the grant mid-turn; the tool disappears at the next model call and any write
  already in flight comes back refused.
* **A cap per turn** (`max_program_writes`, default 20) stops a loop from hammering a program.
* **Nothing is invisible.** The pane prints "✦ typed: y" inline for every write.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 21. Card:
issues/features/2026-09-17-agent-delegate-and-take-over.md (#C1HH).
"""
from __future__ import annotations

import itertools
import threading
import time
from typing import Callable

from .provider import Cancelled

MAX_TEXT = 2000            # one answer, a short command, or a few lines for an editor
MAX_LINES = 50
MAX_INTENT = 200
MAX_GUEST_SESSION = 200    # a guest's own session id, as the index keys it (protocol 26.7)
MAX_SCREEN = 8000          # the screen snapshot the model is shown, in characters
DEFAULT_MAX_WRITES = 20    # writes per turn
MAX_WRITES_LIMIT = 200
REPLY_TIMEOUT = 20.0       # seconds to wait for the pane's answer

# Keys that have no printable text. The GUI maps these names to bytes: the model never sends a
# raw escape or control byte, so it cannot smuggle "quit" or "interrupt" inside `text`.
KEYS = ("enter", "escape", "tab", "backspace", "up", "down", "left", "right",
        "home", "end", "page-up", "page-down", "ctrl-c", "ctrl-d", "ctrl-z")

# Control characters allowed inside `text`. Everything else (escape, carriage return, bells,
# the C1 range) is refused: naming a key is the only way to send one.
_ALLOWED_CONTROL = {"\n", "\t"}

_CALL_IDS = itertools.count(1)

REFUSALS = {
    "not_granted": ("The user has not handed you the program in their terminal pane for this turn, "
                    "so nothing was typed. Tell them what to type, or ask them to delegate the program to you."),
    "password": ("That prompt is asking for a password or passphrase. Relay never types into a masked "
                 "prompt, so nothing was typed. Ask the user to type it themselves."),
    "no_program": "No program is running in the user's terminal pane, so nothing was typed.",
    "taken_over": "The user took control of the terminal, so nothing was typed. Stop typing and let them drive.",
    "cap": "You have reached this turn's limit on keystrokes into the program. Nothing was typed.",
    "no_reply": "The terminal pane did not answer in time; it is not certain whether anything was typed.",
    "cancelled": "The turn was stopped, so nothing was typed.",
    "failed": "The terminal pane could not type that.",
}

SPEC = {
    "type": "function",
    "function": {
        "name": "type_into_program",
        "description": (
            "Type into the interactive program running in the user's VISIBLE terminal pane "
            "(not a shell of your own: run_command is a separate background process). Offered only "
            "for a turn in which the user handed you that program. Send one answer or keystroke per "
            "call and read the returned screen before the next one. Never use it for a password or "
            "passphrase prompt. Every write is shown to the user in their pane, and the user can take "
            "control at any moment, which fails the next call."),
        "parameters": {
            "type": "object",
            "properties": {
                "text": {"type": "string",
                         "description": "Literal text to type, e.g. \"y\". No escape or control "
                                        "characters except newline and tab; use `key` for those."},
                "key": {"type": "string", "enum": list(KEYS),
                        "description": "A named key to press instead of text, e.g. \"escape\"."},
                "submit": {"type": "boolean",
                           "description": "Press Enter after `text` (default true). Pass false for a "
                                          "full-screen program where the keystroke is the whole input."},
                "intent": {"type": "string",
                           "description": "One line saying what this answers and why, shown to the user."},
            },
            "required": ["intent"],
            "additionalProperties": False,
        },
    },
}


def clip_screen(text) -> str:
    """The pane's screen as the model sees it: the last MAX_SCREEN characters, nothing else."""
    if not isinstance(text, str) or not text:
        return ""
    if len(text) <= MAX_SCREEN:
        return text
    return "…\n" + text[-MAX_SCREEN:]


def validate_grant(grant) -> dict:
    """`context.program_control` from the GUI: the user's consent for this one turn."""
    if grant is None:
        return {}
    if not isinstance(grant, dict) or set(grant) - {"granted", "reason", "program", "guest", "question",
                                                    "kind", "masked", "alt_screen", "waiting", "max_writes",
                                                    "screen", "screen_source", "guest_model", "guest_context_pct",
                                                    "guest_busy", "guest_session"}:
        raise ValueError("context.program_control has an unknown field.")
    for key in ("granted", "masked", "alt_screen", "waiting", "guest_busy"):
        if grant.get(key) is not None and type(grant[key]) is not bool:
            raise ValueError(f"context.program_control.{key} must be a boolean.")
    for key, limit in (("reason", 60), ("program", 200), ("guest", 40), ("question", 400),
                       ("kind", 40), ("screen_source", 40), ("guest_model", 64),
                       # Tier B (protocol 26.7): which session of its own the guest in this pane's
                       # shell is writing. The guests' ids are their own (a dashed UUID today), so
                       # this is a length cap and not a shape, exactly as the index's is.
                       ("guest_session", MAX_GUEST_SESSION)):
        value = grant.get(key)
        if value is not None and (not isinstance(value, str) or len(value) > limit):
            raise ValueError(f"context.program_control.{key} must be text of at most {limit} characters.")
    if grant.get("screen") is not None and not isinstance(grant["screen"], str):
        raise ValueError("context.program_control.screen must be text.")
    if grant.get("screen") is not None and len(grant["screen"]) > 200_000:
        raise ValueError("context.program_control.screen is too large.")
    writes = grant.get("max_writes")
    if writes is not None and (type(writes) is not int or not 1 <= writes <= MAX_WRITES_LIMIT):
        raise ValueError(f"context.program_control.max_writes must be an integer from 1 to {MAX_WRITES_LIMIT}.")
    # The guest's context share (protocol 26.3): an integer percentage, present only when the
    # statusline could say. Absent and null both mean "unknown", never zero.
    share = grant.get("guest_context_pct")
    if share is not None and (type(share) is not int or not 0 <= share <= 100):
        raise ValueError("context.program_control.guest_context_pct must be an integer from 0 to 100.")
    return grant


class ProgramControl:
    """Per-pane state plus the blocking round trip. One instance lives on the pane's ToolExecutor."""

    def __init__(self, emit: Callable[[dict], None], cancel: threading.Event):
        self.emit = emit
        self.cancel = cancel
        self._lock = threading.Lock()
        self._pending: dict[str, list] = {}
        self.default_max_writes = DEFAULT_MAX_WRITES
        # Who wants to hear about a `program_state` besides the turn: the worker's session
        # commands follow the guest named in it (protocol 26.7). See `watch()`.
        self._watcher: Callable[["ProgramControl"], None] | None = None
        self.reset()

    # ----- state ---------------------------------------------------------------------------
    def reset(self) -> None:
        self.granted = False
        self.reason = ""
        self.program = ""
        self.guest = ""
        # Which of the guest's own sessions is running in this pane (protocol 26.7). The worker
        # follows its transcript while it runs, so the Sessions pane sees the turn the user is
        # watching rather than the one the last background reconcile found.
        self.guest_session = ""
        self.guest_model = ""
        self.guest_context_pct = None   # unknown until a statusline event says otherwise (26.3)
        self.guest_busy = False
        self.question = ""
        self.kind = "none"
        self.masked = False
        self.alt_screen = False
        self.waiting = False
        self.screen = ""
        self.screen_source = "none"
        self.max_writes = self.default_max_writes
        self.writes = 0

    def _apply(self, state: dict) -> None:
        if "granted" in state:
            self.granted = bool(state.get("granted"))
        for key in ("reason", "program", "guest", "guest_session", "guest_model", "question", "kind",
                    "screen_source"):
            if key in state:
                self[key] = state.get(key) or ""
        for key in ("masked", "alt_screen", "waiting", "guest_busy"):
            if key in state:
                setattr(self, key, bool(state.get(key)))
        if "guest_context_pct" in state:
            self.guest_context_pct = state.get("guest_context_pct")   # validated above; None = unknown
        if "screen" in state:
            self.screen = clip_screen(state.get("screen"))
        if state.get("max_writes"):
            self.max_writes = int(state["max_writes"])

    def __setitem__(self, key: str, value) -> None:
        setattr(self, key, value)

    def watch(self, callback: Callable[["ProgramControl"], None] | None) -> None:
        """Be told after every `program_state` this pane sends.

        The pane is the only thing that knows a guest is running in its shell (Tier B, protocol
        26.7), and `program_state` is the only message that says so, so what follows that guest's
        transcript listens here rather than in `backend/worker.py`: the handler there already
        calls `update()` and needs no line of its own. One watcher — the worker is one pane.
        """
        self._watcher = callback

    def begin_turn(self, grant) -> None:
        """A turn starts: the grant on this prompt is the only consent this turn has."""
        self.reset()
        self._apply(validate_grant(grant))

    def end_turn(self) -> None:
        """Consent never survives the turn it was given for."""
        self.granted = False
        self.writes = 0
        self.fail_pending("cancelled")

    def update(self, state: dict) -> dict:
        """The `program_state` message: the pane's live view, including a take-over."""
        if not isinstance(state, dict):
            raise ValueError("program_state must be an object.")
        self._apply(validate_grant({k: v for k, v in state.items() if k not in ("type", "id")}))
        if not self.granted:
            self.fail_pending(self._revoke_code())
        watcher = self._watcher
        if watcher is not None:
            try:
                watcher(self)
            except Exception:
                # A listener is a bystander. A take-over has to reach the turn whatever a guest
                # tail made of the same message, so nothing it does can fail this update.
                pass
        return self.summary()

    def _revoke_code(self) -> str:
        """Why the pane is no longer letting the agent type. The pane says which in `reason`, so
        "the program exited" and "a password prompt appeared" are never reported as "the user
        took control". An empty reason means the user never handed the program over at all."""
        if self.masked or self.reason == "password":
            return "password"
        if self.reason in ("program_exited", "no_program"):
            return "no_program"
        if self.reason == "take_over":
            return "taken_over"
        return "not_granted"

    def summary(self) -> dict:
        summary = {"granted": self.granted, "reason": self.reason, "program": self.program,
                   "guest": self.guest, "guest_session": self.guest_session,
                   "guest_model": self.guest_model, "guest_busy": self.guest_busy,
                   "kind": self.kind, "masked": self.masked,
                   "waiting": self.waiting, "writes": self.writes, "max_writes": self.max_writes,
                   "screen_source": self.screen_source}
        # Present only when known, exactly as the pane sends it (protocol 26.3).
        if self.guest_context_pct is not None:
            summary["guest_context_pct"] = self.guest_context_pct
        return summary

    def available(self) -> bool:
        """Whether the tool is offered to the model at all. Re-read at every model call, so a
        take-over removes it from the next one."""
        return bool(self.granted)

    # ----- the tool ------------------------------------------------------------------------
    def tool_spec(self) -> dict:
        return SPEC

    def prepare(self, args: dict) -> tuple[dict, str]:
        if not isinstance(args, dict) or set(args) - {"text", "key", "submit", "intent"}:
            raise ValueError("Unknown tool or unexpected argument.")
        intent = args.get("intent")
        if not isinstance(intent, str) or not intent.strip() or len(intent) > MAX_INTENT:
            raise ValueError(f"intent must be one line of at most {MAX_INTENT} characters saying what "
                             "this types and why.")
        has_text, key = "text" in args, args.get("key")
        if has_text == (key is not None):
            raise ValueError("Give exactly one of text or key.")
        payload = {"intent": " ".join(intent.split())}
        if key is not None:
            if key not in KEYS:
                raise ValueError(f"key must be one of: {', '.join(KEYS)}.")
            payload["key"] = key
            preview = f"TYPE INTO PROGRAM\n\n{payload['intent']}\n\n<{key}>"
        else:
            text = args["text"]
            if not isinstance(text, str) or len(text) > MAX_TEXT:
                raise ValueError(f"text must be text of at most {MAX_TEXT} characters.")
            bad = sorted({c for c in text if (ord(c) < 32 or ord(c) == 127 or 0x80 <= ord(c) <= 0x9F)
                          and c not in _ALLOWED_CONTROL})
            if bad:
                raise ValueError("text may not contain control characters (escape, carriage return, …). "
                                 "Use the key argument to press a named key.")
            if text.count("\n") > MAX_LINES:
                raise ValueError(f"text may not contain more than {MAX_LINES} lines.")
            payload["text"] = text
            payload["submit"] = args.get("submit", True)
            if type(payload["submit"]) is not bool:
                raise ValueError("submit must be a boolean.")
            shown = text.replace("\n", "⏎")
            preview = f"TYPE INTO PROGRAM\n\n{payload['intent']}\n\n{shown}" + ("⏎" if payload["submit"] else "")
        return payload, preview

    def _refusal(self) -> str | None:
        # A masked prompt first: the pane drops the grant the moment one appears, and
        # "not granted" would hide the real reason from the model. Either way nothing is typed.
        if self.masked:
            return "password"
        if not self.granted:
            return self._revoke_code()
        if self.writes >= self.max_writes:
            return "cap"
        return None

    def refuse(self, code: str, payload: dict, turn_id=None) -> dict:
        self.emit({"event": "program_input_refused", "turn_id": turn_id, "code": code,
                   "error": REFUSALS.get(code, REFUSALS["failed"]), "intent": payload.get("intent", "")})
        return {"ok": False, "refused": code, "error": REFUSALS.get(code, REFUSALS["failed"]),
                "program": self.program, "screen": self.screen}

    def execute(self, payload: dict, turn_id=None) -> dict:
        if self.cancel.is_set():
            raise Cancelled("Stopped.")
        code = self._refusal()
        if code:
            return self.refuse(code, payload, turn_id)
        call_id = f"pi-{next(_CALL_IDS)}"
        done = threading.Event()
        with self._lock:
            self._pending[call_id] = [done, None]
            self.writes += 1
        self.emit({"event": "program_input", "id": call_id, "turn_id": turn_id,
                   "program": self.program, **payload})
        deadline = time.monotonic() + REPLY_TIMEOUT
        while not done.wait(0.05):
            if self.cancel.is_set():
                self._take(call_id)
                raise Cancelled("Stopped.")
            if time.monotonic() > deadline:
                self._take(call_id)
                return {"ok": False, "refused": "no_reply", "error": REFUSALS["no_reply"]}
        reply = self._take(call_id) or {}
        return self._result(reply)

    def _result(self, reply: dict) -> dict:
        if reply.get("ok"):
            # The screen after the write is the only honest evidence of what the program did.
            self.screen = clip_screen(reply.get("screen"))
            if reply.get("program"):
                self.program = reply["program"]
            self.masked = bool(reply.get("masked"))
            self.waiting = bool(reply.get("waiting"))
            self.question = (reply.get("question") or "")[:400]
            result = {"ok": True, "typed": reply.get("typed", ""), "program": self.program,
                      "waiting_for_input": self.waiting, "question": self.question, "screen": self.screen}
            if not reply.get("program"):
                # The write finished the program off. Say so plainly: a model that only sees
                # "ok" tends to call again, get a refusal, and then describe this write as failed.
                result["program_exited"] = True
                result["note"] = ("The program has exited and the pane is back at the shell prompt. "
                                  "There is nothing left to type into; do not call this tool again "
                                  "for it.")
            return result
        code = reply.get("code") if reply.get("code") in REFUSALS else "failed"
        if code == "cancelled":
            # The turn was stopped while the pane was being asked; unwind it like any other stop.
            raise Cancelled("Stopped.")
        if code in ("taken_over", "not_granted"):
            self.granted = False
        error = reply.get("error") if isinstance(reply.get("error"), str) else None
        return {"ok": False, "refused": code, "error": (error or REFUSALS[code])[:500],
                "program": self.program, "screen": clip_screen(reply.get("screen")) or self.screen}

    # ----- replies from the GUI ------------------------------------------------------------
    def _take(self, call_id: str):
        with self._lock:
            entry = self._pending.pop(call_id, None)
        return entry[1] if entry else None

    def resolve(self, reply: dict) -> None:
        """`program_input_result` from the GUI, delivered on the protocol thread."""
        if not isinstance(reply, dict):
            raise ValueError("program_input_result must be an object.")
        call_id = reply.get("id")
        if not isinstance(call_id, str):
            raise ValueError("program_input_result needs the id of the program_input it answers.")
        with self._lock:
            entry = self._pending.get(call_id)
            if entry is None:
                return   # the turn ended, or the reply is late: nothing is waiting for it
            entry[1] = reply
            entry[0].set()

    def fail_pending(self, code: str) -> None:
        with self._lock:
            entries = list(self._pending.values())
            for entry in entries:
                if entry[1] is None:
                    entry[1] = {"ok": False, "code": code}
                entry[0].set()
