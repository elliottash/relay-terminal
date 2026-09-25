import os
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from relay_core import board_tools, tools as tools_mod
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

    def test_absolute_and_parent_paths_allowed_inside_workspace(self):
        # Absolute paths and `..` are accepted; resolution plus the containment
        # check confine them, so only paths that resolve outside are refused.
        (self.root / 'sub').mkdir()
        (self.root / 'file.txt').write_text('inside')
        absolute = self.tools.prepare('read_file', {'path': str(self.root / 'file.txt')})
        self.assertEqual(self.tools.execute(absolute)['content'], 'inside')
        via_parent = self.tools.prepare('read_file', {'path': 'sub/../file.txt'})
        self.assertEqual(self.tools.execute(via_parent)['content'], 'inside')
        for path in ['../', '/', str(self.root.parent), 'sub/../../']:
            with self.subTest(path=path):
                with self.assertRaises(ValueError):
                    self.tools.prepare('read_file', {'path': path})

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

    def test_write_file_makes_missing_parent_directories(self):
        # Card #NC17: a write into a directory tree that is not there yet creates it, instead of
        # refusing and costing the model a round trip on mkdir.
        result = self.tools.execute(self.tools.prepare(
            'write_file', {'path': 'deep/deeper/new.txt', 'content': 'hi\n'}))
        self.assertTrue(result['created'])
        self.assertEqual((self.root / 'deep/deeper/new.txt').read_text(), 'hi\n')
        # A file in the way of the directories is an error the model can act on, not a traceback.
        (self.root / 'blocked').write_text('a file, not a directory\n')
        with self.assertRaisesRegex(ValueError, 'could not be created'):
            self.tools.execute(self.tools.prepare(
                'write_file', {'path': 'blocked/under.txt', 'content': 'hi\n'}))

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

    # ----- files past the 128 KiB whole-read limit (card #XG2G) -------------------------
    def big_file(self):
        """~500 KiB of numbered lines, the size class of src/Pane.h."""
        path = self.root / 'big.h'
        path.write_text(''.join(f'    int value_{n} = compute({n});\n' for n in range(1, 16001)))
        path.chmod(0o640)
        self.assertGreater(path.stat().st_size, 500 * 1024)
        return path

    def test_edit_file_works_on_a_file_past_128_kib(self):
        path = self.big_file()
        before = path.read_text()
        prepared = self.tools.prepare('edit_file', {'path': 'big.h', 'old_string': 'value_9000 = compute(9000);',
                                                    'new_string': 'value_9000 = compute(9001);'})
        self.assertIn('-    int value_9000 = compute(9000);', prepared.preview)
        self.assertLess(len(prepared.preview), 2000)                 # the diff, not the file
        result = self.tools.execute(prepared)
        self.assertEqual(result['replacements'], 1)
        self.assertEqual(path.read_text(), before.replace('compute(9000);', 'compute(9001);'))
        self.assertEqual(path.stat().st_mode & 0o777, 0o640)

    def test_a_whole_read_of_a_big_file_is_refused_and_names_the_range(self):
        self.big_file()
        with self.assertRaisesRegex(ValueError, '128 KiB.*from_line/to_line'):
            self.tools.execute(self.tools.prepare('read_file', {'path': 'big.h'}))
        result = self.tools.execute(self.tools.prepare('read_file', {'path': 'big.h', 'from_line': 12000,
                                                                     'to_line': 12002}))
        self.assertEqual(result['content'], ''.join(f'    int value_{n} = compute({n});\n' for n in (12000, 12001, 12002)))
        self.assertEqual(result['total_lines'], 16000)

    def test_files_past_8_mib_are_still_refused(self):
        (self.root / 'huge.txt').write_bytes(b'x' * (8 * 1024 * 1024 + 1))
        with self.assertRaisesRegex(ValueError, '8 MiB'):
            self.tools.prepare('edit_file', {'path': 'huge.txt', 'old_string': 'x', 'new_string': 'y'})
        with self.assertRaisesRegex(ValueError, '8 MiB'):
            self.tools.execute(self.tools.prepare('read_file', {'path': 'huge.txt', 'from_line': 1, 'to_line': 1}))

    def test_a_missed_edit_names_the_line_and_column_it_diverges_at(self):
        self.big_file()
        # The #234Z miss: one extra ')' in a line otherwise copied exactly.
        old = '    int value_7000 = compute(7000);\n    int value_7001 = compute(7001));\n'
        with self.assertRaises(ValueError) as caught:
            self.tools.prepare('edit_file', {'path': 'big.h', 'old_string': old, 'new_string': 'x'})
        message = str(caught.exception)
        self.assertIn('old_string was not found in the file', message)
        self.assertIn('line 7001, column 35: the file has ";\\n', message)
        self.assertIn('old_string (its line 2) has ");\\n"', message)
        # A difference before the anchor line is found walking back from it: indentation.
        old = '\tint value_50 = compute(50);\n    int value_51_is_the_longest_line = compute(51);\n'
        (self.root / 'small.h').write_text('    int value_50 = compute(50);\n    int value_51_is_the_longest_line = compute(51);\n')
        with self.assertRaisesRegex(ValueError, r'line 1, column 4: the file has " int value_50.* has "\\tint'):
            self.tools.prepare('edit_file', {'path': 'small.h', 'old_string': old, 'new_string': 'x'})
        # The difference is inside the only line: half of that line anchors it.
        with self.assertRaisesRegex(ValueError, r'line 8000, column 35: the file has ";\\n .* has "\);"'):
            self.tools.prepare('edit_file', {'path': 'big.h', 'old_string': '    int value_8000 = compute(8000));',
                                             'new_string': 'x'})
        # Nothing to anchor on: today's message, no position.
        with self.assertRaises(ValueError) as caught:
            self.tools.prepare('edit_file', {'path': 'big.h', 'old_string': 'no such text anywhere', 'new_string': 'x'})
        self.assertNotIn('closest match', str(caught.exception))

    def test_cancel_before_edit(self):
        path = self.root / 'code.txt'; path.write_text('alpha\n')
        prepared = self.tools.prepare('edit_file', {'path': 'code.txt', 'old_string': 'alpha', 'new_string': 'omega'})
        self.cancel.set()
        with self.assertRaises(Cancelled): self.tools.execute(prepared)
        self.assertEqual(path.read_text(), 'alpha\n')


