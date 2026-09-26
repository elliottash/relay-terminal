# SPDX-License-Identifier: AGPL-3.0-or-later
"""MCP server support (card #SSRQ): config, client, trust, the pane's tools, guests, import.

A fake stdio server (tests/fixtures_mcp_server.py) and a small streamable-HTTP server in this
file stand in for real ones. What is checked is the card's `## Done means`: a configured server's
tools reach a pane agent and a call returns the server's answer; a trusted server runs without an
ask and an untrusted one asks, naming the server, before anything is sent; the import previews and
writes only confirmed rows; a dead, slow or garbled server fails its one call readably.
"""
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import approvals, mcp_config, mcp_import, mcp_tools
from relay_core.agent import Agent
from relay_core.mcp_client import McpClient, McpError
from relay_core.provider import ProviderConfig

REPO = Path(__file__).resolve().parents[1]
FAKE = str(REPO / "tests" / "fixtures_mcp_server.py")
CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')


def stdio(name="fake", trust="untrusted", *extra, **fields):
    return {"command": sys.executable, "args": [FAKE, *extra], "trust": trust, **fields}


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.ws = root / "project"
        (self.ws / ".git").mkdir(parents=True)
        self.global_file = root / "config" / "mcp-servers.json"
        patcher = mock.patch.dict(os.environ, {"RELAY_MCP_CONFIG": str(self.global_file)})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(mcp_tools.close_all)

    def write_global(self, servers, **extra):
        self.global_file.parent.mkdir(parents=True, exist_ok=True)
        self.global_file.write_text(json.dumps({"mcpServers": servers, **extra}))

    def write_project(self, servers):
        (self.ws / ".mcp.json").write_text(json.dumps({"mcpServers": servers}))


class ConfigTests(Base):
    def test_global_and_project_merge_project_waits_for_enablement(self):
        self.write_global({"fake": stdio(trust="trusted"), "other": stdio()})
        self.write_project({"fake": stdio(), "repo": stdio()})
        config = mcp_config.load(self.ws)
        # A cloned repository cannot start a command: its servers wait for `enable`.
        self.assertEqual(sorted(config.pending), ["fake", "repo"])
        self.assertEqual({s.name: s.origin for s in config.servers}, {"fake": "global", "other": "global"})
        mcp_config.enable_project(self.ws, "fake")
        config = mcp_config.load(self.ws)
        fake = config.get("fake")
        self.assertEqual((fake.origin, fake.trust), ("project", "untrusted"), "project wins on a name")
        # A pull that changes the entry disables it until it is enabled again.
        self.write_project({"fake": stdio(extra="x"), "repo": stdio()})
        config = mcp_config.load(self.ws)
        self.assertIn("fake", config.pending)
        self.assertEqual(config.get("fake").origin, "global")

    def test_a_project_file_cannot_declare_itself_trusted(self):
        self.write_project({"repo": stdio(trust="trusted")})
        mcp_config.enable_project(self.ws, "repo")
        self.assertEqual(mcp_config.load(self.ws).get("repo").trust, "untrusted")
        mcp_config.set_trust("repo", "trusted", self.ws)
        self.assertEqual(mcp_config.load(self.ws).get("repo").trust, "trusted")

    def test_bad_entries_are_problems_and_the_rest_load(self):
        self.write_global({"ok": stdio(), "bad name!": stdio(), "neither": {"args": []},
                           "sse": {"url": "https://x.example/sse", "type": "sse"},
                           "off": stdio(enabled=False),
                           "trust": stdio(trust="maybe"), "http": {"url": "https://x.example/mcp"}})
        config = mcp_config.load(self.ws)
        self.assertEqual(sorted(s.name for s in config.servers), ["http", "ok"])
        self.assertEqual(config.disabled, ["off"])
        text = "\n".join(config.problems)
        for fragment in ("bad name!", "needs a command", "legacy SSE", "trust must be"):
            self.assertIn(fragment, text)

    def test_secrets_are_never_in_a_summary_or_an_error(self):
        self.write_global({"s": {"url": "https://user:pw@x.example/mcp?token=abc",
                                 "headers": {"Authorization": "Bearer sekrit"}, "env": {"K": "sekrit"}}})
        summary = json.dumps(mcp_config.load(self.ws).get("s").summary())
        self.assertNotIn("sekrit", summary)
        self.assertNotIn("abc", summary)
        self.assertNotIn("pw", summary)
        self.assertIn("Authorization", summary)
        self.write_global({"s": {"url": "https://x.example/mcp", "env": {"K": ["sekrit"]}}})
        self.assertNotIn("sekrit", "\n".join(mcp_config.load(self.ws).problems))

    def test_cli_list_and_enable(self):
        self.write_project({"repo": stdio()})
        env = {**os.environ, "PYTHONPATH": str(REPO / "backend"), "RELAY_MCP_CONFIG": str(self.global_file)}
        run = lambda *a: subprocess.run([sys.executable, "-m", "relay_core.mcp_config", *a,
                                         "--workspace", str(self.ws)], env=env, capture_output=True, text=True)
        self.assertIn("not enabled", run("list").stdout)
        self.assertEqual(run("enable", "repo", "--trust", "trusted").returncode, 0)
        self.assertIn("repo                     project  trusted", run("list").stdout)


