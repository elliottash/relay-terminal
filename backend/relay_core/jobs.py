# SPDX-License-Identifier: AGPL-3.0-or-later
"""Commands the agent runs, as jobs that can outlive the tool call that started them.

run_command used to kill a command at its timeout. A build that needed three minutes then failed
outright, and the model was refused anything over 120 s (owner, 2026-09-18: "why have a maximum at
all"). Now every command is a job: run_command waits up to its timeout, and a command still running
then is handed back as a job id with its output so far. The model reads more with command_output
(optionally waiting) and ends it with stop_command. A server or watcher starts with background: true.

The limits that remain are about the machine, not the model's patience: at most MAX_RUNNING jobs at
once, each keeps the last KEEP_BYTES of its output, and finished jobs are remembered, newest first,
until their kept output adds up to KEEP_FINISHED_BYTES (every command is a job, so a long conversation
would otherwise keep them all). Card #0C0V: the model sees at most 12,000 characters of a result and
reads the rest by line range (read_lines), so a handle from this conversation has to stay readable —
retention is by bytes, not by count, and a foreground command's job id is that handle too.

A job the model was handed back is also the user's business: it is listed under the pane's prompt
(src/JobsPanel.h), which the table feeds through on_change and snapshot(), and the user can read or
stop it there (protocol: jobs_list, job_output_get, job_stop). Jobs end with their conversation: a new
conversation, the end of a subagent's run, or the worker exiting stops them (stop_all), because no
later turn could name them. Each job is its own process group, so stopping one takes its children.
"""
from __future__ import annotations

import atexit
import base64
import json
import os
import re
import selectors
import shutil
import signal
import subprocess
import threading
import time
import weakref
from dataclasses import dataclass, field
from typing import Callable

MAX_RUNNING = 8
# Finished jobs are forgotten oldest first once their kept output passes this (card #0C0V: it was
# 16 jobs, which a debugging session outran in minutes and left the omitted-lines handles dead).
# A job with little output still counts JOB_OVERHEAD, so thousands of empty ones are bounded too.
KEEP_FINISHED_BYTES = 32 << 20
JOB_OVERHEAD = 4096
PEEK_BYTES = 256 * 1024       # what the user's "show output" gets: the newest part of the kept output
KEEP_BYTES = 1 << 20          # output kept per job; older bytes are dropped (and counted)
TERM_GRACE = 0.3              # seconds between SIGTERM and SIGKILL


@dataclass
class Job:
    id: str
    command: str
    process: subprocess.Popen
    started: float
    buffer: bytearray = field(default_factory=bytearray)
    base: int = 0             # absolute offset of buffer[0]: bytes dropped before it
    base_lines: int = 0       # newlines in those dropped bytes: buffer[0] is on line base_lines + 1
    read_pos: int = 0         # absolute offset the model has read up to
    exit_code: int | None = None
    finished: float | None = None
    stopped: bool = False
    handed_back: bool = False  # outlived its call: listed for the user until the conversation ends
    done: threading.Event = field(default_factory=threading.Event)
    # Called with each chunk of text while a tool call is waiting on this job (the live stream
    # the pane shows under the call); None otherwise.
    live: Callable[[str], None] | None = None
    # The ssh host it runs on over the user's connection (card #S5SH); None for a local command.
    host: str | None = None
    # The per-run memory bound this job asked for (card #WBDX), None for a plain run_command.
    memory_max: str | None = None
    # scope_oom_kills() when the job started: a SIGKILL exit plus a higher count now is the
    # kernel's memory kill of this job (#ZPWT), not an ordinary kill.
    oom_kills: int | None = None

    @property
    def total(self) -> int:
        return self.base + len(self.buffer)

    @property
    def running(self) -> bool:
        return not self.done.is_set()


def _kill_group(process: subprocess.Popen) -> None:
    if os.name == "nt":
        process._relay_tree.stop()
        return
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            return
        if sig == signal.SIGTERM:
            try:
                process.wait(timeout=TERM_GRACE)
            except subprocess.TimeoutExpired:
                pass


