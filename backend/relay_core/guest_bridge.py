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
  locks (a lock whose *port* refuses a connection), binds 127.0.0.1 on an ephemeral port, and only then writes
  its own lock — a lock names a port, so there is no lock before there is a port, and `0.lock`
  is never a file that exists. It prints one ready line on stdout (`{"ready": true, "port": N,
  "lock": path}`); the GUI reads that line and stops waiting. Logs go to stderr, one line each.
  `atexit`, SIGTERM/SIGINT and the GUI's death (PR_SET_PDEATHSIG) each remove the lock, so a
  crashed run leaves nothing behind but a lock the next run's sweep removes.
* **Panes** — the GUI registers each pane as `<state-dir>/panes/<token>.json`: token, runtime
  dir, the shell/guest-event.py helper path, workspace, cwd, the foreground guest and the pane
  shell's pid. The sidecar polls that directory (a pane's cwd moves; its registration is
  rewritten). A registration whose runtime dir has vanished is a closed pane and is dropped.
* **The channel out** — everything the sidecar learns reaches its pane the one way protocol
  26.3 allows: a `bridge` event dropped on the pane's event spool (`guest-events/` under its
  runtime dir). The writer is the hooks phase's `shell/guest-event.py`, **imported** and called
  as `write_event("bridge", "claude", data, directory=…, token=…)` — the same single writer the
  shim's own events use, so the pane reads one file one way and the §26.3 envelope (token, fresh
  sequence, event, guest) is built in one place. Imported and not spawned because nothing may
  block the connection: an interpreter start per event stopped every claude on this sidecar.
* **Blocking tools** — `openDiff` carries `old_file_path`, `new_file_path`,
  `new_file_contents`; the sidecar computes the unified diff (difflib, `a/`-`b/` headers, the
  same shape `tools.py` writes) and puts it in the event beside a `reply` path. The pane shows
  Relay's diff view and a decision; the answer arrives as `{"outcome": ...}` at that path and
  only then does the tools/call return: `FILE_SAVED` — after the sidecar has written
  `new_file_contents` to `new_file_path`, so the GUI never writes a user file — or
  `DIFF_REJECTED`, which is also what an unmatched or abandoned request answers, because a
  guest left hanging is worse than a guest told no.
* **Routing, and the path rule** — a request is matched to the pane the calling claude is
  *running in*: from the peer address of the accepted socket, through `/proc/net/tcp` and
  `/proc/*/fd` to the pid, up its ancestry to a registered pane shell. When that walk cannot be
  made the fallback is the longest workspace/cwd prefix of the paths the request names.
  Unmatched requests are logged and dropped (protocol 26.5):
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

