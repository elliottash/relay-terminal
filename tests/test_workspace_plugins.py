# SPDX-License-Identifier: AGPL-3.0-or-later
"""Task-plugin workspaces: routing, runtimes and tools (#C0Q8 t:4h, t:9a; protocol 36).

What a pane's active plugin changes, and what it must not: the language router replaces Bash's
step 5 only while a Python/Stata workspace is active (or a REPL is in the foreground), the forced
prefixes keep working, the plugin's tools reach the native agent and a guest only through that
workspace, each workspace owns its own kernel or builder, and deactivation closes them.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

from relay_core import py_kernel, task_plugins, tool_groups
from relay_core import workspace_plugins as W
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from relay_core.task_plugins import PluginError, PluginRegistry

REPO = Path(__file__).resolve().parents[1]
WORKER = REPO / "backend" / "worker.py"
TEX_FIXTURE = REPO / "tests" / "fixtures" / "tex_workspace"
CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')


def which_with(*extra):
    """shutil.which, plus fake programs this machine may not have (Stata)."""
    def which(name):
        return f"/usr/bin/{name}" if name in extra else shutil.which(name)
    return which


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ws = self.root / "ws"
        self.ws.mkdir()
        # Deterministic: the stdlib kernel under this Python, whatever Jupyter is installed.
        patcher = mock.patch.object(py_kernel, "jupyter_available", return_value=False)
        patcher.start()
        self.addCleanup(patcher.stop)
        env = mock.patch.dict(os.environ, {"XDG_CACHE_HOME": str(self.root / "cache")})
        env.start()
        self.addCleanup(env.stop)
        self.events = []
        self.manager = self.make_manager()

    def make_manager(self, which=shutil.which):
        registry = PluginRegistry(global_plugins=self.root / "global", state=self.root / "plugins.json",
                                  which=which)
        manager = W.WorkspaceManager(self.events.append, registry)
        self.addCleanup(manager.shutdown)
        return manager

    def wait_for(self, predicate, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            found = [e for e in list(self.events) if predicate(e)]
            if found:
                return found[0]
            time.sleep(0.02)
        self.fail("timed out waiting for an event")


class RoutingTests(Base):
    def route(self, text, mode="auto", **extra):
        return self.manager.route({"text": text, "mode": mode, **extra})

    def test_no_workspace_leaves_bash_routing_untouched(self):
        self.assertIsNone(self.route("x = 1"))
        decision = {"route": "shell", "text": "ls", "reason": "Shell command."}
        self.assertIs(self.manager.annotate(decision, {}), decision)

    def test_python_workspace_routes_code_questions_and_forced_prefixes(self):
        state = self.manager.activate(str(self.ws), "relay.python")
        self.assertEqual(state["state"]["router"]["language"], "python")
        code = self.route("x = 1")
        self.assertEqual((code["route"], code["destination"], code["language"], code["target"]),
                         ("program", "program", "python", "kernel"))
        self.assertEqual(code["plugin_id"], "relay.python")
        ask = self.route("why is the coefficient negative")
        self.assertEqual((ask["route"], ask["target"]), ("agent", "agent"))
        self.assertTrue(ask["agent_signal"])
        self.assertEqual(self.route("def f():")["route"], "incomplete")
        # The typed `!` and `*` chips arrive as modes; `/shell` and `/agent` in the text.
        shell = self.route("ls -la", mode="shell")
        self.assertEqual((shell["route"], shell["target"], shell["forced"]), ("shell", "terminal", True))
        self.assertEqual(self.route("/shell ls")["route"], "shell")
        self.assertEqual(self.route("x = 2", mode="agent")["route"], "agent")
        self.assertEqual(self.route("/agent x = 2")["route"], "agent")
        # Doubled prefixes: `!!cmd` is the program's own shell escape, `**` a starred line.
        escape = self.route("!ls", mode="shell")
        self.assertEqual((escape["route"], escape["text"]), ("program", "!ls"))
        self.assertEqual(self.route("*a, b = [1, 2, 3]", mode="agent")["route"], "program")

    def test_deactivation_restores_bash_routing(self):
        self.manager.activate(str(self.ws), "relay.python")
        self.assertEqual(self.route("x = 1")["route"], "program")
        state = self.manager.deactivate()
        self.assertEqual(state["state"]["router"]["language"], "bash")
        self.assertIsNone(self.route("x = 1"))

    def test_foreground_repl_routes_without_a_plugin(self):
        python = self.route("x = 1", foreground_program="python3")
        self.assertEqual((python["route"], python["language"], python["target"], python["repl"]),
                         ("program", "python", "repl", True))
        self.assertEqual(self.route("%timeit x", foreground_program=["ipython"])["language"], "ipython")
        stata = self.route("regress y x", foreground_program="stata-mp")
        self.assertEqual((stata["route"], stata["language"]), ("program", "stata"))
        self.assertEqual(self.route("list the files", foreground_program="stata-mp")["route"], "agent")
        self.assertIsNone(self.route("x = 1", foreground_program="vim notes.txt"))
        self.assertIsNone(self.route("x = 1", foreground_program=["python3", "app.py"]))
        with self.assertRaises(ValueError):
            self.route("x", foreground_program={"argv": 1})

    def test_foreground_repl_wins_over_a_kernel_workspace(self):
        self.manager.activate(str(self.ws), "relay.python")
        self.assertEqual(self.route("x = 1", foreground_program="python3")["target"], "repl")
        self.assertEqual(self.route("x = 1")["target"], "kernel")

    def test_tex_workspace_routes_like_bash_with_its_label(self):
        shutil.copytree(TEX_FIXTURE, self.ws / "doc")
        if not shutil.which("latexmk"):
            self.skipTest("latexmk is not installed")
        self.manager.activate(str(self.ws), "relay.tex", path=str(self.ws / "doc" / "main.tex"))
        self.assertIsNone(self.route("ls"))
        labelled = self.manager.annotate({"route": "shell", "text": "ls"}, {})
        self.assertEqual((labelled["language"], labelled["destination"], labelled["target"]),
                         ("tex", "shell", "terminal"))

    def test_kernel_names_make_a_bare_name_code(self):
        self.manager.activate(str(self.ws), "relay.python")
        self.assertEqual(self.route("done")["route"], "agent")
        tools = W.PluginTools(self.manager)
        tools.run("py_run_cell", {"code": "done = [1, 2]", "intent": "make a name"})
        self.assertEqual(self.route("done")["route"], "program")


class ActivationProtocolTests(Base):
    def dispatch(self, **request):
        self.manager.dispatch(request, str(self.ws))
        return self.events[-1] if self.events else None

    def test_activate_state_deactivate_messages_and_events(self):
        event = self.dispatch(type="workspace_activate", id="a1", plugin_id="relay.python")
        self.assertEqual((event["event"], event["id"], event["change"], event["workspace_id"]),
                         ("workspace_state", "a1", "activated", "pane"))
        self.assertEqual(event["state"]["state"], "active")
        self.assertEqual(event["tools"]["group"], "py")
        self.assertEqual(event["tools"]["offered"], ["py_run_cell", "py_interrupt", "py_restart",
                                                     "py_variables", "py_history", "py_export"])
        self.assertEqual(event["runtime"]["kind"], "kernel")
        json.dumps(event)   # it goes on the wire
        state = self.dispatch(type="workspace_state", id="s1")
        self.assertEqual((state["change"], state["state"]["plugin_id"]), ("state", "relay.python"))
        gone = self.dispatch(type="workspace_deactivate", id="d1")
        self.assertEqual((gone["change"], gone["state"]["state"]), ("deactivated", "inactive"))
        self.assertEqual(gone["state"]["router"], {"language": "bash", "prefixes": []})
        self.assertIsNone(self.dispatch(type="workspace_state", id="s2")["state"])

    def test_candidates_list_matching_plugins(self):
        (self.ws / "paper.tex").write_text("\\documentclass{article}\n")
        event = self.dispatch(type="workspace_candidates", id="c", path=str(self.ws / "paper.tex"))
        self.assertEqual(event["candidates"][0]["plugin_id"], "relay.tex")
        event = self.dispatch(type="workspace_candidates", id="c2", foreground_program="ipython3")
        self.assertIn("relay.python", [c["plugin_id"] for c in event["candidates"]])

    def test_refusals_leave_nothing_active(self):
        with self.assertRaises(PluginError):
            self.dispatch(type="workspace_activate", plugin_id="relay.nope")
        with self.assertRaises(ValueError):
            self.dispatch(type="kernel_run", code="1")          # no kernel workspace
        with self.assertRaises(ValueError):
            self.dispatch(type="workspace_activate", plugin_id="relay.python", workspace_id="x" * 200)
        self.assertIsNone(self.manager.get())

    def test_python_activates_without_ipykernel(self):
        # Only python3 on PATH: the stdlib kernel is the fallback, so ipykernel is optional.
        manager = self.make_manager(which=lambda name: "/usr/bin/python3" if name == "python3" else None)
        state = manager.activate(str(self.ws), "relay.python")
        required = [d["program"] for d in state["state"]["dependencies"] if not d["optional"]]
        self.assertEqual(required, ["python3"])
        self.assertIn("optional", state["state"]["dependencies"][0]["install_hint"])

    def test_declared_tools_without_an_implementation_are_not_offered(self):
        manager = self.make_manager(which=which_with("stata"))
        state = manager.activate(str(self.ws), "relay.stata")
        self.assertEqual(state["tools"]["offered"], [])
        self.assertEqual(state["tools"]["unavailable"], ["stata_describe", "stata_run"])
        self.assertEqual(W.PluginTools(manager).groups(), {})
        routed = manager.route({"text": "regress y x"})
        self.assertEqual((routed["route"], routed["language"], routed["target"]), ("program", "stata", "repl"))


class KernelTests(Base):
    def test_workspaces_do_not_share_kernel_state(self):
        self.manager.activate(str(self.ws), "relay.python", workspace_id="a")
        self.manager.activate(str(self.ws), "relay.python", workspace_id="b")
        a, b = W.PluginTools(self.manager, "a"), W.PluginTools(self.manager, "b")
        self.assertEqual(a.run("py_run_cell", {"code": "secret = 41", "intent": "set"})["status"], "ok")
        seen = b.run("py_run_cell", {"code": "secret", "intent": "look"})
        self.assertEqual((seen["status"], seen["error"]["ename"]), ("error", "NameError"))
        self.assertEqual(a.run("py_run_cell", {"code": "secret + 1", "intent": "use"})["result_repr"], "42")
        self.assertNotIn("secret", [v["name"] for v in b.run("py_variables", {})["variables"]])
        self.assertIsNot(self.manager.kernel("a").session, self.manager.kernel("b").session)

    def test_human_kernel_run_and_agent_cells_share_one_namespace_in_order(self):
        self.manager.activate(str(self.ws), "relay.python")
        self.manager.dispatch({"type": "kernel_run", "id": "k1", "code": "y = 5\nprint('hi')"}, str(self.ws))
        ran = self.wait_for(lambda e: e.get("event") == "kernel_ran" and e.get("id") == "k1")
        self.assertEqual(ran["status"], "ok")
        tools = W.PluginTools(self.manager)
        doubled = tools.run("py_run_cell", {"code": "y * 2", "intent": "double the user's y"})
        self.assertEqual((doubled["result_repr"], doubled["origin"], doubled["intent"]),
                         ("10", "agent", "double the user's y"))
        records = [e["record"] for e in self.events if e.get("event") == "kernel_record"]
        self.assertEqual([(r["seq"], r["origin"]) for r in records], [(1, "human"), (2, "agent")])
        self.assertEqual(records[0]["stdout"], "hi\n")
        self.assertTrue(any(e.get("event") == "kernel_output" and e.get("text") == "hi\n" for e in self.events))
        self.assertTrue(any(e.get("event") == "kernel_variables" for e in self.events))
        history = tools.run("py_history", {})["records"]
        self.assertEqual([r["origin"] for r in history], ["human", "agent"])
        script = tools.run("py_export", {"format": "script"})["text"]
        self.assertIn("y = 5", script)
        self.assertIn("intent: double the user's y", script)

    def test_intent_is_required_and_stop_interrupts_the_cell(self):
        self.manager.activate(str(self.ws), "relay.python")
        tools = W.PluginTools(self.manager)
        with self.assertRaisesRegex(ValueError, "intent"):
            tools.run("py_run_cell", {"code": "1"})
        cancel = threading.Event()
        threading.Timer(0.5, cancel.set).start()
        started = time.monotonic()
        record = tools.run("py_run_cell", {"code": "import time\ntime.sleep(30)", "intent": "wait"}, cancel)
        self.assertLess(time.monotonic() - started, 15)
        self.assertEqual(record["status"], "interrupted")

    def test_restart_from_the_composer_and_interrupt_message(self):
        self.manager.activate(str(self.ws), "relay.python")
        tools = W.PluginTools(self.manager)
        tools.run("py_run_cell", {"code": "z = 1", "intent": "set"})
        self.manager.dispatch({"type": "kernel_interrupt", "id": "i"}, str(self.ws))
        self.assertEqual(self.events[-1]["event"], "kernel_interrupted")
        self.manager.dispatch({"type": "kernel_restart", "id": "r"}, str(self.ws))
        restarted = self.wait_for(lambda e: e.get("event") == "kernel_ran" and e.get("id") == "r")
        self.assertEqual(restarted["status"], "restarted")
        self.assertEqual(tools.run("py_run_cell", {"code": "z", "intent": "gone?"})["error"]["ename"], "NameError")

    def test_deactivate_and_shutdown_close_the_kernel(self):
        self.manager.activate(str(self.ws), "relay.python")
        W.PluginTools(self.manager).run("py_run_cell", {"code": "1", "intent": "start it"})
        session = self.manager.kernel().session
        proc = session._backend.proc
        self.manager.deactivate()
        self.assertTrue(session._closed)
        proc.wait(timeout=10)
        self.manager.activate(str(self.ws), "relay.python", workspace_id="other")
        W.PluginTools(self.manager, "other").run("py_run_cell", {"code": "1", "intent": "start it"})
        other = self.manager.kernel("other").session
        self.manager.shutdown()
        self.assertTrue(other._closed)
        self.assertIsNone(self.manager.get("other"))

    def test_kernel_env_carries_no_provider_keys(self):
        with mock.patch.dict(os.environ, {"OPENAI_API_KEY": "sk-test", "PYTHONPATH": "/x"}):
            self.manager.activate(str(self.ws), "relay.python")
            tools = W.PluginTools(self.manager)
            out = tools.run("py_run_cell", {"code": "import os\n(os.environ.get('OPENAI_API_KEY'), "
                                                     "os.environ.get('PYTHONPATH'))", "intent": "env"})
        self.assertEqual(out["result_repr"], "(None, '/x')")


class TexTests(Base):
    def setUp(self):
        super().setUp()
        if not shutil.which("latexmk") or not shutil.which("pdflatex"):
            self.skipTest("latexmk/pdflatex are not installed")
        shutil.copytree(TEX_FIXTURE, self.ws / "doc")
        self.main = self.ws / "doc" / "main.tex"

    def test_tex_tools_build_diagnose_and_sync(self):
        state = self.manager.activate(str(self.ws), "relay.tex", path=str(self.main))
        self.assertEqual(state["tools"]["offered"], ["tex_build", "tex_diagnostics", "tex_forward_search",
                                                     "tex_inverse_search", "tex_dependencies"])
        tools = W.PluginTools(self.manager)
        before = tools.run("tex_build", {"action": "status"})
        self.assertFalse(before["current"])
        built = tools.run("tex_build", {"wait_seconds": 300})
        self.assertEqual(built["state"], "live", built)
        self.assertTrue(built["current"])
        self.assertEqual(built["generation"]["revision"], built["source_revision"])
        self.assertTrue(Path(built["generation"]["pdf"]).is_file())
        self.assertTrue(any(e.get("event") == "tex_status" for e in self.events))
        diagnostics = tools.run("tex_diagnostics", {"severity": "error"})
        self.assertEqual(diagnostics["total"], 0)
        deps = tools.run("tex_dependencies", {})
        self.assertTrue(any(d["path"].endswith("intro.tex") for d in deps["dependencies"]))
        if shutil.which("synctex"):
            found = tools.run("tex_forward_search", {"file": "sections/intro.tex", "line": 3})
            self.assertTrue(found["locations"])
            loc = found["locations"][0]
            back = tools.run("tex_inverse_search", {"page": loc["page"], "x": loc["x"], "y": loc["y"]})
            self.assertTrue(back["location"]["file"].endswith(".tex"))
        # A source edit makes the PDF stale without a build.
        self.main.write_text(self.main.read_text().replace("Fixture", "Fixture, edited"))
        self.assertFalse(tools.run("tex_build", {"action": "status"})["current"])
        builder = self.manager.get().runtime.builder
        self.manager.deactivate()
        self.assertFalse(builder._thread.is_alive())


class NativeAgentTests(Base):
    def agent(self):
        agent = Agent(CONFIG, str(self.ws), lambda event: None, provider=object())
        agent.plugin_tools = W.PluginTools(self.manager)
        self.manager.on_change = lambda wid: agent.plugin_workspace_changed()
        agent.refresh_system_prompt()
        return agent

    def names(self, agent):
        return [t["function"]["name"] for t in agent.tools()]

    def test_plugin_tools_are_visible_only_in_an_active_workspace_and_load_lazily(self):
        agent = self.agent()
        before = self.names(agent)
        self.assertFalse(any(n.startswith("py_") for n in before))
        with self.assertRaisesRegex(ValueError, "relay.python workspace, which is not active"):
            agent._prepare("py_run_cell", {"code": "1", "intent": "x"})
        self.manager.activate(str(self.ws), "relay.python")
        listed = self.names(agent)
        self.assertEqual(listed[:len(before)], before, "activation appends; nothing above moves")
        self.assertIn("load_tools", listed)
        self.assertFalse(any(n.startswith("py_") for n in listed))
        load = next(t for t in agent.tools() if t["function"]["name"] == "load_tools")
        self.assertIn("py", load["function"]["parameters"]["properties"]["group"]["enum"])
        self.assertIn("py (py_run_cell", agent.system_prompt())
        with self.assertRaisesRegex(ValueError, 'group="py"'):
            agent._prepare("py_run_cell", {"code": "1", "intent": "x"})
        result = agent._execute(agent._prepare("load_tools", {"group": "py"}), None)
        self.assertEqual(result["tools"][0], "py_run_cell")
        after = self.names(agent)
        self.assertEqual(after[:len(listed)], listed)
        self.assertEqual(after[len(listed):], ["py_run_cell", "py_interrupt", "py_restart", "py_variables",
                                               "py_history", "py_export"])
        out = agent._execute(agent._prepare("py_run_cell", {"code": "6 * 7", "intent": "answer"}), None)
        self.assertEqual(out["result_repr"], "42")
        agent.set_readonly(True)
        with self.assertRaisesRegex(ValueError, "writes nothing"):
            agent._prepare("py_run_cell", {"code": "1", "intent": "x"})
        self.assertIsNotNone(agent._prepare("py_variables", {}))
        agent.set_readonly(False)
        self.manager.deactivate()
        self.assertEqual(self.names(agent), before)
        self.assertNotIn("py", agent.loaded_tool_groups)
        self.assertNotIn("py_run_cell", agent.system_prompt())
        with self.assertRaisesRegex(ValueError, "not active"):
            agent._prepare("py_run_cell", {"code": "1", "intent": "x"})

    def test_load_tools_is_unchanged_without_a_plugin(self):
        self.assertIs(tool_groups.load_tools_spec({}), tool_groups.LOAD_TOOLS_SPEC)
        self.assertEqual(tool_groups.prompt_line(()), "")
        with self.assertRaises(ValueError):
            tool_groups.validate({"group": "py"})

    def test_another_panes_workspace_is_not_this_agents(self):
        agent = self.agent()
        self.manager.activate(str(self.ws), "relay.python", workspace_id="elsewhere")
        self.assertFalse(any(n.startswith("py_") or n == "load_tools" for n in self.names(agent)))


class GuestBridgeTests(Base):
    def setUp(self):
        super().setUp()
        from relay_core.guest_board_bridge import Bridge, exchange
        self.exchange = exchange
        self.bridge = Bridge(False)
        self.addCleanup(self.bridge.close)
        self.agent = Agent(CONFIG, str(self.ws), lambda event: None, provider=object())
        self.agent.plugin_tools = W.PluginTools(self.manager)
        self.bridge.bind(self.agent)
        self.cap = {"socket": self.bridge.path, "token": self.bridge.token}

    def listed(self):
        return {t["name"] for t in self.exchange(self.cap, "tools/list")["tools"]}

    def call(self, name, args, key):
        return self.exchange(self.cap, "tools/call", {"name": name, "arguments": args}, key)

    def test_guest_sees_and_calls_plugin_tools_only_while_active(self):
        before = self.listed()
        version = self.exchange(self.cap, "tools/version")["version"]
        self.assertFalse(any(n.startswith("py_") for n in before))
        self.bridge.begin(threading.Event())
        self.assertEqual(self.call("py_run_cell", {"code": "1", "intent": "x"}, "k0")["code"], "unknown_tool")
        self.manager.activate(str(self.ws), "relay.python")
        self.assertEqual(self.listed() - before, {"py_run_cell", "py_interrupt", "py_restart", "py_variables",
                                                  "py_history", "py_export"})
        self.assertNotEqual(self.exchange(self.cap, "tools/version")["version"], version)
        out = self.call("py_run_cell", {"code": "v = 3\nv * 3", "intent": "guest cell"}, "k1")
        self.assertEqual((out["result_repr"], out["origin"]), ("9", "agent"))
        # The same namespace the native agent and the composer use.
        native = W.PluginTools(self.manager).run("py_run_cell", {"code": "v", "intent": "native"})
        self.assertEqual(native["result_repr"], "3")
        self.agent.set_readonly(True)
        self.assertIn("writes nothing", self.call("py_run_cell", {"code": "1", "intent": "x"}, "k2")["error"])
        self.agent.set_readonly(False)
        self.manager.deactivate()
        self.assertEqual(self.listed(), before)
        self.assertEqual(self.call("py_run_cell", {"code": "1", "intent": "x"}, "k3")["code"], "unknown_tool")

    def test_proxy_announces_a_changed_tool_list(self):
        proc = subprocess.Popen([self.bridge.descriptor["command"], *self.bridge.descriptor["args"]],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(lambda: (proc.kill(), proc.wait(), proc.stdout.close(), proc.stderr.close()))
        proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                     "params": {"protocolVersion": "2025-03-26"}}) + "\n")
        proc.stdin.flush()
        init = json.loads(proc.stdout.readline())
        self.assertTrue(init["result"]["capabilities"]["tools"]["listChanged"])
        time.sleep(W_POLL + 0.5)          # the watcher's first poll takes the baseline
        self.manager.activate(str(self.ws), "relay.python")
        line = proc.stdout.readline()
        self.assertEqual(json.loads(line)["method"], "notifications/tools/list_changed")
        proc.stdin.close()
        proc.wait(timeout=10)


from relay_core.guest_board_bridge import LIST_POLL_SECONDS as W_POLL   # noqa: E402


class WorkerTests(unittest.TestCase):
    """The real worker: activation, routing and a kernel line over NDJSON."""

    def test_worker_activates_routes_and_runs_a_kernel_line(self):
        with tempfile.TemporaryDirectory() as directory:
            ws = Path(directory) / "ws"
            ws.mkdir()
            env = dict(os.environ, XDG_DATA_HOME=directory, XDG_CONFIG_HOME=directory,
                       XDG_CACHE_HOME=directory, HOME=directory, RELAY_KEYRING="off", RELAY_INDEX="off",
                       PYTHONPATH=str(WORKER.parent))
            requests = [
                {"type": "route", "id": "r0", "text": "x = 1", "mode": "auto", "cwd": str(ws)},
                {"type": "workspace_activate", "id": "a", "plugin_id": "relay.python", "workspace": str(ws)},
                {"type": "route", "id": "r1", "text": "x = 1", "mode": "auto", "cwd": str(ws)},
                {"type": "route", "id": "r2", "text": "x = 1", "mode": "auto", "foreground_program": "python3",
                 "workspace_id": "tab2"},
                {"type": "kernel_run", "id": "k", "code": "x = 21\nprint(x * 2)"},
            ]
            proc = subprocess.Popen([sys.executable, "-u", str(WORKER)], stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
            try:
                for request in requests:
                    proc.stdin.write(json.dumps(request) + "\n")
                proc.stdin.flush()
                events, deadline = [], time.monotonic() + 60
                while time.monotonic() < deadline:
                    line = proc.stdout.readline()
                    if not line:
                        break
                    events.append(json.loads(line))
                    if events[-1].get("event") == "kernel_ran":
                        break
                proc.stdin.write(json.dumps({"type": "shutdown"}) + "\n")
                proc.stdin.flush()
                proc.wait(timeout=20)
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.stdout.close()
                proc.stderr.close()
            by_id = {e.get("id"): e for e in events if e.get("id")}
            self.assertNotIn("language", by_id["r0"], "no workspace: the Bash decision, unchanged")
            self.assertEqual(by_id["a"]["event"], "workspace_state")
            # The kernel's language is `ipython` where the worker's Python has ipykernel.
            self.assertEqual((by_id["r1"]["route"], by_id["r1"]["language"], by_id["r1"]["target"]),
                             ("program", "ipython" if py_kernel.jupyter_available() else "python", "kernel"))
            self.assertEqual((by_id["r2"]["route"], by_id["r2"]["target"]), ("program", "repl"))
            self.assertEqual(by_id["k"]["status"], "ok")
            record = next(e["record"] for e in events if e.get("event") == "kernel_record")
            self.assertEqual(record["stdout"], "42\n")
            self.assertEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
