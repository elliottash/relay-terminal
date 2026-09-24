"""zellij inside a Relay pane (card #VD2M), against a real zellij.

Three facts this guards, measured on zellij 0.45.1 aarch64 and expected to hold until zellij
grows a passthrough:

1. `$ZELLIJ` is set (to "0") inside every pane — truthy by presence, never by value, which is
   how shell/remote-integration.sh detects it.
2. OSC 133 written by the shell inside zellij does not reach the outer terminal: zellij has no
   tmux-style passthrough, so Relay's prompt detection for a zellij pane is the screen
   classifier (src/ScreenPrompt.cpp rowHoldsPrompt), never shell marks.
3. Sourcing shell/remote-integration.sh inside zellij says so once and emits no marks: the
   `__relay_r_o` wrapper becomes a no-op under $ZELLIJ.

The tips overlay zellij shows on a fresh session eats input until ESC; every probe dismisses
it first, which is also what a person inside Relay has to do once.
"""
import fcntl
import os
import pty
import select
import struct
import subprocess
import tempfile
import termios
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REMOTE = ROOT / "shell/remote-integration.sh"
SESSION = "relay-test"


def find_zellij():
    for candidate in (os.environ.get("RELAY_TEST_ZELLIJ"),
                      subprocess.run(["sh", "-c", "command -v zellij"], capture_output=True,
                                     text=True).stdout.strip(),
                      os.path.expanduser("~/.local/bin/zellij")):
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


class ZellijTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.zellij = find_zellij()
        if not cls.zellij:
            raise unittest.SkipTest("no zellij installed (set RELAY_TEST_ZELLIJ to point at one)")
        cls.workdir = tempfile.mkdtemp(prefix="relay-zellij-test-")

    def setUp(self):
        self.master = self.slave = -1
        self.buf = b""
        self.proc = None
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 120, 0, 0))

    def tearDown(self):
        for fd in (self.slave, self.master):
            try:
                if fd >= 0:
                    os.close(fd)
            except OSError:
                pass
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
        subprocess.run([self.zellij, "kill-session", SESSION], capture_output=True)

    def read_for(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.master], [], [], 0.2)
            if ready:
                try:
                    data = os.read(self.master, 65536)
                except OSError:
                    break
                if not data:
                    break
                self.buf += data

    def start(self):
        env = dict(os.environ, TERM="xterm-256color", ZELLIJ_CONFIG_DIR=os.path.join(self.workdir, "config"))
        os.makedirs(os.path.join(self.workdir, "config"), exist_ok=True)
        self.proc = subprocess.Popen([self.zellij, "--session", SESSION],
                                     stdin=self.slave, stdout=self.slave, stderr=self.slave,
                                     env=env, start_new_session=True, close_fds=True)
        self.read_for(8)
        os.write(self.master, b"\x1b")   # dismiss the first-run tips overlay
        self.read_for(2)

    def act(self, *args):
        return subprocess.run([self.zellij, "-s", SESSION, "action", *args], capture_output=True, text=True)

    def run_in_pane(self, line, settle=3):
        self.act("write-chars", line)
        self.act("write", "13")
        self.read_for(settle)

    def dump(self):
        path = os.path.join(self.workdir, "screen.txt")
        result = self.act("dump-screen", "--path", path)
        self.assertEqual(result.returncode, 0, result.stderr)
        return Path(path).read_text(encoding="utf-8", errors="replace")

    def test_zellij_env_and_no_osc_passthrough(self):
        self.start()
        self.run_in_pane("echo Z=[$ZELLIJ]")
        self.run_in_pane("printf '\\033]133;A\\033\\'; echo OSC133-SENT")
        screen = self.dump()
        self.assertIn("Z=[0]", screen)           # fact 1: $ZELLIJ set, to "0"
        self.assertIn("OSC133-SENT", screen)     # the printf really ran...
        self.assertNotIn(b"\x1b]133", self.buf)  # ...and fact 2: the OSC never left zellij

    def test_integration_script_goes_quiet_under_zellij(self):
        note = os.path.join(self.workdir, "note.txt")
        marks = os.path.join(self.workdir, "marks.bin")
        probe = os.path.join(self.workdir, "probe.sh")
        with open(probe, "w", encoding="utf-8") as handle:
            handle.write(f'source "{REMOTE}" >"{note}" 2>&1\n'
                         f'__relay_r_o "133;A" >"{marks}" 2>&1\n'
                         f'printf \'NOTE=%s\\n\' "$(head -n1 "{note}")"\n'
                         f'printf \'SIZE=%s\\n\' "$(wc -c <"{marks}")"\n')
        self.start()
        self.run_in_pane(f"bash -ic 'source {probe}'", settle=4)
        screen = self.dump()
        self.assertIn("NOTE=relay: zellij does not pass prompt marks through", screen)  # fact 3, said once
        self.assertIn("SIZE=0", screen)                                               # fact 3, nothing emitted


if __name__ == "__main__":
    unittest.main()