The frame layer is `remote/ws.py`, the tree's own RFC 6455 implementation, given this
connection's 4 MiB cap; only the auth header, the handshake deadline and the close codes are here.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 26 (26.2, 26.3, 26.5).
Card: issues/features/2026-09-19-claude-codex-guest-integration.md (GT7X).
Upstream shape: coder/claudecode.nvim PROTOCOL.md (the reverse-engineered VS Code contract).
"""
from __future__ import annotations

import argparse
import asyncio
import atexit
import difflib
import errno
import hmac
import importlib.util
import ipaddress
import json
import os
import secrets
import signal
import socket
import sys
import tempfile
import time
import uuid
from dataclasses import dataclass, field

from . import guest

# The RFC 6455 implementation is the tree's one, `remote/ws.py`, not a second copy of the frame
# rules (26.5). It sits beside `backend/` in the source tree and in the installed share directory;
# the GUI puts both on the sidecar's PYTHONPATH, and this shim covers a launcher that did not.
try:
    from remote import ws as remote_ws          # type: ignore[import-not-found]
except ImportError:                             # pragma: no cover - exercised by the shim itself
    sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    from remote import ws as remote_ws          # type: ignore[import-not-found]

MCP_PROTOCOL_VERSION = "2025-03-26"   # answered with the client's own when it names one
AUTH_HEADER = "x-claude-code-ide-authorization"   # upstream's custom WebSocket auth header
IDE_NAME = "relay"
# A frame or a reassembled message larger than this is refused *before* the bytes are read, so a
# peer cannot make the sidecar reserve memory by announcing a size. 4 MiB is far more than any
# real openDiff: claude sends whole files, and a 4 MiB source file is not one. It is handed to
# `remote.ws.accept` as that connection's `max_frame`; the remote sessions keep ws's own 2 MiB.
MAX_MESSAGE_BYTES = 4 * 1024 * 1024
POLL_SECONDS = 0.05                    # registration/reply polling tick; a stat, not a read
KEEPALIVE_SECONDS = 30.0               # a WebSocket ping, so an idle claude knows we live
HANDSHAKE_SECONDS = 10.0               # a connection that never sends its HTTP head is dropped
LOCK_PROBE_SECONDS = 0.25              # how long a stale-lock sweep waits for a port to answer
PROC_ROOT = "/proc"                    # overridden by the tests, which build a /proc of their own
# A diff nobody ever answers must not pin a claude for the life of the GUI. Generous on purpose:
# the user may well leave a proposed change on screen over lunch.
DIFF_TIMEOUT_SECONDS = 30 * 60.0

# One name for the handshake's accept key, for the tests that check it against RFC 6455's own
# example. It is `remote.ws`'s, like every other frame rule here.
websocket_accept = remote_ws.accept_key


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


def pid_start_time(pid, proc_root: str | None = None) -> int | None:
    """Field 22 of `/proc/<pid>/stat`: the clock ticks after boot at which this pid began.

    A pid on its own says nothing after a crash — Linux hands the number out again within hours —
    so the lock records this beside it and the sweep compares the two. None when the process is
    gone or `/proc` will not say."""
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return None
    try:
        with open(os.path.join(proc_root or PROC_ROOT, str(pid), "stat"), "r",
                  encoding="utf-8", errors="replace") as stream:
            text = stream.read()
    except OSError:
        return None
    # The second field is the executable name in parentheses and may itself contain spaces and
    # parentheses, so everything is counted from the *last* one.
    tail = text[text.rfind(")") + 1:].split()
    try:
        return int(tail[19])          # stat field 22 = the 20th field after the comm
    except (IndexError, ValueError):
        return None


def parent_pid(pid, proc_root: str | None = None) -> int | None:
    """The PPid of `pid` (stat field 4), or None when there is no readable stat."""
    try:
        with open(os.path.join(proc_root or PROC_ROOT, str(int(pid)), "stat"), "r",
                  encoding="utf-8", errors="replace") as stream:
            text = stream.read()
    except (OSError, TypeError, ValueError):
        return None
    tail = text[text.rfind(")") + 1:].split()
    try:
        parent = int(tail[1])         # stat field 4 = the 2nd field after the comm
    except (IndexError, ValueError):
        return None
    return parent if parent > 0 else None


def process_ancestry(pid, proc_root: str | None = None, limit: int = 64) -> list[int]:
    """`pid` and its parents, nearest first. Bounded, and it never repeats a pid: a `/proc` that
    is being faked, or read while it changes, must not spin here."""
    chain: list[int] = []
    seen: set[int] = set()
    try:
        current: int | None = int(pid)
    except (TypeError, ValueError):
        return chain
    while current and current > 1 and current not in seen and len(chain) < limit:
        chain.append(current)
        seen.add(current)
        current = parent_pid(current, proc_root)
    return chain


# ----- which claude is on this connection (26.5) ------------------------------------------------
#
# Upstream's handshake carries no pane identity, so two claudes in one project could not be told
# apart: both panes match every path a request names, the ranking below fell back to the newest
# registration, and a diff opened beside the wrong terminal. The connection does carry one fact,
# though - the peer's address and port - and on Linux that names the process: `/proc/net/tcp`
# maps the socket to an inode, `/proc/<pid>/fd` maps the inode back to a pid, and the pid's
# ancestry walks up to the pane shell the claude was started from. Every pane registers its
# shell's pid for exactly this walk.
#
# Best effort by design. A hardened `/proc` (hidepid=2), a non-Linux host, a claude that reached
# us through a forwarder, or a socket closed between the accept and the read all fail the walk,
# and the router falls back to the path ranking. Which of the two decided is logged.


def _normalise_ip(text: str) -> str | None:
    """An address in the one spelling both sides can be compared in: an IPv4-mapped v6 address
    (`::ffff:127.0.0.1`, which is how a v6 listener sees a v4 loopback peer) is its v4 self."""
    try:
        address = ipaddress.ip_address(text)
    except ValueError:
        return None
    return str(getattr(address, "ipv4_mapped", None) or address)


def _decode_proc_address(field: str) -> tuple[str, int] | None:
    """One `local_address`/`rem_address` column of /proc/net/tcp{,6}: hex, host-endian per 32-bit
    word for the address and big-endian for the port."""
    host_hex, _, port_hex = field.partition(":")
    try:
        raw = bytes.fromhex(host_hex)
        port = int(port_hex, 16)
    except ValueError:
        return None
    if len(raw) == 4:
        packed, family = raw[::-1], socket.AF_INET
    elif len(raw) == 16:
        packed = b"".join(raw[index:index + 4][::-1] for index in range(0, 16, 4))
        family = socket.AF_INET6
    else:
        return None
    try:
        host = _normalise_ip(socket.inet_ntop(family, packed))
    except (OSError, ValueError):
        return None
    return (host, port) if host else None


def socket_inode(host: str, port: int, proc_root: str | None = None) -> int | None:
    """The inode of the TCP socket whose *local* end is `host:port` — which, for the address an
    accepted connection reports as its peer, is the client's own socket."""
    proc_root = proc_root or PROC_ROOT
    wanted = _normalise_ip(host or "")
    if not wanted or not port:
        return None
    for name in ("tcp", "tcp6"):
        try:
            with open(os.path.join(proc_root, "net", name), "r",
                      encoding="utf-8", errors="replace") as stream:
                rows = stream.read().splitlines()
        except OSError:
            continue
        for row in rows[1:]:
            fields = row.split()
            if len(fields) < 10:
                continue
            if _decode_proc_address(fields[1]) != (wanted, port):
                continue
            try:
                return int(fields[9])
            except ValueError:
                continue
    return None


