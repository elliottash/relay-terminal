# SPDX-License-Identifier: AGPL-3.0-or-later
"""Which Python runs a workspace's kernel (#83YV, relay_core.py_env): the project's venv, then
this worker's own Jupyter, then Relay's managed venv — built only when a console asks. Nothing
here installs anything: `run` and `which` are fakes that record the commands."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from relay_core import py_env


class FakeRun:
    """Answers the probes and the build steps; `fail` names a step (argv[1]) to fail."""

    def __init__(self, imports=None, fail=None):
        self.imports = imports or {}       # python path -> set of importable modules
        self.fail = fail
        self.calls = []

    def __call__(self, argv, **kwargs):
        self.calls.append(list(argv))
        out = ""
        code = 0
        if argv[1:2] == ["-c"]:
            source = argv[2]
            if "sysconfig" in source:              # site_dirs
                out = f"{argv[0]}-site\n{argv[0]}-site\n"
            elif source.startswith("import "):
                wanted = {m.strip() for m in source[len("import "):].split(",")}
                code = 0 if wanted <= self.imports.get(argv[0], set()) else 1
        elif self.fail and self.fail in argv[:3]:
            return subprocess.CompletedProcess(argv, 1, "", "no network\nresolution failed")
        elif "venv" in argv:                       # uv venv … <root> / python -m venv <root>
            root = Path(argv[-1])
            python = py_env.venv_python(root)
            python.parent.mkdir(parents=True, exist_ok=True)
            python.write_text("")
        return subprocess.CompletedProcess(argv, code, out, "")


class Base(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.ws = self.root / "ws"
        self.ws.mkdir()
        self.managed = self.root / "data" / "relay" / "python" / "kernel-py3xx"
        env = mock.patch.dict(os.environ, {"XDG_DATA_HOME": str(self.root / "data")})
        env.start()
        self.addCleanup(env.stop)
        env = mock.patch.dict(os.environ)
        env.start()
        os.environ.pop("RELAY_PYTHON_PROVISION", None)
        self.addCleanup(env.stop)
        self.statuses = []

    def project_venv(self, name=".venv"):
        python = py_env.venv_python(self.ws / name)
        python.parent.mkdir(parents=True)
        python.write_text("")
        return str(python)

    def resolve(self, run, build=False, worker=False, uv=True, client=True):
        with mock.patch.object(py_env, "worker_has_client", return_value=client):
            return py_env.resolve(str(self.ws), build=build, status=lambda text, building: self.statuses.append(text),
                                  worker_jupyter=lambda: worker, run=run,
                                  which=lambda name: "/usr/bin/uv" if uv and name == "uv" else None,
                                  root=self.managed)


class OrderTest(Base):
    def test_the_projects_venv_with_ipykernel_comes_first(self):
        python = self.project_venv()
        plan = self.resolve(FakeRun({python: {"ipykernel", "jupyter_console"}}), worker=True)
        self.assertEqual(("project", python, python), (plan.source, plan.kernel_python, plan.console_python))

    def test_a_venv_folder_named_venv_counts_too(self):
        python = self.project_venv("venv")
        self.assertEqual("project", self.resolve(FakeRun({python: {"ipykernel"}}), worker=True).source)

    def test_a_project_venv_without_ipykernel_is_passed_over(self):
        self.project_venv()
        self.assertEqual("worker", self.resolve(FakeRun(), worker=True).source)

    def test_the_workers_jupyter_is_second_and_builds_nothing(self):
        run = FakeRun()
        plan = self.resolve(run, build=True, worker=True)
        self.assertEqual(("worker", None), (plan.source, plan.kernel_python))
        self.assertFalse(any("install" in call for call in run.calls))

    def test_without_either_nothing_is_built_unless_a_console_asks(self):
        run = FakeRun()
        plan = self.resolve(run)
        self.assertEqual("none", plan.source)
        self.assertIn("console", plan.note)
        self.assertEqual([], [c for c in run.calls if "venv" in c])

    def test_a_managed_venv_already_built_is_used_without_a_console(self):
        python = py_env.venv_python(self.managed)
        python.parent.mkdir(parents=True)
        python.write_text("")
        (self.managed / py_env.MARKER).write_text(json.dumps({"packages": list(py_env.PACKAGES)}))
        plan = self.resolve(FakeRun())
        self.assertEqual(("managed", str(python), str(python)), (plan.source, plan.kernel_python, plan.console_python))
        self.assertEqual((f"{python}-site",), plan.client_sites)

    def test_a_project_venv_with_a_worker_lacking_jupyter_client_borrows_the_managed_client(self):
        project = self.project_venv()
        plan = self.resolve(FakeRun({project: {"ipykernel"}}), build=True, client=False)
        managed = str(py_env.venv_python(self.managed))
        self.assertEqual(("project", project, managed), (plan.source, plan.kernel_python, plan.console_python))
        self.assertEqual((f"{managed}-site",), plan.client_sites)


class BuildTest(Base):
    def test_a_console_builds_the_managed_venv_with_uv_and_says_so(self):
        run = FakeRun()
        plan = self.resolve(run, build=True)
        python = str(py_env.venv_python(self.managed))
        self.assertEqual(("managed", python), (plan.source, plan.kernel_python))
        self.assertEqual(["/usr/bin/uv", "venv", "--quiet", "--python", sys.executable, str(self.managed)], run.calls[0])
        self.assertEqual(["/usr/bin/uv", "pip", "install", "--quiet", "--python", python, *py_env.PACKAGES], run.calls[1])
        marker = json.loads((self.managed / py_env.MARKER).read_text())
        self.assertEqual(list(py_env.PACKAGES), marker["packages"])
        self.assertTrue(self.statuses[0].startswith("Setting up Python for the console"))
        self.assertEqual("Python for the console is ready.", self.statuses[-1])

    def test_without_uv_it_is_venv_and_pip(self):
        run = FakeRun()
        self.resolve(run, build=True, uv=False)
        python = str(py_env.venv_python(self.managed))
        self.assertEqual([sys.executable, "-m", "venv", str(self.managed)], run.calls[0])
        self.assertEqual([python, "-m", "pip", "install"], run.calls[1][:4])

    def test_a_failed_build_leaves_nothing_behind_and_the_note_says_why(self):
        plan = self.resolve(FakeRun(fail="pip"), build=True)
        self.assertEqual("none", plan.source)
        self.assertIn("resolution failed", plan.note)
        self.assertFalse(self.managed.exists())

    def test_a_build_without_its_marker_is_rebuilt(self):
        python = py_env.venv_python(self.managed)
        python.parent.mkdir(parents=True)
        python.write_text("")                       # interrupted: no marker
        run = FakeRun()
        self.assertEqual("managed", self.resolve(run, build=True).source)
        self.assertIn("venv", run.calls[0])

    def test_provisioning_can_be_switched_off(self):
        with mock.patch.dict(os.environ, {"RELAY_PYTHON_PROVISION": "off"}):
            run = FakeRun()
            self.assertEqual("none", self.resolve(run, build=True).source)
        self.assertEqual([], [c for c in run.calls if "venv" in c])


if __name__ == "__main__":
    unittest.main()
