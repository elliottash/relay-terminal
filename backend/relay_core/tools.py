# SPDX-License-Identifier: GPL-3.0-or-later
"""Small tool surface. Every prepared operation needs explicit UI approval.

Workspace checks protect the file tools from accidental path escape. They are NOT
an OS sandbox: an approved shell command has the invoking user's permissions.
"""
from __future__ import annotations

import difflib
import hashlib
import json
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

from .provider import Cancelled

MAX_FILE = 131072
MAX_OUTPUT = 32768
SECRET_NAME = re.compile(r"(?:KEY|TOKEN|SECRET|PASSWORD|CREDENTIAL|COOKIE)", re.I)


def spec(name: str, description: str, properties: dict, required: list[str]) -> dict:
    return {"type": "function", "function": {"name": name, "description": description,
            "parameters": {"type": "object", "properties": properties, "required": required,
                           "additionalProperties": False}}}

TOOLS = [
    spec("run_command", "Run a non-interactive Bash command in the chosen workspace. Requires approval. NOT an OS sandbox. Does not share interactive shell variables or aliases.",
         {"command": {"type": "string"}, "cwd": {"type": "string", "description": "Workspace-relative directory; default '.'"},
          "timeout_seconds": {"type": "integer", "minimum": 1, "maximum": 120}}, ["command"]),
    spec("read_file", "Read a UTF-8 text file inside the workspace. Requires approval before contents are sent to the model.",
         {"path": {"type": "string"}}, ["path"]),
    spec("list_directory", "List at most 200 entries in a workspace directory. Requires approval.",
         {"path": {"type": "string"}}, ["path"]),
    spec("write_file", "Create or replace one UTF-8 file. The user reviews a full diff. Existing-file changes fail if the file changes after review.",
         {"path": {"type": "string"}, "content": {"type": "string"}}, ["path", "content"]),
]