def pid_for_inode(inode: int, proc_root: str | None = None) -> int | None:
    """The pid holding the socket with this inode. Processes we may not read are skipped, not
    errors: on a normal desktop the sidecar and the claude are the same user."""
    proc_root = proc_root or PROC_ROOT
    target = f"socket:[{inode}]"
    try:
        names = os.listdir(proc_root)
    except OSError:
        return None
    for name in names:
        if not name.isdigit():
            continue
        descriptors = os.path.join(proc_root, name, "fd")
        try:
            handles = os.listdir(descriptors)
        except OSError:
            continue
        for handle in handles:
            try:
                if os.readlink(os.path.join(descriptors, handle)) == target:
                    return int(name)
            except OSError:
                continue
    return None


def peer_ancestry(host: str, port: int, proc_root: str | None = None) -> list[int]:
    """The pid on the other end of an accepted connection and its parents, nearest first; empty
    when the walk cannot be made."""
    inode = socket_inode(host, port, proc_root)
    if inode is None:
        return []
    pid = pid_for_inode(inode, proc_root)
    if pid is None:
        return []
    return process_ancestry(pid, proc_root)


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
        payload = {"pid": self.pid, "workspaceFolders": sorted(set(workspace_folders)),
                   "ideName": IDE_NAME, "transport": "ws", "authToken": self.token}
        # Relay's own addition to upstream's shape (an unknown key is ignored by the clients that
        # read this): the pid alone cannot be trusted after a crash, because Linux hands the
        # number out again. `sweep_stale_locks` compares this with the pid's current start time.
        started = pid_start_time(self.pid)
        if started is not None:
            payload["pidStartTime"] = started
        write_json_atomic(self.path, payload)
        return True

    def remove(self) -> None:
        try:
            os.unlink(self.path)
        except OSError:
            pass


def port_answers(port, timeout: float = LOCK_PROBE_SECONDS, host: str = "127.0.0.1"):
    """True when something accepts a loopback connection on `port`, False when the port refuses
    it, None when the answer is not knowable (a timeout, a firewall, no socket to spare).

    A lock file says one thing — "dial this port" — so this is the only question that matters
    about it. The connection is opened and dropped without a byte written; a bridge (ours or
    another editor's) reads an empty HTTP head, times out its handshake and forgets it."""
    try:
        port = int(port)
    except (TypeError, ValueError):
        return False
    if not 0 < port < 65536:
        return False
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(timeout)
    try:
        probe.connect((host, port))
        return True
    except ConnectionRefusedError:
        return False
    except (socket.timeout, TimeoutError):
        return None
    except OSError as error:
        return False if error.errno == errno.ECONNREFUSED else None
    finally:
        probe.close()


