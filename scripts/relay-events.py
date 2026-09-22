#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tabulate retained worker logs locally; no prompts or tool output are exported.

Usage: python3 scripts/relay-events.py [--since 2026-09-22] [--json]
Counts describe recorded results, not necessarily product bugs: refusals and failed
shell commands also count as unsuccessful tools. Rotation limits historical coverage.
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shlex

LINE = re.compile(r'^(\S+) (\w+) (relay\.\S+) pane=(\S+) (\w+)(?: (.*))?$')


def timestamp(value):
    value = datetime.fromisoformat(value.replace('Z', '+00:00'))
    return value.replace(tzinfo=timezone.utc) if value.tzinfo is None else value


def summarize(paths, since=None):
    events = Counter()
    outcomes = Counter()
    tools = defaultdict(lambda: {'calls': 0, 'failed': 0, 'duration_samples': 0, 'total_ms': 0})
    first = last = None
    skipped = 0
    for path in paths:
        with path.open(encoding='utf-8', errors='replace') as stream:
            for line in stream:
                match = LINE.match(line.rstrip('\n'))
                if not match:
                    skipped += 1
                    continue
                date, level, logger, pane, event, fields = match.groups()
                try:
                    when = timestamp(date)
                    values = dict(token.split('=', 1) for token in shlex.split(fields or '') if '=' in token)
                except ValueError:
                    skipped += 1
                    continue
                if since and when < since:
                    continue
                first = min(first, when) if first else when
                last = max(last, when) if last else when
                events[(logger, event, level)] += 1
                if logger == 'relay.agent' and event == 'turn_end':
                    outcomes[values.get('outcome', 'unknown')] += 1
                if logger != 'relay.agent' or event != 'tool':
                    continue
                row = tools[values.get('tool', 'unknown')]
                row['calls'] += 1
                row['failed'] += values.get('ok') == 'False'
                try:
                    ms = int(values['ms'])
                    if ms >= 0:
                        row['total_ms'] += ms
                        row['duration_samples'] += 1
                except (KeyError, ValueError):
                    pass
    return {
        'files': [str(p) for p in paths],
        'first': first.isoformat() if first else None,
        'last': last.isoformat() if last else None,
        'skipped_lines': skipped,
        'tools': [{'tool': name, **row,
                   'failure_pct': round(100 * row['failed'] / row['calls'], 2),
                   'mean_ms': round(row['total_ms'] / row['duration_samples'], 1)
                   if row['duration_samples'] else None}
                  for name, row in sorted(tools.items(), key=lambda item: (-item[1]['failed'], item[0]))],
        'turn_outcomes': dict(sorted(outcomes.items())),
        'events': [{'logger': key[0], 'event': key[1], 'level': key[2], 'count': count}
                   for key, count in sorted(events.items(), key=lambda item: (-item[1], item[0]))],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log-dir', type=Path, default=Path(os.environ.get('XDG_DATA_HOME') or
                        Path.home() / '.local/share') / 'relay/logs')
    parser.add_argument('--since', type=timestamp, help='ISO timestamp or date (UTC if no offset)')
    parser.add_argument('--json', action='store_true', help='machine-readable aggregate report')
    args = parser.parse_args()
    paths = sorted(p for p in args.log_dir.glob('worker.log*')
                   if re.fullmatch(r'worker\.log(?:\.\d+)?', p.name) and p.is_file())
    if not paths:
        parser.error(f'No worker logs found in {args.log_dir}')
    report = summarize(paths, args.since)
    if args.json:
        print(json.dumps(report, indent=2))
        return
    print(f"Retained records: {report['first']} to {report['last']} ({len(paths)} files)")
    print('Failures include refusals and unsuccessful commands; these are not a bug count.')
    print('\nTool                                      Calls  Failed  Fail %   Mean ms')
    for row in report['tools']:
        mean = '-' if row['mean_ms'] is None else str(row['mean_ms'])
        print(f"{row['tool']:40} {row['calls']:6} {row['failed']:7} {row['failure_pct']:7.2f} {mean:>9}")
    print('\nTurn outcomes: ' + json.dumps(report['turn_outcomes'], sort_keys=True))
    print('\nCount  Level  Logger / event')
    for row in report['events']:
        print(f"{row['count']:5}  {row['level']:5}  {row['logger']} / {row['event']}")
    print(f"\nSkipped non-record/malformed lines: {report['skipped_lines']}")


if __name__ == '__main__':
    main()
