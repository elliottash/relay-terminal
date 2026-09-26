# SPDX-License-Identifier: AGPL-3.0-or-later
"""A minimal MCP client: `initialize`, `tools/list`, `tools/call` (card #SSRQ).

Two transports, no third-party dependency:

- **stdio** — the server is a subprocess speaking newline-delimited JSON-RPC on stdin/stdout, the
  framing `guest_board_bridge.py` speaks from the server side. It gets `workspace_plugins.
  runtime_env()` plus its own `env` block, and nothing else of the worker's environment (the
  provider keys above all).
- **streamable HTTP** — each request is a POST; the answer is JSON or an SSE stream carrying it.
  `Mcp-Session-Id` is kept. OAuth is not spoken: a 401 says so.

A server that is down, slow or talks nonsense fails the one call with an `McpError` whose sentence
names the server — never the turn. Every call has a deadline, and Stop cancels it (a
`notifications/cancelled` goes to the server, and the wait ends at once). A dead stdio server is
started again on the next call. Error text never quotes the server's `env` or `headers`.
"""
from __future__ import annotations

import collections
import itertools
import json
import os
import subprocess
import threading
import time
import urllib.error
import urllib.request

from .mcp_config import ServerSpec, expand
from .workspace_plugins import runtime_env

PROTOCOL_VERSION = "2025-06-18"
CLIENT_INFO = {"name": "relay", "version": "1"}
START_TIMEOUT = 30.0
LIST_TIMEOUT = 30.0
MAX_LINE = 16 * 1024 * 1024
MAX_PAGES = 20
STDERR_LINES = 20


class McpError(RuntimeError):
    """One call failed; the message is written for the model and the user."""


class Cancelled(McpError):
    pass


