# SPDX-License-Identifier: AGPL-3.0-or-later
"""A workspace-owned, persistent Python execution session shared by the human and the agent (#33G0).

One `KernelSession` per Python workspace. The composer's statements, `# %%` script cells and the
agent's `run_cell` tool all call `run_cell` on the same session, so they share one namespace: a
variable the human defines is there for the agent's next cell. Calls are served strictly in the
order they were submitted (a ticket queue, not a bare lock, which would not be fair), and every
call leaves an ordered record — who ran it, the agent's stated intent, the code, its stdout,
stderr, value and traceback, and how long it took.

Two backends behind one interface:

  SubprocessBackend  the default and the no-dependency path: a small stdlib-only Python server in
                     a child process (source below, run with `python -c`), JSON lines over two
                     private pipes. It runs a cell the way IPython does — statements, then the
                     value of a final expression — captures stdout and stderr at the file-
                     descriptor level (so output from C extensions and child processes is kept),
                     and is interrupted with SIGINT (CTRL_BREAK_EVENT on Windows).
  JupyterBackend     used when jupyter_client and a kernel spec are installed: the real Jupyter
                     protocol against ipykernel, so IPython syntax (%magic, !cmd, name?) works.

What is never recorded: the text typed in answer to `input()` or `getpass()`. The prompt is shown,
the answer is passed to the program and dropped, so `history()` and `export()` cannot leak it.

`restart()` makes state loss explicit: it appends a record saying the namespace is gone and which
names were in it; a kernel that dies gets the same record before the next cell runs.
"""
from __future__ import annotations

import ast
import copy
import datetime as _dt
import glob
import importlib.util
import json
import os
import queue
import re
import secrets
import shutil
import signal
import subprocess
import sys
import threading
import time
from contextlib import contextmanager
from typing import Callable

ORIGINS = ("human", "agent")
# A cell's status: ok, error, interrupted, timeout (interrupted by run_cell's own deadline), died
# (the kernel process exited under it), restarted (restart() killed it), cancelled (queued behind
# a restart and never run). A restart record's status is "restarted".
STATUSES = ("ok", "error", "interrupted", "timeout", "died", "restarted", "cancelled")
MAX_CAPTURE = 1_000_000        # characters of stdout/stderr kept per cell
MAX_REPR = 20_000
STATE_LOST = "Kernel restarted: every variable, import and definition from earlier cells is gone."


class KernelError(RuntimeError):
    """The kernel could not be started or stopped answering."""


class KernelBusy(KernelError):
    """A call waited longer than its timeout for its turn."""


# ----- Introspection, shared by both backends ----------------------------------------------------
# Runs inside the kernel: the child server exec()s it into its own globals, the Jupyter backend into
# the user namespace (every name starts with `_`, so variables() does not list them).
INTROSPECT_SOURCE = r'''
import json as _relay_json_mod
import reprlib as _relay_reprlib
import types as _relay_types

_RELAY_HIDDEN = {"In", "Out", "exit", "quit", "get_ipython", "open"}
_relay_short = _relay_reprlib.Repr()
_relay_short.maxstring = 80
_relay_short.maxother = 80
_relay_short.maxlist = _relay_short.maxtuple = _relay_short.maxdict = _relay_short.maxset = 6


def _relay_type_name(value):
    t = type(value)
    module = getattr(t, "__module__", "") or ""
    if module == "builtins":
        return t.__name__
    return module.split(".")[0] + "." + t.__name__


def _relay_describe(name, value):
    info = {"name": name, "type": _relay_type_name(value)}
    try:
        shape = getattr(value, "shape", None)
        if isinstance(shape, tuple) and all(isinstance(n, int) for n in shape):
            info["shape"] = list(shape)
        kind = type(value).__name__
        if kind == "DataFrame" and "shape" in info:
            columns = [str(c) for c in list(value.columns)[:12]]
            more = ", ..." if len(value.columns) > 12 else ""
            info["summary"] = "%d rows x %d columns: %s%s" % (shape[0], shape[1], ", ".join(columns), more)
        elif kind == "Series" and "shape" in info:
            info["summary"] = "%d values, dtype %s" % (shape[0], value.dtype)
        elif kind == "ndarray" and "shape" in info:
            info["summary"] = "array %s %s" % ("x".join(map(str, shape)) or "scalar", value.dtype)
        elif isinstance(value, (bool, int, float, complex, str, bytes, type(None))):
            info["summary"] = _relay_short.repr(value)
        elif isinstance(value, (list, tuple, set, frozenset, dict)):
            info["length"] = len(value)
            info["summary"] = "%d items: %s" % (len(value), _relay_short.repr(value))
        elif isinstance(value, type):
            info["summary"] = "class " + value.__qualname__
        elif isinstance(value, (_relay_types.FunctionType, _relay_types.BuiltinFunctionType)):
            try:
                import inspect
                info["summary"] = value.__qualname__ + str(inspect.signature(value))
            except (TypeError, ValueError):
                info["summary"] = value.__qualname__ + "(...)"
        else:
            info["summary"] = _relay_short.repr(value)
    except Exception as exc:          # a broken __repr__ or property must not break the listing
        info["summary"] = "<unavailable: %s>" % type(exc).__name__
    return info


def _relay_variables(namespace):
    out = []
    for name, value in list(namespace.items()):
        if name.startswith("_") or name in _RELAY_HIDDEN or isinstance(value, _relay_types.ModuleType):
            continue
        out.append(_relay_describe(name, value))
    out.sort(key=lambda item: item["name"])
    return out
'''

# `exit()` typed in a Python console pane attached to this kernel runs *in the kernel*, and
# ipykernel's ask_exit sets exit_now — the kernel stops — unless `exit(keep_kernel=True)`. The
# kernel is the agent's too, so leaving a console must never take it down (#83YV): ask_exit only
# sends the payload that tells the console to leave, keeping the kernel.
KEEP_KERNEL_SOURCE = r'''
def _relay_keep_kernel():
    shell = get_ipython()
    shell.ask_exit = lambda: shell.payload_manager.write_payload({"source": "ask_exit", "keepkernel": True})
_relay_keep_kernel()
del _relay_keep_kernel
'''

