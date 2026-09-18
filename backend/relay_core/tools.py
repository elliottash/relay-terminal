# SPDX-License-Identifier: GPL-3.0-or-later
"""Small tool surface. Tools run without a per-action user confirmation.

Workspace checks protect the file tools from accidental path escape. They are NOT
an OS sandbox: a shell command has the invoking user's permissions.
"""
from __future__ import annotations

import difflib
import hashlib
import os
import re
import selectors
import signal
import stat
import subprocess
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .keybindings import KeybindingCatalog
from .program_input import ProgramControl
from .skills import TOOL_SPECS as SKILL_TOOLS, SkillIndex
from .terminal_handoff import TerminalHandoff
from .provider import Cancelled
from .jobs import JobTable
from . import remote_session

MAX_FILE = 131072
MAX_OUTPUT = 32768
# run_command's timeout is how long the call waits before handing a still-running command back as
# a job (relay_core/jobs.py), no longer when the command is killed. A request outside the range is
# clamped, never refused: refusing cost a turn and printed an error for a harmless mistake.
DEFAULT_WAIT = 30
MAX_WAIT = 1800
SECRET_NAME = re.compile(r"(?:KEY|TOKEN|SECRET|PASSWORD|CREDENTIAL|COOKIE)", re.I)


def spec(name: str, description: str, properties: dict, required: list[str]) -> dict:
    return {"type": "function", "function": {"name": name, "description": description,
            "parameters": {"type": "object", "properties": properties, "required": required,
                           "additionalProperties": False}}}

TOOLS = [
    spec("run_command", "Run a non-interactive Bash command in the chosen workspace. NOT an OS sandbox. Does not share interactive shell variables or aliases. "
         "Waits up to timeout_seconds (default 30, at most 1800) for the command to finish. A command still running then is NOT killed: "
         "the result has still_running: true, a job_id and the output so far; read more with command_output (it can wait) and end it with stop_command. "
         "Ask for the time a long build or test suite needs. For a server or watcher that should keep running, set background: true and stop it when done. "
         "Stdin is closed, so a command that prompts fails instead of waiting.",
         {"command": {"type": "string"}, "cwd": {"type": "string", "description": "Workspace-relative directory; default '.'"},
          "timeout_seconds": {"type": "integer", "minimum": 1, "maximum": MAX_WAIT,
                              "description": "Seconds to wait before handing a still-running command back as a job; default 30."},
          "background": {"type": "boolean", "description": "Start it and return after a moment with its job_id and first output, for servers and watchers."}},
         ["command"]),
    spec("read_file", "Read a UTF-8 text file inside the workspace.",
         {"path": {"type": "string"}}, ["path"]),
    spec("list_directory", "List at most 200 entries in a workspace directory.",
         {"path": {"type": "string"}}, ["path"]),
    spec("write_file", "Create a new UTF-8 file, or replace an existing one in full. To change part of a file that already exists, use edit_file instead: it does not resend the whole file. The diff is shown to the user. Fails if the file changes while the write is prepared.",
         {"path": {"type": "string"}, "content": {"type": "string"}}, ["path", "content"]),
    spec("edit_file", "Change an existing UTF-8 file by replacing an exact string. Preferred over write_file for editing a file you have read. old_string must match the file byte for byte, including whitespace and indentation, and must appear exactly once unless replace_all is true: include enough surrounding lines to make it unique. The diff is shown to the user. Fails if the file changes while the edit is prepared.",
         {"path": {"type": "string"}, "old_string": {"type": "string", "description": "The exact text to replace, copied from the file."},
          "new_string": {"type": "string", "description": "The text to put in its place; empty deletes the old text."},
          "replace_all": {"type": "boolean", "description": "Replace every occurrence instead of requiring a unique match; default false."}},
         ["path", "old_string", "new_string"]),
]

# run_command's `host` (card #S5SH): offered only while the user's terminal is logged into a host
# over ssh (the turn's context has remote_session), so a local-only turn never sees it.
HOST_PROPERTY = {"type": "string", "description":
                 "Run on the ssh host the user's terminal is logged into (the Relay context names it), over "
                 "the user's own connection, instead of on this machine. cwd is then a path on that host "
                 "(default: the remote shell's directory). Omit to run locally."}


