# SPDX-License-Identifier: AGPL-3.0-or-later
"""`run_in_terminal`: the agent hands a command to the user's *real* interactive shell.

`run_command` is a separate Bash with no terminal, no stdin and no ssh agent, so `ssh -t`, `sudo`
and every login flow fail there. This tool is the other channel: the worker emits
`terminal_command`, the pane either stages the command in the user's shell and runs it (`run`) or
puts it in the prompt box for the user to edit and submit (`prefill`), and answers
`terminal_command_result`. The turn thread blocks on that reply, as `type_into_program` does.

The model chooses the mode per call; the user's setting is a ceiling on it:

* **Offered only when the pane says so.** `begin_turn` reads `context.terminal_handoff`
  (`"agent"` or `"prefill"`). An older GUI, the phone and headless use never send it, and then the
  tool is not in the tool list at all.
* **`prefill` is a ceiling, not a hint.** With it, a `run` is downgraded here and again in the pane.
* **Never blind-typed.** The pane goes through its staging handshake (input.txt, Ctrl+X Ctrl+R,
  SHA-256 acknowledgement, then Enter) and refuses while a program owns the terminal.
* **Never over the user's draft.** A prompt box with text in it is left alone; the call is refused.
* **A cap per turn** stops a loop from filling the user's terminal.
* **Nothing is invisible.** The pane prints the command and the intent inline before it acts.

The tool returns as soon as the command is started or placed: its output is not in the result.
When the command exits the pane starts a follow-up turn with the exit status and the output.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 22.
"""
from __future__ import annotations

import itertools
import threading
import time
from typing import Callable

from .provider import Cancelled
from .router import bash_syntax

MAX_COMMAND = 2000
MAX_LINES = 50
MAX_INTENT = 200
MAX_HANDOFFS = 3           # hand-overs per turn
REPLY_TIMEOUT = 20.0       # seconds to wait for the pane's answer
MODES = ("run", "prefill")
CEILINGS = ("agent", "prefill")

_ALLOWED_CONTROL = {"\n", "\t"}
_CALL_IDS = itertools.count(1)

REFUSALS = {
    "not_offered": "This pane does not accept commands from the agent, so nothing was run or placed.",
    "syntax": "Bash rejects that command, so nothing was run or placed.",
    "draft": ("The user has their own text in the prompt box and Relay never overwrites it, so nothing "
              "was placed. Show the command in a fenced code block instead."),
    "busy": ("The user's terminal is not at a shell prompt (a program is running, or the shell is not "
             "ready), so nothing was run. Show the command in a fenced code block instead."),
    # A rate limit, not a rule about consent: the old wording ("without the user typing anything")
    # came back to the user as the agent claiming it was not allowed to act unasked.
    "chain": ("You have reached the run of commands Relay allows in the user's terminal before they "
              "take a turn. Stop and tell them where things stand."),
    "cap": "You have reached this turn's limit on commands handed to the user's terminal.",
    "no_reply": "The terminal pane did not answer in time; it is not certain whether the command was run.",
    "cancelled": "The turn was stopped, so nothing was run.",
    "failed": "The terminal pane could not take that command.",
}

SPEC = {
    "type": "function",
    "function": {
        "name": "run_in_terminal",
        "description": (
            "Hand a shell command to the user's REAL interactive terminal pane: their shell, their "
            "tty, their ssh agent and keys. Use it only for what run_command cannot do: interactive "
            "or tty-bound commands such as `ssh -t`, `sudo`, logins and device-auth flows, a REPL or "
            "editor the user should land in, or anything that needs their shell's state. Never use "
            "it for a command run_command can run. You do not need to be asked: hand a command over "
            "whenever it is clearly the next step, since it is printed in the user's pane with your "
            "intent line before it acts and they can edit or stop it. mode \"run\" stages the "
            "command and runs it at once: choose it when you are confident in the exact command and "
            "it is reversible or routine. mode \"prefill\" puts it in the user's prompt box for them "
            "to edit and submit: choose it when it is destructive or hard to undo, when it has a "
            "placeholder to fill in, or when they may want to change it. The user's own setting may "
            "cap this: with a prefill ceiling a \"run\" comes back placed in the prompt box instead, "
            "and the result says so. The result only says whether the command was "
            "started or placed; you do NOT see its output in this turn. After calling it, end your "
            "turn with one line telling the user what will happen in their terminal and what, if "
            "anything, they have to do. When the command exits, Relay sends you its exit status and "
            "output in a follow-up turn, so do not ask the user to report back."),
        "parameters": {
            "type": "object",
            "properties": {
                "command": {"type": "string",
                            "description": "The exact Bash command line. No escape or control characters."},
                "mode": {"type": "string", "enum": list(MODES),
                         "description": "\"run\" to run it now, \"prefill\" to put it in the prompt box."},
                "intent": {"type": "string",
                           "description": "One line saying what this does and why, shown to the user."},
                "report_back": {"type": "boolean",
                                "description": "Send you the exit status and output when it finishes "
                                               "(default true). Pass false when nothing is left to do."},
            },
            "required": ["command", "mode", "intent"],
            "additionalProperties": False,
        },
    },
}


def validate_ceiling(value) -> str:
    """`context.terminal_handoff` from the GUI: whether the tool exists, and how far it may go."""
    if value is None:
        return ""
    if value not in CEILINGS:
        raise ValueError(f"context.terminal_handoff must be one of: {', '.join(CEILINGS)}.")
    return value


