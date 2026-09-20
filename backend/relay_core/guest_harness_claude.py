# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tier A: Claude Code as a headless harness (GT7X, protocol 29).

One `claude -p --input-format stream-json --output-format stream-json` process per pane, driven
over newline-delimited JSON on its stdin and stdout, implementing the `guest_harness.Harness`
contract. Nothing is typed into a TUI and nothing is scraped: every event Relay prints — the
answer's deltas, the guest's tool calls, its diffs, its usage — is a line this module read off
that pipe.

**The command line** (verified against Claude Code 2.1.278 on 2026-09-19, see
`docs/qa_evidence/2026-09-19-claude-codex-guest-integration/harness-claude-README.md`)::

    claude -p --input-format stream-json --output-format stream-json --verbose
           --include-partial-messages
           [--model M] [--effort low|medium|high|xhigh|max]
           [--session-id <uuid4> | --resume <id> [--fork-session]]
           [--permission-mode bypassPermissions --dangerously-skip-permissions]   # bypass
           [--permission-prompts host --permission-prompt-tool stdio]             # ask
           [--permission-prompts none]                                            # deny
           [--settings <file>]

`-p` skips the workspace-trust dialog, `--dangerously-skip-permissions` needs no first-run
acceptance, and a settings file that fails to validate is silently ignored in this mode.

**What the CLI actually does**, which is what the rest of this module is shaped by:

* Nothing at all is printed until the host speaks. The `system`/`init` message that carries the
  session id, the model and the tool list arrives **after the first user message**, not at
  startup. So `start()` cannot wait for it. What `start()` does wait for is the answer to an
  `initialize` control request, which the CLI answers immediately and for free (no model turn):
  that is the liveness check, and `started` is emitted from the first `init` of the first turn.
* A turn is one `{"type":"user",...}` line in and everything up to the next `{"type":"result"}`
  out. Text and thinking arrive as `stream_event` deltas, tool calls as `tool_use` blocks on
  `assistant` messages, their results as `tool_result` blocks on `user` messages.
* `control_request` in both directions. The CLI asks `can_use_tool` (only with
  `--permission-prompt-tool stdio`); we ask `initialize`, `interrupt` and `set_model`. An
  unknown subtype comes back as `{"subtype": "error", "error": "Unsupported control request
  subtype: …"}`, which is how `set_model` falls back to a restart on an older CLI.
* **There is no control request for the effort.** `set_effort`, `setEffort` and
  `set_reasoning_effort` were each tried against 2.1.278 on 2026-09-19 and each came back
  "Unsupported control request subtype" (the probe is free: the CLI answers before any model
  turn). The effort is a command-line flag and nothing else, so `set_effort()` is the same
  restart `set_model()` falls back to — a new process on the same session, between turns.
* A session claude has not written yet cannot be resumed: `--resume <a fresh uuid>` exits 1 with
  "No conversation found with session ID" (measured the same day). So a restart before the first
  turn starts a *fresh* process under the same `--session-id` instead, which loses nothing.
* An interrupted turn ends with `subtype: "error_during_execution"`, `is_error: true`,
  `terminal_reason: "aborted_tools"` or `"aborted_streaming"` and **no `result` field** — so
  `is_error` alone does not mean the turn failed. The process survives it and takes the next
  turn on the same session.
* There is no context-window percentage on this stream (the statusline's
  `context_window.used_percentage` is not sent). `result.modelUsage[<model>].contextWindow`
  carries the window, so `context_pct` is derived from the last request's prompt size, and the
  window and the prompt travel beside it as `context_window` / `context_tokens` (GT7X t:a3).
* **A running tool prints nothing on this stream.** The contract has `tool_output` for the output
  a call produces while it runs (a five-minute build), and this adapter never emits it, because
  2.1.278 does not send that text to a stream-json host. What was checked, on 2026-09-19, against
  the installed binary and for free:
  - `--include-partial-messages`' `stream_event` frames are the Anthropic API's own SSE events for
    the *model's* message — `text_delta`, `thinking_delta`, `input_json_delta`. `input_json_delta`
    streams the tool's **input** being written (the command being typed), never its output.
  - The CLI does have a live Bash stream internally: a `progress` message whose data is
    `{type: "bash_progress", output, fullOutput, elapsedTimeSeconds, totalLines, totalBytes}`.
    The stream-json serialiser turns it into the wire message `tool_progress {tool_use_id,
    tool_name, parent_tool_use_id, elapsed_time_seconds, task_id?, heartbeat?}` — and **drops
    `output` and `fullOutput`**. So what a host can have is how long the call has been running,
    not a byte of what it printed. `tool_heartbeat` does the same for every other tool.
  - `--include-hook-events` adds the hook lifecycle (PreToolUse / PostToolUse); a hook fires
    before or after a tool, never during one, so there is nothing incremental there either.
  `tool_progress` is therefore the only per-tool liveness claude offers, and elapsed seconds are
  not output: turning them into `tool_output` text would be inventing output the guest never
  produced. It is ignored here (`_dispatch`), and what the pane should do with an elapsed-seconds
  tick is a GUI decision, not this module's.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 29. Card:
issues/features/2026-09-19-claude-codex-guest-integration.md (GT7X, task t:x2).
"""
from __future__ import annotations

import difflib
import json
import logging
import os
import queue
import shutil
import subprocess
import threading
import time
import uuid

from .guest_harness import (HarnessError, HarnessEvent, HarnessNotAvailable, HarnessStart,
                            TurnResult, approval_scope, map_tool_name, validate_effort,
                            validate_permissions)

GUEST = "claude"
BINARY = "claude"                  # what `start()` looks for on PATH

# The flags that never change. `--verbose` is what makes -p emit anything but the final answer;
# `--include-partial-messages` is what makes it stream deltas rather than whole messages.
BASE_FLAGS = ("-p", "--input-format", "stream-json", "--output-format", "stream-json",
              "--verbose", "--include-partial-messages")

# `--effort <level>`, as `claude --help` lists them (2.1.278). Checked here rather than passed
# through, because an effort the CLI does not know makes the process exit at startup — and the
# pane would see "the guest stopped" instead of "claude has no such level".
EFFORTS = ("low", "medium", "high", "xhigh", "max")

# The model aliases `--model` documents ("an alias for the latest model (e.g. 'fable', 'opus', or
# 'sonnet') or a model's full name"). There is nothing on the stream-json protocol that lists the
# models, and `claude --help` is the only catalogue there is, so `models()` is this list plus
# whichever full name the running session reported (29.3).
MODEL_ALIASES = ("fable", "opus", "sonnet", "haiku")

# The key test's one turn (keytest, protocol 13.8 for a guest): `--tools ""` is how `claude -p`
# runs with no built-in tool at all, so the probe can neither read nor run anything, whatever the
# prompt says. `for_probe()` is the constructor that adds it.
PROBE_FLAGS = ("--tools", "")

# `claude auth status` (2.1.278): JSON by default (`--json` is the documented default, `--text`
# the other), with `loggedIn` as the one field this reads — `{"loggedIn": true, "authMethod":
# "claude.ai", "apiProvider": "firstParty", "email": …, "subscriptionType": "max", …}`. It is
# a free local read of the credential store, no network and no model turn.
LOGIN_STATUS_ARGS = ("auth", "status", "--json")

# One posture, one set of flags (guest_harness.PERMISSIONS).
PERMISSION_FLAGS = {
    "bypass": ("--permission-mode", "bypassPermissions", "--dangerously-skip-permissions"),
    # `stdio` is the sentinel that routes every prompt to us as a `can_use_tool` control request.
    # Without it `--permission-prompts host` still auto-denies in -p mode (measured 2026-09-19).
    "ask": ("--permission-prompts", "host", "--permission-prompt-tool", "stdio"),
    "deny": ("--permission-prompts", "none"),
}

# Claude Code's own tools, by the shape of the approval they raise (guest_harness: approval.kind).
_COMMAND_TOOLS = ("Bash", "BashOutput", "KillShell")
_PATCH_TOOLS = ("Edit", "Write", "MultiEdit", "NotebookEdit")
# The tools whose result carries an edit; a `diff` is computed for these and no others.
_EDIT_TOOLS = _PATCH_TOOLS
# The tool Claude Code asks a structured question with (protocol 27).
_QUESTION_TOOL = "AskUserQuestion"

# A turn's `result` arrives on its own; nothing else is waited for.
INIT_TIMEOUT = 30.0                # seconds to wait for the `initialize` control response
CONTROL_TIMEOUT = 15.0             # ... for any other control response we ask for
CLOSE_GRACE = 2.0                  # seconds between closing stdin, SIGTERM and SIGKILL
_POLL = 0.2                        # inbox poll, so `cancel` is noticed promptly

log = logging.getLogger("relay.guest_harness_claude")

_EOF = object()                    # the reader thread's "the process stopped talking"


def child_environment(env: dict | None = None) -> dict:
    """The environment one guest claude is started with.

    Relay may itself be launched from inside a Claude Code session, and a child that inherits
    `CLAUDECODE` / `CLAUDE_CODE_*` / `CLAUDE_EFFORT` inherits that session's posture with them —
    a colleague found transcript saving switched off in the child that way. The guest is its own
    session, so those variables are removed rather than passed on.
    """
    out = dict(os.environ if env is None else env)
    for key in list(out):
        if key == "CLAUDECODE" or key == "CLAUDE_EFFORT" or key.startswith("CLAUDE_CODE_"):
            out.pop(key, None)
    return out


def _spawn(cmd: list[str], cwd: str, env: dict):
    return subprocess.Popen(cmd, cwd=cwd, env=env, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, bufsize=1)


def parse_login_status(returncode: int, stdout: str, stderr: str = "") -> bool:
    """Whether `claude auth status --json` says the CLI is signed in.

    The JSON's `loggedIn` is the answer when the output parses; a CLI that printed something else
    (an older one answering in text) is read by its exit status, which is 0 only when signed in.
    """
    text = (stdout or "").strip()
    if text:
        try:
            data = json.loads(text)
        except ValueError:
            data = None
        if isinstance(data, dict) and isinstance(data.get("loggedIn"), bool):
            return data["loggedIn"]
        lowered = (text + "\n" + (stderr or "")).lower()
        if "not logged in" in lowered:
            return False
    return returncode == 0


# ----- diffs -------------------------------------------------------------------------------


def _unified(path: str, before: str, after: str) -> str:
    """A unified diff of one file's before and after, in the shape protocol 23 prints."""
    a = before.splitlines(keepends=True) if before else []
    b = after.splitlines(keepends=True) if after else []
    if a and not a[-1].endswith("\n"):
        a[-1] += "\n"
    if b and not b[-1].endswith("\n"):
        b[-1] += "\n"
    name = path or "file"
    out = "".join(difflib.unified_diff(a, b, fromfile=f"a/{name}", tofile=f"b/{name}"))
    return out


def _from_structured_patch(path: str, hunks) -> str:
    """Render Claude Code's own `structuredPatch` (the hunks its Edit/Write results carry)."""
    if not isinstance(hunks, list) or not hunks:
        return ""
    name = path or "file"
    lines = [f"--- a/{name}\n", f"+++ b/{name}\n"]
    for hunk in hunks:
        if not isinstance(hunk, dict):
            continue
        lines.append("@@ -%s,%s +%s,%s @@\n" % (hunk.get("oldStart", 0), hunk.get("oldLines", 0),
                                                hunk.get("newStart", 0), hunk.get("newLines", 0)))
        for line in hunk.get("lines") or []:
            lines.append(f"{line}\n")
    return "".join(lines) if len(lines) > 2 else ""


