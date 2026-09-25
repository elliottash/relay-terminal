# SPDX-License-Identifier: AGPL-3.0-or-later
"""A pane's task-plugin workspace: its router, its runtime and its tools (#C0Q8 t:4h, t:9a).

`task_plugins` says what a plugin *is* and whether it may run. This module is what happens once a
pane has activated one (protocol 35):

Routing (t:4h). `WorkspaceManager.route` is the `route` message's front door. With no workspace
active and no language REPL in the foreground it returns None and the worker runs
`router.classify` exactly as before — the Bash router is untouched. With a Python or Stata
workspace active, or a Python/IPython/Stata REPL named by the request's `foreground_program`
(#33G0 stage 0), step 5 of classify (`bash -n` plus PATH lookup) is replaced by
`lang_router.classify_line`, which shares steps 1–4 with the Bash router (control characters,
`/shell` and `/agent`, the natural-language pattern, lone replies) and adds the typed `!`/`*`
forcing and the doubled `!!`/`**` escapes. The answer is still a `route` event, with
`route: "program"` (or `"incomplete"`) and the `language`, `destination` and `target` the mode
chip shows before anything is submitted. A foreground REPL wins over the workspace: it owns the
pty, so a program-bound line is typed there (`target: "repl"`), while a kernel workspace's goes
to its kernel (`target: "kernel"`). A TeX workspace routes like Bash and only labels the chip.

Runtimes (t:9a). Each active workspace owns its runtime, keyed by `workspace_id` (the pane's own
workspace is `"pane"`): a `KernelRuntime` wraps one `py_kernel.KernelSession`, a `TexRuntime` one
`tex_build.TexBuilder`. Nothing is shared between two workspace ids, and `deactivate` and
`shutdown` close them. The human's `kernel_run` lines and the agent's `py_run_cell` reach the same
session, so they share one namespace, and every record goes out as a `kernel_record` event in
the order the session appended it.

Tools (t:9a). `PluginTools` is the agent's view of one workspace: the plugin's tool group, offered
through `load_tools` like Relay's own deferred groups and only while that workspace has the
plugin active. A group's tools must be both declared by the manifest and implemented here; a
declared name Relay has no code for (the Stata group, until #MEPR decision 2 picks its bridge) is
reported as unavailable, never offered. The guest bridge offers the same specs to Claude Code and
Codex through `relay_board`, gated by the same object.
"""
from __future__ import annotations

import os
import queue
import shutil
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from . import lang_router
from .task_plugins import PluginError, PluginRegistry

DEFAULT_WORKSPACE = "pane"
MAX_WORKSPACE_ID = 128
#: Characters of one stream a tool result carries; the full record stays in the kernel history.
MAX_TOOL_TEXT = 20_000
MAX_EXPORT = 200_000
#: Environment every plugin runtime gets; the manifest's `runner.env` adds names to it, and
#: nothing else of the worker's environment (provider keys above all) reaches a kernel.
BASE_ENV = ("PATH", "HOME", "USER", "LOGNAME", "LANG", "LANGUAGE", "LC_ALL", "LC_CTYPE", "TZ",
            "TMPDIR", "TEMP", "TMP", "SYSTEMROOT", "WINDIR", "COMSPEC", "PATHEXT", "APPDATA",
            "LOCALAPPDATA", "USERPROFILE", "TERM", "SHELL", "XDG_CONFIG_HOME", "XDG_CACHE_HOME",
            "XDG_DATA_HOME", "XDG_RUNTIME_DIR")
#: Tools whose call can outlast the guest bridge's ordinary 30-second transport deadline.
LONG_TOOLS = ("py_run_cell", "tex_build")


def runtime_env(names=()) -> dict:
    env = {k: v for k, v in os.environ.items() if k in BASE_ENV or k.startswith("LC_")}
    for name in names:
        if name in os.environ:
            env[name] = os.environ[name]
    return env


def validate_workspace_id(value) -> str:
    if value is None or value == "":
        return DEFAULT_WORKSPACE
    if not isinstance(value, str) or len(value) > MAX_WORKSPACE_ID or not value.isprintable() \
            or not value.strip():
        raise ValueError(f"workspace_id must be printable text of at most {MAX_WORKSPACE_ID} characters.")
    return value


def _clip(text, limit: int = MAX_TOOL_TEXT):
    if not isinstance(text, str) or len(text) <= limit:
        return text
    return text[:limit] + f"\n… [{len(text) - limit} more characters; the kernel history keeps them]"


def _obj(args) -> dict:
    if not isinstance(args, dict):
        raise ValueError("Tool arguments must be an object.")
    return args


def _int(args: dict, key: str, default, lo: int, hi: int):
    value = args.get(key, default)
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not lo <= value <= hi:
        raise ValueError(f"{key} must be a number from {lo} to {hi}.")
    return value


def _spec(name: str, description: str, properties: dict, required=()) -> dict:
    return {"type": "function", "function": {
        "name": name, "description": description,
        "parameters": {"type": "object", "properties": properties, "required": list(required),
                       "additionalProperties": False}}}