class ClientTests(Base):
    def client(self, *extra, **fields):
        spec = mcp_config.parse_server("fake", stdio("fake", "trusted", *extra, **fields),
                                       origin="global", source="test")
        client = McpClient(spec, str(self.ws))
        self.addCleanup(client.close)
        return client

    def test_lists_across_pages_and_calls(self):
        client = self.client(env={"FAKE_MCP_SECRET": "abcd"})
        self.assertEqual([t["name"] for t in client.list_tools()], ["echo", "add", "slow", "fail", "crash"])
        result = client.call_tool("add", {"a": 2, "b": 3})
        self.assertEqual(result["content"][0]["text"], "5 secret-len=4")

    def test_worker_environment_does_not_reach_the_server(self):
        with mock.patch.dict(os.environ, {"FAKE_MCP_SECRET": "from-the-worker"}):
            client = self.client()
            self.assertIn("secret-len=0", client.call_tool("add", {"a": 1, "b": 1})["content"][0]["text"])

    def test_env_references_expand_from_the_worker(self):
        with mock.patch.dict(os.environ, {"MY_TOKEN": "xyz"}):
            client = self.client(env={"FAKE_MCP_SECRET": "${MY_TOKEN}"})
            self.assertIn("secret-len=3", client.call_tool("add", {"a": 1, "b": 1})["content"][0]["text"])

    def test_slow_call_times_out_and_stop_cancels(self):
        client = self.client(timeout=0.5)
        started = time.monotonic()
        with self.assertRaisesRegex(McpError, "did not answer tools/call within 0.5 seconds"):
            client.call_tool("slow", {"seconds": 3})
        self.assertLess(time.monotonic() - started, 2.5)
        client.close()
        cancel = threading.Event()
        threading.Timer(0.3, cancel.set).start()
        started = time.monotonic()
        with self.assertRaisesRegex(McpError, "was stopped"):
            client.call_tool("slow", {"seconds": 3}, cancel=cancel, timeout=30)
        self.assertLess(time.monotonic() - started, 2.0)

    def test_a_crash_fails_the_call_readably_and_the_next_call_restarts(self):
        client = self.client()
        with self.assertRaisesRegex(McpError, "stopped while answering tools/call.*fake server crashing"):
            client.call_tool("crash", {})
        self.assertEqual(client.call_tool("echo", {"text": "back"})["content"][0]["text"], "back")

    def test_a_server_that_will_not_start_or_talks_nonsense(self):
        with self.assertRaisesRegex(McpError, "MCP server 'fake' stopped while answering initialize"):
            self.client("--exit-at-start").list_tools()
        with self.assertRaisesRegex(McpError, "did not answer initialize"):
            with mock.patch("relay_core.mcp_client.START_TIMEOUT", 1.0):
                self.client("--garbage").list_tools()
        missing = mcp_config.parse_server("gone", {"command": "/no/such/program"}, origin="global", source="t")
        with self.assertRaisesRegex(McpError, "could not be started"):
            McpClient(missing).list_tools()