class TerminalHandoff:
    """Per-pane state plus the blocking round trip. One instance lives on the pane's ToolExecutor."""

    def __init__(self, emit: Callable[[dict], None], cancel: threading.Event):
        self.emit = emit
        self.cancel = cancel
        self._lock = threading.Lock()
        self._pending: dict[str, list] = {}
        self.ceiling = ""
        self.handoffs = 0

    def begin_turn(self, ceiling) -> None:
        self.ceiling = validate_ceiling(ceiling)
        self.handoffs = 0

    def end_turn(self) -> None:
        self.ceiling = ""
        self.handoffs = 0
        self.fail_pending("cancelled")

    def available(self) -> bool:
        return bool(self.ceiling)

    # ----- the tool ------------------------------------------------------------------------
    def tool_spec(self) -> dict:
        return SPEC

    def prepare(self, args: dict) -> tuple[dict, str]:
        if not isinstance(args, dict) or set(args) - {"command", "mode", "intent", "report_back"}:
            raise ValueError("Unknown tool or unexpected argument.")
        intent = args.get("intent")
        if not isinstance(intent, str) or not intent.strip() or len(intent) > MAX_INTENT:
            raise ValueError(f"intent must be one line of at most {MAX_INTENT} characters saying what "
                             "this command does and why.")
        command = args.get("command")
        if not isinstance(command, str) or not command.strip() or len(command) > MAX_COMMAND:
            raise ValueError(f"command must be text of 1 to {MAX_COMMAND} characters.")
        command = command.strip()
        if any((ord(c) < 32 or ord(c) == 127 or 0x80 <= ord(c) <= 0x9F) and c not in _ALLOWED_CONTROL
               for c in command):
            raise ValueError("command may not contain control characters (escape, carriage return, …).")
        if command.count("\n") > MAX_LINES:
            raise ValueError(f"command may not contain more than {MAX_LINES} lines.")
        mode = args.get("mode")
        if mode not in MODES:
            raise ValueError(f"mode must be one of: {', '.join(MODES)}.")
        report_back = args.get("report_back", True)
        if type(report_back) is not bool:
            raise ValueError("report_back must be a boolean.")
        payload = {"command": command, "mode": mode, "intent": " ".join(intent.split()),
                   "report_back": report_back}
        title = "RUN IN TERMINAL" if mode == "run" else "PREFILL PROMPT BOX"
        return payload, f"{title}\n\n{payload['intent']}\n\n{command}"

    def refuse(self, code: str, detail: str = "") -> dict:
        error = REFUSALS.get(code, REFUSALS["failed"])
        if detail:
            error = f"{error} {detail}"
        return {"ok": False, "refused": code, "error": error[:1500]}

    def execute(self, payload: dict, turn_id=None) -> dict:
        if self.cancel.is_set():
            raise Cancelled("Stopped.")
        if not self.ceiling:
            return self.refuse("not_offered")
        if self.handoffs >= MAX_HANDOFFS:
            return self.refuse("cap")
        ok, message = bash_syntax(payload["command"])
        if not ok:
            return self.refuse("syntax", message)
        # The user's ceiling wins over the model's choice; the pane applies it again.
        mode = "prefill" if self.ceiling == "prefill" else payload["mode"]
        call_id = f"th-{next(_CALL_IDS)}"
        done = threading.Event()
        with self._lock:
            self._pending[call_id] = [done, None]
            self.handoffs += 1
        self.emit({"event": "terminal_command", "id": call_id, "turn_id": turn_id,
                   **payload, "mode": mode})
        deadline = time.monotonic() + REPLY_TIMEOUT
        while not done.wait(0.05):
            if self.cancel.is_set():
                self._take(call_id)
                raise Cancelled("Stopped.")
            if time.monotonic() > deadline:
                self._take(call_id)
                return self.refuse("no_reply")
        return self._result(self._take(call_id) or {}, payload)

    def _result(self, reply: dict, payload: dict) -> dict:
        action = reply.get("action")
        if reply.get("ok") and action in ("started", "prefilled"):
            result = {"ok": True, "action": action, "command": payload["command"]}
            if action == "started":
                result["note"] = ("The command is now running in the user's terminal. You cannot see "
                                  "its output in this turn. End your turn with one line saying what "
                                  "the user will see and what they must do, if anything.")
            else:
                if payload["mode"] == "run":
                    result["downgraded"] = True
                result["note"] = ("The command is in the user's prompt box, not yet run. They may edit "
                                  "it; pressing Enter runs it. End your turn with one line saying so.")
            if payload["report_back"]:
                result["note"] += (" Relay will start a follow-up turn with the exit status and output "
                                   "when the command finishes.")
            return result
        code = reply.get("code") if reply.get("code") in REFUSALS else "failed"
        if code == "cancelled":
            raise Cancelled("Stopped.")
        with self._lock:
            self.handoffs = max(0, self.handoffs - 1)   # nothing reached the terminal
        detail = reply.get("error") if isinstance(reply.get("error"), str) else ""
        return self.refuse(code, detail if code == "failed" else "")

    # ----- replies from the GUI ------------------------------------------------------------
    def _take(self, call_id: str):
        with self._lock:
            entry = self._pending.pop(call_id, None)
        return entry[1] if entry else None

    def resolve(self, reply: dict) -> None:
        """`terminal_command_result` from the GUI, delivered on the protocol thread."""
        if not isinstance(reply, dict):
            raise ValueError("terminal_command_result must be an object.")
        call_id = reply.get("id")
        if not isinstance(call_id, str):
            raise ValueError("terminal_command_result needs the id of the terminal_command it answers.")
        with self._lock:
            entry = self._pending.get(call_id)
            if entry is None:
                return   # the turn ended, or the reply is late: nothing is waiting for it
            entry[1] = reply
            entry[0].set()

    def fail_pending(self, code: str) -> None:
        with self._lock:
            for entry in self._pending.values():
                if entry[1] is None:
                    entry[1] = {"ok": False, "code": code}
                entry[0].set()
