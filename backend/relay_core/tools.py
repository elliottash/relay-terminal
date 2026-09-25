# SPDX-License-Identifier: AGPL-3.0-or-later
"""Small tool surface. Tools run without a per-action user confirmation.

Workspace checks protect the file tools from accidental path escape. They are NOT
an OS sandbox: a shell command has the invoking user's permissions.

With `host` (card #S5SH) run_command and the file tools work on the ssh host the user's terminal is
logged into instead, over the user's own connection. There is no workspace there: what takes its
place is remote_path() here and the scripts in relay_core/remote_files.py — the same secret-file
guard and no `..` for everything, no symlinks, and for a write (only) the remote home or the
directory the user's shell is in. A read goes anywhere the user's own account can read.
"""
from __future__ import annotations

import difflib
import hashlib
import json
import os
from .filelock import chmod_fd
import posixpath
import re
import selectors
import shlex
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
from .questions import Questions
from .panes import PaneMessaging
from .skills import TOOL_SPECS as SKILL_TOOLS, SkillIndex
from .terminal_handoff import TerminalHandoff
from .provider import Cancelled
from .jobs import JobTable, line_range, sent_length, split_lines
from . import approvals, remote_files, remote_session, security
from .open_buffers import conflict_text, local_key, remote_key

MAX_FILE = 131072

# Card #F8R7: a write worked out against an open editor's unsaved text can only be applied there.
UNSAVED_HEADER = "Open in Relay with unsaved edits: this diff is against the editor's text.\n"
NO_EDITOR_ANSWER = ("The file is open in Relay with unsaved edits and the editor did not answer, so "
                    "nothing was written. Try again, or ask the user to save the file first.")
MAX_OUTPUT = 32768
# Card #0C0V: what one command result, file read or search puts into the model's context, which is
# resent on every later step. The user's fold still gets MAX_OUTPUT and the whole file; the model
# gets a head and a tail with a marker naming the omitted lines and the call that reads them, so a
# 1 MB build log costs 3k tokens instead of 8k — every step — and nothing becomes unreadable.
MODEL_RESULT_CHARS = 12_000
COMMAND_HEAD_CHARS = 4_000    # a command's errors are at its end: the tail gets two thirds
FILE_HEAD_CHARS = 8_000       # a file's imports and definitions are at its start: the head does
# Card #XG2G: a whole-file read stops at MAX_FILE, but a ranged read_file and an edit_file only need
# the file in memory, not in a preview, so they work up to this (src/Pane.h is ~1 MB).
MAX_LARGE_FILE = 8 * 1024 * 1024
WHOLE_READ_REFUSAL = ("File exceeds the 128 KiB preview/read limit for a whole-file read. Read it in parts "
                      "with from_line/to_line; edit_file works on it as it is.")
LARGE_FILE_REFUSAL = "File exceeds the 8 MiB limit of Relay's file tools."
# run_command's timeout is how long the call waits before handing a still-running command back as
# a job (relay_core/jobs.py), no longer when the command is killed. A request outside the range is
# clamped, never refused: refusing cost a turn and printed an error for a harmless mistake.
DEFAULT_WAIT = 30
MAX_WAIT = 1800
SECRET_NAME = re.compile(r"(?:KEY|TOKEN|SECRET|PASSWORD|CREDENTIAL|COOKIE)", re.I)


def _subject(payload: dict) -> str:
    """What a type_into_program approval ask shows (card #K2FV): the intent line, then the text
    or key it would type, so the ask is about the actual keystrokes and not their excuse."""
    typed = payload["text"] if "text" in payload else f"<{payload.get('key', '')}>"
    return f"{payload.get('intent', '')} · {typed}"


def looks_secret(part: str) -> bool:
    """Relay's basic secret-file guard, one path component at a time. The same rule applies to a
    workspace path and to a path on an ssh host (card #S5SH): there is no workspace to confine the
    agent to on the host, so this guard is the part of the protection that travels."""
    return (part in {".ssh", ".gnupg", ".git", "id_rsa", "id_ed25519"}
            or part == ".env" or part.startswith(".env.") or part.endswith((".pem", ".key")))


def remote_path(name: str, policy: security.Policy = security.EMPTY) -> str:
    """A path on the ssh host, checked with the rules that replace the workspace there (card #S5SH).

    It is absolute, `~/…`, or relative to the remote shell's directory. What is checked here is what
    can be checked here: `..` is refused outright, as it is locally, so the path the host sees reads
    as what it is, and the secret-file guard is the same one the workspace uses — on a read as much
    as on a write, because it is about credentials, not about how far the agent may reach. Where the
    two differ is containment, which only a write has and which is checked on the host, where
    `$HOME` is known (relay_core/remote_files.py)."""
    if not name.strip():
        raise ValueError("A path on the host is required.")
    if any(character in name for character in "\n\r"):
        raise ValueError("A path on the host must be one line.")
    parts = [part for part in name.split("/") if part not in ("", ".")]
    if ".." in parts:
        raise ValueError("Parent traversal (..) is not allowed in file tools, on the host any more than "
                         "locally. Give the path in full.")
    if any(looks_secret(part) for part in parts):
        raise ValueError("This path is blocked by Relay's basic secret-file guard, which applies on the host "
                         "too: .ssh, .gnupg, .git, .env files, and .pem/.key files stay unread. If the user "
                         "needs something from one, ask them.")
    if any(security.extra_secret(policy, part) for part in parts):
        raise ValueError("This path is blocked by a secret pattern in Options › Security, which applies on "
                         "the host too.")
    # The host's separator is "/" whatever this machine's is: normalise as POSIX.
    return posixpath.normpath(name)


# ----- the recursive-walk cost guard (card #2Y96) ------------------------------------------------
#
# Owner, 2026-09-19: a pane standing in `$HOME` or `/` keeps its wide sandbox — the agent works
# here without per-action approvals by design, and narrowing the sandbox by depth would be theatre.
# What is guarded is the **cost**, not the permission: a recursive search or listing whose root is
# the home directory, `/`, or a directory the home sits under (`/home`) takes minutes and comes
# back with nothing the model can use, so it is refused with a message that says what to pass
# instead. An explicit path below one of those roots is always allowed, and a non-recursive
# `list_directory` of `$HOME` or `/` is untouched — listing one directory is cheap.
#
# Like the command denylist (relay_core/security.py) this is honoured, not unevadable: it reads the
# obvious spellings of the walking programs. A command that hides its root in a variable runs, and
# the entry, byte and time ceilings elsewhere are what bound it then.

#: Programs that walk a whole tree unless told otherwise.
WALKERS_ALWAYS = {"rg", "ripgrep", "ag", "ack", "ack-grep", "fd", "fdfind", "find", "rgrep",
                  "tree", "du", "ncdu"}
#: Programs that walk only with a recursion flag, and the long flags that turn it on. A bundled
#: short flag is read letter by letter, so `grep -rn` and `ls -laR` are caught too.
WALKERS_FLAGGED = {"grep": ("--recursive", "--dereference-recursive"),
                   "egrep": ("--recursive", "--dereference-recursive"),
                   "fgrep": ("--recursive", "--dereference-recursive"),
                   "ls": ("--recursive",)}
#: Of those, the ones whose first positional argument is a pattern rather than a path.
WALKERS_PATTERN_FIRST = {"grep", "egrep", "fgrep", "rgrep", "rg", "ripgrep", "ag", "ack",
                         "ack-grep", "fd", "fdfind"}
#: Words in front of the program that are not the thing that walks.
WALK_PREFIXES = {"sudo", "doas", "command", "builtin", "nohup", "time", "exec", "env", "nice",
                 "ionice", "stdbuf", "xargs"}


def home_dir() -> Path:
    """The user's home as the tools see it (`$HOME`, so a test can move it)."""
    return Path(os.path.normpath(os.path.expanduser("~")))