def tool_diff(tool: str, tool_input: dict, result_meta) -> str:
    """The unified diff of the edit a tool made, or "" when there is none to be had.

    `result_meta` is the `tool_use_result` the CLI puts beside a `tool_result` block; it carries
    `structuredPatch` / `originalFile` / `content` for Edit and Write. When the stream carries no
    file content (an older CLI, or a tool we do not know), the diff is rebuilt from the tool's own
    input — `old_string`/`new_string` for Edit, the whole file for Write, each edit in turn for
    MultiEdit — and when even that is missing the caller simply omits `diff`.
    """
    if tool not in _EDIT_TOOLS:
        return ""
    tool_input = tool_input if isinstance(tool_input, dict) else {}
    path = str(tool_input.get("file_path") or tool_input.get("notebook_path") or "")
    if isinstance(result_meta, dict):
        path = str(result_meta.get("filePath") or path)
        patch = _from_structured_patch(path, result_meta.get("structuredPatch"))
        if patch:
            return patch
        original, content = result_meta.get("originalFile"), result_meta.get("content")
        if isinstance(content, str) and (isinstance(original, str) or original is None):
            return _unified(path, original or "", content)
    if tool == "Write" and isinstance(tool_input.get("content"), str):
        return _unified(path, "", tool_input["content"])
    if tool == "Edit":
        old, new = tool_input.get("old_string"), tool_input.get("new_string")
        if isinstance(old, str) and isinstance(new, str):
            return _unified(path, old, new)
    if tool == "MultiEdit":
        parts = []
        for edit in tool_input.get("edits") or []:
            if isinstance(edit, dict) and isinstance(edit.get("old_string"), str) \
                    and isinstance(edit.get("new_string"), str):
                parts.append(_unified(path, edit["old_string"], edit["new_string"]))
        return "".join(p for p in parts if p)
    return ""


# ----- the harness ---------------------------------------------------------------------------


