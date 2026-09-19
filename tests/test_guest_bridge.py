"""The Claude IDE bridge (protocol 26.5): the lock file, the JSON-RPC/tool layer, and the
blocking openDiff — the last two frame by frame over a real WebSocket, with a hand-rolled client
standing in for claude (no claude process is ever started).

The pieces that a person watches in a live GUI — the diff view, the pane banner — are not here;
docs/qa_evidence/2026-09-19-claude-codex-guest-integration/ has the Xvfb run that shows them.
"""
import asyncio
import json
import os
import secrets
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from relay_core import guest_bridge

BACKEND = Path(__file__).resolve().parents[1] / "backend"


# ----- fixtures ---------------------------------------------------------------------------------


def make_state(root: str) -> str:
    state = os.path.join(root, "state")
    os.makedirs(os.path.join(state, "panes"), exist_ok=True)
    return state


def register_pane(state: str, workspace: str, cwd: str | None = None, helper: str = "",
                  runtime: str | None = None) -> tuple[str, str]:
    """One pane registration, the file the GUI writes: token, runtime dir, helper, workspace."""
    runtime = runtime or tempfile.mkdtemp(dir=state, prefix="runtime-")
    token = secrets.token_hex(8)
    guest_bridge.write_json_atomic(os.path.join(state, "panes", f"{token}.json"),
                                   {"token": token, "runtime_dir": runtime, "helper": helper,
                                    "python": sys.executable, "workspace": workspace,
                                    "cwd": cwd or workspace})
    return token, runtime


def make_bridge(root: str, relay_version: str = "9.9") -> tuple[guest_bridge.Bridge, str, str]:
    state = make_state(root)
    lock_dir = os.path.join(root, "ide")
    os.makedirs(lock_dir, exist_ok=True)
    lock = guest_bridge.LockFile(directory=lock_dir, port=0, pid=os.getpid(), token=secrets.token_hex(16))
    bridge = guest_bridge.Bridge(lock, state, relay_version)
    bridge.refresh_registrations()
    return bridge, state, lock_dir


def read_guest_json(runtime: str) -> dict:
    with open(os.path.join(runtime, "guest.json"), "r", encoding="utf-8") as stream:
        return json.load(stream)


# ----- the lock file ------------------------------------------------------------------------------


class PidAlive(unittest.TestCase):
    def test_live_pid(self):
        self.assertTrue(guest_bridge.pid_alive(os.getpid()))
        self.assertTrue(guest_bridge.pid_alive(str(os.getpid())))

    def test_dead_and_garbage_pids(self):
        for pid in (0, -1, 0x7FFFFFFE, "nonsense", None, {}):
            with self.subTest(pid=pid):
                self.assertFalse(guest_bridge.pid_alive(pid))


class LockFileLifecycle(unittest.TestCase):
    def test_write_and_remove(self):
        with tempfile.TemporaryDirectory() as root:
            lock = guest_bridge.LockFile(directory=root, port=41234, pid=os.getpid(), token="ab" * 16)
            lock.write(["/b", "/a", "/a"])
            self.assertEqual(os.path.join(root, "41234.lock"), lock.path)
            payload = guest_bridge.read_json(lock.path)
            self.assertEqual({"pid": os.getpid(), "ideName": "relay", "transport": "ws",
                              "workspaceFolders": ["/a", "/b"], "authToken": "ab" * 16}, payload)
            lock.remove()
            self.assertFalse(os.path.exists(lock.path))
            lock.remove()   # removing what is gone is not an error


