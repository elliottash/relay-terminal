# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
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
            self.assertEqual(data['skipped_lines'], 2)
            self.assertEqual(data['first'], '2026-09-22T11:00:00+00:00')
            self.assertEqual(sum(x['count'] for x in data['events']), 5)

    def test_empty(self):
        self.assertEqual(report.summarize([])['tools'], [])
        self.assertIsNone(report.summarize([])['first'])


if __name__ == '__main__':
    unittest.main()
