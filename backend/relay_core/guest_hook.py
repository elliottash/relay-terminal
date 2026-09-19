# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Claude Code guest shim: hooks and the statusline over Relay's guest event channel (GT7X).

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 26.4. The installer (relay_core.guest_install)
writes it into a project's `.claude/settings.local.json` as

    [ -n "$RELAY_GUEST_EVENT" ] && [ -n "$RELAY_BACKEND_DIR" ] || exit 0;
    exec "${RELAY_PYTHON:-python3}" "$RELAY_BACKEND_DIR/relay_core/guest_hook.py" <event> --relay-guest

with the hook's JSON on stdin, and the same line with `statusline` for the statusline. The shell
guard is what makes the entry harmless in a terminal Relay is not running: the settings file is
read by every claude started in that project, and without the pane's environment the command must
cost nothing at all. It is run by absolute path so no `PYTHONPATH` is needed.

* **Hook mode** (`<event>` is a Claude hook name): the hook's JSON is forwarded as a `hook`
  channel event and the shim returns at once. The one exception is `PermissionRequest`, which
  Claude sends only when it is actually about to ask the user: the shim asks the pane instead,
  waits for the answer in `guest-answers/<question>.json`, and prints Claude's own
  `hookSpecificOutput.decision` JSON. Nothing is ever approved on the user's behalf: an
  unanswered question times out into no output at all, which leaves Claude to ask as it always
  does. `PreToolUse` is deliberately *not* installed — it fires before every tool call, including
  the ones the user's own permission rules allow without asking.
* **Statusline mode** (`statusline`): the statusline JSON is parsed for the fields Relay can
  show (model, context share), emitted as a `statusline` channel event, and exactly one
  passthrough line is printed so Claude still renders a statusline. The line's format is
  `RELAY_GUEST_STATUSLINE` (fields `{model}`, `{dir}`, `{cwd}`, `{session}`), by default
  `"{model} · {dir}"`.

The channel is a spool directory, not a slot (26.3): each event is one file in
`$RELAY_GUEST_EVENT`, named `<time_ns>-<pid>-<counter>.json` so the pane reads them in the order
they were written, and the pane deletes each file once it has handled it. The single `guest.json`
this replaced lost a question whenever a statusline tick landed on top of it.

The spool write itself lives in `shell/guest-event.py`, the channel's one writer, shared with the
IDE bridge. This shim **imports** it (by path, beside the backend directory it is run from) rather
than spawning it: it used to run a second interpreter per event, twice per statusline tick, to
write a small JSON file.

The hard invariant (26.3): with no `RELAY_GUEST_EVENT` in the environment this is a no-op —
exit 0, write nowhere, print nothing but the statusline passthrough line.
"""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import sys
import time
import uuid

PERMISSION_TIMEOUT_DEFAULT = 120.0   # seconds to hold a permission question open
POLL_SECONDS = 0.05                  # how often the answer file is re-read while waiting
ANSWERS_DIR = "guest-answers"        # under the pane's runtime dir: one file per answer
# Claude hands a Write or an Edit hook the whole new file on stdin, so the read has to be
# generous: at 1 MiB the JSON came back cut in half, parsed as nothing, and the question then
# named no tool at all (review of 51587e3). What crosses the channel is capped separately, below.
MAX_STDIN = 8 * 1024 * 1024
# The pane deletes an event file over 256 KiB unread (26.3), so the shim stays well under it:
# a whole-file Write payload or a long tool_response is cut down rather than lost.
MAX_EVENT_BYTES = 64 * 1024
MAX_FIELD_CHARS = 4096               # one string inside the payload
MAX_ITEMS = 64                       # entries kept from one list
MAX_DEPTH = 8                        # nesting kept before the subtree becomes the marker
TRUNCATED = "… [truncated by Relay]"
# The channel's one writer, beside the backend directory this file is run from: an installed
# Relay and a checkout both have `shell/` and `backend/` as siblings.
# Three shims carry this constant (guest_hook, guest_codex, guest_slash) and they must not
# be folded into one: two of them are run by absolute path with no PYTHONPATH, so they may
# not import from `relay_core` at all. `tests/test_guest.py` (OneChannelInThreeLanguages) is what
# keeps the copies in step.
WRITER = Path(__file__).resolve().parents[2] / "shell" / "guest-event.py"


def main(argv=None) -> int:
    args = [arg for arg in (sys.argv[1:] if argv is None else argv) if not arg.startswith("--")]
    if not args:
        return 0
    raw = _read_stdin()
    if args[0] == "statusline":
        return _statusline(raw)
    return _hook(args[0], raw)


# ----- the channel (26.3) -------------------------------------------------------------------


def _channel() -> tuple[Path, Path, str] | None:
    """(events spool, runtime dir, token) when this process really is inside a Relay pane.

    None is the no-op case, and it is the common one: the hooks live in a settings file that
    every claude started in this project reads, pane or no pane.
    """
    events = os.environ.get("RELAY_GUEST_EVENT", "")
    runtime = os.environ.get("RELAY_RUNTIME_DIR", "")
    token = os.environ.get("RELAY_SESSION_TOKEN", "")
    if not events or not runtime or not token:
        return None
    return Path(events), Path(runtime), token


def _guest() -> str:
    """Which guest this shim speaks for. These are Claude Code's hooks (26.4); a codex install
    can say otherwise with RELAY_GUEST_ID."""
    return os.environ.get("RELAY_GUEST_ID") or "claude"


_writer = None


def writer():
    """`shell/guest-event.py` as a module, loaded once. None when it is not there — a Relay
    without its own helper writes nothing rather than growing a second copy of the spool rules.
    `RELAY_GUEST_WRITER` overrides the path, which is how a test points at another checkout."""
    global _writer
    if _writer is None:
        path = os.environ.get("RELAY_GUEST_WRITER") or str(WRITER)
        try:
            spec = importlib.util.spec_from_file_location("relay_guest_event", path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            _writer = module
        except (OSError, AttributeError, ImportError, SyntaxError, ValueError):
            _writer = False
    return _writer or None


def _send(channel: tuple[Path, Path, str], event: str, data: dict, sequence: str | None = None) -> str | None:
    """One event onto the spool, through the channel's own writer. Returns the sequence the
    envelope carries, or None when nothing could be written — a channel that is missing, full or
    unhappy changes nothing about the guest's own run."""
    events, _runtime, token = channel
    module = writer()
    if module is None:
        return None
    return module.write_event(event, _guest(), data, sequence, directory=events, token=token)


