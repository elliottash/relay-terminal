# SPDX-License-Identifier: AGPL-3.0-or-later
"""Which Python runs a workspace's Jupyter kernel (#83YV).

The owner's order (card #83YV, decision of 2026-09-26):

1. **The project's own venv** — `.venv/` or `venv/` in the workspace — when it has ipykernel. The
   kernel then imports what the project installed.
2. **This worker's own Jupyter**, when its Python already has jupyter_client and a python3 kernel
   spec: what worked before this module, and it needs no build.
3. **Relay's managed venv**, `$XDG_DATA_HOME/relay/python/kernel-py3XY`, with `PACKAGES` in it. It
   is built only when a Python console asks (`resolve(build=True)`), with `uv` when it is on PATH
   and `python -m venv` + pip otherwise, and reported through `status(text, building)`; once built,
   every later kernel uses it.
4. Nothing: the caller falls back to the stdlib subprocess kernel, and `note` says why.

The kernel and the worker are separate processes: the kernel runs under `kernel_python`, and this
worker talks to it through jupyter_client. A worker whose Python lacks jupyter_client borrows the
managed venv's site-packages (`client_sites`, appended to `sys.path`). The managed venv is built
from this worker's own interpreter, so its compiled wheels (pyzmq) match it, and it is named by
that interpreter's version.
"""
from __future__ import annotations

import importlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

PACKAGES = ("ipykernel", "jupyter_client", "jupyter-console", "numpy", "pandas", "matplotlib")
MARKER = "relay-env.json"
PROJECT_VENVS = (".venv", "venv")
BUILD_TIMEOUT = 900.0
PROBE_TIMEOUT = 30.0


class EnvError(RuntimeError):
    """The managed venv could not be built."""


@dataclass(frozen=True)
class KernelEnv:
    source: str                          # "project", "worker", "managed" or "none"
    kernel_python: str | None = None     # the interpreter ipykernel runs under (None: the worker's spec)
    console_python: str | None = None    # a Python with jupyter_console, for the console pane's pty
    client_sites: tuple[str, ...] = field(default_factory=tuple)   # for this worker's jupyter_client
    note: str = ""

    @property
    def jupyter(self) -> bool:
        return self.source != "none"


def venv_python(root: Path) -> Path:
    return root / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def has_modules(python: str, modules, run=subprocess.run) -> bool:
    try:
        return run([python, "-c", "import " + ", ".join(modules)], capture_output=True,
                   timeout=PROBE_TIMEOUT).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def project_python(workspace, run=subprocess.run) -> str | None:
    """The workspace's own venv interpreter, when it can run ipykernel."""
    if not workspace:
        return None
    for name in PROJECT_VENVS:
        python = venv_python(Path(workspace) / name)
        if python.is_file() and has_modules(str(python), ("ipykernel",), run):
            return str(python)
    return None


def managed_root(python_version=None) -> Path:
    major, minor = (python_version or sys.version_info)[:2]
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "relay" / "python" / f"kernel-py{major}{minor}"