# ----- the recursive-walk cost guard (card #2Y96) -----------------------------------------------
#
# Owner's decision, 2026-09-19: a pane in $HOME or / keeps its wide sandbox, and what is refused is
# the cost of crawling it. A narrower path always runs; a non-recursive list never stops working.
class WalkCostTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = Path(self.tmp.name) / 'home' / 'someone'
        (self.home / 'work' / 'repo').mkdir(parents=True)
        self.cancel = threading.Event()
        self.events = []

    def tearDown(self):
        self.tmp.cleanup()

    def executor(self, cwd: Path) -> ToolExecutor:
        return ToolExecutor(str(cwd), self.events.append, self.cancel)

    def refuse(self, command, cwd=None):
        """The refusal a run_command prepare gives, or None when it is allowed through."""
        with patch.dict(os.environ, {'HOME': str(self.home)}):
            tools = self.executor(cwd or self.home)
            try:
                tools.prepare('run_command', {'command': command})
            except ValueError as error:
                return str(error)
            return None

    def test_wide_roots_named(self):
        home = self.home
        self.assertEqual(tools_mod.wide_root(Path('/'), home), 'the whole filesystem')
        self.assertEqual(tools_mod.wide_root(home, home), 'the home directory')
        self.assertEqual(tools_mod.wide_root(home.parent, home), 'a directory the home directory sits under')
        self.assertIsNone(tools_mod.wide_root(home / 'work', home))
        self.assertIsNone(tools_mod.wide_root(Path('/usr/share'), home))

    def test_recursive_search_from_home_is_refused_with_a_narrower_path(self):
        for command in ('grep -rn needle .', 'grep -r needle', 'rg needle', 'find . -name "*.py"',
                        'ls -laR', 'du -sh', 'fd needle', 'tree'):
            message = self.refuse(command)
            self.assertIsNotNone(message, command)
            self.assertIn('the home directory', message)
            self.assertIn(str(self.home / '<subdirectory>'), message)
            self.assertIn('cost limit, not a permission one', message)

    def test_filesystem_root_and_above_the_home_are_refused(self):
        self.assertIn('the whole filesystem', self.refuse('grep -r needle /'))
        self.assertIn('the whole filesystem', self.refuse('du -sh /*'))
        self.assertIn('the whole filesystem', self.refuse('find / -name relay'))
        self.assertIn('sits under', self.refuse(f'rg needle {self.home.parent}'))
        self.assertIn('the home directory', self.refuse('grep -r needle ~'))
        self.assertIn('the home directory', self.refuse('grep -r needle "$HOME"'))

    def test_a_narrower_path_runs(self):
        self.assertIsNone(self.refuse('grep -rn needle work'))
        self.assertIsNone(self.refuse('rg needle work/repo'))
        self.assertIsNone(self.refuse(f'find {self.home / "work"} -name "*.py"'))
        self.assertIsNone(self.refuse('du -sh work'))

    def test_non_recursive_commands_and_listing_stay_allowed(self):
        self.assertIsNone(self.refuse('grep needle notes.txt'))
        self.assertIsNone(self.refuse('ls -la'))
        self.assertIsNone(self.refuse('git status'))
        with patch.dict(os.environ, {'HOME': str(self.home)}):
            tools = self.executor(self.home)
            result = tools.execute(tools.prepare('list_directory', {'path': '.'}))
        self.assertEqual([entry['name'] for entry in result['entries']], ['work'])

    def test_a_walk_anywhere_in_the_line_is_caught(self):
        self.assertIsNotNone(self.refuse('cd work && echo hi; grep -r needle ~'))
        self.assertIsNotNone(self.refuse('find / -name x | head -5'))
        self.assertIsNotNone(self.refuse('sudo du -sh /'))

    def test_a_pane_below_the_home_is_untouched(self):
        cwd = self.home / 'work' / 'repo'
        self.assertIsNone(self.refuse('grep -rn needle .', cwd=cwd))
        self.assertIsNone(self.refuse('rg needle', cwd=cwd))
        # Even from a narrow pane, naming a wide root is still refused.
        self.assertIsNotNone(self.refuse('grep -r needle ~', cwd=cwd))
        self.assertIn(str(cwd / '<subdirectory>'), self.refuse('rg needle /', cwd=cwd))

    def test_the_board_search_tool_shares_the_guard(self):
        with patch.dict(os.environ, {'HOME': str(self.home)}):
            with self.assertRaises(board_tools.BoardToolError) as caught:
                board_tools.search_workspace(self.home, {'pattern': 'needle'})
            self.assertIn('cost limit', str(caught.exception))
            found = board_tools.search_workspace(self.home, {'pattern': 'needle', 'path': 'work'})
        self.assertEqual(found['matches'], [])