class McpClient:
    def __init__(self, spec: ServerSpec, workspace: str | None = None):
        self.spec = spec
        self.workspace = workspace
        self._lock = threading.Lock()          # guards start/stop and the pending table
        self._write_lock = threading.Lock()
        self._start_lock = threading.Lock()     # one start at a time; a second caller waits for it
        self._ids = itertools.count(1)
        self._pending: dict[int, list] = {}
        self._proc: subprocess.Popen | None = None
        self._stderr: collections.deque[str] = collections.deque(maxlen=STDERR_LINES)
        self._session_id = ""
        self._initialized = False
        self.server_info: dict = {}
        self.instructions = ""

    # ----- lifecycle ---------------------------------------------------------------------
    @property
    def label(self) -> str:
        return f"MCP server {self.spec.name!r}"

    def alive(self) -> bool:
        if self.spec.transport == "http":
            return self._initialized
        return self._proc is not None and self._proc.poll() is None and self._initialized

    def ensure_started(self, cancel: threading.Event | None = None) -> None:
        with self._start_lock:
            self._start(cancel)

    def _start(self, cancel: threading.Event | None) -> None:
        with self._lock:
            if self.alive():
                return
            self._stop_locked()
            if self.spec.transport == "stdio":
                self._spawn()
        try:
            result = self._request("initialize", {
                "protocolVersion": PROTOCOL_VERSION, "capabilities": {}, "clientInfo": CLIENT_INFO},
                timeout=START_TIMEOUT, cancel=cancel, starting=True)
        except McpError:
            self.close()
            raise
        if not isinstance(result, dict):
            self.close()
            raise McpError(f"{self.label} answered initialize with something that is not an object.")
        self.server_info = result.get("serverInfo") if isinstance(result.get("serverInfo"), dict) else {}
        self.instructions = str(result.get("instructions") or "")[:2000]
        self._notify("notifications/initialized", {})
        self._initialized = True

    def _spawn(self) -> None:
        env = runtime_env()
        env.update({k: expand(v) for k, v in self.spec.env})
        cwd = expand(self.spec.cwd) if self.spec.cwd else (self.workspace or os.path.expanduser("~"))
        argv = [expand(self.spec.command), *(expand(a) for a in self.spec.args)]
        try:
            self._proc = subprocess.Popen(
                argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                cwd=os.path.expanduser(cwd), env=env, start_new_session=True)
        except (OSError, ValueError) as exc:
            self._proc = None
            reason = exc.strerror if isinstance(exc, OSError) and exc.strerror else str(exc)
            raise McpError(f"{self.label} could not be started ({argv[0]}: {reason}).") from exc
        proc = self._proc
        threading.Thread(target=self._read_stdout, args=(proc,), daemon=True,
                         name=f"mcp-{self.spec.name}-out").start()
        threading.Thread(target=self._read_stderr, args=(proc,), daemon=True,
                         name=f"mcp-{self.spec.name}-err").start()

    def close(self) -> None:
        with self._lock:
            self._stop_locked()

    def _stop_locked(self) -> None:
        self._initialized = False
        proc, self._proc = self._proc, None
        if proc is not None:
            try:
                proc.stdin.close()
            except OSError:
                pass
            try:
                proc.terminate()
                proc.wait(timeout=2)
            except (OSError, subprocess.TimeoutExpired):
                try:
                    proc.kill()
                except OSError:
                    pass
        if self.spec.transport == "http" and self._session_id:
            session, self._session_id = self._session_id, ""
            try:                                  # a courtesy; the server times it out otherwise
                request = urllib.request.Request(expand(self.spec.url), method="DELETE",
                                                 headers={**self._headers(), "Mcp-Session-Id": session})
                urllib.request.urlopen(request, timeout=2).close()
            except Exception:
                pass
        self._fail_all("the server stopped")

    def _fail_all(self, reason: str) -> None:
        for _id, slot in list(self._pending.items()):
            slot[1] = {"__relay_error__": reason}
            slot[0].set()
        self._pending.clear()

    # ----- the three operations ----------------------------------------------------------
    def list_tools(self, cancel: threading.Event | None = None) -> list[dict]:
        self.ensure_started(cancel)
        tools, cursor = [], None
        for _ in range(MAX_PAGES):
            result = self._request("tools/list", {"cursor": cursor} if cursor else {},
                                   timeout=LIST_TIMEOUT, cancel=cancel)
            page = result.get("tools") if isinstance(result, dict) else None
            if not isinstance(page, list):
                raise McpError(f"{self.label} answered tools/list without a tools list.")
            tools += [t for t in page if isinstance(t, dict) and isinstance(t.get("name"), str)]
            cursor = result.get("nextCursor")
            if not cursor:
                break
        return tools

    def call_tool(self, name: str, arguments: dict, cancel: threading.Event | None = None,
                  timeout: float | None = None) -> dict:
        self.ensure_started(cancel)
        result = self._request("tools/call", {"name": name, "arguments": arguments},
                               timeout=timeout or self.spec.timeout, cancel=cancel)
        if not isinstance(result, dict):
            raise McpError(f"{self.label} answered {name} with something that is not an object.")
        return result

    # ----- JSON-RPC --------------------------------------------------------------------
    def _request(self, method: str, params: dict, *, timeout: float,
                 cancel: threading.Event | None, starting: bool = False):
        if self.spec.transport == "http":
            return self._http_request(method, params, timeout=timeout, cancel=cancel)
        request_id = next(self._ids)
        slot = [threading.Event(), None]
        with self._lock:
            if self._proc is None or (not starting and not self._initialized):
                raise McpError(f"{self.label} is not running.")
            self._pending[request_id] = slot
        try:
            self._send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        except McpError:
            self._pending.pop(request_id, None)
            raise
        deadline = time.monotonic() + timeout
        while not slot[0].wait(0.05):
            if cancel is not None and cancel.is_set():
                self._pending.pop(request_id, None)
                self._notify("notifications/cancelled", {"requestId": request_id, "reason": "Stopped"})
                raise Cancelled(f"{method} on {self.label} was stopped.")
            if time.monotonic() > deadline:
                self._pending.pop(request_id, None)
                self._notify("notifications/cancelled", {"requestId": request_id, "reason": "Timed out"})
                raise McpError(f"{self.label} did not answer {method} within {timeout:g} seconds.")
        return self._unwrap(method, slot[1])

    def _unwrap(self, method: str, message):
        if not isinstance(message, dict):
            raise McpError(f"{self.label} sent an unreadable answer to {method}.")
        if "__relay_error__" in message:
            tail = self.stderr_tail()
            raise McpError(f"{self.label} stopped while answering {method} ({message['__relay_error__']})"
                           + (f"; its last output: {tail}" if tail else "") + ".")
        error = message.get("error")
        if isinstance(error, dict):
            raise McpError(f"{self.label} refused {method}: {str(error.get('message') or error)[:500]}")
        return message.get("result")

    def _send(self, message: dict) -> None:
        proc = self._proc
        if proc is None or proc.stdin is None:
            raise McpError(f"{self.label} is not running.")
        data = (json.dumps(message, separators=(",", ":")) + "\n").encode()
        try:
            with self._write_lock:
                proc.stdin.write(data)
                proc.stdin.flush()
        except (OSError, ValueError) as exc:
            raise McpError(f"{self.label} closed its input.") from exc

    def _notify(self, method: str, params: dict) -> None:
        try:
            if self.spec.transport == "http":
                self._http_post({"jsonrpc": "2.0", "method": method, "params": params}, timeout=5)
            else:
                self._send({"jsonrpc": "2.0", "method": method, "params": params})
        except Exception:
            pass

    def _read_stdout(self, proc: subprocess.Popen) -> None:
        stream = proc.stdout
        try:
            while True:
                line = stream.readline(MAX_LINE)
                if not line:
                    break
                try:
                    message = json.loads(line)
                except ValueError:
                    self._stderr.append("(non-JSON on stdout) " + line[:200].decode("utf-8", "replace").strip())
                    continue
                if isinstance(message, dict):
                    self._dispatch(message)
        except (OSError, ValueError):
            pass
        with self._lock:
            if self._proc is proc:
                self._initialized = False
            code = proc.poll()
            self._fail_all("it exited" + (f" with status {code}" if code is not None else ""))

    def _read_stderr(self, proc: subprocess.Popen) -> None:
        try:
            for line in iter(lambda: proc.stderr.readline(4096), b""):
                text = line.decode("utf-8", "replace").strip()
                if text:
                    self._stderr.append(text[:300])
        except (OSError, ValueError):
            pass

    def stderr_tail(self) -> str:
        return " | ".join(list(self._stderr)[-2:])[:400]

    def _dispatch(self, message: dict) -> None:
        if "method" in message:
            # A request from the server. Relay offers no client capabilities, so only `ping` has
            # an answer; everything else is refused so the server does not wait on it.
            if "id" in message:
                if message.get("method") == "ping":
                    reply = {"jsonrpc": "2.0", "id": message["id"], "result": {}}
                else:
                    reply = {"jsonrpc": "2.0", "id": message["id"],
                             "error": {"code": -32601, "message": "Method not supported by Relay."}}
                try:
                    self._send(reply)
                except McpError:
                    pass
            return
        slot = self._pending.pop(message.get("id"), None) if isinstance(message.get("id"), int) else None
        if slot is not None:
            slot[1] = message
            slot[0].set()

    # ----- streamable HTTP -------------------------------------------------------------
    def _headers(self) -> dict:
        return {k: expand(v) for k, v in self.spec.headers}

    def _http_post(self, payload: dict, *, timeout: float):
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
                   "MCP-Protocol-Version": PROTOCOL_VERSION, **self._headers()}
        if self._session_id:
            headers["Mcp-Session-Id"] = self._session_id
        request = urllib.request.Request(expand(self.spec.url), data=json.dumps(payload).encode(),
                                         headers=headers, method="POST")
        return urllib.request.urlopen(request, timeout=timeout)

    def _http_request(self, method: str, params: dict, *, timeout: float, cancel: threading.Event | None):
        request_id = next(self._ids)
        box: dict = {}

        def run():
            try:
                with self._http_post({"jsonrpc": "2.0", "id": request_id, "method": method,
                                      "params": params}, timeout=timeout) as response:
                    session = response.headers.get("Mcp-Session-Id")
                    if session:
                        self._session_id = session
                    kind = (response.headers.get("Content-Type") or "").split(";")[0].strip()
                    if kind == "text/event-stream":
                        box["message"] = self._read_sse(response, request_id)
                    else:
                        box["message"] = json.loads(response.read(MAX_LINE) or b"null")
            except urllib.error.HTTPError as exc:
                if exc.code == 404 and self._session_id:
                    self._session_id = ""
                    self._initialized = False
                box["error"] = (f"{self.label} answered {method} with HTTP {exc.code}"
                                + (" — it needs authorization Relay does not have (set its headers "
                                   "in the MCP config)" if exc.code in (401, 403) else "") + ".")
            except (urllib.error.URLError, OSError) as exc:
                reason = getattr(exc, "reason", None) or exc
                box["error"] = f"{self.label} could not be reached ({reason})."
            except ValueError:
                box["error"] = f"{self.label} sent an unreadable answer to {method}."

        worker = threading.Thread(target=run, daemon=True, name=f"mcp-{self.spec.name}-http")
        worker.start()
        deadline = time.monotonic() + timeout
        while worker.is_alive():
            worker.join(0.05)
            if cancel is not None and cancel.is_set():
                self._notify("notifications/cancelled", {"requestId": request_id, "reason": "Stopped"})
                raise Cancelled(f"{method} on {self.label} was stopped.")
            if time.monotonic() > deadline + 1:
                raise McpError(f"{self.label} did not answer {method} within {timeout:g} seconds.")
        if "error" in box:
            raise McpError(box["error"])
        return self._unwrap(method, box.get("message"))

    def _read_sse(self, response, request_id: int):
        data: list[str] = []
        for raw in response:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if line.startswith("data:"):
                data.append(line[5:].lstrip())
                continue
            if line == "" and data:
                try:
                    message = json.loads("\n".join(data))
                except ValueError:
                    message = None
                data = []
                if isinstance(message, dict) and message.get("id") == request_id and "method" not in message:
                    return message
        return None
