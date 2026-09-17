"""Real interactive Bash/PTY tests; no terminal emulator mock is used here."""
import hashlib
import json
import os
import pty
import select
import signal
import sys
import subprocess
import tempfile
import time
import termios
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class BashSession:
    def __init__(self, home=None, clean=True):
        self.temp = tempfile.TemporaryDirectory(prefix="relay-test-")
        self.runtime = Path(self.temp.name)
        self.output = bytearray()
        self.last = ""
        master, slave = pty.openpty()
        env = dict(os.environ, TERM="xterm-256color", RELAY_RUNTIME_DIR=str(self.runtime),
                   RELAY_SESSION_TOKEN="test-session-token", RELAY_SHELL_EVENT=str(ROOT / "shell/event.py"),
                   RELAY_PYTHON=sys.executable, RELAY_CLEAN_SHELL="1" if clean else "0")
        if home:
            env["HOME"] = str(home)
        self.process = subprocess.Popen([sys.executable, "-S", str(ROOT / "tests/pty_child.py"),
            "--noprofile", "--rcfile", str(ROOT / "shell/integration.bash"), "-i"],
            stdin=slave, stdout=slave, stderr=slave, env=env, start_new_session=True)
        os.close(slave)
        self.pid, self.master = self.process.pid, master
        os.set_blocking(master, False)

    def drain(self):
        while select.select([self.master], [], [], 0)[0]:
            try:
                chunk = os.read(self.master, 65536)
                if not chunk:
                    break
                self.output.extend(chunk)
            except (BlockingIOError, OSError):
                break

    def wait(self, stage, timeout=4):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.drain()
            try:
                event = json.loads((self.runtime / "state.json").read_text())
                if event['event'] == stage and event['sequence'] != self.last and (stage != 'ready' or not (termios.tcgetattr(self.master)[3] & termios.ICANON)):
                    self.last = event['sequence']
                    return event
            except (FileNotFoundError, json.JSONDecodeError):
                pass
            time.sleep(0.01)
        raise AssertionError(f"Timed out waiting for {stage}: {bytes(self.output[-2500:])!r}")

    def submit(self, text):
        data = text.encode()
        (self.runtime / "input.txt").write_bytes(data)
        os.write(self.master, b'\x18\x12')
        event = self.wait('loaded')
        assert event['input_sha256'] == hashlib.sha256(data).hexdigest()
        os.write(self.master, b'\r')

    def close(self):
        try:
            os.killpg(self.pid, signal.SIGHUP)
            self.process.wait(timeout=0.5)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            self.process.kill()
            self.process.wait(timeout=1)
        os.close(self.master)
        self.temp.cleanup()

class ShellTests(unittest.TestCase):
    def setUp(self):
        self.session = BashSession()
        self.session.wait('ready')
    def tearDown(self):
        self.session.close()

    def test_command_and_exit_status(self):
        s = self.session
        s.submit("printf 'RELAY_OK\\n'; false")
        state = s.wait('ready')
        s.drain()
        self.assertEqual(state['status'], 1)
        self.assertIn(b'RELAY_OK', s.output)

    def test_cwd_and_environment_persist(self):
        s = self.session
        s.submit(f"cd '{s.runtime}'; export RELAY_SAMPLE=42")
        state = s.wait('ready')
        self.assertEqual(state['cwd'], str(s.runtime))
        s.submit('printf "PERSIST=%s\\n" "$RELAY_SAMPLE"')
        s.wait('ready'); s.drain()
        self.assertIn(b'PERSIST=42', s.output)

    def test_multiline_unicode(self):
        s = self.session
        s.submit("printf '%s\\n' 'héllo 世界 🐚'\nprintf '%s\\n' 'SECOND_LINE'\n")
        s.wait('ready'); s.drain()
        self.assertIn('héllo 世界 🐚'.encode(), s.output)
        self.assertIn(b'SECOND_LINE', s.output)

    def test_heredoc(self):
        s = self.session
        s.submit("cat <<'EOF'\nHEREDOC_CONTENT\nEOF\n")
        state = s.wait('ready'); s.drain()
        self.assertEqual(state['status'], 0)
        self.assertIn(b'HEREDOC_CONTENT', s.output)

    def test_native_read_and_interrupt(self):
        s = self.session
        s.submit("read -r -p 'TYPE_HERE:' value; printf 'GOT=%s\\n' \"$value\"")
        s.wait('running')
        os.write(s.master, b'native input\r')
        s.wait('ready'); s.drain()
        self.assertIn(b'GOT=native input', s.output)
        s.submit('sleep 10')
        s.wait('running')
        os.write(s.master, b'\x03')
        self.assertEqual(s.wait('ready')['status'], 130)

    def test_aliases_reported(self):
        s = self.session
        s.submit("alias relayhello='echo ALIAS_OK'")
        state = s.wait('ready')
        self.assertIn('relayhello', state['known_commands'])
        s.submit('relayhello')
        s.wait('ready'); s.drain()
        self.assertIn(b'ALIAS_OK', s.output)

class ShellCompatibilityTests(unittest.TestCase):
    def test_preserves_prompt_array_and_status(self):
        with tempfile.TemporaryDirectory() as home:
            Path(home, '.bashrc').write_text("PROMPT_COMMAND=('printf PROMPT_A' 'printf PROMPT_B')\nPS1='test> '\n")
            s = BashSession(home, clean=False)
            try:
                s.wait('ready')
                s.submit('false')
                state = s.wait('ready'); s.drain()
                self.assertEqual(state['status'], 1)
                self.assertIn(b'PROMPT_A', s.output)
                self.assertIn(b'PROMPT_B', s.output)
            finally:
                s.close()

    def test_custom_debug_trap_fails_to_native(self):
        with tempfile.TemporaryDirectory() as home:
            Path(home, '.bashrc').write_text("trap ':' DEBUG\n")
            s = BashSession(home, clean=False)
            try:
                s.wait('unsupported')
            finally:
                s.close()