# ----- The Python kernel runtime -------------------------------------------------------------------
PY_SPECS = [
    _spec("py_run_cell",
          "Run Python code in this workspace's kernel — the same namespace the user's composer "
          "runs in, so their variables are yours and yours are theirs. Cells run strictly in the "
          "order submitted. Say why in `intent`; the user sees it beside the cell.",
          {"code": {"type": "string", "description": "The cell's code."},
           "intent": {"type": "string", "description": "One sentence: what this cell is for."},
           "timeout_seconds": {"type": "number", "description": "Interrupt the cell after this many "
                               "seconds (default 300, at most 3600)."}},
          ("code", "intent")),
    _spec("py_interrupt", "Interrupt the cell running in the workspace's kernel (KeyboardInterrupt).", {}),
    _spec("py_restart",
          "Restart the kernel. Every variable, import and definition — the user's too — is gone; "
          "ask before doing this unless the user asked for it.",
          {"intent": {"type": "string", "description": "Why the restart is needed."}}),
    _spec("py_variables", "List the kernel's variables with type, shape and a short preview.", {}),
    _spec("py_history",
          "The session's cells in the order they ran, human and agent: code, output, value, "
          "error and timing.",
          {"since_seq": {"type": "integer", "description": "Only records after this sequence number."},
           "limit": {"type": "integer", "description": "At most this many records, newest last (default 20)."}}),
    _spec("py_export",
          "The session as a runnable `# %%` Python script (failed cells commented out) or as JSON.",
          {"format": {"type": "string", "enum": ["script", "json"], "description": "Default script."}}),
]


def _has_module(name: str) -> bool:
    import importlib.util
    try:
        return importlib.util.find_spec(name) is not None
    except (ImportError, ValueError):
        return False


def console_command(console, package_root, connection_file: str | None, which=shutil.which,
                    has_module=_has_module, python: str | None = None) -> dict:
    """What a Python console pane runs in its pty (#83YV): `{argv, program, label, shared,
    connection_file, startup, note}`.

    With a kernel connection file and jupyter_console reachable, it is the manifest's
    `console.program` (`jupyter console --existing {connection_file}`) with the startup file as its
    `--config`: the pty is one more client of the workspace kernel, so what the person types and
    what the agent's py_run_cell runs share one namespace (`shared`). `jupyter` on PATH is used
    when it has the console subcommand; otherwise this worker's own Python runs
    `-m jupyter_console`. Without either — or without a connection file, when the kernel runs on
    the stdlib server — the fallback is plain `ipython` with the startup file, then `python3`:
    a REPL of its own (`shared: false`), which the agent reaches by typing into the pty."""
    import sys
    python = python or sys.executable
    startup = None
    if console is not None and console.startup and package_root is not None:
        path = Path(package_root) / console.startup
        startup = str(path) if path.is_file() else None
    program = list(console.program) if console is not None else []
    if connection_file and program:
        argv = [connection_file if word == "{connection_file}" else word for word in program]
        head = None
        if argv[:2] == ["jupyter", "console"]:
            if which("jupyter") and which("jupyter-console"):
                head = [which("jupyter"), "console"]
            elif has_module("jupyter_console"):
                head = [python, "-m", "jupyter_console"]
            rest = argv[2:]
        elif which(argv[0]):
            head, rest = [which(argv[0])], argv[1:]
        if head is not None:
            if startup and head[-1] in ("console", "jupyter_console"):
                rest = rest + ["--config=" + startup]
            return {"argv": head + rest, "program": "jupyter-console", "label": "Python · ipython",
                    "shared": True, "connection_file": connection_file, "startup": startup, "note": ""}
    why = ("the kernel runs on the stdlib server (install ipykernel and jupyter_client for a shared one)"
           if not connection_file else "jupyter_console is not installed (pip install jupyter-console)")
    ipython = which("ipython") or which("ipython3")
    if ipython:
        argv = [ipython] + (["--InteractiveShellApp.exec_files=" + startup] if startup else [])
        return {"argv": argv, "program": "ipython", "label": "Python · ipython", "shared": False,
                "connection_file": connection_file, "startup": startup,
                "note": f"This IPython has its own namespace: {why}. The agent's py_* tools run in the "
                        "workspace kernel, not here."}
    plain = which("python3") or which("python") or python
    return {"argv": [plain], "program": "python", "label": "Python · python", "shared": False,
            "connection_file": connection_file, "startup": None,
            "note": f"This Python has its own namespace: {why}, and IPython is not installed "
                    "(pip install ipython)."}


