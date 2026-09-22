# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tier A: Codex as a headless harness, over `codex app-server` (GT7X, protocol 29).

`codex app-server` speaks JSON-RPC 2.0 over newline-delimited JSON on stdin/stdout (the default
`--listen stdio://`). This module drives one such process for one pane and presents it as the
`guest_harness.Harness` contract, so the worker never learns a word of Codex's vocabulary.

The flow one turn takes, as recorded from codex-cli 0.155.1 (fixtures in
`tests/fixtures/guest_harness_codex/`):

    -> initialize {clientInfo:{name:"relay",version}}        <- {userAgent, codexHome, ...}
    -> initialized (notification)
    -> thread/start {cwd, approvalPolicy, sandbox}           <- {thread:{id,...}, model,
                     [config:{model_reasoning_effort}]}          reasoningEffort, ...}
    -> turn/start {threadId, input:[{type:"text",text}],     <- {turn:{id, status:"inProgress"}}
                   [model], [effort]}
       <- turn/started, item/started, item/agentMessage/delta, item/completed,
          thread/tokenUsage/updated, turn/completed {turn:{status:"completed"}}

The protocol is machine-readable: `codex app-server generate-json-schema --out <dir>` writes
`ClientRequest.json` (every request), `ServerRequest.json` (every request the server makes of us,
i.e. the approvals) and `ServerNotification.json` (every notification). Anything not named below
is ignored and logged at debug, so a newer codex cannot break a pane.

