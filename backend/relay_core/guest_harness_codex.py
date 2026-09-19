# SPDX-License-Identifier: GPL-3.0-or-later
"""Tier A: Codex as a headless harness, over `codex app-server` (GT7X, protocol 29).

`codex app-server` speaks JSON-RPC 2.0 over newline-delimited JSON on stdin/stdout (the default
`--listen stdio://`). This module drives one such process for one pane and presents it as the
`guest_harness.Harness` contract, so the worker never learns a word of Codex's vocabulary.

The flow one turn takes, as recorded from codex-cli 0.155.1 (fixtures in
`tests/fixtures/guest_harness_codex/`):

    -> initialize {clientInfo:{name:"relay",version}}        <- {userAgent, codexHome, ...}
    -> initialized (notification)
    -> thread/start {cwd, approvalPolicy, sandbox}           <- {thread:{id,...}, model, ...}
    -> turn/start {threadId, input:[{type:"text",text}]}     <- {turn:{id, status:"inProgress"}}
       <- turn/started, item/started, item/agentMessage/delta, item/completed,
          thread/tokenUsage/updated, turn/completed {turn:{status:"completed"}}

The protocol is machine-readable: `codex app-server generate-json-schema --out <dir>` writes
`ClientRequest.json` (every request), `ServerRequest.json` (every request the server makes of us,
i.e. the approvals) and `ServerNotification.json` (every notification). Anything not named below
is ignored and logged at debug, so a newer codex cannot break a pane.
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

from .guest_harness import (Emit, HarnessError, HarnessEvent, HarnessNotAvailable, HarnessStart,
                            TurnResult, map_tool_name, validate_permissions)

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
        self._session_id = ""
        self._model = ""
        self._pending_model: str | None = None
        self._announce_started = False
        self._permissions = "bypass"
        self._closed = False
        self._dead = False
        self._dead_reason = ""

    # ----- the contract ------------------------------------------------------------------------

    def start(self, *, cwd: str, model: str | None = None, resume: str | None = None,
              fork: bool = False, permissions: str = "bypass") -> HarnessStart:
        with self._lock:
            if self._proc is not None:
                raise HarnessError("this codex harness has already been started.")
            self._permissions = validate_permissions(permissions)
        exe = shutil.which(self._codex_path)
        if not exe:
            raise HarnessNotAvailable(
                f"Codex is not installed here: {self._codex_path!r} is not on PATH. "
                "Install it with `npm i -g @openai/codex` and sign in with `codex login`.")
        try:
            proc = self._spawn([exe, "app-server"], cwd)
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
            result = self._start_thread(cwd=cwd, model=model, resume=resume, fork=fork)
        except HarnessNotAvailable:
            self.close()
            raise
        except HarnessError as exc:
            self.close()
            raise HarnessNotAvailable(f"Codex's app-server did not come up: {exc}") from exc

        thread = result.get("thread") or {}
        self._session_id = str(thread.get("id") or thread.get("sessionId") or "")
        self._model = str(result.get("model") or thread.get("model") or model or "")
        if not self._session_id:
            self.close()
            raise HarnessNotAvailable("Codex's app-server started no thread (no id came back).")
        self._announce_started = True
        return HarnessStart(session_id=self._session_id, model=self._model)

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
            params = {"threadId": self._session_id,
                      "input": self._build_input(prompt, attachments)}
            with self._lock:
                if self._pending_model:
                    params["model"] = self._pending_model
                    self._pending_model = None
            response = self._request("turn/start", params)
            turn.turn_id = str(((response.get("turn") or {}).get("id")) or "")
            if turn.interrupt_requested or cancel.is_set():
                self._write_interrupt(turn)
            return self._run_turn(turn, cancel)
        finally:
            with self._lock:
                self._turn = None

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
        self._fail_pending(HarnessError("the codex harness was closed."))

    @property
    def session_id(self) -> str:
        return self._session_id

    @property
    def model(self) -> str:
        return self._model

    # ----- starting the thread ------------------------------------------------------------------

    def _start_thread(self, *, cwd: str, model: str | None, resume: str | None,
                      fork: bool) -> dict:
        policy, sandbox = PERMISSION_MODES[self._permissions]
        params: dict = {"cwd": cwd, "approvalPolicy": policy, "sandbox": sandbox}
        if model:
            params["model"] = model
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
        state = turn.items.setdefault(str(params.get("itemId") or ""), {})
        state.setdefault("output", []).append(str(params.get("delta") or ""))

    def _on_item_fileChange_outputDelta(self, turn, params):
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
        if self._model:
            data["model"] = self._model
        window = state.get("modelContextWindow")
        last = (state.get("last") or {}).get("totalTokens")
        if window and last:
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
        if method == "mcpServer/elicitation/request":
            self._respond(rid, {"action": "accept" if allow else "decline"})
        elif method == "item/permissions/requestApproval":
            if allow:
                self._respond(rid, {"permissions": record.get("permissions") or {},
                                    "scope": "turn"})
            else:
                self._respond_error(rid, -32000,
                                    decision.get("message") or "the user refused the request.")
        elif method in ("execCommandApproval", "applyPatchApproval"):
            self._respond(rid, {"decision": "approved" if allow else "denied"})
        else:
            self._respond(rid, {"decision": "accept" if allow else "decline"})

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
            record.update(kind=_APPROVAL_METHODS[method], detail=_approval_detail(method, params))
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

    def _request_async(self, method: str, params: dict) -> None:
        pending, message = self._prepare(method, params)
        pending.ignore = True
        self._write(message)

    def _request(self, method: str, params: dict, timeout: float | None = None) -> dict:
        pending, message = self._prepare(method, params)
        try:
            self._write(message)
        except HarnessError:
            with self._lock:
                self._pending.pop(pending.key, None)
            raise
        if not pending.event.wait(timeout if timeout is not None else self._request_timeout):
            with self._lock:
                self._pending.pop(pending.key, None)
            raise HarnessError(f"Codex did not answer {method} in time.")
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
        except Exception:                                                  # pragma: no cover
            log.warning("codex harness: a %s event could not be delivered.", kind, exc_info=True)


class _Pending:
    __slots__ = ("key", "event", "result", "error", "ignore")

    def __init__(self, key: str):
        self.key = key
        self.event = threading.Event()
        self.result: dict | None = None
        self.error: HarnessError | None = None
        self.ignore = False

    def done(self, result: dict) -> None:
        self.result = result
        self.event.set()

    def fail(self, error: HarnessError) -> None:
        self.error = error
        self.event.set()


# ----- pure helpers ------------------------------------------------------------------------------


def _spawn_codex(argv: list[str], cwd: str):
    return subprocess.Popen(argv, cwd=cwd or None, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, bufsize=1,
                            env=dict(os.environ))


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


def _approval_detail(method: str, params: dict) -> str:
    if method in ("item/commandExecution/requestApproval", "execCommandApproval"):
        command = params.get("command")
        if isinstance(command, list):
            command = " ".join(str(x) for x in command)
        return _short(str(command or "run a command"))
    if method in ("item/fileChange/requestApproval", "applyPatchApproval"):
        reason = params.get("reason")
        changes = params.get("changes")
        if isinstance(changes, dict) and changes:
            return _short("edit " + ", ".join(sorted(changes)))
        return _short(str(reason or "apply its file changes"))
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
