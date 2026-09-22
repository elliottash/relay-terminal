"""Collection incidents are scopes, never runnable unittest loader wrappers (#HG26)."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'backend'))
from relay_core import test_history as H, signals as S
from relay_core.tests_protocol import TestsCommands


class CollectionRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'tests').mkdir()
        (self.root / 'tests/__init__.py').touch()
        self.module = self.root / 'tests/test_board_protocol.py'
        self.key = H.COLLECTION_PREFIX + 'tests.test_board_protocol'

    def run_scope(self, run, *names):
        output = self.root / 'junit.xml'
        done = subprocess.run([sys.executable, '-m', 'relay_core.junit_runner',
                               '--junit', str(output), '--root', str(self.root), '-q', *names],
                              cwd=self.root, capture_output=True, text=True, timeout=20,
                              env=dict(os.environ, PYTHONPATH=str(REPO / 'backend'),
                                       PYTHONDONTWRITEBYTECODE='1'))
        self.assertIn(done.returncode, (0, 1), done.stderr)
        return H.ingest_junit(output, run_id=run, ts=f'2026-09-22T12:00:{int(run):02d}Z')

    def valid(self):
        self.module.write_text('import unittest\nclass T(unittest.TestCase):\n'
                               '    def test_a(self): pass\n    def test_b(self): pass\n')

    def test_failed_import_dedup_runnable_recovery_and_subset_guard(self):
        self.module.write_text('import fake_cards\n')
        names = ['tests.test_board_protocol.T.test_a'] * 11
        first = self.run_scope('1', *names)
        self.assertEqual([r.id for r in first], [self.key])
        self.assertEqual(S.fold(first)[self.key].state, 'pending')
        retry = S.rerun_keys([r.id for r in first], first)
        self.assertEqual(retry, [self.key])
        self.assertEqual(TestsCommands._partition(retry),
                         ([], ['unittest:tests.test_board_protocol'], []))
        rows = first + self.run_scope('2', 'tests.test_board_protocol')
        self.assertEqual(S.fold(rows)[self.key].state, 'open')
        self.valid()
        rows += self.run_scope('3', 'tests.test_board_protocol.T.test_a')
        rows += self.run_scope('4', 'tests.test_board_protocol.T.test_a')
        self.assertEqual(S.fold(rows)[self.key].green_streak, 0)
        rows += self.run_scope('5', 'tests.test_board_protocol')
        self.assertEqual(S.fold(rows)[self.key].state, 'open')
        self.module.write_text('import unittest\nclass T(unittest.TestCase):\n'
                               '    def test_a(self): self.fail("real failure")\n')
        rows += self.run_scope('6', 'tests.test_board_protocol')
        self.assertEqual(S.fold(rows)[self.key].green_streak, 0)
        self.valid()
        rows += self.run_scope('7', 'tests.test_board_protocol')
        self.assertEqual(S.fold(rows)[self.key].state, 'open')
        rows += self.run_scope('8', 'tests.test_board_protocol')
        self.assertEqual(S.fold(rows)[self.key].state, 'resolved')

    def test_historical_wrapper_and_container_keep_claims_until_scope_passes(self):
        old = H.LEGACY_COLLECTION_PREFIX + 'test_board_protocol'
        rows = [H.Execution(ts='2026-09-22T12:00:01Z', id=old, result='error',
                            runner='unittest', run_id='original', message="No module named 'fake_cards'")
                for _ in range(11)]
        rows += [H.Execution(ts='2026-09-22T12:00:02Z', id=old, result='error',
                             runner='unittest', run_id='rerun', message='AttributeError')]
        events = [dict(ts='2026-09-22T12:00:03Z', action='claim', key=key, session='codex-hq')
                  for key in (old, 'run:unittest')]
        state = S.fold(rows, events)
        self.assertEqual(state[old].count, 2)
        for key in (old, 'run:unittest'):
            self.assertEqual(state[key].state, 'open')
            self.assertEqual(state[key].session, 'codex-hq')
        self.valid()
        for run in ('4', '5'):
            rows += self.run_scope(run, 'tests.test_board_protocol.T.test_a')
        for key in (old, 'run:unittest'):
            self.assertEqual(S.fold(rows, events)[key].green_streak, 0)
        rows += self.run_scope('6', 'tests.test_board_protocol')
        for key in (old, 'run:unittest'):
            self.assertEqual(S.fold(rows, events)[key].state, 'open')
        self.module.write_text('import unittest\nclass T(unittest.TestCase):\n'
                               '    def test_a(self): self.fail("still broken")\n')
        rows += self.run_scope('7', 'tests.test_board_protocol')
        for key in (old, 'run:unittest'):
            self.assertEqual(S.fold(rows, events)[key].green_streak, 0)
        self.valid()
        rows += self.run_scope('8', 'tests.test_board_protocol')
        rows += self.run_scope('9', 'tests.test_board_protocol')
        for key in (old, 'run:unittest'):
            self.assertEqual(S.fold(rows, events)[key].state, 'resolved')
        self.assertEqual(TestsCommands._partition([old])[1], ['unittest:tests.test_board_protocol'])

    def test_collection_failure_resets_passing_streak_without_flaky_promotion(self):
        rows = [H.Execution(ts=f'2026-09-22T12:00:{i:02d}Z', id=self.key,
                            result=result, run_id=str(i), commit='same')
                for i, result in enumerate(('error', 'error', 'pass', 'error', 'pass', 'pass'))]
        state = S.fold(rows)
        self.assertEqual(state[self.key].kind, 'broken')
        self.assertEqual(state[self.key].state, 'resolved')