def lock_pid_alive(payload, proc_root: str | None = None) -> bool:
    """The old test, tightened: the lock's pid exists *and* it is the process that wrote the
    lock. A lock written before this field existed has only the pid to go on."""
    if not isinstance(payload, dict):
        return False
    pid = payload.get("pid")
    if not pid_alive(pid):
        return False
    recorded = payload.get("pidStartTime")
    if not isinstance(recorded, int):
        return True
    started = pid_start_time(pid, proc_root)
    return started is None or started == recorded


def sweep_stale_locks(directory: str, keep_pid: int | None = None, probe=None,
                      proc_root: str | None = None) -> list[str]:
    """Remove `<port>.lock` files whose IDE is not listening any more.

    Liveness is the **port**, not the pid. A lock is an advertisement — "an editor is listening
    here, with this token" — and after a crash the pid it names is recycled within hours, so
    `os.kill(pid, 0)` called a dead editor live and left the lock in place for good; every claude
    started in that project then dialled a port nobody was listening on and hung. So the sweep
    connects to the port on loopback with a short timeout. A port that **answers** is a live IDE,
    ours or another editor's, and its lock is never removed however wrong its pid looks. A port
    that **refuses** is gone, and so is its lock. Only when the probe cannot tell — a timeout, a
    host that will not let us connect at all — does the pid decide, and then together with the
    start time the lock recorded, so a recycled pid cannot resurrect a dead lock."""
    probe = probe or port_answers
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
        stem = name[: -len(".lock")]
        answered = probe(int(stem)) if stem.isdigit() else None
        if answered is True:
            continue
        if answered is None and lock_pid_alive(payload, proc_root):
            continue
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
    workspace: str
    cwd: str
    guest: str = ""        # the guest in the pane's foreground right now: "claude", or "" (26.1)
    shell_pid: int = 0     # the pane's shell: every process in the pane descends from it (26.5)
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
        try:
            shell_pid = int(payload.get("shell_pid") or 0)
        except (TypeError, ValueError):
            shell_pid = 0
        panes[token] = PaneRegistration(
            token=token, runtime_dir=runtime,
            helper=str(payload.get("helper") or ""),
            workspace=str(payload.get("workspace") or ""), cwd=str(payload.get("cwd") or ""),
            guest=str(payload.get("guest") or ""), shell_pid=shell_pid,
            path=path, seen=os.stat(path).st_mtime)
    return panes


def pane_for_path(panes: dict[str, PaneRegistration], path: str) -> PaneRegistration | None:
    """The pane a request about `path` belongs to: the longest workspace/cwd prefix of it, and
    among equals the one with a claude actually running in it.

    Every pane registers — it has to, because the port is in its shell's environment before a
    claude could be started there — so two panes open on one project are the ordinary case, and
    only one of them may have a guest. Ranking a pane that is not running a guest level with one
    that is put the diff on screen beside the wrong terminal: whichever registration had been
    rewritten last won, which is a shell that changed directory, not a claude. The guest flag comes
    second after the prefix length (a pane *inside* the file's own directory is still the better
    answer) and the registration mtime remains the last resort, for two panes that really are
    equivalent — two claudes in one project cannot be told apart from the paths alone. That last
    case is what `pane_for_peer` answers instead when the peer walk succeeds; this ranking is the
    fallback for when it does not."""
    if not path:
        return None
    target = os.path.realpath(path)
    best = None
    best_key = (-1, 0, 0.0)
    for pane in panes.values():
        for root in (pane.workspace, pane.cwd):
            if not root:
                continue
            root = os.path.realpath(root)
            if target != root and not target.startswith(root.rstrip(os.sep) + os.sep):
                continue
            key = (len(root), 1 if pane.guest else 0, pane.seen)
            if key > best_key:
                best, best_key = pane, key
    return best