def wide_root(path: Path, home: Path | None = None) -> str | None:
    """Why `path` is too wide to crawl, in words the refusal can use — or None if it is fine."""
    home = home or home_dir()
    candidate = Path(os.path.normpath(str(path)))
    if str(candidate) == os.sep:
        return "the whole filesystem"
    if candidate == home:
        return "the home directory"
    try:
        if home.is_relative_to(candidate):
            return "a directory the home directory sits under"
    except ValueError:
        pass
    return None


def _walk_path(word: str, cwd: Path) -> Path:
    """One argument as the directory a walk would start at. `~` and `$HOME` are spelled out because
    that is how a model writes "my home"; a glob is cut back to the literal directory above it, so
    `du -sh /*` is read as a walk of `/`."""
    text = os.path.expanduser(word.replace("${HOME}", "~").replace("$HOME", "~"))
    cut = min((text.index(character) for character in "*?[" if character in text), default=-1)
    if cut >= 0:
        head = text[:cut]
        text = head if head.endswith(os.sep) else os.path.dirname(head)
        if not text:
            text = os.sep if word.startswith(os.sep) else "."
    if not text:
        return cwd
    return Path(os.path.normpath(text if os.path.isabs(text) else os.path.join(str(cwd), text)))


def _walk_roots(segment: str, cwd: Path) -> list[Path]:
    """The roots one command in a Bash line would walk, or [] if it walks nothing."""
    try:
        words = shlex.split(segment)
    except ValueError:
        words = segment.split()
    program = ""
    rest: list[str] = []
    for i, word in enumerate(words):
        if "=" in word and not word.startswith("-") and word.split("=", 1)[0].isidentifier():
            continue                                    # FOO=bar before the program
        if word.startswith("-"):
            continue
        name = os.path.basename(word)
        if name in WALK_PREFIXES:
            continue
        program, rest = name, words[i + 1:]
        break
    if not program:
        return []
    flags = [word for word in rest if word.startswith("-")]
    if program in WALKERS_FLAGGED:
        long_flags = WALKERS_FLAGGED[program]
        recursive = any(flag in long_flags or (not flag.startswith("--") and
                        any(letter in flag[1:] for letter in ("R" if program == "ls" else "rR")))
                        for flag in flags)
        if not recursive:
            return []
    elif program not in WALKERS_ALWAYS:
        return []
    positional = [word for word in rest if not word.startswith("-")]
    if program == "find":
        # find's paths come first and stop at the first predicate (`find . -name x`).
        paths = []
        for word in rest:
            if word.startswith("-"):
                break
            paths.append(word)
    elif program in WALKERS_PATTERN_FIRST:
        # The first positional is the pattern; with none left, the walk starts at the cwd.
        paths = positional[1:]
    else:
        paths = positional
    return [_walk_path(word, cwd) for word in paths] or [cwd]


def walk_cost_refusal(command: str, cwd: Path, home: Path | None = None) -> str | None:
    """The message a too-wide recursive walk is refused with, or None to let it run (card #2Y96)."""
    if not isinstance(command, str) or not command.strip():
        return None
    home = home or home_dir()
    for segment in security.segments(command):
        for root in _walk_roots(segment, cwd):
            if why := wide_root(root, home):
                narrower = cwd if not wide_root(cwd, home) else root
                return (f"Searching all of {root} ({why}) would take minutes and return little, so "
                        f"Relay refuses it: this is a cost limit, not a permission one. Pass a "
                        f"narrower path — e.g. {narrower / '<subdirectory>'} — or ask the user which "
                        f"directory they mean. A single non-recursive listing of {root} is allowed.")
    return None


def spec(name: str, description: str, properties: dict, required: list[str]) -> dict:
    return {"type": "function", "function": {"name": name, "description": description,
            "parameters": {"type": "object", "properties": properties, "required": required,
                           "additionalProperties": False}}}


class ToolResult(dict):
    """A tool result whose copy for the model (`model`) is shorter than what the user's fold and
    the saved turn get, which is the dict itself (card #0C0V). See model_result()."""
    model: dict | None = None