# ----- The child server (stdlib only; any Python 3.8+ the workspace chooses) ---------------------
CHILD_SOURCE = r'''
import ast, asyncio, builtins, inspect, json, linecache, os, signal, sys, traceback, types

CTL_IN, CTL_OUT, TOKEN = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
if os.name == "nt":
    import msvcrt
    CTL_IN = msvcrt.open_osfhandle(CTL_IN, os.O_RDONLY)
    CTL_OUT = msvcrt.open_osfhandle(CTL_OUT, 0)
else:
    # The control pipes are ours: programs the user starts must not inherit them.
    os.set_inheritable(CTL_IN, False)
    os.set_inheritable(CTL_OUT, False)
ctl_in = os.fdopen(CTL_IN, "r", encoding="utf-8", newline="\n")
ctl_out = os.fdopen(CTL_OUT, "w", encoding="utf-8", newline="\n")

exec(compile(INTROSPECT, "<relay-introspect>", "exec"), globals())

sys.argv = [""]
main = types.ModuleType("__main__")
main.__builtins__ = builtins
sys.modules["__main__"] = main
ns = main.__dict__
state = {"executing": False}


def send(message):
    ctl_out.write(json.dumps(message) + "\n")
    ctl_out.flush()


def on_interrupt(signum, frame):
    # Only a running cell is interrupted; a signal between cells is dropped.
    if state["executing"]:
        raise KeyboardInterrupt


signal.signal(signal.SIGINT, on_interrupt)
if hasattr(signal, "SIGBREAK"):
    signal.signal(signal.SIGBREAK, on_interrupt)


def ask(prompt, password):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.flush()
        except Exception:
            pass
    send({"type": "input_request", "prompt": str(prompt), "password": bool(password)})
    line = ctl_in.readline()
    if not line:
        raise EOFError("the session closed")
    reply = json.loads(line)
    if "error" in reply:
        raise EOFError(reply["error"])
    return str(reply.get("value", ""))


def relay_input(prompt=""):
    if prompt:
        sys.stdout.write(str(prompt))
    return ask(prompt, False)


def relay_getpass(prompt="Password: ", stream=None):
    return ask(prompt, True)


builtins.input = relay_input
try:
    import getpass
    getpass.getpass = relay_getpass
except Exception:
    pass


def user_traceback(exc):
    tb = exc.__traceback__
    while tb is not None and tb.tb_frame.f_code.co_filename == "<string>":
        tb = tb.tb_next
    return "".join(traceback.format_exception(type(exc), exc, tb))


def safe_repr(value):
    try:
        text = repr(value)
    except Exception as exc:
        return "<repr failed: %s: %s>" % (type(exc).__name__, exc)
    return text if len(text) <= MAX_REPR else text[:MAX_REPR] + "... (%d characters)" % len(text)


def mark(cell):
    for stream in (sys.stdout, sys.stderr, sys.__stdout__, sys.__stderr__):
        try:
            stream.flush()
        except Exception:
            pass
    marker = ("\x1e" + TOKEN + ":" + str(cell) + "\x1e").encode()
    for fd in (1, 2):
        try:
            os.write(fd, marker)
        except OSError:
            pass


def run(code_obj):
    result = eval(code_obj, ns)
    if code_obj.co_flags & inspect.CO_COROUTINE:
        result = asyncio.run(result)
    return result


def execute(message):
    cell, code = message["id"], message["code"]
    filename = "<cell-%s>" % cell
    linecache.cache[filename] = (len(code), None, code.splitlines(True), filename)
    reply = {"type": "done", "id": cell, "status": "ok", "result_repr": None, "result_type": None,
             "error": None}
    flags = getattr(ast, "PyCF_ALLOW_TOP_LEVEL_AWAIT", 0)
    try:
        try:
            state["executing"] = True
            send({"type": "started", "id": cell})
            tree = ast.parse(code, filename, "exec")
            last = None
            if tree.body and isinstance(tree.body[-1], ast.Expr):
                last = ast.Expression(tree.body.pop().value)
            run(compile(tree, filename, "exec", flags=flags))
            if last is not None:
                value = run(compile(last, filename, "eval", flags=flags))
                if value is not None:
                    ns["_"] = value
                    reply["result_repr"] = safe_repr(value)
                    reply["result_type"] = _relay_type_name(value)
        finally:
            state["executing"] = False
    except KeyboardInterrupt as exc:
        reply["status"] = "interrupted"
        reply["error"] = {"ename": "KeyboardInterrupt", "evalue": "", "traceback": user_traceback(exc)}
    except BaseException as exc:      # SystemExit included: a cell must not end the kernel
        reply["status"] = "error"
        reply["error"] = {"ename": type(exc).__name__, "evalue": str(exc), "traceback": user_traceback(exc)}
    state["executing"] = False
    mark(cell)
    send(reply)


send({"type": "ready", "pid": os.getpid(), "python": sys.version.split()[0],
      "executable": sys.executable})
while True:
    try:
        line = ctl_in.readline()
        if not line:
            break
        message = json.loads(line)
        op = message.get("op")
        if op == "execute":
            execute(message)
        elif op == "variables":
            send({"type": "variables", "items": _relay_variables(ns)})
        elif op == "shutdown":
            break
    except KeyboardInterrupt:
        state["executing"] = False
        continue
'''


def _child_program() -> str:
    return (f"MAX_REPR = {MAX_REPR}\nINTROSPECT = {INTROSPECT_SOURCE!r}\n" + CHILD_SOURCE)