class _HttpServer(http.server.BaseHTTPRequestHandler):
    sse = False
    seen_auth: list = []

    def log_message(self, *args):
        pass

    def do_DELETE(self):
        self.send_response(200)
        self.end_headers()

    def do_POST(self):
        message = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        type(self).seen_auth.append(self.headers.get("Authorization"))
        if "id" not in message:
            self.send_response(202)
            self.end_headers()
            return
        if message["method"] == "initialize":
            result = {"protocolVersion": "2025-06-18", "capabilities": {}, "serverInfo": {"name": "h"}}
        elif message["method"] == "tools/list":
            result = {"tools": [{"name": "ping", "inputSchema": {"type": "object"}}]}
        else:
            if self.headers.get("Mcp-Session-Id") != "s1":
                self.send_response(400)
                self.end_headers()
                return
            result = {"content": [{"type": "text", "text": "pong"}]}
        body = json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": result})
        self.send_response(200)
        self.send_header("Mcp-Session-Id", "s1")
        if type(self).sse:
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            self.wfile.write(b'event: message\ndata: {"jsonrpc":"2.0","method":"notifications/progress"}\n\n')
            self.wfile.write(f"data: {body}\n\n".encode())
        else:
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body.encode())


class HttpTests(Base):
    def serve(self, sse):
        handler = type("H", (_HttpServer,), {"sse": sse, "seen_auth": []})
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        return server, handler

    def test_streamable_http_json_and_sse(self):
        for sse in (False, True):
            server, handler = self.serve(sse)
            with mock.patch.dict(os.environ, {"HTTP_TOKEN": "t0k"}):
                spec = mcp_config.parse_server("h", {"url": f"http://127.0.0.1:{server.server_port}/mcp",
                                                     "headers": {"Authorization": "Bearer ${HTTP_TOKEN}"}},
                                               origin="global", source="t")
                client = McpClient(spec)
                self.assertEqual([t["name"] for t in client.list_tools()], ["ping"])
                self.assertEqual(client.call_tool("ping", {})["content"][0]["text"], "pong")
                client.close()
            self.assertIn("Bearer t0k", handler.seen_auth)

    def test_unreachable_url_fails_readably(self):
        spec = mcp_config.parse_server("h", {"url": "http://127.0.0.1:9/mcp"}, origin="global", source="t")
        with self.assertRaisesRegex(McpError, "MCP server 'h' could not be reached"):
            McpClient(spec).list_tools()