class KernelRuntime:
    """One `KernelSession` for one workspace. Human lines run on a FIFO thread of their own, so a
    burst of `kernel_run` messages keeps its order without blocking the worker's loop."""

    kind = "kernel"
    group = "py"
    specs = PY_SPECS

    def __init__(self, workspace_id: str, workspace_dir: str, manifest, dependencies, emit,
                 path: str | None = None, session_factory=None):
        from . import py_kernel
        self.workspace_id = workspace_id
        self.emit = emit
        self.plugin_id = getattr(manifest, "id", None)
        self.console = getattr(manifest, "console", None)
        self.package_root = getattr(manifest, "root", None)
        env = runtime_env(manifest.runner.env if manifest.runner else ())
        if session_factory is not None:
            self.session = session_factory(cwd=workspace_dir, env=env, on_output=self._output,
                                           on_record=self._record)
        else:
            # Jupyter (ipykernel, IPython syntax) when this Relay's Python can reach it; otherwise
            # the stdlib server under the user's own `python3`, the one `requires` resolved — so
            # ipykernel is an upgrade, never a requirement.
            jupyter = py_kernel.jupyter_available()
            python = None if jupyter else next(
                (d.get("path") for d in dependencies if d.get("program") in ("python3", "python")
                 and d.get("path")), None)
            self.session = py_kernel.KernelSession("auto" if jupyter else "subprocess", cwd=workspace_dir,
                                                   env=env, python=python, on_output=self._output,
                                                   on_record=self._record)
        self.names: frozenset[str] = frozenset()
        self._jobs: queue.Queue = queue.Queue()
        self._thread = threading.Thread(target=self._serve, name=f"relay-kernel-{workspace_id}", daemon=True)
        self._thread.start()

    # -- routing ---------------------------------------------------------------------------------
    @property
    def language(self) -> str:
        """`ipython` once the session runs on ipykernel (magics, `!cmd`, `name?`), else `python`."""
        backend = getattr(self.session, "_backend", None)
        # Before the first cell there is no backend yet: "auto" was chosen only because Jupyter is
        # there, so it will be ipykernel.
        name = getattr(backend, "name", None) or getattr(self.session, "_backend_choice", "")
        return "ipython" if name in ("jupyter", "auto") else "python"

    def info(self) -> dict:
        try:
            return dict(self.session.info())
        except Exception:
            return {}

    # -- events ----------------------------------------------------------------------------------
    def _output(self, seq: int, stream: str, text: str) -> None:
        self.emit({"event": "kernel_output", "workspace_id": self.workspace_id, "seq": seq,
                   "stream": stream, "text": text})

    def _record(self, record: dict) -> None:
        self.emit({"event": "kernel_record", "workspace_id": self.workspace_id, "record": record})

    def refresh_names(self, request_id=None) -> list[dict] | None:
        """Re-read the namespace after a cell: the router's names, and the Variables pane."""
        try:
            variables = self.session.variables(timeout=5)
        except Exception:
            return None
        self.names = frozenset(v.get("name") for v in variables if isinstance(v, dict) and v.get("name"))
        event = {"event": "kernel_variables", "workspace_id": self.workspace_id, "variables": variables}
        if request_id is not None:
            event["id"] = request_id
        self.emit(event)
        return variables

    # -- the human's side --------------------------------------------------------------------------
    def submit(self, code: str, request_id=None) -> None:
        if not isinstance(code, str) or not code.strip():
            raise ValueError("kernel_run needs code.")
        self._jobs.put(("run", code, request_id))

    def submit_restart(self, request_id=None) -> None:
        self._jobs.put(("restart", "", request_id))

    def submit_variables(self, request_id=None) -> None:
        self._jobs.put(("variables", "", request_id))

    def submit_console(self, request_id=None) -> None:
        """Start the kernel now and answer `workspace_console` with the program a Python console
        pane runs in its pty (#83YV). On the FIFO thread: a kernel takes seconds to start, and
        the worker's loop must not wait for it."""
        self._jobs.put(("console", "", request_id))

    def console_answer(self) -> dict:
        self.session.start()
        info = self.info()
        return {"workspace_id": self.workspace_id, "plugin_id": self.plugin_id,
                **console_command(self.console, self.package_root, info.get("connection_file")),
                "runtime": {"kind": self.kind, **info}}

    def _serve(self) -> None:
        while True:
            job = self._jobs.get()
            if job is None:
                return
            kind, code, request_id = job
            try:
                if kind == "run":
                    record = self.session.run_cell(code, origin="human")
                    self.emit({"event": "kernel_ran", "id": request_id, "workspace_id": self.workspace_id,
                               "seq": record.get("seq"), "status": record.get("status")})
                    self.refresh_names()
                elif kind == "console":
                    self.emit({"event": "workspace_console", "id": request_id, **self.console_answer()})
                elif kind == "restart":
                    record = self.session.restart(origin="human")
                    self.names = frozenset()
                    self.emit({"event": "kernel_ran", "id": request_id, "workspace_id": self.workspace_id,
                               "seq": record.get("seq"), "status": record.get("status")})
                else:
                    self.refresh_names(request_id)
            except Exception as exc:
                self.emit({"event": "error", "id": request_id, "workspace_id": self.workspace_id,
                           "text": f"Python kernel: {exc}"[:2000]})

    def interrupt(self) -> bool:
        return self.session.interrupt()

    # -- the agent's side --------------------------------------------------------------------------
    def writes(self, name: str, args: dict) -> bool:
        return name in ("py_run_cell", "py_restart", "py_interrupt")

    def preview(self, name: str, args: dict) -> str:
        if name == "py_run_cell":
            return f"PYTHON CELL\n\n{args.get('intent') or ''}\n\n{args.get('code') or ''}".rstrip()
        return name.replace("_", " ").upper()

    def run(self, name: str, args: dict, cancel: threading.Event | None = None) -> dict:
        if name == "py_run_cell":
            code, intent = args.get("code"), args.get("intent")
            if not isinstance(code, str) or not code.strip():
                raise ValueError("code is required.")
            if not isinstance(intent, str) or not intent.strip():
                raise ValueError("intent is required: one sentence saying what the cell is for.")
            timeout = _int(args, "timeout_seconds", 300, 1, 3600)
            record = self._run_cancellable(code, intent.strip(), timeout, cancel)
            self.refresh_names()
            return self._tool_record(record)
        if name == "py_interrupt":
            return {"interrupted": self.session.interrupt()}
        if name == "py_restart":
            intent = args.get("intent")
            record = self.session.restart(origin="agent", intent=intent if isinstance(intent, str) else None)
            self.names = frozenset()
            return self._tool_record(record)
        if name == "py_variables":
            variables = self.refresh_names()
            if variables is None:
                raise ValueError("The kernel is busy running earlier cells; try again when they finish.")
            return {"variables": variables}
        if name == "py_history":
            limit = _int(args, "limit", 20, 1, 500)
            since = _int(args, "since_seq", 0, 0, 10 ** 9)
            records = [r for r in self.session.history() if r.get("seq", 0) > since][-int(limit):]
            return {"records": [self._tool_record(r) for r in records]}
        if name == "py_export":
            fmt = args.get("format", "script")
            if fmt not in ("script", "json"):
                raise ValueError("format must be script or json.")
            text = self.session.export(fmt)
            return {"format": fmt, "text": _clip(text, MAX_EXPORT), "truncated": len(text) > MAX_EXPORT}
        raise ValueError(f"Unknown Python workspace tool {name}.")

    def _run_cancellable(self, code, intent, timeout, cancel):
        """run_cell on a helper thread, so Stop interrupts the cell instead of waiting it out."""
        box: dict = {}

        def work():
            try:
                box["record"] = self.session.run_cell(code, origin="agent", intent=intent, timeout=timeout)
            except BaseException as exc:          # handed to the caller below
                box["error"] = exc

        thread = threading.Thread(target=work, name=f"relay-kernel-agent-{self.workspace_id}", daemon=True)
        thread.start()
        interrupted = False
        while thread.is_alive():
            thread.join(0.1)
            if cancel is not None and cancel.is_set() and not interrupted:
                interrupted = True
                self.session.interrupt()
        if "error" in box:
            raise box["error"]
        return box["record"]

    @staticmethod
    def _tool_record(record: dict) -> dict:
        out = {k: record.get(k) for k in ("seq", "kind", "origin", "intent", "status", "result_repr",
                                          "result_type", "error", "duration", "backend")}
        out["code"] = _clip(record.get("code") or "", 4000)
        out["stdout"] = _clip(record.get("stdout") or "")
        out["stderr"] = _clip(record.get("stderr") or "")
        if record.get("displays"):
            out["displays"] = len(record["displays"])
        if record.get("kind") == "restart":
            out["message"], out["lost_names"] = record.get("message"), record.get("lost_names")
        return out

    def close(self) -> None:
        self._jobs.put(None)
        try:
            self.session.shutdown()
        except Exception:
            pass