class _Capture:
    """One of the child's output pipes, cut into cells at the end-of-cell markers the child writes.

    The child writes `\\x1e<token>:<seq>\\x1e` to fd 1 and fd 2 after each cell, so output that was
    still in a pipe when the cell's reply arrived is still counted as that cell's."""

    def __init__(self, name: str, pipe, token: str):
        self.name = name
        self._pipe = pipe
        self._marker = re.compile("\x1e" + re.escape(token) + r":(\d+)\x1e")
        self._prefix = "\x1e" + token + ":"
        self._cv = threading.Condition()
        self._buf: list[str] = []
        self._size = 0
        self._done: dict[int, str] = {}
        self._eof = False
        self._cell: int | None = None
        self._callback: Callable[[int, str, str], None] | None = None
        self._thread = threading.Thread(target=self._read, name=f"relay-kernel-{name}", daemon=True)
        self._thread.start()

    def begin(self, cell: int, callback) -> None:
        with self._cv:
            self._cell, self._callback = cell, callback

    def _emit(self, text: str, calls: list) -> None:
        if not text:
            return
        if self._size < MAX_CAPTURE:
            kept = text[:MAX_CAPTURE - self._size]
            self._buf.append(kept)
            self._size += len(kept)
            if len(kept) < len(text):
                self._buf.append(f"\n… output truncated at {MAX_CAPTURE} characters\n")
        if self._callback is not None and self._cell is not None:
            calls.append((self._callback, self._cell, text))

    def _feed(self, pending: str, final: bool) -> str:
        calls: list = []
        with self._cv:
            while True:
                match = self._marker.search(pending)
                if not match:
                    break
                self._emit(pending[:match.start()], calls)
                self._done[int(match.group(1))] = "".join(self._buf)
                self._buf, self._size = [], 0
                pending = pending[match.end():]
                self._cv.notify_all()
            hold = ""
            cut = pending.rfind("\x1e")
            if not final and cut >= 0:
                tail = pending[cut:]
                if "\x1e" not in tail[1:] and len(tail) < len(self._prefix) + 24 and (
                        self._prefix.startswith(tail) or tail.startswith(self._prefix)):
                    pending, hold = pending[:cut], tail
            self._emit(pending, calls)
        for callback, cell, text in calls:
            try:
                callback(cell, self.name, text)
            except Exception:
                pass
        return hold

    def _read(self) -> None:
        import codecs
        decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        pending = ""
        fd = self._pipe.fileno()
        try:
            while True:
                try:
                    chunk = os.read(fd, 65536)
                except OSError:
                    chunk = b""
                if not chunk:
                    break
                pending = self._feed(pending + decoder.decode(chunk), final=False)
            self._feed(pending + decoder.decode(b"", final=True), final=True)
        finally:
            with self._cv:
                self._eof = True
                self._cv.notify_all()
            try:
                self._pipe.close()     # the reader owns its pipe: nobody closes it under a read
            except OSError:
                pass

    def collect(self, cell: int, timeout: float) -> str:
        """The cell's output, once its marker arrived (or the pipe closed, or `timeout` passed)."""
        with self._cv:
            self._cv.wait_for(lambda: cell in self._done or self._eof, timeout)
            if cell in self._done:
                return self._done.pop(cell)
            text = "".join(self._buf)
            self._buf, self._size = [], 0
            return text

    def join(self, timeout: float) -> None:
        self._thread.join(timeout)


