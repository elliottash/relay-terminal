# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Python console pane (#83YV): what the worker tells a pane to run, and that it shares the
agent's kernel.

`workspace_activate {console: true}` starts relay.python's kernel and answers `workspace_console`
with the argv the pane runs in its pty: `jupyter console --existing <connection file>` with the
plugin's startup file as its config, so the person typing in the pty and the agent calling
py_run_cell work in one namespace; plain `ipython` (then `python3`), each with a namespace of its
own, where jupyter_console or ipykernel is missing. The startup file puts OSC 133 marks on the
prompt and around each cell, in jupyter console and in plain IPython alike.

The Jupyter cases need ipykernel, jupyter_client and jupyter_console in the Python running the
tests; they are skipped, saying so, where those are missing.
"""
import os
from pathlib import Path
import pty
import re
import select
import shutil
import sys
import tempfile
import time
import unittest
from unittest import mock

from relay_core import py_kernel, task_plugins
from relay_core import workspace_plugins as W
from relay_core.task_plugins import PluginRegistry

REPO = Path(__file__).resolve().parents[1]
PACKAGE = REPO / "backend" / "relay_core" / "plugins_bundled" / "python"
STARTUP = PACKAGE / "ipython_startup.py"
PROGRAM = ("jupyter", "console", "--existing", "{connection_file}")


def has(module):
    import importlib.util
    return importlib.util.find_spec(module) is not None


JUPYTER = py_kernel.jupyter_available()
CONSOLE = JUPYTER and has("jupyter_console")
IPYTHON = has("IPython")
MARK = re.compile(r"\x1b\]133;([A-D](?:;\d+)?)\x07")


def spec(program=PROGRAM, startup="ipython_startup.py"):
    return task_plugins.ConsoleSpec(tuple(program), (), "osc133", startup)


def which_of(**found):
    return lambda name: found.get(name)


class ConsoleCommandTest(unittest.TestCase):
    """console_command: the manifest's program when a kernel can be shared, else a REPL of its own."""

    def test_jupyter_on_path_runs_the_manifest_program_with_the_startup_as_config(self):
        answer = W.console_command(spec(), PACKAGE, "/run/k.json",
                                   which=which_of(jupyter="/v/bin/jupyter", **{"jupyter-console": "/v/bin/jupyter-console"}),
                                   has_module=lambda m: False)
        self.assertEqual(["/v/bin/jupyter", "console", "--existing", "/run/k.json", f"--config={STARTUP}"],
                         answer["argv"])
        self.assertEqual((True, "Python · ipython", "jupyter-console", "/run/k.json"),
                         (answer["shared"], answer["label"], answer["program"], answer["connection_file"]))
        self.assertEqual("", answer["note"])

    def test_jupyter_without_the_console_subcommand_uses_the_workers_own_module(self):
        answer = W.console_command(spec(), PACKAGE, "/run/k.json", which=which_of(jupyter="/v/bin/jupyter"),
                                   has_module=lambda m: m == "jupyter_console", python="/w/python3")
        self.assertEqual(["/w/python3", "-m", "jupyter_console", "--existing", "/run/k.json",
                          f"--config={STARTUP}"], answer["argv"])
        self.assertTrue(answer["shared"])

    def test_no_jupyter_console_falls_back_to_ipython_with_the_startup_file(self):
        answer = W.console_command(spec(), PACKAGE, "/run/k.json", which=which_of(ipython="/u/ipython"),
                                   has_module=lambda m: False)
        self.assertEqual(["/u/ipython", f"--InteractiveShellApp.exec_files={STARTUP}"], answer["argv"])
        self.assertFalse(answer["shared"])
        self.assertEqual("Python · ipython", answer["label"])
        self.assertIn("jupyter-console", answer["note"])

    def test_a_stdlib_kernel_has_no_connection_file_so_the_console_is_ipython_of_its_own(self):
        answer = W.console_command(spec(), PACKAGE, None,
                                   which=which_of(jupyter="/v/bin/jupyter", ipython3="/u/ipython3",
                                                  **{"jupyter-console": "/v/bin/jupyter-console"}),
                                   has_module=lambda m: True)
        self.assertEqual("/u/ipython3", answer["argv"][0])
        self.assertFalse(answer["shared"])
        self.assertIn("stdlib server", answer["note"])

    def test_nothing_but_python_is_a_plain_repl_without_marks(self):
        answer = W.console_command(spec(), PACKAGE, None, which=which_of(python3="/usr/bin/python3"),
                                   has_module=lambda m: False)
        self.assertEqual((["/usr/bin/python3"], "Python · python", None),
                         (answer["argv"], answer["label"], answer["startup"]))

    def test_the_bundled_manifest_declares_the_program_and_its_startup(self):
        manifest = PluginRegistry().record(None, "relay.python").manifest
        self.assertEqual(PROGRAM, manifest.console.program)
        self.assertTrue((manifest.root / manifest.console.startup).is_file())
        self.assertIn("print", [row["text"] for row in W.console_completions(manifest)])

    def test_stata_console_resolves_an_installed_alternative(self):
        manifest = PluginRegistry().record(None, "relay.stata").manifest
        answer = W.stata_console_command(manifest.console, which=which_of(**{"stata-mp": "/bin/stata-mp"}))
        self.assertEqual(["/bin/stata-mp", "-q"], answer["argv"])
        self.assertEqual("stata-mp", answer["program"])
        self.assertEqual([], W.stata_console_command(manifest.console, which=which_of())["argv"])


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ws = self.root / "ws"
        self.ws.mkdir()
        env = mock.patch.dict(os.environ, {"XDG_CACHE_HOME": str(self.root / "cache")})
        env.start()
        self.addCleanup(env.stop)
        self.events = []
        registry = PluginRegistry(global_plugins=self.root / "global", state=self.root / "plugins.json")
        self.manager = W.WorkspaceManager(self.events.append, registry)
        self.addCleanup(self.manager.shutdown)

    def wait_for(self, event, request_id=None, timeout=90):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for item in list(self.events):
                if item.get("event") == event and (request_id is None or item.get("id") == request_id):
                    return item
                if item.get("event") == "error" and item.get("id") == request_id:
                    self.fail(item["text"])
            time.sleep(0.05)
        self.fail(f"no {event} within {timeout} s: {self.events}")

    def open_console(self, request_id="c1"):
        self.manager.dispatch({"type": "workspace_activate", "id": request_id, "plugin_id": "relay.python",
                               "console": True}, str(self.ws))
        state = self.wait_for("workspace_state", request_id)
        self.assertEqual({"state": "starting"}, state["console"])
        return self.wait_for("workspace_console", request_id)


