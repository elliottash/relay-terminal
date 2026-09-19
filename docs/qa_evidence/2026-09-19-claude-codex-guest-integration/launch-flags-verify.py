#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Drive the *real* Claude Code and Codex CLIs and check every flag `guest_launch` relies on (GT7X).

`backend/relay_core/guest_launch.py` builds two command lines out of flags whose behaviour is not
written down anywhere we control:

    claude --settings <file> --dangerously-skip-permissions
    codex  -c notify=[…] -c tui.notification_condition="always" \
           --dangerously-bypass-approvals-and-sandbox

This script is the evidence that those command lines do what 26.9 says they do, against the CLIs
installed on this machine. It runs the real binaries on the owner's own subscriptions, so it is
deliberately frugal: five model calls for a whole run — four of them the single line
"Reply with the single word ok.", and one that Claude's own shell mode provokes in step B.

    python3 docs/qa_evidence/2026-09-19-claude-codex-guest-integration/launch-flags-verify.py
    python3 …/launch-flags-verify.py --only A,C          # a subset; F is free, B and D are free

Steps
-----

A  `claude -p --settings <file>` runs, answers, and its **hooks fire**: the settings file written
   by `guest_launch.write_claude_settings()` puts `UserPromptSubmit` and `Stop` envelopes on the
   pane's guest spool (26.3).                                                  [1 model call]
B  The Claude TUI started the same way takes `!` as shell mode and `/exit` quits. Also watches
   for a startup dialog. Claude itself answers a `!` command with a model turn, so the step asks
   for no model call and provokes exactly one.                                 [1 model call]
C  `codex exec` with `guest_launch.codex_overrides()`'s two `-c` values runs and the `notify`
   entry fires, putting a `hook`/`notify` envelope on the same spool.           [1 model call]
D  The Codex TUI takes `!` as shell mode and `/quit` quits.                     [0 model calls]
E  Codex 0.155.1's `hooks` feature injected with `-c hooks.<Event>=[…]`: once without and once
   with `--dangerously-bypass-hook-trust`, because that is the only difference that matters.
                                                                               [2 model calls]
F  The `--help` lines every one of the above rests on, quoted verbatim.         [0 model calls]

Nothing here writes into `~/.claude` or `~/.codex` (the CLIs write their own state, which is
theirs). `HOME` stays the real home because both CLIs need their real login. Everything this
script creates lives under `--work-dir`, `/tmp/claude-1000/launchv` by default — a short path,
because a pty and a spool under a long one run into the 108-byte socket limit.