class SubprocessBackend:
    """The stdlib child server. Works wherever a Python interpreter does; no extra packages."""

    name = "subprocess"

    def __init__(self, python: str | None = None, cwd: str | None = None, env: dict | None = None,
                 startup_timeout: float = 30.0):
        self.python = python or sys.executable
        self.cwd = cwd
        self.env = env
        self.startup_timeout = startup_timeout
        self.proc: subprocess.Popen | None = None
        self.info: dict = {}
        self._ctl: queue.Queue = queue.Queue()
        self._started = threading.Event()
        self._current: int | None = None

    # -- lifecycle -------------------------------------------------------------------------------
    def start(self) -> None:
        token = secrets.token_hex(12)
        in_r, in_w = os.pipe()
        out_r, out_w = os.pipe()
        env = dict(os.environ if self.env is None else self.env)
        env.setdefault("PYTHONIOENCODING", "utf-8")
        env.setdefault("MPLBACKEND", "Agg")      # figures go to output adapters, never a window
        kwargs: dict = {}
        if os.name == "nt":
            import msvcrt
            handles = [msvcrt.get_osfhandle(in_r), msvcrt.get_osfhandle(out_w)]
            for handle in handles:
                os.set_handle_inheritable(handle, True)
            info = subprocess.STARTUPINFO()
            info.lpAttributeList = {"handle_list": handles}
            ids = [str(h) for h in handles]
            kwargs.update(startupinfo=info, close_fds=True,
                          creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
        else:
            ids = [str(in_r), str(out_w)]
            kwargs.update(pass_fds=(in_r, out_w), start_new_session=True)
        try:
            self.proc = subprocess.Popen([self.python, "-u", "-c", _child_program(), *ids, token],
                                         stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, cwd=self.cwd, env=env, **kwargs)
        except OSError as exc:
            for fd in (in_r, in_w, out_r, out_w):
                os.close(fd)
            raise KernelError(f"could not start {self.python}: {exc}") from exc
        finally:
            if self.proc is not None:
                os.close(in_r)
                os.close(out_w)
        self._ctl_w = os.fdopen(in_w, "w", encoding="utf-8", newline="\n")
        self._ctl_r = os.fdopen(out_r, "r", encoding="utf-8", newline="\n")
        self._out = _Capture("stdout", self.proc.stdout, token)
        self._err = _Capture("stderr", self.proc.stderr, token)
        self._reader = threading.Thread(target=self._read_ctl, name="relay-kernel-ctl", daemon=True)
        self._reader.start()
        message = self._next(self.startup_timeout)
        if message is None or message.get("type") != "ready":
            stderr = self._err.collect(-1, 1.0)
            self.kill()
            raise KernelError(f"the Python session did not start: {stderr.strip()[-2000:] or message}")
        self.info = {"pid": message["pid"], "python": message["python"], "executable": message["executable"]}

    def _read_ctl(self) -> None:
        try:
            for line in self._ctl_r:
                try:
                    message = json.loads(line)
                except ValueError:
                    continue
                if message.get("type") == "started":
                    if message.get("id") == self._current:
                        self._started.set()
                    continue
                self._ctl.put(message)
        except (OSError, ValueError):
            pass
        finally:
            self._ctl.put({"type": "eof"})
            try:
                self._ctl_r.close()
            except OSError:
                pass

    def _next(self, timeout: float | None) -> dict | None:
        try:
            return self._ctl.get(timeout=timeout)
        except queue.Empty:
            return None

    def _send(self, message: dict) -> None:
        try:
            self._ctl_w.write(json.dumps(message) + "\n")
            self._ctl_w.flush()
        except (OSError, ValueError) as exc:
            raise KernelError("the Python session is not running") from exc

    @property
    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    # -- operations ------------------------------------------------------------------------------
    def execute(self, seq: int, code: str, on_stream, on_input) -> dict:
        self._out.begin(seq, on_stream)
        self._err.begin(seq, on_stream)
        self._started.clear()
        self._current = seq
        try:
            self._send({"op": "execute", "id": seq, "code": code})
            while True:
                message = self._next(None)
                kind = message.get("type")
                if kind == "input_request":
                    try:
                        self._answer(message, on_input)
                    except KernelError:
                        pass            # killed while asking: the eof follows
                elif kind == "done" and message.get("id") == seq:
                    reply = message
                    break
                elif kind == "eof":
                    try:
                        code_ = self.proc.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        code_ = None
                    reply = {"status": "died", "result_repr": None, "result_type": None,
                             "error": {"ename": "KernelDied", "evalue": f"the Python process exited ({code_})",
                                       "traceback": ""}}
                    break
        finally:
            self._current = None
        wait = 5.0 if reply["status"] != "died" else 1.0
        reply["stdout"] = self._out.collect(seq, wait)
        reply["stderr"] = self._err.collect(seq, wait)
        reply["displays"] = []
        return reply

    def _answer(self, request: dict, on_input) -> None:
        if on_input is None:
            self._send({"op": "input_reply", "error": "input() is not available in this session"})
            return
        try:
            value = on_input(request.get("prompt", ""), bool(request.get("password")))
        except Exception as exc:
            self._send({"op": "input_reply", "error": f"input cancelled: {exc}"})
            return
        if value is None:
            self._send({"op": "input_reply", "error": "input cancelled"})
        else:
            self._send({"op": "input_reply", "value": str(value)})

    def interrupt(self) -> None:
        if not self.alive:
            return
        # A signal that arrives before the cell starts is dropped by the child, so wait for it.
        self._started.wait(2.0)
        if os.name == "nt":
            self.proc.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            os.kill(self.proc.pid, signal.SIGINT)

    def variables(self) -> list[dict]:
        self._send({"op": "variables"})
        while True:
            message = self._next(30.0)
            if message is None:
                raise KernelError("the Python session did not answer")
            if message.get("type") == "variables":
                return message["items"]
            if message.get("type") == "eof":
                raise KernelError("the Python session exited")

    def kill(self) -> None:
        if self.proc is None:
            return
        if self.proc.poll() is None:
            try:
                if os.name == "nt":
                    self.proc.kill()
                else:
                    os.killpg(self.proc.pid, signal.SIGKILL)   # its own session: children go too
            except (OSError, ProcessLookupError):
                pass
        self._close()

    def shutdown(self) -> None:
        if self.proc is None:
            return
        if self.proc.poll() is None:
            try:
                self._send({"op": "shutdown"})
                self.proc.wait(timeout=3)
            except (KernelError, subprocess.TimeoutExpired):
                pass
        self.kill()

    def _close(self) -> None:
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        for stream in (getattr(self, "_ctl_w", None),):
            try:
                if stream:
                    stream.close()
            except OSError:
                pass
        # The reader threads close stdout, stderr and the control pipe themselves at EOF.
        for thread in (getattr(self, "_out", None), getattr(self, "_err", None)):
            if thread:
                thread.join(2.0)


ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


class JupyterBackend:
    """ipykernel (or any Python kernel spec) over the Jupyter protocol, through jupyter_client."""

    name = "jupyter"
    POLL = 0.05

    def __init__(self, kernel_name: str = "python3", cwd: str | None = None, env: dict | None = None,
                 startup_timeout: float = 60.0, kernel_python: str | None = None):
        self.kernel_name = kernel_name
        # An interpreter with ipykernel to launch instead of the named spec: the project's venv or
        # Relay's managed one (relay_core.py_env, #83YV).
        self.kernel_python = kernel_python
        self.cwd = cwd
        self.env = env
        self.startup_timeout = startup_timeout
        self.km = None
        self.kc = None
        self.info: dict = {}
        # kill() may come from another thread (restart, run_cell's deadline) while execute() polls
        # the client's zmq sockets, which are not thread-safe: the kernel is killed at once, the
        # channels are stopped by whichever thread is not using them.
        self._executing = False
        self._killed = False
        # restart() reuses this manager, so a kill still running on a deadline's timer thread
        # must finish before it — its last step stops the channels, which would be the new ones.
        self._lifecycle = threading.Lock()

    def start(self) -> None:
        from jupyter_client.manager import KernelManager
        if self.kernel_python:
            from jupyter_client.kernelspec import KernelSpec, KernelSpecManager
            spec = KernelSpec(argv=[self.kernel_python, "-m", "ipykernel_launcher", "-f", "{connection_file}"],
                              display_name="Python", language="python")

            class OneSpec(KernelSpecManager):
                def get_kernel_spec(self, kernel_name):
                    return spec

            self.km = KernelManager(kernel_name=self.kernel_name, kernel_spec_manager=OneSpec())
        else:
            self.km = KernelManager(kernel_name=self.kernel_name)
        kwargs: dict = {}
        if self.cwd:
            kwargs["cwd"] = self.cwd
        if self.env is not None:
            kwargs["env"] = dict(self.env)
        try:
            self.km.start_kernel(**kwargs)
            self._connect()
        except Exception as exc:
            self.kill(final=True)
            raise KernelError(f"the Jupyter kernel {self.kernel_name!r} did not start: {exc}") from exc
        self._introspect()

    def _connect(self) -> None:
        self.kc = self.km.client()
        self.kc.start_channels()
        self.kc.wait_for_ready(timeout=self.startup_timeout)

    def _introspect(self) -> None:
        reply = self._run_silent(INTROSPECT_SOURCE + "\nimport sys as _relay_sys\n" + KEEP_KERNEL_SOURCE,
                                 {"version": "_relay_sys.version.split()[0]",
                                  "executable": "_relay_sys.executable"})
        # The connection file is how a `jupyter console --existing` in a Python console pane
        # attaches to this same kernel (#83YV); restart() keeps it, and the ports it names.
        self.info = {"kernel": self.kernel_name,
                     "python": _plain(reply.get("version")), "executable": _plain(reply.get("executable")),
                     "connection_file": self.km.connection_file}

    def restart(self) -> None:
        """A fresh kernel on the same connection file and ports, so every other client attached
        to it — a Python console pane's jupyter console — follows it instead of being orphaned.
        Works on a kernel that died, too: the manager still has its launch arguments."""
        with self._lifecycle:
            self._stop_channels()
            self._killed = False
            try:
                self.km.restart_kernel(now=True)
                self._connect()
                failed = None
            except Exception as exc:
                failed = exc
        if failed is not None:
            self.kill()
            raise KernelError(f"the Jupyter kernel {self.kernel_name!r} did not restart: {failed}") from failed
        self._introspect()

    @property
    def alive(self) -> bool:
        # A kill in flight (a deadline's timer thread) or stopped channels count as dead even
        # while the process lingers: the session restarts it rather than executing on nothing.
        return self.km is not None and not self._killed and self.kc is not None and self.km.is_alive()

    def _shell_reply(self, msg_id: str, timeout: float | None) -> dict:
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            if deadline is not None and time.monotonic() > deadline:
                raise KernelError("the Jupyter kernel did not reply")
            try:
                message = self.kc.get_shell_msg(timeout=self.POLL)
            except queue.Empty:
                if self._killed or not self.alive:
                    raise KernelError("the Jupyter kernel exited")
                continue
            except Exception as exc:
                raise KernelError(f"the Jupyter kernel exited: {exc}") from exc
            if message.get("parent_header", {}).get("msg_id") == msg_id:
                return message

    def _run_silent(self, code: str, expressions: dict) -> dict:
        msg_id = self.kc.execute(code, silent=True, store_history=False, user_expressions=expressions,
                                 allow_stdin=False)
        content = self._shell_reply(msg_id, 30.0)["content"]
        if content.get("status") != "ok":
            raise KernelError(f"kernel setup failed: {content.get('ename')}: {content.get('evalue')}")
        return content.get("user_expressions", {})

    def execute(self, seq: int, code: str, on_stream, on_input) -> dict:
        self._executing = True
        try:
            return self._execute(seq, code, on_stream, on_input)
        finally:
            self._executing = False
            if self._killed:
                self._stop_channels()

    def _died(self, reply: dict) -> dict:
        reply.update(status="died", error={"ename": "KernelDied", "evalue": "the Jupyter kernel exited",
                                           "traceback": ""})
        return reply

    def _execute(self, seq: int, code: str, on_stream, on_input) -> dict:
        msg_id = self.kc.execute(code, store_history=True, allow_stdin=on_input is not None,
                                 stop_on_error=False, user_expressions={"type": "_relay_type_name(_)"})
        out: dict = {"stdout": [], "stderr": []}
        reply = {"status": "ok", "result_repr": None, "result_type": None, "error": None, "displays": []}
        idle = False
        while not idle:
            if on_input is not None:
                try:
                    request = self.kc.get_stdin_msg(timeout=0)
                except queue.Empty:
                    request = None
                if request and request["header"]["msg_type"] == "input_request":
                    content = request["content"]
                    if content.get("prompt") and not content.get("password"):
                        # As the subprocess server does: the transcript shows the prompt, never
                        # the answer.
                        out["stdout"].append(content["prompt"])
                        if on_stream:
                            on_stream(seq, "stdout", content["prompt"])
                    try:
                        value = on_input(content.get("prompt", ""), bool(content.get("password")))
                    except Exception:
                        value = None
                    self.kc.input("" if value is None else str(value))
            try:
                message = self.kc.get_iopub_msg(timeout=self.POLL)
            except queue.Empty:
                if self._killed or not self.alive:
                    self._died(reply)
                    break
                continue
            except Exception:           # the channels went away with the kernel
                self._died(reply)
                break
            if message.get("parent_header", {}).get("msg_id") != msg_id:
                continue
            kind, content = message["header"]["msg_type"], message["content"]
            if kind == "stream":
                name = "stderr" if content.get("name") == "stderr" else "stdout"
                out[name].append(content.get("text", ""))
                if on_stream:
                    on_stream(seq, name, content.get("text", ""))
            elif kind == "execute_result":
                reply["result_repr"] = _plain(content.get("data", {}).get("text/plain"))
            elif kind == "display_data":
                reply["displays"].append(content.get("data", {}))
            elif kind == "error":
                reply["error"] = {"ename": content.get("ename", ""), "evalue": content.get("evalue", ""),
                                  "traceback": ANSI.sub("", "\n".join(content.get("traceback", [])))}
            elif kind == "status" and content.get("execution_state") == "idle":
                idle = True
        if reply["status"] != "died":
            try:
                content = self._shell_reply(msg_id, 30.0)["content"]
            except KernelError:
                content = {}
            status = content.get("status", "ok")
            if reply["result_repr"] is not None:
                reply["result_type"] = _plain(content.get("user_expressions", {}).get("type"))
            if status == "error" or reply["error"]:
                interrupted = (reply["error"] or {}).get("ename") == "KeyboardInterrupt"
                reply["status"] = "interrupted" if interrupted else "error"
                if reply["error"] is None:
                    reply["error"] = {"ename": content.get("ename", ""), "evalue": content.get("evalue", ""),
                                      "traceback": ANSI.sub("", "\n".join(content.get("traceback", [])))}
            elif status == "aborted":
                reply["status"] = "cancelled"
        reply["stdout"] = "".join(out["stdout"])[:MAX_CAPTURE]
        reply["stderr"] = "".join(out["stderr"])[:MAX_CAPTURE]
        return reply

    def interrupt(self) -> None:
        if self.alive:
            self.km.interrupt_kernel()

    def variables(self) -> list[dict]:
        result = self._run_silent("", {"v": "_relay_json_mod.dumps(_relay_variables(get_ipython().user_ns))"})
        data = result.get("v", {})
        if data.get("status") != "ok":
            raise KernelError(f"variables failed: {data.get('ename')}: {data.get('evalue')}")
        return json.loads(ast.literal_eval(data["data"]["text/plain"]))

    def _stop_channels(self) -> None:
        kc, self.kc = self.kc, None
        if kc is not None:
            try:
                kc.stop_channels()
            except Exception:
                pass

    def kill(self, final: bool = False) -> None:
        """Kill the kernel now. Unless `final`, the manager keeps its zmq context and connection
        file (`restart=True`), so restart() can bring a kernel back on the same ports."""
        with self._lifecycle:
            self._killed = True
            if self.km is not None:
                try:
                    self.km.shutdown_kernel(now=True, restart=not final)
                except Exception:
                    pass
            if not self._executing:
                self._stop_channels()

    def shutdown(self) -> None:
        if self.km is not None and self.alive and self.kc is not None:
            try:
                self._stop_channels()
                self.km.shutdown_kernel(now=False)
                return
            except Exception:
                pass
        self.kill(final=True)


def _plain(expression: dict | str | None) -> str | None:
    if isinstance(expression, dict):
        if expression.get("status") != "ok":
            return None
        text = expression.get("data", {}).get("text/plain")
        try:
            return str(ast.literal_eval(text))
        except (ValueError, SyntaxError, TypeError):
            return text
    return expression


def jupyter_available(kernel_name: str = "python3") -> bool:
    """jupyter_client importable and the kernel spec installed (ipykernel provides python3)."""
    if importlib.util.find_spec("jupyter_client") is None:
        return False
    try:
        from jupyter_client.kernelspec import KernelSpecManager, NoSuchKernel
        try:
            KernelSpecManager().get_kernel_spec(kernel_name)
        except NoSuchKernel:
            return False
    except Exception:
        return False
    return True


# ----- The session ----------------------------------------------------------------------------------
class KernelSession:
    """One persistent Python state for a workspace, shared by the human and the agent.

    backend: "auto" (Jupyter when available, else the subprocess server), "subprocess", "jupyter",
    or a backend object with the same methods (start, execute, interrupt, variables, kill, shutdown,
    alive, info, name).
    input_handler(prompt, password) -> str | None answers input()/getpass(); with no handler the
    program's input() raises (EOFError; IPython's StdinNotImplementedError on the Jupyter backend).
    The answer is never recorded.
    Records a person or the agent caused carry origin "human" or "agent"; the state-lost record
    written when the kernel died on its own carries origin "system".
    on_output(seq, stream, text) streams output while a cell runs; on_record(record) is called with
    every record as it is appended to the history."""

    def __init__(self, backend="auto", *, cwd: str | None = None, env: dict | None = None,
                 python: str | None = None, kernel_name: str = "python3",
                 input_handler: Callable[[str, bool], str | None] | None = None,
                 on_output: Callable[[int, str, str], None] | None = None,
                 on_record: Callable[[dict], None] | None = None,
                 interrupt_grace: float = 5.0):
        self._backend_choice = backend
        self._cwd, self._env, self._python, self._kernel_name = cwd, env, python, kernel_name
        self.input_handler = input_handler
        self.on_output = on_output
        self.on_record = on_record
        self.interrupt_grace = interrupt_grace
        self._backend = None
        self._started = False
        self._cv = threading.Condition()
        self._next_ticket = 0
        self._serving = 0
        self._abandoned: set[int] = set()
        self._epoch = 0
        self._current: int | None = None
        self._seq = 0
        self._records: list[dict] = []
        self._records_lock = threading.Lock()
        self._closed = False

    # -- backend -----------------------------------------------------------------------------------
    def _make_backend(self):
        choice = self._backend_choice
        if not isinstance(choice, str):
            # A class or factory makes a fresh backend per (re)start; an instance is reused.
            return choice() if isinstance(choice, type) or not hasattr(choice, "execute") else choice
        if choice == "auto":
            choice = "jupyter" if self._python is None and jupyter_available(self._kernel_name) else "subprocess"
        if choice == "jupyter":
            return JupyterBackend(self._kernel_name, self._cwd, self._env)
        if choice == "subprocess":
            return SubprocessBackend(self._python, self._cwd, self._env)
        raise ValueError(f"Unknown kernel backend {choice!r}.")

    @property
    def backend_name(self) -> str:
        return getattr(self._backend, "name", str(self._backend_choice))

    def info(self) -> dict:
        backend = self._backend
        return {"backend": self.backend_name, "alive": bool(backend and backend.alive),
                "cells": sum(1 for r in self._records if r["kind"] == "cell"),
                **(getattr(backend, "info", {}) or {})}

    def start(self) -> "KernelSession":
        with self._turn():
            self._ensure_backend()
        return self

    def _ensure_backend(self) -> None:
        """Called on the session's turn. Starts the backend, or replaces a dead one with an
        explicit state-lost record so nobody assumes the old variables are still there."""
        if self._closed:
            raise KernelError("the session is shut down")
        if self._backend is not None and self._backend.alive:
            return
        died = self._backend is not None and self._started
        if died and self._reusable():
            self._backend.restart()
        else:
            if self._backend is not None:
                self._backend.kill()
            self._backend = self._make_backend()
            self._backend.start()
        if died:
            self._append(self._restart_record("system", None, "the kernel process exited", None))
        self._started = True

    def _reusable(self) -> bool:
        """A started backend that restarts in place (Jupyter: same connection file and ports)."""
        return self._backend is not None and self._started and callable(getattr(self._backend, "restart", None))

    # -- ordering ----------------------------------------------------------------------------------
    @contextmanager
    def _turn(self, timeout: float | None = None):
        """Serve calls strictly in submission order. Yields the restart epoch at submission."""
        with self._cv:
            ticket, epoch = self._next_ticket, self._epoch
            self._next_ticket += 1
            if not self._cv.wait_for(lambda: self._serving == ticket, timeout):
                self._abandoned.add(ticket)
                raise KernelBusy("the session is busy running earlier cells")
        try:
            yield epoch
        finally:
            with self._cv:
                self._serving += 1
                while self._serving in self._abandoned:
                    self._abandoned.discard(self._serving)
                    self._serving += 1
                self._cv.notify_all()

    def _append(self, record: dict) -> dict:
        with self._records_lock:
            self._seq += 1
            record["seq"] = self._seq
            self._records.append(record)
        if self.on_record:
            try:
                self.on_record(copy.deepcopy(record))
            except Exception:
                pass
        return record

    def _next_seq(self) -> int:
        with self._records_lock:
            return self._seq + 1

    # -- the public operations ----------------------------------------------------------------------
    def run_cell(self, code: str, origin: str = "human", intent: str | None = None,
                 timeout: float | None = None) -> dict:
        """Run `code` in the shared state after every earlier submission; returns its record.

        `timeout` (seconds) interrupts the cell when it runs that long, and kills the kernel if the
        interrupt is ignored for `interrupt_grace` more seconds (the next cell then starts fresh,
        after a state-lost record)."""
        if origin not in ORIGINS:
            raise ValueError(f"origin must be one of {', '.join(ORIGINS)}")
        if not isinstance(code, str):
            raise TypeError("code must be text")
        if intent is not None and not isinstance(intent, str):
            raise TypeError("intent must be text")
        with self._turn() as epoch:
            base = {"kind": "cell", "origin": origin, "intent": intent, "code": code, "stdout": "",
                    "stderr": "", "result_repr": None, "result_type": None, "displays": [], "error": None,
                    "started_at": _now(), "duration": 0.0, "backend": self.backend_name}
            if epoch != self._epoch:
                base.update(status="cancelled",
                            error={"ename": "Cancelled", "evalue": "the kernel was restarted before this cell ran",
                                   "traceback": ""})
                return copy.deepcopy(self._append(base))
            self._ensure_backend()
            base["backend"] = self.backend_name
            seq = self._next_seq()
            timers, fired = [], []
            if timeout is not None:
                def expire():
                    fired.append("interrupt")
                    self._backend.interrupt()
                    kill = threading.Timer(self.interrupt_grace, lambda: (fired.append("kill"), self._backend.kill()))
                    kill.daemon = True
                    timers.append(kill)
                    kill.start()
                timer = threading.Timer(timeout, expire)
                timer.daemon = True
                timers.append(timer)
                timer.start()
            with self._cv:
                self._current = seq
            started = time.monotonic()
            try:
                reply = self._backend.execute(seq, code, self._stream, self.input_handler)
            finally:
                with self._cv:
                    self._current = None
                for timer in list(timers):
                    timer.cancel()
            base["duration"] = round(time.monotonic() - started, 6)
            for key in ("stdout", "stderr", "result_repr", "result_type", "displays", "error"):
                base[key] = reply.get(key, base[key])
            status = reply.get("status", "ok")
            if epoch != self._epoch and status in {"died", "interrupted", "error"}:
                status = "restarted"
                base["error"] = {"ename": "KernelRestarted", "evalue": "the kernel was restarted while this cell ran",
                                 "traceback": ""}
            elif fired and status in {"interrupted", "died"}:
                status = "timeout"
                base["error"] = dict(base["error"] or {}, ename="Timeout",
                                     evalue=f"stopped after {timeout:g} s" + (" (kernel killed)" if "kill" in fired else ""))
            base["status"] = status
            return copy.deepcopy(self._append(base))

    def _stream(self, seq: int, stream: str, text: str) -> None:
        if self.on_output:
            self.on_output(seq, stream, text)

    def interrupt(self) -> bool:
        """Interrupt the running cell. False when nothing was running."""
        with self._cv:
            running = self._current is not None
        if running and self._backend is not None:
            self._backend.interrupt()
        return running

    def restart(self, origin: str = "human", intent: str | None = None) -> dict:
        """Start a fresh kernel. A running cell is stopped and cells queued behind it are cancelled;
        the returned record says that the state is gone and names what was in it."""
        if origin not in ORIGINS:
            raise ValueError(f"origin must be one of {', '.join(ORIGINS)}")
        with self._cv:
            self._epoch += 1
            running = self._current is not None
        if running and self._backend is not None:
            self._backend.kill()
        with self._turn():
            lost = None
            if self._backend is not None and self._backend.alive:
                try:
                    lost = [item["name"] for item in self._backend.variables()]
                except Exception:
                    lost = None
            if self._reusable():
                self._backend.restart()
            else:
                if self._backend is not None and self._backend.alive:
                    self._backend.shutdown()
                elif self._backend is not None:
                    self._backend.kill()
                self._backend = self._make_backend()
                self._backend.start()
            self._started = True
            return copy.deepcopy(self._append(self._restart_record(origin, intent, "requested", lost)))

    def _restart_record(self, origin: str, intent: str | None, reason: str, lost: list | None) -> dict:
        return {"kind": "restart", "origin": origin, "intent": intent, "code": "", "status": "restarted",
                "reason": reason, "message": STATE_LOST, "lost_names": lost, "stdout": "", "stderr": "",
                "result_repr": None, "result_type": None, "displays": [], "error": None,
                "started_at": _now(), "duration": 0.0, "backend": self.backend_name}

    def variables(self, timeout: float | None = None) -> list[dict]:
        """The namespace: [{name, type, summary, shape?, length?}], after queued cells have run."""
        with self._turn(timeout):
            self._ensure_backend()
            return self._backend.variables()

    def history(self) -> list[dict]:
        with self._records_lock:
            return copy.deepcopy(self._records)

    def export(self, fmt: str = "script") -> str:
        """The session in order: "script" is a runnable `# %%` Python file, "json" every record.
        Neither holds anything typed at an input()/getpass() prompt: it was never recorded."""
        records = self.history()
        if fmt == "json":
            return json.dumps({"format": "relay-python-session", "version": 1, "backend": self.backend_name,
                               "exported_at": _now(), "records": records}, indent=2)
        if fmt != "script":
            raise ValueError("fmt must be 'script' or 'json'")
        lines = [f"# Relay Python session, exported {_now()} ({self.backend_name} backend).",
                 "# Cells in the order they ran; `# out:` lines are what each printed or returned.",
                 "# Cells that raised or were stopped are kept, commented out, so the file runs through.",
                 "# Answers typed at input()/getpass() prompts were not recorded."]
        for record in records:
            lines.append("")
            if record["kind"] == "restart":
                lines.append(f"# %% [{record['seq']}] ---- kernel restarted ({record['reason']}): "
                             "the state above is gone ----")
                continue
            if record["status"] == "cancelled":
                continue
            header = f"# %% [{record['seq']}] {record['origin']}"
            if record["intent"]:
                header += " — intent: " + " ".join(record["intent"].split())
            ran = record["status"] == "ok"
            if not ran:
                error = record["error"] or {}
                header += f" — {record['status']}: {error.get('ename', '')} {error.get('evalue', '')}".rstrip()
            lines.append(header)
            code = record["code"].rstrip("\n")
            lines.extend(code.split("\n") if ran else ["# " + ln if ln else "#" for ln in code.split("\n")])
            shown = (record["stdout"] or "") + (record["result_repr"] + "\n" if record["result_repr"] else "")
            for ln in shown.rstrip("\n").split("\n")[:20] if shown.strip() else []:
                lines.append("# out: " + ln)
        return "\n".join(lines) + "\n"

    def shutdown(self) -> None:
        with self._cv:
            self._epoch += 1
            running = self._current is not None
        if running and self._backend is not None:
            self._backend.kill()
        with self._turn():
            self._closed = True
            if self._backend is not None:
                self._backend.shutdown()

    def __enter__(self) -> "KernelSession":
        return self.start()

    def __exit__(self, *exc) -> None:
        self.shutdown()


def _now() -> str:
    return _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="milliseconds")


