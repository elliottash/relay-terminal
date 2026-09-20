#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Switchboard command line: check the card format, regenerate the index, migrate.

For collaborators without the Relay GUI, for CI and for pre-commit hooks.
Never calls a model and never uses the network.

  relay-board.py check [--fix] [--json] [--strict]
  relay-board.py index [--stdout] [--private]
  relay-board.py policy [--stdout]
  relay-board.py migrate [--apply]
  relay-board.py verifier <ID> [--json]
  relay-board.py signals [list|claim|release|dismiss|promote] [KEY] [--as NAME] ...
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


def cmd_policy(args, board: board_mod.Board) -> int:
    """Regenerate `<board>/POLICY.md` and the pointer block in CLAUDE.md / AGENTS.md.

    A new board gets both from the scaffold; this is for a board that predates them, or one whose
    copy has gone stale because the policy or the `deliver` skill changed.  Both files are
    generated, so there is nothing to merge: the file is rewritten and the block is replaced
    between its markers, leaving everything else in the instruction file alone.
    """
    if args.stdout:
        sys.stdout.write(board_mod.policy_text(board))
        return 0
    written = board_mod.write_policy(board)
    for path in written:
        print(f"wrote {path}")
    if not written:
        print(f"unchanged {board.root / board_mod.POLICY_FILE} (and the instruction files)")
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


def cmd_signals(args, board: board_mod.Board) -> int:
    """The faults the machine is tracking, for an agent with no `board_signals` tool (#AQ6X).

    A *signal* is folded out of `<board>/.private/` — the run history plus an append-only log of
    actions — so this is the only honest way to reach one from a shell: there is no card file to
    edit, the store is local to this machine, and nothing here is committed.

    A guest has no pane session token, so `--as` is the name that goes in the claim (`claude-code`,
    `codex`, whatever names you in a thread), exactly as `assignee` is on a card.  Everything else
    is the same rule the tool enforces, from the same module: `environmental` and `flaky-known`
    only, a comment, and an expiry at most a week out.
    """
    from relay_core import signals as sig
    action = (args.action or 'list').lower()
    signals = sig.state(board.repo, board.root)
    if action == 'list':
        rows = [s for s in sig.sort_signals(signals.values())
                if s.state in ('open', 'pending', 'dismissed')]
        if args.json:
            print(json.dumps([s.to_dict() for s in rows], indent=2))
            return 0
        if not rows:
            print('no signals: every check this project records is passing')
            return 0
        for one in rows:
            marks = ''.join(m for m in (' regressed' if one.regressed else '',
                                        ' stale' if one.stale else ''))
            held = f" held by {one.session}" if one.session else ''
            card = f" card #{one.card}" if one.card else ''
            print(f"{one.state:9} {one.kind:7} {one.key}  {one.count} failure(s), "
                  f"last {one.last_seen}{marks}{held}{card}")
            if one.message:
                print(f"            {one.message[:160]}")
        return 0
    key = (args.key or '').strip()
    if not key:
        print(f"signals {action} needs the signal's key, as `signals` lists it", file=sys.stderr)
        return 2
    one = signals.get(key)
    if one is None or one.state in ('resolved', 'removed'):
        print(f"no open signal {key!r} (a signal resolves by its check passing)", file=sys.stderr)
        return 2
    path = sig.default_path(board.repo, board.root)
    who = (args.who or '').strip()
    if action == 'claim':
        if not who:
            print('signals claim needs --as <name>: a guest has no pane token, so the name in '
                  'the claim is what tells the next agent who is on it', file=sys.stderr)
            return 2
        if one.session and one.session != who and not args.force:
            print(f"{key} is held by {one.session}: read its history, say what you are doing "
                  "instead, and pass --force only when the user says to take it over",
                  file=sys.stderr)
            return 2
        sig.append_event({'action': 'claim', 'key': key, 'session': who}, path)
        print(f"{key} claimed by {who}; it resolves on "
              f"{sig.RESOLVE_PASSES.get(one.kind, 2)} consecutive passing executions and on "
              "nothing else")
        return 0
    if action == 'release':
        sig.append_event({'action': 'release', 'key': key, 'session': who,
                          'reason': args.reason or ''}, path)
        print(f"{key} released")
        if (args.reason or '') != sig.GAVE_UP:
            return 0
        action = 'promote'                      # gave up: it is a person's problem now
    if action == 'dismiss':
        try:
            dismissal = sig.check_dismissal(args.reason, args.comment, args.until, by_agent=True)
        except sig.SignalError as exc:
            print(str(exc), file=sys.stderr)
            return 2
        sig.append_event({'action': 'dismiss', 'key': key, 'by': who, **dismissal}, path)
        print(f"{key} dismissed as {dismissal['reason']} until {dismissal['until']}; it keeps "
              'counting underneath and blocks no card')
        return 0
    from relay_core import board_tools as bt
    tools = bt.BoardTools(board, autonomy='auto', enforce_limits=False, duplicate_check=False,
                          context=bt.ToolContext(actor=who or 'agent'))
    result = tools.promote_signal(key, args.reason or 'by hand')
    if result.get('error'):
        print(str(result['error']), file=sys.stderr)
        return 2
    if not result.get('promoted'):
        print(f"{key} already has card #{result.get('card') or '?'}")
        return 0
    print(f"{key} is now card #{result['card']} ({result['path']}); the signal stays open until "
          'its check passes')
    return 0


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

    policy = sub.add_parser('policy', help="regenerate the board's POLICY.md and the CLAUDE.md / "
                                          "AGENTS.md pointer at it")
    policy.add_argument('--stdout', action='store_true', help='print POLICY.md instead of writing')
    policy.set_defaults(func=cmd_policy)

    migrate = sub.add_parser('migrate', help='convert a pre-board card tree to cards')
    migrate.add_argument('--apply', action='store_true', help='write the changes (default: dry run)')
    migrate.set_defaults(func=cmd_migrate)

    verifier = sub.add_parser('verifier', help='who should QA a card, given what is installed here')
    verifier.add_argument('id', help='the card id, e.g. K7Q2')
    verifier.add_argument('--json', action='store_true')
    verifier.set_defaults(func=cmd_verifier)

    signals = sub.add_parser('signals', help='the faults the machine is tracking (card #AQ6X): '
                                             'list them, or claim, release, dismiss or promote one')
    signals.add_argument('action', nargs='?', default='list',
                         choices=['list', 'claim', 'release', 'dismiss', 'promote'])
    signals.add_argument('key', nargs='?', help="the signal's key, e.g. ctest:panelayout")
    signals.add_argument('--as', dest='who', default='',
                         help='the name that holds the claim (a guest has no pane token)')
    signals.add_argument('--reason', default='',
                         help='on release, `gave-up` files it as a bug card; on dismiss, '
                              'environmental or flaky-known')
    signals.add_argument('--comment', default='', help='on dismiss: what you checked, and why '
                                                       "this is not the code's fault")
    signals.add_argument('--until', default='', help='on dismiss: the date it comes back, at most '
                                                     'a week out')
    signals.add_argument('--force', action='store_true',
                         help='on claim: take one another session holds')
    signals.add_argument('--json', action='store_true')
    signals.set_defaults(func=cmd_signals)

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