class ClaudeHarness:
    """One `claude` process for one pane. Matches `guest_harness.Harness`.

    `spawn` is the only seam: tests hand in a fake process so the whole protocol can be replayed
    from a recorded transcript without starting the real CLI.
    """

    guest = GUEST

    @classmethod
    def for_probe(cls, **kwargs) -> "ClaudeHarness":
        """The harness the key test drives for its one turn: the same process, started with no
        built-in tools (PROBE_FLAGS), so the prompt is the only thing it can answer with."""
        extra = list(kwargs.pop("extra_args", None) or ()) + list(PROBE_FLAGS)
        return cls(extra_args=extra, **kwargs)

    def __init__(self, *, settings: str | None = None, binary: str = BINARY, spawn=None,
                 extra_args: list[str] | None = None):
        self._settings = settings
        self._binary = binary
        self._spawn = spawn or _spawn
        self._extra_args = list(extra_args or ())

        self._proc = None
        self._cwd = ""
        self._permissions = "bypass"
        self._session_id = ""
        self._model = ""
        self._effort = ""
        self._effort_pending = False
        # Whether the CLI has opened this session yet (its first `system`/`init` came). Until it
        # has, there is no transcript on disk and `--resume` would be refused, so a relaunch
        # starts fresh under the same id instead. Never cleared by `close()`: the session is on
        # disk from then on, whatever this object does.
        self._resumable = False
        self._tools: list[str] = []

        self._inbox: queue.Queue = queue.Queue()      # everything a turn reads
        self._pending: dict[str, list] = {}           # our control requests, by request_id
        self._approvals: dict[str, dict] = {}         # the CLI's, by request_id
        self._tool_calls: dict[str, dict] = {}        # tool_use id -> {tool, input, started}
        self._stderr: list[str] = []
        self._write_lock = threading.Lock()
        self._turn_lock = threading.RLock()
        self._state_lock = threading.Lock()
        self._started_emitted = False
        self._interrupted = False
        self._readers: list[threading.Thread] = []

    # ----- lifecycle ---------------------------------------------------------------------

    def _argv(self, *, model: str | None, session_id: str | None, resume: str | None,
              fork: bool, permissions: str, effort: str | None = None) -> list[str]:
        argv = [self._binary, *BASE_FLAGS]
        if model:
            argv += ["--model", model]
        if effort:
            argv += ["--effort", effort]
        if resume:
            argv += ["--resume", resume]
            if fork:
                argv.append("--fork-session")
        elif session_id:
            argv += ["--session-id", session_id]
        argv += list(PERMISSION_FLAGS[permissions])
        if self._settings:
            argv += ["--settings", self._settings]
        argv += self._extra_args
        return argv

    def start(self, *, cwd: str, model: str | None = None, resume: str | None = None,
              fork: bool = False, permissions: str = "bypass",
              effort: str | None = None) -> HarnessStart:
        permissions = validate_permissions(permissions)
        effort = self._level(validate_effort(effort)) if effort else None
        if self._proc is not None:
            raise HarnessError("this claude harness is already started.")
        if shutil.which(self._binary) is None:
            raise HarnessNotAvailable(
                f"Claude Code is not installed here ({self._binary} is not on PATH).")
        if not cwd or not os.path.isdir(cwd):
            raise HarnessNotAvailable(f"{cwd or '(no directory)'} is not a directory to start in.")

        self._cwd = cwd
        self._permissions = permissions
        self._model = model or ""
        self._effort = effort or ""
        # A fork gets its id from the CLI (it makes a new one); everything else we name ourselves,
        # so the pane has a session id to file the transcript under before the first turn.
        new_id = "" if resume else str(uuid.uuid4())
        self._session_id = "" if (resume and fork) else (resume or new_id)
        argv = self._argv(model=model, session_id=new_id or None, resume=resume, fork=fork,
                          permissions=permissions, effort=effort)
        self._launch(argv)
        self._handshake()
        return HarnessStart(session_id=self._session_id, model=self._model)

    def _launch(self, argv: list[str]) -> None:
        log.debug("starting claude harness: %s (cwd=%s)", " ".join(argv), self._cwd)
        try:
            self._proc = self._spawn(argv, self._cwd, child_environment())
        except FileNotFoundError as exc:
            raise HarnessNotAvailable(f"Claude Code could not be started: {exc}") from exc
        except OSError as exc:
            raise HarnessNotAvailable(f"Claude Code could not be started: {exc}") from exc
        self._readers = [
            threading.Thread(target=self._read_stdout, args=(self._proc,),
                             name="claude-harness-out", daemon=True),
            threading.Thread(target=self._read_stderr, args=(self._proc,),
                             name="claude-harness-err", daemon=True)]
        for reader in self._readers:
            reader.start()

    def _handshake(self) -> None:
        """Ask the CLI to initialize. Free (no model turn) and answered at once, so it is the
        one thing `start()` can hold the process to before a prompt exists."""
        try:
            reply = self._control({"subtype": "initialize"}, timeout=INIT_TIMEOUT)
        except HarnessError as exc:
            self.close()
            raise HarnessNotAvailable(f"Claude Code did not start: {exc}") from exc
        if reply is None:
            # An older CLI may not answer `initialize`. Only a dead process is fatal here.
            if self._proc is not None and self._proc.poll() is not None:
                self.close()
                raise HarnessNotAvailable(
                    "Claude Code stopped before it said anything." + self._stderr_tail())
            log.debug("claude harness: no initialize response; carrying on")
            return
        account = reply.get("account") if isinstance(reply, dict) else None
        if isinstance(account, dict):
            log.debug("claude harness: %s, %s", account.get("subscriptionType"),
                      account.get("apiProvider"))

    def close(self) -> None:
        proc, self._proc = self._proc, None
        self._started_emitted = False       # a later start() is a new session to announce
        if proc is None:
            return
        try:
            if proc.stdin is not None and not proc.stdin.closed:
                proc.stdin.close()
        except Exception:
            pass
        for step in ("wait", "terminate", "kill"):
            if proc.poll() is not None:
                break
            if step == "terminate":
                try:
                    proc.terminate()
                except Exception:
                    pass
            elif step == "kill":
                try:
                    proc.kill()
                except Exception:
                    pass
            try:
                proc.wait(timeout=CLOSE_GRACE)
            except Exception:
                pass
        # Only once the readers have run out of pipe: closing a stream another thread is still
        # reading from is how a file descriptor gets reused under it.
        readers, self._readers = self._readers, []
        for reader in readers:
            reader.join(timeout=CLOSE_GRACE)
        for stream in (proc.stdout, proc.stderr):
            if stream is not None and not getattr(stream, "closed", True) \
                    and not any(r.is_alive() for r in readers):
                try:
                    stream.close()
                except Exception:
                    pass
        self._fail_pending("the guest was closed.")

    # ----- the pipes ---------------------------------------------------------------------

    def _read_stdout(self, proc) -> None:
        try:
            for line in proc.stdout:
                line = line.strip()
                if not line:
                    continue
                try:
                    message = json.loads(line)
                except ValueError:
                    log.debug("claude harness: not JSON on stdout: %.200s", line)
                    continue
                if not isinstance(message, dict):
                    log.debug("claude harness: JSON that is not an object: %.200s", line)
                    continue
                if message.get("type") == "control_response":
                    self._resolve(message)
                    continue
                self._inbox.put(message)
        except Exception as exc:                       # the pipe went away under us
            log.debug("claude harness: stdout reader stopped: %s", exc)
        finally:
            self._inbox.put(_EOF)
            self._fail_pending("the guest stopped.")

    def _read_stderr(self, proc) -> None:
        try:
            for line in proc.stderr:
                line = line.rstrip()
                if not line:
                    continue
                with self._state_lock:
                    self._stderr.append(line)
                    del self._stderr[:-20]
                log.debug("claude harness stderr: %.300s", line)
        except Exception:
            pass

    def _stderr_tail(self, limit: int = 5) -> str:
        with self._state_lock:
            tail = list(self._stderr)[-limit:]
        return ("\n" + "\n".join(tail)) if tail else ""

    def _write(self, message: dict) -> None:
        proc = self._proc
        if proc is None or proc.stdin is None:
            raise HarnessError("the guest is not running.")
        line = json.dumps(message, ensure_ascii=False) + "\n"
        with self._write_lock:
            try:
                proc.stdin.write(line)
                proc.stdin.flush()
            except (BrokenPipeError, ValueError, OSError) as exc:
                raise HarnessError(
                    f"the guest stopped listening: {exc}{self._stderr_tail()}") from exc

    # ----- control requests ---------------------------------------------------------------

    def _resolve(self, message: dict) -> None:
        body = message.get("response") if isinstance(message.get("response"), dict) else {}
        request_id = str(body.get("request_id") or "")
        with self._state_lock:
            slot = self._pending.pop(request_id, None)
        if slot is None:
            log.debug("claude harness: control response for an unknown request %r", request_id)
            return
        slot[0] = body
        slot[1].set()

    def _fail_pending(self, why: str) -> None:
        with self._state_lock:
            pending, self._pending = self._pending, {}
        for slot in pending.values():
            slot[0] = {"subtype": "error", "error": why}
            slot[1].set()

    def _control(self, request: dict, *, timeout: float = CONTROL_TIMEOUT, wait: bool = True):
        """Send one control request. Returns its `response` body, or None when it did not come.
        Raises HarnessError when the CLI answered with an error."""
        request_id = str(uuid.uuid4())
        slot = [None, threading.Event()]
        if wait:
            with self._state_lock:
                self._pending[request_id] = slot
        self._write({"type": "control_request", "request_id": request_id, "request": request})
        if not wait:
            return None
        if not slot[1].wait(timeout):
            with self._state_lock:
                self._pending.pop(request_id, None)
            return None
        body = slot[0] or {}
        if body.get("subtype") == "error":
            raise HarnessError(str(body.get("error") or "the guest refused the request."))
        response = body.get("response")
        return response if response is not None else {}

    # ----- one turn -----------------------------------------------------------------------

    def send(self, prompt: str, *, attachments: list[dict] | None = None, emit,
             cancel: threading.Event) -> TurnResult:
        with self._turn_lock:
            # An effort asked for while the last turn was running is applied here, before this
            # one starts: it is a command-line flag, so applying it is a restart (`set_effort`).
            self._apply_effort()
            if self._proc is None:
                raise HarnessError("the guest is not running.")
            self._interrupted = False
            try:
                return self._run_turn(prompt, attachments or [], emit, cancel)
            finally:
                self._deny_stale_approvals()

    def _user_message(self, prompt: str, attachments: list[dict]) -> dict:
        content: list[dict] = [{"type": "text", "text": prompt or ""}]
        for item in attachments:
            if not isinstance(item, dict) or item.get("kind") != "image":
                continue
            data, media = item.get("data"), item.get("media_type")
            if not data or not media:
                continue
            content.append({"type": "image",
                            "source": {"type": "base64", "media_type": media, "data": data}})
        return {"type": "user", "message": {"role": "user", "content": content}}

    def _run_turn(self, prompt: str, attachments: list[dict], emit, cancel) -> TurnResult:
        held: list[dict] = []
        while True:                                    # a turn starts on an empty inbox
            try:
                stale = self._inbox.get_nowait()
            except queue.Empty:
                break
            if stale is _EOF:
                raise HarnessError("the guest stopped." + self._stderr_tail())
            if isinstance(stale, dict) and stale.get("type") == "rate_limit_event":
                held.append(stale)         # a usage figure is worth keeping; the rest is stale
        self._write(self._user_message(prompt, attachments))

        state = {"text": [], "saw_delta": False, "asked_stop": False}
        for message in held[-1:]:                      # only the newest figure is a figure
            self._dispatch(message, state, emit)
        while True:
            if cancel.is_set() and not state["asked_stop"]:
                state["asked_stop"] = True
                self.interrupt()
            try:
                message = self._inbox.get(timeout=_POLL)
            except queue.Empty:
                continue
            if message is _EOF:
                raise HarnessError("the guest stopped in the middle of a turn."
                                   + self._stderr_tail())
            if message.get("type") == "result":
                return self._finish(message, state, emit)
            try:
                self._dispatch(message, state, emit)
            except HarnessError:
                raise
            except Exception as exc:                   # one odd message must not end the pane
                log.debug("claude harness: could not handle %r: %s", message.get("type"), exc)

    def _dispatch(self, message: dict, state: dict, emit) -> None:
        kind = message.get("type")
        if kind == "system":
            self._on_system(message, emit)
        elif kind == "stream_event":
            self._on_stream_event(message, state, emit)
        elif kind == "assistant":
            self._on_assistant(message, state, emit)
        elif kind == "user":
            self._on_user(message, emit)
        elif kind == "control_request":
            self._on_control_request(message, emit)
        elif kind == "rate_limit_event":
            data = _limits_event(message)
            if data:
                emit(HarnessEvent("limits", data))
        elif kind in ("prompt_suggestion", "result", "tool_progress"):
            # `tool_progress` is a running call's elapsed seconds (and `heartbeat` for the tools
            # that have no stream of their own). It carries no output — the CLI drops
            # `bash_progress.output` on its way to stream-json — so there is nothing here to make
            # a `tool_output` event out of; see the module docstring.
            pass
        else:
            log.debug("claude harness: ignoring a %r message", kind)

    def _on_system(self, message: dict, emit) -> None:
        subtype = message.get("subtype")
        if subtype == "init":
            with self._state_lock:
                self._resumable = True      # the CLI has opened the session; --resume works now
                self._session_id = str(message.get("session_id") or self._session_id)
                self._model = str(message.get("model") or self._model)
                tools = message.get("tools")
                self._tools = [str(t) for t in tools] if isinstance(tools, list) else []
            if not self._started_emitted:
                self._started_emitted = True
                emit(HarnessEvent("started", {"session_id": self._session_id,
                                              "model": self._model}))
        elif subtype == "status":
            if message.get("status") == "compacting":
                emit(HarnessEvent("notice", {"text": "Claude is compacting its context."}))
            elif message.get("compact_result") == "failed":
                emit(HarnessEvent("notice", {
                    "text": "Claude could not compact: "
                            + str(message.get("compact_error") or "no reason given")}))
            elif message.get("compact_result") == "success":
                emit(HarnessEvent("notice", {"text": "Claude compacted its context."}))
        elif subtype == "permission_denied":
            emit(HarnessEvent("notice", {
                "text": str(message.get("message")
                            or f"Claude was denied {message.get('tool_name') or 'a tool'}.")}))
        elif subtype == "compact_boundary":
            emit(HarnessEvent("notice", {"text": "Claude compacted its context."}))
        else:
            log.debug("claude harness: ignoring system/%s", subtype)

    def _on_stream_event(self, message: dict, state: dict, emit) -> None:
        event = message.get("event")
        if not isinstance(event, dict) or event.get("type") != "content_block_delta":
            return
        delta = event.get("delta")
        if not isinstance(delta, dict):
            return
        if delta.get("type") == "text_delta":
            text = delta.get("text") or ""
            if text:
                state["saw_delta"] = True
                emit(HarnessEvent("delta", {"text": text}))
        elif delta.get("type") == "thinking_delta":
            text = delta.get("thinking") or ""
            if text:
                emit(HarnessEvent("thinking", {"text": text}))

    def _on_assistant(self, message: dict, state: dict, emit) -> None:
        body = message.get("message")
        if not isinstance(body, dict):
            return
        model = body.get("model")
        if isinstance(model, str) and model and not model.startswith("<"):
            with self._state_lock:
                self._model = model
        for block in body.get("content") or []:
            if not isinstance(block, dict):
                continue
            if block.get("type") == "text":
                text = block.get("text") or ""
                if text:
                    state["text"].append(text)
                    # Without partial messages (or if the CLI ever stops sending them) the pane
                    # would print nothing at all, so the whole block goes out once instead.
                    if not state["saw_delta"]:
                        emit(HarnessEvent("delta", {"text": text}))
            elif block.get("type") == "tool_use":
                self._on_tool_use(block, emit)

    def _on_tool_use(self, block: dict, emit) -> None:
        call_id = str(block.get("id") or "")
        name = str(block.get("name") or "")
        tool_input = block.get("input") if isinstance(block.get("input"), dict) else {}
        with self._state_lock:
            self._tool_calls[call_id] = {"tool": name, "input": tool_input,
                                         "started": time.monotonic()}
        data = {"call_id": call_id, "tool": map_tool_name(GUEST, name),
                "input": dict(tool_input, _guest_tool=name)}
        label = tool_input.get("description")
        if isinstance(label, str) and label.strip():
            data["label"] = label.strip()
        emit(HarnessEvent("tool_started", data))

    def _on_user(self, message: dict, emit) -> None:
        body = message.get("message")
        if not isinstance(body, dict):
            return
        content = body.get("content")
        if not isinstance(content, list):
            return
        meta = message.get("tool_use_result")
        for block in content:
            if not isinstance(block, dict) or block.get("type") != "tool_result":
                continue
            call_id = str(block.get("tool_use_id") or "")
            with self._state_lock:
                call = self._tool_calls.pop(call_id, None)
            name = str(call.get("tool") if call else "")
            data = {"call_id": call_id, "tool": map_tool_name(GUEST, name),
                    "output": _result_text(block.get("content")),
                    "ok": not bool(block.get("is_error"))}
            if call:
                data["ms"] = int((time.monotonic() - call["started"]) * 1000)
                diff = tool_diff(name, call["input"], meta)
                if diff:
                    data["diff"] = diff
            emit(HarnessEvent("tool_result", data))

    def _on_control_request(self, message: dict, emit) -> None:
        request = message.get("request") if isinstance(message.get("request"), dict) else {}
        request_id = str(message.get("request_id") or "")
        subtype = request.get("subtype")
        if subtype != "can_use_tool":
            log.debug("claude harness: unsupported control request %r from the guest", subtype)
            self._write({"type": "control_response", "response": {
                "subtype": "error", "request_id": request_id,
                "error": f"Relay does not handle {subtype!r}."}})
            return
        name = str(request.get("tool_name") or "")
        tool_input = request.get("input") if isinstance(request.get("input"), dict) else {}
        with self._state_lock:
            self._approvals[request_id] = {"tool": name, "input": tool_input}
        if name == _QUESTION_TOOL:
            questions = tool_input.get("questions")
            emit(HarnessEvent("question", {
                "id": request_id,
                "questions": questions if isinstance(questions, list) else []}))
            return
        emit(HarnessEvent("approval", {
            "id": request_id, "kind": _approval_kind(name),
            "detail": _approval_detail(name, request, tool_input)}))

    def _finish(self, message: dict, state: dict, emit) -> TurnResult:
        usage = _usage_event(message)
        if usage:
            emit(HarnessEvent("usage", usage))
        text = message.get("result")
        if not isinstance(text, str) or not text.strip():
            text = "".join(state["text"])
        interrupted = self._interrupted or str(message.get("terminal_reason") or "").startswith(
            "aborted")
        if interrupted:
            emit(HarnessEvent("done", {"text": text, "stop_reason": "interrupted"}))
            return TurnResult(text=text, stop_reason="interrupted",
                              usage=usage or {})
        if message.get("is_error"):
            why = _error_text(message) or "Claude Code ended the turn with an error."
            emit(HarnessEvent("error", {"text": why, "code": str(message.get("subtype") or "")}))
            raise HarnessError(why)
        emit(HarnessEvent("done", {"text": text, "stop_reason": "end"}))
        return TurnResult(text=text, stop_reason="end", usage=usage or {})

    # ----- the rest of the contract ---------------------------------------------------------

    def interrupt(self) -> None:
        """Ask the CLI to abort the running turn. `interrupt` is answered even when nothing is
        running (`{"still_queued": []}`), so this is safe to call at any time."""
        if self._proc is None:
            return
        self._interrupted = True
        self._deny_stale_approvals(reason="the turn was interrupted.")
        try:
            self._control({"subtype": "interrupt"}, wait=False)
        except HarnessError as exc:
            log.debug("claude harness: could not interrupt: %s", exc)

    def set_model(self, model: str) -> str:
        """Switch the model for the next turn. 2.1.278 answers `set_model` with a bare success
        and does not name the model back, so the requested name is what is returned until the
        next turn's messages report what the CLI actually ran."""
        model = (model or "").strip()
        if not model:
            raise HarnessError("a model name is needed.")
        if self._proc is None:
            raise HarnessError("the guest is not running.")
        try:
            self._control({"subtype": "set_model", "model": model})
        except HarnessError as exc:
            if "unsupported" not in str(exc).lower():
                raise
            log.debug("claude harness: set_model unsupported, restarting on %s", model)
            self._restart(model)
        with self._state_lock:
            self._model = model
        return model

    def _restart(self, model: str) -> None:
        """The fallback for a CLI without `set_model`: the same session, on the new model."""
        if not self._session_id:
            raise HarnessError("the guest has no session to resume on a new model yet.")
        self._relaunch(model=model, effort=self._effort or None)

    def _relaunch(self, *, model: str | None, effort: str | None) -> None:
        """Start the guest again with the flags it should have now, keeping its session.

        A session the CLI has already opened is continued with `--resume <id>`; one it has not
        written yet is started again under the same `--session-id`, because `--resume` on an id
        with no transcript exits 1 ("No conversation found with session ID") and there is nothing
        to carry over anyway. Either way the pane keeps the id it has been showing.
        """
        session_id = self._session_id or str(uuid.uuid4())
        self._session_id = session_id
        resume = session_id if self._resumable else None
        self.close()
        self._started_emitted = False
        self._inbox = queue.Queue()
        self._launch(self._argv(model=model, session_id=None if resume else session_id,
                                resume=resume, fork=False, permissions=self._permissions,
                                effort=effort))
        self._handshake()

    # ----- the reasoning effort -------------------------------------------------------------

    def _level(self, effort: str | None) -> str:
        """`effort` as this CLI spells it, or a HarnessError naming what it does have."""
        if not effort:
            raise HarnessError("a reasoning effort is needed.")
        if effort not in EFFORTS:
            raise HarnessError(
                f"Claude Code takes one of {', '.join(EFFORTS)} for its reasoning effort, "
                f"not {effort!r}.")
        return effort

    def set_effort(self, effort: str) -> str:
        """Switch the reasoning effort, in force from the next turn.

        There is no control request for it (see the module docstring: three spellings were tried
        against 2.1.278 and each was "Unsupported control request subtype"), so this is the same
        restart `set_model` falls back to: a new process on the same session, with `--effort` on
        its command line. A turn in flight keeps the effort it started with — restarting under it
        would kill it — and the new process is started at the top of the next `send()`.
        """
        level = self._level(validate_effort(effort))
        if self._proc is None:
            raise HarnessError("the guest is not running.")
        with self._state_lock:
            if level == self._effort:
                return level
            self._effort = level
            self._effort_pending = True
        self._apply_effort()
        return level

    def _apply_effort(self) -> bool:
        """Restart on the effort that is waiting, if no turn is running. True when it happened."""
        if not self._turn_lock.acquire(blocking=False):
            return False                     # a turn owns the process; the next `send()` does it
        try:
            with self._state_lock:
                if not self._effort_pending or self._proc is None:
                    return False
                self._effort_pending = False
                effort = self._effort
            self._relaunch(model=self._model or None, effort=effort or None)
            return True
        finally:
            self._turn_lock.release()

    def models(self) -> list[dict]:
        """The aliases `--model` documents, each with the five levels `--effort` takes (29.3).

        Claude Code publishes no catalogue on this protocol — `claude --help` is the only list
        there is — so this is static, plus the full model name the running session reported when
        that is not one of the aliases, marked as the one in force.
        """
        with self._state_lock:
            running = self._model
        # "claude opus", not "opus" (owner, 2026-09-20): the family name on the row, lower-case
        # like every other model label; the id stays the alias `--model` takes.
        rows = [{"id": alias, "label": "claude " + alias, "efforts": list(EFFORTS),
                 "default_effort": None} for alias in MODEL_ALIASES]
        known = {row["id"] for row in rows}
        if running and running not in known:
            rows.append({"id": running, "label": running, "efforts": list(EFFORTS),
                         "default_effort": None})
        for row in rows:
            if running and row["id"] == running:
                row["current"] = True
        return rows

    @property
    def effort(self) -> str:
        """The effort the guest is running on, or "" when it was never set (its own default)."""
        with self._state_lock:
            return self._effort

    def compact(self) -> None:
        """`/compact` as an ordinary user message — there is no control request for it, and the
        CLI does treat a slash command on this stream as one (`status: "compacting"`). It runs
        as a turn of its own, so it waits for any turn in flight and its `result` is swallowed
        rather than reported; `notice` events on the next turn say what became of it."""
        if self._proc is None:
            return
        threading.Thread(target=self._compact_now, name="claude-harness-compact",
                         daemon=True).start()

    def _compact_now(self) -> None:
        with self._turn_lock:
            if self._proc is None:
                return
            try:
                self._write({"type": "user", "message": {
                    "role": "user", "content": [{"type": "text", "text": "/compact"}]}})
            except HarnessError as exc:
                log.debug("claude harness: could not compact: %s", exc)
                return
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                try:
                    message = self._inbox.get(timeout=_POLL)
                except queue.Empty:
                    continue
                if message is _EOF:
                    self._inbox.put(_EOF)
                    return
                if message.get("type") == "result":
                    log.debug("claude harness: compaction finished (%s)", message.get("subtype"))
                    return

    def answer(self, request_id: str, decision: dict) -> None:
        """Answer one `approval` or `question` the guest raised. Both go back as the
        `control_response` to its `can_use_tool` request: an allowed tool runs with the input it
        asked for, a denied one gets the message as its tool result — which is also how a
        question's answers reach the conversation.

        `decision["scope"]` (guest_harness.APPROVAL_SCOPES) is expressible here, which is not what
        the contract assumes of every guest — both of claude's richer answers are fields on this
        same response, verified against 2.1.278's own validator on 2026-09-19:

        * `session` — an allow may carry `updatedPermissions`, a list of
          `{type: "addRules", behavior: "allow"|"deny"|"ask", rules: [{toolName, ruleContent?}],
          destination: "userSettings"|"projectSettings"|"localSettings"|"session"|"cliArg"}`.
          `destination: "session"` is exactly "for the rest of this session and no longer", and
          `_session_rule` makes the rule as narrow as the thing that was asked about (this
          command, this file), never a blanket "Bash is allowed now".
        * `stop` — a deny may carry `interrupt: true`, which the CLI logs as "SDK permission
          prompt deny+interrupt" and acts on by aborting the turn.

        Anything else is `once`, which is what this method did before the key existed.
        """
        request_id = str(request_id or "")
        decision = decision if isinstance(decision, dict) else {}
        with self._state_lock:
            pending = self._approvals.pop(request_id, None)
        if pending is None:
            log.debug("claude harness: an answer for %r that nothing is waiting on", request_id)
            return
        scope = approval_scope(decision)
        if "answers" in decision:
            body = {"behavior": "deny", "message": _answers_text(decision.get("answers"))}
        elif str(decision.get("behavior")) == "allow":
            body = {"behavior": "allow",
                    "updatedInput": decision.get("updatedInput") or pending["input"]}
            if scope == "session":
                rule = _session_rule(str(pending.get("tool") or ""), pending.get("input"))
                if rule is not None:
                    body["updatedPermissions"] = [rule]
        else:
            body = {"behavior": "deny",
                    "message": str(decision.get("message") or "The user said no.")}
            if scope == "stop":
                body["interrupt"] = True
                self._interrupted = True
        try:
            self._write({"type": "control_response", "response": {
                "subtype": "success", "request_id": request_id, "response": body}})
        except HarnessError as exc:
            log.debug("claude harness: could not answer %r: %s", request_id, exc)

    def _deny_stale_approvals(self, reason: str = "the turn ended.") -> None:
        with self._state_lock:
            stale = list(self._approvals)
        for request_id in stale:
            self.answer(request_id, {"behavior": "deny", "message": reason})

    @property
    def session_id(self) -> str:
        return self._session_id

    @property
    def model(self) -> str:
        return self._model

    @property
    def tools(self) -> list[str]:
        """What the guest said it can do, from the turn's `init` (empty before the first turn)."""
        with self._state_lock:
            return list(self._tools)


