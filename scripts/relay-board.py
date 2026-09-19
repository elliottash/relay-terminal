#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Switchboard command line: check the card format, regenerate the index, migrate.

For collaborators without the Relay GUI, for CI and for pre-commit hooks.
Never calls a model and never uses the network.

  relay-board.py check [--fix] [--json] [--strict]
  relay-board.py index [--stdout] [--private]
  relay-board.py migrate [--apply]
  relay-board.py verifier <ID> [--json]
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from relay_core import board as board_mod
from relay_core import qa_verifiers as qa


def default_board_dir() -> Path:
    """The board of the checkout this is run in: `.switchboard/`, else `switchboard/`, else `issues/`.

    `board.BOARD_FOLDERS` is the one list of spellings, in precedence order, and this walks it in
    the same order as the worker, so the command line and Relay agree about which board a project
    has -- including a **hidden** `.switchboard/`, which is what Relay creates since 2026-09-19 and
    which a shell glob or a plain `ls` does not show.

    A folder holding `board.yaml` wins over one that merely has the right name, so a half-made
    `switchboard/` beside a real `.switchboard/` does not shadow the board.
    """
    here = Path.cwd()
    names = board_mod.BOARD_FOLDERS
    roots = [here, *here.parents]
    try:
        top = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True,
                             text=True, timeout=10)
        if top.returncode == 0 and top.stdout.strip():
            roots.insert(0, Path(top.stdout.strip()))
    except (OSError, subprocess.SubprocessError):
        pass
    for folder in roots:                       # a real board first, wherever it is above us
        found = board_mod.board_folder(folder)
        if found is not None:
            return found
    for folder in roots:                       # then a folder with the right name and no board.yaml
        for name in names:
            if (folder / name).is_dir():
                return folder / name
    return here / board_mod.DEFAULT_BOARD_FOLDER


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


def cmd_verifier(args, board: board_mod.Board) -> int:
    """Who should verify a card, from this machine's guests, keys and local endpoints.

    The same function the worker and the card detail use (`relay_core.qa_verifiers.recommend`), so
    a collaborator without the GUI gets the same answer the Switchboard would give.
    """
    card_id = str(args.id).strip().lstrip('#').upper()
    card = board.card_by_id(card_id)
    if card is None:
        print(f"no card #{card_id} on this board", file=sys.stderr)
        return 2
    implementer = str(card.front.get('implemented_by') or '')
    result = qa.recommend_here(implementer)
    result['commits'] = qa.card_commits(board.repo, card_id, card.front.get('links'), implementer)
    if card.front.get('verified_by'):
        result['verified_by'] = str(card.front['verified_by'])
    if args.json:
        print(json.dumps(result, indent=2))
        return 0 if result.get('recommended') else 1
    if not implementer:
        print(f"#{card_id} names no implemented_by, so any verifier is independent.")
    print(qa.summary_line(result, card_id))
    if result.get('note'):
        print(result['note'])
    for commit in result['commits']:
        agrees = 'agrees' if commit['agrees'] else 'DISAGREES' if commit['agrees'] is False else 'no trailer'
        print(f"  commit {commit['hash']}: {commit['trailer'] or '—'} ({agrees})")
    return 0 if result.get('recommended') else 1


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--issues', '--board', dest='issues', type=Path, default=None,
                        help='the board directory: .switchboard/, switchboard/ or issues/ '
                             '(default: the board of the git checkout it is run in)')
    sub = parser.add_subparsers(dest='command', required=True)

    check = sub.add_parser('check', help='verify the card, task and thread format')
    check.add_argument('--fix', action='store_true', help='apply the fixable repairs')
    check.add_argument('--json', action='store_true')
    check.add_argument('--strict', action='store_true', help='warnings fail too')
    check.set_defaults(func=cmd_check)

    index = sub.add_parser('index', help="regenerate the board's BOARD.md")
    index.add_argument('--stdout', action='store_true', help='print instead of writing')
    index.add_argument('--private', action='store_true', help='include private cards')
    index.set_defaults(func=cmd_index)

    migrate = sub.add_parser('migrate', help='convert a pre-board card tree to cards')
    migrate.add_argument('--apply', action='store_true', help='write the changes (default: dry run)')
    migrate.set_defaults(func=cmd_migrate)

    verifier = sub.add_parser('verifier', help='who should QA a card, given what is installed here')
    verifier.add_argument('id', help='the card id, e.g. K7Q2')
    verifier.add_argument('--json', action='store_true')
    verifier.set_defaults(func=cmd_verifier)

    args = parser.parse_args(argv)
    issues = args.issues or default_board_dir()
    if not issues.is_dir():
        print(f"no board directory at {issues}", file=sys.stderr)
        return 2
    board = board_mod.Board(issues)
    try:
        return args.func(args, board)
    except board_mod.BoardError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