# ----- The TeX document runtime --------------------------------------------------------------------
TEX_SPECS = [
    _spec("tex_build",
          "Build the workspace's main .tex with latexmk now (`action: build`, waits for the result), "
          "or report the build state without building (`action: status`): state, the published "
          "generation and the source revision it was built from, and whether the PDF is current.",
          {"action": {"type": "string", "enum": ["build", "status"], "description": "Default build."},
           "wait_seconds": {"type": "number", "description": "How long a build may be waited for "
                            "(default 120, at most 600); a longer one keeps running and "
                            "`action: status` reports it."}}),
    _spec("tex_diagnostics", "The last build's errors and warnings as file:line rows.",
          {"severity": {"type": "string", "enum": ["error", "warning", "all"], "description": "Default all."},
           "limit": {"type": "integer", "description": "At most this many rows (default 50)."}}),
    _spec("tex_forward_search", "Where a source line lands in the current PDF (SyncTeX): page and position.",
          {"file": {"type": "string", "description": "Source file, absolute or relative to the document's folder."},
           "line": {"type": "integer", "description": "1-based line."},
           "column": {"type": "integer", "description": "0-based column (optional)."}},
          ("file", "line")),
    _spec("tex_inverse_search", "The source file and line behind a point in the current PDF (SyncTeX).",
          {"page": {"type": "integer", "description": "1-based page."},
           "x": {"type": "number", "description": "PDF points from the page's left edge."},
           "y": {"type": "number", "description": "PDF points from the page's top edge."}},
          ("page", "x", "y")),
    _spec("tex_dependencies", "The main file and every file it pulls in (inputs, includes, bibliography, "
          "graphics), in document order, with whether each exists.", {}),
]


