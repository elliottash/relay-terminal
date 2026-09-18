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

    # At its timeout a command is handed back as a job, not killed (relay_core/jobs.py).
    def test_timeout(self):
        started = time.monotonic()
        result = self.tools.execute(self.tools.prepare('run_command', {'command': 'sleep 20', 'timeout_seconds': 1}))
        self.assertTrue(result['still_running'])
        self.assertNotIn('exit_code', result)
        self.assertLess(time.monotonic() - started, 3)
        stopped = self.tools.execute(self.tools.prepare('stop_command', {'job_id': result['job_id']}))
        self.assertTrue(stopped['stopped'])

    def test_timeout_after_stdout_closed(self):
        result = self.tools.execute(self.tools.prepare('run_command', {'command': 'exec 1>&- 2>&-; sleep 20', 'timeout_seconds': 1}))
        self.assertTrue(result['still_running'])
        self.tools.execute(self.tools.prepare('stop_command', {'job_id': result['job_id']}))

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

    def test_write_file_reports_created_and_diff_counts(self):
        created = self.tools.execute(self.tools.prepare('write_file', {'path': 'new.txt', 'content': 'a\nb\n'}))
        self.assertTrue(created['created'])
        self.assertEqual((created['added'], created['removed']), (2, 0))
        changed = self.tools.execute(self.tools.prepare('write_file', {'path': 'new.txt', 'content': 'a\nc\n'}))
        self.assertFalse(changed['created'])
        self.assertEqual((changed['added'], changed['removed']), (1, 1))

    # ----- edit_file: one exact string, not the whole file -------------------------------
    def test_edit_file_unique_replacement(self):
        path = self.root / 'code.txt'
        path.write_text('alpha\nbeta\ngamma\n')
        path.chmod(0o640)
        prepared = self.tools.prepare('edit_file', {'path': 'code.txt', 'old_string': 'beta', 'new_string': 'BETA'})
        self.assertTrue(prepared.preview.startswith(f'EDIT FILE\n\n{path}\n\n'))
        self.assertIn('--- a/code.txt', prepared.preview)
        self.assertIn('-beta', prepared.preview)
        self.assertIn('+BETA', prepared.preview)
        self.assertTrue(prepared.preview.endswith('Old bytes: 17; new bytes: 17.'))
        self.assertEqual(path.read_text(), 'alpha\nbeta\ngamma\n')  # preparing never writes
        result = self.tools.execute(prepared)
        self.assertEqual(path.read_text(), 'alpha\nBETA\ngamma\n')
        self.assertEqual(path.stat().st_mode & 0o777, 0o640)  # the file keeps its mode
        self.assertEqual(result['replacements'], 1)
        self.assertEqual((result['added'], result['removed']), (1, 1))
        self.assertEqual(result['written_bytes'], 17)
        self.assertNotIn('created', result)

    def test_edit_file_ambiguous_then_replace_all(self):
        path = self.root / 'code.txt'
        path.write_text('x = 1\ny = x\nz = x\n')
        with self.assertRaisesRegex(ValueError, 'occurs 3 times'):
            self.tools.prepare('edit_file', {'path': 'code.txt', 'old_string': 'x', 'new_string': 'w'})
        self.assertEqual(path.read_text(), 'x = 1\ny = x\nz = x\n')
        result = self.tools.execute(self.tools.prepare(
            'edit_file', {'path': 'code.txt', 'old_string': 'x', 'new_string': 'w', 'replace_all': True}))
        self.assertEqual(path.read_text(), 'w = 1\ny = w\nz = w\n')
        self.assertEqual(result['replacements'], 3)
        self.assertEqual((result['added'], result['removed']), (3, 3))

    def test_edit_file_refuses_what_it_cannot_apply(self):
        (self.root / 'code.txt').write_text('alpha\n')
        cases = [({'old_string': 'delta', 'new_string': 'x'}, 'not found'),
                 ({'old_string': '', 'new_string': 'x'}, 'must not be empty'),
                 ({'old_string': 'alpha', 'new_string': 'alpha'}, 'identical'),
                 ({'old_string': 'alpha', 'new_string': 'x', 'replace_all': 'yes'}, 'true or false')]
        for arguments, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    self.tools.prepare('edit_file', {'path': 'code.txt', **arguments})
        with self.assertRaisesRegex(ValueError, 'use write_file'):
            self.tools.prepare('edit_file', {'path': 'missing.txt', 'old_string': 'a', 'new_string': 'b'})
        with self.assertRaises(ValueError):  # unexpected argument
            self.tools.prepare('edit_file', {'path': 'code.txt', 'old_string': 'a', 'new_string': 'b', 'mode': '644'})
        self.assertEqual((self.root / 'code.txt').read_text(), 'alpha\n')

    def test_refuse_stale_edit(self):
        path = self.root / 'code.txt'; path.write_text('alpha\n')
        prepared = self.tools.prepare('edit_file', {'path': 'code.txt', 'old_string': 'alpha', 'new_string': 'omega'})
        path.write_text('alpha\nuser line\n')
        with self.assertRaisesRegex(ValueError, 'changed while the write was prepared'):
            self.tools.execute(prepared)
        self.assertEqual(path.read_text(), 'alpha\nuser line\n')
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'appeared or disappeared'):
            self.tools.execute(prepared)

    def test_edit_file_keeps_the_path_guards(self):
        (self.root / 'link.txt').symlink_to('/etc/passwd')
        for path in ['link.txt', '.env', '.ssh/id_rsa', '../etc/passwd', '.git/config']:
            with self.subTest(path=path):
                with self.assertRaises((ValueError, FileNotFoundError)):
                    self.tools.prepare('edit_file', {'path': path, 'old_string': 'root', 'new_string': 'relay'})

    def test_cancel_before_edit(self):
        path = self.root / 'code.txt'; path.write_text('alpha\n')
        prepared = self.tools.prepare('edit_file', {'path': 'code.txt', 'old_string': 'alpha', 'new_string': 'omega'})
        self.cancel.set()
        with self.assertRaises(Cancelled): self.tools.execute(prepared)
        self.assertEqual(path.read_text(), 'alpha\n')
