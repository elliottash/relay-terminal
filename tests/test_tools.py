import hashlib
import json
import os
import subprocess
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
        with patch.dict(os.environ, {'MOONSHOT_API_KEY': 'DO_NOT_LEAK', 'ZAI_API_KEY': 'NO', 'RELAY_UNRELATED': 'NO'}):
            result = self.tools.execute(self.tools.prepare('run_command', {'command': 'env'}))
        self.assertNotIn('DO_NOT_LEAK', result['output'])
        self.assertNotIn('ZAI_API_KEY', result['output'])
        self.assertNotIn('RELAY_UNRELATED', result['output'])

    # Card #WNKN: the two RELAY_ names that are pane identity pass through command_env() where
    # every other RELAY_* (and every secret-looking name) is stripped.
    def test_command_env_relay_identity_passes_through(self):
        with patch.dict(os.environ, {'RELAY_SESSION_TOKEN': 'tok-1', 'RELAY_PANE_ID': 'pane-9',
                                     'RELAY_UNRELATED': 'x', 'ANTHROPIC_API_KEY': 'DO_NOT_LEAK'}):
            env = tools_mod.command_env()
        self.assertEqual('tok-1', env.get('RELAY_SESSION_TOKEN'))
        self.assertEqual('pane-9', env.get('RELAY_PANE_ID'))
        self.assertNotIn('RELAY_UNRELATED', env)
        self.assertNotIn('ANTHROPIC_API_KEY', env)

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