# ----- hook mode (26.4) ---------------------------------------------------------------------


def _hook(name: str, raw: str) -> int:
    channel = _channel()
    if channel is None:
        return 0                     # the hard invariant: no channel, no effect anywhere
    payload = _capped(_json(raw))
    if not payload and len(raw) >= MAX_STDIN:
        # The read hit its own ceiling, so what came back is half a JSON document and parses as
        # nothing. The user still gets a question they can answer, which is the point — just one
        # with nothing to show in it. A small payload that is simply not JSON stays empty.
        payload = {"relay_truncated": True}
    if name != "PermissionRequest":
        _send(channel, "hook", {"name": name, "payload": payload})
        return 0     # every other hook is a signal, not a question: it never holds claude up
    # A permission request: Claude only sends one when it is about to ask the user, so the pane
    # asks instead. The sequence is the shim's own uuid4 and names the answer file, so an answer
    # can only ever be for this question — a stale one from an earlier question is not read.
    sequence = _send(channel, "hook", {"name": name, "payload": payload}, sequence=str(uuid.uuid4()))
    if sequence is None:
        return 0     # the question never reached the pane: claude asks the way it always does
    decision = _wait_for_answer(channel, sequence)
    if decision is None:
        return 0     # unanswered (or no pane watching): print nothing, claude asks as ever
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PermissionRequest",
                                             "decision": {"behavior": decision}}}))
    return 0


def _wait_for_answer(channel: tuple[Path, Path, str], sequence: str) -> str | None:
    _events, runtime, token = channel
    path = runtime / ANSWERS_DIR / (sequence + ".json")
    deadline = time.monotonic() + _timeout()
    while time.monotonic() < deadline:
        answer = _take_answer(path)
        if answer is not None and answer.get("token") == token:
            decision = answer.get("decision")
            return decision if decision in ("allow", "deny") else None
        time.sleep(POLL_SECONDS)
    return None


def _take_answer(path: Path) -> dict | None:
    """The answer to this one question, removed from disk as it is read: a decision is used
    once, and nothing of it is left in the runtime directory afterwards (26.3)."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None          # not answered yet: the common case, and no unlink to attempt
    try:
        path.unlink()
    except OSError:
        pass
    try:
        value = json.loads(text)
    except ValueError:
        return None
    return value if isinstance(value, dict) else None


def _timeout() -> float:
    try:
        value = float(os.environ.get("RELAY_GUEST_PERMISSION_TIMEOUT", ""))
    except ValueError:
        return PERMISSION_TIMEOUT_DEFAULT
    return value if value > 0 else PERMISSION_TIMEOUT_DEFAULT


# ----- keeping one event small (26.3) ---------------------------------------------------------


def _capped(payload: dict) -> dict:
    """Claude's hook JSON, cut down to something the channel carries.

    A `Write` or `Edit` holds the whole new file in `tool_input`, and a `PostToolUse` holds the
    whole tool result, either of which can be megabytes. The pane deletes an event file over
    256 KiB unread, so an uncapped payload meant the question simply never appeared. Long
    strings are cut with a visible marker first; if that is still too much, `tool_input` goes
    and `relay_truncated` says so, which is a question the user can still answer ("Bash", no
    command shown) rather than no question at all.
    """
    trimmed = _trim(payload, 0)
    if _size(trimmed) <= MAX_EVENT_BYTES:
        return trimmed
    smaller = {key: value for key, value in trimmed.items() if key != "tool_input"}
    smaller["relay_truncated"] = True
    if _size(smaller) <= MAX_EVENT_BYTES:
        return smaller
    bare = {key: trimmed[key] for key in ("hook_event_name", "tool_name", "session_id")
            if isinstance(trimmed.get(key), str)}
    bare["relay_truncated"] = True
    return bare


def _trim(value, depth: int):
    if depth > MAX_DEPTH:
        return TRUNCATED
    if isinstance(value, str):
        return value if len(value) <= MAX_FIELD_CHARS else value[:MAX_FIELD_CHARS] + TRUNCATED
    if isinstance(value, dict):
        return {str(key): _trim(item, depth + 1) for key, item in list(value.items())[:MAX_ITEMS]}
    if isinstance(value, list):
        return [_trim(item, depth + 1) for item in value[:MAX_ITEMS]]
    if isinstance(value, (int, float, bool)) or value is None:
        return value
    return _trim(str(value), depth)


def _size(value) -> int:
    try:
        return len(json.dumps(value, ensure_ascii=False).encode("utf-8"))
    except (TypeError, ValueError):
        return MAX_EVENT_BYTES + 1


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