def pane_for_peer(panes: dict[str, PaneRegistration], ancestry) -> PaneRegistration | None:
    """The pane a claude is *running in*, from the pid ancestry of its socket: the first pane
    shell the walk meets, nearest first, so a claude started inside a nested shell still lands on
    the pane that owns the terminal. None when nothing in the ancestry is a registered shell."""
    by_pid = {pane.shell_pid: pane for pane in panes.values() if pane.shell_pid}
    for pid in ancestry or ():
        pane = by_pid.get(pid)
        if pane is not None:
            return pane
    return None


def path_in_pane(pane: PaneRegistration, path: str) -> str | None:
    """`path` resolved, if it lands inside this pane's workspace or cwd; None otherwise."""
    resolved = os.path.realpath(path)
    for root in (pane.workspace, pane.cwd):
        if not root:
            continue
        root = os.path.realpath(root)
        if resolved == root or resolved.startswith(root.rstrip(os.sep) + os.sep):
            return resolved
    return None


def resolve_in_pane(panes: dict[str, PaneRegistration], token: str, path: str) -> str | None:
    """`path` made absolute and symlink-free, but only if it still lands inside the pane `token`
    owns. Otherwise None, and the caller must refuse.

    This is the whole of the bridge's file-write rule. A guest names two paths in one openDiff and
    only the old one decides which pane shows the diff, so without this a diff opened in a pane on
    the user's project could name `/tmp/…/authorized_keys` as the file to save and the sidecar
    would have written it — outside the workspace, outside the pane, outside anything the user
    agreed to. `realpath` first, because a symlink inside the workspace pointing out of it is the
    same attack with one more step.

    The question is containment in *this* pane, not "does this pane win the ranking for this
    path": since the peer walk can route a request to the second of two panes open on one
    project, asking the ranking here would have refused every path in the project it shares."""
    if not path or not token:
        return None
    pane = panes.get(token)
    return None if pane is None else path_in_pane(pane, path)


def workspace_folders_of(panes: dict[str, PaneRegistration]) -> list[str]:
    folders = set()
    for pane in panes.values():
        for root in (pane.workspace, pane.cwd):
            if root:
                folders.add(root)
    return sorted(folders)


# ----- the unified diff the pane will show ------------------------------------------------------


def diff_label(path: str, root: str = "") -> str:
    """What the `a/`-`b/` header calls a file: its path inside `root`, as `tools.py` writes it.

    Relay's own writes label a change `a/src/x.py`, relative to the workspace, and the diff view
    strips the prefix and shows the rest. The bridge's paths are absolute and already resolved, so
    without this the header read `a//home/you/project/src/x.py` — true, but not what the same view
    shows for every other diff in the app."""
    if not root or not path:
        return path
    root = root.rstrip(os.sep)
    if path == root:
        return os.path.basename(path) or path
    return path[len(root) + 1:] if path.startswith(root + os.sep) else path