@dataclass(frozen=True)
class Prepared:
    name: str
    arguments: dict
    preview: str
    path: Path | None = None
    old_sha: str | None = None
    existed: bool = False


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
    def __init__(self, root: str, emit: Callable[[dict], None], cancel: threading.Event):
        self.workspace = Workspace(root)
        self.emit = emit
        self.cancel = cancel
        self._process: subprocess.Popen | None = None
        self._lock = threading.Lock()

    def stop_process(self):
        with self._lock:
            process = self._process
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

    @staticmethod
    def _text(args: dict, key: str, *, maximum: int = MAX_FILE) -> str:
        value = args.get(key)
        if not isinstance(value, str) or len(value.encode("utf-8")) > maximum or "\x00" in value:
            raise ValueError(f"{key} must be text of at most {maximum} bytes.")
        return value

    def prepare(self, name: str, arguments: dict) -> Prepared:
        if not isinstance(arguments, dict):
            raise ValueError("Tool arguments must be an object.")
        args = dict(arguments)
        allowed = {"run_command": {"command", "cwd", "timeout_seconds"},
                   "read_file": {"path"}, "list_directory": {"path"}, "write_file": {"path", "content"}}
        if name not in allowed or set(args) - allowed[name]:
            raise ValueError("Unknown tool or unexpected argument.")
        if name == "run_command":
            command = self._text(args, "command", maximum=16384)
            if not command.strip():
                raise ValueError("Command must not be empty.")
            cwd = self.workspace.resolve(args.get("cwd", "."))
            if not cwd.is_dir():
                raise ValueError("Command working directory must be a directory.")
            timeout = args.get("timeout_seconds", 30)
            if type(timeout) is not int or not 1 <= timeout <= 120:
                raise ValueError("Timeout must be an integer from 1 to 120 seconds.")
            args["timeout_seconds"] = timeout
            return Prepared(name, args, f"RUN COMMAND\n\nWorking directory: {cwd}\nTimeout: {timeout}s\n\n{command}\n\nWARNING: This command is not sandboxed. It can access files and the network with your user permissions. Its output is sent to your chosen model provider.", cwd)
        path = self.workspace.resolve(self._text(args, "path", maximum=4096), allow_missing=name == "write_file")
        if name == "read_file":
            # Contents are intentionally not opened until the user approves.
            return Prepared(name, args, f"READ FILE\n\n{path}\n\nApprove sending this file's contents to your model provider.", path)
        if name == "list_directory":
            return Prepared(name, args, f"LIST DIRECTORY\n\n{path}\n\nApprove sending up to 200 entry names to your model provider.", path)
        content = self._text(args, "content")
        if not path.parent.is_dir():
            raise ValueError("Parent directory must already exist. Relay does not create directory trees automatically.")
        existed = path.exists()
        old = self.workspace.read_bytes(path) if existed else b""
        old_sha = hashlib.sha256(old).hexdigest()
        diff = "".join(difflib.unified_diff(old.decode("utf-8").splitlines(keepends=True),
                     content.splitlines(keepends=True), fromfile=f"a/{args['path']}" if existed else "/dev/null",
                     tofile=f"b/{args['path']}"))
        # Preserve reviewability for files whose only change is a trailing newline.
        preview = f"WRITE FILE\n\n{path}\n\n{diff or '(No text changes)'}\n\nOld bytes: {len(old)}; new bytes: {len(content.encode('utf-8'))}."
        return Prepared(name, args, preview, path, old_sha, existed)

    def execute(self, prepared: Prepared) -> dict:
        if self.cancel.is_set():
            raise Cancelled("Stopped.")
        name, args = prepared.name, prepared.arguments
        if name == "run_command":
            # Recheck paths after approval.
            cwd = self.workspace.resolve(args.get("cwd", "."))
            return self._run(args["command"], cwd, args["timeout_seconds"])
        path = self.workspace.resolve(args["path"], allow_missing=name == "write_file")
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
            raise ValueError("File appeared or disappeared after approval. Request a new diff.")
        old = self.workspace.read_bytes(path) if path.exists() else b""
        if hashlib.sha256(old).hexdigest() != prepared.old_sha:
            raise ValueError("File changed after approval. Nothing was overwritten; request a fresh diff.")
        mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o600
        fd, tempname = tempfile.mkstemp(prefix=".relay-write-", dir=path.parent)
        try:
            with os.fdopen(fd, "wb") as out:
                os.fchmod(out.fileno(), mode)
                out.write(args["content"].encode("utf-8"))
                out.flush()
                os.fsync(out.fileno())
            if self.cancel.is_set():
                raise Cancelled("Stopped.")
            os.replace(tempname, path)
        finally:
            if os.path.exists(tempname):
                os.unlink(tempname)
        return {"path": args["path"], "written_bytes": len(args["content"].encode('utf-8')),
                "sha256": hashlib.sha256(args["content"].encode('utf-8')).hexdigest()}

    def _run(self, command: str, cwd: Path, timeout: int) -> dict:
        env = {key: value for key, value in os.environ.items()
               if not SECRET_NAME.search(key) and not key.startswith("RELAY_")
               and key not in {"BASH_ENV", "ENV", "PYTHONPATH", "LD_PRELOAD", "LD_LIBRARY_PATH", "SSH_AUTH_SOCK"}
               and not key.startswith("BASH_FUNC_")}
        env.update({"TERM": "dumb", "PAGER": "cat", "GIT_TERMINAL_PROMPT": "0"})
        process = subprocess.Popen(["/bin/bash", "--noprofile", "--norc", "-c", command],
                                   cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   start_new_session=True, bufsize=0)
        with self._lock:
            self._process = process
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        started = time.monotonic()
        output = bytearray()
        total = 0
        timed_out = False
        try:
            while selector.get_map():
                if self.cancel.is_set() or time.monotonic() - started > timeout:
                    timed_out = not self.cancel.is_set()
                    break
                for key, _ in selector.select(0.1):
                    chunk = os.read(key.fileobj.fileno(), 8192)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        continue
                    total += len(chunk)
                    remaining = MAX_OUTPUT - len(output)
                    if remaining > 0:
                        shown = chunk[:remaining]
                        output.extend(shown)
                        self.emit({"event": "tool_output", "text": shown.decode("utf-8", "replace")})
            # A command can close stdout and continue running. Enforce timeout then, too.
            while process.poll() is None and not timed_out and not self.cancel.is_set():
                if time.monotonic() - started > timeout:
                    timed_out = True
                    break
                time.sleep(0.03)
        finally:
            # Always clean up the process group, including children left by background jobs.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=0.3)
            except subprocess.TimeoutExpired:
                pass
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            selector.close()
            if process.stdout:
                process.stdout.close()
            with self._lock:
                self._process = None
        if self.cancel.is_set():
            raise Cancelled("Stopped.")
        return {"exit_code": process.returncode, "output": output.decode("utf-8", "replace"),
                "truncated": total > MAX_OUTPUT, "timed_out": timed_out,
                "duration_seconds": round(time.monotonic() - started, 3)}
