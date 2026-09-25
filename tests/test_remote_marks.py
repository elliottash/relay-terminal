#!/usr/bin/env python3
"""Marks over a mosh link: OSC 52 as the only OSC that survives (#XQ8F).

mosh syncs the screen state, not the byte stream, so the bare OSC 133/7/777/7772 marks
shell/remote-integration.sh emits never reach the client. But mosh 1.4 forwards OSC 52
(clipboard writes), so with RELAY_M=1 the script wraps every mark as

    ESC ] 52 ; c ; <base64 of "relay:" + the OSC payload> BEL

and Relay's terminal cores unpack it again (engine/tests/CoreTest.cpp). These tests run
the script the way the GUI types it (src/RemoteSession.cpp bootstrapLine, gzip+base64 on
one eval line, the row count inside the payload) in real interactive bash and zsh on a
pty, plus one end-to-end pass through a real mosh login on this machine.

Run: python3 tests/test_remote_marks.py
"""

from __future__ import annotations

import base64
import fcntl
import gzip
import os
import pty
import select
import shutil
import struct
import subprocess
import tempfile
import termios
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REMOTE = ROOT / "shell" / "remote-integration.sh"


def stripped_script() -> bytes:
    """The script as the GUI types it: comments and blank lines removed."""
    return b"".join(row + b"\n" for row in REMOTE.read_bytes().split(b"\n")
                    if row.strip() and not row.strip().startswith(b"#"))


def typed_line(rows: int, mosh: bool) -> str:
    """The eval line relay::remote::bootstrapLine builds. RELAY_M rides inside the payload
    beside RELAY_R, never in front of the eval, for the same reason RELAY_R does: a shell
    that cannot parse a prefix assignment prints the payload back at the user (#S5SH)."""
    settings = f"RELAY_R={rows}"
    if mosh:
        settings += "\nRELAY_M=1"
    blob = base64.b64encode(gzip.compress((settings + "\n").encode() + stripped_script(), mtime=0)).decode()
    return f" eval \"$(printf %s '{blob}' | base64 -d | gzip -dc)\""


def rows_for(mosh: bool, columns: int = 80) -> int:
    """The settled row count bootstrapLine computes: the count is part of what it counts."""
    rows = 1
    for _ in range(4):
        again = max(1, (len(typed_line(rows, mosh)) + columns - 1) // columns)
        if again == rows:
            break
        rows = again
    return rows


def wrapped(payload: bytes) -> bytes:
    """The OSC 52 form the script uses under RELAY_M=1 (a BEL terminator)."""
    return b"\x1b]52;c;" + base64.b64encode(payload) + b"\x07"


class PtyShell:
    """A shell on a pty, typed into the way Relay types into a remote shell."""

    def __init__(self, argv: list[str], env: dict[str, str], rows: int = 24, cols: int = 80):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.proc = subprocess.Popen(argv, stdin=slave, stdout=slave, stderr=slave, env=env,
                                     start_new_session=True, preexec_fn=PtyShell._controlling_tty)
        os.close(slave)
        self.output = b""

    @staticmethod
    def _controlling_tty() -> None:
        fcntl.ioctl(0, termios.TIOCSCTTY, 0)

    def read(self, seconds: float = 1.0) -> None:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([self.master], [], [], 0.1)[0]:
                try:
                    self.output += os.read(self.master, 65536)
                except OSError:
                    break

    def wait_for(self, until: bytes, seconds: float = 20.0, since: int = 0) -> None:
        end = time.monotonic() + seconds
        while self.output.find(until, since) == -1:
            if self.proc.poll() is not None:
                self.read(0.2)
                if self.output.find(until, since) == -1:
                    raise AssertionError(f"shell exited: {self.output[-500:]!r}")
                return
            if time.monotonic() > end:
                raise AssertionError(f"timed out: {self.output[-1500:]!r}")
            self.read(0.2)

    def run(self, line: str, wait: bytes = b"RP> ", seconds: float = 20.0) -> None:
        since = len(self.output)
        os.write(self.master, line.encode() + b"\r")
        self.wait_for(wait, seconds, since)

    def close(self) -> None:
        try:
            os.killpg(self.proc.pid, 9)
        except ProcessLookupError:
            pass
        try:
            self.proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass
        os.close(self.master)


def ssh_localhost_ok() -> bool:
    """mosh needs `ssh host true` to start its server."""
    try:
        return subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no",
                               "-o", "ConnectTimeout=5", "localhost", "true"],
                              capture_output=True, timeout=15).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