def run_command_spec(with_host: bool) -> dict:
    tool = TOOLS[0]
    if not with_host:
        return tool
    function = dict(tool["function"])
    parameters = dict(function["parameters"])
    parameters["properties"] = {**parameters["properties"], "host": HOST_PROPERTY}
    function["parameters"] = parameters
    return {**tool, "function": function}


@dataclass(frozen=True)
class Prepared:
    name: str
    arguments: dict
    preview: str
    path: Path | None = None
    old_sha: str | None = None
    existed: bool = False
    # write_file and edit_file: the exact text the write puts on disk (edit_file works it out from
    # old_string/new_string while preparing, so the preview and the write are the same computation),
    # how many occurrences it replaced, and the diff's +/- line counts the result reports.
    content: str | None = None
    replacements: int = 0
    added: int = 0
    removed: int = 0
    # The unified diff on its own, so the tool events can carry it without anything parsing
    # `preview` back apart (protocol 23, the concise tool-call line).
    diff: str = ""


class Workspace:
    def __init__(self, root: str | Path):
        self.root = Path(root).expanduser().resolve(strict=True)
        if not self.root.is_dir():
            raise ValueError("Workspace must be a directory.")

    def resolve(self, name: str, *, allow_missing: bool = False) -> Path:
        if not isinstance(name, str) or not name or "\x00" in name:
            raise ValueError("A valid workspace-relative path is required.")
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Absolute paths and parent traversal are not allowed in file tools.")
        candidate = self.root / relative
        # Refuse symlinks even when they lead back inside the workspace.
        current = self.root
        for part in relative.parts:
            current /= part
            if current.is_symlink():
                raise ValueError("File tools do not follow symlinks.")
        resolved = candidate.resolve(strict=not allow_missing)
        if not resolved.is_relative_to(self.root):
            raise ValueError("Path escapes the workspace.")
        if any(part in {".ssh", ".gnupg", ".git"} or part == ".env" or part.startswith(".env.")
               or part in {"id_rsa", "id_ed25519"} or part.endswith((".pem", ".key"))
               for part in relative.parts):
            raise ValueError("This path is blocked by Relay's basic secret-file guard.")
        return resolved

    @staticmethod
    def read_bytes(path: Path) -> bytes:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        fd = os.open(path, flags)
        with os.fdopen(fd, "rb") as handle:
            info = os.fstat(handle.fileno())
            if not stat.S_ISREG(info.st_mode):
                raise ValueError("Only regular files are supported.")
            if info.st_size > MAX_FILE:
                raise ValueError("File exceeds the 128 KiB preview/read limit.")
            data = handle.read(MAX_FILE + 1)
        if len(data) > MAX_FILE or b"\x00" in data:
            raise ValueError("File is too large or binary.")
        data.decode("utf-8")
        return data