class TexRuntime:
    kind = "tex"
    group = "tex"
    specs = TEX_SPECS
    language = "tex"

    def __init__(self, workspace_id: str, workspace_dir: str, manifest, dependencies, emit,
                 path: str | None = None, builder_factory=None):
        from . import tex_build
        self.workspace_id = workspace_id
        self.emit = emit
        target = path or workspace_dir
        root = tex_build.detect_root(target)
        if root is None:
            raise PluginError(f"no main .tex file found for {target}; activate the TeX workspace with "
                              "`path` naming the document.")
        self.root = root
        make = builder_factory or tex_build.TexBuilder
        self.builder = make(root.root_dir, root.main_file, engine=root.engine or "pdf",
                            on_state=self._state)

    names: frozenset[str] = frozenset()

    def info(self) -> dict:
        return {"main_file": self.root.main_file, "root_dir": self.root.root_dir,
                "engine": self.builder.engine, "reason": self.root.reason}

    def _state(self, status) -> None:
        self.emit({"event": "tex_status", "workspace_id": self.workspace_id, "status": status.to_dict()})

    def writes(self, name: str, args: dict) -> bool:
        return name == "tex_build" and args.get("action", "build") == "build"

    def preview(self, name: str, args: dict) -> str:
        if name == "tex_build":
            return f"TEX BUILD\n\n{self.root.main_file}"
        return name.replace("_", " ").upper()

    def status(self) -> dict:
        status = self.builder.status
        try:
            revision = self.builder.source_revision()
        except OSError:
            revision = None
        gen = status.generation
        errors = sum(1 for d in status.diagnostics if d.severity == "error")
        return {"state": status.state, "seq": status.seq, "building": status.building,
                "main_file": self.root.main_file, "engine": self.builder.engine,
                "source_revision": revision,
                "current": bool(gen is not None and gen.consistent and gen.revision == revision),
                "generation": None if gen is None else {
                    "id": gen.id, "revision": gen.revision, "pdf": gen.pdf, "pages": gen.pages,
                    "published_at": gen.published_at, "consistent": gen.consistent},
                "failed": None if status.failed is None else {
                    "id": status.failed.id, "revision": status.failed.revision,
                    "returncode": status.failed.returncode, "reason": status.failed.reason,
                    "log": status.failed.log},
                "errors": errors, "warnings": len(status.diagnostics) - errors}

    def run(self, name: str, args: dict, cancel: threading.Event | None = None) -> dict:
        from . import tex_build
        if name == "tex_build":
            action = args.get("action", "build")
            if action not in ("build", "status"):
                raise ValueError("action must be build or status.")
            if action == "status":
                return self.status()
            wait = _int(args, "wait_seconds", 120, 0, 600)
            self.builder.request_build(debounce=0)
            deadline = time.monotonic() + wait
            finished = False
            while time.monotonic() < deadline:
                if cancel is not None and cancel.is_set():
                    self.builder.cancel()
                    break
                try:
                    self.builder.wait_idle(timeout=min(0.25, max(0.01, deadline - time.monotonic())))
                    finished = True
                    break
                except TimeoutError:
                    continue
            out = self.status()
            if not finished:
                out["note"] = "Still building; call tex_build with action status to see the result."
            return out
        if name == "tex_diagnostics":
            severity = args.get("severity", "all")
            if severity not in ("error", "warning", "all"):
                raise ValueError("severity must be error, warning or all.")
            limit = int(_int(args, "limit", 50, 1, 1000))
            status = self.builder.status
            rows = [d.to_dict() for d in status.diagnostics if severity == "all" or d.severity == severity]
            return {"state": status.state, "diagnostics": rows[:limit], "total": len(rows)}
        if name == "tex_forward_search":
            file, line = args.get("file"), args.get("line")
            if not isinstance(file, str) or not file:
                raise ValueError("file is required.")
            if isinstance(line, bool) or not isinstance(line, int) or line < 1:
                raise ValueError("line must be a positive integer.")
            column = int(_int(args, "column", 0, 0, 100000))
            path = file if os.path.isabs(file) else os.path.join(self.root.root_dir, file)
            try:
                found = self.builder.forward_search(os.path.normpath(path), line, column)
            except tex_build.TexError as exc:
                raise ValueError(str(exc)) from exc
            return {"locations": [loc.to_dict() for loc in found]}
        if name == "tex_inverse_search":
            page = args.get("page")
            if isinstance(page, bool) or not isinstance(page, int) or page < 1:
                raise ValueError("page must be a positive integer.")
            x, y = _int(args, "x", None, 0, 100000), _int(args, "y", None, 0, 100000)
            if x is None or y is None:
                raise ValueError("x and y are required.")
            try:
                found = self.builder.inverse_search(page, float(x), float(y))
            except tex_build.TexError as exc:
                raise ValueError(str(exc)) from exc
            return {"location": None if found is None else found.to_dict()}
        if name == "tex_dependencies":
            return {"main_file": self.root.main_file,
                    "dependencies": [d.to_dict() for d in self.builder.dependencies()]}
        raise ValueError(f"Unknown TeX workspace tool {name}.")

    def close(self) -> None:
        try:
            self.builder.close()
        except Exception:
            pass


