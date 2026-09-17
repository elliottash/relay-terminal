import os
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from relay_core.tools import ToolExecutor, MAX_OUTPUT
from relay_core.provider import Cancelled

class ToolTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.cancel = threading.Event()
        self.events = []
        self.tools = ToolExecutor(self.tmp.name, self.events.append, self.cancel)
    def tearDown(self):
        self.tmp.cleanup()

    def test_prepare_does_not_execute_command(self):
        self.tools.prepare('run_command', {'command': 'touch sentinel'})
        self.assertFalse((self.root / 'sentinel').exists())

    def test_command_output_and_exit(self):
        prepared = self.tools.prepare('run_command', {'command': "printf 'hello'; exit 7"})
        result = self.tools.execute(prepared)
        self.assertEqual(result['output'], 'hello')
        self.assertEqual(result['exit_code'], 7)

    def test_secret_env_removed(self):
        with patch.dict(os.environ, {'MOONSHOT_API_KEY': 'DO_NOT_LEAK', 'ZAI_API_KEY': 'NO', 'RELAY_SESSION_TOKEN': 'NO'}):
            result = self.tools.execute(self.tools.prepare('run_command', {'command': 'env'}))
        self.assertNotIn('DO_NOT_LEAK', result['output'])
        self.assertNotIn('ZAI_API_KEY', result['output'])
        self.assertNotIn('RELAY_SESSION_TOKEN', result['output'])

    def test_timeout(self):
        started = time.monotonic()
        result = self.tools.execute(self.tools.prepare('run_command', {'command': 'sleep 20', 'timeout_seconds': 1}))
        self.assertTrue(result['timed_out'])
        self.assertLess(time.monotonic() - started, 3)

    def test_timeout_after_stdout_closed(self):
        result = self.tools.execute(self.tools.prepare('run_command', {'command': 'exec 1>&- 2>&-; sleep 20', 'timeout_seconds': 1}))
        self.assertTrue(result['timed_out'])

    def test_output_cap(self):
        result = self.tools.execute(self.tools.prepare('run_command', {'command': 'head -c 100000 /dev/zero | tr "\\0" x'}))
        self.assertTrue(result['truncated'])
        self.assertEqual(len(result['output']), MAX_OUTPUT)

    def test_file_read_and_diff(self):
        path = self.root / 'code.txt'
        path.write_text('old\n')
        read = self.tools.execute(self.tools.prepare('read_file', {'path': 'code.txt'}))
        self.assertEqual(read['content'], 'old\n')
        prepared = self.tools.prepare('write_file', {'path': 'code.txt', 'content': 'new\n'})
        self.assertIn('-old', prepared.preview)
        self.assertIn('+new', prepared.preview)
        self.assertEqual(path.read_text(), 'old\n')
        self.tools.execute(prepared)
        self.assertEqual(path.read_text(), 'new\n')

    def test_refuse_stale_write(self):
        path = self.root / 'code.txt'; path.write_text('old')
        prepared = self.tools.prepare('write_file', {'path': 'code.txt', 'content': 'model edit'})
        path.write_text('user edit')
        with self.assertRaisesRegex(ValueError, 'changed while the write was prepared'):
            self.tools.execute(prepared)
        self.assertEqual(path.read_text(), 'user edit')

    def test_new_file_race(self):
        prepared = self.tools.prepare('write_file', {'path': 'new.txt', 'content': 'model edit'})
        (self.root / 'new.txt').write_text('user edit')
        with self.assertRaises(ValueError): self.tools.execute(prepared)

    def test_path_escape_and_secret_guard(self):
        for path in ['../etc/passwd', '/etc/passwd', '.env', '.env.local', '.ssh/id_rsa', '.git/config', 'secret.pem']:
            with self.subTest(path=path):
                with self.assertRaises((ValueError, FileNotFoundError)):
                    self.tools.prepare('read_file', {'path': path})
        (self.root / 'outside').symlink_to('/etc/passwd')
        with self.assertRaises(ValueError):
            self.tools.prepare('read_file', {'path': 'outside'})

    def test_symlink_swap_after_approval(self):
        path = self.root / 'file.txt'; path.write_text('safe')
        prepared = self.tools.prepare('read_file', {'path': 'file.txt'})
        path.unlink(); path.symlink_to('/etc/passwd')
        with self.assertRaises(ValueError): self.tools.execute(prepared)

    def test_fifo_does_not_block(self):
        os.mkfifo(self.root / 'fifo')
        prepared = self.tools.prepare('read_file', {'path': 'fifo'})
        with self.assertRaisesRegex(ValueError, 'regular'):
            self.tools.execute(prepared)

    def test_cancel_before_execution(self):
        prepared = self.tools.prepare('run_command', {'command': 'touch sentinel'})
        self.cancel.set()
        with self.assertRaises(Cancelled): self.tools.execute(prepared)
        self.assertFalse((self.root / 'sentinel').exists())

    def test_cancel_running_command(self):
        errors = []
        def run():
            try:
                self.tools.execute(self.tools.prepare('run_command', {'command': 'sleep 30'}))
            except Cancelled:
                errors.append('cancelled')
        thread = threading.Thread(target=run); thread.start()
        time.sleep(0.2); self.cancel.set(); self.tools.stop_process()
        thread.join(2)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, ['cancelled'])

    def test_unknown_tool_and_argument(self):
        with self.assertRaises(ValueError): self.tools.prepare('rm', {})
        with self.assertRaises(ValueError): self.tools.prepare('run_command', {'command': 'true', 'auto_approve': True})
        with self.assertRaises(ValueError): self.tools.prepare('run_command', {'command': 'true', 'timeout_seconds': True})

    def test_create_and_list(self):
        self.tools.execute(self.tools.prepare('write_file', {'path': 'created.txt', 'content': 'hello'}))
        result = self.tools.execute(self.tools.prepare('list_directory', {'path': '.'}))
        self.assertEqual(result['entries'][0]['name'], 'created.txt')
