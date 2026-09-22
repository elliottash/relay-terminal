"""Independent HG26 edge checks; uses temporary data only."""
import importlib.util
import io
import json
import logging
import os
from pathlib import Path
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'backend'))
spec = importlib.util.spec_from_file_location('report', ROOT / 'scripts/relay-events.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
from relay_core import logs, signals, test_history

class IndependentChecks(unittest.TestCase):
    def test_legacy_and_empty_denominator(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'worker.log'
            p.write_text('2026-09-22T12:00:00Z INFO relay.agent pane=test tool tool=run_command ok=False ms=-1\n'
                         '2026-09-22T12:00:01Z INFO relay.agent pane=test tool tool=run_command origin=qa outcome=pending ms=0\n'
                         '2026-09-22T12:00:02Z INFO relay.agent pane=test tool tool=run_command origin=qa outcome=refused ms=2\n')
            data = r.summarize([p])
            row = data['tools'][0]
            self.assertEqual(data['origins'], {'unknown': 1, 'qa': 2})
            self.assertEqual(row['outcomes'], {'unknown': 1, 'pending': 1, 'refused': 1})
            self.assertEqual(row['completed_classified'], 0)
            self.assertIsNone(row['unexpected_per_100_completed'])
            self.assertEqual(row['duration_samples'], 2)
            self.assertEqual(r.summarize([p], origin='interactive')['tools'], [])
            self.assertEqual(r.summarize([p], origin='unknown')['tools'][0]['calls'], 1)

    def test_real_formatter_and_privacy(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {'XDG_DATA_HOME': tmp, 'RELAY_LOG_ORIGIN': 'qa'}):
            logger = logs.configure(level='info', pane='review')
            try:
                logs.event(logs.get('agent'), 'tool', tool='run_command', outcome='internal_error',
                           ok=False, ms=10, session='session-private', turn='turn-private', error_code='E_RUNTIME',
                           error='DO_NOT_EXPORT secret detail', args='DO_NOT_EXPORT command', output='DO_NOT_EXPORT output')
                logs.event(logs.get('provider'), 'provider_http_retry', wait_s=1.25)
                data = r.summarize([Path(tmp) / 'relay/logs/worker.log'], origin='qa')
                self.assertEqual(data['tools'][0]['unexpected_per_100_completed'], 100)
                self.assertEqual(data['retry_wait_seconds'], 1.25)
                self.assertEqual((data['affected_sessions'], data['affected_turns']), (1, 1))
                encoded = json.dumps(data)
                for private in ['DO_NOT_EXPORT', 'session-private', 'turn-private']:
                    self.assertNotIn(private, encoded)
            finally:
                for h in list(logger.handlers):
                    h.close()
                    logger.removeHandler(h)

    def test_real_signal_store_links_and_read_only(self):
        now = datetime.now(timezone.utc)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            hp = root / '.private/tests/history.jsonl'
            ep = root / '.private/signals/events.jsonl'
            key = 'unittest:tests.review.C.test_one'
            executions = [test_history.Execution(ts=(now - timedelta(minutes=3-i)).isoformat(),
                          id=key, result='fail', runner='unittest', run_id=f'run-{i}',
                          message='PRIVATE_ERROR AB12', excerpt='PRIVATE_OUTPUT') for i in range(2)]
            test_history.append(executions, hp)
            for action in [dict(action='claim', session='owner-review'), dict(action='promote', card='AB12')]:
                signals.append_event(dict(action, key=key, ts=(now - timedelta(seconds=10)).isoformat()), ep)
            before = (hp.read_bytes(), ep.read_bytes())
            rows = r.signal_summary(root)
            self.assertEqual(len(rows), 1)
            self.assertEqual((rows[0]['card'], rows[0]['owner']), ('AB12', 'owner-review'))
            self.assertNotIn('PRIVATE_', json.dumps(rows))
            self.assertEqual(before, (hp.read_bytes(), ep.read_bytes()))

    def test_retention_boundary_unrelated_symlink_and_modes(self):
        now = datetime(2026, 9, 22, 12, tzinfo=timezone.utc)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'snapshots'
            root.mkdir(mode=0o700)
            def put(days, suffix, **extra):
                p = root / f'relay-events-20260801T00000000000{suffix}Z.json'
                p.write_text(json.dumps(dict(kind=r.SNAPSHOT_KIND, generated_at=(now-timedelta(days=days)).isoformat(), **extra)))
                return p
            old = put(31, 1)
            edge = put(30, 2)
            foreign = put(31, 3)
            foreign.write_text(foreign.read_text().replace(r.SNAPSHOT_KIND, 'foreign-kind'))
            broken = put(31, 4)
            broken.write_text('{')
            link = root / 'relay-events-20260801T000000000005Z.json'
            link.symlink_to(foreign)
            data = r.summarize([])
            target = r.save_snapshot(data, root, now=now)
            self.assertFalse(old.exists())
            for p in [edge, foreign, broken, link]: self.assertTrue(p.exists())
            self.assertEqual(target.stat().st_mode & 0o777, 0o600)

    def test_overlapping_windows_filters_and_future(self):
        now = datetime(2026, 9, 22, 12, tzinfo=timezone.utc)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for hours, origin, count in [(25, None, 10), (2, None, 20), (1, None, 30), (0, 'qa', 40), (-1, None, 999)]:
                at = now - timedelta(hours=hours)
                data = dict(r.summarize([]), generated_at=at.isoformat(), origin_filter=origin, outcomes={'success': count})
                r.save_snapshot(data, root, now=at)
            rows = r.review_snapshots(root, now=now)
            self.assertEqual([row['outcomes']['success'] for row in rows], [10, 30, 40])

    def test_malformed_timestamp_does_not_break_review(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'relay-events-20260922T120000000000Z.json'
            p.write_text(json.dumps({'kind': r.SNAPSHOT_KIND, 'generated_at': None}))
            self.assertEqual(r.review_snapshots(Path(tmp)), [])

    def test_malformed_timestamp_does_not_break_retention(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'relay-events-20260922T120000000000Z.json'
            p.write_text(json.dumps({'kind': r.SNAPSHOT_KIND, 'generated_at': 123}))
            self.assertTrue(r.save_snapshot(r.summarize([]), Path(tmp)).exists())
            self.assertTrue(p.exists())

if __name__ == '__main__': unittest.main(verbosity=2)