# ----- scripts/land.py authorship, before a pane's first tool write (card #WNKN) -----------------
class LandAuthorshipTests(unittest.TestCase):
    """The auto-begin + authorship-journal hook in ToolExecutor.execute, run against a temp git
    repo with a fake scripts/land.py that records its argv and what the claimed file still
    held. Every failure mode must leave the write itself untouched."""

    TOKEN = "wnkn-tok"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.repo = self.base / "repo"
        (self.repo / "scripts").mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        (self.repo / "file.txt").write_text("old\n")
        self.land = self.base / "land"           # the land root (authors/, blobs/, registry.json)
        self.calls = self.base / "calls.jsonl"   # what the fake scripts/land.py saw
        self.fake_land()
        self.events = []
        self.tools = ToolExecutor(str(self.repo), self.events.append, threading.Event())

    def tearDown(self):
        self.tmp.cleanup()

    def fake_land(self, exit_code=0, sleep=0):
        """A scripts/land.py that records its argv, and whether the claimed file still holds
        FAKE_EXPECT bytes when it runs (the auto-begin must precede the write), then exits."""
        (self.repo / "scripts" / "land.py").write_text(
            "import json, os, sys, time\n"
            "rel = sys.argv[3]\n"
            "content = open(rel, 'rb').read() if os.path.exists(rel) else b''\n"
            "with open(os.environ['FAKE_CALLS'], 'a', encoding='utf-8') as fh:\n"
            "    fh.write(json.dumps({'argv': sys.argv[1:],\n"
            "                         'content_ok': content == os.environ['FAKE_EXPECT'].encode()})\n"
            "             + '\\n')\n"
            f"time.sleep({sleep})\n"
            f"sys.exit({exit_code})\n")

    def write(self, path, content, expected_before):
        """One write_file through the executor, as if this pane (token + pane id) made it."""
        with patch.dict(os.environ, {"RELAY_SESSION_TOKEN": self.TOKEN, "RELAY_PANE_ID": "pane-7",
                                      "RELAY_LAND_ROOT": str(self.land),
                                      "FAKE_CALLS": str(self.calls), "FAKE_EXPECT": expected_before}):
            return self.tools.execute(self.tools.prepare("write_file", {"path": path, "content": content}))

    def calls_read(self):
        return [json.loads(line) for line in self.calls.read_text(encoding="utf-8").splitlines()]

    def journal_read(self):
        journal = self.land / "authors" / f"{self.TOKEN}.jsonl"
        return journal, [json.loads(line) for line in journal.read_text(encoding="utf-8").splitlines()]

    def test_begin_runs_once_per_path_before_the_write(self):
        self.write("file.txt", "new\n", "old\n")
        calls = self.calls_read()
        self.assertEqual(1, len(calls))
        self.assertEqual(["begin", self.TOKEN, "file.txt", "--auto", "--contact", "pane pane-7"],
                         calls[0]["argv"])
        self.assertTrue(calls[0]["content_ok"], "auto-begin must see the pre-write bytes")
        self.assertEqual("new\n", (self.repo / "file.txt").read_text())
        self.write("file.txt", "newer\n", "new\n")   # a second write to the same path
        self.assertEqual(1, len(self.calls_read()), "one auto-begin per path")

    def test_begin_contact_carries_the_claimed_card(self):
        self.tools.authorship_card = "#WNKN"
        self.write("file.txt", "new\n", "old\n")
        self.assertEqual("pane pane-7 #WNKN", self.calls_read()[0]["argv"][-1])

    def test_journal_line_shape_and_blobs(self):
        self.tools.authorship_card = "#WNKN"
        self.tools.provenance = {"turn_id": "turn-42", "model": "test-model"}
        self.write("file.txt", "new\n", "old\n")
        journal, entries = self.journal_read()
        self.assertEqual(1, len(entries))
        self.assertEqual({"ts", "repo", "path", "before", "after", "pane", "token", "card", "turn_id"},
                         set(entries[0]))
        self.assertEqual(str(self.repo.resolve()), entries[0]["repo"])
        self.assertEqual("file.txt", entries[0]["path"])
        self.assertEqual(hashlib.sha256(b"old\n").hexdigest(), entries[0]["before"])
        self.assertEqual(hashlib.sha256(b"new\n").hexdigest(), entries[0]["after"])
        self.assertEqual("pane-7", entries[0]["pane"])
        self.assertEqual(self.TOKEN, entries[0]["token"])
        self.assertEqual("#WNKN", entries[0]["card"])
        self.assertEqual("turn-42", entries[0]["turn_id"])
        self.assertIsInstance(entries[0]["ts"], float)
        self.assertEqual(b"old\n", (self.land / "blobs" / entries[0]["before"]).read_bytes())
        self.assertEqual(b"new\n", (self.land / "blobs" / entries[0]["after"]).read_bytes())
        self.assertEqual(0o700, (self.land / "authors").stat().st_mode & 0o777)
        self.assertEqual(0o700, (self.land / "blobs").stat().st_mode & 0o777)

    def test_journal_of_a_new_file_has_null_before(self):
        self.write("made.txt", "first\n", "")
        journal, entries = self.journal_read()
        self.assertIsNone(entries[0]["before"])
        self.assertEqual(hashlib.sha256(b"first\n").hexdigest(), entries[0]["after"])
        self.assertEqual(b"first\n", (self.land / "blobs" / entries[0]["after"]).read_bytes())
        self.assertFalse((self.land / "blobs" / hashlib.sha256(b"").hexdigest()).exists())

    def test_failing_begin_does_not_block_the_write(self):
        self.fake_land(exit_code=1)
        self.write("file.txt", "new\n", "old\n")
        self.assertEqual("new\n", (self.repo / "file.txt").read_text())
        self.assertEqual(1, len(self.calls_read()))
        _, entries = self.journal_read()            # the journal is independent of the claim
        self.assertEqual(hashlib.sha256(b"new\n").hexdigest(), entries[0]["after"])

    def test_slow_begin_times_out_and_does_not_block_the_write(self):
        self.fake_land(sleep=30)
        started = time.monotonic()
        self.write("file.txt", "new\n", "old\n")
        self.assertEqual("new\n", (self.repo / "file.txt").read_text())
        self.assertEqual(1, len(self.calls_read()))  # it ran, then was killed at the timeout
        self.assertLess(time.monotonic() - started, 20)
        self.assertTrue((self.land / "authors" / f"{self.TOKEN}.jsonl").exists())

    def test_a_registry_claim_already_on_the_path_skips_auto_begin(self):
        (self.land).mkdir(parents=True, exist_ok=True)
        (self.land / "registry.json").write_text(json.dumps(
            {"version": 1, "sessions": {self.TOKEN: {"claims": ["file.txt"]}}}), encoding="utf-8")
        self.write("file.txt", "new\n", "old\n")
        self.assertFalse(self.calls.exists(), "a hand-begun claim is never auto-begun again")
        _, entries = self.journal_read()
        self.assertEqual(1, len(entries))

    def test_no_token_means_no_begin_and_no_journal(self):
        env = {key: value for key, value in os.environ.items() if key != "RELAY_SESSION_TOKEN"}
        with patch.dict(os.environ, env, clear=True):
            self.tools.execute(self.tools.prepare("write_file", {"path": "file.txt", "content": "new\n"}))
        self.assertEqual("new\n", (self.repo / "file.txt").read_text())
        self.assertFalse(self.calls.exists())
        self.assertFalse((self.land / "authors").exists())
        self.assertFalse((self.land / "blobs").exists())

    def test_a_repo_without_scripts_land_is_untouched(self):
        (self.repo / "scripts" / "land.py").unlink()
        self.write("file.txt", "new\n", "old\n")
        self.assertEqual("new\n", (self.repo / "file.txt").read_text())
        self.assertFalse(self.calls.exists())
        self.assertFalse((self.land / "authors").exists())

    def test_the_journal_rotates_past_4mb(self):
        authors = self.land / "authors"
        authors.mkdir(parents=True)
        (authors / f"{self.TOKEN}.jsonl").write_text("x" * (4 * 1024 * 1024 + 1))
        self.write("file.txt", "new\n", "old\n")
        self.assertEqual(4 * 1024 * 1024 + 1, (authors / f"{self.TOKEN}.1.jsonl").stat().st_size)
        journal, entries = self.journal_read()
        self.assertEqual(1, len(entries))