def head_tail(text: str, head_chars: int, tail_chars: int) -> tuple[str, str, int, int, int]:
    """Cut `text` (more than head_chars + tail_chars long) to whole lines at each end.

    Sizes are as sent (jobs.sent_length). Returns (head, tail, first omitted line, last omitted
    line, omitted bytes), lines 1-based in `text`. A line too long to fit is cut inside it, and is
    then counted as omitted."""
    lines = split_lines(text)
    sizes = [sent_length(line) for line in lines]
    head, used = 0, 0
    while head < len(lines) and used + sizes[head] <= head_chars:
        used += sizes[head]
        head += 1
    tail, tail_used = 0, 0
    while (tail < len(lines) - head - 1
           and tail_used + sizes[-1 - tail] <= tail_chars):
        tail_used += sizes[-1 - tail]
        tail += 1
    # A cut inside a line takes half the budget: escaping can at most double what it is sent as.
    head_text = "".join(lines[:head]) if head else text[:head_chars // 2]
    tail_text = "".join(lines[len(lines) - tail:]) if tail else text[-(tail_chars // 2):]
    omitted = len(text.encode("utf-8")) - len(head_text.encode("utf-8")) - len(tail_text.encode("utf-8"))
    return head_text, tail_text, head + 1, len(lines) - tail, max(0, omitted)


def _marker(first: int, last: int, omitted_bytes: int, call: str) -> str:
    count = last - first + 1
    return (f"\n[… {count:,} line{'s' if count != 1 else ''} / {omitted_bytes:,} bytes omitted; "
            f"{call} reads them]\n")


def _range_args(args: dict) -> tuple[int, int | None] | None:
    """from_line/to_line of a command_output or read_file call, checked: None when neither."""
    if "from_line" not in args and "to_line" not in args:
        return None
    first, last = args.get("from_line", 1), args.get("to_line")
    for key, value in (("from_line", first), ("to_line", last)):
        if value is not None and (type(value) is not int or value < 1):
            raise ValueError(f"{key} must be a line number, 1 or more.")
    if last is not None and last < first:
        raise ValueError("to_line must not be before from_line.")
    return first, last


def model_result(name: str, result) -> dict:
    """What the model gets for a tool result (card #0C0V): the bounded copy a command result or a
    file read carries, search matches cut to MODEL_RESULT_CHARS, and anything else unchanged."""
    if isinstance(result, ToolResult) and result.model is not None:
        return result.model
    if name == "search_files" and isinstance(result, dict) and isinstance(result.get("matches"), list):
        kept, used = [], 0
        for match in result["matches"]:
            used += sent_length(str(match)) + 4
            if used > MODEL_RESULT_CHARS:
                break
            kept.append(match)
        if len(kept) < len(result["matches"]):
            return {**result, "matches": kept, "truncated": True,
                    "note": f"{len(result['matches']) - len(kept)} more matches omitted; narrow "
                            "the pattern, path or glob to see them."}
    return result

# Card #DVV2: the lifetimes scratch rows understand (see relay_core/scratch.py). Checked at
# prepare time so a bad lifetime is refused before any directory is made.
SCRATCH_LIFETIME_RE = re.compile(r"^(task|session|until-promoted|user|days:\d+)$")

TOOLS = [
    spec("run_command", "Run a non-interactive Bash command in the chosen workspace, or with host on the ssh host the Relay context names. NOT an OS sandbox. Does not share interactive shell variables or aliases. "
         "Waits up to timeout_seconds (default 30, at most 1800) for the command to finish. A command still running then is NOT killed: "
         "the result has still_running: true, a job_id and the output so far; read more with command_output (it can wait) and end it with stop_command. "
         "Set timeout_seconds to the time a long build or test suite needs; do not ask the user how long it takes. For a server or watcher that should keep running, set background: true and stop it when done. "
         "There is no tty and stdin is closed, so a command that prompts, needs sudo or logs in somewhere fails instead of waiting: hand that one to run_in_terminal when the tool is offered. "
         "A recursive search or listing (grep -r, rg, find, du, ls -R) whose root is the home directory, / or a directory above the home is refused because it would take minutes: give it a narrower path.",
         {"command": {"type": "string"}, "cwd": {"type": "string", "description": "Workspace-relative directory; default '.'"},
          "timeout_seconds": {"type": "integer", "minimum": 1, "maximum": MAX_WAIT,
                              "description": "Seconds to wait before handing a still-running command back as a job; default 30."},
          "background": {"type": "boolean", "description": "Start it and return after a moment with its job_id and first output, for servers and watchers."}},
         ["command"]),
    spec("read_file", "Read a UTF-8 text file inside the workspace. A long file comes back as its head and tail "
         "with total_lines; read the rest with from_line/to_line. A whole-file read stops at 128 KiB; "
         "with from_line/to_line a file up to 8 MiB can be read.",
         {"path": {"type": "string"}, "from_line": {"type": "integer", "minimum": 1},
          "to_line": {"type": "integer", "minimum": 1}}, ["path"]),
    spec("list_directory", "List at most 200 entries in a workspace directory.",
         {"path": {"type": "string"}}, ["path"]),
    spec("write_file", "Create a new UTF-8 file, or replace an existing one in full. To change part of a file that already exists, use edit_file instead: it does not resend the whole file. The diff is shown to the user. Fails if the file changes while the write is prepared.",
         {"path": {"type": "string"}, "content": {"type": "string"}}, ["path", "content"]),
    spec("edit_file", "Change an existing UTF-8 file by replacing an exact string. Preferred over write_file for editing a file you have read. old_string must match the file byte for byte, including whitespace and indentation, and must appear exactly once unless replace_all is true: include enough surrounding lines to make it unique. The diff is shown to the user. Fails if the file changes while the edit is prepared. Works on files up to 8 MiB.",
         {"path": {"type": "string"}, "old_string": {"type": "string", "description": "The exact text to replace, copied from the file."},
          "new_string": {"type": "string", "description": "The text to put in its place; empty deletes the old text."},
          "replace_all": {"type": "boolean", "description": "Replace every occurrence instead of requiring a unique match; default false."}},
         ["path", "old_string", "new_string"]),
    spec("scratch_dir",
         "Ask for a working directory instead of inventing a path (#DVV2): Relay creates it under a root "
         "it owns, records it in the scratch ledger, and returns the path. Never invent temp paths, never "
         "write to /tmp, and never create a new top-level folder under $HOME — ask for scratch instead. "
         "class=scratch (default) is task working space reclaimed at session end; class=keep must live in "
         "the project and be promoted into it later (scratch_release with promote_to) or dropped; "
         "class=install is a persistent tool install root only the user removes.",
         {"class": {"type": "string", "enum": ["scratch", "keep", "install"],
                    "description": "scratch (default), keep, or install"},
          "purpose": {"type": "string", "description": "One line: what this directory is for."},
          "card": {"type": "string", "description": "Board card this directory serves, if any."},
          "lifetime": {"type": "string",
                       "description": "task | session | days:N | until-promoted | user; "
                                      "default follows the class (scratch: session, keep: until-promoted, install: user)."}},
         ["purpose"]),
    spec("scratch_release",
         "End a ledgered scratch directory (#DVV2): ref is the row id scratch_dir returned, or its path. "
         "scratch is reclaimed (deleted safely). keep must be promoted into the project first "
         "(promote_to: a path inside it — the tree is moved there and the row marked promoted) or "
         "explicitly dropped with drop; it is never silently deleted. install is refused: the user "
         "removes those. A refusal here is guidance, not a failure.",
         {"ref": {"type": "string", "description": "Row id or path of the directory to end."},
          "promote_to": {"type": "string",
                         "description": "keep only: move the tree to this path in the project first."},
          "drop": {"type": "boolean", "description": "keep only: delete it explicitly instead of promoting."}},
         ["ref"]),
]

# `host` (card #S5SH): offered only while the user's terminal is logged into a host over ssh (the
# turn's context has remote_session), so a local-only turn never sees it.
HOST_PROPERTY = {"type": "string", "description":
                 "Run on the ssh host the user's terminal is logged into (the Relay context names it), over "
                 "the user's own connection, instead of on this machine. cwd is then a path on that host "
                 "(default: the remote shell's directory). Omit to run locally."}
HOST_READ_PROPERTY = {"type": "string", "description":
                      "Read from the ssh host the user's terminal is logged into (the Relay context names it), "
                      "over the user's own connection, instead of from this machine. path is then a path on "
                      "that host: absolute, ~/…, or relative to the remote shell's directory. Anything the "
                      "user's own account can read there, you can read. Omit for this machine."}
HOST_WRITE_PROPERTY = {"type": "string", "description":
                       "Write the file on the ssh host the user's terminal is logged into (the Relay context "
                       "names it), over the user's own connection, instead of on this machine. path is then a "
                       "path on that host: absolute, ~/…, or relative to the remote shell's directory, and — "
                       "unlike a read — it must be inside the user's home there or the directory their shell "
                       "is in. Omit for this machine."}
#: The tools that take `host`, and the property each one is offered with.
HOST_TOOLS = {"run_command": HOST_PROPERTY, "read_file": HOST_READ_PROPERTY,
              "list_directory": HOST_READ_PROPERTY, "write_file": HOST_WRITE_PROPERTY,
              "edit_file": HOST_WRITE_PROPERTY}


def with_host(tool: dict) -> dict:
    """The same tool, with its `host` property added. The original is left untouched: the tool list
    is rebuilt for every model call, and a turn with no ssh session must see no `host` at all."""
    function = dict(tool["function"])
    parameters = dict(function["parameters"])
    parameters["properties"] = {**parameters["properties"], "host": HOST_TOOLS[function["name"]]}
    function["parameters"] = parameters
    return {**tool, "function": function}


def run_command_spec(with_host_property: bool) -> dict:
    return with_host(TOOLS[0]) if with_host_property else TOOLS[0]


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
    # Card #S5SH: the ssh host this call works on, and the path on it (absolute or relative to the
    # remote shell's directory, guarded by remote_path()). Both None for a call on this machine,
    # which is what `path` above being a real local Path still means.
    host: str | None = None
    remote_path: str | None = None
    # Card #K2FV: the capabilities this call already drew its approval ask for, so the re-check at
    # execution time asks only about what the policy added since — one ask per action.
    approved: tuple[str, ...] = ()
    # Card #F8R7: the open editor's path when this write was worked out against its *unsaved* text
    # rather than the disk's (`old_sha` is then that text's hash). Only the editor can apply it.
    buffer: str | None = None


class Workspace:
    def __init__(self, root: str | Path, policy: security.Policy = security.EMPTY):
        self.root = Path(root).expanduser().resolve(strict=True)
        if not self.root.is_dir():
            raise ValueError("Workspace must be a directory.")
        # Options › Security (#3KB7): folders a *read* may also land in, and extra secret
        # patterns. Writing is never widened past the workspace.
        self.policy = policy

    def resolve(self, name: str, *, allow_missing: bool = False, for_read: bool = False) -> Path:
        """Confine `name` to the workspace. A read may also land in a folder the user listed in
        Options › Security (#3KB7); a write never can, so `for_read` is passed only by the read
        tools. Every other guard — no symlinks, no secret files — applies to an extra folder
        exactly as it does to the workspace."""
        try:
            return self._within(name, self.root, allow_missing=allow_missing)
        except ValueError:
            if not for_read or not self.policy.readable_roots:
                raise
        for root in self.policy.readable_roots:
            try:
                resolved = self._within(name, root, allow_missing=allow_missing)
            except ValueError:
                continue
            return resolved
        raise ValueError("Path escapes the workspace, and is not in a folder Options › Security "
                         "lists as readable.")

    def _within(self, name: str, root: Path, *, allow_missing: bool = False) -> Path:
        if not isinstance(name, str) or not name or "\x00" in name:
            raise ValueError("A valid path is required.")
        # Card #E99H (owner, 2026-09-18): an absolute path and a `..` in the middle of one are
        # ordinary ways to name a file, so neither is refused on sight any more. What confines a
        # call is the same thing it always was — the path has to land inside the workspace, and
        # the symlink and secret-file guards below walk what it lands on.
        candidate = Path(name)
        if not candidate.is_absolute():
            candidate = root / candidate
        # Collapse `..` textually first: a lexical path is what the guards below can walk, and it
        # keeps a `..` from being answered by the filesystem before this check runs.
        candidate = Path(os.path.normpath(candidate))
        if not candidate.is_relative_to(root):
            raise ValueError("Path escapes the workspace.")
        relative = candidate.relative_to(root)
        # Refuse symlinks even when they lead back inside the workspace.
        current = root
        for part in relative.parts:
            current /= part
            if current.is_symlink():
                raise ValueError("File tools do not follow symlinks.")
        resolved = candidate.resolve(strict=not allow_missing)
        if not resolved.is_relative_to(root):
            raise ValueError("Path escapes the workspace.")
        if any(looks_secret(part) for part in relative.parts):
            raise ValueError("This path is blocked by Relay's basic secret-file guard.")
        if any(security.extra_secret(self.policy, part) for part in relative.parts):
            raise ValueError("This path is blocked by a secret pattern in Options › Security.")
        return resolved

    @staticmethod
    def read_bytes(path: Path, limit: int = MAX_LARGE_FILE) -> bytes:
        """A regular UTF-8 text file of at most `limit` bytes. A whole-file read_file holds itself
        to MAX_FILE in _read_result; edits and ranged reads need only this (card #XG2G)."""
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        fd = os.open(path, flags)
        with os.fdopen(fd, "rb") as handle:
            info = os.fstat(handle.fileno())
            if not stat.S_ISREG(info.st_mode):
                raise ValueError("Only regular files are supported.")
            if info.st_size > limit:
                raise ValueError(LARGE_FILE_REFUSAL if limit >= MAX_LARGE_FILE else WHOLE_READ_REFUSAL)
            data = handle.read(limit + 1)
        if len(data) > limit or b"\x00" in data:
            raise ValueError("File is too large or binary.")
        data.decode("utf-8")
        return data


class ToolExecutor:
    def __init__(self, root: str, emit: Callable[[dict], None], cancel: threading.Event,
                 keybindings: KeybindingCatalog | None = None, skills: SkillIndex | None = None,
                 policy: security.Policy = security.EMPTY):
        # Options › Security (#3KB7): the command denylist, extra readable folders and extra
        # secret patterns. Held here rather than in Workspace alone because the denylist guards
        # run_command, which has no path to resolve.
        self.policy = policy
        self.workspace = Workspace(root, policy)
        # Card #K2FV: which capabilities stop and ask before they happen (relay_core/approvals.py).
        # Allow-all unless the pane says otherwise: the cautious set belongs to a fresh install's
        # first-launch choice, which the GUI's configure always carries (approvals_chosen: false).
        self.approvals = approvals.ALLOW_ALL
        # Whether an ask may be drawn at all. Distinct from `can_ask` on purpose: a subagent cannot
        # ask a question (nobody knows it exists) but its actions still draw approval asks in the
        # pane it belongs to, named as the subagent's.
        self.may_approve = True
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
        # Asking the user a question and waiting for the answer; see relay_core/questions.py.
        # Offered in both modes (owner, 2026-09-19: "let the non-plan agent use the questions as
        # well (like warp / claude)"); `can_ask` is off for a subagent's executor, which cannot see
        # the pane the ask would be drawn in.
        self.questions = Questions(emit, cancel)
        # Messaging another pane of this Relay (relay_core/panes.py, card #R5TC). A tool family
        # like the two above: emit to the pane, wait for its answer. Offered only to a main
        # agent whose GUI has sent a pane roster — `RestrictedExecutor` (subagents.py) builds
        # its tool list without it, and a subagent is never told which pane it runs in.
        self.panes = PaneMessaging(emit, cancel)
        self.can_ask = True
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
        # Card #F8R7: the files open in Relay's editor (relay_core/open_buffers.py), set by the
        # worker; None writes the disk as always. `provenance` is the turn and model a write is
        # labelled with in the editor, set by the agent before each write.
        self.buffers = None
        self.provenance: dict = {}

    def _approval(self, name: str, args: dict, *, exists: bool = False, outside_workspace: bool = False,
                  subject: str = "", already: tuple[str, ...] = ()) -> tuple[str, ...]:
        """Card #K2FV: put the approval ask up for a call the checklist asks about.

        Checked while preparing, so nothing has run when the ask goes up — and again at execution
        for run_command, whose policy may change while an ask sits unanswered. A capability already
        approved for this call (`already`) or for the rest of the turn is not asked for again. Deny
        raises the refusal the model reads and carries on; Stop still stops the turn, exactly as it
        does under a question (questions.ask_approval shares the round trip).
        """
        if not self.may_approve:
            return already
        wanted = [capability for capability
                  in approvals.needed(self.approvals, name, args, exists=exists,
                                      outside_workspace=outside_workspace)
                  if capability not in already and not self.questions.turn_allows(capability)]
        for capability in wanted:
            if self.questions.ask_approval(capability, subject) == "deny":
                raise ValueError(approvals.refusal(capability))
        return already + tuple(wanted)

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
            # run_command and the file tools all reach the host the user is logged into.
            tools = [with_host(tool) if tool["function"]["name"] in HOST_TOOLS else tool
                     for tool in tools]
        tools += JOB_TOOLS
        if self.skills is not None:
            tools += SKILL_TOOLS
        # Read at every model call, so a take-over removes the tool from the next one.
        if self.program.available():
            tools = tools + [self.program.tool_spec()]
        if self.terminal.available():
            tools = tools + [self.terminal.tool_spec()]
        if self.can_ask:
            tools = tools + [self.questions.tool_spec()]
        # Read per call like the two above: the roster arrives with the first pane_message push
        # and the kill switch (agent/cross_pane) removes the tools from the next call.
        if self.panes.available():
            tools = tools + self.panes.tool_specs()
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
            self._approval(name, payload, subject=_subject(payload))
            return Prepared(name, payload, preview)
        if name == "run_in_terminal":
            payload, preview = self.terminal.prepare(args)
            self._approval(name, payload, subject=payload["command"])
            return Prepared(name, payload, preview)
        if name == "scratch_dir":
            cls = args.get("class", "scratch")
            if cls not in ("scratch", "keep", "install"):
                raise ValueError("class must be scratch, keep or install.")
            purpose = self._text(args, "purpose", maximum=300)
            if not purpose.strip() or "\n" in purpose:
                raise ValueError("purpose must be one non-empty line.")
            card = args.get("card", "")
            if card is not None and not isinstance(card, str):
                raise ValueError("card must be text.")
            lifetime = args.get("lifetime")
            if lifetime is not None and (not isinstance(lifetime, str)
                                         or not SCRATCH_LIFETIME_RE.match(lifetime.strip())):
                raise ValueError("lifetime must be task, session, days:N, until-promoted or user.")
            args.update(purpose=purpose, card=(card or "").strip())
            args["class"] = cls
            if lifetime is not None:
                args["lifetime"] = lifetime.strip()
            return Prepared(name, args, f"SCRATCH DIR ({cls})\n\n{purpose}")
        if name == "scratch_release":
            ref = self._text(args, "ref", maximum=4096)
            promote_to = args.get("promote_to")
            if promote_to is not None and not isinstance(promote_to, str):
                raise ValueError("promote_to must be text.")
            drop = args.get("drop", False)
            if not isinstance(drop, bool):
                raise ValueError("drop must be true or false.")
            args.update(ref=ref, drop=drop)
            return Prepared(name, args, f"SCRATCH RELEASE\n\n{ref}")
        if name == "ask_user":
            if not self.can_ask:
                raise ValueError("ask_user is not available here: you cannot reach the user.")
            payload, preview = self.questions.prepare(args)
            return Prepared(name, payload, preview)
        if name in ("pane_list", "pane_send"):
            payload, preview = self.panes.prepare(name, args)
            return Prepared(name, payload, preview)
        if name == "set_keybinding":
            catalog = self.keybindings
            if catalog is None:
                raise ValueError("Unknown tool or unexpected argument.")
            normalized, preview = catalog.prepare(args)
            return Prepared(name, normalized, preview, catalog.path)
        if name in ("command_output", "stop_command"):
            if set(args) - ({"job_id", "wait_seconds", "from_line", "to_line"}
                            if name == "command_output" else {"job_id"}):
                raise ValueError("Unknown tool or unexpected argument.")
            job = self.jobs.get(args.get("job_id"))
            if name == "stop_command":
                return Prepared(name, {"job_id": job.id}, f"STOP COMMAND\n\n{job.id}: {job.command}")
            wait = clamp_seconds(args.get("wait_seconds", 0), 0, 0, MAX_WAIT)
            prepared = {"job_id": job.id, "wait_seconds": wait}
            shown = f"COMMAND OUTPUT\n\n{job.id}: {job.command}\nWait: up to {wait}s"
            if (lines := _range_args(args)) is not None:
                prepared.update({"from_line": lines[0], "to_line": lines[1]})
                shown += f"\nLines: {lines[0]}-{lines[1] or 'end'}"
            return Prepared(name, prepared, shown)
        allowed = {"run_command": {"command", "cwd", "timeout_seconds", "background", "host"},
                   "read_file": {"path", "host", "from_line", "to_line"}, "list_directory": {"path", "host"},
                   "write_file": {"path", "content", "host"},
                   "edit_file": {"path", "old_string", "new_string", "replace_all", "host"}}
        if name not in allowed or set(args) - allowed[name]:
            raise ValueError("Unknown tool or unexpected argument.")
        host = self._host(args)
        if name == "read_file":
            _range_args(args)                     # refused here, before anything is read
        if name == "run_command":
            command = self._text(args, "command", maximum=16384)
            if not command.strip():
                raise ValueError("Command must not be empty.")
            # Options › Security (#3KB7). Checked here as well as at execution so the refusal is
            # immediate and no preview shows a command that will not run. A denylist is honoured,
            # not unevadable — see relay_core/security.py.
            if rule := security.denied_command(self.policy, command):
                raise ValueError(security.refusal(rule))
            # Card #K2FV: the ask goes up while preparing, before anything runs.
            approved = self._approval(name, args, subject=command)
            background = args.get("background", False)
            if not isinstance(background, bool):
                raise ValueError("background must be true or false.")
            args["background"] = background
            timeout = clamp_seconds(args.get("timeout_seconds", DEFAULT_WAIT), DEFAULT_WAIT, 1, MAX_WAIT)
            args["timeout_seconds"] = timeout
            wait = "Background" if background else f"Waits: {timeout}s, then continues as a job"
            if host:
                return self._prepare_remote(args, command, host, wait, approved=approved)
            args["cwd"] = args.get("cwd") or self.default_cwd
            cwd = self.workspace.resolve(args["cwd"])
            if not cwd.is_dir():
                raise ValueError("Command working directory must be a directory.")
            # Card #2Y96: a recursive walk of the home directory or `/` is refused on cost. Checked
            # here only, unlike the denylist: the answer depends on the command and the cwd, neither
            # of which changes between prepare and execute.
            if message := walk_cost_refusal(command, cwd):
                raise ValueError(message)
            return Prepared(name, args, f"RUN COMMAND\n\nWorking directory: {cwd}\n{wait}\n\n{command}", cwd,
                            approved=approved)
        if host:
            return self._prepare_remote_file(name, args, host)
        path = self.workspace.resolve(self._text(args, "path", maximum=4096),
                                      allow_missing=name in ("write_file", "edit_file"),
                                      for_read=name in ("read_file", "list_directory"))
        if name == "read_file":
            self._approval(name, args, outside_workspace=not path.is_relative_to(self.workspace.root),
                           subject=str(path))
            return Prepared(name, args, f"READ FILE\n\n{path}", path)
        if name == "list_directory":
            self._approval(name, args, outside_workspace=not path.is_relative_to(self.workspace.root),
                           subject=str(path))
            return Prepared(name, args, f"LIST DIRECTORY\n\n{path}", path)
        existed = path.exists()
        # A file open in Relay with unsaved edits is edited as the user sees it (card #F8R7).
        unsaved = self._unsaved_buffer(local_key(path)) if existed else None
        if name == "edit_file":
            if not existed:
                raise ValueError("edit_file needs a file that already exists; use write_file to create one.")
            old = unsaved[1] if unsaved else self.workspace.read_bytes(path)
            content, replacements = self._edited(args, old)
        else:
            # Missing parent directories are made at execute time (card #NC17): refusing here cost
            # the model a whole round trip on mkdir for what is the common case.
            content, replacements = self._text(args, "content"), 0
            old = unsaved[1] if unsaved else (self.workspace.read_bytes(path) if existed else b"")
        self._approval(name, args, exists=existed, subject=str(path))
        return self._write_prepared(name, args, str(path), old, content, existed, replacements, path=path,
                                    header=UNSAVED_HEADER if unsaved else "",
                                    buffer=unsaved[0]["path"] if unsaved else None)

    def _write_prepared(self, name: str, args: dict, shown: str, old: bytes, content: str, existed: bool,
                        replacements: int, *, path: Path | None = None, host: str | None = None,
                        header: str = "", buffer: str | None = None) -> Prepared:
        """The diff the user sees and the bytes the write will put in place — the same computation
        for a file on this machine and one on the ssh host (card #S5SH)."""
        old_sha = hashlib.sha256(old).hexdigest()
        diff = "".join(difflib.unified_diff(old.decode("utf-8").splitlines(keepends=True),
                     content.splitlines(keepends=True), fromfile=f"a/{args['path']}" if existed else "/dev/null",
                     tofile=f"b/{args['path']}"))
        added = sum(1 for line in diff.splitlines() if line.startswith("+") and not line.startswith("+++"))
        removed = sum(1 for line in diff.splitlines() if line.startswith("-") and not line.startswith("---"))
        # Preserve reviewability for files whose only change is a trailing newline.
        title = "EDIT FILE" if name == "edit_file" else "WRITE FILE"
        preview = (f"{title}{f' ON {host}' if host else ''}\n\n{header}{shown}\n\n{diff or '(No text changes)'}"
                   f"\n\nOld bytes: {len(old)}; new bytes: {len(content.encode('utf-8'))}.")
        return Prepared(name, args, preview, path, old_sha, existed, content, replacements, added, removed,
                        diff=diff, host=host, remote_path=shown if host else None, buffer=buffer)

    def _host(self, args: dict) -> str:
        """The `host` argument, checked as text. Empty or absent is a call on this machine, and the
        key is dropped so nothing downstream (labels, the fold) shows a host that was not used."""
        host = args.get("host")
        if host is not None and not isinstance(host, str):
            raise ValueError("host must be text: the host the user's terminal is logged into.")
        if not host:
            args.pop("host", None)
            return ""
        return host

    def _prepare_remote_file(self, name: str, args: dict, host: str) -> Prepared:
        """read_file, list_directory, write_file and edit_file on the ssh host (card #S5SH).

        The path is a path on the host, so it is not resolved against the workspace; the rules that
        replace the workspace are in remote_path() and in the scripts (relay_core/remote_files.py).
        A write reads the file first, over the same connection, so the user sees the real diff."""
        session = self._remote_ready(host)
        path = remote_path(self._text(args, "path", maximum=4096), self.policy)
        user = session.get("user")
        header = f"Host: {f'{user}@{host}' if user else host} (over the user's ssh connection)\n"
        if name == "read_file":
            # No read_outside ask here: that row is about the folders Options › Security adds for
            # reads on this machine (#3KB7). On the host there are no extra folders — anything the
            # user's account can read there is the read tools' stated contract (card #S5SH).
            return Prepared(name, args, f"READ FILE ON {host}\n\n{header}{path}", host=host, remote_path=path)
        if name == "list_directory":
            return Prepared(name, args, f"LIST DIRECTORY ON {host}\n\n{header}{path}", host=host,
                            remote_path=path)
        old, existed = self._remote_before(session, path)
        # Open in Relay with unsaved edits (card #F8R7): edited as the user sees it, as locally.
        unsaved = (self._unsaved_buffer(remote_key(session["host"], path, session.get("cwd")))
                   if existed else None)
        if unsaved:
            old = unsaved[1]
        if name == "edit_file":
            if not existed:
                raise ValueError("edit_file needs a file that already exists; use write_file to create one.")
            content, replacements = self._edited(args, old)
        else:
            content, replacements = self._text(args, "content"), 0
        # The edit/create ask (card #K2FV) travels: a file on the ssh host is still a file the
        # user may want asked about, and the ask names the host in its subject line.
        self._approval(name, args, exists=existed, subject=f"{path} on {host}")
        return self._write_prepared(name, args, path, old, content, existed, replacements, host=host,
                                    header=header + (UNSAVED_HEADER if unsaved else "") + "\n",
                                    buffer=unsaved[0]["path"] if unsaved else None)

    def _prepare_remote(self, args: dict, command: str, host: str, wait: str,
                        approved: tuple[str, ...] = ()) -> Prepared:
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
        return Prepared("run_command", args, preview, approved=approved)

    # ----- the file tools on the ssh host (card #S5SH) -----------------------------------

    def _remote_ready(self, host: str) -> dict:
        """The session a `host` argument may use, with its connection-sharing socket still there.

        run_command checks the socket only when it runs; a file tool reads the host while it is
        prepared (the diff the user approves is the real one), so it checks here too."""
        session = remote_session.check_host(self.remote_session, host)
        remote_session.require_socket(session)
        return session

    def _remote_run(self, session: dict, script: str, path: str, *, stdin: bytes = b"",
                    wrong_type: str = "Only regular files are supported.") -> bytes:
        """One script on the host, through run_command's argv builder and environment."""
        if self.cancel.is_set():
            raise Cancelled("Stopped.")
        cwd = session.get("cwd") or None
        proc = remote_files.run(session, script, cwd=None if path.startswith("/") else cwd,
                                stdin=stdin, env=command_env())
        return remote_files.output(proc, session, path, wrong_type=wrong_type)

    def _remote_read(self, session: dict, path: str) -> bytes:
        """A remote file's text, refused for the same reasons the local read refuses it. Read up to
        MAX_LARGE_FILE, so a ranged read works; a whole read is held to MAX_FILE in _read_result."""
        data = self._remote_run(session, remote_files.read_script(
            path, session.get("cwd") or None, cap=MAX_LARGE_FILE + 1), path)
        return _as_text(data)

    def _remote_before(self, session: dict, path: str) -> tuple[bytes, bool]:
        """What a write or an edit is about to replace, and whether the file is there at all.

        Contained like the write it belongs to, so a write outside the remote home is refused by the
        first call rather than after reading the file it may not touch."""
        script = remote_files.read_script(path, session.get("cwd") or None, cap=MAX_LARGE_FILE + 1, optional=True,
                                          contain=True)
        cwd = session.get("cwd") or None
        if self.cancel.is_set():
            raise Cancelled("Stopped.")
        proc = remote_files.run(session, script, cwd=None if path.startswith("/") else cwd,
                                env=command_env())
        if proc.returncode == remote_files.MISSING:
            return b"", False
        return _as_text(remote_files.output(proc, session, path)), True

    def _execute_remote_file(self, prepared: Prepared) -> dict:
        """The prepared call, done on the host. The session is checked again first: the user may
        have logged out between the diff and this."""
        name, args, path = prepared.name, prepared.arguments, prepared.remote_path
        session = self._remote_ready(prepared.host)
        if name == "read_file":
            if unsaved := self._unsaved_buffer(remote_key(session["host"], path, session.get("cwd"))):
                return self._unsaved_result(self._read_result(args, unsaved[1], host=session["host"]))
            data = self._remote_read(session, path)
            return self._read_result(args, data, host=session["host"])
        if name == "list_directory":
            data = self._remote_run(session, remote_files.list_script(path, session.get("cwd") or None),
                                    path, wrong_type="Path is not a directory.")
            found, truncated = remote_files.entries(data)
            return {"entries": found, "truncated": truncated, "host": session["host"]}
        if (routed := self._through_buffer(prepared, remote_key(session["host"], path, session.get("cwd")),
                                           remote=True)) is not None:
            routed["host"] = session["host"]
            return routed
        if prepared.buffer:
            raise ValueError(NO_EDITOR_ANSWER)
        old, existed = self._remote_before(session, path)
        if existed != prepared.existed:
            raise ValueError("File appeared or disappeared while the write was prepared. Request a new diff.")
        if hashlib.sha256(old).hexdigest() != prepared.old_sha:
            raise ValueError("File changed while the write was prepared. Nothing was overwritten; request a fresh diff.")
        data = (prepared.content if prepared.content is not None else args["content"]).encode("utf-8")
        self._remote_run(session, remote_files.write_script(path, session.get("cwd") or None), path,
                         stdin=data)
        result = {"path": args["path"], "written_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                  "added": prepared.added, "removed": prepared.removed, "host": session["host"]}
        if name == "edit_file":
            result["replacements"] = prepared.replacements
        else:
            result["created"] = not prepared.existed
        return result

    # ----- files open in Relay's editor (card #F8R7, protocol §35) ---------------------------

    def _unsaved_buffer(self, key: str | None) -> tuple[dict, bytes] | None:
        """The open editor's text, when this file is open in Relay *with unsaved edits*: that text
        is what the user is looking at, so it is what a read shows and an edit is worked out
        against. A clean editor holds the disk's text, so it costs no round trip; neither does a
        file nobody has open, or a GUI that did not answer."""
        entry = self.buffers.entry(key) if self.buffers is not None else None
        if entry is None or not entry.get("dirty"):
            return None
        reply = self.buffers.request({"op": "read", "path": entry["path"]})
        if not reply or not reply.get("ok") or not isinstance(reply.get("text"), str):
            return None
        return entry, reply["text"].encode("utf-8")

    @staticmethod
    def _unsaved_result(result: dict) -> dict:
        result["open_buffer"] = "unsaved"
        result["note"] = ("This is the text in the user's open editor in Relay, which has unsaved edits; "
                          "the file on disk differs until they save. Commands you run see the disk.")
        return result

    def _through_buffer(self, prepared: Prepared, key: str | None, *, remote: bool = False) -> dict | None:
        """write_file / edit_file on a file open in Relay: the editor applies it (src/FilePanes.cpp,
        FilePreview::applyAgentPatch) — exactly when its text is the one this was worked out
        against, merged when the user's unsaved edits are elsewhere in the file, and refused with
        the lines in question when they overlap. None when the file is not open or nothing
        answered, and the caller writes the disk as it always did."""
        entry = self.buffers.entry(key) if self.buffers is not None else None
        if entry is None:
            return None
        name, args = prepared.name, prepared.arguments
        content = prepared.content if prepared.content is not None else args["content"]
        what = "Edit" if name == "edit_file" else "Write"
        fields = {"op": "patch", "path": entry["path"], "tool": name, "base_sha256": prepared.old_sha,
                  "content": content,
                  "intent": f"{what} {posixpath.basename(str(args['path']))} (+{prepared.added} -{prepared.removed})",
                  "turn_id": self.provenance.get("turn_id"), "model": self.provenance.get("model")}
        if name == "edit_file":
            fields.update(old_string=args["old_string"], new_string=args["new_string"],
                          replace_all=bool(args.get("replace_all", False)))
        reply = self.buffers.request(fields, remote=remote)
        if reply is None:
            return None
        if not reply.get("ok"):
            error = reply.get("error")
            if error in ("not_open", "not_text", "not_representable", "unsupported") and not prepared.buffer:
                return None   # the editor cannot hold it; the disk write follows, and its watcher sees it
            if error == "conflict":
                raise ValueError(conflict_text({**reply, "path": args["path"]}))
            message = reply.get("message") if isinstance(reply.get("message"), str) else ""
            if error == "stale":
                raise ValueError(message or f"{args['path']} changed in the editor while this was prepared, so "
                                 "nothing was changed. Read it again and redo the edit.")
            raise ValueError(message or f"Relay could not apply this to {args['path']}, which is open in the "
                             "editor; nothing was changed.")
        saved = reply.get("saved") is True
        result = {"path": args["path"], "written_bytes": len(content.encode("utf-8")) if saved else 0,
                  "sha256": reply.get("sha256") if isinstance(reply.get("sha256"), str) else prepared.old_sha,
                  "added": prepared.added, "removed": prepared.removed,
                  "open_buffer": {"applied": reply.get("applied") or "exact", "saved": saved}}
        if name == "edit_file":
            result["replacements"] = prepared.replacements
        else:
            result["created"] = not prepared.existed
        where = f"{args['path']} is open in Relay's editor"
        if saved:
            result["note"] = f"{where}: the change was applied there as one undoable step and saved."
        else:
            reason = reply.get("save_error") if isinstance(reply.get("save_error"), str) else ""
            result["note"] = (f"{where} with unsaved edits: the change was applied to that unsaved buffer "
                              f"(one undoable step), not to the file on disk. It reaches the disk when the "
                              f"user saves; commands you run see the disk until then."
                              + (f" Saving failed: {reason}" if reason else ""))
        if reply.get("applied") == "merged":
            result["note"] += " It was merged around the user's own unsaved edits."
        if reply.get("applied") == "held":
            # Review before apply (card #PBZ4, owner decision D1): nothing is in the file yet.
            result["written_bytes"] = 0
            result["note"] = (f"{where} with Review before apply on: your change is waiting on the file's "
                              "agent bar for the user to Apply or Discard. It is not in the buffer or on disk "
                              "yet, so a read shows the file without it. Tell them what you proposed; do not "
                              "make the edit again.")
        elif reply.get("applied") == "conflict":
            # Same-line conflicts are merged inline (owner decision D2): both versions are in the buffer.
            regions = [c for c in (reply.get("conflicts") or []) if isinstance(c, dict)]
            lines = ", ".join(str(c["line"]) for c in regions if isinstance(c.get("line"), int))
            result["note"] = (f"{where} and the user has unsaved edits on the same lines, so the change went into "
                              f"that unsaved buffer with both versions between conflict markers (<<<<<<< editor, "
                              f"||||||| base, =======, >>>>>>> agent)"
                              + (f" at line {lines}" if lines else "")
                              + ". They resolve it; do not edit those lines again unless they ask, and say that "
                              "the conflict is there.")
            result["open_buffer"]["conflicts"] = len(regions)
        return result

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
            raise ValueError("old_string was not found in the file. " + _closest_mismatch(text, old_string)
                             + "Read the file again and copy the exact text, including whitespace and indentation.")
        if found > 1 and not replace_all:
            raise ValueError(f"old_string occurs {found} times in the file. Add surrounding lines so it matches "
                             f"once, or set replace_all: true to change all {found}.")
        content = text.replace(old_string, new_string) if replace_all else text.replace(old_string, new_string, 1)
        if len(content.encode("utf-8")) > MAX_LARGE_FILE:
            raise ValueError("The edited file would exceed the 8 MiB limit of Relay's file tools.")
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
        # `ask_user` is not here: `Agent._execute` runs it itself, because the ask has to carry
        # the turn it belongs to (`turn_id`) and nothing else in a Prepared does. A copy here
        # could only ever be reached by a caller that had bypassed the agent, and would drop it.
        if name == "set_keybinding":
            catalog = self.keybindings
            if catalog is None or args["action"] not in catalog.actions:
                raise ValueError("Keybinding catalog changed; the action is no longer available.")
            return catalog.apply(args)
        if name == "run_command" and args.get("host"):
            # Recheck the session at execution time: the user may have logged out since.
            session = self._remote_ready(args["host"])
            self._approval("run_command", args, subject=f"{args['command']} (on {args['host']})",
                           already=prepared.approved)
            argv = remote_session.ssh_argv(session, args["command"], args.get("cwd"))
            return self._run(args["command"], self.workspace.root, args["timeout_seconds"],
                             args.get("background", False), argv=argv, host=session["host"])
        if name == "run_command":
            # Recheck the denylist and the paths at execution time: the policy may have changed
            # since prepare, and a path may have become a symlink.
            if rule := security.denied_command(self.policy, args["command"]):
                raise ValueError(security.refusal(rule))
            # Card #K2FV: the checklist may have changed while the ask sat unanswered; the
            # capabilities approved at prepare are not asked for again (Prepared.approved).
            self._approval("run_command", args, subject=args["command"], already=prepared.approved)
            cwd = self.workspace.resolve(args.get("cwd", "."))
            return self._run(args["command"], cwd, args["timeout_seconds"], args.get("background", False))
        if name == "command_output":
            job = self.jobs.get(args["job_id"])
            if "from_line" in args:
                return self._job_lines(job, args)
            return self._await(job, args["wait_seconds"])
        if name == "stop_command":
            job = self.jobs.get(args["job_id"])
            self.jobs.stop(job)
            return self._job_result(job)
        if prepared.host:
            return self._execute_remote_file(prepared)
        path = self.workspace.resolve(args["path"], allow_missing=name in ("write_file", "edit_file"),
                                      for_read=name in ("read_file", "list_directory"))
        if name == "read_file":
            if unsaved := self._unsaved_buffer(local_key(path)):
                return self._unsaved_result(self._read_result(args, unsaved[1]))
            return self._read_result(args, self.workspace.read_bytes(path))
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
        # A file open in Relay's editor is changed there, as one undo step (card #F8R7). No answer
        # from the editor, or no editor, is the disk write below, guarded by the revision read.
        if (routed := self._through_buffer(prepared, local_key(path))) is not None:
            return routed
        if prepared.buffer:
            raise ValueError(NO_EDITOR_ANSWER)
        if path.exists() != prepared.existed:
            raise ValueError("File appeared or disappeared while the write was prepared. Request a new diff.")
        old = self.workspace.read_bytes(path) if path.exists() else b""
        if hashlib.sha256(old).hexdigest() != prepared.old_sha:
            raise ValueError("File changed while the write was prepared. Nothing was overwritten; request a fresh diff.")
        # edit_file computed its whole new text while preparing; write_file carries the model's.
        data = (prepared.content if prepared.content is not None else args["content"]).encode("utf-8")
        mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o600
        try:
            # write_file creates the directories its path needs (card #NC17); for edit_file the
            # file exists, so this is a no-op.
            path.parent.mkdir(parents=True, exist_ok=True)
        except OSError as error:
            raise ValueError(f"The directories above {path} could not be created "
                             f"({error.strerror or error}). Check the path, then try again.") from None
        fd, tempname = tempfile.mkstemp(prefix=".relay-write-", dir=path.parent)
        try:
            with os.fdopen(fd, "wb") as out:
                chmod_fd(out.fileno(), mode)
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
        job = self.jobs.start(command, cwd, command_env(), argv=argv, host=host)
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

    def _job_lines(self, job, args: dict) -> dict:
        """command_output with from_line/to_line: a re-read of the job's kept output, which leaves
        the unread position alone (card #0C0V). A wait still waits first."""
        if args["wait_seconds"] and job.running:
            with self._lock:
                self._waiting = job
            try:
                self.jobs.wait(job, args["wait_seconds"], self.cancel)
            finally:
                with self._lock:
                    self._waiting = None
            if self.cancel.is_set():
                self.jobs.stop(job)
                raise Cancelled("Stopped.")
        result = self.jobs.read_lines(job, args["from_line"], args["to_line"], MODEL_RESULT_CHARS)
        if job.running:
            result["still_running"] = True
        else:
            result["exit_code"] = job.exit_code
        return result

    def _read_result(self, args: dict, data: bytes, *, host: str | None = None) -> dict:
        """read_file's result: the whole file for the user, and for the model either the lines
        asked for or, past MODEL_RESULT_CHARS, the head and tail and how to read the rest. Only a
        whole-file read is held to MAX_FILE; a range of a bigger file is read (card #XG2G)."""
        ranged = "from_line" in args or "to_line" in args
        if not ranged and len(data) > MAX_FILE:
            raise ValueError(WHOLE_READ_REFUSAL)
        text = data.decode("utf-8")
        result = ToolResult({"path": args["path"], "content": text, "sha256": hashlib.sha256(data).hexdigest()})
        if host:
            result["host"] = host
        lines = split_lines(text)
        if ranged:
            chosen = line_range(lines, args.get("from_line", 1), args.get("to_line"), MODEL_RESULT_CHARS)
            result.update(chosen)
            result["content"] = result.pop("output")
            return result
        if sent_length(text) > MODEL_RESULT_CHARS:
            head, tail, first, last, omitted = head_tail(text, FILE_HEAD_CHARS,
                                                         MODEL_RESULT_CHARS - FILE_HEAD_CHARS)
            where = f', host="{host}"' if host else ""
            call = f'read_file(path={json.dumps(args["path"])}{where}, from_line={first}, to_line={last})'
            result.model = {**result, "content": head + _marker(first, last, omitted, call) + tail,
                            "total_lines": len(lines), "bytes": len(data)}
        return result

    def _job_result(self, job) -> dict:
        data, first_line, dropped = self.jobs.take(job)
        output = data[-MAX_OUTPUT:] if len(data) > MAX_OUTPUT else data
        omitted = dropped + len(data) - len(output)
        result = ToolResult({"output": output.decode("utf-8", "replace"), "truncated": omitted > 0,
                             "omitted_bytes": omitted})
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
        text = data.decode("utf-8", "replace")
        if sent_length(text) > MODEL_RESULT_CHARS:
            # Card #0C0V: the model's copy is the head and tail of everything it had not read yet,
            # with exact counts and the call that re-reads the middle; the user's is `output` above.
            head, tail, first, last, omitted = head_tail(text, COMMAND_HEAD_CHARS,
                                                         MODEL_RESULT_CHARS - COMMAND_HEAD_CHARS)
            call = (f'command_output(job_id="{job.id}", from_line={first_line + first - 1}, '
                    f'to_line={first_line + last - 1})')
            lines, total = self.jobs.counts(job)
            model = {key: value for key, value in result.items() if key not in ("truncated", "omitted_bytes")}
            model["output"] = head + _marker(first, last, omitted, call) + tail
            model.update({"total_lines": lines, "total_bytes": total})
            if dropped:
                model["dropped_bytes"] = dropped
            result.model = model
        return result


def command_env() -> dict:
    """The environment every command Relay starts gets: this process's, without the names that
    carry secrets or would change how a shell starts. ssh inherits it too (card #S5SH) — without
    SSH_AUTH_SOCK, because the user's master connection needs no agent."""
    env = {key: value for key, value in os.environ.items()
           if not SECRET_NAME.search(key) and not key.startswith("RELAY_")
           and key not in {"BASH_ENV", "ENV", "PYTHONPATH", "LD_PRELOAD", "LD_LIBRARY_PATH", "SSH_AUTH_SOCK"}
           and not key.startswith("BASH_FUNC_")}
    env.update({"TERM": "dumb", "PAGER": "cat", "GIT_TERMINAL_PROMPT": "0"})
    return env


def _closest_mismatch(text: str, old_string: str) -> str:
    """Where a missed edit_file old_string first stops matching the file (card #XG2G), so the retry
    is one step: its longest line that does occur anchors it — or, when the difference is inside
    the only line, that line's first or second half — the match is grown back and forward from
    there, and the first differing character is named by line and column. Empty when nothing in
    old_string is distinctive enough to anchor on."""
    lines = sorted({line.strip() for line in old_string.split("\n")}, key=len, reverse=True)
    halves = [part.strip() for line in lines for part in (line[:len(line) // 2], line[len(line) // 2:])]
    keys = [key for key in lines[:8] + halves[:16] if len(key) >= 8]
    best = None
    for key in keys:
        at_old, at_file, seen = old_string.find(key), text.find(key), 0
        while at_file != -1 and seen < 50:
            back = 0
            while back < at_old and at_file - back > 0 and text[at_file - back - 1] == old_string[at_old - back - 1]:
                back += 1
            ahead = at_old + len(key)
            end = at_file + len(key)
            forward = 0
            while (ahead + forward < len(old_string) and end + forward < len(text)
                   and text[end + forward] == old_string[ahead + forward]):
                forward += 1
            if back < at_old:
                where = (at_file - back - 1, at_old - back - 1)
            else:
                where = (end + forward, ahead + forward)
            if best is None or back + forward > best[0]:
                best = (back + forward, *where)
            seen += 1
            at_file = text.find(key, at_file + 1)
        if best is not None:
            break
    if best is None:
        return ""
    _, in_file, in_old = best
    line = text.count("\n", 0, in_file) + 1
    column = in_file - (text.rfind("\n", 0, in_file) + 1) + 1
    old_line = old_string.count("\n", 0, in_old) + 1
    has = json.dumps(text[in_file:in_file + 30]) if in_file < len(text) else "the end of the file"
    wants = json.dumps(old_string[in_old:in_old + 30])
    return (f"The closest match first differs at line {line}, column {column}: the file has {has} where "
            f"old_string (its line {old_line}) has {wants}. ")


def _as_text(data: bytes) -> bytes:
    """A file read from an ssh host, refused for the same reasons Workspace.read_bytes refuses a
    local one: too big, binary, or not UTF-8."""
    if len(data) > MAX_LARGE_FILE:
        raise ValueError(LARGE_FILE_REFUSAL)
    if b"\x00" in data:
        raise ValueError("File is too large or binary.")
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        raise ValueError("File is not UTF-8 text; Relay's file tools read text files.") from None
    return data


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
         "wait_seconds (default 0, at most 1800) waits for the job to finish first; it returns early when it does. "
         "from_line/to_line instead re-read those lines of its output, finished or not.",
         {"job_id": {"type": "string"}, "wait_seconds": {"type": "integer", "minimum": 0, "maximum": MAX_WAIT},
          "from_line": {"type": "integer", "minimum": 1}, "to_line": {"type": "integer", "minimum": 1}}, ["job_id"]),
    spec("stop_command", "Stop a run_command job and its child processes, and return its last output. "
         "Stop servers and watchers you started once you no longer need them.",
         {"job_id": {"type": "string"}}, ["job_id"]),
]
