#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Switchboard command line: check the card format, regenerate the index, migrate.

For collaborators without the Relay GUI, for CI and for pre-commit hooks.
Never calls a model and never uses the network.

  relay-board.py check [--fix] [--json] [--strict]
  relay-board.py index [--stdout] [--private]
  relay-board.py migrate [--apply]
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from relay_core import board as board_mod


def default_issues_dir() -> Path:
    here = Path.cwd()
    try:
        top = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True,
                             text=True, timeout=10)
        if top.returncode == 0 and top.stdout.strip():
            candidate = Path(top.stdout.strip()) / 'issues'
            if candidate.is_dir():
                return candidate
    except (OSError, subprocess.SubprocessError):
        pass
    for folder in [here, *here.parents]:
        if (folder / 'issues').is_dir():
            return folder / 'issues'
    return here / 'issues'


def cmd_check(args, board: board_mod.Board) -> int:
    problems = board.check(fix=args.fix)
    if args.json:
        print(json.dumps([p.__dict__ for p in problems], indent=2))
    else:
        for problem in problems:
            print(problem)
        errors = sum(1 for p in problems if p.severity == 'error')
        warnings = len(problems) - errors
        cards = len(board.card_paths())
        print(f"{cards} card(s) checked: {errors} error(s), {warnings} warning(s)"
              + (" (fixable ones were fixed)" if args.fix else ""))
    if any(p.severity == 'error' for p in problems):
        return 1
    return 1 if args.strict and problems else 0


def cmd_index(args, board: board_mod.Board) -> int:
    text = board.index_markdown(include_private=args.private)
    if args.stdout:
        sys.stdout.write(text)
        return 0
    path = board.root / board_mod.BOARD_INDEX
    before = path.read_text(encoding='utf-8') if path.exists() else None
    board.write_index(include_private=args.private)
    print(f"{'unchanged' if before == text else 'wrote'} {path}")
    return 0


def cmd_migrate(args, board: board_mod.Board) -> int:
    report = board_mod.migrate(board.root, apply=args.apply, repo=board.repo)
    for item in report.migrations:
        if item.action == 'convert':
            print(f"  {item.path}: id {item.card_id}, status {item.status}, rank {item.rank}"
                  + (f"  [{item.note}]" if item.note else ""))
        else:
            print(f"  {item.path}: already a card ({item.card_id or 'no id'})")
    for item in report.skipped:
        print(f"  SKIP {item.path}: {item.note}")
    print(report.summary())
    if not args.apply:
        print("dry run: nothing was written (pass --apply)")
    return 1 if report.skipped else 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--issues', type=Path, default=None,
                        help='the issues/ directory (default: the git checkout it is run in)')
    sub = parser.add_subparsers(dest='command', required=True)

    check = sub.add_parser('check', help='verify the card, task and thread format')
    check.add_argument('--fix', action='store_true', help='apply the fixable repairs')
    check.add_argument('--json', action='store_true')
    check.add_argument('--strict', action='store_true', help='warnings fail too')
    check.set_defaults(func=cmd_check)

    index = sub.add_parser('index', help='regenerate issues/BOARD.md')
    index.add_argument('--stdout', action='store_true', help='print instead of writing')
    index.add_argument('--private', action='store_true', help='include private cards')
    index.set_defaults(func=cmd_index)

    migrate = sub.add_parser('migrate', help='convert a pre-board issues/ tree to cards')
    migrate.add_argument('--apply', action='store_true', help='write the changes (default: dry run)')
    migrate.set_defaults(func=cmd_migrate)

    args = parser.parse_args(argv)
    issues = args.issues or default_issues_dir()
    if not issues.is_dir():
        print(f"no issues directory at {issues}", file=sys.stderr)
        return 2
    board = board_mod.Board(issues)
    try:
        return args.func(args, board)
    except board_mod.BoardError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
