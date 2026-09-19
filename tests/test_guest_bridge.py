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
HELPER = Path(__file__).resolve().parents[1] / "shell" / "guest-event.py"


# ----- fixtures ---------------------------------------------------------------------------------


def make_state(root: str) -> str:
    state = os.path.join(root, "state")
    os.makedirs(os.path.join(state, "panes"), exist_ok=True)
    return state


def register_pane(state: str, workspace: str, cwd: str | None = None, helper: str | None = None,
                  runtime: str | None = None) -> tuple[str, str]:
    """One pane registration, the file the GUI writes: token, runtime dir, helper, workspace.
    The default helper is the real shell/guest-event.py, exactly as the GUI registers it — the
    channel's writer is the contract these tests exercise, not a stand-in for it."""
    runtime = runtime or tempfile.mkdtemp(dir=state, prefix="runtime-")
    token = secrets.token_hex(8)
    guest_bridge.write_json_atomic(os.path.join(state, "panes", f"{token}.json"),
                                   {"token": token, "runtime_dir": runtime,
                                    "helper": str(HELPER) if helper is None else helper,
                                    "python": sys.executable, "workspace": workspace,
                                    "cwd": cwd or workspace})
    return token, runtime


def make_bridge(root: str, relay_version: str = "9.9",
                port: int = 45999) -> tuple[guest_bridge.Bridge, str, str]:
    """A bridge with a lock that already knows its port: the sidecar writes no lock before the
    listener is bound, so a fixture at port 0 would be a bridge whose lock never appears."""
    state = make_state(root)
    lock_dir = os.path.join(root, "ide")
    os.makedirs(lock_dir, exist_ok=True)
    lock = guest_bridge.LockFile(directory=lock_dir, port=port, pid=os.getpid(),
                                 token=secrets.token_hex(16))
    bridge = guest_bridge.Bridge(lock, state, relay_version)
    bridge.refresh_registrations()
    return bridge, state, lock_dir


def spool_events(runtime: str) -> list[dict]:
    """Every envelope on the pane's event spool, oldest first (26.3).

    The channel is a directory of one file per event since the review of 51587e3 — the single
    `guest.json` slot it replaced lost the first of two events written back to back. Nothing
    deletes the files here (the pane does that), so a test that emits twice sees both.
    """
    events = os.path.join(runtime, "guest-events")
    try:
        names = sorted(name for name in os.listdir(events) if name.endswith(".json"))
    except OSError:
        return []
    envelopes = []
    for name in names:
        try:
            with open(os.path.join(events, name), "r", encoding="utf-8") as stream:
                envelopes.append(json.load(stream))
        except (OSError, ValueError):
            pass
    return envelopes


def read_guest_json(runtime: str) -> dict:
    """The last event on the spool. Raises like the old single-slot read when there is none, so
    a caller that polls can keep catching OSError."""
    events = spool_events(runtime)
    if not events:
        raise FileNotFoundError(os.path.join(runtime, "guest-events"))
    return events[-1]


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

    def test_no_lock_is_written_before_the_port_is_known(self):
        """`0.lock` would publish a live authToken at a port nobody listens on, and `remove()`
        — which only ever unlinks the real port's name — could never clean it up."""
        with tempfile.TemporaryDirectory() as root:
            lock = guest_bridge.LockFile(directory=root, port=0, pid=os.getpid(), token="cd" * 16)
            self.assertFalse(lock.write(["/a"]))
            self.assertEqual([], os.listdir(root))
            lock.port = 41000
            self.assertTrue(lock.write(["/a"]))
            self.assertEqual(["41000.lock"], os.listdir(root))


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