class AgentTests(Base):
    def agent(self, servers):
        self.write_global(servers)
        agent = Agent(CONFIG, str(self.ws), lambda event: None, provider=object())
        agent.mcp_tools = mcp_tools.McpTools(str(self.ws))
        agent.mcp_tools.discover(wait=15)
        agent.mcp_tools.take_change()
        agent.plugin_workspace_changed()
        return agent

    def names(self, agent):
        return [t["function"]["name"] for t in agent.tools()]

    def test_a_trusted_servers_tools_load_and_run_without_an_ask(self):
        agent = self.agent({"fake": stdio(trust="trusted")})
        listed = self.names(agent)
        self.assertIn("load_tools", listed)
        self.assertFalse(any(n.startswith("mcp_fake_") for n in listed))
        self.assertIn("mcp_fake (mcp_fake_echo, mcp_fake_add", agent.system_prompt())
        with self.assertRaisesRegex(ValueError, 'group="mcp_fake"'):
            agent._prepare("mcp_fake_echo", {"text": "hi"})
        agent._execute(agent._prepare("load_tools", {"group": "mcp_fake"}), None)
        after = self.names(agent)
        self.assertEqual(after[:len(listed)], listed, "loading appends; nothing above moves")
        self.assertEqual(after[len(listed):], ["mcp_fake_echo", "mcp_fake_add", "mcp_fake_slow",
                                               "mcp_fake_fail", "mcp_fake_crash"])
        with mock.patch.object(agent.executor.questions, "ask_approval") as ask:
            out = agent._execute(agent._prepare("mcp_fake_add", {"a": 20, "b": 22}), None)
        ask.assert_not_called()
        self.assertEqual(out, {"server": "fake", "tool": "add", "content": "42 secret-len=0"})
        # A failing, a crashing and a vanished tool each fail their one call with a sentence.
        with self.assertRaisesRegex(ValueError, "fake MCP server: fail failed: the thing broke"):
            agent._execute(agent._prepare("mcp_fake_fail", {}), None)
        with self.assertRaisesRegex(ValueError, "stopped while answering"):
            agent._execute(agent._prepare("mcp_fake_crash", {}), None)
        self.assertEqual(agent._execute(agent._prepare("mcp_fake_echo", {"text": "alive"}), None)["content"],
                         "alive")
        with self.assertRaisesRegex(ValueError, "offers no tool called mcp_fake_nope"):
            agent._prepare("mcp_fake_nope", {})

    def test_an_untrusted_server_asks_naming_the_server_before_the_call(self):
        agent = self.agent({"fake": stdio()})
        agent._execute(agent._prepare("load_tools", {"group": "mcp_fake"}), None)
        spec = next(t for t in agent.tools() if t["function"]["name"] == "mcp_fake_add")
        self.assertIn("Asks the user before each call", spec["function"]["description"])
        asked = []

        def answer(decision):
            def ask(capability, subject):
                asked.append((capability, subject))
                return decision
            return ask

        with mock.patch.object(agent.executor.questions, "ask_approval", side_effect=answer("deny")):
            with self.assertRaisesRegex(ValueError, "did not allow this \\(use the untrusted MCP server fake\\)"):
                agent._prepare("mcp_fake_add", {"a": 1, "b": 2})
        self.assertEqual(asked[0][0], "mcp:fake")
        self.assertIn("add on fake", asked[0][1])
        header, question = approvals.prompt("mcp:fake", "add on fake")
        self.assertIn("fake", header + question)
        with mock.patch.object(agent.executor.questions, "ask_approval", side_effect=answer("once")):
            prepared = agent._prepare("mcp_fake_add", {"a": 1, "b": 2})
        self.assertEqual(agent._execute(prepared, None)["content"], "3 secret-len=0")
        self.assertEqual(len(asked), 2, "once is once: the next call asks again")
        # A read-only turn refuses a tool the server does not mark read-only, before any ask.
        agent.set_readonly(True)
        with self.assertRaisesRegex(ValueError, "writes nothing"):
            agent._prepare("mcp_fake_add", {"a": 1, "b": 2})
        agent.set_readonly(False)
        # "Always" trusts the server in its config, and asks no more.
        with mock.patch.object(agent.executor.questions, "ask_approval", side_effect=answer("always")):
            agent._prepare("mcp_fake_add", {"a": 1, "b": 2})
            agent._prepare("mcp_fake_add", {"a": 1, "b": 2})
        self.assertEqual(len(asked), 3)
        self.assertEqual(mcp_config.load(self.ws).get("fake").trust, "trusted")

    def test_an_executor_that_cannot_ask_refuses_an_untrusted_call(self):
        agent = self.agent({"fake": stdio()})
        agent._execute(agent._prepare("load_tools", {"group": "mcp_fake"}), None)
        agent.executor.may_approve = False
        with self.assertRaisesRegex(ValueError, "did not allow"):
            agent._prepare("mcp_fake_echo", {"text": "x"})

    def test_a_dead_server_offers_nothing_and_says_why(self):
        agent = self.agent({"dead": stdio("dead", "trusted", "--exit-at-start"), "fake": stdio(trust="trusted")})
        self.assertNotIn("mcp_dead", agent.system_prompt())
        self.assertIn("mcp_fake", agent.system_prompt())
        with self.assertRaisesRegex(ValueError, "mcp_dead_x is unavailable: MCP server 'dead'"):
            agent._prepare("mcp_dead_x", {})

    def test_a_late_server_joins_at_the_next_turn_boundary(self):
        self.write_global({"fake": stdio(trust="trusted")})
        agent = Agent(CONFIG, str(self.ws), lambda event: None, provider=object())
        agent.mcp_tools = mcp_tools.McpTools(str(self.ws))
        agent.mcp_tools.discover(wait=0)          # nothing has answered yet
        agent.mcp_tools.take_change()
        agent.refresh_system_prompt()
        self.assertNotIn("mcp_fake", agent.system_prompt())
        deadline = time.monotonic() + 15
        while not agent.mcp_tools.groups() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertTrue(agent.mcp_tools.take_change())
        agent.plugin_workspace_changed()          # what ask() does at the turn's start
        self.assertIn("mcp_fake", agent.system_prompt())

    def test_no_servers_changes_nothing(self):
        self.write_global({})
        plain = Agent(CONFIG, str(self.ws), lambda event: None, provider=object())
        with_mcp = self.agent({})
        self.assertEqual(self.names(plain), self.names(with_mcp))
        self.assertEqual(plain.system_prompt(), with_mcp.system_prompt())