Findings are in `launch-flags-README.md` beside this file.
"""
from __future__ import annotations

import argparse
import fcntl
import json
import os
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BACKEND = REPO / "backend"
PROMPT = "Reply with the single word ok."      # the one line every model call is allowed to be
BANG = "relay-bang-ok"                          # what `!echo` must print in both TUIs
STEPS = ("A", "B", "C", "D", "E", "F")
PROJECT = "proj"       # the temp project under --work-dir; --fresh-project makes it a new one

# The pty steps. Claude Code and Codex both paint a full-screen TUI; a slow first frame is
# normal, so every wait here is generous and the step as a whole is capped.
TUI_START_WAIT = 12.0       # seconds before the first key is sent
TUI_SETTLE = 8.0            # seconds waited after a command is typed
TUI_TOTAL = 90.0            # seconds before the child is killed and the step reported as stuck
MODEL_TIMEOUT = 180.0       # seconds for one non-interactive model call

CSI = re.compile(r"\x1b\[([0-?]*)([ -/]*)([@-~])")
OSC = re.compile(r"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)")


class Screen:
    """Just enough of a terminal to read a TUI back.

    Stripping escapes is not enough for either of these programs. Both lay a line out by jumping
    the cursor to a column (`ESC [ n G`) instead of emitting runs of spaces — strip that and the
    words glue together ("Yes,Itrustthisfolder") — and both redraw a line in place many times a
    second, so an append-only log turns one composer into "echorrelay-bang-okg". What is needed is
    a grid with a cursor: a redraw then overwrites what it redrew, and what comes out is what a
    person would have seen. Lines that scroll off the top are kept, so the transcript holds the
    whole session and not just its last frame.

    Colour and the other SGR attributes are dropped on purpose; so are the alternate-screen and
    bracketed-paste toggles, because nothing here needs to tell one screen from the other.
    """

    def __init__(self, rows=40, columns=120):
        self.rows, self.columns = rows, columns
        self.grid = [[" "] * columns for _ in range(rows)]
        self.scrollback: list[str] = []
        self.row = self.column = 0

    # -- the grid

    def _scroll(self) -> None:
        self.scrollback.append("".join(self.grid[0]).rstrip())
        self.grid.pop(0)
        self.grid.append([" "] * self.columns)

    def _put(self, char: str) -> None:
        if self.column >= self.columns:
            self.column = 0
            self.row += 1
        while self.row >= self.rows:
            self._scroll()
            self.row -= 1
        self.grid[self.row][self.column] = char
        self.column += 1

    def _blank(self, row, start, stop) -> None:
        for column in range(max(0, start), min(self.columns, stop)):
            self.grid[row][column] = " "

    # -- the stream

    def feed(self, data: str) -> None:
        index = 0
        while index < len(data):
            char = data[index]
            if char == "\x1b":
                match = CSI.match(data, index)
                if match:
                    self._csi(match.group(1), match.group(3))
                    index = match.end()
                    continue
                match = OSC.match(data, index)
                index = match.end() if match else index + 2
                continue
            if char == "\r":
                self.column = 0
            elif char == "\n":
                self.row += 1
                while self.row >= self.rows:
                    self._scroll()
                    self.row -= 1
            elif char == "\x08":
                self.column = max(0, self.column - 1)
            elif char == "\t":
                self.column = min(self.columns - 1, (self.column // 8 + 1) * 8)
            elif char >= " ":
                self._put(char)
            index += 1

    def _csi(self, parameters: str, final: str) -> None:
        if parameters.startswith("?"):          # private modes: alt screen, paste, cursor shape
            return
        numbers = [int(part) if part.isdigit() else 0 for part in parameters.split(";")]
        first = numbers[0] if numbers else 0
        if final == "A":
            self.row = max(0, self.row - max(1, first))
        elif final in "Be":
            self.row = min(self.rows - 1, self.row + max(1, first))
        elif final in "Ca":
            self.column = min(self.columns - 1, self.column + max(1, first))
        elif final == "D":
            self.column = max(0, self.column - max(1, first))
        elif final in "Ed":
            self.row = min(self.rows - 1, self.row + max(1, first) if final == "E" else first - 1)
            if final == "E":
                self.column = 0
        elif final == "F":
            self.row = max(0, self.row - max(1, first))
            self.column = 0
        elif final in "G`":
            self.column = max(0, min(self.columns - 1, max(1, first) - 1))
        elif final in "Hf":
            self.row = max(0, min(self.rows - 1, max(1, first) - 1))
            second = numbers[1] if len(numbers) > 1 else 1
            self.column = max(0, min(self.columns - 1, max(1, second) - 1))
        elif final == "J":
            if first in (0, 2, 3):
                if first == 0:
                    self._blank(self.row, self.column, self.columns)
                    rows = range(self.row + 1, self.rows)
                else:
                    rows = range(self.rows)
                for row in rows:
                    self._blank(row, 0, self.columns)
            elif first == 1:
                for row in range(self.row):
                    self._blank(row, 0, self.columns)
                self._blank(self.row, 0, self.column + 1)
        elif final == "K":
            if first == 0:
                self._blank(self.row, self.column, self.columns)
            elif first == 1:
                self._blank(self.row, 0, self.column + 1)
            else:
                self._blank(self.row, 0, self.columns)
        elif final == "L":
            for _ in range(max(1, first)):
                self.grid.insert(self.row, [" "] * self.columns)
                self.grid.pop()
        elif final == "M":
            for _ in range(max(1, first)):
                self.grid.pop(self.row)
                self.grid.append([" "] * self.columns)
        elif final == "P":
            row = self.grid[self.row]
            del row[self.column:self.column + max(1, first)]
            row.extend([" "] * (self.columns - len(row)))
        elif final == "S":
            for _ in range(max(1, first)):
                self._scroll()
        elif final == "X":
            self._blank(self.row, self.column, self.column + max(1, first))

    # -- reading it back

    def text(self) -> str:
        """Everything that scrolled past, then the screen as it stands."""
        rows = self.scrollback + ["".join(row).rstrip() for row in self.grid]
        while rows and not rows[-1]:
            rows.pop()
        return "\n".join(rows)

    def frame(self) -> str:
        """Only what is on the screen right now."""
        return "\n".join("".join(row).rstrip() for row in self.grid).rstrip()


def render(data: str, rows=40, columns=120) -> str:
    screen = Screen(rows, columns)
    screen.feed(data)
    return screen.text()


# ----- the pane's side of a launch ------------------------------------------------------------


def relay_env(runtime_dir: Path) -> dict:
    """The environment a Relay pane exports for a guest (26.2/26.3), on top of the real one.

    `RELAY_BACKEND_DIR` is absolute: the hook command in the settings file is run by the guest in
    *its* cwd, which is the temp project, not the checkout.

    The `CLAUDE_*` variables are dropped first. This script is likely to be run *from* a Claude
    Code session, and a claude that inherits `CLAUDE_CODE_CHILD_SESSION` behaves differently from
    the one a Relay pane starts: it says "Transcript saving is off", and it picks up the parent's
    `CLAUDE_EFFORT`. A pane's shell has none of them, so neither does the guest here.
    """
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("CLAUDE") and key != "CODEX_SANDBOX"}
    environment.update({
        "RELAY_RUNTIME_DIR": str(runtime_dir),
        "RELAY_SESSION_TOKEN": "test",
        "RELAY_GUEST_EVENT": str(runtime_dir / "guest-events"),
        "RELAY_BACKEND_DIR": str(BACKEND),
        "RELAY_PYTHON": sys.executable or shutil.which("python3") or "/usr/bin/python3",
    })
    return environment


def make_runtime(work: Path, step: str) -> Path:
    """A fresh runtime dir with an empty 0700 spool, so each step's envelopes are its own."""
    runtime_dir = work / f"rt{step}"
    if runtime_dir.exists():
        shutil.rmtree(runtime_dir)
    spool = runtime_dir / "guest-events"
    spool.mkdir(parents=True)
    os.chmod(runtime_dir, 0o700)
    os.chmod(spool, 0o700)
    return runtime_dir


