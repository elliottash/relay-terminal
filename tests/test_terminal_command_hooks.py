"""#TCXT: acceptance hooks on a real PTY; marker payloads are not history."""
import base64
import os
import re
import shutil
import subprocess
import tempfile
import termios
import time
import unittest
from pathlib import Path

from test_shell import BashSession, ROOT

MARKER = re.compile(rb'\x1b]777;notify;relay-command;([^;\x07]*);([^;\x07]*);([^\x07]*)\x07')


class CommandHookTests(unittest.TestCase):
    def start(self, rc=None):
        home = tempfile.TemporaryDirectory()
        self.addCleanup(home.cleanup)
        if rc:
            Path(home.name, '.bashrc').write_text(rc)
        session = BashSession(home=home.name, clean=not bool(rc))
        self.addCleanup(session.close)
        session.wait('ready')
        session.output.clear()
        return session

    def records(self, session):
        session.drain()
        return [(m[0].decode(), base64.b64decode(m[1]).decode(),
                 base64.b64decode(m[2]).decode()) for m in MARKER.findall(session.output)]

    def test_full_compound_before_output_and_completion_toggle_off(self):
        s = self.start('export RELAY_SHELL_INTEGRATION=0\nHISTCONTROL=ignorespace\n')
        command = " printf '%s\\n' 'héllo 世界'; printf 'LAST\\n'; false"
        os.write(s.master, command.encode() + b'\r')
        self.assertEqual(s.wait('ready')['status'], 1)
        self.assertEqual(self.records(s), [('test-session-token', command, str(ROOT))])
        output = bytes(s.output)
        self.assertLess(MARKER.search(output).end(), output.index('héllo 世界\r\n'.encode()))
        self.assertIn(b'\x1b]133;D;1\x07\x1b]133;A\x07', output)

    def test_subshell_output_follows_start_marker(self):
        s = self.start()
        command = "(printf 'SUBSHELL_OUTPUT\\n')"
        s.submit(command)
        s.wait('ready')
        self.assertEqual([r[1] for r in self.records(s)], [command])
        output = bytes(s.output)
        self.assertLess(MARKER.search(output).end(), output.index(b'SUBSHELL_OUTPUT\r\n'))
        self.assertIn(b'\x1b]133;D;0\x07', output)

    def test_staged_multiline_full_command(self):
        s = self.start()
        command = "printf 'ONE\\n'\nprintf 'TWO\\n'\n"
        s.submit(command)
        s.wait('ready')
        self.assertEqual([r[1] for r in self.records(s)], [command])

    def test_continuation_reports_unavailable_not_last_fragment(self):
        s = self.start()
        os.write(s.master, b"if true; then\r")
        time.sleep(.1)
        os.write(s.master, b"printf 'BODY\\n'\rfi\r")
        s.wait('ready')
        self.assertEqual([r[1] for r in self.records(s)], [''])

    def test_cancelled_input_does_not_leak_into_next_command(self):
        s = self.start()
        os.write(s.master, b"echo 'CANCELLED\r")
        time.sleep(.1)
        os.write(s.master, b'\x03')
        s.wait('ready')
        os.write(s.master, b"printf 'NEXT\\n'\r")
        s.wait('ready')
        self.assertEqual([r[1] for r in self.records(s)], ["printf 'NEXT\\n'"])

    def test_secret_program_input_is_not_command(self):
        s = self.start()
        command = "read -rs secret; printf 'DONE\\n'"
        s.submit(command)
        s.wait('running')
        deadline = time.monotonic() + 2
        while termios.tcgetattr(s.master)[3] & termios.ECHO:
            self.assertLess(time.monotonic(), deadline, 'read -s did not disable echo')
            time.sleep(.01)
        os.write(s.master, b'secret-program-input\r')
        s.wait('ready')
        self.assertEqual([r[1] for r in self.records(s)], [command])
        self.assertNotIn(b'secret-program-input', s.output)

    def test_vi_insert_acceptance(self):
        s = self.start('set -o vi\n')
        os.write(s.master, b"printf 'VI\\n'; false\r")
        s.wait('ready')
        self.assertEqual(self.records(s)[0][1], "printf 'VI\\n'; false")

    def test_custom_enter_macro_preserved_and_unavailable(self):
        s = self.start("bind '\"\\C-m\": \"\\C-j\"'\nbind '\"\\C-j\": \"\\e[99~\"'\nbind '\"\\e[99~\": accept-line'\n")
        os.write(s.master, b"printf 'CUSTOM\\n'\r")
        s.wait('ready')
        self.assertEqual([r[1] for r in self.records(s)], [''])
        self.assertIn(b'CUSTOM\r\n', s.output)

    def test_existing_debug_trap_uses_unavailable_ps0_fallback(self):
        with tempfile.TemporaryDirectory() as home:
            Path(home, '.bashrc').write_text("trap ':' DEBUG\n")
            s = BashSession(home=home, clean=False)
            self.addCleanup(s.close)
            s.wait('unsupported')
            time.sleep(.05)
            s.drain()
            s.output.clear()
            os.write(s.master, b"printf 'FALLBACK\\n'; false\r")
            deadline = time.monotonic() + 3
            while b'\x1b]133;D;1\x07' not in s.output:
                self.assertLess(time.monotonic(), deadline)
                time.sleep(.01)
                s.drain()
            self.assertEqual([r[1] for r in self.records(s)], [''])
            self.assertIn(b'FALLBACK\r\n', s.output)

    def test_start_cwd_precedes_cd(self):
        s = self.start()
        s.submit("cd /; printf 'AFTER_CD\\n'")
        self.assertEqual(s.wait('ready')['cwd'], '/')
        self.assertEqual(self.records(s)[0][2], str(ROOT))

    @unittest.skipUnless(shutil.which('zsh'), 'Zsh unavailable')
    def test_zsh_exact_preexec_and_status(self):
        script = f'''source {ROOT}/shell/relay-integration.zsh
RELAY_SESSION_TOKEN=test-session-token
__relay_mark_preexec 'echo one; echo two'
false
__relay_mark_precmd
'''
        result = subprocess.run(['zsh', '-f', '-i', '-c', script], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(base64.b64decode(MARKER.search(result.stdout)[2]), b'echo one; echo two')
        self.assertIn(b'\x1b]133;D;1\x07', result.stdout)
        self.assertIn(b'\x1b]133;A\x07', result.stdout)

    def test_fish_and_powershell_sources_use_acceptance_not_history(self):
        fish = (ROOT / 'shell/relay-integration.fish').read_text()
        ps = (ROOT / 'shell/integration.ps1').read_text()
        self.assertIn('--on-event fish_preexec', fish)
        self.assertIn('"$argv[1]"', fish)
        self.assertIn('--on-event fish_postexec', fish)
        self.assertIn('UTF8.GetBytes($line)', ps)
        self.assertIn('$line = & $global:__relay_readline', ps)
        for text in (fish, ps):
            self.assertIn('777;notify;relay-command;', text)
            self.assertIn('133;D;', text)
            self.assertIn('133;A', text)

    def test_remote_bash_unavailable_zsh_exact_source(self):
        text = (ROOT / 'shell/remote-integration.sh').read_text()
        self.assertIn('text= # Bash has no exact acceptance hook', text)
        self.assertIn('__relay_r_gap "$1"', text)
        self.assertIn('__relay_r_command "$text"', text)
        self.assertIn('relay-command;$__relay_r_token;', text)