**The reasoning effort** (owner, 2026-09-19: "you should be able to pick the model and reasoning
effort for those"). Where the schema puts it, read off `generate-json-schema` for 0.155.1:

* `TurnStartParams.effort` — "Override the reasoning effort for this turn and subsequent turns",
  a `ReasoningEffort`, which is any non-empty string the *model* advertises. So `set_effort()`
  only has to remember the level and put it on the next `turn/start`.
* `ThreadStartParams` has **no** `effort` field, but it does have `config` (a free-form overrides
  map), and `model_reasoning_effort` there is the same key the TUI's `-c` takes. A thread started
  that way answers with `reasoningEffort: "<level>"`, so `start(effort=…)` uses it and the thread
  is on the right level before its first turn. `ThreadResumeParams` has `config` too.
* `ModelListResponse.data[]` is the catalogue `models()` reports: `id`, `displayName`,
  `defaultReasoningEffort` and `supportedReasoningEfforts: [{reasoningEffort, description}]`.

Nothing is refused here: an effort a model will not take is refused by codex, and its own words
are what the pane shows (a `HarnessError` from `turn/start`).

**Streaming tool output** (GT7X task t:a3). `item/commandExecution/outputDelta` is
`{threadId, turnId, itemId, delta}` with the delta as plain text, and each one becomes a
`tool_output` event under the item's id, so a five-minute build ticks in the pane instead of
sitting on a frozen call line. The delta is buffered as well, because that buffer is still the
fallback for an `item/completed` with no `aggregatedOutput`. `item/fileChange/outputDelta` has the
same shape and 0.155.1's schema marks it deprecated ("the server no longer emits this
notification"); it is handled the same way for the servers that still do.

**Usage limits** (the subscription's rolling windows, not the context). 0.155.1's schema has
`account/rateLimits/read` (params `{}`; response `{rateLimits: RateLimitSnapshot, …}`) and the
notification `account/rateLimits/updated {rateLimits: RateLimitSnapshot}`, described as a
"sparse rolling rate-limit update" that clients "merge into the most recent read". A
`RateLimitSnapshot` is `{primary?, secondary?: {usedPercent: int, windowDurationMins?: int,
resetsAt?: int}, planType?, rateLimitReachedType?, limitId?, …}`. Recorded live on 2026-09-20
against this codex, on a Pro plan:

    -> account/rateLimits/read {}
    <- {"ordinaryUsageAllowed": true, "rateLimits": {"limitId": "codex", "limitName": null,
        "normalModelSlug": null, "primary": {"usedPercent": 53, "windowDurationMins": 10080,
        "resetsAt": 1790065926}, "secondary": null, "credits": {...}, "individualLimit": null,
        "spendControlReached": false, "planType": "pro", "rateLimitReachedType": null},
        "rateLimitsByLimitId": {"codex": {...the same...}},
        "rateLimitResetCredits": {"availableCount": 0, "credits": []}, "accountId": "…",
        "rateLimitUpsell": null}

So *primary* is not always the five-hour window: here it is the 7-day one (10080 minutes) and
there is no secondary. `windowDurationMins` decides the kind (`window_kind_for_minutes`), and the
adapter reads once at `start()` and then merges every `account/rateLimits/updated`, emitting a
`limits` event on the next turn (or at once, inside one).

**Approval scopes.** `answer()`'s `decision["scope"]` reaches the words codex has and
allow/deny does not: `acceptForSession` (v2) / `approved_for_session` (v1) for `session`,
`cancel` (v2) / `abort` (v1) for a deny that also ends the turn. See `_answer_record`.
"""
from __future__ import annotations

import base64
import json
import logging
import os
import queue
import shutil
import subprocess
import threading
import time
from collections import deque

from .guest_harness import (Emit, HarnessError, HarnessSteerUncertain, HarnessEvent, HarnessNotAvailable, HarnessStart,
                            TurnResult, approval_scope, chunk_tool_output, map_tool_name,
                            validate_effort, validate_permissions,
                            window_kind_for_minutes)
from .presets import model_name

log = logging.getLogger(__name__)

CLIENT_NAME = "relay"
# Relay's own version (CMakeLists.txt `project(Relay VERSION ...)`); it reaches codex's user-agent,
# so `RELAY_VERSION` overrides it when the GUI knows better than this default.
CLIENT_VERSION = os.environ.get("RELAY_VERSION") or "0.1.0"

# What each of guest_harness.PERMISSIONS means to codex: (approvalPolicy, sandbox).
#   bypass  what `--dangerously-bypass-approvals-and-sandbox` means, and the owner's rule: the
#           guest moves around the file system like Relay's own agent and is never asked.
#   ask     codex asks, and every ask becomes an `approval` event answered through `answer()`.
#   deny    codex asks, and every ask is refused for the user (with a `notice` so the pane says so).
PERMISSION_MODES = {
    "bypass": ("never", "danger-full-access"),
    "ask": ("on-request", "workspace-write"),
    "deny": ("on-request", "read-only"),
}

# `codex login status` (codex-cli 0.155.1): one line on stdout and exit 0 when signed in —
# `Logged in using ChatGPT`, or `Logged in using an API key - …` — and `Not logged in` on stderr
# with exit 1 otherwise. A local read of `~/.codex/auth.json`; nothing is fetched.
LOGIN_STATUS_ARGS = ("login", "status")

# The config key `thread/start`'s free-form `config` map takes for the reasoning effort — the one
# `codex -c model_reasoning_effort="high"` sets on the TUI.
EFFORT_CONFIG_KEY = "model_reasoning_effort"

# What `models()` falls back to when the catalogue cannot be had: every level 0.155.1's models
# advertise between them. The per-model subsets come from `model/list` and differ (gpt-5.5 has no
# `ultra`), which is why this is a fallback and not a table.
EFFORTS = ("low", "medium", "high", "xhigh", "max", "ultra")

# `item/started` item types that are a tool call in Relay's sense. The `type` is what
# `guest_harness.map_tool_name("codex", ...)` keys on.
TOOL_ITEM_TYPES = ("commandExecution", "fileChange", "mcpToolCall", "dynamicToolCall",
                   "collabAgentToolCall", "webSearch", "imageView")

# Server -> client requests this adapter knows how to answer.
_APPROVAL_METHODS = {
    "item/commandExecution/requestApproval": "command",
    "execCommandApproval": "command",                     # v1 name, still sent by older servers
    "item/fileChange/requestApproval": "patch",
    "applyPatchApproval": "patch",                        # v1 name
    "item/permissions/requestApproval": "other",
    "mcpServer/elicitation/request": "other",
}
_QUESTION_METHODS = ("item/tool/requestUserInput",)

# Server -> client notifications this adapter turns into events. Every other notification is
# ignored and logged at debug, so a newer codex only ever means a quieter pane.
_NOTIFICATIONS = {
    "item/agentMessage/delta": "_on_item_agentMessage_delta",
    "item/reasoning/textDelta": "_on_item_reasoning_textDelta",
    "item/reasoning/summaryTextDelta": "_on_item_reasoning_summaryTextDelta",
    "item/commandExecution/outputDelta": "_on_item_commandExecution_outputDelta",
    "item/fileChange/outputDelta": "_on_item_fileChange_outputDelta",
    "item/fileChange/patchUpdated": "_on_item_fileChange_patchUpdated",
    "item/started": "_on_item_started",
    "item/completed": "_on_item_completed",
    "thread/tokenUsage/updated": "_on_thread_tokenUsage_updated",
    "account/rateLimits/updated": "_on_account_rateLimits_updated",
    "thread/compacted": "_on_thread_compacted",
    "warning": "_on_warning",
    "guardianWarning": "_on_guardianWarning",
    "configWarning": "_on_configWarning",
    "model/rerouted": "_on_model_rerouted",
    "error": "_on_error",
    "turn/completed": "_on_turn_completed",
}

_EOF = object()
_STDERR_KEEP = 25


class _TurnState:
    """One running turn: where its events go, and what its items have said so far."""

    def __init__(self, emit: Emit):
        self.emit = emit
        self.queue: "queue.Queue" = queue.Queue()
        self.turn_id: str | None = None
        self.items: dict[str, dict] = {}
        self.messages: list[dict] = []
        self.usage_baseline: dict = {}
        self.interrupt_requested = False
        self.interrupt_sent = False
        self.error: dict | None = None
        self.error_deadline = 0.0


class CodexHarness:
    """One `codex app-server` process for one pane. Matches `guest_harness.Harness`."""

    guest = "codex"

    @classmethod
    def for_probe(cls, **kwargs) -> "CodexHarness":
        """The harness the key test drives for its one turn. Codex has no "no tools" switch; the
        test starts it with `permissions="deny"` (read-only sandbox, every ask refused), which is
        as far as its own posture table goes."""
        return cls(**kwargs)

    def __init__(self, *, codex_path: str | None = None, spawn=None,
                 client_version: str | None = None, request_timeout: float = 120.0,
                 close_timeout: float = 2.0, error_grace: float = 5.0):
        self._codex_path = codex_path or os.environ.get("RELAY_CODEX_BIN") or "codex"
        self._spawn = spawn or _spawn_codex
        self._client_version = client_version or CLIENT_VERSION
        self._request_timeout = request_timeout
        self._close_timeout = close_timeout
        self._error_grace = error_grace

        self._lock = threading.RLock()
        self._proc = None
        self._reader: threading.Thread | None = None
        self._stderr_thread: threading.Thread | None = None
        self._stderr = deque(maxlen=_STDERR_KEEP)
        self._next_id = 0
        self._pending: dict[str, "_Pending"] = {}
        self._server_requests: dict[str, dict] = {}
        self._turn: _TurnState | None = None
        self._usage: dict = {}
        # The subscription's rolling windows as codex last reported them (a merged
        # RateLimitSnapshot: primary / secondary), and whether a turn has yet told the pane.
        self._limits: dict = {}
        self._limits_fresh = False
        self._session_id = ""
        self._model = ""
        self._effort = ""
        self._pending_model: str | None = None
        self._pending_effort: str | None = None
        self._announce_started = False
        self._permissions = "bypass"
        self._closed = False
        self._dead = False
        self._dead_reason = ""

    # ----- the contract ------------------------------------------------------------------------

    def start(self, *, cwd: str, model: str | None = None, resume: str | None = None,
              fork: bool = False, permissions: str = "bypass",
              effort: str | None = None, board_bridge: dict | None = None,
              instructions: str | None = None) -> HarnessStart:
        self._board_bridge = board_bridge
        self._instructions = instructions
        effort = validate_effort(effort)
        with self._lock:
            if self._proc is not None:
                raise HarnessError("this codex harness has already been started.")
            self._permissions = validate_permissions(permissions)
            self._effort = effort or ""
        exe = shutil.which(self._codex_path)
        if not exe:
            raise HarnessNotAvailable(
                f"Codex is not installed here: {self._codex_path!r} is not on PATH. "
                "Install it with `npm i -g @openai/codex` and sign in with `codex login`.")
        try:
            argv = [exe, "app-server"]
            if board_bridge:
                argv += ["-c", "features.multi_agent=false", "-c", "features.multi_agent_v2=false"]
                for key, value in board_bridge.items():
                    argv += ["-c", "mcp_servers.relay_board." + key + "=" + json.dumps(value)]
            proc = self._spawn(argv, cwd)
        except OSError as exc:
            raise HarnessNotAvailable(f"Codex could not be started: {exc}") from exc
        with self._lock:
            self._proc = proc
        self._reader = threading.Thread(target=self._read_stdout, name="codex-harness-stdout",
                                        daemon=True)
        self._reader.start()
        if getattr(proc, "stderr", None) is not None:
            self._stderr_thread = threading.Thread(target=self._read_stderr,
                                                   name="codex-harness-stderr", daemon=True)
            self._stderr_thread.start()

        try:
            self._request("initialize", {"clientInfo": {"name": CLIENT_NAME,
                                                        "version": self._client_version}})
            self._notify("initialized", {})
            result = self._start_thread(cwd=cwd, model=model, resume=resume, fork=fork,
                                        effort=effort)
        except HarnessNotAvailable:
            self.close()
            raise
        except HarnessError as exc:
            self.close()
            raise HarnessNotAvailable(f"Codex's app-server did not come up: {exc}") from exc

        thread = result.get("thread") or {}
        self._session_id = str(thread.get("id") or thread.get("sessionId") or "")
        self._model = str(result.get("model") or thread.get("model") or model or "")
        # The level the thread actually came up on, which is codex's answer and not our request.
        self._effort = str(result.get("reasoningEffort") or thread.get("reasoningEffort")
                           or effort or "")
        if not self._session_id:
            self.close()
            raise HarnessNotAvailable("Codex's app-server started no thread (no id came back).")
        self._announce_started = True
        self._read_limits()
        return HarnessStart(session_id=self._session_id, model=self._model)

    def _read_limits(self) -> None:
        """Ask for the subscription's rolling windows once, without waiting: the answer lands on
        the reader thread (`_dispatch`) and is emitted as `limits` at the next turn. A server
        without the method (or an account without limits) answers with an error, which is
        ignored: the figures are a courtesy, never a condition of starting."""
        try:
            self._request_async("account/rateLimits/read", {},
                                on_result=lambda result: self._merge_limits(
                                    (result or {}).get("rateLimits")))
        except HarnessError as exc:
            log.debug("codex harness: could not ask for rate limits: %s", exc)

    def _merge_limits(self, snapshot) -> None:
        """Fold a RateLimitSnapshot into the held one. The schema calls an update *sparse*
        ("merge available values… does not clear a previously observed value"), so a window that
        is absent or null leaves what was known; a window that is present replaces it."""
        if not isinstance(snapshot, dict):
            return
        with self._lock:
            for key in ("primary", "secondary"):
                window = snapshot.get(key)
                if isinstance(window, dict) and not isinstance(window.get("usedPercent"), bool) \
                        and isinstance(window.get("usedPercent"), (int, float)):
                    self._limits[key] = dict(window)
            for key in ("planType", "rateLimitReachedType", "limitId"):
                if key in snapshot and snapshot[key] is not None:
                    self._limits[key] = snapshot[key]
            self._limits_fresh = bool(self._limits.get("primary") or self._limits.get("secondary"))

    def _limits_event(self) -> dict:
        """The held snapshot as a `limits` event, or {} when no window is known."""
        with self._lock:
            held = dict(self._limits)
        windows = []
        for position, key in enumerate(("primary", "secondary")):
            window = held.get(key)
            if not isinstance(window, dict):
                continue
            used = window.get("usedPercent")
            if isinstance(used, bool) or not isinstance(used, (int, float)):
                continue
            resets = window.get("resetsAt")
            windows.append({"kind": window_kind_for_minutes(window.get("windowDurationMins"),
                                                            position),
                            "used_percent": round(min(100.0, max(0.0, float(used))), 1),
                            "resets_at": int(resets)
                            if isinstance(resets, (int, float)) and not isinstance(resets, bool)
                            and resets > 0 else None})
        if not windows:
            return {}
        data = {"windows": windows}
        if held.get("rateLimitReachedType"):
            data["status"] = "rejected"
        return data

    def _emit_limits_if_fresh(self, turn: _TurnState) -> None:
        with self._lock:
            fresh, self._limits_fresh = self._limits_fresh, False
        if fresh:
            data = self._limits_event()
            if data:
                self._emit(turn, "limits", data)

    def send(self, prompt: str, *, attachments: list[dict] | None = None, emit: Emit,
             cancel: threading.Event) -> TurnResult:
        self._require_live()
        turn = _TurnState(emit)
        with self._lock:
            if self._turn is not None:
                raise HarnessError("a codex turn is already running in this pane.")
            self._turn = turn
            turn.usage_baseline = dict(self._usage.get("total") or {})
        try:
            if self._announce_started:
                self._announce_started = False
                self._emit(turn, "started", {"session_id": self._session_id, "model": self._model})
            self._emit_limits_if_fresh(turn)
            params = {"threadId": self._session_id,
                      "input": self._build_input(prompt, attachments)}
            with self._lock:
                if self._pending_model:
                    params["model"] = self._pending_model
                    self._pending_model = None
                if self._pending_effort:
                    # "Override the reasoning effort for this turn and subsequent turns."
                    params["effort"] = self._pending_effort
                    self._pending_effort = None
            response = self._request("turn/start", params)
            turn.turn_id = str(((response.get("turn") or {}).get("id")) or "")
            if turn.interrupt_requested or cancel.is_set():
                self._write_interrupt(turn)
            return self._run_turn(turn, cancel)
        finally:
            with self._lock:
                self._turn = None

    def steer(self, prompt: str, *, accepted) -> None:
        """Native active-turn input; the RPC reply, not the write, acknowledges delivery."""
        with self._lock:
            turn = self._turn
        if turn is None or not turn.turn_id or turn.interrupt_requested:
            raise HarnessError("No active Codex turn to steer")
        self._request("turn/steer", {"threadId": self._session_id,
                                     "expectedTurnId": turn.turn_id,
                                     "input": self._build_input(prompt, None)}, steering=True)
        accepted()

    def interrupt(self) -> None:
        with self._lock:
            turn = self._turn
        if turn is None:
            return
        turn.interrupt_requested = True
        self._write_interrupt(turn)

    def set_model(self, model: str) -> str:
        name = (model or "").strip()
        if not name:
            raise HarnessError("a model name is required.")
        resolved = self._resolve_model(name)
        with self._lock:
            self._pending_model = resolved
            self._model = resolved
            self._announce_started = True
        return resolved

    def set_effort(self, effort: str) -> str:
        """Remember the level; the next `turn/start` carries it as `effort`.

        Nothing is checked against the model here: which levels a model has is what `model/list`
        says and codex is the one that enforces it, so a level it will not take comes back as the
        `turn/start` error, in codex's own words (29.1's `error` → the turn's error).
        """
        level = validate_effort(effort)
        if level is None:
            raise HarnessError("a reasoning effort is needed.")
        with self._lock:
            self._pending_effort = level
            self._effort = level
        return level

    def models(self) -> list[dict]:
        """`model/list` as the contract's rows (29.3): id = the slug `turn/start` takes, label =
        the display name, efforts and the default from the model's own fields.

        Best effort: an empty list when the process is not up or the server will not answer, and
        the pane then simply offers no menu.
        """
        if self._proc is None or self._closed or self._dead:
            return []
        try:
            listing = self._request("model/list", {}, timeout=min(self._request_timeout, 20.0))
        except HarnessError as exc:
            log.debug("codex harness: model/list failed (%s); no models to offer.", exc)
            return []
        return catalog_rows(listing.get("data") or [], current=self._model)

    @property
    def effort(self) -> str:
        """The level the thread is on, as codex reported it (empty when it never said)."""
        with self._lock:
            return self._effort

    def compact(self) -> None:
        if self._closed or self._dead or not self._session_id:
            return
        try:
            self._request_async("thread/compact/start", {"threadId": self._session_id})
        except HarnessError as exc:                                       # pragma: no cover
            log.debug("codex harness: compact could not be asked for: %s", exc)

    def answer(self, request_id: str, decision: dict) -> None:
        key = str(request_id)
        with self._lock:
            record = self._server_requests.pop(key, None)
        if record is None:
            raise HarnessError(f"codex has no question {request_id!r} waiting for an answer.")
        self._answer_record(record, decision or {})

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._closed = True
            proc = self._proc
        if proc is None:
            return
        try:
            if getattr(proc, "stdin", None) is not None:
                proc.stdin.close()
        except Exception:                                                 # pragma: no cover
            pass
        try:
            proc.terminate()
        except Exception:                                                 # pragma: no cover
            pass
        try:
            proc.wait(self._close_timeout)
        except Exception:
            try:
                proc.kill()
            except Exception:                                             # pragma: no cover
                pass
        # The readers end on the pipes' EOF; only then may the streams be closed, or a descriptor
        # is reused under a thread still reading it. A reader still alive keeps its pipe.
        readers = [t for t in (self._reader, self._stderr_thread) if t is not None]
        for reader in readers:
            if reader is not threading.current_thread():
                reader.join(self._close_timeout)
        if not any(r.is_alive() for r in readers if r is not threading.current_thread()):
            for stream in (getattr(proc, "stdout", None), getattr(proc, "stderr", None)):
                if stream is not None and not getattr(stream, "closed", True):
                    try:
                        stream.close()
                    except Exception:                                     # pragma: no cover
                        pass
        self._fail_pending(HarnessError("the codex harness was closed."))

    @property
    def session_id(self) -> str:
        return self._session_id

    @property
    def model(self) -> str:
        return self._model

    # ----- starting the thread ------------------------------------------------------------------

    def _start_thread(self, *, cwd: str, model: str | None, resume: str | None,
                      fork: bool, effort: str | None = None) -> dict:
        policy, sandbox = PERMISSION_MODES[self._permissions]
        params: dict = {"cwd": cwd, "approvalPolicy": policy, "sandbox": sandbox}
        if getattr(self, "_instructions", None):
            # Supported by start, resume and fork. Keep Codex's base instructions.
            params["developerInstructions"] = self._instructions
        if model:
            params["model"] = model
        if effort:
            # `thread/start` has no `effort` field; `config` is the overrides map, and this is the
            # key the TUI's `-c` sets. The response then reports `reasoningEffort: "<level>"`.
            params["config"] = {EFFORT_CONFIG_KEY: effort}
        if getattr(self, "_board_bridge", None):
            overrides = params.setdefault("config", {})
            overrides.update({"features.multi_agent": False, "features.multi_agent_v2": False})
            for key, value in self._board_bridge.items():
                overrides["mcp_servers.relay_board." + key] = value
        if resume:
            params["threadId"] = resume
            method = "thread/fork" if fork else "thread/resume"
        else:
            method = "thread/start"
            if fork:
                log.debug("codex harness: fork asked for with nothing to fork from;"
                          " starting fresh.")
        return self._request(method, params)

    def _resolve_model(self, name: str) -> str:
        """Ask `model/list` what codex calls this model. Best effort: the name passes through
        when the list cannot be had, because `turn/start` validates it anyway."""
        if self._closed or self._dead:
            return name
        try:
            listing = self._request("model/list", {}, timeout=min(self._request_timeout, 20.0))
        except HarnessError as exc:
            log.debug("codex harness: model/list failed (%s); using %r as given.", exc, name)
            return name
        wanted = name.lower()
        for entry in listing.get("data") or []:
            for field in ("id", "model", "displayName"):
                value = entry.get(field)
                if isinstance(value, str) and value.lower() == wanted:
                    return str(entry.get("model") or entry.get("id") or name)
        return name

    def _build_input(self, prompt: str, attachments: list[dict] | None) -> list[dict]:
        items: list[dict] = [{"type": "text", "text": prompt or ""}]
        for attachment in attachments or []:
            if not isinstance(attachment, dict):
                continue
            kind = attachment.get("kind") or attachment.get("type") or ""
            if kind != "image":
                log.debug("codex harness: attachment of kind %r is not sent.", kind)
                continue
            path = attachment.get("path")
            data = attachment.get("data")
            if data:
                if isinstance(data, (bytes, bytearray)):
                    data = base64.b64encode(bytes(data)).decode("ascii")
                media = attachment.get("media_type") or "image/png"
                items.append({"type": "image", "url": f"data:{media};base64,{data}"})
            elif path:
                items.append({"type": "localImage", "path": str(path)})
        return items

    # ----- the turn ------------------------------------------------------------------------------

    def _run_turn(self, turn: _TurnState, cancel: threading.Event) -> TurnResult:
        while True:
            if cancel.is_set() and not turn.interrupt_sent:
                turn.interrupt_requested = True
                self._write_interrupt(turn)
            try:
                message = turn.queue.get(timeout=0.1)
            except queue.Empty:
                if self._dead:
                    raise self._dead_error()
                if turn.error and time.monotonic() > turn.error_deadline:
                    raise self._turn_error(turn)
                continue
            if message is _EOF:
                raise self._dead_error()
            result = self._handle(turn, message)
            if result is not None:
                return result

    def _handle(self, turn: _TurnState, message: dict) -> TurnResult | None:
        method = message.get("method") or ""
        params = message.get("params") or {}
        if method == "__server_request__":
            self._emit_server_request(turn, message["record"])
            return None
        name = _NOTIFICATIONS.get(method)
        if name is None:
            log.debug("codex harness: notification %r ignored.", method)
            return None
        return getattr(self, name)(turn, params)

    # -- notifications (one method per wire name; anything else falls through to debug) ----------

    def _on_item_agentMessage_delta(self, turn, params):
        text = params.get("delta")
        if text:
            self._emit(turn, "delta", {"text": text})

    def _on_item_reasoning_textDelta(self, turn, params):
        text = params.get("delta")
        if text:
            self._emit(turn, "thinking", {"text": text})

    def _on_item_reasoning_summaryTextDelta(self, turn, params):
        self._on_item_reasoning_textDelta(turn, params)

    def _on_item_commandExecution_outputDelta(self, turn, params):
        """What the command has printed so far, as it prints it.

        `CommandExecutionOutputDeltaNotification` is `{threadId, turnId, itemId, delta}` and the
        delta is plain text, not base64 (`command/exec`'s own notification is the base64 one, and
        is a different channel this adapter does not use). It is still buffered as well, because
        `item/completed` only carries `aggregatedOutput` when codex kept it, and the buffer is
        what fills a `tool_result` when it does not.
        """
        item_id = str(params.get("itemId") or "")
        delta = str(params.get("delta") or "")
        if not delta:
            return
        state = turn.items.setdefault(item_id, {})
        state.setdefault("output", []).append(delta)
        for chunk in chunk_tool_output(delta):
            self._emit(turn, "tool_output", {"call_id": item_id, "text": chunk})

    def _on_item_fileChange_outputDelta(self, turn, params):
        # `FileChangeOutputDeltaNotification` has the same shape and codex 0.155.1's schema says
        # the server no longer emits it ("Deprecated legacy notification for `apply_patch` textual
        # output"). Kept, and streamed the same way, for the older servers that still send it.
        self._on_item_commandExecution_outputDelta(turn, params)

    def _on_item_fileChange_patchUpdated(self, turn, params):
        state = turn.items.setdefault(str(params.get("itemId") or ""), {})
        state["changes"] = params.get("changes") or []

    def _on_item_started(self, turn, params):
        item = params.get("item") or {}
        kind = str(item.get("type") or "")
        item_id = str(item.get("id") or "")
        if kind == "agentMessage":
            turn.messages.append({"id": item_id, "phase": item.get("phase"), "text": ""})
            return None
        if kind not in TOOL_ITEM_TYPES:
            return None
        state = turn.items.setdefault(item_id, {})
        state["type"] = kind
        state["started_ms"] = params.get("startedAtMs")
        if item.get("changes"):
            state["changes"] = item["changes"]
        self._emit_tool_started(turn, item_id, kind, item)
        return None

    def _on_item_completed(self, turn, params):
        item = params.get("item") or {}
        kind = str(item.get("type") or "")
        item_id = str(item.get("id") or "")
        if kind == "agentMessage":
            for entry in turn.messages:
                if entry["id"] == item_id:
                    entry["text"] = item.get("text") or ""
                    entry["phase"] = item.get("phase", entry.get("phase"))
                    break
            else:
                turn.messages.append({"id": item_id, "phase": item.get("phase"),
                                      "text": item.get("text") or ""})
            return None
        if kind == "contextCompaction":
            self._emit(turn, "notice", {"text": "Codex compacted its context."})
            return None
        if kind not in TOOL_ITEM_TYPES:
            return None
        state = turn.items.setdefault(item_id, {})
        if "type" not in state:                       # a completion we never saw start
            state["type"] = kind
            self._emit_tool_started(turn, item_id, kind, item)
        data = _tool_result(kind, item, state)
        data["call_id"] = item_id
        data["tool"] = map_tool_name("codex", kind)
        ms = item.get("durationMs")
        if ms is None and state.get("started_ms") and params.get("completedAtMs"):
            ms = int(params["completedAtMs"]) - int(state["started_ms"])
        if ms is not None:
            data["ms"] = int(ms)
        self._emit(turn, "tool_result", data)
        return None

    def _on_thread_tokenUsage_updated(self, turn, params):
        return None                        # the state is kept on the reader thread; nothing to emit

    def _on_account_rateLimits_updated(self, turn, params):
        # Merged on the reader thread already (`_dispatch`); here the turn is running, so say so.
        # Two updates that both landed before this ran are one event: the merged, newest state.
        self._emit_limits_if_fresh(turn)
        return None

    def _on_thread_compacted(self, turn, params):
        self._emit(turn, "notice", {"text": "Codex compacted its context."})

    def _on_warning(self, turn, params):
        text = params.get("summary") or params.get("message")
        if text:
            self._emit(turn, "notice", {"text": str(text)})

    _on_guardianWarning = _on_warning
    _on_configWarning = _on_warning

    def _on_model_rerouted(self, turn, params):
        target = params.get("to") or params.get("model")
        if target:
            self._emit(turn, "notice", {"text": f"Codex rerouted this turn to {target}."})

    def _on_error(self, turn, params):
        error = params.get("error") or {}
        text = str(error.get("message") or "Codex reported an error.")
        details = error.get("additionalDetails")
        if details:
            text = f"{text}\n{details}"
        if params.get("willRetry"):
            self._emit(turn, "notice", {"text": f"{text} (codex will retry)"})
            return None
        code = (error.get("codexErrorInfo") or {}).get("type") if isinstance(
            error.get("codexErrorInfo"), dict) else None
        data = {"text": text}
        if code:
            data["code"] = str(code)
        self._emit(turn, "error", data)
        turn.error = data
        turn.error_deadline = time.monotonic() + self._error_grace
        return None

    def _on_turn_completed(self, turn, params):
        payload = params.get("turn") or {}
        if turn.turn_id and payload.get("id") and str(payload["id"]) != turn.turn_id:
            log.debug("codex harness: turn/completed for another turn %r ignored.", payload["id"])
            return None
        status = str(payload.get("status") or "completed")
        usage = self._usage_for(turn)
        if usage:
            self._emit(turn, "usage", dict(usage))
        if status == "failed":
            error = payload.get("error") or {}
            if not turn.error:
                text = str(error.get("message") or "Codex's turn failed.")
                turn.error = {"text": text}
                self._emit(turn, "error", dict(turn.error))
            raise self._turn_error(turn)
        text = _final_text(turn, payload)
        stop = "interrupted" if status == "interrupted" or turn.interrupt_requested else "end"
        return TurnResult(text=text, stop_reason=stop, usage=usage)

    # ----- tool events ---------------------------------------------------------------------------

    def _emit_tool_started(self, turn: _TurnState, item_id: str, kind: str, item: dict) -> None:
        payload, label = _tool_start(kind, item)
        payload["_guest_tool"] = kind
        data = {"call_id": item_id, "tool": map_tool_name("codex", kind), "input": payload}
        if label:
            data["label"] = label
        self._emit(turn, "tool_started", data)

    def _usage_for(self, turn: _TurnState) -> dict:
        with self._lock:
            state = dict(self._usage)
        total = state.get("total") or {}
        if not total:
            return {}
        base = turn.usage_baseline or {}
        data = {
            "input_tokens": max(int(total.get("inputTokens") or 0)
                                - int(base.get("inputTokens") or 0), 0),
            "output_tokens": max(int(total.get("outputTokens") or 0)
                                 - int(base.get("outputTokens") or 0), 0),
        }
        # What the prefix cache saved this turn, the same way as the two above: `tokenUsage.total`
        # is cumulative over the thread, so the turn's share is the rise since its baseline. Its
        # `cachedInputTokens` is part of `inputTokens`, as OpenAI counts them (#GMCF decision 5).
        for source, target in (("cachedInputTokens", "cached_input_tokens"),
                               ("cacheWriteInputTokens", "cache_write_input_tokens")):
            if source in total:
                data[target] = max(int(total.get(source) or 0) - int(base.get(source) or 0), 0)
        if self._model:
            data["model"] = self._model
        window = state.get("modelContextWindow")
        last = (state.get("last") or {}).get("totalTokens")
        # The guest's own context, as codex measures it: `last` is the tokens the most recent
        # request carried and `modelContextWindow` the model's window, so the pane can say
        # "13k of 258k" and not only a percentage (GT7X task t:a3).
        if window:
            data["context_window"] = int(window)
        if isinstance(last, (int, float)) and last >= 0:
            data["context_tokens"] = int(last)
        if window and isinstance(last, (int, float)) and last >= 0:
            data["context_pct"] = round(100.0 * int(last) / int(window), 1)
        return data

    # ----- approvals and questions ----------------------------------------------------------------

    def _emit_server_request(self, turn: _TurnState, record: dict) -> None:
        if record["kind"] == "question":
            self._emit(turn, "question", {"id": record["id"], "questions": record["questions"]})
        elif record["kind"] == "notice":
            self._emit(turn, "notice", {"text": record["text"]})
        else:
            self._emit(turn, "approval", {"id": record["id"], "kind": record["kind"],
                                          "detail": record["detail"]})

    def _answer_record(self, record: dict, decision: dict) -> None:
        """Send one approval answer back, in the words the method that asked expects.

        `decision["scope"]` (guest_harness.APPROVAL_SCOPES) is what codex has richer words for
        than allow/deny, read off `generate-json-schema` for 0.155.1:

        * `once` — `accept` / `decline`, what this adapter always sent.
        * `session` — `acceptForSession`: "future prompts in the same session-scoped approval
          cache should run without prompting". On `item/permissions/requestApproval` the same
          idea is the response's own `scope: "session"` in place of the default `"turn"`.
        * `stop` (deny only) — `cancel`: "the turn will also be immediately interrupted". The two
          methods that have no such word (a permissions request, an MCP elicitation's `cancel`
          only closes the dialog) get the refusal they would have got plus an `interrupt()`, so
          "refuse and stop" means the same thing whatever asked.
        """
        method = record["method"]
        rid = record["raw_id"]
        if method in _QUESTION_METHODS:
            answers = decision.get("answers") or []
            payload: dict = {}
            for qid, given in zip(record.get("question_ids") or [], answers):
                payload[qid] = {"answers": [str(x) for x in (given or [])]}
            self._respond(rid, {"answers": payload})
            return
        allow = str(decision.get("behavior") or "").lower() == "allow"
        scope = approval_scope(decision)
        stop = not allow and scope == "stop"
        ours = False                      # does *this* adapter have to end the turn, or codex?
        if method == "mcpServer/elicitation/request":
            # `McpServerElicitationAction`: accept | decline | cancel. `cancel` is "the user
            # dismissed it", not "end the turn", so a stop is a cancel *and* an interrupt.
            self._respond(rid, {"action": "accept" if allow else "cancel" if stop else "decline"})
            ours = stop
        elif method == "item/permissions/requestApproval":
            if allow:
                # `PermissionGrantScope`: turn | session. The default is `turn`.
                self._respond(rid, {"permissions": record.get("permissions") or {},
                                    "scope": "session" if scope == "session" else "turn"})
            else:
                self._respond_error(rid, -32000,
                                    decision.get("message") or "the user refused the request.")
                ours = stop               # a refusal here has no "and stop" of its own
        elif method in ("execCommandApproval", "applyPatchApproval"):
            # The v1 `ReviewDecision` spelling, for the older servers that still ask this way:
            # `abort` is "the agent should not do anything until the user's next command".
            self._respond(rid, {"decision": "approved_for_session" if allow and scope == "session"
                                else "approved" if allow else "abort" if stop else "denied"})
        else:
            self._respond(rid, {"decision": "acceptForSession" if allow and scope == "session"
                                else "accept" if allow else "cancel" if stop else "decline"})
        if ours:
            self.interrupt()

    def _on_server_request(self, message: dict) -> None:
        method = str(message.get("method") or "")
        params = message.get("params") or {}
        raw_id = message.get("id")
        record = {"method": method, "raw_id": raw_id, "id": str(raw_id)}

        if method in _QUESTION_METHODS:
            questions = []
            ids = []
            for question in params.get("questions") or []:
                ids.append(str(question.get("id") or ""))
                one = {"header": question.get("header") or "",
                       "question": question.get("question") or ""}
                options = [o.get("label") or o.get("value") or ""
                           for o in (question.get("options") or [])]
                if options:
                    one["options"] = options
                questions.append(one)
            record.update(kind="question", questions=questions, question_ids=ids)
        elif method in _APPROVAL_METHODS:
            # The item the approval is about, when the adapter is already following one: a v2
            # file-change approval names only its `itemId`, so the paths come from what
            # `item/started` and `patchUpdated` recorded for the diff.
            with self._lock:
                turn = self._turn
            item = (turn.items.get(str(params.get("itemId") or "")) or {}) if turn is not None else {}
            record.update(kind=_APPROVAL_METHODS[method],
                          detail=_approval_detail(method, params, item))
            if method == "item/permissions/requestApproval":
                record["permissions"] = params.get("permissions") or {}
        else:
            log.debug("codex harness: server request %r refused (not known here).", method)
            self._respond_error(raw_id, -32601, f"relay does not implement {method}")
            return

        with self._lock:
            self._server_requests[record["id"]] = record
            turn = self._turn
            posture = self._permissions

        auto = None
        if posture == "deny":
            auto = {"behavior": "deny", "message": "this pane refuses approvals."}
        elif posture == "bypass" and record["kind"] in ("command", "patch"):
            auto = {"behavior": "allow"}
        elif turn is None:
            auto = {"behavior": "deny", "message": "no turn is running in this pane."}

        if auto is not None:
            with self._lock:
                self._server_requests.pop(record["id"], None)
            self._answer_record(record, auto)
            note = ("Codex asked to " + (record.get("detail") or method) + " — "
                    + ("allowed" if auto["behavior"] == "allow" else "refused")
                    + " by this pane's permissions.")
            if turn is not None:
                turn.queue.put({"method": "__server_request__",
                                "record": {"kind": "notice", "text": note}})
            else:
                log.debug("codex harness: %s", note)
            return
        turn.queue.put({"method": "__server_request__", "record": record})

    # ----- the wire -------------------------------------------------------------------------------

    def _read_stdout(self) -> None:
        stdout = self._proc.stdout
        while True:
            try:
                raw = stdout.readline()
            except Exception as exc:                                       # pragma: no cover
                log.debug("codex harness: stdout read failed: %s", exc)
                raw = ""
            if raw == "" or raw == b"":
                break
            if isinstance(raw, bytes):
                raw = raw.decode("utf-8", "replace")
            line = raw.strip()
            if not line:
                continue
            try:
                message = json.loads(line)
            except ValueError:
                log.debug("codex harness: non-JSON line on stdout skipped: %.120s", line)
                continue
            if not isinstance(message, dict):
                log.debug("codex harness: JSON that is not an object skipped: %.120s", line)
                continue
            try:
                self._dispatch(message)
            except Exception:                                              # pragma: no cover
                log.exception("codex harness: a message could not be handled.")
        self._on_eof()

    def _dispatch(self, message: dict) -> None:
        if "method" in message and "id" in message:
            self._on_server_request(message)
            return
        if "method" in message:
            method = message.get("method")
            if method == "thread/tokenUsage/updated":
                usage = (message.get("params") or {}).get("tokenUsage") or {}
                with self._lock:
                    self._usage = dict(usage)
            elif method == "account/rateLimits/updated":
                # Kept whether or not a turn runs: outside one, the next `send()` reports it.
                self._merge_limits((message.get("params") or {}).get("rateLimits"))
            with self._lock:
                turn = self._turn
            if turn is None:
                log.debug("codex harness: notification %r outside a turn ignored.", method)
                return
            turn.queue.put(message)
            return
        key = str(message.get("id"))
        with self._lock:
            pending = self._pending.pop(key, None)
        if pending is None:
            log.debug("codex harness: response to unknown request %r ignored.", key)
            return
        if "error" in message and message["error"] is not None:
            error = message["error"] or {}
            pending.fail(HarnessError(str(error.get("message") or "codex refused the request.")))
        else:
            pending.done(message.get("result") if isinstance(message.get("result"), dict) else {})
            if pending.on_result is not None:
                try:
                    pending.on_result(pending.result)
                except Exception:                                          # pragma: no cover
                    log.debug("codex harness: a response callback failed.", exc_info=True)

    def _read_stderr(self) -> None:
        stream = self._proc.stderr
        while True:
            try:
                raw = stream.readline()
            except Exception:                                              # pragma: no cover
                break
            if raw == "" or raw == b"":
                break
            if isinstance(raw, bytes):
                raw = raw.decode("utf-8", "replace")
            self._stderr.append(raw.rstrip("\n"))

    def _on_eof(self) -> None:
        with self._lock:
            if self._dead:
                return
            self._dead = True
            self._dead_reason = "\n".join(list(self._stderr)[-8:])
            turn = self._turn
        error = self._dead_error()
        self._fail_pending(error)
        if turn is not None:
            turn.queue.put(_EOF)

    def _dead_error(self) -> HarnessError:
        if self._closed:
            return HarnessError("the codex harness was closed.")
        text = "Codex's app-server stopped."
        if self._dead_reason:
            text = f"{text}\n{self._dead_reason}"
        return HarnessError(text)

    def _turn_error(self, turn: _TurnState) -> HarnessError:
        return HarnessError((turn.error or {}).get("text") or "Codex's turn failed.")

    def _fail_pending(self, error: HarnessError) -> None:
        with self._lock:
            pending = list(self._pending.values())
            self._pending.clear()
        for one in pending:
            one.fail(error)

    def _write(self, message: dict) -> None:
        with self._lock:
            proc = self._proc
            closed = self._closed
        if proc is None:
            raise HarnessError("the codex harness has not been started.")
        if closed or self._dead:
            raise self._dead_error()
        line = json.dumps(message) + "\n"
        try:
            proc.stdin.write(line)
            proc.stdin.flush()
        except Exception as exc:
            raise HarnessError(f"Codex's app-server stopped taking input: {exc}") from exc

    def _notify(self, method: str, params: dict) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def _respond(self, raw_id, result: dict) -> None:
        self._write({"jsonrpc": "2.0", "id": raw_id, "result": result})

    def _respond_error(self, raw_id, code: int, message: str) -> None:
        try:
            self._write({"jsonrpc": "2.0", "id": raw_id,
                         "error": {"code": code, "message": message}})
        except HarnessError as exc:                                        # pragma: no cover
            log.debug("codex harness: a refusal could not be sent: %s", exc)

    def _request_async(self, method: str, params: dict, on_result=None) -> None:
        """Send and do not wait. `on_result`, when given, runs on the reader thread with the
        result dict when the answer arrives; an error answer is dropped either way."""
        pending, message = self._prepare(method, params)
        pending.ignore = True
        pending.on_result = on_result
        self._write(message)

    def _request(self, method: str, params: dict, timeout: float | None = None, *, steering=False) -> dict:
        pending, message = self._prepare(method, params)
        try:
            self._write(message)
        except HarnessError as exc:
            with self._lock:
                self._pending.pop(pending.key, None)
            if steering:
                raise HarnessSteerUncertain("Codex steering write failed; delivery is uncertain") from exc
            raise
        if not pending.event.wait(timeout if timeout is not None else self._request_timeout):
            with self._lock:
                self._pending.pop(pending.key, None)
            error = HarnessSteerUncertain if steering else HarnessError
            raise error(f"Codex did not answer {method} in time.")
        if pending.error is not None:
            raise pending.error
        return pending.result or {}

    def _prepare(self, method: str, params: dict):
        with self._lock:
            self._next_id += 1
            key = str(self._next_id)
            pending = _Pending(key)
            self._pending[key] = pending
        return pending, {"jsonrpc": "2.0", "id": int(key), "method": method, "params": params}

    def _write_interrupt(self, turn: _TurnState) -> None:
        if turn.interrupt_sent or not turn.turn_id:
            return
        turn.interrupt_sent = True
        try:
            self._request_async("turn/interrupt", {"threadId": self._session_id,
                                                   "turnId": turn.turn_id})
        except HarnessError as exc:
            log.debug("codex harness: interrupt could not be sent: %s", exc)

    def _require_live(self) -> None:
        if self._proc is None:
            raise HarnessError("the codex harness has not been started.")
        if self._closed or self._dead:
            raise self._dead_error()

    def _emit(self, turn: _TurnState, kind: str, data: dict) -> None:
        try:
            turn.emit(HarnessEvent(kind, data))
        except HarnessSteerUncertain:
            raise
        except Exception:                                                  # pragma: no cover
            log.warning("codex harness: a %s event could not be delivered.", kind, exc_info=True)


class _Pending:
    __slots__ = ("key", "event", "result", "error", "ignore", "on_result")

    def __init__(self, key: str):
        self.key = key
        self.event = threading.Event()
        self.result: dict | None = None
        self.error: HarnessError | None = None
        self.ignore = False
        self.on_result = None

    def done(self, result: dict) -> None:
        self.result = result
        self.event.set()

    def fail(self, error: HarnessError) -> None:
        self.error = error
        self.event.set()


# ----- pure helpers ------------------------------------------------------------------------------


def catalog_rows(entries, current: str = "") -> list[dict]:
    """`model/list`'s `data` (or `codex debug models`' `models`) as the contract's model rows.

    Both catalogues carry the same facts under different spellings — the app-server answers
    camelCase (`displayName`, `supportedReasoningEfforts[].reasoningEffort`,
    `defaultReasoningEffort`) and `codex debug models` snake_case (`slug`, `display_name`,
    `supported_reasoning_levels[].effort`, `default_reasoning_level`) — so one reader serves the
    adapter and the worker's background scan (`guest_harness_provider`).

    `name` is `presets.model_name` of the id, not codex's `displayName`: the owner's picker says
    `gpt-5.6-sol`, not "GPT-5.6-Sol" and not "Codex" (card #MDL1), and that is also what folds the
    row into the one the OpenAI API and OpenRouter serve. `label` is the same string.
    """
    rows: list[dict] = []
    for entry in entries or []:
        if not isinstance(entry, dict):
            continue
        if entry.get("hidden") is True or entry.get("visibility") == "hide":
            continue
        model_id = str(entry.get("id") or entry.get("slug") or entry.get("model") or "").strip()
        if not model_id:
            continue
        efforts = []
        for level in (entry.get("supportedReasoningEfforts")
                      or entry.get("supported_reasoning_levels") or []):
            name = level.get("reasoningEffort") or level.get("effort") if isinstance(level, dict) \
                else level
            try:
                name = validate_effort(name)
            except ValueError:
                name = None
            if name and name not in efforts:
                efforts.append(name)
        try:
            default = validate_effort(entry.get("defaultReasoningEffort")
                                      or entry.get("default_reasoning_level"))
        except ValueError:
            default = None
        name = model_name("guest:codex", model_id)
        row = {"id": model_id, "name": name, "label": name,
               "efforts": efforts, "default_effort": default}
        if current and model_id == current:
            row["current"] = True
        rows.append(row)
    return rows


def _spawn_codex(argv: list[str], cwd: str):
    return subprocess.Popen(argv, cwd=cwd or None, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, bufsize=1,
                            env=dict(os.environ))


def parse_login_status(returncode: int, stdout: str, stderr: str = "") -> bool:
    """Whether `codex login status` says the CLI is signed in: the `Logged in …` line and exit 0.
    `Not logged in` (stderr, exit 1) and anything unrecognised are both "no", because a status
    that cannot be read is not one to launch a turn on."""
    text = ((stdout or "") + "\n" + (stderr or "")).lower()
    if "not logged in" in text:
        return False
    return returncode == 0 and "logged in" in text


def _short(text: str, limit: int = 160) -> str:
    one = " ".join((text or "").split())
    return one if len(one) <= limit else one[: limit - 1] + "…"


def _tool_start(kind: str, item: dict) -> tuple[dict, str]:
    """The `input` dict and the optional label for a `tool_started` event."""
    if kind == "commandExecution":
        command = item.get("command") or ""
        return {"command": command, "cwd": item.get("cwd")}, _short(command)
    if kind == "fileChange":
        changes = item.get("changes") or []
        paths = [str(c.get("path") or "") for c in changes]
        label = paths[0] if len(paths) == 1 else f"{len(paths)} files"
        return {"files": paths,
                "changes": [{"path": str(c.get("path") or ""),
                             "kind": (c.get("kind") or {}).get("type") or "update"}
                            for c in changes]}, label
    if kind == "mcpToolCall":
        server = item.get("server") or ""
        tool = item.get("tool") or ""
        return {"server": server, "tool": tool,
                "arguments": item.get("arguments")}, _short(f"{server}.{tool}".strip("."))
    if kind == "dynamicToolCall":
        tool = item.get("tool") or ""
        return {"tool": tool, "arguments": item.get("arguments"),
                "namespace": item.get("namespace")}, _short(tool)
    if kind == "collabAgentToolCall":
        return {"tool": item.get("tool") or "", "prompt": item.get("prompt"),
                "model": item.get("model")}, _short(item.get("prompt") or item.get("tool") or "")
    if kind == "webSearch":
        query = item.get("query") or ""
        return {"query": query}, _short(query)
    if kind == "imageView":
        path = str(item.get("path") or "")
        return {"path": path}, _short(path)
    return {k: v for k, v in item.items() if k not in ("type", "id")}, ""   # pragma: no cover


def _tool_result(kind: str, item: dict, state: dict) -> dict:
    """The `output`/`ok`/`diff` half of a `tool_result` event."""
    status = str(item.get("status") or "")
    if kind == "commandExecution":
        output = item.get("aggregatedOutput")
        if output is None:
            output = "".join(state.get("output") or [])
        exit_code = item.get("exitCode")
        ok = status == "completed" and (exit_code in (0, None))
        data = {"output": output or "", "ok": bool(ok)}
        if isinstance(exit_code, int) and not isinstance(exit_code, bool):
            data["exit_code"] = exit_code
        if exit_code is not None:
            data["output"] = data["output"] if ok else f"{data['output']}\n(exit {exit_code})"
        return data
    if kind == "fileChange":
        changes = item.get("changes") or state.get("changes") or []
        diff = "".join(_unified_diff(c) for c in changes)
        added, removed = _diff_counts(diff)
        names = ", ".join(str(c.get("path") or "") for c in changes)
        summary = f"{len(changes)} file(s) changed, +{added} -{removed}"
        data = {"output": f"{summary}: {names}" if names else summary,
                "ok": status == "completed"}
        if diff:
            data["diff"] = diff
        return data
    if kind == "mcpToolCall":
        error = item.get("error")
        result = item.get("result")
        return {"output": _stringify(error or result), "ok": status == "completed" and not error}
    if kind == "dynamicToolCall":
        parts = [c.get("text") or "" for c in (item.get("contentItems") or [])
                 if isinstance(c, dict)]
        success = item.get("success")
        ok = status == "completed" if success is None else bool(success)
        return {"output": "".join(parts), "ok": ok}
    if kind == "webSearch":
        results = item.get("results") or []
        return {"output": f"{len(results)} result(s) for {item.get('query') or ''}".strip(),
                "ok": True}
    if kind == "imageView":
        return {"output": str(item.get("path") or ""), "ok": True}
    return {"output": _stringify(item), "ok": status in ("", "completed")}  # pragma: no cover


def _stringify(value) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    try:
        return json.dumps(value)
    except (TypeError, ValueError):                                        # pragma: no cover
        return str(value)


def _unified_diff(change: dict) -> str:
    """A unified diff for one `FileUpdateChange`. Codex sends a unified body for an update and
    the file's whole content for an add or a delete, so the headers are put back here."""
    path = str(change.get("path") or "")
    kind = (change.get("kind") or {}).get("type") or "update"
    old, new = _diff_labels(path)
    body = change.get("diff") or ""
    if "@@" in body:
        if body.lstrip().startswith(("---", "diff ")):
            return body if body.endswith("\n") else body + "\n"
        return f"--- {old}\n+++ {new}\n" + (body if body.endswith("\n") else body + "\n")
    lines = body.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    count = len(lines)
    if kind == "delete":
        head = f"--- {old}\n+++ /dev/null\n@@ -1,{count} +0,0 @@\n"
        return head + "".join(f"-{line}\n" for line in lines)
    head = f"--- /dev/null\n+++ {new}\n@@ -0,0 +1,{count} @@\n"
    return head + "".join(f"+{line}\n" for line in lines)


def _diff_labels(path: str) -> tuple[str, str]:
    """`a/`/`b/` in front of a relative path, and an absolute path left as `diff -u` writes it."""
    if path.startswith("/"):
        return path, path
    return f"a/{path}", f"b/{path}"


def _diff_counts(diff: str) -> tuple[int, int]:
    added = removed = 0
    for line in diff.split("\n"):
        if line.startswith("+") and not line.startswith("+++"):
            added += 1
        elif line.startswith("-") and not line.startswith("---"):
            removed += 1
    return added, removed


def _approval_detail(method: str, params: dict, item: dict | None = None) -> str:
    """What the guest wants to do, in its own words, for the ask the pane draws (29.3).

    `item` is what the adapter has recorded about the item the approval is about. It matters for a
    file change: v1's `applyPatchApproval` carried a `changes` map, but v2's
    `item/fileChange/requestApproval` carries only `itemId`, `reason` and `grantRoot`, so without
    the item the ask could only quote codex's reason ("command failed; retry without sandbox?")
    and never say which file was about to be written. The user is deciding whether to let a guest
    write outside its sandbox; which file is the thing they need to know.
    """
    if method in ("item/commandExecution/requestApproval", "execCommandApproval"):
        command = params.get("command")
        if isinstance(command, list):
            command = " ".join(str(x) for x in command)
        return _short(str(command or "run a command"))
    if method in ("item/fileChange/requestApproval", "applyPatchApproval"):
        reason = str(params.get("reason") or "").strip()
        changes = params.get("changes")
        paths: list[str] = []
        if isinstance(changes, dict) and changes:                       # v1 carried them here
            paths = sorted(changes)
        elif isinstance(changes, list) and changes:
            paths = [str(c.get("path") or "") for c in changes if isinstance(c, dict)]
        if not paths:                                                   # v2: from the tracked item
            tracked = (item or {}).get("changes") or []
            paths = [str(c.get("path") or "") for c in tracked if isinstance(c, dict)]
        paths = [p for p in paths if p]
        if paths:
            edit = "edit " + ", ".join(paths[:3]) + ("…" if len(paths) > 3 else "")
            return _short(f"{edit} ({reason})" if reason else edit)
        return _short(reason or "apply its file changes")
    if method == "item/permissions/requestApproval":
        return _short(str(params.get("reason") or "widen its permissions"))
    if method == "mcpServer/elicitation/request":
        return _short(str(params.get("message") or "answer an MCP server"))
    return _short(method)                                                  # pragma: no cover


def _final_text(turn: _TurnState, payload: dict) -> str:
    """The turn's answer: the final-answer messages, or every message when codex named no phase."""
    messages = [m for m in turn.messages if m.get("text")]
    if not messages:
        messages = [{"phase": item.get("phase"), "text": item.get("text") or ""}
                    for item in (payload.get("items") or [])
                    if item.get("type") == "agentMessage"]
    final = [m for m in messages if m.get("phase") == "final_answer"]
    if not final:
        final = [m for m in messages if m.get("phase") in (None, "")]
    if not final:
        final = messages
    return "\n\n".join(m["text"] for m in final if m.get("text")).strip()