class ToolExecutor:
    def __init__(self, root: str, emit: Callable[[dict], None], cancel: threading.Event,
                 keybindings: KeybindingCatalog | None = None, skills: SkillIndex | None = None):
        self.workspace = Workspace(root)
        # Skill folders are read only through the index, which confines paths to each skill.
        self.skills = skills if skills is not None and skills.skills else None
        # Replaced wholesale by the worker's "keybindings" message; read once per call.
        self.keybindings = keybindings
        self.emit = emit
        self.cancel = cancel
        # Typing into the program in the user's visible pane. Offered only for a turn the user
        # handed the program over for; see relay_core/program_input.py.
        self.program = ProgramControl(emit, cancel)
        # Handing a command to the user's real shell. Offered only when the pane says it takes
        # them; see relay_core/terminal_handoff.py.
        self.terminal = TerminalHandoff(emit, cancel)
        # Where run_command runs when the model gives no cwd: the directory the user's terminal is in.
        self.default_cwd = "."
        # The ssh session the user's terminal is logged into this turn (remote_session.validate's
        # copy), or None. run_command's `host` reaches that host and no other.
        self.remote_session: dict | None = None
        # Every command is a job; the one a tool call is waiting on is what Stop ends. The ones
        # handed back are listed in the pane (`jobs` events); a subagent's executor turns that off.
        self.announce_jobs = True
        self.jobs = JobTable(on_change=self._announce_jobs)
        self._waiting = None
        self._lock = threading.Lock()

    def set_default_cwd(self, path: str | None) -> None:
        """Follow the user's terminal. Anything outside the workspace falls back to its root."""
        self.default_cwd = "."
        if not isinstance(path, str) or not path:
            return
        try:
            relative = Path(path).expanduser().resolve(strict=True).relative_to(self.workspace.root)
        except (ValueError, OSError):
            return
        candidate = str(relative) or "."
        try:
            self.workspace.resolve(candidate)
        except ValueError:
            return
        self.default_cwd = candidate

    def set_remote_session(self, session: dict | None) -> None:
        """Follow the user's terminal onto an ssh host (card #S5SH); None when it is local."""
        self.remote_session = session if isinstance(session, dict) and session.get("host") else None

    def stop_process(self):
        """Stop: end the command this turn is waiting on. Jobs it handed back keep running."""
        with self._lock:
            job = self._waiting
        if job is not None:
            self.jobs.stop(job)

    def shutdown(self) -> None:
        """The conversation is over (new conversation, subagent done): no turn can name its jobs."""
        self.jobs.stop_all(forget=True)

    def jobs_event(self) -> dict:
        return {"event": "jobs", "jobs": self.jobs.snapshot()}

    def _announce_jobs(self) -> None:
        if self.announce_jobs:
            self.emit(self.jobs_event())

    @staticmethod
    def _text(args: dict, key: str, *, maximum: int = MAX_FILE) -> str:
        value = args.get(key)
        if not isinstance(value, str) or len(value.encode("utf-8")) > maximum or "\x00" in value:
            raise ValueError(f"{key} must be text of at most {maximum} bytes.")
        return value

    def tools(self) -> list[dict]:
        catalog = self.keybindings
        tools = TOOLS + [catalog.tool_spec()] if catalog is not None else list(TOOLS)
        if self.remote_session is not None:
            tools[0] = run_command_spec(True)
        tools += JOB_TOOLS
        if self.skills is not None:
            tools += SKILL_TOOLS
        # Read at every model call, so a take-over removes the tool from the next one.
        if self.program.available():
            tools = tools + [self.program.tool_spec()]
        if self.terminal.available():
            tools = tools + [self.terminal.tool_spec()]
        return tools

    def prepare(self, name: str, arguments: dict) -> Prepared:
        if not isinstance(arguments, dict):
            raise ValueError("Tool arguments must be an object.")
        args = dict(arguments)
        if name in ("load_skill", "read_skill_file"):
            if self.skills is None or set(args) - {"name", "path"} or (name == "load_skill" and "path" in args):
                raise ValueError("Unknown tool or unexpected argument.")
            skill = self.skills.get(args.get("name"))
            if name == "load_skill":
                return Prepared(name, {"name": skill.id}, f"LOAD SKILL\n\n{skill.id}")
            if not isinstance(args.get("path"), str):
                raise ValueError("path must be text.")
            return Prepared(name, {"name": skill.id, "path": args["path"]}, f"READ SKILL FILE\n\n{skill.id}/{args['path']}")
        if name == "type_into_program":
            payload, preview = self.program.prepare(args)
            return Prepared(name, payload, preview)
        if name == "run_in_terminal":
            payload, preview = self.terminal.prepare(args)
            return Prepared(name, payload, preview)
        if name == "set_keybinding":
            catalog = self.keybindings
            if catalog is None:
                raise ValueError("Unknown tool or unexpected argument.")
            normalized, preview = catalog.prepare(args)
            return Prepared(name, normalized, preview, catalog.path)
        if name in ("command_output", "stop_command"):
            if set(args) - ({"job_id", "wait_seconds"} if name == "command_output" else {"job_id"}):
                raise ValueError("Unknown tool or unexpected argument.")
            job = self.jobs.get(args.get("job_id"))
            if name == "stop_command":
                return Prepared(name, {"job_id": job.id}, f"STOP COMMAND\n\n{job.id}: {job.command}")
            wait = clamp_seconds(args.get("wait_seconds", 0), 0, 0, MAX_WAIT)
            return Prepared(name, {"job_id": job.id, "wait_seconds": wait},
                            f"COMMAND OUTPUT\n\n{job.id}: {job.command}\nWait: up to {wait}s")
        allowed = {"run_command": {"command", "cwd", "timeout_seconds", "background", "host"},
                   "read_file": {"path"}, "list_directory": {"path"}, "write_file": {"path", "content"},
                   "edit_file": {"path", "old_string", "new_string", "replace_all"}}
        if name not in allowed or set(args) - allowed[name]:
            raise ValueError("Unknown tool or unexpected argument.")
        if name == "run_command":
            command = self._text(args, "command", maximum=16384)
            if not command.strip():
                raise ValueError("Command must not be empty.")
            host = args.get("host")
            if host is not None and not isinstance(host, str):
                raise ValueError("host must be text: the host the user's terminal is logged into.")
            if not host:
                args.pop("host", None)
            background = args.get("background", False)
            if not isinstance(background, bool):
                raise ValueError("background must be true or false.")
            args["background"] = background
            timeout = clamp_seconds(args.get("timeout_seconds", DEFAULT_WAIT), DEFAULT_WAIT, 1, MAX_WAIT)
            args["timeout_seconds"] = timeout
            wait = "Background" if background else f"Waits: {timeout}s, then continues as a job"
            if host:
                return self._prepare_remote(args, command, host, wait)
            args["cwd"] = args.get("cwd") or self.default_cwd
            cwd = self.workspace.resolve(args["cwd"])
            if not cwd.is_dir():
                raise ValueError("Command working directory must be a directory.")
            return Prepared(name, args, f"RUN COMMAND\n\nWorking directory: {cwd}\n{wait}\n\n{command}", cwd)
        path = self.workspace.resolve(self._text(args, "path", maximum=4096),
                                      allow_missing=name in ("write_file", "edit_file"))
        if name == "read_file":
            return Prepared(name, args, f"READ FILE\n\n{path}", path)
        if name == "list_directory":
            return Prepared(name, args, f"LIST DIRECTORY\n\n{path}", path)
        existed = path.exists()
        if name == "edit_file":
            if not existed:
                raise ValueError("edit_file needs a file that already exists; use write_file to create one.")
            old = self.workspace.read_bytes(path)
            content, replacements = self._edited(args, old)
        else:
            content, replacements = self._text(args, "content"), 0
            if not path.parent.is_dir():
                raise ValueError("Parent directory must already exist. Relay does not create directory trees automatically.")
            old = self.workspace.read_bytes(path) if existed else b""
        old_sha = hashlib.sha256(old).hexdigest()
        diff = "".join(difflib.unified_diff(old.decode("utf-8").splitlines(keepends=True),
                     content.splitlines(keepends=True), fromfile=f"a/{args['path']}" if existed else "/dev/null",
                     tofile=f"b/{args['path']}"))
        added = sum(1 for line in diff.splitlines() if line.startswith("+") and not line.startswith("+++"))
        removed = sum(1 for line in diff.splitlines() if line.startswith("-") and not line.startswith("---"))
        # Preserve reviewability for files whose only change is a trailing newline.
        title = "EDIT FILE" if name == "edit_file" else "WRITE FILE"
        preview = f"{title}\n\n{path}\n\n{diff or '(No text changes)'}\n\nOld bytes: {len(old)}; new bytes: {len(content.encode('utf-8'))}."
        return Prepared(name, args, preview, path, old_sha, existed, content, replacements, added, removed,
                        diff=diff)

    def _prepare_remote(self, args: dict, command: str, host: str, wait: str) -> Prepared:
        """run_command with `host`: the same call, run over the user's ssh connection (card #S5SH).

        cwd is a path on the host, so it is not resolved against the workspace; it defaults to the
        remote shell's directory, and without one the command starts in the remote home."""
        session = remote_session.check_host(self.remote_session, host)
        cwd = args.get("cwd") or session.get("cwd") or ""
        if not isinstance(cwd, str) or len(cwd) > 4096 or any(c in cwd for c in "\x00\n\r"):
            raise ValueError("cwd must be a directory path on the remote host, one line of at most 4096 characters.")
        if cwd:
            args["cwd"] = cwd
        else:
            args.pop("cwd", None)
        user = session.get("user")
        who = f"{user}@{host}" if user else host
        preview = (f"RUN COMMAND ON {host}\n\nHost: {who} (over the user's ssh connection)\n"
                   f"Working directory: {cwd or 'the remote home directory'}\n{wait}\n\n{command}")
        return Prepared("run_command", args, preview)

    def _edited(self, args: dict, old: bytes) -> tuple[str, int]:
        """The whole new text of an edit_file, and how many occurrences it replaces.

        Computed while preparing, so the diff the user sees is the bytes the write puts on disk.
        Every error says what the model should do instead."""
        old_string, new_string = self._text(args, "old_string"), self._text(args, "new_string")
        replace_all = args.get("replace_all", False)
        if type(replace_all) is not bool:
            raise ValueError("replace_all must be true or false.")
        if not old_string:
            raise ValueError("old_string must not be empty. Use write_file to create a file or replace one in full.")
        if old_string == new_string:
            raise ValueError("old_string and new_string are identical; the edit would change nothing.")
        text = old.decode("utf-8")
        found = text.count(old_string)
        if found == 0:
            raise ValueError("old_string was not found in the file. Read the file again and copy the exact text, "
                             "including whitespace and indentation.")
        if found > 1 and not replace_all:
            raise ValueError(f"old_string occurs {found} times in the file. Add surrounding lines so it matches "
                             f"once, or set replace_all: true to change all {found}.")
        content = text.replace(old_string, new_string) if replace_all else text.replace(old_string, new_string, 1)
        if len(content.encode("utf-8")) > MAX_FILE:
            raise ValueError("The edited file would exceed the 128 KiB limit.")
        return content, found if replace_all else 1

    def execute(self, prepared: Prepared) -> dict:
        if self.cancel.is_set():
            raise Cancelled("Stopped.")
        name, args = prepared.name, prepared.arguments
        if name == "load_skill":
            return self.skills.load_skill(args["name"])
        if name == "read_skill_file":
            return self.skills.read_file(args["name"], args["path"])
        if name == "type_into_program":
            return self.program.execute(args)
        if name == "run_in_terminal":
            return self.terminal.execute(args)
        if name == "set_keybinding":
            catalog = self.keybindings
            if catalog is None or args["action"] not in catalog.actions:
                raise ValueError("Keybinding catalog changed; the action is no longer available.")
            return catalog.apply(args)
        if name == "run_command" and args.get("host"):
            # Recheck the session at execution time: the user may have logged out since.
            session = remote_session.check_host(self.remote_session, args["host"])
            if not remote_session.socket_alive(session):
                raise ValueError(f"the ssh connection to {session['host']} has closed (its connection-sharing "
                                 "socket is gone), so nothing ran. Ask the user whether they are still logged in.")
            argv = remote_session.ssh_argv(session, args["command"], args.get("cwd"))
            return self._run(args["command"], self.workspace.root, args["timeout_seconds"],
                             args.get("background", False), argv=argv, host=session["host"])
        if name == "run_command":
            # Recheck paths at execution time.
            cwd = self.workspace.resolve(args.get("cwd", "."))
            return self._run(args["command"], cwd, args["timeout_seconds"], args.get("background", False))
        if name == "command_output":
            job = self.jobs.get(args["job_id"])
            return self._await(job, args["wait_seconds"])
        if name == "stop_command":
            job = self.jobs.get(args["job_id"])
            self.jobs.stop(job)
            return self._job_result(job)
        path = self.workspace.resolve(args["path"], allow_missing=name in ("write_file", "edit_file"))
        if name == "read_file":
            data = self.workspace.read_bytes(path)
            return {"path": args["path"], "content": data.decode("utf-8"), "sha256": hashlib.sha256(data).hexdigest()}
        if name == "list_directory":
            if not path.is_dir():
                raise ValueError("Path is not a directory.")
            entries = []
            with os.scandir(path) as scan:
                for i, entry in enumerate(scan):
                    if i >= 200:
                        return {"entries": sorted(entries, key=lambda x: x['name']), "truncated": True}
                    entries.append({"name": entry.name, "type": "symlink" if entry.is_symlink() else "directory" if entry.is_dir(follow_symlinks=False) else "file"})
            return {"entries": sorted(entries, key=lambda x: x['name']), "truncated": False}
        if path.exists() != prepared.existed:
            raise ValueError("File appeared or disappeared while the write was prepared. Request a new diff.")
        old = self.workspace.read_bytes(path) if path.exists() else b""
        if hashlib.sha256(old).hexdigest() != prepared.old_sha:
            raise ValueError("File changed while the write was prepared. Nothing was overwritten; request a fresh diff.")
        # edit_file computed its whole new text while preparing; write_file carries the model's.
        data = (prepared.content if prepared.content is not None else args["content"]).encode("utf-8")
        mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o600
        fd, tempname = tempfile.mkstemp(prefix=".relay-write-", dir=path.parent)
        try:
            with os.fdopen(fd, "wb") as out:
                os.fchmod(out.fileno(), mode)
                out.write(data)
                out.flush()
                os.fsync(out.fileno())
            if self.cancel.is_set():
                raise Cancelled("Stopped.")
            os.replace(tempname, path)
        finally:
            if os.path.exists(tempname):
                os.unlink(tempname)
        result = {"path": args["path"], "written_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                  "added": prepared.added, "removed": prepared.removed}
        if name == "edit_file":
            result["replacements"] = prepared.replacements
        else:
            result["created"] = not prepared.existed
        return result

    def _run(self, command: str, cwd: Path, timeout: int, background: bool = False, *,
             argv: list[str] | None = None, host: str | None = None) -> dict:
        """Start `command` as a job and wait for it. `argv`/`host`: the same job, run as ssh over
        the user's connection (card #S5SH); everything else — env, waiting, output — is shared."""
        env = {key: value for key, value in os.environ.items()
               if not SECRET_NAME.search(key) and not key.startswith("RELAY_")
               and key not in {"BASH_ENV", "ENV", "PYTHONPATH", "LD_PRELOAD", "LD_LIBRARY_PATH", "SSH_AUTH_SOCK"}
               and not key.startswith("BASH_FUNC_")}
        env.update({"TERM": "dumb", "PAGER": "cat", "GIT_TERMINAL_PROMPT": "0"})
        job = self.jobs.start(command, cwd, env, argv=argv, host=host)
        # A background job still gets a moment: a server that fails at once says so in this result.
        return self._await(job, BACKGROUND_GLANCE if background else timeout)

    def _await(self, job, seconds: float) -> dict:
        """Wait on a job with the live output stream on; Stop during the wait ends the job."""
        streamed = 0

        def live(text: str) -> None:
            nonlocal streamed
            if streamed < MAX_OUTPUT:
                piece = text[:MAX_OUTPUT - streamed]
                streamed += len(piece)
                self.emit({"event": "tool_output", "text": piece})

        with self._lock:
            self._waiting = job
        try:
            self.jobs.wait(job, seconds, self.cancel, live)
        finally:
            with self._lock:
                self._waiting = None
        if self.cancel.is_set():
            self.jobs.stop(job)
            raise Cancelled("Stopped.")
        return self._job_result(job)

    def _job_result(self, job) -> dict:
        result = self.jobs.take_output(job, MAX_OUTPUT)
        result["job_id"] = job.id
        if job.host:
            result["host"] = job.host
        result["duration_seconds"] = round((job.finished or time.monotonic()) - job.started, 3)
        if job.running:
            self.jobs.hand_back(job)
            result["still_running"] = True
            result["note"] = (f"Still running as {job.id}. Read more with command_output "
                              f"(wait_seconds up to {MAX_WAIT}), or end it with stop_command.")
        else:
            result["exit_code"] = job.exit_code
            if job.stopped:
                result["stopped"] = True
            elif job.host and job.exit_code == 255:
                result["note"] = (f"ssh exited 255: the connection to {job.host} failed or closed, so the "
                                  "command may not have run. The user's ssh session may have ended; ask them.")
        return result


def clamp_seconds(value, default: int, low: int, high: int) -> int:
    """A wait the model asked for, made valid: numbers (and numeric text) are rounded into
    [low, high]; anything else is the default. true/false are not numbers here."""
    if isinstance(value, bool):
        raise ValueError("A number of seconds is required, not true/false.")
    if isinstance(value, str):
        try:
            value = float(value.strip())
        except ValueError:
            return default
    if not isinstance(value, (int, float)) or value != value:
        return default
    return int(min(high, max(low, round(value))))


BACKGROUND_GLANCE = 2
JOB_TOOLS = [
    spec("command_output", "Read the output a run_command job has printed since you last read it, and whether it is still running. "
         "wait_seconds (default 0, at most 1800) waits for the job to finish first; it returns early when it does.",
         {"job_id": {"type": "string"}, "wait_seconds": {"type": "integer", "minimum": 0, "maximum": MAX_WAIT}}, ["job_id"]),
    spec("stop_command", "Stop a run_command job and its child processes, and return its last output. "
         "Stop servers and watchers you started once you no longer need them.",
         {"job_id": {"type": "string"}}, ["job_id"]),
]