class ResolveInPane(unittest.TestCase):
    """The path rule: a path the bridge may write is one that resolves inside the pane that owns
    the request. Everything else — another pane's file, `..`, a symlink out — is None."""

    def setUp(self):
        self.root = os.path.realpath(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.workspace = os.path.join(self.root, "project")
        self.outside = os.path.join(self.root, "outside")
        os.makedirs(self.workspace)
        os.makedirs(self.outside)
        self.panes = {"a": guest_bridge.PaneRegistration(
            token="a", runtime_dir="/tmp", helper="", python=sys.executable,
            workspace=self.workspace, cwd=self.workspace, seen=1.0)}

    def test_a_path_inside_the_pane_resolves(self):
        target = os.path.join(self.workspace, "src", "x.py")
        self.assertEqual(target, guest_bridge.resolve_in_pane(self.panes, "a", target))

    def test_a_path_outside_the_pane_is_none(self):
        self.assertIsNone(guest_bridge.resolve_in_pane(
            self.panes, "a", os.path.join(self.outside, "authorized_keys")))

    def test_a_dotdot_escape_is_none(self):
        self.assertIsNone(guest_bridge.resolve_in_pane(
            self.panes, "a", os.path.join(self.workspace, "..", "outside", "x")))

    def test_a_symlink_pointing_out_of_the_pane_is_none(self):
        link = os.path.join(self.workspace, "innocent.txt")
        os.symlink(os.path.join(self.outside, "secret"), link)
        self.assertIsNone(guest_bridge.resolve_in_pane(self.panes, "a", link))

    def test_another_panes_file_is_none(self):
        other = os.path.join(self.root, "other")
        os.makedirs(other)
        self.panes["b"] = guest_bridge.PaneRegistration(
            token="b", runtime_dir="/tmp", helper="", python=sys.executable,
            workspace=other, cwd=other, seen=1.0)
        self.assertIsNone(guest_bridge.resolve_in_pane(self.panes, "a", os.path.join(other, "x")))
        self.assertEqual(os.path.join(other, "x"),
                         guest_bridge.resolve_in_pane(self.panes, "b", os.path.join(other, "x")))

    def test_an_empty_path_or_token_is_none(self):
        self.assertIsNone(guest_bridge.resolve_in_pane(self.panes, "a", ""))
        self.assertIsNone(guest_bridge.resolve_in_pane(self.panes, "", self.workspace))


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

    def test_a_notification_method_sent_as_a_request_gets_an_empty_result(self):
        """JSON-RPC has no reply whose whole body is `null`; a client that puts an id on
        logging/setLevel has asked a question and gets an answer."""
        for method in ("notifications/initialized", "notifications/cancelled", "logging/setLevel"):
            with self.subTest(method=method):
                replies = self._call(method, {}, request_id=42)
                self.assertEqual([{"jsonrpc": "2.0", "id": 42, "result": {}}], replies)

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

    def test_the_real_helper_writes_the_263_envelope(self):
        """shell/guest-event.py is the channel's one writer, for the bridge as for the shim: the
        envelope the pane reads is the one it builds — token from the environment, event and
        guest from the sidecar's argv, the event's data from its stdin."""
        _, runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        self.assertTrue(self.bridge.emit(pane, "openFile", {"filePath": "/x"}))
        event = read_guest_json(runtime)
        self.assertEqual("bridge", event["event"])
        self.assertEqual("claude", event["guest"])
        self.assertEqual(pane.token, event["token"])
        self.assertTrue(event["sequence"])
        self.assertEqual("openFile", event["data"]["tool"])
        self.assertEqual("/x", event["data"]["filePath"])

    def test_a_failing_helper_is_a_failed_emit(self):
        """No second writer exists: a helper that fails means the event is simply not sent."""
        helper = os.path.join(self.root, "guest-event.py")
        Path(helper).write_text("import sys; sys.exit(3)\n")
        _, runtime = register_pane(self.state, self.workspace, helper=helper)
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        self.assertFalse(self.bridge.emit(pane, "openFile", {"filePath": "/x"}))
        self.assertEqual([], spool_events(runtime))

    def test_a_missing_helper_is_a_failed_emit(self):
        _, runtime = register_pane(self.state, self.workspace, helper="")
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        self.assertFalse(self.bridge.emit(pane, "openFile", {"filePath": "/x"}))
        self.assertEqual([], spool_events(runtime))

    def test_a_vanished_runtime_dir_is_a_logged_failure_not_a_crash(self):
        _, runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        os.rmdir(runtime)
        self.assertFalse(self.bridge.emit(pane, "openFile", {"filePath": "/x"}))

    def test_the_helper_gets_a_minimal_environment(self):
        """The sidecar inherits the GUI's whole environment, provider keys included. The helper
        that runs once per bridge event gets six variables and no more."""
        helper = os.path.join(self.root, "guest-event.py")
        Path(helper).write_text(
            "import json, os, sys, time\n"
            "event = json.load(sys.stdin)\n"
            "event['env'] = dict(os.environ)\n"
            "spool = os.path.join(os.environ['RELAY_RUNTIME_DIR'], 'guest-events')\n"
            "os.makedirs(spool, exist_ok=True)\n"
            "path = os.path.join(spool, '%020d.json' % time.time_ns())\n"
            "open(path + '.tmp', 'w').write(json.dumps(event))\n"
            "os.replace(path + '.tmp', path)\n")
        token, runtime = register_pane(self.state, self.workspace, helper=helper)
        self.bridge.refresh_registrations()
        pane = next(iter(self.bridge.panes.values()))
        os.environ["RELAY_TEST_SECRET_KEY"] = "sk-do-not-leak"
        self.addCleanup(os.environ.pop, "RELAY_TEST_SECRET_KEY", None)
        self.assertTrue(self.bridge.emit(pane, "openFile", {"filePath": "/x"}))
        environment = read_guest_json(runtime)["env"]
        self.assertNotIn("RELAY_TEST_SECRET_KEY", environment)
        self.assertEqual(runtime, environment["RELAY_RUNTIME_DIR"])
        self.assertEqual(token, environment["RELAY_SESSION_TOKEN"])
        self.assertEqual(sys.executable, environment["RELAY_PYTHON"])
        self.assertLessEqual(set(environment),
                             set(guest_bridge.HELPER_ENV_PASSTHROUGH)
                             | {"RELAY_RUNTIME_DIR", "RELAY_SESSION_TOKEN", "RELAY_PYTHON"})

    def test_the_lock_tracks_the_registered_folders(self):
        self.bridge.lock.write([])
        register_pane(self.state, self.workspace)
        self.assertTrue(self.bridge.refresh_registrations())
        self.assertEqual([self.workspace], guest_bridge.read_json(self.bridge.lock.path)["workspaceFolders"])


class OpenDiff(unittest.IsolatedAsyncioTestCase):
    """The blocking tool, without a socket: dispatch, the event, the reply file, the outcome."""

    def setUp(self):
        self.root = os.path.realpath(tempfile.mkdtemp())
        self.bridge, self.state, self.lock_dir = make_bridge(self.root)
        self.workspace = os.path.join(self.root, "project")
        os.makedirs(self.workspace, exist_ok=True)
        self.outside = os.path.join(self.root, "outside")
        os.makedirs(self.outside, exist_ok=True)
        self.target = os.path.join(self.workspace, "x.py")
        Path(self.target).write_text("keep\nold\n")
        self.token, self.runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()

    def _call(self, arguments, connection=None):
        return self.bridge.dispatch({"jsonrpc": "2.0", "id": 7, "method": "tools/call",
                                     "params": {"name": "openDiff", "arguments": arguments}},
                                    connection=connection)[0]

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

    # ----- the path rule (A1): the pane that shows the diff is the pane that gets written --------

    def _no_diff_was_opened(self, reply) -> None:
        self.assertIsInstance(reply, dict)
        self.assertEqual(guest_bridge.DIFF_REJECTED, reply["result"]["content"][0]["text"])
        self.assertEqual({}, self.bridge.pending)
        self.assertEqual([], spool_events(self.runtime),
                         "a refused diff is never shown to the user either")

    async def test_a_new_file_path_outside_the_pane_is_refused(self):
        """The live exploit: route on an old_file_path in the workspace, write somewhere else."""
        victim = os.path.join(self.outside, "authorized_keys")
        Path(victim).write_text("original\n")
        self._no_diff_was_opened(self._call(
            {"old_file_path": self.target, "new_file_path": victim,
             "new_file_contents": "ssh-rsa AAAA...\n"}))
        self.assertEqual("original\n", Path(victim).read_text())

    async def test_a_dotdot_new_file_path_is_refused(self):
        self._no_diff_was_opened(self._call(
            {"old_file_path": self.target,
             "new_file_path": os.path.join(self.workspace, "..", "outside", "escaped"),
             "new_file_contents": "x\n"}))

    async def test_a_symlink_out_of_the_workspace_is_refused(self):
        victim = os.path.join(self.outside, "secret")
        Path(victim).write_text("original\n")
        link = os.path.join(self.workspace, "innocent.txt")
        os.symlink(victim, link)
        self._no_diff_was_opened(self._call(
            {"old_file_path": self.target, "new_file_path": link, "new_file_contents": "owned\n"}))
        self.assertEqual("original\n", Path(victim).read_text())

    async def test_an_old_file_path_outside_the_pane_is_refused_too(self):
        """The new path is the one that gets written, but the old one is the file whose contents
        the diff shows; both must be the pane's."""
        outside = os.path.join(self.outside, "elsewhere.py")
        Path(outside).write_text("secret\n")
        self._no_diff_was_opened(self._call(
            {"old_file_path": outside, "new_file_path": self.target, "new_file_contents": "x\n"}))

    async def test_the_event_carries_the_resolved_paths(self):
        """What the pane shows the user is the file that would actually be written."""
        os.makedirs(os.path.join(self.workspace, "sub"), exist_ok=True)
        winding = os.path.join(self.workspace, "sub", "..", "x.py")
        self._call({"old_file_path": winding, "new_file_path": winding, "new_file_contents": "z\n"})
        event = read_guest_json(self.runtime)
        self.assertEqual(self.target, event["data"]["new_file_path"])
        self.assertEqual(self.target, event["data"]["old_file_path"])
        self.assertEqual(self.target, event["data"]["file"])

    # ----- who owns a pending diff (A2, A5) -------------------------------------------------------

    async def test_close_tab_settles_the_diff_of_that_name(self):
        deferred = self._call(self._arguments())
        reply = self.bridge.dispatch({"jsonrpc": "2.0", "id": 8, "method": "tools/call",
                                      "params": {"name": "close_tab",
                                                 "arguments": {"tab_name": "Proposed changes"}}})[0]
        self.assertEqual("TAB_CLOSED", reply["result"]["content"][0]["text"])
        self.assertEqual(guest_bridge.DIFF_REJECTED, deferred.pending.future.result())

    async def test_close_tab_of_another_name_leaves_the_diff_alone(self):
        deferred = self._call(self._arguments())
        self.bridge.dispatch({"jsonrpc": "2.0", "id": 8, "method": "tools/call",
                              "params": {"name": "close_tab", "arguments": {"tab_name": "other"}}})
        self.assertFalse(deferred.pending.future.done())
        self.bridge.dispatch({"jsonrpc": "2.0", "id": 9, "method": "tools/call",
                              "params": {"name": "close_tab", "arguments": {}}})
        self.assertFalse(deferred.pending.future.done(), "a nameless close_tab closes nothing")

    async def test_close_all_diff_tabs_spares_another_connections_diff(self):
        mine, theirs = object(), object()
        ours = self._call(self._arguments(), connection=mine)
        other = self._call({"old_file_path": self.target, "new_file_path": self.target,
                            "new_file_contents": "keep\nother\n", "tab_name": "Theirs"},
                           connection=theirs)
        reply = self.bridge.dispatch({"jsonrpc": "2.0", "id": 8, "method": "tools/call",
                                      "params": {"name": "closeAllDiffTabs", "arguments": {}}},
                                     connection=mine)[0]
        self.assertEqual("CLOSED_1_DIFF_TABS", reply["result"]["content"][0]["text"])
        self.assertEqual(guest_bridge.DIFF_REJECTED, ours.pending.future.result())
        self.assertFalse(other.pending.future.done())

    async def test_a_connection_that_drops_settles_its_own_diffs_only(self):
        mine, theirs = object(), object()
        ours = self._call(self._arguments(), connection=mine)
        other = self._call(self._arguments("keep\nother\n"), connection=theirs)
        self.bridge.abandon_connection(mine)
        self.assertEqual(guest_bridge.DIFF_REJECTED, ours.pending.future.result())
        self.assertFalse(other.pending.future.done())

    async def test_a_diff_nobody_answers_expires(self):
        deferred = self._call(self._arguments())
        self.assertEqual(0, self.bridge.expire_pending())
        self.assertFalse(deferred.pending.future.done())
        self.assertEqual(1, self.bridge.expire_pending(deferred.pending.deadline + 1.0))
        self.assertEqual(guest_bridge.DIFF_REJECTED, deferred.pending.future.result())
        self.assertEqual({}, self.bridge.pending)

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
        self.root = os.path.realpath(tempfile.mkdtemp())
        self.bridge, self.state, self.lock_dir = make_bridge(self.root)
        self.workspace = os.path.join(self.root, "project")
        os.makedirs(self.workspace, exist_ok=True)
        self.outside = os.path.join(self.root, "outside")
        os.makedirs(self.outside, exist_ok=True)
        self.target = os.path.join(self.workspace, "x.py")
        Path(self.target).write_text("keep\nold\n")
        self.token, self.runtime = register_pane(self.state, self.workspace)
        self.bridge.refresh_registrations()
        self.server = guest_bridge.BridgeServer(self.bridge)
        self.port = await self.server.start()
        self._writers = []
        self.reader, self.writer = await self._connect()

    async def asyncTearDown(self):
        for writer in self._writers:
            writer.close()
        await asyncio.sleep(0.05)   # let the server notice, settle and finish its tasks
        for task in self.server._tasks:
            task.cancel()
        if self.server.server:
            self.server.server.close()

    async def _connect(self):
        """A second (or third) claude on the same bridge, handshake done."""
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self._writers.append(writer)
        response = await handshake(reader, writer, self.port, self.bridge.lock.token)
        self.assertIn("101", response.splitlines()[0])
        return reader, writer

    def _patch(self, name: str, value) -> None:
        """A module constant for the length of one test; the bridge reads them at use time."""
        original = getattr(guest_bridge, name)
        setattr(guest_bridge, name, value)
        self.addCleanup(setattr, guest_bridge, name, original)

    async def _send(self, message: dict, writer=None) -> None:
        writer = writer or self.writer
        writer.write(client_frame(json.dumps(message).encode()))
        await writer.drain()

    async def _replies(self, count: int, reader=None, timeout: float = 5.0) -> dict:
        """The next `count` JSON-RPC replies, keyed by id — the order two answers arrive in is
        not the bridge's promise, only that both do."""
        reader = reader or self.reader
        out = {}
        for _ in range(count):
            message = await asyncio.wait_for(next_text(reader), timeout)
            out[message.get("id")] = message
        return out

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

    async def _wait_for_event(self, timeout: float = 5.0, seen: set | None = None) -> dict:
        """The spool as the pane's poll would read it: written by the bridge's own tick. The
        pane deletes each file as it handles it and nothing does that here, so every event of the
        test is still on the spool; `seen` skips the reply paths this test has already
        collected."""
        seen = seen or set()
        deadline = asyncio.get_running_loop().time() + timeout
        while asyncio.get_running_loop().time() < deadline:
            for event in spool_events(self.runtime):
                reply = event.get("data", {}).get("reply")
                if event.get("data", {}).get("tool") == "openDiff" and reply and reply not in seen:
                    return event
            await asyncio.sleep(0.02)
        self.fail("the bridge never wrote the openDiff event")

    # ----- a pending diff must not gag the connection (A2) ----------------------------------------

    async def _open_a_diff(self, request_id: int, tab_name: str = "Proposed changes",
                           writer=None, seen: set | None = None) -> dict:
        await self._send({"jsonrpc": "2.0", "id": request_id, "method": "tools/call",
                          "params": {"name": "openDiff", "arguments": {
                              "old_file_path": self.target, "new_file_path": self.target,
                              "new_file_contents": "keep\nnew\n", "tab_name": tab_name}}}, writer)
        return await self._wait_for_event(seen=seen)

    async def test_the_connection_is_still_served_while_a_diff_waits(self):
        """The read loop used to await the pending future inline, so nothing else on the socket
        was read until the user decided: no tools/list, no pong — and no way in."""
        event = await self._open_a_diff(20)
        await self._send({"jsonrpc": "2.0", "id": 21, "method": "tools/list", "params": {}})
        reply = await asyncio.wait_for(next_text(self.reader), 5.0)
        self.assertEqual(21, reply["id"])
        self.assertEqual(12, len(reply["result"]["tools"]))

        self.writer.write(client_frame(b"still there", opcode=0x9))
        await self.writer.drain()
        opcode, payload = await asyncio.wait_for(server_message(self.reader), 5.0)
        self.assertEqual(0xA, opcode)
        self.assertEqual(b"still there", payload)

        guest_bridge.write_json_atomic(event["data"]["reply"], {"outcome": guest_bridge.FILE_SAVED})
        reply = await asyncio.wait_for(next_text(self.reader), 5.0)
        self.assertEqual(20, reply["id"])
        self.assertEqual(guest_bridge.FILE_SAVED, reply["result"]["content"][0]["text"])

    async def test_close_tab_cancels_the_clients_own_pending_diff(self):
        """How claude withdraws a diff. It could never be read before, let alone answered."""
        await self._open_a_diff(22, tab_name="✻ x.py")
        await self._send({"jsonrpc": "2.0", "id": 23, "method": "tools/call",
                          "params": {"name": "close_tab", "arguments": {"tab_name": "✻ x.py"}}})
        replies = await self._replies(2)
        self.assertEqual("TAB_CLOSED", replies[23]["result"]["content"][0]["text"])
        self.assertEqual(guest_bridge.DIFF_REJECTED, replies[22]["result"]["content"][0]["text"])
        self.assertEqual("keep\nold\n", Path(self.target).read_text())

    async def test_a_diff_nobody_answers_times_out_on_the_wall_clock(self):
        self._patch("DIFF_TIMEOUT_SECONDS", 0.3)
        await self._open_a_diff(24)
        reply = await asyncio.wait_for(next_text(self.reader), 5.0)
        self.assertEqual(24, reply["id"])
        self.assertEqual(guest_bridge.DIFF_REJECTED, reply["result"]["content"][0]["text"])
        self.assertEqual("keep\nold\n", Path(self.target).read_text())

    async def test_close_all_diff_tabs_is_this_connections_business_only(self):
        """Two claudes, one sidecar: one tidying up must not cancel the diff the user is reading
        in the other pane."""
        other_reader, other_writer = await self._connect()
        mine = await self._open_a_diff(25, tab_name="Mine")
        theirs = await self._open_a_diff(26, tab_name="Theirs", writer=other_writer,
                                         seen={mine["data"]["reply"]})
        await self._send({"jsonrpc": "2.0", "id": 27, "method": "tools/call",
                          "params": {"name": "closeAllDiffTabs", "arguments": {}}})
        replies = await self._replies(2)
        self.assertEqual("CLOSED_1_DIFF_TABS", replies[27]["result"]["content"][0]["text"])
        self.assertEqual(guest_bridge.DIFF_REJECTED, replies[25]["result"]["content"][0]["text"])

        guest_bridge.write_json_atomic(theirs["data"]["reply"], {"outcome": guest_bridge.FILE_SAVED})
        reply = await asyncio.wait_for(next_text(other_reader), 5.0)
        self.assertEqual(26, reply["id"])
        self.assertEqual(guest_bridge.FILE_SAVED, reply["result"]["content"][0]["text"])

    # ----- the path rule, again, at the moment of the write (A1) ----------------------------------

    async def test_the_path_is_checked_again_when_the_user_saves(self):
        """The pane set and the filesystem both move while a decision is on screen: the file the
        bridge writes is re-resolved against the panes as they are at that instant."""
        victim = os.path.join(self.outside, "authorized_keys")
        Path(victim).write_text("original\n")
        event = await self._open_a_diff(28)
        os.unlink(self.target)
        os.symlink(victim, self.target)   # planted after the user was shown an honest diff
        guest_bridge.write_json_atomic(event["data"]["reply"], {"outcome": guest_bridge.FILE_SAVED})
        reply = await asyncio.wait_for(next_text(self.reader), 5.0)
        self.assertEqual(28, reply["id"])
        self.assertEqual(guest_bridge.DIFF_REJECTED, reply["result"]["content"][0]["text"])
        self.assertEqual("original\n", Path(victim).read_text())

    # ----- keepalive, framing and the handshake (A6, A7, A8, A9) ----------------------------------

    async def test_the_keepalive_ping_goes_out_on_its_interval(self):
        self._patch("KEEPALIVE_SECONDS", 0.15)
        self.server._last_ping = 0.0   # due now; the interval below is the one measured
        started = asyncio.get_running_loop().time()
        for _ in range(2):
            opcode, payload = await asyncio.wait_for(server_message(self.reader), 5.0)
            self.assertEqual(0x9, opcode)
            self.assertEqual(b"relay", payload)
        self.assertGreaterEqual(asyncio.get_running_loop().time() - started, 0.15)

    async def test_an_unmasked_client_frame_fails_the_connection(self):
        """RFC 6455 §5.1. A server that reads unmasked client frames is the hole masking closes."""
        payload = json.dumps({"jsonrpc": "2.0", "id": 30, "method": "ping"}).encode()
        self.writer.write(bytes([0x81, len(payload)]) + payload)
        await self.writer.drain()
        opcode, body = await asyncio.wait_for(server_message(self.reader), 5.0)
        self.assertEqual(0x8, opcode)
        self.assertEqual(1002, struct.unpack(">H", body[:2])[0])

    async def test_an_oversized_frame_is_refused_before_a_byte_is_read(self):
        """Only the 10-byte header is sent: the close comes back without the announced payload,
        so nothing of that size was ever allocated or awaited."""
        self.writer.write(bytes([0x81, 0xFF]) + struct.pack(">Q", 64 * 1024 * 1024))
        await self.writer.drain()
        opcode, body = await asyncio.wait_for(server_message(self.reader), 5.0)
        self.assertEqual(0x8, opcode)
        self.assertEqual(1009, struct.unpack(">H", body[:2])[0])

    async def test_fragments_cannot_add_up_past_the_cap(self):
        self._patch("MAX_MESSAGE_BYTES", 256)
        self.writer.write(client_frame(b"a" * 200, opcode=0x1, fin=False))
        self.writer.write(client_frame(b"b" * 200, opcode=0x0))
        await self.writer.drain()
        opcode, body = await asyncio.wait_for(server_message(self.reader), 5.0)
        self.assertEqual(0x8, opcode)
        self.assertEqual(1009, struct.unpack(">H", body[:2])[0])

    async def test_a_client_that_never_sends_its_handshake_is_dropped(self):
        self._patch("HANDSHAKE_SECONDS", 0.25)
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self._writers.append(writer)
        self.assertEqual(b"", await asyncio.wait_for(reader.read(), 5.0), "the server hung up")

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


class AcceptKey(unittest.TestCase):
    def test_the_accept_key_matches_rfc_6455(self):
        """Whichever implementation is in use — remote/ws.py's when the tree's `remote` package
        is importable, the local three lines when only backend/ is on the path."""
        self.assertEqual("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
                         guest_bridge.websocket_accept("dGhlIHNhbXBsZSBub25jZQ=="))

    def test_the_cap_is_a_sane_size(self):
        self.assertEqual(4 * 1024 * 1024, guest_bridge.MAX_MESSAGE_BYTES)


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

    def test_no_zero_lock_is_ever_written(self):
        """A pane registered before the sidecar starts used to make `refresh_registrations()`
        write `0.lock` — a live authToken at a port nobody listens on, which nothing removes."""
        with tempfile.TemporaryDirectory() as root:
            state = make_state(root)
            lock_dir = os.path.join(root, "ide")
            os.makedirs(lock_dir)
            for index in range(4):
                register_pane(state, os.path.join(root, f"project-{index}"))
            process = self._start(state, lock_dir)
            ready = json.loads(process.stdout.readline())
            self.assertTrue(ready["ready"], ready)
            self.assertEqual([f"{ready['port']}.lock"], sorted(os.listdir(lock_dir)))
            self.assertEqual(4, len(guest_bridge.read_json(ready["lock"])["workspaceFolders"]),
                             "the panes registered before the port was known are still in it")
            process.terminate()
            process.wait(timeout=5)
            self.assertEqual([], os.listdir(lock_dir))

    def test_atexit_removes_the_lock_when_serve_never_unwinds(self):
        """`serve()`'s own `finally` covers the ordinary exits. The atexit handler is for the
        ones it does not see — here the interpreter shutting down under a daemon thread, which
        is exactly the shape of a GUI that tears its worker down without a signal."""
        script = ("import json, sys, threading\n"
                  "from relay_core import guest_bridge\n"
                  "ready = threading.Event()\n"
                  "seen = {}\n"
                  "def line(text):\n"
                  "    seen.update(json.loads(text)); ready.set()\n"
                  "thread = threading.Thread(target=guest_bridge.serve,\n"
                  "                          args=(sys.argv[1], sys.argv[2], '0', line), daemon=True)\n"
                  "thread.start()\n"
                  "assert ready.wait(20), 'the bridge never became ready'\n"
                  "print(json.dumps(seen), flush=True)\n")
        with tempfile.TemporaryDirectory() as root:
            state = make_state(root)
            lock_dir = os.path.join(root, "ide")
            os.makedirs(lock_dir)
            environment = dict(os.environ)
            environment["PYTHONPATH"] = str(BACKEND)
            done = subprocess.run([sys.executable, "-u", "-c", script, state, lock_dir],
                                  capture_output=True, text=True, timeout=60, env=environment)
            self.assertEqual(0, done.returncode, done.stderr)
            ready = json.loads(done.stdout.splitlines()[0])
            self.assertTrue(ready["ready"], ready)
            self.assertFalse(os.path.exists(ready["lock"]), "atexit removed the lock")
            self.assertEqual([], os.listdir(lock_dir))

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
