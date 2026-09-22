# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import json
from datetime import datetime, timedelta, timezone
from unittest.mock import patch
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('event_report', Path(__file__).resolve().parents[1] / 'scripts/relay-events.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class EventReportTests(unittest.TestCase):
    def test_rotations_filtering_failures_and_malformed_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / name for name in ('worker.log', 'worker.log.1')]
            paths[0].write_text('''2026-09-22T12:00:00Z INFO relay.agent pane=a tool tool=run_command ok=False ms=20
2026-09-22T12:01:00Z ERROR relay.agent pane=a turn_end outcome=error error="a quoted error"
2026-09-22T12:02:00Z INFO relay.agent pane=a tool tool=read_file ok=True
2026-09-22T12:03:00Z DEBUG relay.worker pane=a request type=ping
traceback continuation
2026-09-22T12:04:00Z INFO relay.agent pane=a tool tool="broken
''')
            paths[1].write_text('''2026-09-21T12:00:00Z INFO relay.agent pane=b tool tool=run_command ok=False ms=99
2026-09-22T11:00:00Z INFO relay.agent pane=b tool tool=run_command ok=True ms=10
''')
            data = report.summarize(paths, report.timestamp('2026-09-22'))
            row = data['tools'][0]
            self.assertEqual((row['calls'], row['failed'], row['mean_ms'], row['failure_pct']), (2, 1, 15, 50))
            self.assertIsNone(data['tools'][1]['mean_ms'])
            self.assertEqual(data['turn_outcomes'], {'error': 1})
            self.assertEqual(data['skipped_lines'], 1)
            self.assertEqual(data['undated_lines_all_files'], 1)
            self.assertEqual(data['outcomes'], {'unknown': 3})
            self.assertEqual(data['first'], '2026-09-22T11:00:00+00:00')
            self.assertEqual(sum(x['count'] for x in data['events']), 5)

    def test_empty(self):
        self.assertEqual(report.summarize([])['tools'], [])
        self.assertIsNone(report.summarize([])['first'])

    def test_outcomes_denominator_origin_and_safe_reasons(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'worker.log'
            lines = []
            outcomes = ['success', 'pending', 'refused', 'command_nonzero', 'timed_out',
                        'transport_error', 'internal_error', 'future_outcome']
            for i, outcome in enumerate(outcomes):
                lines.append(f'2026-09-22T12:00:00Z INFO relay.agent pane=a tool tool=run_command '
                             f'origin=interactive outcome={outcome} session=s turn=t ms={i * 10} '
                             'error_code="secret error body"')
            lines.append('2026-09-22T12:00:00Z INFO relay.agent pane=a tool tool=run_command '
                         'origin=test outcome=internal_error ms=999 error_code=E_TEST')
            lines.append('2026-09-22T12:00:00Z INFO relay.provider pane=a provider_http_retry '
                         'origin=interactive wait_s=0.5')
            lines.append('2026-09-21T12:00:00Z INFO malformed')
            path.write_text('\n'.join(lines))
            data = report.summarize([path], report.timestamp('2026-09-22'), 'interactive')
            row = data['tools'][0]
            self.assertEqual(row['completed_classified'], 5)
            self.assertEqual(row['unexpected_per_100_completed'], 40)
            self.assertEqual(row['outcomes']['unknown'], 1)
            self.assertEqual((row['p50_ms'], row['p95_ms']), (30, 70))
            self.assertEqual(data['origin_filtered_records'], 1)
            self.assertEqual(data['skipped_lines'], 0)
            self.assertEqual(data['affected_sessions'], 1)
            self.assertEqual(data['affected_turns'], 1)
            self.assertEqual(data['retry_wait_seconds'], 0.5)
            self.assertNotIn('secret error body', json.dumps(data))

    def test_snapshot_retention_and_review_never_sum_overlapping_windows(self):
        now = datetime(2026, 9, 22, 12, tzinfo=timezone.utc)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            unrelated = root / 'notes.json'
            unrelated.write_text('{}')
            data = report.summarize([])
            data['generated_at'] = (now - timedelta(days=31)).isoformat()
            old = report.save_snapshot(data, root, now=now - timedelta(days=31))
            data['generated_at'] = (now - timedelta(hours=1)).isoformat()
            first = report.save_snapshot(data, root, now=now - timedelta(hours=1))
            data['generated_at'] = now.isoformat()
            newest = report.save_snapshot(data, root, now=now)
            self.assertFalse(old.exists())
            self.assertTrue(first.exists())
            self.assertTrue(unrelated.exists())
            self.assertEqual(newest.stat().st_mode & 0o777, 0o600)
            rows = report.review_snapshots(root, now=now)
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]['snapshot'], str(newest))
            data['origin_filter'] = 'test'
            data['generated_at'] = (now + timedelta(seconds=1)).isoformat()
            report.save_snapshot(data, root, now=now + timedelta(seconds=1))
            self.assertEqual(len(report.review_snapshots(root, now=now + timedelta(seconds=1))), 2)

    def test_signal_report_uses_explicit_links_and_excludes_message(self):
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
        from relay_core import signals
        signal = SimpleNamespace(state='open', first_seen='2026-09-20T00:00:00Z',
                                 key='collection:unittest:tests.example', kind='broken',
                                 session='owner', card='AB12', last_seen='2026-09-21T00:00:00Z', count=2,
                                 message='private error output')
        with tempfile.TemporaryDirectory() as directory, patch.object(signals, 'fold', return_value={'key': signal}):
            rows = report.signal_summary(Path(directory))
        self.assertEqual(rows[0]['card'], 'AB12')
        self.assertEqual(rows[0]['owner'], 'owner')
        self.assertNotIn('private error output', json.dumps(rows))


if __name__ == '__main__':
    unittest.main()