class ActivationTest(Base):
    def test_console_is_refused_for_a_plugin_without_a_kernel_and_nothing_is_activated(self):
        with self.assertRaisesRegex(ValueError, "kernel plugin"):
            self.manager.dispatch({"type": "workspace_activate", "plugin_id": "relay.tex", "console": True},
                                  str(self.ws))
        self.assertIsNone(self.manager.get())
        with self.assertRaisesRegex(ValueError, "true or false"):
            self.manager.dispatch({"type": "workspace_activate", "plugin_id": "relay.python", "console": "yes"},
                                  str(self.ws))

    def test_stata_console_answers_with_a_program_or_an_explanation(self):
        self.manager.registry.which = which_of(**{"stata-se": "/bin/stata-se"})
        with mock.patch.object(W, "stata_console_command", return_value={"argv": ["/bin/stata-se", "-q"],
                              "program": "stata-se", "label": "Stata · stata-se", "shared": False, "note": ""}):
            self.manager.dispatch({"type": "workspace_activate", "id": "s1", "plugin_id": "relay.stata",
                                   "console": True}, str(self.ws))
        answer = self.wait_for("workspace_console", "s1")
        self.assertEqual(["/bin/stata-se", "-q"], answer["argv"])
        self.assertEqual("relay.stata", self.manager.get().plugin_id)

    def test_without_jupyter_the_answer_is_a_repl_of_its_own_on_the_stdlib_kernel(self):
        with mock.patch.object(py_kernel, "jupyter_available", return_value=False):
            answer = self.open_console()
        self.assertFalse(answer["shared"])
        self.assertIsNone(answer["connection_file"])
        self.assertEqual(("subprocess", True), (answer["runtime"]["backend"], answer["runtime"]["alive"]))
        self.assertEqual("relay.python", answer["plugin_id"])


