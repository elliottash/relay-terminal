#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Local diagnostic summaries and daily snapshots. See docs/DEBUG-HYGIENE.md.

No prompts, command arguments, tool output or free-text errors are exported.
Historical records without outcome/origin remain unknown. These are observations,
not bug counts; rotated/live logs cannot provide a complete or atomic history.
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
import json
import math
import os
from pathlib import Path
import re
import shlex
import sys
import tempfile

LINE = re.compile(r'^(\S+) (\w+) (relay\.\S+) pane=(\S+) (\w+)(?: (.*))?$')
OUTCOMES = {'success', 'pending', 'refused', 'command_nonzero', 'timed_out',
            'transport_error', 'internal_error', 'unknown'}
COMPLETED = OUTCOMES - {'pending', 'refused', 'unknown'}
ORIGINS = {'interactive', 'test', 'qa', 'unknown'}
SNAPSHOT_KIND = 'relay-diagnostic-summary-v1'
SNAPSHOT_NAME = re.compile(r'relay-events-\d{8}T\d{12}Z\.json')


def timestamp(value):
    value = datetime.fromisoformat(value.replace('Z', '+00:00'))
    return value.replace(tzinfo=timezone.utc) if value.tzinfo is None else value


def percentile(values, pct):
    return sorted(values)[max(0, math.ceil(len(values) * pct / 100) - 1)] if values else None


def safe_code(value):
    # Deliberately do not copy free-text exception messages into aggregate files.
    return value if isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9_.:-]{1,80}', value) else 'unknown'


def summarize(paths, since=None, origin=None):
    paths = list(paths)
    events, turns, origins, reasons, classifications = (Counter() for _ in range(5))
    tools = defaultdict(lambda: {'calls': 0, 'failed': 0, 'duration_samples': 0, 'total_ms': 0,
                                 'outcomes': Counter(), 'origins': Counter(), '_ms': []})
    sessions, turn_ids = set(), set()
    first = last = None
    skipped = undated = filtered = 0
    retry_seconds = 0.0
    retry_events = 0
    for path in paths:
        with path.open(encoding='utf-8', errors='replace') as stream:
            for line in stream:
                # Apply the date window before counting dated parse failures.
                try:
                    when = timestamp(line.split(maxsplit=1)[0])
                except (ValueError, IndexError):
                    undated += 1
                    continue
                if since and when < since:
                    continue
                match = LINE.match(line.rstrip('\n'))
                if not match:
                    skipped += 1
                    continue
                date, level, logger, pane, event, fields = match.groups()
                try:
                    values = dict(token.split('=', 1) for token in shlex.split(fields or '') if '=' in token)
                except ValueError:
                    skipped += 1
                    continue
                source = values.get('origin', 'unknown')
                source = source if source in ORIGINS else 'unknown'
                if origin and source != origin:
                    filtered += 1
                    continue
                first = min(first, when) if first else when
                last = max(last, when) if last else when
                events[(logger, event, level)] += 1
                origins[source] += 1
                if event == 'provider_http_retry':
                    retry_events += 1
                    try:
                        wait = float(values.get('wait_s', '0'))
                        if math.isfinite(wait) and wait >= 0:
                            retry_seconds += wait
                    except ValueError:
                        pass
                if logger == 'relay.agent' and event == 'turn_end':
                    turns[values.get('outcome', 'unknown')] += 1
                if logger != 'relay.agent' or event != 'tool':
                    continue
                row = tools[values.get('tool', 'unknown')]
                row['calls'] += 1
                row['failed'] += values.get('ok') == 'False'
                outcome = values.get('outcome', 'unknown')
                outcome = outcome if outcome in OUTCOMES else 'unknown'
                row['outcomes'][outcome] += 1
                row['origins'][source] += 1
                classifications[outcome] += 1
                if outcome in {'transport_error', 'internal_error', 'command_nonzero', 'timed_out'}:
                    reasons[(outcome, safe_code(values.get('error_code')))] += 1
                    if values.get('session'):
                        sessions.add(values['session'])
                        if values.get('turn'):
                            turn_ids.add((values['session'], values['turn']))
                try:
                    ms = int(values['ms'])
                    if ms >= 0:
                        row['total_ms'] += ms
                        row['duration_samples'] += 1
                        row['_ms'].append(ms)
                except (KeyError, ValueError):
                    pass
    result_tools = []
    for name, row in sorted(tools.items(), key=lambda item: (-item[1]['failed'], item[0])):
        samples = row.pop('_ms')
        completed = sum(row['outcomes'][o] for o in COMPLETED)
        unexpected = sum(row['outcomes'][o] for o in ('transport_error', 'internal_error'))
        result_tools.append({'tool': name, **row, 'completed_classified': completed,
                             'unexpected_errors': unexpected,
                             'unexpected_per_100_completed': round(100 * unexpected / completed, 2) if completed else None,
                             'failure_pct': round(100 * row['failed'] / row['calls'], 2),
                             'mean_ms': round(row['total_ms'] / len(samples), 1) if samples else None,
                             'p50_ms': percentile(samples, 50), 'p95_ms': percentile(samples, 95)})
    return {
        'kind': SNAPSHOT_KIND, 'generated_at': datetime.now(timezone.utc).isoformat(),
        'since': since.isoformat() if since else None, 'origin_filter': origin,
        'files': [str(p) for p in paths],
        'first': first.isoformat() if first else None, 'last': last.isoformat() if last else None,
        'skipped_lines': skipped, 'undated_lines_all_files': undated, 'origin_filtered_records': filtered,
        'coverage': 'Retained live logs only; rotation may truncate this window. Undated lines cannot be time-filtered.',
        'tools': result_tools, 'outcomes': dict(classifications), 'origins': dict(origins),
        'failure_reasons': [{'outcome': key[0], 'error_code': key[1], 'count': n}
                            for key, n in sorted(reasons.items())],
        'affected_sessions': len(sessions), 'affected_turns': len(turn_ids),
        'retry_events': retry_events, 'retry_wait_seconds': round(retry_seconds, 3),
        'turn_outcomes': dict(sorted(turns.items())),
        'events': [{'logger': key[0], 'event': key[1], 'level': key[2], 'count': n}
                   for key, n in sorted(events.items(), key=lambda item: (-item[1], item[0]))],
    }