class RemoteMarksTest(unittest.TestCase):
    def shell(self, argv: list[str], ps1_first: bool = False) -> PtyShell:
        home = tempfile.mkdtemp(prefix="relay-marks-")
        env = dict(os.environ, HOME=home, TERM="xterm", COLUMNS="80", LINES="24",
                   PS1="RP> ", LC_ALL="C.UTF-8", LANG="C.UTF-8")
        env.pop("ZELLIJ", None), env.pop("TMUX", None), env.pop("STY", None), env.pop("RELAY_M", None)
        shell = PtyShell(argv, env)
        self.addCleanup(shell.close)
        shell.read(1.0)
        if ps1_first:   # zsh -f draws its own prompt until PS1 is set
            shell.run("PS1='RP> '", wait=b"RP> ")
        return shell

    def marks(self, mosh: bool, argv: list[str], ps1_first: bool = False) -> PtyShell:
        shell = self.shell(argv, ps1_first)
        shell.run(typed_line(rows_for(mosh), mosh))
        shell.run(":")   # one command, so 133;D and OSC 7 fire
        return shell

    def assert_prompt_wrapped_once(self, shell: PtyShell, print_ps1: str) -> None:
        """The PS1 guard (*133;A*|*52;c;*) must hold in bash's text form and zsh's byte
        form alike, or every prompt would re-wrap PS1 and grow it without bound."""
        shell.output = b""
        shell.run(print_ps1)
        a_mark = base64.b64encode(b"relay:133;A")   # once in the printed PS1, once in the redrawn prompt
        self.assertEqual(shell.output.count(a_mark), 2, shell.output[-400:])

    def test_bash_wraps_marks_in_osc52_under_relay_m(self):
        if shutil.which("bash") is None:
            self.skipTest("bash is not installed")
        shell = self.marks(mosh=True,
                           argv=["bash", "--noprofile", "--norc", "--noediting", "-i"])
        out = shell.output
        self.assert_prompt_wrapped_once(shell, 'printf %s "$PS1"')
        # Every mark arrives as a clipboard write whose decoded text starts with "relay:".
        for payload in (b"relay:133;A", b"relay:133;B", b"relay:133;D;0", b"relay:7772;shell"):
            self.assertIn(wrapped(payload), out, payload)
        # OSC 7 rides the channel too: its host and path are inside the base64.
        self.assertIn(b"\x1b]52;c;" + base64.b64encode(b"relay:7;file://"), out)
        # And nothing rides bare, which mosh would drop.
        self.assertNotIn(b"\x1b]133;", out)
        self.assertNotIn(b"\x1b]7;file://", out)
        self.assertNotIn(b"\x1b]777;", out)
        self.assertNotIn(b"\x1b]7772;", out)

    def test_bash_marks_stay_bare_without_relay_m(self):
        if shutil.which("bash") is None:
            self.skipTest("bash is not installed")
        out = self.marks(mosh=False,
                         argv=["bash", "--noprofile", "--norc", "--noediting", "-i"]).output
        self.assertIn(b"\x1b]133;A\x07", out)
        self.assertIn(b"\x1b]133;D;0\x07", out)
        self.assertIn(b"\x1b]7;file://", out)
        self.assertNotIn(b"\x1b]52;c;", out)

    def test_zsh_wraps_marks_in_osc52_under_relay_m(self):
        zsh = shutil.which("zsh")
        if zsh is None:
            self.skipTest("zsh is not installed")
        shell = self.marks(mosh=True, argv=[zsh, "-f", "-i"], ps1_first=True)
        out = shell.output
        self.assert_prompt_wrapped_once(shell, 'print -rn -- "$PS1"')
        for payload in (b"relay:133;A", b"relay:133;B", b"relay:133;D;0", b"relay:7772;end-output"):
            self.assertIn(wrapped(payload), out, payload)
        self.assertIn(b"\x1b]52;c;" + base64.b64encode(b"relay:7;file://"), out)
        self.assertNotIn(b"\x1b]133;", out)
        self.assertNotIn(b"\x1b]7;file://", out)
        self.assertNotIn(b"\x1b]7772;", out)

    def test_zsh_marks_stay_bare_without_relay_m(self):
        zsh = shutil.which("zsh")
        if zsh is None:
            self.skipTest("zsh is not installed")
        out = self.marks(mosh=False, argv=[zsh, "-f", "-i"], ps1_first=True).output
        self.assertIn(b"\x1b]133;A\x07", out)
        self.assertIn(b"\x1b]7772;end-output\x07", out)
        self.assertNotIn(b"\x1b]52;c;", out)

    def test_typed_line_carries_relay_m_inside_the_payload(self):
        line = typed_line(3, mosh=True)
        self.assertTrue(line.startswith(' eval "$(printf'), line[:40])
        self.assertNotIn("RELAY_M=", line)          # never typed in front of the eval
        self.assertGreater(len(line), len(typed_line(3, mosh=False)))
        self.assertLess(len(line), 3000)            # the terminal input buffer (#S5SH)

    def test_mosh_delivers_osc52_and_drops_osc133(self):
        """End to end: a real mosh login forwards OSC 52 to the client and drops OSC 133."""
        if shutil.which("mosh") is None:
            self.skipTest("mosh is not installed")
        if shutil.which("ssh") is None or not ssh_localhost_ok():
            self.skipTest("ssh -o BatchMode=yes localhost does not work here")
        blob = base64.b64encode(b"relay:133;A").decode()
        inner = rf"printf '\033]133;A\a\033]52;c;{blob}\a'; sleep 2"
        home = tempfile.mkdtemp(prefix="relay-mosh-")
        env = dict(os.environ, HOME=home, TERM="xterm-256color", LANG="C.UTF-8")
        client = PtyShell(["mosh", "localhost", "--", "sh", "-c", inner], env, rows=24, cols=80)
        self.addCleanup(client.close)
        deadline = time.monotonic() + 45
        while (wrapped(b"relay:133;A") not in client.output and client.proc.poll() is None
               and time.monotonic() < deadline):
            client.read(0.5)
        out = client.output
        self.assertIn(wrapped(b"relay:133;A"), out)          # the OSC 52 arrived at the client
        self.assertNotIn(b"\x1b]133;", out)                  # the bare mark did not
        if client.proc.poll() not in (None, 0):
            self.skipTest(f"mosh could not run here: {out[-200:]!r}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