#: The plugins Relay has tool code for: id -> runtime class. The group and the names a runtime
#: implements are its own; the manifest decides which of them this plugin offers.
RUNTIMES = {"relay.python": KernelRuntime, "relay.tex": TexRuntime}


# ----- The workspaces ------------------------------------------------------------------------------
@dataclass
class Workspace:
    id: str
    directory: str
    plugin_id: str
    state: dict                    # ActivationState.to_dict()
    language: str                  # the manifest's router language
    runner_kind: str | None
    runtime: object | None = None
    path: str | None = None

    def offered(self) -> tuple[str, str, list[dict], list[str]]:
        """(group, what, specs, unavailable names) this workspace gives its agent."""
        tools = self.state.get("tools") or {}
        declared = {item["name"]: item["description"] for item in tools.get("items", [])}
        runtime = self.runtime
        if runtime is None or not declared or tools.get("group") != getattr(runtime, "group", None):
            return "", "", [], sorted(declared)
        specs = [spec for spec in runtime.specs if spec["function"]["name"] in declared]
        have = {spec["function"]["name"] for spec in specs}
        what = f"work in this pane's {self.state.get('plugin_id')} workspace"
        return runtime.group, what, specs, sorted(set(declared) - have)


class WorkspaceManager:
    """Every active workspace of one worker, keyed by workspace id. Thread-safe."""

    def __init__(self, emit: Callable[[dict], None], registry: PluginRegistry | None = None,
                 on_change: Callable[[str], None] | None = None, runtimes: dict | None = None):
        self.emit = emit
        self.registry = registry or PluginRegistry()
        self.on_change = on_change
        self.runtimes = dict(RUNTIMES if runtimes is None else runtimes)
        self._lock = threading.RLock()
        self._spaces: dict[str, Workspace] = {}

    def get(self, workspace_id: str = DEFAULT_WORKSPACE) -> Workspace | None:
        with self._lock:
            return self._spaces.get(workspace_id)

    # -- lifecycle ---------------------------------------------------------------------------------
    def activate(self, directory: str, plugin_id: str, workspace_id: str = DEFAULT_WORKSPACE,
                 path: str | None = None) -> dict:
        if not isinstance(plugin_id, str) or not plugin_id:
            raise ValueError("workspace_activate needs plugin_id.")
        if not isinstance(directory, str) or not os.path.isdir(directory):
            raise ValueError("workspace_activate needs an existing workspace directory.")
        if path is not None and (not isinstance(path, str) or not os.path.exists(path)):
            raise ValueError("path must name an existing file.")
        workspace_id = validate_workspace_id(workspace_id)
        directory = str(Path(directory).expanduser().resolve())
        with self._lock:
            previous = self._spaces.get(workspace_id)
            if previous is not None and previous.plugin_id == plugin_id and previous.directory == directory \
                    and previous.path == path:
                # Activating what is already active keeps its runtime: a kernel's state survives.
                return self._describe(previous, "activated")
            state = self.registry.activate(directory, plugin_id, tab=workspace_id).to_dict()
            record = self.registry.record(directory, plugin_id)
            manifest = record.manifest
            runtime = None
            runtime_class = self.runtimes.get(plugin_id)
            try:
                if runtime_class is not None and manifest.tools is not None:
                    runtime = runtime_class(workspace_id, directory, manifest, state["dependencies"],
                                            self.emit, path=path)
            except Exception:
                self.registry.deactivate(directory, tab=workspace_id)
                if previous is not None:
                    # The failed activation must not strand the old one half-replaced.
                    self._spaces.pop(workspace_id, None)
                    self._close(previous)
                raise
            if previous is not None:
                self._close(previous)
            space = Workspace(workspace_id, directory, plugin_id, state, manifest.router.language,
                              manifest.runner.kind if manifest.runner else None, runtime, path)
            self._spaces[workspace_id] = space
        self._changed(workspace_id)
        return self._describe(space, "activated")

    def deactivate(self, workspace_id: str = DEFAULT_WORKSPACE) -> dict:
        workspace_id = validate_workspace_id(workspace_id)
        with self._lock:
            space = self._spaces.pop(workspace_id, None)
            directory = space.directory if space is not None else None
        if space is not None:
            self._close(space)
            state = self.registry.deactivate(directory, tab=workspace_id).to_dict()
        else:
            state = {"workspace": None, "tab": workspace_id, "plugin_id": None, "state": "inactive",
                     "router": {"language": "bash", "prefixes": []}, "runner": None, "tools": None,
                     "notes": ["nothing was active"]}
        self._changed(workspace_id)
        return {"workspace_id": workspace_id, "change": "deactivated", "state": state,
                "tools": {"group": None, "offered": [], "unavailable": []}, "runtime": None}

    def describe(self, workspace_id: str = DEFAULT_WORKSPACE) -> dict:
        space = self.get(validate_workspace_id(workspace_id))
        if space is None:
            return {"workspace_id": workspace_id, "change": "state", "state": None,
                    "tools": {"group": None, "offered": [], "unavailable": []}, "runtime": None}
        return self._describe(space, "state")

    def _describe(self, space: Workspace, change: str) -> dict:
        group, _what, specs, unavailable = space.offered()
        runtime = space.runtime
        return {"workspace_id": space.id, "change": change, "state": space.state,
                "tools": {"group": group or None, "offered": [s["function"]["name"] for s in specs],
                          "unavailable": unavailable},
                "runtime": None if runtime is None else {"kind": runtime.kind, **runtime.info()}}

    def _close(self, space: Workspace) -> None:
        if space.runtime is not None:
            space.runtime.close()

    def _changed(self, workspace_id: str) -> None:
        if self.on_change is not None:
            try:
                self.on_change(workspace_id)
            except Exception:
                pass

    def shutdown(self) -> None:
        with self._lock:
            spaces = list(self._spaces.values())
            self._spaces.clear()
        for space in spaces:
            self._close(space)

    def candidates(self, directory: str | None, path: str | None = None, foreground_program=None) -> list[dict]:
        return [c.to_dict() for c in self.registry.select_for(path, foreground_program, workspace=directory)]

    # -- the wire (protocol 36) --------------------------------------------------------------------
    TYPES = frozenset({"workspace_activate", "workspace_deactivate", "workspace_state", "workspace_candidates",
                       "kernel_run", "kernel_interrupt", "kernel_restart", "kernel_variables"})

    def handles(self, kind) -> bool:
        return kind in self.TYPES

    def dispatch(self, request: dict, directory: str | None) -> None:
        """One protocol-36 message. `directory` is the pane's configured workspace, used when the
        message names none. Errors are raised for the worker's ordinary `error` event."""
        kind, request_id = request.get("type"), request.get("id")
        workspace_id = validate_workspace_id(request.get("workspace_id"))
        asked = request.get("workspace")
        if asked is not None and not isinstance(asked, str):
            raise ValueError("workspace must be a directory path.")
        folder = asked or directory
        if kind == "workspace_activate":
            if not folder:
                raise ValueError("Configure a workspace, or name one, before activating a task plugin.")
            console = request.get("console", False)
            if not isinstance(console, bool):
                raise ValueError("console must be true or false.")
            if console and getattr(self.runtimes.get(request.get("plugin_id")), "kind", None) != "kernel":
                # Refused before activating, so a refusal changes nothing.
                raise ValueError("console: true needs a kernel plugin (relay.python).")
            result = self.activate(folder, request.get("plugin_id"), workspace_id, request.get("path"))
            if console:
                runtime = self.kernel(workspace_id)
                result["console"] = {"state": "starting"}
            self.emit({"event": "workspace_state", "id": request_id, **result})
            if console:
                runtime.submit_console(request_id)
        elif kind == "workspace_deactivate":
            self.emit({"event": "workspace_state", "id": request_id, **self.deactivate(workspace_id)})
        elif kind == "workspace_state":
            self.emit({"event": "workspace_state", "id": request_id, **self.describe(workspace_id)})
        elif kind == "workspace_candidates":
            path = request.get("path")
            if path is not None and not isinstance(path, str):
                raise ValueError("path must be text.")
            self.emit({"event": "workspace_candidates", "id": request_id, "candidates": self.candidates(
                folder, path, _foreground(request.get("foreground_program")))})
        elif kind == "kernel_run":
            self.kernel(workspace_id).submit(request.get("code"), request_id)
        elif kind == "kernel_interrupt":
            self.emit({"event": "kernel_interrupted", "id": request_id, "workspace_id": workspace_id,
                       "interrupted": self.kernel(workspace_id).interrupt()})
        elif kind == "kernel_restart":
            self.kernel(workspace_id).submit_restart(request_id)
        elif kind == "kernel_variables":
            self.kernel(workspace_id).submit_variables(request_id)
        else:
            raise ValueError("Unknown workspace message.")

    # -- the kernel, from the composer -------------------------------------------------------------
    def kernel(self, workspace_id: str = DEFAULT_WORKSPACE) -> KernelRuntime:
        space = self.get(validate_workspace_id(workspace_id))
        runtime = space.runtime if space is not None else None
        if not isinstance(runtime, KernelRuntime) and getattr(runtime, "kind", None) != "kernel":
            raise ValueError(f"workspace {workspace_id!r} has no kernel; activate a kernel plugin first.")
        return runtime

    # -- routing (t:4h) ----------------------------------------------------------------------------
    def route(self, request: dict) -> dict | None:
        """The `route` event's fields when a workspace or a foreground REPL decides; None when the
        Bash router should, exactly as before (then `annotate` adds the workspace's label)."""
        workspace_id = validate_workspace_id(request.get("workspace_id"))
        repl = lang_router.detect_repl(_foreground(request.get("foreground_program")))
        space = self.get(workspace_id)
        if repl is not None:
            language, target, names = repl, "repl", ()
        elif space is not None and space.language in ("python", "stata"):
            runtime = space.runtime
            if space.runner_kind == "kernel" and runtime is not None:
                language, target, names = runtime.language, "kernel", runtime.names
            else:
                language, target, names = space.language, "repl", ()
        else:
            return None
        decision = lang_router.classify_line(request.get("text", ""), language, request.get("mode", "auto"),
                                             names=names)
        route = decision.destination
        fields = {"route": route, "text": decision.normalized_text, "reason": decision.reason,
                  "syntax_ok": not decision.syntax_error, "syntax_error": decision.syntax_error,
                  "valid": route != "agent", "invalid_reason": decision.syntax_error if route == "agent" else "",
                  "needs_assist": False, "assist_reason": "", "agent_signal": route == "agent" and not decision.forced,
                  "explain_invalid": False,
                  "destination": route, "language": language, "forced": decision.forced,
                  "target": target if route == "program" else ("terminal" if route == "shell" else route),
                  "workspace_id": workspace_id}
        if space is not None:
            fields["plugin_id"] = space.plugin_id
        if repl is not None:
            fields["repl"] = True
        return fields

    def annotate(self, decision: dict, request: dict) -> dict:
        """A Bash decision in a workspace that routes like Bash (TeX): the chip's label rides along.
        With no workspace the decision is returned untouched, byte for byte."""
        space = self.get(validate_workspace_id(request.get("workspace_id")))
        if space is None:
            return decision
        route = decision.get("route")
        return {**decision, "destination": route, "language": space.language, "forced": False,
                "target": "terminal" if route == "shell" else route, "workspace_id": space.id,
                "plugin_id": space.plugin_id}