@unittest.skipUnless(JUPYTER, "ipykernel and jupyter_client are not installed in this Python")
class SharedKernelTest(Base):
    """The console's kernel client and the agent's py_run_cell share one namespace."""

    def client(self, connection_file):
        from jupyter_client import BlockingKernelClient
        client = BlockingKernelClient(connection_file=connection_file)
        client.load_connection_file()
        client.start_channels()
        self.addCleanup(client.stop_channels)
        client.wait_for_ready(timeout=30)
        return client

    def execute(self, client, code):
        reply = client.execute_interactive(code, timeout=30, output_hook=lambda msg: None)
        return reply["content"]

    def test_x_typed_in_the_console_is_read_by_py_run_cell_and_py_variables(self):
        answer = self.open_console()
        connection_file = answer["connection_file"]
        self.assertTrue(connection_file and Path(connection_file).is_file(), answer)
        self.assertEqual(connection_file, answer["runtime"]["connection_file"])
        if CONSOLE:
            self.assertTrue(answer["shared"])
            self.assertIn(connection_file, answer["argv"])
            self.assertIn(f"--config={STARTUP}", answer["argv"])
        console = self.client(connection_file)
        self.assertEqual("ok", self.execute(console, "x = 41")["status"])
        tools = W.PluginTools(self.manager)
        result = tools.run("py_run_cell", {"code": "print(x + 1)", "intent": "add one to the user's x"})
        self.assertEqual(("ok", "42\n"), (result["status"], result["stdout"]))
        names = [v["name"] for v in tools.run("py_variables", {})["variables"]]
        self.assertIn("x", names)
        # And the other way: what the agent defines, the console reads.
        tools.run("py_run_cell", {"code": "y = x * 2", "intent": "derive y"})
        self.execute(console, "assert y == 82")

    def test_exit_typed_in_a_console_leaves_the_shared_kernel_running(self):
        """`exit()` runs in the kernel, and ipykernel stops itself on it unless told to keep."""
        answer = self.open_console()
        console = self.client(answer["connection_file"])
        self.execute(console, "x = 41")
        reply = self.execute(console, "exit()")
        self.assertIn({"source": "ask_exit", "keepkernel": True}, reply.get("payload", []))
        time.sleep(1.0)                     # ipykernel would stop its loop 0.1 s after exit_now
        result = W.PluginTools(self.manager).run("py_run_cell", {"code": "x", "intent": "still here"})
        self.assertEqual(("ok", "41"), (result["status"], result["result_repr"]))

    def test_a_restart_keeps_the_connection_file_so_the_console_follows_the_new_kernel(self):
        answer = self.open_console()
        console = self.client(answer["connection_file"])
        self.execute(console, "x = 41")
        tools = W.PluginTools(self.manager)
        record = tools.run("py_restart", {"intent": "clear the namespace"})
        self.assertIn("x", record["lost_names"])
        self.assertEqual(answer["connection_file"], self.manager.kernel().info()["connection_file"])
        self.assertTrue(Path(answer["connection_file"]).is_file())
        deadline = time.monotonic() + 30
        while True:          # the old client reconnects to the same ports on its own
            try:
                content = self.execute(console, "x")
                break
            except Exception:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.2)
        self.assertEqual(("error", "NameError"), (content["status"], content["ename"]))

    def test_the_worker_answers_on_the_wire(self):
        """Through worker.py itself: activation, then workspace_console with a live kernel."""
        import json
        import subprocess
        worker = REPO / "backend" / "worker.py"
        env = dict(os.environ, PYTHONPATH=str(worker.parent), XDG_CACHE_HOME=str(self.root / "cache"),
                   HOME=str(self.root))
        proc = subprocess.Popen([sys.executable, str(worker)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True, env=env, cwd=str(self.ws))
        self.addCleanup(proc.kill)
        send = lambda message: (proc.stdin.write(json.dumps(message) + "\n"), proc.stdin.flush())
        send({"type": "workspace_activate", "id": "w1", "plugin_id": "relay.python", "console": True,
              "workspace": str(self.ws)})
        deadline, seen = time.monotonic() + 90, []
        answer = None
        while time.monotonic() < deadline and answer is None:
            ready, _, _ = select.select([proc.stdout], [], [], 1)
            if ready:
                line = proc.stdout.readline()
                if not line:
                    break
                event = json.loads(line)
                seen.append(event.get("event"))
                if event.get("event") == "error" and event.get("id") == "w1":
                    self.fail(event)
                if event.get("event") == "workspace_console":
                    answer = event
        self.assertIsNotNone(answer, seen)
        self.assertLess(seen.index("workspace_state"), seen.index("workspace_console"))
        self.assertTrue(Path(answer["connection_file"]).is_file())
        send({"type": "shutdown"})


def run_in_pty(argv, steps, settle=4.0, env=None):
    """Run argv in a pty; each step is (bytes to type, seconds to wait, callable or None)."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(env or {})
        os.environ["TERM"] = "xterm-256color"
        os.execvp(argv[0], argv)
    out = bytearray()

    def pump(seconds, until=None):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if until is not None and until in out:
                return
            ready, _, _ = select.select([fd], [], [], 0.05)
            if ready:
                try:
                    out.extend(os.read(fd, 65536))
                except OSError:
                    return
    try:
        pump(settle, b"133;B")
        for typed, seconds, action in steps:
            if typed:
                os.write(fd, typed)
            if action is not None:
                action()
            pump(seconds)
    finally:
        try:
            os.kill(pid, 9)
        except ProcessLookupError:
            pass
        os.waitpid(pid, 0)
        os.close(fd)
    return out.decode("utf-8", "replace")


@unittest.skipUnless(CONSOLE, "ipykernel, jupyter_client and jupyter_console are not installed in this Python")
class ConsolePtyTest(Base):
    """The argv the worker hands the pane, run in a real pty."""

    def test_prompt_marks_shared_namespace_agent_output_and_interrupt(self):
        answer = self.open_console()
        tools = W.PluginTools(self.manager)
        results = {}

        def agent_adds_one():
            results["agent"] = tools.run("py_run_cell", {"code": "print(x + 1)", "intent": "add one to x"})

        text = run_in_pty(answer["argv"], [
            (b"x = 41\r", 2.0, None),
            (None, 3.0, agent_adds_one),
            (b"1/0\r", 2.0, None),
            (b"import time; time.sleep(30)\r", 1.5, None),
            (b"\x03", 3.0, None),
            (b"print('after', x)\r", 2.0, None),
            (b"exit()\r", 2.0, None),
        ], settle=30)
        marks = MARK.findall(text)
        self.assertIn("A", marks)
        self.assertIn("B", marks)
        # One C…D per cell: x = 41 ok, 1/0 failed, the sleep interrupted, print ok, exit() ok.
        ends = [m for m in marks if m.startswith("D")]
        self.assertEqual(["D;0", "D;1", "D;1", "D;0", "D;0"], ends, text[-3000:])
        self.assertEqual("42\n", results["agent"]["stdout"])
        # jupyter console shows another client's cell as its input under the `[agent] ` prompt
        # prefix the startup file sets, then its output.
        shown = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07]*\x07", "", text)
        self.assertIn("[agent] ", shown)
        self.assertRegex(shown, r"print\(x \+ 1\)\r?\n42\r?\n")
        self.assertIn("KeyboardInterrupt", text)
        self.assertIn("after 41", text)
        self.assertNotIn("^[", text)
        # exit() leaves the kernel the agent shares running.
        self.assertEqual("ok", tools.run("py_run_cell", {"code": "x", "intent": "still here"})["status"])


def assert_repainted(test, text):
    """The pane erases the prompt row, prints the agent's lines, then sends Ctrl+X Ctrl+P: a
    second full prompt (A, then B) follows the first, with the half-typed line drawn after it."""
    marks = MARK.findall(text)
    test.assertEqual(2, marks.count("B"), marks)
    tail = text.rsplit("\x1b]133;B\x07", 1)[1]
    test.assertIn("x = 4", re.sub(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07]*\x07", "", tail))


@unittest.skipUnless(CONSOLE, "ipykernel, jupyter_client and jupyter_console are not installed in this Python")
class ConsoleRedrawTest(Base):
    def test_ctrl_x_ctrl_p_repaints_the_prompt_after_the_agents_lines(self):
        text = run_in_pty(self.open_console()["argv"], [(b"x = 4", 1.0, None), (b"\x18\x10", 2.0, None)],
                          settle=30)
        assert_repainted(self, text)


@unittest.skipUnless(IPYTHON, "IPython is not installed in this Python")
class PlainIPythonPtyTest(unittest.TestCase):
    def test_ctrl_x_ctrl_p_repaints_the_prompt(self):
        text = run_in_pty([sys.executable, "-m", "IPython", "--no-banner",
                           f"--InteractiveShellApp.exec_files={STARTUP}"],
                          [(b"x = 4", 1.0, None), (b"\x18\x10", 1.5, None)], settle=20)
        assert_repainted(self, text)

    def test_the_startup_file_marks_plain_ipython(self):
        text = run_in_pty([sys.executable, "-m", "IPython", "--no-banner",
                           f"--InteractiveShellApp.exec_files={STARTUP}"],
                          [(b"1/0\r", 1.5, None), (b"x = 41\r", 1.5, None), (b"%who\r", 1.5, None),
                           (b"exit()\r", 1.0, None)], settle=20)
        ends = [m for m in MARK.findall(text) if m.startswith("D")]
        self.assertEqual(["D;1", "D;0", "D;0", "D;0"], ends, text[-2000:])   # exit() is a cell too
        self.assertIn("B", MARK.findall(text))
        self.assertNotIn("^[", text)
        # The startup file leaves only the user's names behind.
        who = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07]*\x07", "", text.split("%who", 1)[1])
        self.assertIn("x", who)
        self.assertNotIn("RelayPrompts", who)
        self.assertNotIn("_relay", who)


if __name__ == "__main__":
    unittest.main()