class GuestBridgeTests(Base):
    def test_guest_sees_and_calls_mcp_tools_under_the_same_trust(self):
        from relay_core.guest_board_bridge import Bridge, exchange
        self.write_global({"fake": stdio()})
        agent = Agent(CONFIG, str(self.ws), lambda event: None, provider=object())
        agent.mcp_tools = mcp_tools.McpTools(str(self.ws))
        agent.mcp_tools.discover(wait=15)
        bridge = Bridge(False)
        self.addCleanup(bridge.close)
        bridge.bind(agent)
        cap = {"socket": bridge.path, "token": bridge.token}
        names = {t["name"] for t in exchange(cap, "tools/list")["tools"]}
        self.assertLessEqual({"mcp_fake_echo", "mcp_fake_add"}, names)
        bridge.begin(threading.Event())
        call = lambda args, key: exchange(cap, "tools/call", {"name": "mcp_fake_add", "arguments": args}, key)
        with mock.patch.object(agent.executor.questions, "ask_approval", return_value="deny") as ask:
            self.assertIn("did not allow", call({"a": 1, "b": 1}, "k1")["error"])
        ask.assert_called_once()
        self.assertEqual(ask.call_args[0][0], "mcp:fake")
        with mock.patch.object(agent.executor.questions, "ask_approval", return_value="once"):
            self.assertEqual(call({"a": 4, "b": 5}, "k2")["content"], "9 secret-len=0")