# ----- land_try: build and test the tip plus this session's hunks (card #76QW) ---------------------
class LandTryTests(unittest.TestCase):
    """The land_try tool against a fake scripts/land.py in a temp workspace root: the argv it
    builds (session defaulted from the pane token, flags and paths forwarded), the contract
    lines parsed into fields, exit 5/6 reported as ok=false with the output tail, and the
    refusals when there is no session to name or no scripts/land.py to run."""

    TOKEN = "qw76-tok"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.repo = self.base / "repo"                 # the workspace root, with land.py in it
        (self.repo / "scripts").mkdir(parents=True)
        self.calls = self.base / "calls.jsonl"         # what the fake scripts/land.py saw
        self.events = []
        self.tools = ToolExecutor(str(self.repo), self.events.append, threading.Event())

    def tearDown(self):
        self.tmp.cleanup()

    def fake_land(self, exit_code=0, extra=0, tip_line="tip 0653463cfa42"):
        """A scripts/land.py that records its argv and cwd, prints the contract lines (plus
        `extra` noise lines, as compiler errors or a ctest tail would), and exits exit_code."""
        (self.repo / "scripts" / "land.py").write_text(
            "import json, os, sys\n"
            "with open(os.environ['FAKE_CALLS'], 'a', encoding='utf-8') as fh:\n"
            "    fh.write(json.dumps({'argv': sys.argv[1:], 'cwd': os.getcwd()}) + '\\n')\n"
            f"print({tip_line!r})\n"
            "print('M +12 -0 backend/relay_core/tools.py')\n"
            "slot = os.environ['FAKE_ROOT'] + '/verify-slots/qw76-tool-0'\n"
            "print('src ' + slot + '/src')\n"
            "print('build ' + slot + '/build')\n"
            "print('binary ' + slot + '/build/relay')\n"
            f"for i in range({extra}):\n"
            "    print('error line %d' % i)\n"
            f"sys.exit({exit_code})\n")

    def try_it(self, arguments, exit_code=0, extra=0, tip_line="tip 0653463cfa42"):
        """One land_try through prepare and execute, as if this pane (token TOKEN) called it."""
        self.fake_land(exit_code=exit_code, extra=extra, tip_line=tip_line)
        env = {"RELAY_SESSION_TOKEN": self.TOKEN, "FAKE_CALLS": str(self.calls),
               "FAKE_ROOT": str(self.repo)}
        with patch.dict(os.environ, env):
            return self.tools.execute(self.tools.prepare("land_try", arguments))

    def calls_read(self):
        return [json.loads(line) for line in self.calls.read_text(encoding="utf-8").splitlines()]

    def test_land_try_is_offered(self):
        names = [tool["function"]["name"] for tool in self.tools.tools()]
        self.assertIn("land_try", names)

    def test_land_try_argv_and_parsed_fields_on_a_green_run(self):
        result = self.try_it({"tests": "test_tools", "target": "LandTryTests",
                              "paths": ["backend/relay_core/tools.py", "tests/test_tools.py"],
                              "commit": "0653463cfa42"})
        call = self.calls_read()[0]
        self.assertEqual(["try", self.TOKEN, "--tests", "test_tools", "--target", "LandTryTests",
                          "--commit", "0653463cfa42", "--paths",
                          "backend/relay_core/tools.py", "tests/test_tools.py"], call["argv"])
        self.assertEqual(str(self.repo.resolve()), call["cwd"], "must run in the workspace root")
        self.assertTrue(result["ok"])
        self.assertEqual(0, result["exit_code"])
        self.assertEqual("0653463cfa42", result["tip"])
        self.assertTrue(result["src"].endswith("/verify-slots/qw76-tool-0/src"))
        self.assertTrue(result["build"].endswith("/verify-slots/qw76-tool-0/build"))
        self.assertTrue(result["binary"].endswith("/verify-slots/qw76-tool-0/build/relay"))
        self.assertIn("M +12 -0 backend/relay_core/tools.py", result["output"])

    def test_land_try_session_defaults_to_the_panes_token(self):
        self.try_it({})
        self.assertEqual(["try", self.TOKEN], self.calls_read()[0]["argv"])

    def test_land_try_an_explicit_session_beats_the_token(self):
        self.try_it({"session": "other-session"})
        self.assertEqual(["try", "other-session"], self.calls_read()[0]["argv"])

    def test_land_try_a_commit_line_names_the_tip_field_too(self):
        result = self.try_it({}, tip_line="commit deadbeef99")
        self.assertEqual("deadbeef99", result["tip"])

    def test_land_try_build_failure_is_ok_false_with_the_output_tail(self):
        result = self.try_it({}, exit_code=5, extra=200)   # compiler errors on stdout (contract)
        self.assertFalse(result["ok"])
        self.assertEqual(5, result["exit_code"])
        lines = result["output"].splitlines()
        self.assertEqual(120, len(lines), "the result carries the last ~120 lines")
        self.assertEqual("error line 80", lines[0])
        self.assertEqual("error line 199", lines[-1])
        self.assertEqual("0653463cfa42", result["tip"], "fields parse even when the tail has none")

    def test_land_try_test_failure_is_ok_false(self):
        result = self.try_it({}, exit_code=6, extra=3)     # a ctest tail on stdout (contract)
        self.assertFalse(result["ok"])
        self.assertEqual(6, result["exit_code"])
        self.assertEqual(8, len(result["output"].splitlines()))
        self.assertIn("error line 2", result["output"].splitlines()[-1])

    def test_land_try_no_land_script_is_refused_before_anything_runs(self):
        # The workspace has scripts/ but no land.py: prepare refuses before any job can run.
        self.assertFalse((self.repo / "scripts" / "land.py").exists())
        with patch.dict(os.environ, {"RELAY_SESSION_TOKEN": self.TOKEN}):
            with self.assertRaises(ValueError) as raised:
                self.tools.prepare("land_try", {})
        self.assertIn("scripts/land.py", str(raised.exception))
        self.assertFalse(self.calls.exists())

    def test_land_try_no_session_and_no_pane_token_is_refused(self):
        env = {key: value for key, value in os.environ.items() if key != "RELAY_SESSION_TOKEN"}
        with patch.dict(os.environ, env, clear=True):
            with self.assertRaises(ValueError) as raised:
                self.tools.prepare("land_try", {})
        self.assertIn("session", str(raised.exception))
        self.assertFalse(self.calls.exists())