def spool_envelopes(runtime_dir: Path) -> list[tuple[str, dict]]:
    """Every event file on the spool, oldest first (the name sorts by time, 26.3)."""
    spool = runtime_dir / "guest-events"
    out = []
    for path in sorted(spool.glob("*.json")):
        try:
            out.append((path.name, json.loads(path.read_text())))
        except (OSError, ValueError) as error:
            out.append((path.name, {"unreadable": str(error)}))
    return out


def describe(envelopes) -> list[str]:
    lines = []
    for name, body in envelopes:
        data = body.get("data") or {}
        payload = json.dumps(data.get("payload"))
        if len(payload) > 240:
            payload = payload[:240] + "… [truncated]"
        lines.append(f"{name}  event={body.get('event')!r} guest={body.get('guest')!r} "
                     f"data.name={data.get('name')!r}\n    payload={payload}")
    return lines


def write_settings(runtime_dir: Path, project: Path) -> tuple[Path, dict]:
    """Call `guest_launch.write_claude_settings()` itself — the point is to test what Relay writes,
    not a copy of it. Imported here rather than at module scope so `--only F` needs no backend."""
    sys.path.insert(0, str(BACKEND))
    from relay_core import guest_launch                       # noqa: E402  (deliberate)
    path = Path(guest_launch.write_claude_settings(str(runtime_dir), cwd=str(project),
                                                   home=os.path.expanduser("~")))
    return path, json.loads(path.read_text())


def codex_overrides() -> list[str]:
    sys.path.insert(0, str(BACKEND))
    from relay_core import guest_launch                       # noqa: E402
    return guest_launch.codex_overrides(sys.executable or "/usr/bin/python3")


# ----- a terminal to drive a TUI in -----------------------------------------------------------