def unified_diff(old_path: str, new_path: str, new_contents: str, old_contents: str | None,
                 root: str = "") -> str:
    """The diff Relay's diff view shows for an openDiff, in the shape `tools.py` writes:
    `a/` and `b/` headers (or `/dev/null` for a file claude is creating), the paths relative to
    `root` — the workspace of the pane the diff is about — when they are inside it."""
    if old_contents is None:
        old_contents = ""
    diff = "".join(difflib.unified_diff(
        old_contents.splitlines(keepends=True), new_contents.splitlines(keepends=True),
        fromfile=f"a/{diff_label(old_path, root)}" if old_contents or os.path.exists(old_path)
                 else "/dev/null",
        tofile=f"b/{diff_label(new_path, root)}"))
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
        the channel's one writer (26.3), **imported**, not spawned: the writer builds the §26.3
        envelope (token, fresh sequence, event, guest) and drops it into `guest-events/` as its
        own file, and the bridge supplies the event's data as an argument. A writer that is
        missing, fails, or has nowhere to write is a failed emit, and the caller tells claude so.

        It used to be a `subprocess.run(...)` with a 10-second timeout on the event loop, which
        meant every connection stopped — no `tools/list`, no pong, not even the client's own
        `close_tab` — for as long as an interpreter took to start. The owner's rule for this
        bridge is that nothing blocks the connection, so the write happens here: the spool file
        is a few hundred bytes (the writer caps an envelope at 256 KiB and truncates the rest),
        and an `os.replace` of that size is not something to hand to an executor."""
        writer = spool_writer(pane.helper)
        if writer is None:
            self.log(f"helper_missing tool={tool} helper={spool_writer_path(pane.helper)!r}")
            return False
        if not os.path.isdir(pane.runtime_dir):
            # The runtime dir belongs to the pane; when it is gone the pane is gone, and a quiet
            # write into a recreated directory must not be read as a delivered event. (The
            # registration poll drops such panes; this catches one that closed inside the tick.)
            self.log(f"channel_error tool={tool} runtime_dir={pane.runtime_dir} is gone")
            return False
        data = dict(args)
        data["tool"] = tool
        if reply_path:
            data["reply"] = reply_path
        # The pane's environment contract, as arguments: `RELAY_GUEST_EVENT` is the spool
        # directory (`guest-events/` under the pane's runtime dir) and `RELAY_SESSION_TOKEN` is
        # the token the envelope carries. The sidecar serves every pane, so it names both per
        # call rather than reading its own environment — which is the GUI's, provider keys and
        # all, and no longer goes anywhere near this.
        spool = os.path.join(pane.runtime_dir, getattr(writer, "EVENTS_DIR_NAME", "guest-events"))
        try:
            sequence = writer.write_event("bridge", "claude", data, directory=spool,
                                          token=pane.token)
        except (asyncio.CancelledError, KeyboardInterrupt):
            raise
        except BaseException as error:   # the writer promises not to raise; a bug in it is not ours
            self.log(f"helper_error tool={tool} {error!r}")
            return False
        if not sequence:
            self.log(f"helper_failed tool={tool} spool={spool}")
            return False
        return True

    def route(self, tool: str, path: str) -> PaneRegistration | None:
        """Which pane a request belongs to, and why (26.5).

        The peer walk first: the pid on the other end of this connection, up its ancestry, to the
        shell of one registered pane. That is the only thing that tells two claudes in one project
        apart, so when it answers it wins outright. When it cannot — no `/proc` to read, a pane
        that has not reported its shell pid yet, a claude that did not come straight down a
        loopback socket — the path ranking below decides, as it always did."""
        pane = pane_for_peer(self.panes, getattr(self.connection, "peer_ancestry", ()))
        if pane is not None:
            self.log(f"routed tool={tool} pane={pane.token[:8]} by=peer")
            return pane
        pane = pane_for_path(self.panes, path)
        if pane is None:
            return None
        self.log(f"routed tool={tool} pane={pane.token[:8]} by=ranking")
        return pane

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
        pane = self.route("openFile", path)
        if pane is None:
            self.log(f"unmatched tool=openFile path={path}")
            return tool_error(f"No Relay pane is open in a folder containing {path}.")
        if not self.emit(pane, "openFile", {"filePath": path,
                                            "makeFrontmost": bool(arguments.get("makeFrontmost", True))}):
            # An event that was not written is a file that will not open. Saying "Opened file"
            # anyway told the guest a thing it could check and find untrue, and `openDiff` next
            # to it has always refused out loud when its emit failed.
            self.log(f"open_file_dropped path={path}")
            return tool_error(f"Relay could not open {path}: the pane's event channel is gone.")
        return text_result(f"Opened file: {path}")

    def tool_openDiff(self, arguments: dict) -> dict | "Deferred":
        old_path = str(arguments.get("old_file_path") or "")
        new_path = str(arguments.get("new_file_path") or "")
        contents = arguments.get("new_file_contents")
        contents = contents if isinstance(contents, str) else ""
        tab_name = str(arguments.get("tab_name") or "")
        pane = self.route("openDiff", old_path or new_path)
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
        diff = unified_diff(source, target, contents, read_text(source),
                            root=pane.workspace or pane.cwd)
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
        # Whose diffs those are: this connection's, and — when the peer walk identified the claude
        # — only the ones opened in its own pane, so two claudes in one project that happen to
        # name a tab the same way cannot withdraw each other's decision.
        pane = pane_for_peer(self.panes, getattr(self.connection, "peer_ancestry", ()))
        for pending in list(self.pending.values()):
            if not tab_name or pending.tab_name != tab_name:
                continue
            if pending.connection is not self.connection:
                continue
            if pane is not None and pending.pane_token and pending.pane_token != pane.token:
                continue
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


_spool_writers: dict[str, object] = {}


def spool_writer_path(helper: str) -> str:
    """Which file the channel's writer is. `RELAY_GUEST_WRITER` overrides the pane's own
    registration, which is how a test points at another checkout — the same override
    `relay_core.guest_hook` honours."""
    return os.environ.get("RELAY_GUEST_WRITER") or helper or ""


def spool_writer(helper: str):
    """`shell/guest-event.py` as a module, loaded once per path — the importlib-by-path load
    `relay_core.guest_hook` already uses, so the spool's naming, atomicity and size cap exist in
    one file and the bridge does not start an interpreter to reach them. None when there is no
    such file, or it will not import; the caller reads that as a failed emit."""
    path = spool_writer_path(helper)
    if path in _spool_writers:
        return _spool_writers[path]
    module = None
    if path and os.path.isfile(path):
        try:
            spec = importlib.util.spec_from_file_location("relay_guest_event", path)
            candidate = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(candidate)
            module = candidate if hasattr(candidate, "write_event") else None
        except (asyncio.CancelledError, KeyboardInterrupt):
            raise
        except BaseException:   # SystemExit included: a file that is not the writer is not one
            module = None
    _spool_writers[path] = module
    return module


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


# ----- WebSocket: remote/ws.py, with the bridge's own doorman -------------------------------------
#
# The frame layer is `remote/ws.py`, the tree's RFC 6455 implementation, not a second copy of the
# same rules (26.5). What is left here is the part that is the bridge's and not the rendezvous's:
# upstream's `x-claude-code-ide-authorization` header, checked before the upgrade is completed;
# the handshake deadline; the 4 MiB frame cap, which ws takes as a per-connection `max_frame`; and
# the translation of a protocol fault into the close code claude is sent (1002, or 1009 for a
# message that is too big) — ws raises, a server decides what to say.


class Connection:
    """One claude on the bridge: its socket, and who is on the other end of it.

    The identity is why this is a class and not the bare `ws.WebSocket`. The peer walk is made
    once, when the connection is accepted, and the ancestry it returns is what every later
    request on this connection is routed by; walking `/proc` per tool call would ask the same
    question of the same unchanging process tree a hundred times."""

    def __init__(self, socket: remote_ws.WebSocket, peer_ancestry=()):
        self.socket = socket
        self.peer_ancestry = tuple(peer_ancestry)

    @property
    def peer(self) -> str:
        return self.socket.peer

    async def read_message(self) -> str | None:
        """One complete text message, or None when the connection ended. Pings are answered and a
        close is handled inside ws, so the caller only ever sees data and disconnect. A protocol
        fault fails the connection with its own close code, as this layer always did."""
        try:
            message = await self.socket.recv()
        except remote_ws.ConnectionClosed:
            return None
        except UnicodeDecodeError:
            # RFC 6455 §8.1: a text frame that is not valid UTF-8 fails the connection. ws decodes
            # strictly; this layer used to substitute replacement characters and hand the result
            # to the JSON parser, which answered "Parse error" to something that was never text.
            await self.close(1007, "a text frame must be valid UTF-8")
            return None
        except remote_ws.WebSocketError as error:
            await self.close(getattr(error, "code", 1002), str(error))
            return None
        return message if isinstance(message, str) else message.decode("utf-8", "replace")

    async def send_message(self, text: str) -> None:
        await self.socket.send(text)

    async def ping(self) -> None:
        await self.socket.ping(b"relay")

    async def close(self, code: int = 1000, reason: str = "") -> None:
        try:
            await self.socket.close(code, reason)
        except (remote_ws.WebSocketError, ConnectionError, OSError):
            pass


async def refuse_http(writer: asyncio.StreamWriter, status: str, why: str) -> None:
    """The one HTTP answer the bridge ever writes: it is an editor, not a web server."""
    body = why.encode()
    writer.write(f"HTTP/1.1 {status}\r\nContent-Length: {len(body)}\r\n"
                 "Connection: close\r\n\r\n".encode() + body)
    try:
        await writer.drain()
    except (ConnectionError, OSError):
        pass
    writer.close()


async def handshake(reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
                    auth_token: str) -> remote_ws.WebSocket | None:
    """Upstream's handshake: an HTTP upgrade carrying `x-claude-code-ide-authorization`. None
    when the connection was refused, and the refusal has already been written."""
    peer = writer.get_extra_info("peername") or ()
    try:
        request = await asyncio.wait_for(
            remote_ws.read_request(reader, peer=f"{peer[0]}:{peer[1]}" if len(peer) >= 2 else ""),
            HANDSHAKE_SECONDS)
    except asyncio.TimeoutError:
        # A connection that opens and then says nothing holds a task and a socket for as long as
        # the GUI runs. Ten seconds is forever for a loopback handshake.
        writer.close()
        return None
    except remote_ws.WebSocketError:
        await refuse_http(writer, "400 Bad Request", "the request head is too large")
        return None
    if request is None:
        writer.close()
        return None
    if request.method != "GET" or not request.wants_upgrade:
        await refuse_http(writer, "400 Bad Request", "expected a websocket upgrade")
        return None
    if not request.header("sec-websocket-key"):
        await refuse_http(writer, "400 Bad Request", "missing Sec-WebSocket-Key")
        return None
    presented = request.header(AUTH_HEADER)
    # Bytes, not str: `compare_digest` on two `str`s raises TypeError unless both are ASCII, and
    # the presented one is whatever the peer put in the header. A non-ASCII token is a wrong token
    # and gets the same 401 as an empty one, not a traceback and a dropped socket.
    if not auth_token or not hmac.compare_digest(presented.encode("utf-8", "surrogateescape"),
                                                 auth_token.encode()):
        await refuse_http(writer, "401 Unauthorized", "bad or missing auth token")
        return None
    try:
        return await remote_ws.accept(request, reader, writer, max_frame=MAX_MESSAGE_BYTES)
    except remote_ws.WebSocketError as error:
        await refuse_http(writer, "400 Bad Request", str(error))
        return None


# ----- the server ----------------------------------------------------------------------------------


class BridgeServer:
    """The loopback listener and the ticking heart: registrations in, replies out, pings."""

    def __init__(self, bridge: Bridge):
        self.bridge = bridge
        self.server: asyncio.AbstractServer | None = None
        self._tasks: list[asyncio.Task] = []
        self._panes_stamp = 0.0
        self.connections: set[Connection] = set()
        self._last_ping = 0.0

    async def start(self) -> int:
        self.server = await asyncio.start_server(self._client, host="127.0.0.1", port=0)
        self._last_ping = time.monotonic()
        self._tasks.append(asyncio.ensure_future(self._tick()))
        return self.server.sockets[0].getsockname()[1]

    async def _client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        settlers: set[asyncio.Task] = set()
        socket = None
        try:
            accepted = await handshake(reader, writer, self.bridge.lock.token)
            if accepted is None:
                writer.close()
                return
            socket = Connection(accepted, self.identify(writer))
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
            if socket is not None:
                self.bridge.abandon_connection(socket)
            for task in list(settlers):
                task.cancel()
            try:
                writer.close()
            except OSError:
                pass

    def identify(self, writer: asyncio.StreamWriter) -> tuple[int, ...]:
        """The pid ancestry of whoever opened this connection, once, at accept time (26.5). An
        empty tuple is "the walk did not work here", and the router falls back to the path
        ranking; either way the decision is logged when a request is routed."""
        peer = writer.get_extra_info("peername") or ()
        if len(peer) < 2:
            return ()
        try:
            ancestry = tuple(peer_ancestry(str(peer[0]), int(peer[1])))
        except OSError as error:                      # a /proc that will not be read
            self.bridge.log(f"peer_walk_failed {error!r}")
            return ()
        self.bridge.log(f"peer_walk peer={peer[0]}:{peer[1]} pids={list(ancestry[:4])}")
        return ancestry

    async def _serve(self, socket: Connection, settlers: set[asyncio.Task]) -> None:
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

    async def _answer_when_settled(self, socket: Connection, deferred: Deferred) -> None:
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
