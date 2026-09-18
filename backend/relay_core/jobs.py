# SPDX-License-Identifier: GPL-3.0-or-later
"""Commands the agent runs, as jobs that can outlive the tool call that started them.

run_command used to kill a command at its timeout. A build that needed three minutes then failed
outright, and the model was refused anything over 120 s (owner, 2026-09-18: "why have a maximum at
all"). Now every command is a job: run_command waits up to its timeout, and a command still running
then is handed back as a job id with its output so far. The model reads more with command_output
(optionally waiting) and ends it with stop_command. A server or watcher starts with background: true.

The limits that remain are about the machine, not the model's patience: at most MAX_RUNNING jobs at
once, and each keeps the last KEEP_BYTES of its output. Jobs end with their conversation: a new
conversation, the end of a subagent's run, or the worker exiting stops them (stop_all), because no
later turn could name them. Each job is its own process group, so stopping one takes its children.
"""
from __future__ import annotations

import atexit
import os
import selectors
import signal
import subprocess
import threading
import time
import weakref
from dataclasses import dataclass, field
from typing import Callable

MAX_RUNNING = 8
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
    read_pos: int = 0         # absolute offset the model has read up to
    exit_code: int | None = None
    finished: float | None = None
    stopped: bool = False
    done: threading.Event = field(default_factory=threading.Event)
    # Called with each chunk of text while a tool call is waiting on this job (the live stream
    # the pane shows under the call); None otherwise.
    live: Callable[[str], None] | None = None

    @property
    def total(self) -> int:
        return self.base + len(self.buffer)

    @property
    def running(self) -> bool:
        return not self.done.is_set()


def _kill_group(process: subprocess.Popen) -> None:
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


class JobTable:
    """The jobs of one agent (one conversation, or one subagent)."""

    def __init__(self):
        self._jobs: dict[str, Job] = {}
        self._lock = threading.Lock()
        self._next = 1
        _TABLES.add(self)

    def start(self, command: str, cwd, env: dict) -> Job:
        with self._lock:
            running = sum(1 for job in self._jobs.values() if job.running)
            if running >= MAX_RUNNING:
                raise ValueError(f"{running} commands are already running. Stop one with stop_command "
                                 "(or wait for one with command_output) before starting another.")
            job_id = f"job-{self._next}"
            self._next += 1
        process = subprocess.Popen(["/bin/bash", "--noprofile", "--norc", "-c", command],
                                   cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   start_new_session=True, bufsize=0)
        job = Job(job_id, command, process, time.monotonic())
        with self._lock:
            self._jobs[job_id] = job
        threading.Thread(target=self._pump, args=(job,), name=f"relay-{job_id}", daemon=True).start()
        return job

    def get(self, job_id) -> Job:
        with self._lock:
            job = self._jobs.get(job_id) if isinstance(job_id, str) else None
        if job is None:
            raise ValueError(f"No command {job_id!r}. Job ids come from a run_command result "
                             "(\"job_id\"); jobs end with the conversation.")
        return job

    # Reads the job's output until its shell exits. The shell's exit ends the job, as a
    # foreground command always did: whatever it left running in its process group is stopped
    # then, so a stray `server &` inside a command does not outlive it. (A server the model wants
    # kept is a job of its own, started with background: true.)
    def _pump(self, job: Job) -> None:
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

    def _append(self, job: Job, chunk: bytes) -> None:
        with self._lock:
            job.buffer.extend(chunk)
            if len(job.buffer) > KEEP_BYTES:
                drop = len(job.buffer) - KEEP_BYTES
                del job.buffer[:drop]
                job.base += drop
            live = job.live
        if live is not None:
            live(chunk.decode("utf-8", "replace"))

    def wait(self, job: Job, seconds: float, cancel: threading.Event,
             live: Callable[[str], None] | None = None) -> bool:
        """Wait until the job ends, `seconds` pass, or `cancel` is set. True when it ended."""
        with self._lock:
            job.live = live if job.running else None
        deadline = time.monotonic() + max(0.0, seconds)
        try:
            while not job.done.is_set() and not cancel.is_set():
                left = deadline - time.monotonic()
                if left <= 0:
                    break
                job.done.wait(min(0.1, left))
        finally:
            with self._lock:
                job.live = None
        return job.done.is_set()

    def take_output(self, job: Job, limit: int) -> dict:
        """The output the model has not read yet: at most `limit` bytes, the newest if more."""
        with self._lock:
            omitted = max(0, job.base - job.read_pos)
            start = max(job.read_pos, job.base)
            data = bytes(job.buffer[start - job.base:])
            job.read_pos = job.total
        if len(data) > limit:
            omitted += len(data) - limit
            data = data[-limit:]
        return {"output": data.decode("utf-8", "replace"), "truncated": omitted > 0, "omitted_bytes": omitted}

    def stop(self, job: Job) -> None:
        if job.running:
            job.stopped = True
            _kill_group(job.process)
            job.done.wait(3)

    def stop_all(self) -> None:
        with self._lock:
            jobs = [job for job in self._jobs.values() if job.running]
        for job in jobs:
            self.stop(job)

    def running(self) -> list[Job]:
        with self._lock:
            return [job for job in self._jobs.values() if job.running]


_TABLES: "weakref.WeakSet[JobTable]" = weakref.WeakSet()


@atexit.register
def _stop_everything() -> None:
    for table in list(_TABLES):
        table.stop_all()
