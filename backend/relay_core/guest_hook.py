# SPDX-License-Identifier: GPL-3.0-or-later
"""The Claude Code guest shim: hooks and the statusline over Relay's guest event channel (GT7X).

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 26.4. The installer (relay_core.guest_install)
writes it into a project's `.claude/settings.json` as

    "$RELAY_PYTHON" -m relay_core.guest_hook <event>

with the hook's JSON on stdin, and as `… -m relay_core.guest_hook statusline` for the statusline.
Both modes reach the pane only through the channel the GUI sets up (26.3): the helper
`shell/guest-event.py` at `$RELAY_GUEST_EVENT`, writing `guest.json` in `$RELAY_RUNTIME_DIR`.

* **Hook mode** (`<event>` is a Claude hook name): the hook's JSON is forwarded as a `hook`
  channel event. `PreToolUse` is a permission request, so the shim asks the pane, waits for the
  user's answer in `guest-answer.json`, and prints Claude's own permission-decision JSON.
  Nothing is ever approved on the user's behalf: an unanswered question times out into no output
  at all, which leaves Claude to ask the way it always does.
* **Statusline mode** (`statusline`): the statusline JSON is parsed for the fields Relay can
  show (model, context share), emitted as a `statusline` channel event, and exactly one
  passthrough line is printed so Claude still renders a statusline. The line's format is
  `RELAY_GUEST_STATUSLINE` (fields `{model}`, `{dir}`, `{cwd}`, `{session}`), by default
  `"{model} · {dir}"`.

The hard invariant (26.3): with no `RELAY_GUEST_EVENT` in the environment this is a no-op —
exit 0, write nowhere, print nothing but the statusline passthrough line. A hook installed in a
user's global settings must therefore be harmless in every other terminal, including a terminal
that has no Relay around it at all.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

PERMISSION_TIMEOUT_DEFAULT = 120.0   # seconds to hold a permission question open
HELPER_TIMEOUT = 10.0                # seconds for one channel write; a hung helper never blocks
POLL_SECONDS = 0.05                  # how often the answer file is re-read while waiting
ANSWER_NAME = "guest-answer.json"
MAX_STDIN = 1024 * 1024              # a hook payload larger than this is not one Relay reads


def main(argv=None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if not args:
        return 0
    raw = _read_stdin()
    if args[0] == "statusline":
        return _statusline(raw)
    return _hook(args[0], raw)


# ----- the channel (26.3) -------------------------------------------------------------------


def _channel() -> tuple[Path, str, str] | None:
    """(runtime dir, token, helper) when this process really is inside a Relay pane.

    None is the no-op case, and it is the common one: the hooks live in a settings file that
    every terminal on this machine reads.
    """
    helper = os.environ.get("RELAY_GUEST_EVENT", "")
    runtime = os.environ.get("RELAY_RUNTIME_DIR", "")
    token = os.environ.get("RELAY_SESSION_TOKEN", "")
    if not helper or not runtime or not token:
        return None
    return Path(runtime), token, helper


def _guest() -> str:
    """Which guest this shim speaks for. These are Claude Code's hooks (26.4); a codex install
    can say otherwise with RELAY_GUEST_ID."""
    return os.environ.get("RELAY_GUEST_ID") or "claude"


def _send(channel: tuple[Path, str, str], event: str, data: dict, sequence: str | None = None) -> None:
    """One write to the channel: the helper takes the event JSON on stdin and replaces
    guest.json atomically. A helper that is missing, slow or unhappy changes nothing about the
    guest's own run — Relay's surfaces are an addition, never a dependency."""
    _runtime, _token, helper = channel
    command = [sys.executable, helper, event, _guest()]
    if sequence:
        command.append(sequence)
    try:
        subprocess.run(command, input=json.dumps(data), text=True, timeout=HELPER_TIMEOUT,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        pass


# ----- hook mode (26.4) ---------------------------------------------------------------------


def _hook(name: str, raw: str) -> int:
    channel = _channel()
    if channel is None:
        return 0                     # the hard invariant: no channel, no effect anywhere
    payload = _json(raw)
    if name != "PreToolUse":
        _send(channel, "hook", {"name": name, "payload": payload})
        return 0
    # A permission request: the pane asks the user, and the answer comes back through
    # guest-answer.json. The sequence is the shim's own uuid4, so an answer can only ever be
    # for this question — a stale file from an earlier one does not match and is ignored.
    sequence = str(uuid.uuid4())
    _send(channel, "hook", {"name": name, "payload": payload}, sequence=sequence)
    decision = _wait_for_answer(channel, sequence)
    if decision is None:
        return 0     # unanswered (or no pane watching): print nothing, Claude asks as it always does
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": decision,
        "permissionDecisionReason": "Allowed in Relay." if decision == "allow"
                                    else "Denied in Relay by the user."}}))
    return 0


def _wait_for_answer(channel: tuple[Path, str, str], sequence: str) -> str | None:
    runtime, token, _helper = channel
    path = runtime / ANSWER_NAME
    deadline = time.monotonic() + _timeout()
    while time.monotonic() < deadline:
        answer = _read_answer(path)
        if (answer is not None and answer.get("token") == token
                and answer.get("sequence") == sequence):
            decision = answer.get("decision")
            return decision if decision in ("allow", "deny") else None
        time.sleep(POLL_SECONDS)
    return None


def _read_answer(path: Path) -> dict | None:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
    except (OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def _timeout() -> float:
    try:
        value = float(os.environ.get("RELAY_GUEST_PERMISSION_TIMEOUT", ""))
    except ValueError:
        return PERMISSION_TIMEOUT_DEFAULT
    return value if value > 0 else PERMISSION_TIMEOUT_DEFAULT


# ----- statusline mode (26.4) ---------------------------------------------------------------


def _statusline(raw: str) -> int:
    payload = _json(raw)
    channel = _channel()
    if channel is not None:
        _send(channel, "statusline", _statusline_fields(payload))
    print(_passthrough_line(payload))
    return 0


def _statusline_fields(payload: dict) -> dict:
    """The fields Relay can show, and only those (26.3): a field the statusline input does not
    carry stays absent, so the chip says "unknown" rather than a made-up number."""
    data: dict = {}
    model = _model_name(payload)
    if model:
        data["model"] = model
    share = _context_pct(payload)
    if share is not None:
        data["context_pct"] = share
    return data


def _model_name(payload: dict) -> str:
    model = payload.get("model")
    if isinstance(model, dict):
        for key in ("display_name", "id"):
            value = model.get(key)
            if isinstance(value, str) and value.strip():
                return value.strip()[:64]
    if isinstance(model, str) and model.strip():
        return model.strip()[:64]
    return ""


def _context_pct(payload: dict) -> int | None:
    """How much of the guest's context window is in use, when the statusline input says so.

    The documented key has moved (`context_pct`, then `context_window.used_percentage`, then
    `context.used_percentage`), so all three are read. `exceeds_200k_tokens` is the one hard
    fact that has always been there: a conversation past the window is 100% by definition.
    """
    for path in (("context_pct",), ("context_window", "used_percentage"),
                 ("context", "used_percentage")):
        value = payload
        for key in path:
            value = value.get(key) if isinstance(value, dict) else None
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            return max(0, min(100, int(round(value))))
    if payload.get("exceeds_200k_tokens") is True:
        return 100
    return None


def _passthrough_line(payload: dict) -> str:
    """The one line Claude renders. Relay's shim must not cost the user their statusline, so
    this always prints — in every terminal, with or without Relay around it."""
    directory = _directory(payload)
    fields = {"model": _model_name(payload),
              "dir": os.path.basename(directory.rstrip("/")) if directory else "",
              "cwd": directory,
              "session": _text(payload.get("session_id"))}
    template = os.environ.get("RELAY_GUEST_STATUSLINE") or "{model} · {dir}"
    try:
        line = template.format(**fields)
    except (KeyError, IndexError, ValueError):
        line = "{model} · {dir}".format(**fields)
    return line.replace("\n", " ").strip() or "Relay"


def _directory(payload: dict) -> str:
    workspace = payload.get("workspace")
    if isinstance(workspace, dict):
        for key in ("current_dir", "project_dir"):
            value = workspace.get(key)
            if isinstance(value, str) and value:
                return value
    return _text(payload.get("cwd"))


def _text(value) -> str:
    return value.strip() if isinstance(value, str) else ""


# ----- plumbing -----------------------------------------------------------------------------


def _read_stdin() -> str:
    try:
        return sys.stdin.read(MAX_STDIN)
    except (OSError, ValueError):
        return ""


def _json(text: str) -> dict:
    try:
        value = json.loads(text or "null")
    except ValueError:
        return {}
    return value if isinstance(value, dict) else {}


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError):
        sys.exit(0)   # a shim must never break the guest's own run
