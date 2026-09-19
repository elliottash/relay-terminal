# SPDX-License-Identifier: GPL-3.0-or-later
"""The Claude IDE bridge: one sidecar per GUI run (issue GT7X, protocol 26.5).

Relay plays the *editor* side of Claude Code's IDE integration. A `claude` the user started in a
pane connects back to this sidecar and speaks the WebSocket variant of MCP that the official
extensions speak: JSON-RPC 2.0 over a loopback-only WebSocket (`initialize`, `tools/list`,
`tools/call`). Discovery is upstream's own: the pane shell carries `CLAUDE_CODE_SSE_PORT` and
`ENABLE_IDE_INTEGRATION` (`guest.bridge_env`, injected in `startTerminal`, protocol 26.2), and
`~/.claude/ide/<port>.lock` (pid, ideName "relay", workspaceFolders, transport "ws", authToken)
is what a claude started anywhere else in that shell reads to find the same server.

Shape of the thing, one process:

* **Lifecycle** — `python -m relay_core.guest_bridge serve --state-dir DIR`. It sweeps stale
  locks (a lock whose pid is gone), binds 127.0.0.1 on an ephemeral port, and only then writes
  its own lock — a lock names a port, so there is no lock before there is a port, and `0.lock`
  is never a file that exists. It prints one ready line on stdout (`{"ready": true, "port": N,
  "lock": path}`); the GUI reads that line and stops waiting. Logs go to stderr, one line each.
  `atexit`, SIGTERM/SIGINT and the GUI's death (PR_SET_PDEATHSIG) each remove the lock, so a
  crashed run leaves nothing behind but a lock the next run's sweep removes.
* **Panes** — the GUI registers each pane as `<state-dir>/panes/<token>.json`: token, runtime
  dir, the shell/guest-event.py helper path, python, workspace and cwd. The sidecar polls that
  directory (a pane's cwd moves; its registration is rewritten). A registration whose runtime
  dir has vanished is a closed pane and is dropped.
* **The channel out** — everything the sidecar learns reaches its pane the one way protocol
  26.3 allows: a `bridge` event dropped on the pane's event spool (`guest-events/` under its
  runtime dir). The writer is the hooks phase's `shell/guest-event.py`, called as
  `guest-event.py bridge claude` with the event's data on stdin and
  `RELAY_RUNTIME_DIR`/`RELAY_SESSION_TOKEN` in its environment — the same single writer the shim's own events use, so the pane reads one file
  one way, and the §26.3 envelope (token, fresh sequence, event, guest) is built in one place.
* **Blocking tools** — `openDiff` carries `old_file_path`, `new_file_path`,
  `new_file_contents`; the sidecar computes the unified diff (difflib, `a/`-`b/` headers, the
  same shape `tools.py` writes) and puts it in the event beside a `reply` path. The pane shows
  Relay's diff view and a decision; the answer arrives as `{"outcome": ...}` at that path and
  only then does the tools/call return: `FILE_SAVED` — after the sidecar has written
  `new_file_contents` to `new_file_path`, so the GUI never writes a user file — or
  `DIFF_REJECTED`, which is also what an unmatched or abandoned request answers, because a
  guest left hanging is worse than a guest told no.
* **Routing, and the path rule** — a request is matched to a pane by the longest workspace/cwd
  prefix of the paths it names. Unmatched requests are logged and dropped (protocol 26.5):
  openDiff answers `DIFF_REJECTED`, the rest answer a JSON-RPC error, so no call is ever left
  without a reply. *Every* path an openDiff names must resolve — `os.path.realpath`, so `..` and
  symlinks are followed first — inside that one pane, not just the path that chose it, and the
  check is made again against the live pane set at the moment of the write. The file the sidecar
  saves is always a file inside the workspace the user is looking at.
* **Nothing blocks the connection** — a pending openDiff is settled by its own task, so
  `tools/list`, pings and the client's own `close_tab` (which cancels the diff by tab name) are
  read and answered while a decision is on screen. A pending diff also ends on its own after
  `DIFF_TIMEOUT_SECONDS`, when its pane closes, when its connection drops, or at shutdown.
* **getDiagnostics answers `[]`** — Relay has no LSP source. Documented, not faked.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 26 (26.2, 26.3, 26.5).
Card: issues/features/2026-09-19-claude-codex-guest-integration.md (GT7X).
Upstream shape: coder/claudecode.nvim PROTOCOL.md (the reverse-engineered VS Code contract).
"""
from __future__ import annotations

import argparse
import asyncio
import atexit
import base64
import difflib
import hashlib
import hmac
import json
import os
import secrets
import signal
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from dataclasses import dataclass, field

from . import guest

MCP_PROTOCOL_VERSION = "2025-03-26"   # answered with the client's own when it names one
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
AUTH_HEADER = "x-claude-code-ide-authorization"   # upstream's custom WebSocket auth header
IDE_NAME = "relay"
# A frame or a reassembled message larger than this is refused *before* the bytes are read, so a
# peer cannot make the sidecar reserve memory by announcing a size. 4 MiB is far more than any
# real openDiff: claude sends whole files, and a 4 MiB source file is not one.
MAX_MESSAGE_BYTES = 4 * 1024 * 1024
POLL_SECONDS = 0.05                    # registration/reply polling tick; a stat, not a read
KEEPALIVE_SECONDS = 30.0               # a WebSocket ping, so an idle claude knows we live
HANDSHAKE_SECONDS = 10.0               # a connection that never sends its HTTP head is dropped
# A diff nobody ever answers must not pin a claude for the life of the GUI. Generous on purpose:
# the user may well leave a proposed change on screen over lunch.
DIFF_TIMEOUT_SECONDS = 30 * 60.0

try:   # remote/ws.py is this tree's RFC 6455 implementation; accept_key there is pure and
    # side-effect-free, so the bridge borrows it when the `remote` package is on the path.
    from remote.ws import accept_key as websocket_accept   # type: ignore[import-not-found]