class _ScriptedProvider(http.server.BaseHTTPRequestHandler):
    """An OpenAI-compatible endpoint that plays `script`: each entry is one response."""
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
        self.server.seen.append(body)
        step = self.server.script[min(len(self.server.seen), len(self.server.script)) - 1]
        if isinstance(step, tuple):
            name, args = step
            message = {"role": "assistant", "content": None, "tool_calls": [{
                "id": f"call{len(self.server.seen)}", "type": "function",
                "function": {"name": name, "arguments": json.dumps(args)}}]}
            finish = "tool_calls"
        else:
            message, finish = {"role": "assistant", "content": step}, "stop"
        raw = json.dumps({"id": "s", "object": "chat.completion", "created": int(time.time()),
                          "model": "stub", "choices": [{"index": 0, "finish_reason": finish, "message": message}],
                          "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


class WorkerTests(Base):
    """The real worker: configure, a turn that loads the group and calls the tool, the ask."""

    def run_turn(self, trust, decision=None):
        self.write_global({"fake": stdio(trust=trust)})
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _ScriptedProvider)
        server.seen = []
        server.script = [("load_tools", {"group": "mcp_fake"}), ("mcp_fake_add", {"a": 20, "b": 22}), "Done."]
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        root = Path(self.tmp.name)
        env = {**os.environ, "XDG_DATA_HOME": str(root / "data"), "XDG_CONFIG_HOME": str(root / "config"),
               "RELAY_KEYRING": "off", "RELAY_INDEX": "off", "PYTHONPATH": str(REPO / "backend"),
               "RELAY_MCP_CONFIG": str(self.global_file)}
        proc = subprocess.Popen([sys.executable, "-u", str(REPO / "backend" / "worker.py")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, env=env, cwd=self.ws)
        self.addCleanup(lambda: (proc.poll() is None and proc.kill(), proc.stdout.close(), proc.stderr.close()))
        watchdog = threading.Timer(60, proc.kill)
        watchdog.start()
        self.addCleanup(watchdog.cancel)
        send = lambda m: (proc.stdin.write(json.dumps(m) + "\n"), proc.stdin.flush())
        send({"type": "configure", "id": "c1", "base_url": f"http://127.0.0.1:{server.server_port}/v1",
              "model": "stub", "api_key": "", "workspace": str(self.ws), "board": {"attach": False}})
        send({"type": "ask", "id": "a1", "text": "add 20 and 22 with the fake server"})
        events = []
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            events.append(json.loads(line))
            event = events[-1]
            if event.get("event") == "question" and event.get("kind") == "approval":
                send({"type": "question_answer", "id": event["id"], "decision": decision})
            if event.get("event") == "agent_finished" or (event.get("event") == "error" and event.get("id") == "a1"):
                break
        send({"type": "shutdown"})
        proc.stdin.close()
        proc.wait(timeout=20)
        # The turn's own requests carry the tool list; a title or summary request after it does not.
        return [body for body in server.seen if body.get("tools")], events

    def tool_results(self, seen):
        return [m["content"] for m in seen[-1]["messages"] if m.get("role") == "tool"]

    def test_a_trusted_server_is_loaded_and_called_in_a_real_turn(self):
        seen, events = self.run_turn("trusted")
        self.assertEqual(len(seen), 3, events[-3:])
        self.assertIn("mcp_fake (mcp_fake_echo", seen[0]["messages"][0]["content"])
        first = {t["function"]["name"] for t in seen[0]["tools"]}
        self.assertNotIn("mcp_fake_add", first)
        self.assertIn("mcp_fake_add", {t["function"]["name"] for t in seen[1]["tools"]})
        self.assertIn("42 secret-len=0", self.tool_results(seen)[-1])
        self.assertFalse([e for e in events if e.get("event") == "question"])

    def test_an_untrusted_server_asks_in_a_real_turn_and_deny_refuses(self):
        seen, events = self.run_turn("untrusted", decision="deny")
        asks = [e for e in events if e.get("event") == "question" and e.get("kind") == "approval"]
        self.assertEqual(len(asks), 1)
        self.assertEqual(asks[0]["capability"], "mcp:fake")
        self.assertIn("untrusted MCP server fake", asks[0]["question"])
        self.assertIn("add on fake", asks[0]["subject"])
        result = self.tool_results(seen)[-1]
        self.assertIn("did not allow", result)
        self.assertNotIn("42", result)
        self.assertEqual(len(seen), 3, "the turn carried on after the refusal")


class ImportTests(Base):
    def test_preview_then_write_only_confirmed_rows(self):
        root = Path(self.tmp.name)
        claude = root / "claude.json"
        claude.write_text(json.dumps({
            "mcpServers": {"github": {"type": "stdio", "command": "gh-mcp", "env": {"GH_TOKEN": "sekrit"}},
                           "broken": {"args": ["x"]}},
            "projects": {str(self.ws): {"mcpServers": {"local": {"command": "local-mcp"}}}}}))
        codex = root / "config.toml"
        codex.write_text('[mcp_servers.docs]\nurl = "https://docs.example/mcp"\n'
                         'bearer_token_env_var = "DOCS_TOKEN"\n'
                         '[mcp_servers.github]\ncommand = "other-gh"\n')
        warp = root / "warp.json"
        warp.write_text(json.dumps({"mcp_servers": {"fs": {"command": "fs-mcp",
                                                           "working_directory": "/tmp"}}}))
        self.write_global({"fs": {"command": "fs-mcp", "cwd": "/tmp", "trust": "trusted"}})
        rows, problems = mcp_import.collect(workspace=str(self.ws), claude=claude, codex=codex, warp=warp)
        status = {(r.name, r.source): r.status for r in rows}
        self.assertEqual(status, {("github", "claude"): "new", ("broken", "claude"): "invalid",
                                  ("local", "claude (project)"): "new", ("docs", "codex"): "new",
                                  ("github", "codex"): "conflict", ("fs", "warp"): "same"})
        table = mcp_import.preview(rows)
        self.assertIn("GH_TOKEN", table)
        self.assertNotIn("sekrit", table)
        added = mcp_import.apply(rows, {"docs", "github", "fs"}, "untrusted")
        self.assertEqual(sorted(added), ["docs", "github"])
        servers = mcp_config.server_map(json.loads(self.global_file.read_text()))
        self.assertEqual(sorted(servers), ["docs", "fs", "github"])
        self.assertEqual(servers["github"]["command"], "gh-mcp", "the conflicting codex row was not taken")
        self.assertEqual(servers["docs"]["headers"], {"Authorization": "Bearer ${DOCS_TOKEN}"})
        self.assertEqual(servers["docs"]["trust"], "untrusted")
        self.assertEqual(oct(self.global_file.stat().st_mode & 0o777), "0o600")

    def test_cli_preview_writes_nothing(self):
        root = Path(self.tmp.name)
        claude = root / "claude.json"
        claude.write_text(json.dumps({"mcpServers": {"a": {"command": "a-mcp"}}}))
        env = {**os.environ, "PYTHONPATH": str(REPO / "backend"), "RELAY_MCP_CONFIG": str(self.global_file),
               "HOME": str(root)}
        base = [sys.executable, "-m", "relay_core.mcp_import", "--claude-config", str(claude)]
        out = subprocess.run(base, env=env, capture_output=True, text=True)
        self.assertIn("Nothing written", out.stdout)
        self.assertFalse(self.global_file.exists())
        out = subprocess.run(base + ["--add", "a", "--trust", "trusted"], env=env, capture_output=True, text=True)
        self.assertIn("Added 1 server(s) as trusted: a", out.stdout)
        self.assertEqual(mcp_config.load().get("a").trust, "trusted")


if __name__ == "__main__":
    unittest.main()