# ----- reading the CLI's shapes ----------------------------------------------------------------


def _result_text(content) -> str:
    """A tool_result's `content`: a string, or the text blocks of a list of blocks."""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts = []
        for block in content:
            if isinstance(block, dict):
                if block.get("type") == "text":
                    parts.append(str(block.get("text") or ""))
                elif block.get("type") == "image":
                    parts.append("[image]")
            elif isinstance(block, str):
                parts.append(block)
        return "\n".join(p for p in parts if p)
    if content is None:
        return ""
    return json.dumps(content, ensure_ascii=False)


def _session_rule(tool: str, tool_input) -> dict | None:
    """The `updatedPermissions` entry for "allow this, for the rest of the session".

    As narrow as what was asked about: the rule names the tool, and `ruleContent` the one command
    or the one path the approval was raised for, so allowing one `npm test` does not allow every
    Bash for the session. With nothing specific to name, the rule is the tool alone — which is all
    claude can be told, and is what "allow for session" means for a tool that takes no target.
    """
    tool = (tool or "").strip()
    if not tool:
        return None
    source = tool_input if isinstance(tool_input, dict) else {}
    content = None
    if tool in _COMMAND_TOOLS:
        content = source.get("command")
    else:
        for key in ("file_path", "notebook_path", "path", "url", "pattern"):
            if isinstance(source.get(key), str) and source[key].strip():
                content = source[key]
                break
    rule: dict = {"toolName": tool}
    if isinstance(content, str) and content.strip():
        rule["ruleContent"] = content.strip()
    return {"type": "addRules", "behavior": "allow", "rules": [rule], "destination": "session"}


