"""The workspace-owned Python session shared by the human and the agent (#33G0, task t:zb)."""
import contextlib
import importlib.util
import io
import json
import os
import stat
import tempfile
import threading
import time
import unittest

from relay_core import lang_router
from relay_core.py_kernel import (KernelBusy, KernelError, KernelSession, STATE_LOST, jupyter_available,
                                  probe_stata)

HAS_PANDAS = importlib.util.find_spec("pandas") is not None


class SessionCase(unittest.TestCase):
    backend = "subprocess"

    def session(self, **kwargs) -> KernelSession:
        session = KernelSession(self.backend, **kwargs).start()
        self.addCleanup(session.shutdown)
        return session

    def run_later(self, delay, fn):
        timer = threading.Timer(delay, fn)
        timer.start()
        self.addCleanup(timer.cancel)


class SharedStateTest(SessionCase):
    def test_agent_sees_what_the_human_defined(self):
        s = self.session()
        human = s.run_cell("coef = -0.5\nlabels = ['a', 'b']", origin="human")
        agent = s.run_cell("print(coef * 2, labels)\ncoef < 0", origin="agent", intent="check the sign")
        self.assertEqual(("ok", "ok"), (human["status"], agent["status"]))
        self.assertEqual("-1.0 ['a', 'b']\n", agent["stdout"])
        self.assertEqual("True", agent["result_repr"])
        self.assertEqual(("agent", "check the sign"), (agent["origin"], agent["intent"]))
        # …and the human sees what the agent defined.
        s.run_cell("fitted = coef + 1", origin="agent")
        self.assertEqual("0.5", s.run_cell("fitted")["result_repr"])

    def test_record_shape_and_order(self):
        s = self.session()
        first = s.run_cell("1")
        second = s.run_cell("2", origin="agent")
        self.assertEqual([1, 2], [first["seq"], second["seq"]])
        for key in ("seq", "kind", "origin", "intent", "code", "stdout", "stderr", "result_repr", "error",
                    "duration", "status", "started_at"):
            self.assertIn(key, first)
        self.assertGreaterEqual(first["duration"], 0)
        self.assertEqual([1, 2], [r["seq"] for r in s.history()])

    def test_last_expression_like_ipython(self):
        s = self.session()
        self.assertEqual("3", s.run_cell("a = 1\nb = 2\na + b")["result_repr"])
        self.assertIsNone(s.run_cell("a = 5")["result_repr"])
        self.assertIsNone(s.run_cell("None")["result_repr"])
        self.assertEqual("5", s.run_cell("_ if False else a")["result_repr"])
        record = s.run_cell("{'k': [1, 2]}")
        self.assertEqual(("{'k': [1, 2]}", "dict"), (record["result_repr"], record["result_type"]))
        self.assertEqual("{'k': [1, 2]}", s.run_cell("_")["result_repr"])

    def test_output_is_captured_per_cell_and_at_the_fd_level(self):
        s = self.session()
        record = s.run_cell("import os, subprocess, sys\n"
                            "print('out')\nsys.stderr.write('err\\n')\n"
                            "os.write(1, b'fd1\\n')\n"
                            "subprocess.run([sys.executable, '-c', 'print(\"child\")'])\n")
        self.assertEqual("ok", record["status"], record["error"])
        self.assertEqual("out\nfd1\nchild\n", record["stdout"])
        self.assertEqual("err\n", record["stderr"])
        self.assertEqual("", s.run_cell("x = 1")["stdout"])   # nothing leaks into the next cell

    def test_output_streams_while_the_cell_runs(self):
        seen = []
        s = self.session(on_output=lambda seq, stream, text: seen.append((seq, stream, text)))
        record = s.run_cell("import time\nprint('a', flush=True)\ntime.sleep(0.1)\nprint('b')")
        self.assertEqual("a\nb\n", record["stdout"])
        self.assertEqual("a\nb\n", "".join(t for seq, stream, t in seen if stream == "stdout"))
        self.assertTrue(all(seq == record["seq"] for seq, _, _ in seen))

    def test_errors_carry_a_user_traceback(self):
        s = self.session()
        record = s.run_cell("def f():\n    return 1 / 0\nf()")
        self.assertEqual("error", record["status"])
        self.assertEqual("ZeroDivisionError", record["error"]["ename"])
        self.assertIn("return 1 / 0", record["error"]["traceback"])     # source lines resolve
        self.assertNotIn("\x1b[", record["error"]["traceback"])        # no terminal colour codes
        if self.backend == "subprocess":                               # IPython words its own
            self.assertIn('File "<cell-1>", line 2, in f', record["error"]["traceback"])
            self.assertNotIn("<string>", record["error"]["traceback"])  # no server frames
        syntax = s.run_cell("x = (")
        self.assertEqual(("error", "SyntaxError"), (syntax["status"], syntax["error"]["ename"]))

    def test_system_exit_does_not_end_the_session(self):
        s = self.session()
        s.run_cell("kept = 7")
        self.assertEqual("SystemExit", s.run_cell("import sys; sys.exit(2)")["error"]["ename"])
        self.assertEqual("7", s.run_cell("kept")["result_repr"])

    def test_top_level_await(self):
        s = self.session()
        record = s.run_cell("import asyncio\nawait asyncio.sleep(0)\n40 + 2")
        self.assertEqual(("ok", "42"), (record["status"], record["result_repr"]))