def _foreground(value):
    if value is None or value == "":
        return None
    if isinstance(value, str):
        if len(value) > 4096:
            raise ValueError("foreground_program is too long.")
        return value
    if isinstance(value, list) and len(value) <= 256 and all(isinstance(v, str) and len(v) <= 4096 for v in value):
        return value
    raise ValueError("foreground_program must be a command line or an argv list.")


# ----- The agent's view ----------------------------------------------------------------------------
class PluginTools:
    """What one agent may call: the tool group of the workspace it lives in, and nothing else."""

    def __init__(self, manager: WorkspaceManager, workspace_id: str = DEFAULT_WORKSPACE):
        self.manager = manager
        self.workspace_id = validate_workspace_id(workspace_id)

    def _space(self) -> Workspace | None:
        return self.manager.get(self.workspace_id)

    def groups(self) -> dict[str, tuple[tuple[str, ...], str, list[dict]]]:
        """{group: (names, what it is for, specs)} for the active workspace; {} when none."""
        space = self._space()
        if space is None:
            return {}
        group, what, specs, _missing = space.offered()
        if not specs:
            return {}
        return {group: (tuple(s["function"]["name"] for s in specs), what, specs)}

    def group_of(self, name: str) -> str | None:
        for group, (names, _what, _specs) in self.groups().items():
            if name in names:
                return group
        return None

    def specs(self) -> list[dict]:
        return [spec for _names, _what, specs in self.groups().values() for spec in specs]

    def handles(self, name: str) -> bool:
        return self.group_of(name) is not None

    def inactive_refusal(self, name: str) -> str | None:
        """A sentence for a plugin tool called where its workspace is not active; None if `name`
        is no plugin tool at all."""
        for plugin_id, runtime in self.manager.runtimes.items():
            if any(spec["function"]["name"] == name for spec in runtime.specs):
                return (f"{name} belongs to the {plugin_id} workspace, which is not active in this "
                        "pane; the user activates it from the tab's workspace menu.")
        return None

    def writes(self, name: str, args: dict) -> bool:
        space = self._space()
        return bool(space and space.runtime and space.runtime.writes(name, args))

    def preview(self, name: str, args: dict) -> str:
        space = self._space()
        return space.runtime.preview(name, args) if space and space.runtime else name

    def run(self, name: str, args: dict, cancel: threading.Event | None = None) -> dict:
        space = self._space()
        if space is None or space.runtime is None or not self.handles(name):
            raise ValueError(self.inactive_refusal(name) or f"Unknown tool {name}.")
        return space.runtime.run(name, _obj(args), cancel)