def raise_oom_score_adj(value: int) -> None:
    """Raise this process's oom_score_adj to `value` — raise-only, never lower, mirroring
    backend/worker.py's prefer_as_oom_victim rule (a child of a context already at 1000 keeps it).
    Card #ZPWT: run_command children run at 1000 so the memcg OOM killer picks the command, not
    the worker (500) that owns the conversation. Raw os.* calls only: this also runs in Popen's
    preexec_fn, after fork, where allocation is off-limits."""
    try:
        fd = os.open("/proc/self/oom_score_adj", os.O_RDONLY)
        try:
            current = int(os.read(fd, 16).strip() or b"0")
        finally:
            os.close(fd)
        if current >= value:
            return
        fd = os.open("/proc/self/oom_score_adj", os.O_WRONLY)
        try:
            os.write(fd, str(value).encode("ascii"))
        finally:
            os.close(fd)
    except (OSError, ValueError):
        pass


def _prefer_child_oom_victim() -> None:
    """preexec hook for run_command children (#ZPWT). Not Windows: no preexec_fn there."""
    raise_oom_score_adj(1000)


def scope_oom_kills() -> int | None:
    """The oom_kill count of the cgroup this process is in — an isolated pane's agent scope — or
    None when unreadable (cgroup v1, no systemd). Compared around a job (#ZPWT) to tell a memory
    kill from any other SIGKILL in its result."""
    try:
        with open("/proc/self/cgroup") as f:
            line = f.read().strip().splitlines()[-1]
        path = line.split("::", 1)[1] if "::" in line else ""
        if not path:
            return None
        with open(f"/sys/fs/cgroup{path}/memory.events") as f:
            for row in f:
                if row.startswith("oom_kill "):
                    return int(row.split()[1])
    except (OSError, ValueError, IndexError):
        pass
    return None


_MEMORY_SIZE = re.compile(r"^(?:\d+[KMGT]?|infinity)$")


def is_memory_size(size: str) -> bool:
    """True when `size` is a MemoryMax a caller may pass: 8G, 512M, 4T or infinity."""
    return bool(_MEMORY_SIZE.match(str(size).strip()))


def scoped_argv(argv: list[str], memory_max: str) -> list[str]:
    """`argv` run by systemd-run in a scope of its own under app-relay.slice with
    MemoryMax=`memory_max` (card #WBDX): a job that knows its size asks for that bound instead of
    dying at the pane cap, and stays inside the slice ceiling that bounds all panes together.
    `--scope` execs in place, so the job keeps its pipes, its pid and its process group, and
    OOMPolicy=continue keeps a breach of the bound to this one process. sd-bus reaches the user
    manager without DBUS_SESSION_BUS_ADDRESS through $XDG_RUNTIME_DIR/bus (#ZPWT step 0 proved
    it), so nothing the pane had removed is needed back."""
    size = str(memory_max).strip()
    if not _MEMORY_SIZE.match(size):
        raise ValueError(f"memory_max {memory_max!r} is not a size: use 8G, 512M, 4T or infinity.")
    if os.name == "nt":
        raise ValueError("memory_max is not supported on Windows.")
    exe = shutil.which("systemd-run")
    if not exe:
        raise ValueError("memory_max needs systemd-run, which this machine does not have.")
    # MemorySwapMax=0 because memory.max alone bounds only RAM: without it a job simply swaps
    # past the bound (a rehearsal for #ZPWT caught a 900M hog surviving a 600M max that way).
    # A bound the job can sidestep on disk is not a bound.
    return [exe, "--user", "--scope", "--quiet", "--collect", "--slice=app-relay.slice",
            "-p", f"MemoryMax={size}", "-p", "MemorySwapMax=0", "-p", "OOMPolicy=continue", "--"] + argv