def managed_python(root: Path | None = None) -> Path | None:
    """The managed venv's interpreter when a finished build of the current `PACKAGES` is there."""
    root = root or managed_root()
    try:
        marker = json.loads((root / MARKER).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    python = venv_python(root)
    return python if python.is_file() and marker.get("packages") == list(PACKAGES) else None


def site_dirs(python: str, run=subprocess.run) -> tuple[str, ...]:
    code = "import sysconfig; p = sysconfig.get_paths(); print(p['purelib']); print(p['platlib'])"
    try:
        out = run([python, "-c", code], capture_output=True, text=True, timeout=PROBE_TIMEOUT)
    except (OSError, subprocess.SubprocessError):
        return ()
    return tuple(dict.fromkeys(line for line in out.stdout.splitlines() if line.strip()))


def _lock(path: Path):
    handle = open(path, "a+")
    try:
        import fcntl
        fcntl.flock(handle, fcntl.LOCK_EX)
    except ImportError:          # Windows: one worker per pane still builds at most one venv at a time
        pass
    return handle


def ensure_managed(status: Callable[[str, bool], None] = lambda text, building: None, which=shutil.which,
                   run=subprocess.run, python: str | None = None, root: Path | None = None) -> Path:
    """The managed venv's interpreter, building the venv first when it is missing or stale. Two
    panes asking at once build it once: the second waits on the lock, then finds the marker."""
    python = python or sys.executable
    root = root or managed_root()
    if (ready := managed_python(root)) is not None:
        return ready
    root.parent.mkdir(parents=True, exist_ok=True)
    with _lock(root.parent / f"{root.name}.lock"):
        if (ready := managed_python(root)) is not None:
            return ready
        status("Setting up Python for the console (first time only: "
               + ", ".join(PACKAGES) + ")…", True)
        # A venv is not relocatable (its scripts name its path), so it is built where it stays; the
        # marker is written last, and a directory without one is a build that did not finish.
        shutil.rmtree(root, ignore_errors=True)
        uv = which("uv")
        target = str(venv_python(root))
        steps = ([[uv, "venv", "--quiet", "--python", python, str(root)],
                  [uv, "pip", "install", "--quiet", "--python", target, *PACKAGES]] if uv else
                 [[python, "-m", "venv", str(root)],
                  [target, "-m", "pip", "install", "--quiet", "--disable-pip-version-check", *PACKAGES]])
        for step in steps:
            try:
                done = run(step, capture_output=True, text=True, timeout=BUILD_TIMEOUT)
            except (OSError, subprocess.SubprocessError) as exc:
                shutil.rmtree(root, ignore_errors=True)
                status("Setting up Python for the console failed.", False)
                raise EnvError(f"{Path(step[0]).name} failed: {exc}") from exc
            if done.returncode != 0:
                shutil.rmtree(root, ignore_errors=True)
                status("Setting up Python for the console failed.", False)
                tail = (done.stderr or done.stdout or "").strip().splitlines()[-3:]
                raise EnvError(f"{Path(step[0]).name} {step[1]} failed: " + " / ".join(tail))
        (root / MARKER).write_text(json.dumps({
            "packages": list(PACKAGES), "python": python, "built_with": "uv" if uv else "venv",
            "built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}) + "\n", encoding="utf-8")
        status("Python for the console is ready.", False)
        return venv_python(root)


def add_client_sites(sites) -> None:
    """Let this worker import jupyter_client from the managed venv. Appended, so the worker's own
    modules keep precedence."""
    for site in sites:
        if site and site not in sys.path:
            sys.path.append(site)
    importlib.invalidate_caches()


def worker_has_client() -> bool:
    return importlib.util.find_spec("jupyter_client") is not None


def resolve(workspace, *, build: bool = False,
            status: Callable[[str, bool], None] = lambda text, building: None,
            worker_jupyter: Callable[[], bool] | None = None, run=subprocess.run, which=shutil.which,
            root: Path | None = None) -> KernelEnv:
    """The environment a workspace's kernel runs in, in the owner's order (module docstring)."""
    if worker_jupyter is None:
        from .py_kernel import jupyter_available as worker_jupyter
    managed = managed_python(root)

    def managed_or_build() -> tuple[Path | None, str]:
        if managed is not None or not build or os.environ.get("RELAY_PYTHON_PROVISION") == "off":
            return managed, ""
        try:
            return ensure_managed(status, which=which, run=run, root=root), ""
        except EnvError as exc:
            return None, str(exc)

    project = project_python(workspace, run)
    if project is not None:
        sites: tuple[str, ...] = ()
        helper, why = (None, "")
        if not worker_has_client():
            helper, why = managed_or_build()
            if helper is None:
                return KernelEnv("none", note=why or "this Python has no jupyter_client to reach the "
                                 "project's kernel with; a Python console builds one")
            sites = site_dirs(str(helper), run)
        console = project if has_modules(project, ("jupyter_console",), run) else \
            (str(helper or managed) if (helper or managed) else None)
        return KernelEnv("project", project, console, sites)
    if worker_jupyter():
        console = sys.executable if importlib.util.find_spec("jupyter_console") else \
            (str(managed) if managed else None)
        return KernelEnv("worker", None, console)
    python, why = managed_or_build()
    if python is None:
        return KernelEnv("none", note=why or "no Jupyter here yet: a Python console sets one up")
    return KernelEnv("managed", str(python), str(python), site_dirs(str(python), run))