def _approval_kind(tool: str) -> str:
    if tool in _COMMAND_TOOLS:
        return "command"
    if tool in _PATCH_TOOLS:
        return "patch"
    return "tool" if tool else "other"


def _approval_detail(tool: str, request: dict, tool_input: dict) -> str:
    """One line the pane can put the Allow / Deny question on."""
    for key in ("command", "file_path", "notebook_path", "url", "pattern", "path"):
        value = tool_input.get(key)
        if isinstance(value, str) and value.strip():
            return f"{tool}: {value.strip()}"
    described = request.get("description")
    if isinstance(described, str) and described.strip():
        return f"{tool}: {described.strip()}"
    return tool or "a tool"


def _answers_text(answers) -> str:
    """Protocol 27.3's answers as the sentence the guest reads back."""
    if isinstance(answers, list):
        picked = []
        for answer in answers:
            if isinstance(answer, list):
                picked.extend(str(a) for a in answer if str(a))
            elif answer:
                picked.append(str(answer))
        if picked:
            return "The user answered: " + "; ".join(picked)
    return "The user answered."


def _usage_event(message: dict) -> dict:
    """The `usage` event a `result` becomes. `context_pct` is derived — the CLI does not send
    the statusline's `context_window.used_percentage` on this stream — from the last request's
    prompt (fresh input + both cache counters) against the model's own context window."""
    usage = message.get("usage") if isinstance(message.get("usage"), dict) else {}
    model_usage = message.get("modelUsage") if isinstance(message.get("modelUsage"), dict) else {}
    data: dict = {}
    for key in ("input_tokens", "output_tokens", "cache_creation_input_tokens",
                "cache_read_input_tokens"):
        value = usage.get(key)
        if isinstance(value, (int, float)):
            data[key] = int(value)
    cost = message.get("total_cost_usd")
    if isinstance(cost, (int, float)):
        data["cost_usd"] = float(cost)
    model, window = "", 0
    for name, entry in model_usage.items():
        if isinstance(entry, dict) and isinstance(entry.get("contextWindow"), (int, float)):
            model, window = str(name), int(entry["contextWindow"])
            break
    if model:
        data["model"] = model
    if window > 0:
        # The guest's own context, as claude reports it: the window the model has and what the
        # last request put in it, so the pane can say "13k of 258k" and not only a percentage.
        data["context_window"] = window
        prompt = (data.get("input_tokens", 0) + data.get("cache_read_input_tokens", 0)
                  + data.get("cache_creation_input_tokens", 0))
        if prompt > 0:
            data["context_tokens"] = prompt
            data["context_pct"] = round(min(100.0, 100.0 * prompt / window), 1)
    return data