def signal_summary(board):
    """Read-only, explicit signal→card links. Never infer links from error text."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
    from relay_core import signals, test_history
    history = test_history.read(board / '.private/tests/history.jsonl')
    actions = signals.read_events(board / '.private/signals/events.jsonl')
    now = datetime.now(timezone.utc)
    rows = []
    for signal in signals.fold(history, actions).values():
        if signal.state != 'open':
            continue
        try:
            age = max(0, (now - timestamp(signal.first_seen)).total_seconds() / 86400)
        except ValueError:
            age = None
        rows.append({'key': signal.key, 'kind': signal.kind, 'owner': signal.session or None,
                     'card': signal.card or None, 'age_days': round(age, 2) if age is not None else None,
                     'last_seen': signal.last_seen, 'failures': signal.count})
    return sorted(rows, key=lambda r: -(r['age_days'] or 0))


def save_snapshot(report, directory, retention_days=30, now=None):
    now = now or datetime.now(timezone.utc)
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    name = f'relay-events-{now:%Y%m%dT%H%M%S%fZ}.json'
    fd, temporary = tempfile.mkstemp(prefix='.relay-events-', dir=directory)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(report, stream, indent=2)
            stream.write('\n')
        target = directory / name
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    # Only prune this tool's valid snapshots; never arbitrary files in the directory.
    for old in directory.iterdir():
        if not SNAPSHOT_NAME.fullmatch(old.name) or old.is_symlink() or old == target:
            continue
        try:
            data = json.loads(old.read_text())
            created = timestamp(data['generated_at'])
            if data.get('kind') == SNAPSHOT_KIND and created < now - timedelta(days=retention_days):
                old.unlink()
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return target


def review_snapshots(directory, days=7, now=None):
    now = now or datetime.now(timezone.utc)
    latest = {}
    for path in directory.glob('relay-events-*.json'):
        try:
            data = json.loads(path.read_text())
            created = timestamp(data['generated_at'])
            if data.get('kind') != SNAPSHOT_KIND or not now - timedelta(days=days) <= created <= now:
                continue
            # Keep filters separate; never silently substitute a test-only snapshot for an interactive one.
            key = (created.astimezone(timezone.utc).date().isoformat(), data.get('origin_filter') or 'all')
            if key not in latest or created > latest[key][0]:
                latest[key] = created, path, data
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return [{'day': key[0], 'origin_filter': key[1], 'snapshot': str(path),
             'first': data.get('first'), 'last': data.get('last'), 'outcomes': data.get('outcomes', {}),
             'retry_wait_seconds': data.get('retry_wait_seconds', 0), 'signals': data.get('signals', [])}
            for key, (_, path, data) in sorted(latest.items())]


def main():
    data_home = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / 'relay'
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log-dir', type=Path, default=data_home / 'logs')
    parser.add_argument('--since', type=timestamp, help='ISO timestamp or date (UTC if no offset)')
    parser.add_argument('--origin', choices=sorted(ORIGINS))
    parser.add_argument('--board', type=Path, help='board directory, to include open signals and explicit card links')
    parser.add_argument('--json', action='store_true', help='machine-readable aggregate report')
    parser.add_argument('--snapshot', nargs='?', const=data_home / 'diagnostics', type=Path,
                        help='save a private aggregate snapshot (default local Relay diagnostics directory)')
    parser.add_argument('--retention-days', type=int, default=30)
    parser.add_argument('--review', nargs='?', const=data_home / 'diagnostics', type=Path,
                        help='review latest snapshot per UTC day/filter; overlapping windows are never summed')
    parser.add_argument('--days', type=int, default=7)
    args = parser.parse_args()
    if args.days < 1 or args.retention_days < 1:
        parser.error('days and retention-days must be positive')
    if args.review:
        if args.snapshot or args.since or args.origin or args.board:
            parser.error('--review cannot be combined with snapshot/window/board options')
        rows = review_snapshots(args.review, args.days)
        print(json.dumps({'note': 'Latest per UTC day/filter; overlapping windows are not additive.', 'snapshots': rows}, indent=2))
        return
    paths = sorted(p for p in args.log_dir.glob('worker.log*')
                   if re.fullmatch(r'worker\.log(?:\.\d+)?', p.name) and p.is_file())
    if not paths:
        parser.error(f'No worker logs found in {args.log_dir}')
    report = summarize(paths, args.since, args.origin)
    if args.board:
        if not (args.board / 'board.yaml').is_file():
            parser.error('--board must name a directory containing board.yaml')
        report['signals'] = signal_summary(args.board)
    if args.snapshot:
        print(f'Snapshot: {save_snapshot(report, args.snapshot, args.retention_days)}', file=sys.stderr)
    if args.json:
        print(json.dumps(report, indent=2))
        return
    print(f"Retained records: {report['first']} to {report['last']} ({len(paths)} files)")
    print(report['coverage'])
    print('Legacy failed counts include expected outcomes; only explicit outcomes classify new records.')
    print('\nTool                                      Calls  Legacy-failed  Completed  Unexpected/100  p50 ms  p95 ms')
    for row in report['tools']:
        print(f"{row['tool']:40} {row['calls']:6} {row['failed']:14} {row['completed_classified']:10} "
              f"{str(row['unexpected_per_100_completed']):>15} {str(row['p50_ms']):>7} {str(row['p95_ms']):>7}")
    for key in ('outcomes', 'origins', 'turn_outcomes', 'failure_reasons'):
        print(f'\n{key}: ' + json.dumps(report[key], sort_keys=True))
    print(f"\nRetries: {report['retry_events']}; requested wait: {report['retry_wait_seconds']} s")
    print(f"Affected sessions/turns (classified failures): {report['affected_sessions']}/{report['affected_turns']}")
    print('\nCount  Level  Logger / event')
    for row in report['events']:
        print(f"{row['count']:5}  {row['level']:5}  {row['logger']} / {row['event']}")
    if 'signals' in report:
        print('\nOpen signals: ' + json.dumps(report['signals'], indent=2))
    print(f"\nDated malformed lines in window: {report['skipped_lines']}; undated in all files: {report['undated_lines_all_files']}")


if __name__ == '__main__':
    main()