class InterruptTest(SessionCase):
    def test_interrupt_a_busy_loop_and_keep_the_state(self):
        s = self.session()
        s.run_cell("before = 'kept'")
        self.run_later(0.3, s.interrupt)
        started = time.monotonic()
        record = s.run_cell("while True: pass")
        self.assertLess(time.monotonic() - started, 10)
        self.assertEqual(("interrupted", "KeyboardInterrupt"), (record["status"], record["error"]["ename"]))
        self.assertEqual("'kept'", s.run_cell("before")["result_repr"])

    def test_interrupt_a_sleep(self):
        s = self.session()
        self.run_later(0.3, s.interrupt)
        self.assertEqual("interrupted", s.run_cell("import time; time.sleep(30)")["status"])

    def test_interrupt_while_idle_is_a_no_op(self):
        s = self.session()
        self.assertFalse(s.interrupt())
        time.sleep(0.1)
        self.assertEqual("ok", s.run_cell("x = 1")["status"])

    def test_timeout_interrupts(self):
        s = self.session()
        record = s.run_cell("while True: pass", origin="agent", timeout=0.3)
        self.assertEqual(("timeout", "Timeout"), (record["status"], record["error"]["ename"]))
        self.assertEqual("ok", s.run_cell("x = 1")["status"])

    def test_an_ignored_interrupt_kills_and_the_loss_is_recorded(self):
        s = self.session(interrupt_grace=0.5)
        s.run_cell("lost = 1")
        record = s.run_cell("import time\nwhile True:\n    try:\n        time.sleep(0.05)\n"
                            "    except KeyboardInterrupt:\n        pass", timeout=0.3)
        self.assertEqual("timeout", record["status"])
        self.assertIn("killed", record["error"]["evalue"])
        after = s.run_cell("lost")
        restart = s.history()[-2]
        self.assertEqual(("restart", "system", STATE_LOST), (restart["kind"], restart["origin"], restart["message"]))
        self.assertEqual(("error", "NameError"), (after["status"], after["error"]["ename"]))

    def test_a_dead_kernel_is_replaced_with_a_state_lost_record(self):
        s = self.session()
        s.run_cell("x = 1")
        died = s.run_cell("import os; os._exit(3)")
        self.assertEqual(("died", "KernelDied"), (died["status"], died["error"]["ename"]))
        after = s.run_cell("y = 2\ny")
        history = s.history()
        self.assertEqual(["cell", "cell", "restart", "cell"], [r["kind"] for r in history])
        self.assertEqual("the kernel process exited", history[2]["reason"])
        self.assertEqual("2", after["result_repr"])


class RestartTest(SessionCase):
    def test_restart_clears_state_and_says_so(self):
        s = self.session()
        s.run_cell("a = 1\nb = [1, 2]")
        record = s.restart(origin="agent", intent="clean slate")
        self.assertEqual(("restart", "restarted", STATE_LOST), (record["kind"], record["status"], record["message"]))
        self.assertEqual(["a", "b"], record["lost_names"])
        self.assertEqual(("agent", "clean slate"), (record["origin"], record["intent"]))
        self.assertEqual("NameError", s.run_cell("a")["error"]["ename"])
        self.assertEqual([], s.variables())

    def test_restart_stops_the_running_cell_and_cancels_the_queue(self):
        s = self.session()
        results = {}
        running = threading.Thread(target=lambda: results.update(running=s.run_cell("import time; time.sleep(30)")))
        running.start()
        time.sleep(0.3)
        queued = threading.Thread(target=lambda: results.update(queued=s.run_cell("queued = 1")))
        queued.start()
        time.sleep(0.2)
        record = s.restart()
        running.join(10)
        queued.join(10)
        self.assertEqual("restarted", results["running"]["status"])
        self.assertEqual("cancelled", results["queued"]["status"])
        self.assertIsNone(record["lost_names"])     # it was busy: nothing could be asked
        self.assertEqual("NameError", s.run_cell("queued")["error"]["ename"])


