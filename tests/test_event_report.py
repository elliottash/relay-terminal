# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import json
import os
import subprocess
import sys
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

    def test_protocol_exceptions_are_grouped_without_untrusted_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'worker.log'
            path.write_text('''2026-09-23T12:00:00Z ERROR relay.worker pane=a protocol_error kind=configure error=AttributeError msg="private prompt"
2026-09-23T12:00:01Z ERROR relay.worker pane=a protocol_error kind=configure error=ValueError msg="private prompt" origin=qa
2026-09-23T12:00:02Z ERROR relay.worker pane=a protocol_error kind=secret_value error=SecretValue msg="private prompt" origin=interactive
2026-09-23T12:00:03Z INFO relay.agent pane=a tool tool=agent_wait outcome=pending origin=interactive
''')
            data = report.summarize([path])
            self.assertEqual(data['protocol_errors'], [
                {'kind': 'configure', 'exception': 'AttributeError', 'origin': 'unknown', 'count': 1},
                {'kind': 'configure', 'exception': 'ValueError', 'origin': 'qa', 'count': 1},
                {'kind': 'unknown', 'exception': 'unknown', 'origin': 'interactive', 'count': 1},
            ])
            self.assertEqual(data['outcomes'], {'pending': 1})
            self.assertNotIn('private prompt', json.dumps(data))
            self.assertNotIn('secret_value', json.dumps(data))

    def test_qa_launcher_isolates_child_diagnostics(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            normal = Path(directory) / 'normal'
            normal.mkdir()
            env = dict(os.environ, XDG_DATA_HOME=str(normal),
                       PYTHONPATH=str(root / 'backend'))
            code = ("import os; from relay_core import logs; "
                    "log=logs.configure('worker'); "
                    "logs.event(log, 'qa_probe', level_name='info'); "
                    "print(os.environ['RELAY_LOG_ORIGIN'], os.environ['RELAY_LOG_RUN_ID'])")
            result = subprocess.run([str(root / 'scripts/relay-qa-run'), sys.executable, '-c', code],
                                    env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertRegex(result.stdout.strip(), r'^qa qa-\d{8}T\d{6}Z-\d+$')
            self.assertFalse((normal / 'relay/logs/worker.log').exists())

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

    def test_invalid_snapshot_metadata_does_not_interrupt_review_or_retention(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            invalid = [None, 123, [], {}, {'kind': 'foreign', 'generated_at': None}]
            invalid.extend({'kind': report.SNAPSHOT_KIND, 'generated_at': value}
                           for value in (None, 123, [], 'not-a-date'))
            files = []
            for i, data in enumerate(invalid):
                path = root / f'relay-events-20260922T120000{i:06d}Z.json'
                path.write_text(json.dumps(data))
                files.append(path)
            self.assertEqual(report.review_snapshots(root), [])
            self.assertTrue(report.save_snapshot(report.summarize([]), root).exists())
            self.assertTrue(all(path.exists() for path in files))

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