class Terminal:
    """A child on a pty, read into one buffer and replayed through `Screen`. No pexpect: the
    driver has to be re-runnable from a bare checkout, and all this needs is the standard
    library."""

    def __init__(self, argv, cwd, env, cols=120, rows=40):
        self.argv = list(argv)
        self.rows, self.columns = rows, cols
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.master = master
        self.proc = subprocess.Popen(self.argv, cwd=str(cwd), env=env, stdin=slave, stdout=slave,
                                     stderr=slave, start_new_session=True, close_fds=True)
        os.close(slave)
        self.buffer = bytearray()
        self.started = time.monotonic()

    def pump(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return
            ready, _, _ = select.select([self.master], [], [], min(0.25, left))
            if not ready:
                continue
            try:
                chunk = os.read(self.master, 65536)
            except OSError:
                return
            if not chunk:
                return
            self.buffer += chunk

    def send(self, text: str, settle: float = 0.6) -> None:
        os.write(self.master, text.encode())
        self.pump(settle)

    def text(self) -> str:
        """The whole session: what scrolled past, then the screen as it stands."""
        return render(self.buffer.decode("utf-8", "replace"), self.rows, self.columns)

    def frame(self) -> str:
        """Only the screen as it stands — what a dialog is or is not still on."""
        screen = Screen(self.rows, self.columns)
        screen.feed(self.buffer.decode("utf-8", "replace"))
        return screen.frame()

    def wait_exit(self, timeout: float) -> int | None:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if self.proc.poll() is not None:
                self.pump(0.5)
                return self.proc.returncode
            self.pump(0.5)
        return None

    def kill(self) -> None:
        if self.proc.poll() is None:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except OSError:
                self.proc.kill()
            self.proc.wait(timeout=10)
        try:
            os.close(self.master)
        except OSError:
            pass


def run(argv, cwd, env, timeout) -> tuple[int, str]:
    """A non-interactive run with stdin closed — both CLIs otherwise wait on it."""
    try:
        done = subprocess.run(argv, cwd=str(cwd), env=env, stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired as expired:
        return 124, (expired.output or b"").decode("utf-8", "replace")
    return done.returncode, done.stdout.decode("utf-8", "replace")


# ----- the steps ------------------------------------------------------------------------------


class Report:
    def __init__(self):
        self.steps: dict[str, dict] = {}
        self.calls = 0
        self.started = time.monotonic()

    def add(self, step, verdict, lines, calls=0):
        self.calls += calls
        self.steps[step] = {"verdict": verdict, "lines": lines, "calls": calls}
        print(f"\n===== {step}: {verdict} =====")
        for line in lines:
            print(line)


def step_a(work: Path, report: Report) -> None:
    runtime_dir = make_runtime(work, "A")
    project = work / PROJECT
    project.mkdir(exist_ok=True)
    settings, body = write_settings(runtime_dir, project)
    environment = relay_env(runtime_dir)
    argv = ["claude", "-p", PROMPT, "--settings", str(settings),
            "--dangerously-skip-permissions"]
    code, output = run(argv, project, environment, MODEL_TIMEOUT)
    envelopes = spool_envelopes(runtime_dir)
    names = [(e.get("data") or {}).get("name") for _, e in envelopes]
    lines = [f"$ {' '.join(argv)}",
             f"settings file: {settings}",
             f"settings keys: {sorted(body)}  hooks: {sorted(body.get('hooks', {}))}",
             f"statusLine present: {'statusLine' in body}",
             f"exit={code}  stdout={output.strip()!r}",
             f"spool: {len(envelopes)} file(s)"] + describe(envelopes)
    trust = [l for l in output.splitlines() if "trust" in l.lower() or "accept" in l.lower()]
    lines.append(f"lines mentioning trust/accept in the output: {trust or 'none'}")
    good = (code == 0 and "ok" in output.lower()
            and "UserPromptSubmit" in names and "Stop" in names and not trust)
    report.add("A", "VERIFIED" if good else "NOT VERIFIED", lines, calls=1)


def step_b(work: Path, report: Report) -> None:
    """The Claude TUI: `!` shell mode and `/exit`, started exactly as `guest_launch` says."""
    runtime_dir = make_runtime(work, "B")
    project = work / PROJECT
    project.mkdir(exist_ok=True)
    settings, _ = write_settings(runtime_dir, project)
    argv = ["claude", "--settings", str(settings), "--dangerously-skip-permissions"]
    terminal = Terminal(argv, project, relay_env(runtime_dir))
    lines = [f"$ {' '.join(argv)}   (in a 120x40 pty)"]
    good = False
    try:
        terminal.pump(TUI_START_WAIT)
        opening = terminal.frame()
        gate = [l.strip() for l in opening.splitlines() if l.strip()]
        if re.search(r"trust this folder|safety check", opening, re.I):
            # The *workspace* trust dialog, once per directory claude has not seen. It is not
            # `--dangerously-skip-permissions` asking to be accepted — that flag never prompts —
            # but a pane starting a guest in a fresh project meets it, so it is recorded and
            # answered: Down to "Yes, I trust this folder", Enter.
            lines += ["opening dialog (verbatim):"] + [f"    {l}" for l in gate]
            lines.append("answered with: Down, Enter  ('Yes, I trust this folder')")
            terminal.send("\x1b[B", settle=1.5)
            terminal.send("\r", settle=6.0)
            lines.append("dialog still on screen afterwards: "
                         f"{bool(re.search(r'trust this folder', terminal.frame(), re.I))}")
        else:
            lines.append("no acceptance or trust dialog on startup: "
                         f"{[l for l in gate if l][:3]}")
        terminal.pump(3.0)

        terminal.send("!", settle=3.0)                    # on an empty composer
        after_bang = terminal.frame()
        shell_mode = bool(re.search(r"^!\s*$", after_bang, re.M))
        lines.append(f"after '!' on an empty composer, the prompt becomes '!': {shell_mode}")
        terminal.send(f"echo {BANG}", settle=2.0)
        terminal.send("\r", settle=TUI_SETTLE)
        terminal.pump(6.0)
        screen = terminal.text()
        ran = [l.rstrip() for l in screen.splitlines() if BANG in l]
        # Claude prints a bash-mode command as "! echo …" and its output under a "⎿" gutter.
        output_shown = any(l.strip().startswith("⎿") and BANG in l for l in screen.splitlines())
        lines.append(f"lines carrying {BANG!r}: {ran[-4:]}")
        lines.append(f"the output is shown on its own '⎿' line: {output_shown}")
        envelopes = spool_envelopes(runtime_dir)
        names = [(e.get("data") or {}).get("name") for _, e in envelopes]
        lines.append(f"spool after the bang command: {names or 'empty'}")
        lines.append("    UserPromptSubmit would mean the *line* was sent as a prompt; "
                     "Stop means a model turn ran and ended.")
        assistant = [l.rstrip() for l in screen.splitlines() if l.lstrip().startswith("●")]
        lines.append(f"assistant turn after the bang command: {assistant[-1:] or 'none'}")

        terminal.send("/exit", settle=2.0)
        terminal.send("\r", settle=1.5)
        code = terminal.wait_exit(min(30.0, TUI_TOTAL - (time.monotonic() - terminal.started)))
        lines.append(f"/exit -> exit code {code!r}")
        good = shell_mode and output_shown and code == 0
    finally:
        stuck = terminal.proc.poll() is None
        transcript = work / "B-screen.txt"
        transcript.write_text(terminal.text())
        lines.append(f"whole screen transcript: {transcript}")
        terminal.kill()
        if stuck:
            lines.append("the child had to be killed: it did not exit on /exit")
    # No prompt is typed in this step; the turn is Claude's own answer to the bang command, and
    # it is counted because it is a real call on the owner's subscription.
    report.add("B", "VERIFIED" if good else "NOT VERIFIED", lines, calls=1)


def step_c(work: Path, report: Report) -> None:
    runtime_dir = make_runtime(work, "C")
    project = work / PROJECT
    project.mkdir(exist_ok=True)
    environment = relay_env(runtime_dir)
    overrides = codex_overrides()
    argv = ["codex", "exec", *overrides, "--dangerously-bypass-approvals-and-sandbox",
            "--skip-git-repo-check", PROMPT]
    lines = [f"guest_launch.codex_overrides(): {overrides}", f"$ {' '.join(argv)}"]
    code, output = run(argv, project, environment, MODEL_TIMEOUT)
    envelopes = spool_envelopes(runtime_dir)
    names = [(e.get("data") or {}).get("name") for _, e in envelopes]
    tail = [l for l in output.strip().splitlines()[-8:]]
    lines += [f"exit={code}", "output tail:"] + [f"    {l}" for l in tail]
    lines += [f"spool: {len(envelopes)} file(s)"] + describe(envelopes)
    report.add("C", "VERIFIED" if code == 0 and "notify" in names else "NOT VERIFIED",
               lines, calls=1)


def step_d(work: Path, report: Report) -> None:
    """The Codex TUI: its startup hook-trust gate, `!` shell mode, and `/quit`."""
    runtime_dir = make_runtime(work, "D")
    project = work / PROJECT
    project.mkdir(exist_ok=True)
    argv = ["codex", *codex_overrides(), "--dangerously-bypass-approvals-and-sandbox"]
    terminal = Terminal(argv, project, relay_env(runtime_dir))
    lines = [f"$ {' '.join(argv)}   (in a 120x40 pty)"]
    good = False
    try:
        terminal.pump(TUI_START_WAIT)
        opening = terminal.frame()
        gate = [l.strip() for l in opening.splitlines() if l.strip()]
        if re.search(r"hooks need review", opening, re.I):
            # Codex gates *every* enabled hook it has not seen before, whatever wrote it, behind
            # this full-screen question. Option 3 is taken deliberately: trusting here would
            # write a `hooks.state."…".trusted_hash` into the user's own config, and this script
            # is not allowed to persist anything.
            lines += ["startup gate (verbatim):"] + [f"    {l}" for l in gate]
            lines.append("answered with: '3', Enter  ('Continue without trusting "
                         "(hooks won't run)') - trusting would persist into ~/.codex")
            terminal.send("3", settle=1.5)
            terminal.send("\r", settle=6.0)
        elif re.search(r"do you trust", opening, re.I):
            lines += ["startup gate (verbatim):"] + [f"    {l}" for l in gate]
            lines.append("a directory-trust question, not answered: it would persist")
        else:
            lines.append(f"no startup gate: {[l for l in gate if l][:3]}")
        terminal.pump(2.0)

        terminal.send("!", settle=3.0)                    # on an empty composer
        after_bang = terminal.frame()
        shell_mode = "Shell mode" in after_bang and bool(re.search(r"^!\s*$", after_bang, re.M))
        lines.append("after '!' on an empty composer: the status bar says 'Shell mode' "
                     f"and the prompt becomes '!': {shell_mode}")
        terminal.send(f"echo {BANG}", settle=2.0)
        terminal.send("\r", settle=TUI_SETTLE)
        terminal.pump(4.0)
        screen = terminal.text()
        ran = [l.rstrip() for l in screen.splitlines() if BANG in l]
        lines.append(f"lines carrying {BANG!r}: {ran[-4:]}")
        local = any(re.search(rf"You ran echo {re.escape(BANG)}", l) for l in screen.splitlines())
        output_shown = any(l.strip().startswith("└") and BANG in l for l in screen.splitlines())
        lines.append(f"shown as a local run ('You ran …'): {local};  "
                     f"its output under the '└' gutter: {output_shown}")
        envelopes = spool_envelopes(runtime_dir)
        names = [(e.get("data") or {}).get("name") for _, e in envelopes]
        lines.append(f"spool after the bang command: {names or 'empty'}  "
                     "(a notify here would mean a model turn ran)")

        terminal.send("/quit", settle=2.0)
        terminal.send("\r", settle=1.5)
        code = terminal.wait_exit(min(30.0, TUI_TOTAL - (time.monotonic() - terminal.started)))
        lines.append(f"/quit -> exit code {code!r}")
        good = shell_mode and local and output_shown and not names and code == 0
    finally:
        stuck = terminal.proc.poll() is None
        transcript = work / "D-screen.txt"
        transcript.write_text(terminal.text())
        lines.append(f"whole screen transcript: {transcript}")
        terminal.kill()
        if stuck:
            lines.append("the child had to be killed: it did not exit on /quit")
    report.add("D", "VERIFIED" if good else "NOT VERIFIED", lines)


def step_e(work: Path, report: Report) -> None:
    project = work / PROJECT
    project.mkdir(exist_ok=True)
    results = []
    lines = []
    for label, extra in (("without --dangerously-bypass-hook-trust", []),
                         ("with --dangerously-bypass-hook-trust",
                          ["--dangerously-bypass-hook-trust"])):
        marker = work / f"codex-stop-hook-{'bypass' if extra else 'plain'}"
        if marker.exists():
            marker.unlink()
        override = ('hooks.Stop=[{hooks=[{type="command",'
                    f'command="touch {marker}"}}]}}]')
        argv = ["codex", "exec", "-c", override,
                "--dangerously-bypass-approvals-and-sandbox", *extra,
                "--skip-git-repo-check", PROMPT]
        code, output = run(argv, project, relay_env(make_runtime(work, "E")), MODEL_TIMEOUT)
        said = [l.rstrip() for l in output.splitlines()
                if re.search(r"hook|trust", l, re.I)]
        lines += [f"--- {label}", f"$ {' '.join(argv)}", f"exit={code}",
                  f"hook/trust lines in the output: {said or 'none'}",
                  f"marker {marker.name} created: {marker.exists()}"]
        results.append(marker.exists())
        report.calls += 1
    lines.append("verdict rests on the pair: the hook must NOT run without the flag and MUST "
                 "run with it.")
    good = results == [False, True]
    report.add("E", "VERIFIED" if good else "NOT VERIFIED", lines)


HELP_WANTED = {
    ("claude", "--help"): ["--settings", "--ide", "--dangerously-skip-permissions",
                           "-r, --resume", "--fork-session", "--session-id"],
    ("codex", "--help"): ["-c, --config", "--dangerously-bypass-approvals-and-sandbox",
                          "--dangerously-bypass-hook-trust", "-C, --cd"],
    ("codex", "resume", "--help"): ["-c, --config",
                                    "--dangerously-bypass-approvals-and-sandbox"],
    ("codex", "fork", "--help"): ["-c, --config",
                                  "--dangerously-bypass-approvals-and-sandbox"],
}


def help_block(text: str, option: str) -> list[str]:
    """The lines of one option's entry: its own line plus the indented continuation under it.

    An option's own line starts near the left margin (2 columns in `claude --help`, 2 or 6 in
    `codex --help`); a continuation is indented far further. Without that bound, `--settings`
    matches the middle of `--bare`'s prose rather than its own entry.
    """
    rows = text.splitlines()
    for index, row in enumerate(rows):
        indent = len(row) - len(row.lstrip())
        if indent <= 6 and row.strip().startswith(option):
            block = [row.rstrip()]
            for follower in rows[index + 1:]:
                if not follower.strip():
                    break
                if len(follower) - len(follower.lstrip()) <= indent:
                    break
                block.append(follower.rstrip())
            return block
    return [f"!! {option} not found"]


def step_f(work: Path, report: Report) -> None:
    lines = []
    good = True
    for argv, options in HELP_WANTED.items():
        code, text = run(list(argv), work, os.environ.copy(), 60)
        lines.append(f"--- $ {' '.join(argv)}   (exit {code})")
        for option in options:
            block = help_block(text, option)
            good = good and not block[0].startswith("!!")
            lines += block
    report.add("F", "VERIFIED" if good else "NOT VERIFIED", lines)


RUNNERS = {"A": step_a, "B": step_b, "C": step_c, "D": step_d, "E": step_e, "F": step_f}


def versions() -> list[str]:
    out = []
    for argv in (["claude", "--version"], ["codex", "--version"]):
        code, text = run(argv, Path.cwd(), os.environ.copy(), 60)
        out.append(f"$ {' '.join(argv)} -> {text.strip()!r} (exit {code})")
    return out


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", default=",".join(STEPS),
                        help="comma-separated step letters, e.g. A,C,F (default: all)")
    parser.add_argument("--work-dir", default="/tmp/claude-1000/launchv",
                        help="scratch root; keep it short, a pty and a unix socket live under it")
    parser.add_argument("--fresh-project", action="store_true",
                        help="give the guests a directory neither CLI has seen, which brings back "
                             "Claude's one-time workspace-trust dialog (step B records it). Off by "
                             "default: accepting it leaves a `hasTrustDialogAccepted` entry in the "
                             "user's ~/.claude.json, one per directory.")
    args = parser.parse_args(argv)
    chosen = [s.strip().upper() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in chosen if s not in RUNNERS]
    if unknown:
        parser.error(f"unknown step(s) {unknown}; known: {', '.join(STEPS)}")
    work = Path(args.work_dir)
    work.mkdir(parents=True, exist_ok=True)
    global PROJECT
    PROJECT = f"proj-{int(time.time())}" if args.fresh_project else "proj"
    (work / PROJECT).mkdir(exist_ok=True)

    report = Report()
    print("\n".join(versions()))
    for step in STEPS:
        if step in chosen:
            RUNNERS[step](work, report)

    elapsed = time.monotonic() - report.started
    print("\n===== summary =====")
    for step in STEPS:
        if step in report.steps:
            print(f"{step}: {report.steps[step]['verdict']}")
    print(f"model calls: {report.calls}   wall time: {elapsed:.1f} s")
    return 0 if all(s["verdict"] == "VERIFIED" for s in report.steps.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