# Claude Code's `rate_limit_event`, as 2.1.278 writes it to a stream-json host (recorded live on
# 2026-09-20 from `claude -p … --output-format stream-json --verbose`; one per turn, after the
# model's `message_stop` and before the `result`):
#
#   {"type": "rate_limit_event",
#    "rate_limit_info": {"status": "allowed", "resetsAt": 1789926600,
#                        "rateLimitType": "five_hour", "overageStatus": "rejected",
#                        "overageDisabledReason": "org_level_disabled", "isUsingOverage": false,
#                        "unifiedWindows": {"five_hour": {"utilization": 0.05,
#                                                         "resetsAt": 1789926600},
#                                           "seven_day": {"utilization": 0.01,
#                                                         "resetsAt": 1790499600}}},
#    "uuid": "…", "session_id": "…"}
#
# `utilization` is a 0–1 fraction (0.05 is 5% used) and `resetsAt` is unix seconds. The top-level
# `rateLimitType` / `resetsAt` / `status` describe the window that currently *governs* — which
# one would reject next — and carry no figure of their own, so `unifiedWindows` is what the
# event is made of. `status` is allowed | allowed_warning | rejected.
_CLAUDE_WINDOWS = (("five_hour", "5h"), ("seven_day", "weekly"))


def _limits_event(message: dict) -> dict:
    """The `limits` event a `rate_limit_event` becomes, or {} when it names no window."""
    info = message.get("rate_limit_info")
    if not isinstance(info, dict):
        return {}
    unified = info.get("unifiedWindows")
    windows = []
    if isinstance(unified, dict):
        for wire, kind in _CLAUDE_WINDOWS:
            entry = unified.get(wire)
            if not isinstance(entry, dict):
                continue
            used = entry.get("utilization")
            if isinstance(used, bool) or not isinstance(used, (int, float)):
                continue
            resets = entry.get("resetsAt")
            windows.append({"kind": kind,
                            "used_percent": round(min(100.0, max(0.0, float(used) * 100.0)), 1),
                            "resets_at": int(resets)
                            if isinstance(resets, (int, float)) and not isinstance(resets, bool)
                            and resets > 0 else None})
    if not windows:
        return {}
    data = {"windows": windows}
    status = info.get("status")
    if isinstance(status, str) and status.strip():
        data["status"] = status.strip()
    return data


def _error_text(message: dict) -> str:
    errors = message.get("errors")
    if isinstance(errors, list) and errors:
        return "; ".join(str(e) for e in errors if e)
    for key in ("result", "error", "api_error_status"):
        value = message.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip()
    subtype = message.get("subtype")
    return f"Claude Code ended the turn: {subtype}" if subtype else ""