def shell_argv(command: str, env: dict) -> list[str]:
    if os.name != "nt":
        return [env.get("RELAY_BASH") or "/bin/bash", "--noprofile", "--norc", "-c", command]
    # EncodedCommand avoids Windows argv quoting corrupting quotes/newlines. Preserve native
    # exit codes; a failed cmdlet must also produce a nonzero result. Explicit exit still wins.
    script = ("[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false); "
              "$global:LASTEXITCODE = 0;\n" + command +
              "\nif (!$?) { if ($LASTEXITCODE) { exit $LASTEXITCODE }; exit 1 }; exit 0")
    encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
    return [env.get("RELAY_POWERSHELL") or "pwsh.exe", "-NoLogo", "-NoProfile",
            "-NonInteractive", "-OutputFormat", "Text", "-EncodedCommand", encoded]


def split_lines(text: str) -> list[str]:
    """`text` as lines that keep their "\\n" — split on "\\n" only, so line numbers agree with a
    newline count (str.splitlines also splits on \\r, form feeds and more)."""
    parts = text.split("\n")
    return [part + "\n" for part in parts[:-1]] + ([parts[-1]] if parts[-1] else [])


def sent_length(text: str) -> int:
    """How many characters `text` takes in a tool message, which is JSON: a newline or a quote
    costs two. The bounds of card #0C0V are in these, so a log of short lines stays inside them."""
    return len(json.dumps(text, ensure_ascii=False)) - 2