except ImportError:   # the sidecar runs with only backend/ on PYTHONPATH: the same three lines.
    def websocket_accept(key: str) -> str:
        return base64.b64encode(hashlib.sha1((key + WEBSOCKET_GUID).encode()).digest()).decode()


# ----- atomic files ----------------------------------------------------------------------------


def write_json_atomic(path: str, payload) -> None:
    """`payload` as JSON at `path`, whole or not at all: a temp file beside it, then rename."""
    directory = os.path.dirname(path) or "."
    os.makedirs(directory, exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=directory, prefix=".relay-bridge-", suffix=".tmp")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            json.dump(payload, stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def read_json(path: str):
    try:
        with open(path, "r", encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, ValueError):
        return None


def pid_alive(pid) -> bool:
    """True when a process with this pid exists. pid 0/-1 and garbage are dead, not errors."""
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True   # exists, ours or not: not ours to remove
    except OSError:
        return False
    return True


# ----- the lock file -----------------------------------------------------------------------------


@dataclass
class LockFile:
    """`<lock_dir>/<port>.lock`: what a claude reads to find (and authenticate to) the bridge."""
    directory: str
    port: int
    pid: int
    token: str

    @property
    def path(self) -> str:
        return os.path.join(self.directory, f"{self.port}.lock")

    def write(self, workspace_folders) -> bool:
        """Write the lock, unless the port is not known yet.

        A lock is an *advertisement*: it names a port a claude will dial and the token it will
        present. Before `start()` returns there is no port, and writing `0.lock` would publish a
        live authToken at a port nobody listens on and leave a file `remove()` — which only ever
        unlinks the real port's name — could not clean up. So port 0 writes nothing."""
        if self.port <= 0:
            return False
        write_json_atomic(self.path, {"pid": self.pid, "workspaceFolders": sorted(set(workspace_folders)),
                                      "ideName": IDE_NAME, "transport": "ws", "authToken": self.token})
        return True

    def remove(self) -> None:
        try:
            os.unlink(self.path)
        except OSError:
            pass


def sweep_stale_locks(directory: str, keep_pid: int | None = None) -> list[str]:
    """Remove `<port>.lock` files whose IDE is gone. A live IDE's lock — ours or another
    editor's — is never touched; a lock with no readable pid is stale (upstream writes one)."""
    removed = []
    try:
        names = os.listdir(directory)
    except OSError:
        return removed
    for name in names:
        if not name.endswith(".lock"):
            continue
        path = os.path.join(directory, name)
        payload = read_json(path)
        pid = payload.get("pid") if isinstance(payload, dict) else None
        if keep_pid is not None and pid == keep_pid:
            continue
        if not pid_alive(pid):
            try:
                os.unlink(path)
                removed.append(path)
            except OSError:
                pass
    return removed


# ----- panes ------------------------------------------------------------------------------------


@dataclass
class PaneRegistration:
    """One pane of this GUI run, as the GUI registered it in the sidecar's state dir."""
    token: str
    runtime_dir: str
    helper: str            # <data>/shell/guest-event.py — the channel's one writer (26.3)
    python: str            # the interpreter Relay runs (RELAY_PYTHON), for the helper
    workspace: str
    cwd: str
    path: str = ""         # the registration file this came from
    seen: float = 0.0


def load_registrations(directory: str) -> dict[str, PaneRegistration]:
    """Every pane registration in `directory`, keyed by token. A pane whose runtime dir has
    vanished is a closed pane; its registration is not loaded (the GUI rewrites the file if the
    pane lives again, so dropping it here costs nothing)."""
    panes = {}
    try:
        names = os.listdir(directory)
    except OSError:
        return panes
    for name in names:
        if not name.endswith(".json"):
            continue
        path = os.path.join(directory, name)
        payload = read_json(path)
        if not isinstance(payload, dict):
            continue
        token = payload.get("token")
        runtime = payload.get("runtime_dir")
        if not isinstance(token, str) or not isinstance(runtime, str) or not os.path.isdir(runtime):
            continue
        panes[token] = PaneRegistration(
            token=token, runtime_dir=runtime,
            helper=str(payload.get("helper") or ""), python=str(payload.get("python") or sys.executable),
            workspace=str(payload.get("workspace") or ""), cwd=str(payload.get("cwd") or ""),
            path=path, seen=os.stat(path).st_mtime)
    return panes


def pane_for_path(panes: dict[str, PaneRegistration], path: str) -> PaneRegistration | None:
    """The pane a request about `path` belongs to: the longest workspace/cwd prefix of it.
    Ties go to the most recently written registration, so the pane the user is in wins."""
    if not path:
        return None
    target = os.path.realpath(path)
    best = None
    best_key = (-1, 0.0)
    for pane in panes.values():
        for root in (pane.workspace, pane.cwd):
            if not root:
                continue
            root = os.path.realpath(root)
            if target != root and not target.startswith(root.rstrip(os.sep) + os.sep):
                continue
            key = (len(root), pane.seen)
            if key > best_key:
                best, best_key = pane, key
    return best


def resolve_in_pane(panes: dict[str, PaneRegistration], token: str, path: str) -> str | None:
    """`path` made absolute and symlink-free, but only if it still lands inside the pane `token`
    owns. Otherwise None, and the caller must refuse.

    This is the whole of the bridge's file-write rule. A guest names two paths in one openDiff and
    only the old one decides which pane shows the diff, so without this a diff opened in a pane on
    the user's project could name `/tmp/…/authorized_keys` as the file to save and the sidecar
    would have written it — outside the workspace, outside the pane, outside anything the user
    agreed to. `realpath` first, because a symlink inside the workspace pointing out of it is the
    same attack with one more step."""
    if not path or not token:
        return None
    resolved = os.path.realpath(path)
    owner = pane_for_path(panes, resolved)
    if owner is None or owner.token != token:
        return None
    return resolved


def workspace_folders_of(panes: dict[str, PaneRegistration]) -> list[str]:
    folders = set()
    for pane in panes.values():
        for root in (pane.workspace, pane.cwd):
            if root:
                folders.add(root)
    return sorted(folders)


# ----- the unified diff the pane will show ------------------------------------------------------


def unified_diff(old_path: str, new_path: str, new_contents: str, old_contents: str | None) -> str:
    """The diff Relay's diff view shows for an openDiff, in the shape `tools.py` writes:
    `a/` and `b/` headers (or `/dev/null` for a file claude is creating)."""
    if old_contents is None:
        old_contents = ""
    diff = "".join(difflib.unified_diff(
        old_contents.splitlines(keepends=True), new_contents.splitlines(keepends=True),
        fromfile=f"a/{old_path}" if old_contents or os.path.exists(old_path) else "/dev/null",
        tofile=f"b/{new_path}"))
    return diff


def read_text(path: str, limit: int = MAX_MESSAGE_BYTES) -> str:
    """The old side of an openDiff. A file Relay cannot read (missing, huge, not text) opens as
    empty and the diff says so with its whole-file addition, which is still a true picture."""
    try:
        if os.path.getsize(path) > limit:
            return ""
        with open(path, "r", encoding="utf-8", errors="replace") as stream:
            return stream.read(limit)
    except OSError:
        return ""


def write_text_atomic(path: str, contents: str) -> None:
    directory = os.path.dirname(path) or "."
    os.makedirs(directory, exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=directory, prefix=".relay-bridge-", suffix=".tmp")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            stream.write(contents)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


# ----- the twelve published tools ----------------------------------------------------------------
#
# The VS Code extension registers exactly these (claudecode.nvim PROTOCOL.md); claude calls them
# over the WebSocket and reads MCP content arrays back. Relay answers what Relay has: a terminal
# has no editor selection, no dirty documents and no notebook kernel, and those tools say so
# with `success: false` rather than inventing an answer. getDiagnostics is the documented empty
# case (protocol 26.5): `[]`, because Relay has no LSP source.

TOOL_SPECS: tuple[dict, ...] = (
    {"name": "openFile",
     "description": "Open a file in the editor and optionally select a range of text.",
     "inputSchema": {"type": "object", "properties": {
         "filePath": {"type": "string", "description": "Absolute path of the file to open."},
         "preview": {"type": "boolean", "description": "Open in preview mode."},
         "startText": {"type": "string", "description": "Text to find and select from."},
         "endText": {"type": "string", "description": "Text to find and select to."},
         "selectToEndOfLine": {"type": "boolean", "description": "Extend the selection to the line end."},
         "makeFrontmost": {"type": "boolean", "description": "Make the file the active editor."}},
         "required": ["filePath"]}},
    {"name": "openDiff",
     "description": "Open a diff for the file and wait for the user to save or reject it. Blocking.",
     "inputSchema": {"type": "object", "properties": {
         "old_file_path": {"type": "string", "description": "Path of the file on disk."},
         "new_file_path": {"type": "string", "description": "Path the proposed contents belong to."},
         "new_file_contents": {"type": "string", "description": "The proposed contents."},
         "tab_name": {"type": "string", "description": "A name for the diff tab."}},
         "required": ["old_file_path", "new_file_path", "new_file_contents"]}},
    {"name": "getCurrentSelection",
     "description": "Get the current text selection in the active editor.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "getLatestSelection",
     "description": "Get the most recent text selection, even if it is not in the active editor.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "getOpenEditors",
     "description": "Get the editors currently open.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "getWorkspaceFolders",
     "description": "Get the workspace folders currently open.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "getDiagnostics",
     "description": "Get language diagnostics. Relay has no language server, so this is always empty.",
     "inputSchema": {"type": "object", "properties": {
         "uri": {"type": "string", "description": "File URI to limit the diagnostics to."}}}},
    {"name": "checkDocumentDirty",
     "description": "Check whether a document has unsaved changes.",
     "inputSchema": {"type": "object", "properties": {
         "filePath": {"type": "string", "description": "Path of the file to check."}},
         "required": ["filePath"]}},
    {"name": "saveDocument",
     "description": "Save a document with unsaved changes.",
     "inputSchema": {"type": "object", "properties": {
         "filePath": {"type": "string", "description": "Path of the file to save."}},
         "required": ["filePath"]}},
    {"name": "close_tab",
     "description": "Close a tab by name.",
     "inputSchema": {"type": "object", "properties": {
         "tab_name": {"type": "string", "description": "Name of the tab to close."}},
         "required": ["tab_name"]}},
    {"name": "closeAllDiffTabs",
     "description": "Close all diff tabs, rejecting any diffs still waiting for a decision.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "executeCode",
     "description": "Execute Python code in the notebook kernel of the current file.",
     "inputSchema": {"type": "object", "properties": {
         "code": {"type": "string", "description": "The code to execute."}},
         "required": ["code"]}},
)
TOOL_NAMES = tuple(spec["name"] for spec in TOOL_SPECS)

FILE_SAVED = "FILE_SAVED"
DIFF_REJECTED = "DIFF_REJECTED"


def text_result(text: str) -> dict:
    return {"content": [{"type": "text", "text": text}]}


def json_result(payload) -> dict:
    """Upstream stringifies JSON inside the text content; claude parses it back out."""
    return text_result(json.dumps(payload))


# ----- the bridge itself --------------------------------------------------------------------------


@dataclass
class PendingDiff:
    """An openDiff the pane has not answered yet, and everything needed to answer it without it:
    the pane that was asked (it may close), the connection that asked (another claude's
    `closeAllDiffTabs` is none of its business), the tab name (`close_tab` names it) and the wall
    clock deadline after which an unanswered diff is a rejected one."""
    reply_path: str
    future: asyncio.Future = field(default=None)
    pane_token: str = ""
    connection: object = None
    tab_name: str = ""
    deadline: float = 0.0


class Bridge:
    """The MCP side of the IDE integration. `dispatch()` is the whole JSON-RPC surface; the
    WebSocket layer only moves bytes. Tools that need a pane return a future when they must
    wait (openDiff) and the caller answers the JSON-RPC request when it resolves."""

    def __init__(self, lock: LockFile, state_dir: str, relay_version: str = "0"):
        self.lock = lock
        self.state_dir = state_dir
        self.relay_version = relay_version
        self.panes: dict[str, PaneRegistration] = {}
        self.panes_dir = os.path.join(state_dir, "panes")
        self.replies_dir = os.path.join(state_dir, "replies")
        self.pending: dict[str, PendingDiff] = {}   # reply path -> the openDiff waiting on it
        self.folders_written: list[str] = []
        # The connection whose message is being dispatched right now. `dispatch()` is synchronous
        # from end to end — no await anywhere below it — so on one event loop this is never two
        # connections at once, and a tool handler can ask "who called me?" without threading an
        # argument through twelve signatures. None when nothing is dispatching, and None for the
        # tests that drive `dispatch()` with no socket at all.
        self.connection: object = None

    # ----- plumbing the server calls ------------------------------------------------------------

    def log(self, message: str) -> None:
        print(f"guest_bridge {message}", file=sys.stderr, flush=True)

    def refresh_registrations(self) -> bool:
        """Re-read the panes dir when it changed; keep the lock's workspaceFolders true."""
        panes = load_registrations(self.panes_dir)
        changed = {token: pane for token, pane in panes.items()
                   if token not in self.panes or self.panes[token].seen != pane.seen}
        gone = [token for token in self.panes if token not in panes]
        self.panes = panes
        folders = workspace_folders_of(panes)
        if folders != self.folders_written:
            self.folders_written = folders
            self.lock.write(folders)
            return True
        return bool(changed) or bool(gone)

    def abandon_pane(self, token: str) -> None:
        """A pane closed while one of its diffs was waiting: the user cannot answer a pane that
        is gone, and claude must not wait forever."""
        for pending in list(self.pending.values()):
            if not pending.future.done() and pending.pane_token == token:
                self.settle(pending, DIFF_REJECTED, "pane closed")

    def abandon_connection(self, connection: object) -> None:
        """The claude that asked has hung up. Nothing is left to answer, so nothing may be saved:
        a decision the user makes after the guest is gone must not still write its file."""
        for pending in list(self.pending.values()):
            if not pending.future.done() and pending.connection is connection:
                self.settle(pending, DIFF_REJECTED, "connection closed")

    def expire_pending(self, now: float | None = None) -> int:
        """Reject the diffs whose wall-clock deadline has passed. A guest told no is recoverable;
        a guest blocked forever on a diff the user walked away from is not."""
        now = time.monotonic() if now is None else now
        expired = 0
        for pending in list(self.pending.values()):
            if pending.deadline and now >= pending.deadline and not pending.future.done():
                self.settle(pending, DIFF_REJECTED, "timed out")
                expired += 1
        return expired

    def settle(self, pending: PendingDiff, outcome: str, why: str = "") -> None:
        self.pending.pop(pending.reply_path, None)
        if not pending.future.done():
            pending.future.set_result(outcome)
        if why:
            self.log(f"open_diff_settled outcome={outcome} reason={why}")

    def check_replies(self) -> None:
        for reply_path, pending in list(self.pending.items()):
            payload = read_json(reply_path)
            if not isinstance(payload, dict):
                continue
            outcome = payload.get("outcome")
            if outcome in (FILE_SAVED, DIFF_REJECTED):
                try:
                    os.unlink(reply_path)
                except OSError:
                    pass
                self.settle(pending, outcome)

    # ----- the guest event channel (protocol 26.3) ------------------------------------------------

    def emit(self, pane: PaneRegistration, tool: str, args: dict, reply_path: str | None = None) -> bool:
        """One `bridge` event onto the pane's event spool, written by `shell/guest-event.py` —
        the channel's one writer (26.3): the helper builds the §26.3 envelope (token, fresh
        sequence, event, guest) and drops it into `guest-events/` as its own file; the bridge
        supplies only the event's data on stdin. A helper that is missing, fails, or has nowhere to write is
        a failed emit, and the caller tells claude so."""
        if not (pane.helper and os.path.isfile(pane.helper)):
            self.log(f"helper_missing tool={tool} helper={pane.helper!r}")
            return False
        if not os.path.isdir(pane.runtime_dir):
            # The runtime dir belongs to the pane; when it is gone the pane is gone, and a quiet
            # helper exit must not be read as a delivered event. (The registration poll drops
            # such panes; this catches one that closed inside the tick.)
            self.log(f"channel_error tool={tool} runtime_dir={pane.runtime_dir} is gone")
            return False
        data = dict(args)
        data["tool"] = tool
        if reply_path:
            data["reply"] = reply_path
        environment = helper_environment(pane)
        try:
            run = subprocess.run([pane.python, pane.helper, "bridge", "claude"], input=json.dumps(data),
                                 capture_output=True, text=True, timeout=10, env=environment)
        except (OSError, subprocess.SubprocessError) as error:
            self.log(f"helper_error tool={tool} {error!r}")
            return False
        if run.returncode != 0:
            self.log(f"helper_failed tool={tool} code={run.returncode} {run.stderr.strip()[:200]}")
            return False
        return True

    # ----- JSON-RPC --------------------------------------------------------------------------------

    def dispatch(self, message, connection: object = None) -> list | None:
        """One decoded WebSocket message -> the replies to send (each a dict), or None.
        A valid request that must wait returns a `Deferred` inside the list instead.

        `connection` is who asked; it is remembered for the duration of this call so that the
        tools which act on other calls (`close_tab`, `closeAllDiffTabs`) act only on this
        client's. Synchronous throughout, so the attribute cannot straddle two dispatches."""
        self.connection = connection
        try:
            if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
                return [rpc_error(None, -32600, "Invalid Request")]
            method = message.get("method")
            if not isinstance(method, str):
                return [rpc_error(message.get("id"), -32600, "Invalid Request")]
            has_id = "id" in message
            params = message.get("params") or {}
            if not isinstance(params, dict):
                params = {}
            result = self.handle(method, params, message.get("id") if has_id else None)
            if not has_id:
                return None   # a notification: nothing to answer
            if result is None:
                # `handle` answers None for the methods that are notifications *by convention*
                # (notifications/initialized and friends). A client that sends one with an id has
                # asked a question, and JSON-RPC has no reply whose whole body is `null`: that is
                # what went on the wire before, and a strict client rejects it. An empty result is
                # the honest answer — nothing to report, the request succeeded.
                return [rpc_ok(message.get("id"), {})]
            return [result]
        finally:
            self.connection = None

    def handle(self, method: str, params: dict, request_id):
        if method == "initialize":
            version = params.get("protocolVersion")
            return {"jsonrpc": "2.0", "id": request_id, "result": {
                "protocolVersion": version if isinstance(version, str) else MCP_PROTOCOL_VERSION,
                "capabilities": {"logging": {}, "prompts": {"listChanged": True},
                                 "resources": {"subscribe": True, "listChanged": True},
                                 "tools": {"listChanged": True}},
                "serverInfo": {"name": IDE_NAME, "version": self.relay_version}}}
        if method == "ping":
            return rpc_ok(request_id, {})
        if method == "tools/list":
            return rpc_ok(request_id, {"tools": [dict(spec) for spec in TOOL_SPECS]})
        if method == "tools/call":
            return self.call_tool(str(params.get("name") or ""), params.get("arguments") or {}, request_id)
        # MCP surfaces Relay's bridge does not carry; answering them empty keeps a client that
        # asks anyway talking to us instead of tearing the connection down.
        if method in ("prompts/list",):
            return rpc_ok(request_id, {"prompts": []})
        if method in ("resources/list",):
            return rpc_ok(request_id, {"resources": []})
        if method in ("notifications/initialized", "notifications/cancelled", "logging/setLevel"):
            return None
        return rpc_error(request_id, -32601, f"Method not found: {method}")

    def call_tool(self, name: str, arguments, request_id):
        if name not in TOOL_NAMES:
            return rpc_error(request_id, -32602, f"Unknown tool: {name}")
        if not isinstance(arguments, dict):
            arguments = {}
        handler = getattr(self, f"tool_{name}", None)
        if handler is None:
            return rpc_ok(request_id, tool_error(f"{name} is not available in Relay."))
        outcome = handler(arguments)
        if isinstance(outcome, Deferred):
            # A blocking tool: the server answers this request when the future settles, and that
            # reply must carry this request's own id, because the client matches on it.
            outcome.request_id = request_id
            return outcome
        return rpc_ok(request_id, outcome)

    # ----- the tools ---------------------------------------------------------------------------------

    def tool_openFile(self, arguments: dict) -> dict:
        path = str(arguments.get("filePath") or "")
        pane = pane_for_path(self.panes, path)
        if pane is None:
            self.log(f"unmatched tool=openFile path={path}")
            return tool_error(f"No Relay pane is open in a folder containing {path}.")
        self.emit(pane, "openFile", {"filePath": path,
                                     "makeFrontmost": bool(arguments.get("makeFrontmost", True))})
        return text_result(f"Opened file: {path}")

    def tool_openDiff(self, arguments: dict) -> dict | "Deferred":
        old_path = str(arguments.get("old_file_path") or "")
        new_path = str(arguments.get("new_file_path") or "")
        contents = arguments.get("new_file_contents")
        contents = contents if isinstance(contents, str) else ""
        tab_name = str(arguments.get("tab_name") or "")
        pane = pane_for_path(self.panes, old_path or new_path)
        if pane is None:
            # Unmatched is logged and dropped (26.5); openDiff answers DIFF_REJECTED rather than
            # nothing, because a blocking call that never returns hangs the guest.
            self.log(f"unmatched tool=openDiff old={old_path} new={new_path}")
            return text_result(DIFF_REJECTED)
        # Both paths must resolve inside the pane that matched, not just the one that chose it.
        # The routing above looks at old_file_path first, and new_file_path is what gets written.
        old_real = resolve_in_pane(self.panes, pane.token, old_path) if old_path else ""
        new_real = resolve_in_pane(self.panes, pane.token, new_path) if new_path else ""
        if (old_path and old_real is None) or (new_path and new_real is None):
            self.log(f"refused tool=openDiff reason=outside-pane pane={pane.token[:8]} "
                     f"old={old_path} new={new_path}")
            return text_result(DIFF_REJECTED)
        target = new_real or old_real
        if not target:
            self.log("refused tool=openDiff reason=no-path")
            return text_result(DIFF_REJECTED)
        os.makedirs(self.replies_dir, exist_ok=True)
        reply_path = os.path.join(self.replies_dir, f"{uuid.uuid4()}.json")
        source = old_real or target
        diff = unified_diff(source, target, contents, read_text(source))
        # The event carries the resolved paths: what the pane shows the user is the file the
        # sidecar would actually write, symlinks and `..` already followed.
        sent = self.emit(pane, "openDiff",
                         {"old_file_path": source, "new_file_path": target,
                          "new_file_contents": contents, "tab_name": tab_name,
                          "diff": diff, "file": target},
                         reply_path=reply_path)
        if not sent:
            self.log(f"open_diff_dropped old={old_path} new={new_path}")
            return text_result(DIFF_REJECTED)
        future = asyncio.get_running_loop().create_future()
        pending = PendingDiff(reply_path=reply_path, future=future, pane_token=pane.token,
                              connection=self.connection, tab_name=tab_name,
                              deadline=time.monotonic() + DIFF_TIMEOUT_SECONDS)
        self.pending[reply_path] = pending
        return Deferred(pending, contents, target)

    def tool_getCurrentSelection(self, arguments: dict) -> dict:
        return json_result({"success": False, "message": "No active editor found"})

    def tool_getLatestSelection(self, arguments: dict) -> dict:
        return json_result({"success": False, "message": "No selection available"})

    def tool_getOpenEditors(self, arguments: dict) -> dict:
        return json_result({"tabs": []})

    def tool_getWorkspaceFolders(self, arguments: dict) -> dict:
        folders = workspace_folders_of(self.panes)
        return json_result({"success": True,
                            "folders": [{"name": os.path.basename(f) or f,
                                         "uri": "file://" + f, "path": f} for f in folders],
                            "rootPath": folders[0] if folders else ""})

    def tool_getDiagnostics(self, arguments: dict) -> dict:
        return text_result("[]")   # documented empty: Relay has no LSP source (26.5)

    def tool_checkDocumentDirty(self, arguments: dict) -> dict:
        return json_result({"success": False,
                            "message": f"Document not open: {arguments.get('filePath', '')}"})

    def tool_saveDocument(self, arguments: dict) -> dict:
        return json_result({"success": False,
                            "message": f"Document not open: {arguments.get('filePath', '')}"})

    def tool_close_tab(self, arguments: dict) -> dict:
        # Relay has no editor tabs, but a diff *is* a tab here and this is how claude cancels one
        # it opened: it hits escape, sends close_tab with the same tab_name, and waits. Answering
        # TAB_CLOSED without settling that diff leaves its own openDiff blocked forever on a
        # decision it has just withdrawn. Only this connection's diffs, and only that name.
        tab_name = str(arguments.get("tab_name") or "")
        for pending in list(self.pending.values()):
            if tab_name and pending.tab_name == tab_name and pending.connection is self.connection:
                self.settle(pending, DIFF_REJECTED, f"close_tab {tab_name}")
        return text_result("TAB_CLOSED")   # upstream answers it unconditionally

    def tool_closeAllDiffTabs(self, arguments: dict) -> dict:
        # "All" is all of *this client's*. Two claudes in two panes share one sidecar, and one of
        # them tidying up must not cancel a diff the user is reading in the other.
        count = 0
        for pending in list(self.pending.values()):
            if pending.connection is not self.connection:
                continue
            self.settle(pending, DIFF_REJECTED, "closeAllDiffTabs")
            count += 1
        return text_result(f"CLOSED_{count}_DIFF_TABS")

    def tool_executeCode(self, arguments: dict) -> dict:
        return json_result({"success": False, "message": "Relay has no notebook kernel."})


HELPER_ENV_PASSTHROUGH = ("PATH", "HOME", "PYTHONPATH", "LANG", "LC_ALL")


def helper_environment(pane: PaneRegistration) -> dict[str, str]:
    """The environment `shell/guest-event.py` is run with: what a python script needs to start
    (PATH, HOME, PYTHONPATH, the locale) and the three variables that tell it which pane it is
    writing for. Nothing else.

    The sidecar inherits the GUI's whole environment, provider keys and all; handing that to a
    child spawned once per bridge event would put every one of them one `os.environ` away from
    whatever the helper — or anything it execs — decides to do. The helper's job needs six
    variables, so it gets six."""
    environment = {name: os.environ[name] for name in HELPER_ENV_PASSTHROUGH if name in os.environ}
    environment.setdefault("PATH", os.defpath)
    environment["RELAY_RUNTIME_DIR"] = pane.runtime_dir
    environment["RELAY_SESSION_TOKEN"] = pane.token
    environment["RELAY_PYTHON"] = os.environ.get("RELAY_PYTHON") or pane.python
    return environment


@dataclass
class Deferred:
    """A tool call whose answer comes later: what tool_openDiff returns to the server."""
    pending: PendingDiff
    contents: str   # new_file_contents; written by the bridge itself on FILE_SAVED
    new_path: str
    request_id: object = None   # filled in by call_tool; the settled reply carries it


def rpc_ok(request_id, result: dict) -> dict:
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def rpc_error(request_id, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def tool_error(message: str) -> dict:
    return {"content": [{"type": "text", "text": message}], "isError": True}


# ----- WebSocket, RFC 6455 server side --------------------------------------------------------------


class WebSocket:
    """One claude connection: the HTTP upgrade (with upstream's auth header), then frames.
    Server-to-client frames are unmasked; client-to-server frames must be masked and are
    unmasked here. Text frames carry one JSON document each (the MCP-over-WebSocket shape)."""

    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter, auth_token: str):
        self.reader = reader
        self.writer = writer
        self.auth_token = auth_token
        # Frames must not interleave: the read loop answers pings while settle tasks send tool
        # replies, and two coroutines writing a header each would produce one unreadable frame.
        self.send_lock = asyncio.Lock()

    async def handshake(self) -> bool:
        try:
            head = await asyncio.wait_for(self.reader.readuntil(b"\r\n\r\n"), HANDSHAKE_SECONDS)
        except asyncio.TimeoutError:
            # A connection that opens and then says nothing holds a task and a socket for as long
            # as the GUI runs. Ten seconds is forever for a loopback handshake.
            self.writer.close()
            return False
        request = head.decode("latin-1", "replace")
        headers = {}
        lines = request.split("\r\n")
        for line in lines[1:]:
            if ":" in line:
                key, value = line.split(":", 1)
                headers[key.strip().lower()] = value.strip()
        if not lines or not lines[0].startswith("GET ") or "websocket" not in headers.get("upgrade", "").lower():
            return await self._refuse("400 Bad Request", "expected a websocket upgrade")
        key = headers.get("sec-websocket-key")
        if not key:
            return await self._refuse("400 Bad Request", "missing Sec-WebSocket-Key")
        presented = headers.get(AUTH_HEADER, "")
        if not self.auth_token or not hmac.compare_digest(presented, self.auth_token):
            return await self._refuse("401 Unauthorized", "bad or missing auth token")
        accept = websocket_accept(key)
        self.writer.write(("HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                           f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
        await self.writer.drain()
        return True

    async def _refuse(self, status: str, why: str) -> bool:
        body = why.encode()
        self.writer.write(f"HTTP/1.1 {status}\r\nContent-Length: {len(body)}\r\n"
                          "Connection: close\r\n\r\n".encode() + body)
        try:
            await self.writer.drain()
        except (ConnectionError, OSError):
            pass
        return False

    async def read_message(self) -> str | None:
        """One complete text message, or None when the connection ended. Ping is answered and
        close is echoed here, so the caller only ever sees data and disconnect."""
        chunks = []
        size = 0
        while True:
            head = await self.reader.readexactly(2)
            fin, opcode = head[0] & 0x80, head[0] & 0x0F
            masked, length = head[1] & 0x80, head[1] & 0x7F
            if not masked:
                # RFC 6455 §5.1: a client frame is always masked, and a server that accepts an
                # unmasked one is the hole the masking rule exists to close (a crafted HTTP form
                # post read as a frame). Fail the connection, do not try to interpret it.
                await self.close(1002, "client frames must be masked")
                return None
            if length == 126:
                length = struct.unpack(">H", await self.reader.readexactly(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", await self.reader.readexactly(8))[0]
            # Both checks are before any allocation: the announced length, and what it would make
            # the reassembled message. A peer cannot reserve memory here by lying about a size.
            if length > MAX_MESSAGE_BYTES or size + length > MAX_MESSAGE_BYTES:
                await self.close(1009, "message too large")
                return None
            mask = await self.reader.readexactly(4)
            payload = await self.reader.readexactly(length) if length else b""
            payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
            if opcode == 0x8:   # close: echo and end
                await self.close(1000)
                return None
            if opcode == 0x9:   # ping: answer with a pong of the same payload
                await self._send_frame(0xA, payload)
                continue
            if opcode == 0xA:   # pong
                continue
            chunks.append(payload)
            size += len(payload)
            if fin:
                return b"".join(chunks).decode("utf-8", "replace")

    async def send_message(self, text: str) -> None:
        await self._send_frame(0x1, text.encode("utf-8"))

    async def _send_frame(self, opcode: int, payload: bytes) -> None:
        header = bytes([0x80 | opcode])
        length = len(payload)
        if length < 126:
            header += bytes([length])
        elif length < 65536:
            header += bytes([126]) + struct.pack(">H", length)
        else:
            header += bytes([127]) + struct.pack(">Q", length)
        async with self.send_lock:
            self.writer.write(header + payload)
            await self.writer.drain()

    async def ping(self) -> None:
        await self._send_frame(0x9, b"relay")

    async def close(self, code: int = 1000, reason: str = "") -> None:
        try:
            await self._send_frame(0x8, struct.pack(">H", code) + reason.encode()[:120])
        except (ConnectionError, asyncio.CancelledError):
            pass
        self.writer.close()


# ----- the server ----------------------------------------------------------------------------------


class BridgeServer:
    """The loopback listener and the ticking heart: registrations in, replies out, pings."""

    def __init__(self, bridge: Bridge):
        self.bridge = bridge
        self.server: asyncio.AbstractServer | None = None
        self._tasks: list[asyncio.Task] = []
        self._panes_stamp = 0.0
        self.connections: set[WebSocket] = set()
        self._last_ping = 0.0

    async def start(self) -> int:
        self.server = await asyncio.start_server(self._client, host="127.0.0.1", port=0)
        self._last_ping = time.monotonic()
        self._tasks.append(asyncio.ensure_future(self._tick()))
        return self.server.sockets[0].getsockname()[1]

    async def _client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        socket = WebSocket(reader, writer, self.bridge.lock.token)
        settlers: set[asyncio.Task] = set()
        try:
            if not await socket.handshake():
                writer.close()
                return
            self.connections.add(socket)
            await self._serve(socket, settlers)
        except (ConnectionError, asyncio.IncompleteReadError, asyncio.LimitOverrunError,
                asyncio.TimeoutError):
            pass
        except asyncio.CancelledError:
            raise
        except Exception as error:   # a broken client must not take the bridge down
            self.bridge.log(f"client_error {error!r}")
        finally:
            self.connections.discard(socket)
            # The guest is gone: nothing it asked can be answered, so nothing it asked may still
            # write a file. Settle first, then cancel — a settled future makes its task finish on
            # its own and the cancel below is only for the ones that were already sending.
            self.bridge.abandon_connection(socket)
            for task in list(settlers):
                task.cancel()
            try:
                writer.close()
            except OSError:
                pass

    async def _serve(self, socket: WebSocket, settlers: set[asyncio.Task]) -> None:
        while True:
            text = await socket.read_message()
            if text is None:
                return
            try:
                message = json.loads(text)
            except ValueError:
                await socket.send_message(json.dumps(rpc_error(None, -32700, "Parse error")))
                continue
            replies = self.bridge.dispatch(message, connection=socket)
            for reply in replies or []:
                if isinstance(reply, Deferred):
                    # A blocking tool is answered by its own task, never here. Awaiting it inline
                    # stops this loop reading, and then nothing else on the connection is served
                    # while a diff is on screen: no tools/list, no pong, and — worst — not the
                    # client's own close_tab, which is how it cancels the very diff we are waiting
                    # for. The deadlock was real; this is the fix.
                    task = asyncio.ensure_future(self._answer_when_settled(socket, reply))
                    settlers.add(task)
                    task.add_done_callback(settlers.discard)
                else:
                    await socket.send_message(json.dumps(reply))

    async def _answer_when_settled(self, socket: WebSocket, deferred: Deferred) -> None:
        pending = deferred.pending
        try:
            outcome = await pending.future
        except asyncio.CancelledError:
            self.bridge.settle(pending, DIFF_REJECTED, "cancelled")
            return   # the connection is going away; there is nobody to tell
        reply = rpc_ok(deferred.request_id, text_result(outcome))
        if outcome == FILE_SAVED:
            # The save is the bridge's to do (26.5): the GUI shows and decides, the MCP editor
            # writes, exactly as the diff editor it is standing in for would have.
            #
            # The path was checked when the diff was opened, but that was minutes ago: panes come
            # and go, and a symlink can be planted while a decision is on screen. So resolve it
            # again, against the pane set as it is now, and write only what comes back.
            target = resolve_in_pane(self.bridge.panes, pending.pane_token, deferred.new_path)
            if target is None:
                self.bridge.log(f"refused tool=openDiff reason=outside-pane-at-save "
                                f"path={deferred.new_path}")
                reply = rpc_ok(deferred.request_id, text_result(DIFF_REJECTED))
            else:
                try:
                    write_text_atomic(target, deferred.contents)
                except OSError as error:
                    self.bridge.log(f"save_failed path={target} {error!r}")
                    reply = rpc_ok(deferred.request_id,
                                   tool_error(f"Relay could not save {target}: {error}"))
        try:
            await socket.send_message(json.dumps(reply))
        except (ConnectionError, OSError) as error:
            self.bridge.log(f"reply_dropped {error!r}")

    async def _tick(self) -> None:
        """The one timer: panes in, replies out, stale diffs expired, a keepalive ping. Each is a
        stat or a tiny write; 20 times a second costs nothing and keeps a blocking openDiff
        responsive. The ping runs on its own, much slower, clock."""
        while True:
            try:
                await asyncio.sleep(POLL_SECONDS)
                try:
                    stamp = os.stat(self.bridge.panes_dir).st_mtime
                except OSError:
                    stamp = 0.0
                if stamp != self._panes_stamp:
                    self._panes_stamp = stamp
                    before = set(self.bridge.panes)
                    self.bridge.refresh_registrations()
                    for token in before - set(self.bridge.panes):
                        self.bridge.abandon_pane(token)
                self.bridge.check_replies()
                self.bridge.expire_pending()
                await self._keepalive()
            except asyncio.CancelledError:
                raise
            except Exception as error:
                self.bridge.log(f"tick_error {error!r}")

    async def _keepalive(self) -> None:
        """A ping on every live connection, every KEEPALIVE_SECONDS. An idle claude with a diff on
        screen sees nothing on the socket for as long as the user takes; the ping is how it — and
        anything between the two, a container's conntrack included — knows the bridge is alive."""
        now = time.monotonic()
        if now - self._last_ping < KEEPALIVE_SECONDS:
            return
        self._last_ping = now
        for socket in list(self.connections):
            try:
                await socket.ping()
            except (ConnectionError, OSError):
                self.connections.discard(socket)


# ----- entry point ----------------------------------------------------------------------------------


def install_parent_death_signal() -> None:
    """PR_SET_PDEATHSIG(SIGTERM): if the GUI dies without saying goodbye, the kernel tells us.
    Belt to the atexit brace `serve()` fastens once the lock exists — a SIGKILLed GUI runs no
    handlers at all, here or there."""
    try:
        import ctypes
        libc = ctypes.CDLL("libc.so.6", use_errno=True)
        libc.prctl(1, signal.SIGTERM)   # PR_SET_PDEATHSIG = 1
    except Exception:
        pass   # not Linux, or no ctypes: atexit and SIGTERM still cover the normal paths


def serve(state_dir: str, lock_dir: str | None = None, relay_version: str = "0",
          ready_line=print) -> int:
    """Run the bridge until SIGTERM/SIGINT. Returns the process exit code.

    The lock's life is bounded on three sides: it is not written until there is a port to
    advertise, an `atexit` handler removes it the moment it exists, and the normal exit path below
    removes it again (removing what is gone is not an error)."""
    install_parent_death_signal()
    lock_dir = lock_dir or guest.claude_ide_lock_dir()
    sweep_stale_locks(lock_dir)   # locks whose IDE is gone, so a fresh claude sees only live ones
    os.makedirs(os.path.join(state_dir, "panes"), exist_ok=True)
    os.makedirs(os.path.join(state_dir, "replies"), exist_ok=True)
    lock = LockFile(directory=lock_dir, port=0, pid=os.getpid(), token=secrets.token_hex(16))
    bridge = Bridge(lock, state_dir, relay_version)
    # The panes are read *after* the port is known, below: refreshing here would have the lock
    # written at port 0 the moment a pane was already registered.

    stopped = asyncio.Event()
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    def stop(*_):
        stopped.set()

    for sig in (signal.SIGTERM, signal.SIGINT):
        try:
            loop.add_signal_handler(sig, stop)
        except (NotImplementedError, RuntimeError, ValueError):
            # ValueError: not the main thread. Neither handler can be installed there, and
            # `signal.signal` says so the same way; the caller that runs the bridge off the main
            # thread is responsible for stopping it, and atexit still removes the lock.
            try:
                signal.signal(sig, lambda *_: stopped.set())
            except ValueError:
                pass

    server = BridgeServer(bridge)
    try:
        port = loop.run_until_complete(server.start())
    except OSError as error:
        ready_line(json.dumps({"ready": False, "error": f"bind failed: {error}"}))
        return 1
    lock.port = port
    bridge.refresh_registrations()   # may write the lock itself now that the port is real
    lock.write(bridge.folders_written)   # and it exists even when no pane has registered yet
    atexit.register(lock.remove)

    async def announce() -> None:
        ready_line(json.dumps({"ready": True, "port": port, "lock": lock.path}))
        await stopped.wait()
        for task in server._tasks:
            task.cancel()
        for pending in list(bridge.pending.values()):
            bridge.settle(pending, DIFF_REJECTED, "bridge stopping")
        server.server.close()

    announcer = asyncio.ensure_future(announce())
    try:
        loop.run_until_complete(announcer)
    finally:
        lock.remove()
        loop.close()
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="relay_core.guest_bridge",
                                     description="Relay's Claude IDE bridge sidecar (GT7X).")
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("serve", help="Run the bridge until SIGTERM (what the GUI starts).")
    run.add_argument("--state-dir", required=True, help="The GUI-made directory for this run's files.")
    run.add_argument("--lock-dir", default=None, help="Override ~/.claude/ide (tests).")
    run.add_argument("--relay-version", default="0", help="Reported as serverInfo.version.")
    args = parser.parse_args(argv)
    if args.command == "serve":
        return serve(args.state_dir, args.lock_dir, args.relay_version)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