class OrderingTest(SessionCase):
    def test_concurrent_submits_run_in_record_order(self):
        s = self.session()
        s.run_cell("log = []")
        barrier = threading.Barrier(12)

        def submit(i):
            barrier.wait()
            s.run_cell(f"log.append({i})", origin="agent" if i % 2 else "human")
        threads = [threading.Thread(target=submit, args=(i,)) for i in range(12)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(30)
        ran = json.loads(s.run_cell("import json; json.dumps(log)")["result_repr"].strip("'"))
        cells = [r for r in s.history() if r["code"].startswith("log.append")]
        self.assertEqual(sorted(ran), list(range(12)))
        self.assertEqual(ran, [int(r["code"][11:-1]) for r in sorted(cells, key=lambda r: r["seq"])])
        seqs = [r["seq"] for r in s.history()]
        self.assertEqual(seqs, sorted(seqs))

    def test_a_submission_waits_for_the_one_before(self):
        s = self.session()
        order = []
        first = threading.Thread(target=lambda: order.append(s.run_cell("import time; time.sleep(0.3); 1")["seq"]))
        first.start()
        time.sleep(0.1)
        order.append(s.run_cell("2")["seq"])
        first.join()
        self.assertEqual([1, 2], sorted(order))
        self.assertEqual(["1", "2"], [r["result_repr"] for r in s.history()])

    def test_variables_timeout_while_busy_leaves_the_queue_working(self):
        s = self.session()
        worker = threading.Thread(target=lambda: s.run_cell("import time; time.sleep(0.6)"))
        worker.start()
        time.sleep(0.1)
        with self.assertRaises(KernelBusy):
            s.variables(timeout=0.1)
        worker.join()
        self.assertEqual("ok", s.run_cell("z = 3")["status"])
        self.assertEqual(["z"], [v["name"] for v in s.variables()])


class VariablesTest(SessionCase):
    def test_basic_types(self):
        s = self.session()
        s.run_cell("import os\nn = 3\nf = 1.5\nname = 'x' * 200\nitems = [1, 2, 3]\n"
                   "table = {'a': 1, 'b': 2}\n_private = 1\n"
                   "def fit(x, y=2):\n    return x\nclass Model:\n    pass\nm = Model()")
        found = {v["name"]: v for v in s.variables()}
        self.assertEqual(["f", "fit", "items", "m", "n", "name", "table", "Model"],
                         sorted(found, key=lambda k: (k[0].isupper(), k)))
        self.assertNotIn("os", found)          # modules are not variables
        self.assertNotIn("_private", found)
        self.assertEqual(("int", "3"), (found["n"]["type"], found["n"]["summary"]))
        self.assertEqual(3, found["items"]["length"])
        self.assertIn("3 items", found["items"]["summary"])
        self.assertEqual(("dict", 2), (found["table"]["type"], found["table"]["length"]))
        self.assertLess(len(found["name"]["summary"]), 100)
        self.assertEqual("fit(x, y=2)", found["fit"]["summary"])
        self.assertEqual("class Model", found["Model"]["summary"])
        self.assertEqual("__main__.Model", found["m"]["type"])

    def test_objects_with_a_shape_and_a_broken_repr(self):
        s = self.session()
        s.run_cell("class Grid:\n    shape = (3, 4)\n    def __repr__(self):\n        raise RuntimeError('no')\ng = Grid()")
        grid = {v["name"]: v for v in s.variables()}["g"]
        self.assertEqual([3, 4], grid["shape"])
        self.assertIn("Grid", grid["summary"])       # a repr that raises is named, not propagated

    @unittest.skipUnless(HAS_PANDAS, "pandas is not installed")
    def test_dataframe_shape(self):
        s = self.session()
        s.run_cell("import pandas as pd\ndf = pd.DataFrame({'wage': [1.0, 2.0, 3.0], 'educ': [12, 16, 18]})")
        df = {v["name"]: v for v in s.variables()}["df"]
        self.assertEqual(("pandas.DataFrame", [3, 2]), (df["type"], df["shape"]))
        self.assertIn("wage, educ", df["summary"])

    def test_names_feed_the_language_router(self):
        # A bare word that is a defined variable is code for the router, not a reply.
        s = self.session()
        s.run_cell("done = True")
        names = [v["name"] for v in s.variables()]
        self.assertEqual("program", lang_router.classify_line("done", "python", names=names).destination)
        self.assertEqual("agent", lang_router.classify_line("done", "python").destination)


class InputAndExportTest(SessionCase):
    def test_input_answers_are_used_but_never_recorded(self):
        answers = iter(["alice", "s3cr3t-pw"])
        s = self.session(input_handler=lambda prompt, password: next(answers))
        record = s.run_cell("import getpass\nuser = input('User: ')\npw = getpass.getpass('Password: ')\n"
                            "print(len(pw))\nuser")
        self.assertEqual(("ok", "'alice'"), (record["status"], record["result_repr"]))
        self.assertIn("User: ", record["stdout"])
        dump = json.dumps(s.history()) + s.export("script") + s.export("json")
        self.assertNotIn("s3cr3t-pw", dump)
        self.assertNotIn("alice'", dump.replace("'alice'", ""))   # only the value the cell returned

    def test_no_handler_means_eof(self):
        s = self.session()
        self.assertIn(s.run_cell("input('? ')")["error"]["ename"], {"EOFError", "StdinNotImplementedError"})
        self.assertEqual("ok", s.run_cell("x = 1")["status"])

    def test_script_export_reproduces_the_session(self):
        s = self.session()
        s.run_cell("total = 0")
        s.run_cell("total += 5\nprint(total)", origin="agent", intent="add five")
        s.run_cell("total / 0")
        s.run_cell("total *= 2")
        script = s.export("script")
        self.assertIn("# %% [2] agent — intent: add five", script)
        self.assertIn("# total / 0", script)                 # the failing cell is commented out
        namespace: dict = {}
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            exec(compile(script, "<export>", "exec"), namespace)
        self.assertEqual("5\n", printed.getvalue())
        self.assertEqual(10, namespace["total"])

    def test_json_export_has_every_record_in_order(self):
        s = self.session()
        s.run_cell("a = 1")
        s.restart()
        s.run_cell("b = 2", origin="agent", intent="after restart")
        data = json.loads(s.export("json"))
        self.assertEqual("relay-python-session", data["format"])
        self.assertEqual([("cell", 1), ("restart", 2), ("cell", 3)],
                         [(r["kind"], r["seq"]) for r in data["records"]])
        with self.assertRaises(ValueError):
            s.export("ipynb")

    def test_argument_checks(self):
        s = self.session()
        with self.assertRaises(ValueError):
            s.run_cell("1", origin="robot")
        with self.assertRaises(TypeError):
            s.run_cell(b"1")
        s.shutdown()
        with self.assertRaises(KernelError):
            s.run_cell("1")


@unittest.skipUnless(jupyter_available(), "jupyter_client and a python3 kernel spec are not installed")
class JupyterSharedStateTest(SharedStateTest):
    backend = "jupyter"


@unittest.skipUnless(jupyter_available(), "jupyter_client and a python3 kernel spec are not installed")
class JupyterInterruptTest(InterruptTest):
    backend = "jupyter"


@unittest.skipUnless(jupyter_available(), "jupyter_client and a python3 kernel spec are not installed")
class JupyterRestartTest(RestartTest):
    backend = "jupyter"


@unittest.skipUnless(jupyter_available(), "jupyter_client and a python3 kernel spec are not installed")
class JupyterVariablesTest(VariablesTest):
    backend = "jupyter"


@unittest.skipUnless(jupyter_available(), "jupyter_client and a python3 kernel spec are not installed")
class JupyterInputAndExportTest(InputAndExportTest):
    backend = "jupyter"


class ProbeStataTest(unittest.TestCase):
    """The probe reports; it never runs Stata, and finding nothing is a result, not a pass."""

    def test_nothing_installed(self):
        with tempfile.TemporaryDirectory() as empty:
            report = probe_stata(path=empty, dir_patterns=())
        self.assertFalse(report["available"])
        self.assertEqual([], report["binaries"])
        self.assertEqual([], report["bridges"])
        self.assertIn("No Stata", report["note"])

    def test_a_binary_on_path_and_an_install_dir(self):
        with tempfile.TemporaryDirectory() as root:
            bindir = os.path.join(root, "bin")
            install = os.path.join(root, "stata18")
            os.makedirs(bindir)
            os.makedirs(os.path.join(install, "utilities", "pystata"))
            for folder, name in ((bindir, "stata-mp"), (install, "stata-se")):
                exe = os.path.join(folder, name)
                with open(exe, "w") as fh:
                    fh.write("#!/bin/sh\nexit 1\n")
                os.chmod(exe, os.stat(exe).st_mode | stat.S_IEXEC)
            report = probe_stata(path=bindir, dir_patterns=(os.path.join(root, "stata*"),))
        self.assertTrue(report["available"])
        self.assertEqual(["stata-mp", "stata-se"], [os.path.basename(b) for b in report["binaries"]])
        self.assertEqual(["18"], report["version_hints"])
        self.assertEqual([os.path.join(install, "utilities")], report["pystata_dirs"])
        self.assertEqual(["pystata", "console"], [b["bridge"] for b in report["bridges"]])
        self.assertIn("license", report["note"])

    def test_this_machine(self):
        report = probe_stata()
        for key in ("binaries", "install_dirs", "pystata", "stata_kernel", "bridges", "available", "note"):
            self.assertIn(key, report)
        self.assertEqual(bool(report["binaries"]), report["available"])


if __name__ == "__main__":
    unittest.main()