def line_range(lines: list[str], first: int, last: int | None, limit: int, skipped: int = 0) -> dict:
    """Lines `first`..`last` (1-based, inclusive; None = to the end) of a text whose first
    `skipped` lines are gone and whose remaining lines are `lines`: as many whole lines as fit in
    `limit` characters as sent (sent_length), with `next_from_line` when the range goes on. Shared
    by command_output and read_file (card #0C0V)."""
    total = skipped + len(lines)
    start = max(first, skipped + 1)
    end = total if last is None else min(last, total)
    taken, used, line, result = [], 0, start, {"total_lines": total}
    while line <= end:
        text = lines[line - skipped - 1]
        size = sent_length(text)
        if used + size > limit:
            if not taken:
                taken.append(text[:limit // 2])
                result["cut"] = (f"Line {line} is {len(text):,} characters; only its first {limit // 2:,} "
                                 "are shown. Read the rest with a command (cut -c, head -c).")
                line += 1
            break
        taken.append(text)
        used += size
        line += 1
    result.update({"output": "".join(taken), "from_line": start, "to_line": line - 1})
    if line <= end:
        result["next_from_line"] = line
    return result


class JobTable:
    """The jobs of one agent (one conversation, or one subagent)."""

    def __init__(self, on_change: Callable[[], None] | None = None):
        # Called (from any thread) when the list the user sees changes: a job handed back, or a
        # handed-back job ending.
        self.on_change = on_change
        self._jobs: dict[str, Job] = {}
        self._lock = threading.Lock()
        self._next = 1
        _TABLES.add(self)

    def start(self, command: str, cwd, env: dict, *, argv: list[str] | None = None,
              host: str | None = None, memory_max: str | None = None) -> Job:
        """Run `command` with bash, or run `argv` (ssh to `host`, card #S5SH) and keep `command` as
        the job's name. Either way it is one process group, stopped and read the same.

        `memory_max` (card #WBDX, local commands only) runs this one command in a scope of its own
        under app-relay.slice with that MemoryMax: a job that knows its size asks for the bound it
        needs instead of dying at the pane's cap. Every local child also raises its own
        oom_score_adj to 1000 (#ZPWT), so when a memory limit is hit the kernel kills the command
        and not the worker that owns this conversation."""
        with self._lock:
            running = sum(1 for job in self._jobs.values() if job.running)
            if running >= MAX_RUNNING:
                raise ValueError(f"{running} commands are already running. Stop one with stop_command "
                                 "(or wait for one with command_output) before starting another.")
            job_id = f"job-{self._next}"
            self._next += 1
            kept = 0
            for old in reversed([job for job in self._jobs.values() if not job.running]):
                kept += max(len(old.buffer), JOB_OVERHEAD)
                if kept > KEEP_FINISHED_BYTES:
                    del self._jobs[old.id]
        if memory_max and host:
            raise ValueError("memory_max bounds local commands; an ssh job's memory is the remote "
                             "host's to manage.")
        argv = list(argv or shell_argv(command, env))
        if memory_max:
            argv = scoped_argv(argv, memory_max)
        process = subprocess.Popen(argv,
                                   cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   start_new_session=os.name != "nt", bufsize=0,
                                   preexec_fn=_prefer_child_oom_victim if os.name != "nt" else None,
                                   creationflags=0x08000004 if os.name == "nt" else 0)
        if os.name == "nt":
            from .windows_jobs import attach
            attach(process)
        job = Job(job_id, command, process, time.monotonic(), host=host, memory_max=memory_max)
        job.oom_kills = scope_oom_kills()
        with self._lock:
            self._jobs[job_id] = job
        threading.Thread(target=self._pump, args=(job,), name=f"relay-{job_id}", daemon=True).start()
        return job

    def get(self, job_id) -> Job:
        with self._lock:
            job = self._jobs.get(job_id) if isinstance(job_id, str) else None
        if job is None:
            raise ValueError(f"No command {job_id!r}. Job ids come from a run_command result "
                             "(\"job_id\"); jobs end with the conversation, and the oldest finished "
                             "ones are dropped once their output passes 32 MiB. Rerun the command.")
        return job

    # Reads the job's output until its shell exits. The shell's exit ends the job, as a
    # foreground command always did: whatever it left running in its process group is stopped
    # then, so a stray `server &` inside a command does not outlive it. (A server the model wants
    # kept is a job of its own, started with background: true.)
    def _pump(self, job: Job) -> None:
        if os.name == "nt":
            self._pump_windows(job)
            return
        stream = job.process.stdout
        selector = selectors.DefaultSelector()
        selector.register(stream, selectors.EVENT_READ)
        exited_at = None
        try:
            while True:
                events = selector.select(0.1)
                eof = False
                for key, _ in events:
                    chunk = os.read(key.fileobj.fileno(), 65536)
                    if not chunk:
                        eof = True
                        break
                    self._append(job, chunk)
                if eof:
                    break
                if exited_at is None and job.process.poll() is not None:
                    exited_at = time.monotonic()
                    _kill_group(job.process)      # the shell is gone: end what it left behind
                # A child that ignored SIGTERM and keeps the pipe open is SIGKILLed above; give the
                # pipe a moment to report EOF, then stop reading regardless.
                if exited_at is not None and time.monotonic() - exited_at > 1.0:
                    break
            job.process.wait()
        finally:
            selector.close()
            stream.close()
            with self._lock:
                job.exit_code = job.process.returncode
                job.finished = time.monotonic()
                job.live = None
            job.done.set()
            if job.handed_back:
                self._changed()

    def _pump_windows(self, job: Job) -> None:
        # Windows selectors cannot read anonymous pipes. A reader drains concurrently with
        # the waiter, which closes the job tree when its root exits, also releasing pipe EOF.
        def read():
            try:
                while chunk := job.process.stdout.read(65536):
                    self._append(job, chunk)
            finally:
                job.process.stdout.close()
        reader = threading.Thread(target=read, name=f"relay-read-{job.id}", daemon=True)
        reader.start()
        try:
            job.process.wait()
        finally:
            _kill_group(job.process)
            reader.join()
            with self._lock:
                job.exit_code = job.process.returncode
                job.finished = time.monotonic()
                job.live = None
            job.done.set()
            if job.handed_back:
                self._changed()

    def _append(self, job: Job, chunk: bytes) -> None:
        with self._lock:
            job.buffer.extend(chunk)
            if len(job.buffer) > KEEP_BYTES:
                drop = len(job.buffer) - KEEP_BYTES
                job.base_lines += job.buffer.count(b"\n", 0, drop)
                del job.buffer[:drop]
                job.base += drop
            live = job.live
        if live is not None:
            live(chunk.decode("utf-8", "replace"))

    def wait(self, job: Job, seconds: float, cancel: threading.Event,
             live: Callable[[str], None] | None = None, wake: Callable[[], bool] | None = None) -> bool:
        """Wait until the job ends, `seconds` pass, or `cancel` is set. True when it ended.

        `wake` (command_output's steer check, Queue.peek_steer) ends the wait early too, leaving
        the job running: a user message is waiting to join the turn, and the step boundary that
        follows the early tool result delivers it."""
        with self._lock:
            job.live = live if job.running else None
        deadline = time.monotonic() + max(0.0, seconds)
        try:
            while not job.done.is_set() and not cancel.is_set():
                if wake is not None and wake():
                    break
                left = deadline - time.monotonic()
                if left <= 0:
                    break
                job.done.wait(min(0.1, left))
        finally:
            with self._lock:
                job.live = None
        return job.done.is_set()

    def take(self, job: Job) -> tuple[bytes, int, int]:
        """The kept output the model has not read yet, and marks it read: (data, the line number
        data starts on, bytes dropped before it that were never read)."""
        with self._lock:
            omitted = max(0, job.base - job.read_pos)
            start = max(job.read_pos, job.base)
            data = bytes(job.buffer[start - job.base:])
            line = job.base_lines + job.buffer.count(b"\n", 0, start - job.base) + 1
            job.read_pos = job.total
        return data, line, omitted

    def counts(self, job: Job) -> tuple[int, int]:
        """(lines, bytes) of everything the job has printed, dropped bytes included."""
        with self._lock:
            lines = job.base_lines + job.buffer.count(b"\n")
            if job.buffer and not job.buffer.endswith(b"\n"):
                lines += 1
            return lines, job.total

    def read_lines(self, job: Job, first: int, last: int | None, limit: int) -> dict:
        """Lines `first`..`last` (1-based, inclusive; `last` None = to the end) of the kept output,
        at most `limit` characters of whole lines. The model's read position stays where it is:
        this is the range re-read an omitted-lines marker names (card #0C0V)."""
        with self._lock:
            data = bytes(job.buffer)
            base_lines = job.base_lines
        result = {"job_id": job.id, **line_range(split_lines(data.decode("utf-8", "replace")),
                                                 first, last, limit, base_lines)}
        if base_lines and first <= base_lines:
            result["note"] = (f"Lines before {base_lines + 1} were dropped: a job keeps its last "
                              f"{KEEP_BYTES >> 20} MiB of output.")
        return result

    def hand_back(self, job: Job) -> None:
        """The call returned while the job runs on: from now on the user sees it too."""
        if not job.handed_back:
            job.handed_back = True
            self._changed()

    def _changed(self) -> None:
        if self.on_change is not None:
            self.on_change()

    def snapshot(self) -> list[dict]:
        """The handed-back jobs, oldest first, as the protocol's `jobs` event lists them."""
        now = time.monotonic()
        with self._lock:
            jobs = [job for job in self._jobs.values() if job.handed_back]
        return [{"job_id": job.id, "command": job.command[:300], "running": job.running,
                 "exit_code": job.exit_code, "stopped": job.stopped,
                 "elapsed_ms": int(((job.finished or now) - job.started) * 1000),
                 **({"host": job.host} if job.host else {})} for job in jobs]

    def peek(self, job: Job, limit: int = PEEK_BYTES) -> dict:
        """The newest `limit` bytes of kept output, for the user. The model's read position stays."""
        with self._lock:
            data = bytes(job.buffer[-limit:])
            omitted = job.total - len(data)
        return {"output": data.decode("utf-8", "replace"), "truncated": omitted > 0, "omitted_bytes": omitted}

    def stop(self, job: Job) -> None:
        if job.running:
            job.stopped = True
            _kill_group(job.process)
            job.done.wait(3)

    def stop_all(self, forget: bool = False) -> None:
        """Stop every running job; `forget` also drops them all (a new conversation starts empty)."""
        with self._lock:
            jobs = [job for job in self._jobs.values() if job.running]
            listed = any(job.handed_back for job in self._jobs.values())
        for job in jobs:
            self.stop(job)
        if forget:
            with self._lock:
                self._jobs.clear()
            if listed:
                self._changed()

    def running(self) -> list[Job]:
        with self._lock:
            return [job for job in self._jobs.values() if job.running]


_TABLES: "weakref.WeakSet[JobTable]" = weakref.WeakSet()


@atexit.register
def _stop_everything() -> None:
    for table in list(_TABLES):
        table.stop_all()