# ----- Stata: what is installed (report only) --------------------------------------------------
STATA_BINARIES = ("stata-mp", "stata-se", "stata-be", "stata-ic", "stata", "xstata-mp", "xstata-se",
                  "xstata-be", "xstata", "StataMP-64.exe", "StataSE-64.exe", "StataBE-64.exe",
                  "StataIC-64.exe", "Stata-64.exe", "StataMP", "StataSE", "StataBE")
STATA_DIR_PATTERNS = ("/usr/local/stata*", "/opt/stata*", "/Applications/Stata*",
                      "C:/Program Files/Stata*", os.path.expanduser("~/stata*"))


def probe_stata(path: str | None = None, dir_patterns=STATA_DIR_PATTERNS) -> dict:
    """What Stata bridges this machine has, without running Stata (so no license is checked).

    Returns {binaries, install_dirs, version_hints, pystata, stata_setup, stata_kernel,
    kernelspecs, bridges, available, note}. `bridges` lists the paths a Stata workspace could take,
    in preference order, each with what it still needs; `available` is False when no Stata was
    found. Absence is reported, never treated as a pass."""
    binaries = []
    for name in STATA_BINARIES:
        found = shutil.which(name, path=path)
        if found and found not in binaries:
            binaries.append(found)
    install_dirs = sorted({d for pattern in dir_patterns for d in glob.glob(pattern) if os.path.isdir(d)})
    for d in install_dirs:
        for name in STATA_BINARIES:
            candidate = os.path.join(d, name)
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK) and candidate not in binaries:
                binaries.append(candidate)
    hints = sorted({m.group(1) for d in install_dirs + binaries
                    for m in [re.search(r"[Ss]tata[^0-9/\\]*?(\d{2})(?!\d)", d)] if m})
    pystata = importlib.util.find_spec("pystata") is not None
    pystata_dirs = [os.path.join(d, "utilities") for d in install_dirs
                    if os.path.isdir(os.path.join(d, "utilities", "pystata"))]
    stata_setup = importlib.util.find_spec("stata_setup") is not None
    stata_kernel = importlib.util.find_spec("stata_kernel") is not None
    kernelspecs: list[str] = []
    if importlib.util.find_spec("jupyter_client") is not None:
        try:
            from jupyter_client.kernelspec import KernelSpecManager
            kernelspecs = sorted(n for n in KernelSpecManager().find_kernel_specs() if "stata" in n.lower())
        except Exception:
            kernelspecs = []
    bridges = []
    if binaries and (pystata or stata_setup or pystata_dirs):
        bridges.append({"bridge": "pystata", "needs": "Stata 17 or later with a valid license"
                        + ("" if pystata or stata_setup else f"; add {pystata_dirs[0]} to sys.path")})
    if binaries and stata_kernel:
        bridges.append({"bridge": "stata_kernel", "needs": "its kernel spec installed"
                        if not kernelspecs else "a valid license"})
    if binaries:
        bridges.append({"bridge": "console", "needs": "a valid license; runs `stata -q` in the pane"})
    available = bool(binaries)
    if not available:
        note = "No Stata installation found on PATH or in the usual install directories."
    else:
        note = f"Found {len(binaries)} Stata binar{'y' if len(binaries) == 1 else 'ies'}; " \
               "the license and version are only confirmed by running it."
    return {"binaries": binaries, "install_dirs": install_dirs, "version_hints": hints,
            "pystata": pystata, "pystata_dirs": pystata_dirs, "stata_setup": stata_setup,
            "stata_kernel": stata_kernel, "kernelspecs": kernelspecs, "bridges": bridges,
            "available": available, "note": note}