class SweepStaleLocks(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()

    def _lock(self, port: int, payload) -> str:
        path = os.path.join(self.root, f"{port}.lock")
        with open(path, "w", encoding="utf-8") as stream:
            if isinstance(payload, str):
                stream.write(payload)
            else:
                json.dump(payload, stream)
        return path

    def test_dead_ide_is_swept_live_ide_is_kept(self):
        dead = self._lock(1, {"pid": 0x7FFFFFFE, "ideName": "VS Code"})
        live = self._lock(2, {"pid": os.getpid(), "ideName": "relay"})
        junk = self._lock(3, "not json at all")
        nolock = self._lock(4, {"pid": 0x7FFFFFFE})
        os.rename(nolock, os.path.join(self.root, "4.json"))
        removed = guest_bridge.sweep_stale_locks(self.root)
        self.assertEqual([dead, junk], sorted(removed))
        self.assertTrue(os.path.exists(live))
        self.assertTrue(os.path.exists(os.path.join(self.root, "4.json")))   # not a lock file

    def test_keep_pid_is_spared_even_when_it_looks_dead(self):
        path = self._lock(7, {"pid": 0x7FFFFFFE})
        self.assertEqual([], guest_bridge.sweep_stale_locks(self.root, keep_pid=0x7FFFFFFE))
        self.assertTrue(os.path.exists(path))

    def test_missing_directory_is_not_an_error(self):
        self.assertEqual([], guest_bridge.sweep_stale_locks(os.path.join(self.root, "nope")))


# ----- panes and routing -------------------------------------------------------------------------


class Registrations(unittest.TestCase):
    def test_a_valid_registration_loads(self):
        with tempfile.TemporaryDirectory() as root:
            state = make_state(root)
            token, runtime = register_pane(state, "/work")
            panes = guest_bridge.load_registrations(os.path.join(state, "panes"))
            self.assertEqual([token], list(panes))
            self.assertEqual(runtime, panes[token].runtime_dir)
            self.assertEqual("/work", panes[token].workspace)

    def test_a_pane_whose_runtime_dir_vanished_is_dropped(self):
        with tempfile.TemporaryDirectory() as root:
            state = make_state(root)
            _, runtime = register_pane(state, "/work")
            os.rmdir(runtime)
            self.assertEqual({}, guest_bridge.load_registrations(os.path.join(state, "panes")))

    def test_junk_files_are_ignored(self):
        with tempfile.TemporaryDirectory() as root:
            state = make_state(root)
            panes_dir = os.path.join(state, "panes")
            Path(panes_dir, "notes.txt").write_text("hello")
            Path(panes_dir, "broken.json").write_text("{ not json")
            Path(panes_dir, "empty.json").write_text('{"token": "x"}')
            self.assertEqual({}, guest_bridge.load_registrations(panes_dir))


class PaneRouting(unittest.TestCase):
    def _pane(self, token: str, workspace: str, cwd: str, seen: float):
        return guest_bridge.PaneRegistration(token=token, runtime_dir="/tmp", helper="",
                                             python=sys.executable, workspace=workspace, cwd=cwd,
                                             seen=seen)

    def test_longest_prefix_wins(self):
        panes = {"outer": self._pane("outer", "/work", "/work", 1.0),
                 "inner": self._pane("inner", "/work/project", "/work/project", 1.0)}
        self.assertEqual("inner", guest_bridge.pane_for_path(panes, "/work/project/src/x.py").token)
        self.assertEqual("outer", guest_bridge.pane_for_path(panes, "/work/other/x.py").token)

    def test_cwd_counts_when_it_is_not_the_workspace(self):
        panes = {"a": self._pane("a", "/work", "/work/sub", 1.0)}
        self.assertEqual("a", guest_bridge.pane_for_path(panes, "/work/sub/deep/x.py").token)
        self.assertIsNone(guest_bridge.pane_for_path(panes, "/elsewhere/x.py"))

    def test_a_prefix_that_is_not_a_directory_boundary_does_not_match(self):
        panes = {"a": self._pane("a", "/work", "/work", 1.0)}
        self.assertIsNone(guest_bridge.pane_for_path(panes, "/workshop/x.py"))

    def test_the_newer_registration_breaks_a_tie(self):
        panes = {"old": self._pane("old", "/work", "/work", 1.0),
                 "new": self._pane("new", "/work", "/work", 2.0)}
        self.assertEqual("new", guest_bridge.pane_for_path(panes, "/work/x.py").token)

    def test_no_path_and_no_panes(self):
        self.assertIsNone(guest_bridge.pane_for_path({}, "/work/x.py"))
        panes = {"a": self._pane("a", "/work", "/work", 1.0)}
        self.assertIsNone(guest_bridge.pane_for_path(panes, ""))


class UnifiedDiff(unittest.TestCase):
    def test_change_headers_and_lines(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "x.py")
            Path(path).write_text("keep\nold\n")
            diff = guest_bridge.unified_diff(path, path, "keep\nnew\n", "keep\nold\n")
        self.assertIn("--- a/" + path, diff)
        self.assertIn("+++ b/" + path, diff)
        self.assertIn("-old", diff)
        self.assertIn("+new", diff)

    def test_a_new_file_reads_as_dev_null(self):
        diff = guest_bridge.unified_diff("/gone/new.py", "/gone/new.py", "hello\n", "")
        self.assertIn("--- /dev/null", diff)
        self.assertIn("+++ b//gone/new.py", diff)

    def test_no_change_is_an_empty_diff(self):
        self.assertEqual("", guest_bridge.unified_diff("/x", "/x", "same\n", "same\n"))


# ----- the JSON-RPC and tool layer, without a socket ----------------------------------------------


class JsonRpc(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.bridge, self.state, self.lock_dir = make_bridge(self.root)
        self.workspace = os.path.join(self.root, "project")
        os.makedirs(self.workspace, exist_ok=True)

    def _call(self, method, params=None, request_id=1):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        if request_id is not None:
            message["id"] = request_id
        return self.bridge.dispatch(message)

    def test_initialize_echoes_the_client_version(self):
        reply = self._call("initialize", {"protocolVersion": "2025-06-18"})[0]
        self.assertEqual("2025-06-18", reply["result"]["protocolVersion"])
        self.assertEqual("relay", reply["result"]["serverInfo"]["name"])
        self.assertEqual("9.9", reply["result"]["serverInfo"]["version"])
        self.assertTrue(reply["result"]["capabilities"]["tools"]["listChanged"])
        # No version offered: the bridge answers the one its docs name.
        self.assertEqual(guest_bridge.MCP_PROTOCOL_VERSION,
                         self._call("initialize", {})[0]["result"]["protocolVersion"])

    def test_tools_list_is_the_twelve_published_tools(self):
        tools = self._call("tools/list", {})[0]["result"]["tools"]
        self.assertEqual(["openFile", "openDiff", "getCurrentSelection", "getLatestSelection",
                          "getOpenEditors", "getWorkspaceFolders", "getDiagnostics",
                          "checkDocumentDirty", "saveDocument", "close_tab", "closeAllDiffTabs",
                          "executeCode"], [tool["name"] for tool in tools])
        for tool in tools:
            with self.subTest(tool=tool["name"]):
                self.assertEqual("object", tool["inputSchema"]["type"])
                self.assertTrue(tool["description"])

    def test_a_notification_gets_no_reply(self):
        self.assertIsNone(self._call("notifications/initialized", {}, request_id=None))
        self.assertIsNone(self._call("notifications/cancelled", {}, request_id=None))

    def test_unknown_method_and_unknown_tool(self):
        reply = self._call("tools/deleteEverything", {})[0]
        self.assertEqual(-32601, reply["error"]["code"])
        reply = self._call("tools/call", {"name": "rm_rf", "arguments": {}})[0]
        self.assertEqual(-32602, reply["error"]["code"])

    def test_bad_envelopes(self):
        self.assertEqual(-32600, self.bridge.dispatch({"method": "initialize"})[0]["error"]["code"])
        self.assertEqual(-32600, self.bridge.dispatch(["initialize"])[0]["error"]["code"])
        self.assertEqual(-32600, self.bridge.dispatch({"jsonrpc": "2.0"})[0]["error"]["code"])

    def test_empty_mcp_surfaces_do_not_break_a_client(self):
        self.assertEqual({"prompts": []}, self._call("prompts/list", {})[0]["result"])
        self.assertEqual({"resources": []}, self._call("resources/list", {})[0]["result"])
        self.assertEqual({}, self._call("ping", {})[0]["result"])


class Tools(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.bridge, self.state, self.lock_dir = make_bridge(self.root)
        self.workspace = os.path.join(self.root, "project")
        os.makedirs(self.workspace, exist_ok=True)
        self.token, self.runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()

    def _call(self, name, arguments=None):
        return self.bridge.dispatch({"jsonrpc": "2.0", "id": 5, "method": "tools/call",
                                     "params": {"name": name, "arguments": arguments or {}}})[0]

    def _text(self, reply) -> str:
        return reply["result"]["content"][0]["text"]

    def test_get_diagnostics_is_the_documented_empty_list(self):
        self.assertEqual("[]", self._text(self._call("getDiagnostics", {"uri": "file:///x"})))
        self.assertEqual("[]", self._text(self._call("getDiagnostics", {})))

    def test_workspace_folders_come_from_the_registered_panes(self):
        payload = json.loads(self._text(self._call("getWorkspaceFolders", {})))
        self.assertEqual([self.workspace], payload["folders"][0]["path"] and [payload["rootPath"]])
        self.assertTrue(payload["success"])

    def test_the_editor_tools_relay_does_not_have_say_so(self):
        selection = json.loads(self._text(self._call("getCurrentSelection", {})))
        self.assertFalse(selection["success"])
        self.assertEqual({"tabs": []}, json.loads(self._text(self._call("getOpenEditors", {}))))
        dirty = json.loads(self._text(self._call("checkDocumentDirty", {"filePath": "/x"})))
        self.assertFalse(dirty["success"])
        save = json.loads(self._text(self._call("saveDocument", {"filePath": "/x"})))
        self.assertFalse(save["success"])
        code = json.loads(self._text(self._call("executeCode", {"code": "1+1"})))
        self.assertFalse(code["success"])
        self.assertEqual("TAB_CLOSED", self._text(self._call("close_tab", {"tab_name": "x"})))
        self.assertEqual("CLOSED_0_DIFF_TABS", self._text(self._call("closeAllDiffTabs", {})))

    def test_open_file_reaches_its_pane(self):
        target = os.path.join(self.workspace, "src", "x.py")
        reply = self._call("openFile", {"filePath": target})
        self.assertEqual(f"Opened file: {target}", self._text(reply))
        event = read_guest_json(self.runtime)
        self.assertEqual("bridge", event["event"])
        self.assertEqual("claude", event["guest"])
        self.assertEqual(self.token, event["token"])
        self.assertEqual("openFile", event["data"]["tool"])
        self.assertEqual(target, event["data"]["filePath"])
        self.assertTrue(event["sequence"])

    def test_open_file_outside_every_pane_is_an_error(self):
        reply = self._call("openFile", {"filePath": "/somewhere/else/x.py"})
        self.assertTrue(reply["result"]["isError"])


class EventChannel(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.bridge, self.state, self.lock_dir = make_bridge(self.root)
        self.workspace = os.path.join(self.root, "project")
        os.makedirs(self.workspace, exist_ok=True)

    def test_the_sequence_changes_on_every_event(self):
        _, runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        self.bridge.emit(pane, "openFile", {"filePath": "/x"})
        first = read_guest_json(runtime)["sequence"]
        self.bridge.emit(pane, "openFile", {"filePath": "/y"})
        self.assertNotEqual(first, read_guest_json(runtime)["sequence"])

    def test_the_helper_is_the_writer_when_it_exists(self):
        """The fallback writer must never be the one that wrote the file once the hooks phase's
        shell/guest-event.py is there: the fake helper tags what it wrote."""
        helper = os.path.join(self.root, "guest-event.py")
        Path(helper).write_text(
            "import json, os, sys\n"
            "event = json.load(sys.stdin)\n"
            "event['via'] = 'helper'\n"
            "path = os.path.join(os.environ['RELAY_RUNTIME_DIR'], 'guest.json')\n"
            "open(path + '.tmp', 'w').write(json.dumps(event))\n"
            "os.replace(path + '.tmp', path)\n")
        _, runtime = register_pane(self.state, self.workspace, helper=helper)
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        self.assertTrue(self.bridge.emit(pane, "openFile", {"filePath": "/x"}))
        self.assertEqual("helper", read_guest_json(runtime)["via"])

    def test_a_failing_helper_falls_back_to_the_identical_bytes(self):
        helper = os.path.join(self.root, "guest-event.py")
        Path(helper).write_text("import sys; sys.exit(3)\n")
        _, runtime = register_pane(self.state, self.workspace, helper=helper)
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        self.assertTrue(self.bridge.emit(pane, "openFile", {"filePath": "/x"}))
        event = read_guest_json(runtime)
        self.assertEqual("bridge", event["event"])
        self.assertNotIn("via", event)

    def test_a_vanished_runtime_dir_is_a_logged_failure_not_a_crash(self):
        _, runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        os.rmdir(runtime)
        self.assertFalse(self.bridge.emit(pane, "openFile", {"filePath": "/x"}))

    def test_the_lock_tracks_the_registered_folders(self):
        self.bridge.lock.write([])
        register_pane(self.state, self.workspace)
        self.assertTrue(self.bridge.refresh_registrations())
        self.assertEqual([self.workspace], guest_bridge.read_json(self.bridge.lock.path)["workspaceFolders"])


class OpenDiff(unittest.IsolatedAsyncioTestCase):
    """The blocking tool, without a socket: dispatch, the event, the reply file, the outcome."""

    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.bridge, self.state, self.lock_dir = make_bridge(self.root)
        self.workspace = os.path.join(self.root, "project")
        os.makedirs(self.workspace, exist_ok=True)
        self.target = os.path.join(self.workspace, "x.py")
        Path(self.target).write_text("keep\nold\n")
        self.token, self.runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()

    def _call(self, arguments):
        return self.bridge.dispatch({"jsonrpc": "2.0", "id": 7, "method": "tools/call",
                                     "params": {"name": "openDiff", "arguments": arguments}})[0]

    def _arguments(self, contents="keep\nnew\n"):
        return {"old_file_path": self.target, "new_file_path": self.target,
                "new_file_contents": contents, "tab_name": "Proposed changes"}

    async def test_the_call_waits_for_the_reply_file(self):
        deferred = self._call(self._arguments())
        self.assertIsInstance(deferred, guest_bridge.Deferred)
        event = read_guest_json(self.runtime)
        self.assertEqual("openDiff", event["data"]["tool"])
        self.assertEqual("Proposed changes", event["data"]["tab_name"])
        self.assertIn("-old", event["data"]["diff"])
        self.assertIn("+new", event["data"]["diff"])
        self.assertEqual(self.target, event["data"]["file"])
        reply_path = event["data"]["reply"]
        self.assertTrue(os.path.isabs(reply_path))
        self.assertFalse(deferred.pending.future.done())

        guest_bridge.write_json_atomic(reply_path, {"outcome": guest_bridge.FILE_SAVED})
        self.bridge.check_replies()
        self.assertTrue(deferred.pending.future.done())
        self.assertEqual(guest_bridge.FILE_SAVED, deferred.pending.future.result())
        self.assertFalse(os.path.exists(reply_path))   # consumed
        self.assertNotIn(reply_path, self.bridge.pending)

    async def test_rejection_settles_the_same_way(self):
        deferred = self._call(self._arguments())
        reply_path = read_guest_json(self.runtime)["data"]["reply"]
        guest_bridge.write_json_atomic(reply_path, {"outcome": guest_bridge.DIFF_REJECTED})
        self.bridge.check_replies()
        self.assertEqual(guest_bridge.DIFF_REJECTED, deferred.pending.future.result())

    async def test_an_unmatched_diff_is_rejected_at_once(self):
        reply = self._call({"old_file_path": "/elsewhere/x.py", "new_file_path": "/elsewhere/x.py",
                            "new_file_contents": "x\n"})
        self.assertIsInstance(reply, dict)
        self.assertEqual(guest_bridge.DIFF_REJECTED, reply["result"]["content"][0]["text"])

    async def test_close_all_diff_tabs_rejects_what_waits(self):
        deferred = self._call(self._arguments())
        reply = self.bridge.dispatch({"jsonrpc": "2.0", "id": 8, "method": "tools/call",
                                      "params": {"name": "closeAllDiffTabs", "arguments": {}}})[0]
        self.assertEqual("CLOSED_1_DIFF_TABS", reply["result"]["content"][0]["text"])
        self.assertEqual(guest_bridge.DIFF_REJECTED, deferred.pending.future.result())

    async def test_a_pane_that_closes_rejects_its_waiting_diff(self):
        deferred = self._call(self._arguments())
        self.bridge.abandon_pane(self.token)
        self.assertEqual(guest_bridge.DIFF_REJECTED, deferred.pending.future.result())

    async def test_a_closed_pane_is_noticed_by_the_registration_poll(self):
        """A pane that closes takes its registration and its runtime dir with it; the poll's
        refresh drops the token, and the tick rejects whatever that pane was waiting on."""
        deferred = self._call(self._arguments())
        os.unlink(os.path.join(self.state, "panes", f"{self.token}.json"))
        shutil.rmtree(self.runtime)
        self.bridge.refresh_registrations()
        self.assertNotIn(self.token, self.bridge.panes)
        self.bridge.abandon_pane(self.token)
        self.assertEqual(guest_bridge.DIFF_REJECTED, deferred.pending.future.result())


# ----- the same layer over a real WebSocket, with a hand-rolled claude ----------------------------


def client_frame(payload: bytes, opcode: int = 0x1, fin: bool = True) -> bytes:
    """A masked client frame, RFC 6455: what a browser (or claude's ws client) sends. `fin`
    false starts (or continues) a fragmented message."""
    mask = os.urandom(4)
    masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    header = bytes([(0x80 if fin else 0x00) | opcode])
    length = len(payload)
    if length < 126:
        header += bytes([0x80 | length])
    elif length < 65536:
        header += bytes([0x80 | 126]) + struct.pack(">H", length)
    else:
        header += bytes([0x80 | 127]) + struct.pack(">Q", length)
    return header + mask + masked


async def server_message(reader) -> tuple[int, bytes]:
    head = await reader.readexactly(2)
    opcode, length = head[0] & 0x0F, head[1] & 0x7F
    if length == 126:
        length = struct.unpack(">H", await reader.readexactly(2))[0]
    elif length == 127:
        length = struct.unpack(">Q", await reader.readexactly(8))[0]
    payload = await reader.readexactly(length) if length else b""
    return opcode, payload


async def next_text(reader) -> dict:
    """The next data frame, skipping the keepalive pings the server sends."""
    while True:
        opcode, payload = await server_message(reader)
        if opcode == 0x1:
            return json.loads(payload.decode())


async def handshake(reader, writer, port: int, token: str) -> str:
    key = secrets.token_urlsafe(16)
    writer.write(("GET / HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\n"
                  "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
                  "x-claude-code-ide-authorization: %s\r\n\r\n" % (port, key, token)).encode())
    await writer.drain()
    head = await reader.readuntil(b"\r\n\r\n")
    return head.decode("latin-1")


class WebSocketEndToEnd(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.root = tempfile.mkdtemp()
        self.bridge, self.state, self.lock_dir = make_bridge(self.root)
        self.workspace = os.path.join(self.root, "project")
        os.makedirs(self.workspace, exist_ok=True)
        self.target = os.path.join(self.workspace, "x.py")
        Path(self.target).write_text("keep\nold\n")
        self.token, self.runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()
        self.server = guest_bridge.BridgeServer(self.bridge)
        self.port = await self.server.start()
        self.reader, self.writer = await asyncio.open_connection("127.0.0.1", self.port)
        response = await handshake(self.reader, self.writer, self.port, self.bridge.lock.token)
        self.assertIn("101", response.splitlines()[0])

    async def asyncTearDown(self):
        self.writer.close()
        for task in self.server._tasks:
            task.cancel()
        if self.server.server:
            self.server.server.close()

    async def _send(self, message: dict) -> None:
        self.writer.write(client_frame(json.dumps(message).encode()))
        await self.writer.drain()

    async def test_initialize_list_and_an_ordinary_tool(self):
        await self._send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                          "params": {"protocolVersion": "2025-03-26"}})
        reply = await next_text(self.reader)
        self.assertEqual(1, reply["id"])
        self.assertEqual("relay", reply["result"]["serverInfo"]["name"])

        await self._send({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
        reply = await next_text(self.reader)
        self.assertEqual(12, len(reply["result"]["tools"]))

        await self._send({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
                          "params": {"name": "getDiagnostics", "arguments": {}}})
        reply = await next_text(self.reader)
        self.assertEqual("[]", reply["result"]["content"][0]["text"])

        await self._send({"jsonrpc": "2.0", "id": 4, "method": "tools/call",
                          "params": {"name": "openFile", "arguments": {"filePath": self.target}}})
        reply = await next_text(self.reader)
        self.assertIn("Opened file", reply["result"]["content"][0]["text"])
        self.assertEqual(self.target, read_guest_json(self.runtime)["data"]["filePath"])

    async def test_open_diff_saves_the_file_and_returns_file_saved(self):
        await self._send({"jsonrpc": "2.0", "id": 9, "method": "tools/call",
                          "params": {"name": "openDiff", "arguments": {
                              "old_file_path": self.target, "new_file_path": self.target,
                              "new_file_contents": "keep\nnew\n", "tab_name": "Proposed changes"}}})
        # The server is waiting on the pane now; the pane is the GUI, and this test is it.
        event = await self._wait_for_event()
        self.assertIn("+new", event["data"]["diff"])
        guest_bridge.write_json_atomic(event["data"]["reply"], {"outcome": guest_bridge.FILE_SAVED})
        reply = await next_text(self.reader)
        self.assertEqual(9, reply["id"])
        self.assertEqual(guest_bridge.FILE_SAVED, reply["result"]["content"][0]["text"])
        with open(self.target, "r", encoding="utf-8") as stream:
            self.assertEqual("keep\nnew\n", stream.read())   # the bridge wrote it, not the GUI

    async def test_open_diff_rejection_leaves_the_file_alone(self):
        await self._send({"jsonrpc": "2.0", "id": 10, "method": "tools/call",
                          "params": {"name": "openDiff", "arguments": {
                              "old_file_path": self.target, "new_file_path": self.target,
                              "new_file_contents": "keep\nchanged\n"}}})
        event = await self._wait_for_event()
        guest_bridge.write_json_atomic(event["data"]["reply"], {"outcome": guest_bridge.DIFF_REJECTED})
        reply = await next_text(self.reader)
        self.assertEqual(guest_bridge.DIFF_REJECTED, reply["result"]["content"][0]["text"])
        with open(self.target, "r", encoding="utf-8") as stream:
            self.assertEqual("keep\nold\n", stream.read())

    async def _wait_for_event(self, timeout: float = 5.0) -> dict:
        """guest.json as the pane's poll would read it: written by the bridge's own tick."""
        deadline = asyncio.get_running_loop().time() + timeout
        while asyncio.get_running_loop().time() < deadline:
            try:
                event = read_guest_json(self.runtime)
                if event.get("data", {}).get("tool") == "openDiff" and event["data"].get("reply"):
                    return event
            except (OSError, ValueError):
                pass
            await asyncio.sleep(0.02)
        self.fail("the bridge never wrote the openDiff event")

    async def test_a_wrong_auth_token_is_refused(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        response = await handshake(reader, writer, self.port, "0" * 32)
        self.assertIn("401", response.splitlines()[0])
        writer.close()

    async def test_a_plain_http_request_is_refused(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        writer.write(b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
        await writer.drain()
        head = await reader.readuntil(b"\r\n\r\n")
        self.assertIn("400", head.decode("latin-1").splitlines()[0])
        writer.close()

    async def test_malformed_json_gets_a_parse_error_and_the_socket_lives(self):
        self.writer.write(client_frame(b"{ not json"))
        await self.writer.drain()
        reply = await next_text(self.reader)
        self.assertEqual(-32700, reply["error"]["code"])
        await self._send({"jsonrpc": "2.0", "id": 11, "method": "ping", "params": {}})
        self.assertEqual(11, (await next_text(self.reader))["id"])

    async def test_a_fragmented_message_is_reassembled(self):
        payload = json.dumps({"jsonrpc": "2.0", "id": 12, "method": "tools/list", "params": {}}).encode()
        half = len(payload) // 2
        self.writer.write(client_frame(payload[:half], opcode=0x1, fin=False))
        self.writer.write(client_frame(payload[half:], opcode=0x0))   # continuation, FIN
        await self.writer.drain()
        reply = await next_text(self.reader)
        self.assertEqual(12, reply["id"])
        self.assertEqual(12, len(reply["result"]["tools"]))

    async def test_a_ping_is_answered_with_a_pong(self):
        self.writer.write(client_frame(b"are you there", opcode=0x9))
        await self.writer.drain()
        opcode, payload = await server_message(self.reader)
        self.assertEqual(0xA, opcode)
        self.assertEqual(b"are you there", payload)


# ----- the sidecar as a process: ready line, lock, SIGTERM ----------------------------------------


class ProcessLifecycle(unittest.TestCase):
    def _start(self, state: str, lock_dir: str, timeout: float = 20.0) -> subprocess.Popen:
        environment = dict(os.environ)
        environment["PYTHONPATH"] = str(BACKEND)
        process = subprocess.Popen([sys.executable, "-u", "-m", "relay_core.guest_bridge", "serve",
                                    "--state-dir", state, "--lock-dir", lock_dir],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   env=environment)
        self.addCleanup(self._kill, process)
        return process

    def _kill(self, process: subprocess.Popen) -> None:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()

    def test_ready_line_lock_and_removal_on_sigterm(self):
        with tempfile.TemporaryDirectory() as root:
            state = make_state(root)
            lock_dir = os.path.join(root, "ide")
            os.makedirs(lock_dir)
            stale = os.path.join(lock_dir, "1.lock")
            Path(stale).write_text(json.dumps({"pid": 0x7FFFFFFE, "ideName": "VS Code"}))
            live = os.path.join(lock_dir, "2.lock")
            Path(live).write_text(json.dumps({"pid": os.getpid(), "ideName": "Neovim"}))
            register_pane(state, os.path.join(root, "project"))

            process = self._start(state, lock_dir)
            ready = json.loads(process.stdout.readline())
            self.assertTrue(ready["ready"], ready)
            self.assertGreater(ready["port"], 0)
            self.assertEqual(os.path.join(lock_dir, f"{ready['port']}.lock"), ready["lock"])

            payload = guest_bridge.read_json(ready["lock"])
            self.assertEqual(process.pid, payload["pid"], "the lock names the sidecar's own pid")
            self.assertEqual("relay", payload["ideName"])
            self.assertEqual("ws", payload["transport"])
            self.assertEqual(32, len(payload["authToken"]))
            self.assertEqual([os.path.join(root, "project")], payload["workspaceFolders"])
            self.assertFalse(os.path.exists(stale), "a dead IDE's lock is swept at startup")
            self.assertTrue(os.path.exists(live), "a live editor's lock is left alone")

            process.terminate()
            process.wait(timeout=5)
            self.assertFalse(os.path.exists(ready["lock"]), "SIGTERM removes the lock")

    def test_a_second_run_reuses_no_port_and_leaves_one_lock(self):
        with tempfile.TemporaryDirectory() as root:
            state = make_state(root)
            lock_dir = os.path.join(root, "ide")
            os.makedirs(lock_dir)
            first = self._start(state, lock_dir)
            first_ready = json.loads(first.stdout.readline())
            second = self._start(state, lock_dir)
            second_ready = json.loads(second.stdout.readline())
            self.assertNotEqual(first_ready["port"], second_ready["port"])
            first.terminate()
            first.wait(timeout=5)
            self.assertTrue(os.path.exists(second_ready["lock"]))
            self.assertFalse(os.path.exists(first_ready["lock"]))


if __name__ == "__main__":
    unittest.main()
