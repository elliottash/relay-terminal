# SPDX-License-Identifier: AGPL-3.0-or-later
"""Local operator commands for per-person Pro codes; never an HTTP issuance endpoint."""
from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path

from .store import Store


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Manage Relay Pro access codes locally")
    parser.add_argument("--db", required=True, help="existing gateway SQLite database")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("issue", help="print one new code once").add_argument("person")
    commands.add_parser("list", help="list person labels and issue/revocation times")
    commands.add_parser("revoke", help="revoke a person's code immediately").add_argument("person")
    args = parser.parse_args(argv)
    if not Path(args.db).is_file():
        parser.error("--db must be an existing gateway database")
    store = None
    try:
        store = Store(args.db)
        if args.command == "issue":
            print(store.issue_pro_code(args.person))
        elif args.command == "list":
            print(json.dumps(store.list_pro_codes(), indent=2))
        elif not store.revoke_pro_code(args.person):
            print("No active code for that person.", file=sys.stderr)
            return 1
        return 0
    except sqlite3.IntegrityError:
        print("Issue refused: person already has an active code or code collision; revoke first.",
              file=sys.stderr)
        return 1
    except (sqlite3.Error, ValueError):
        print("Pro operation failed; check the database and person label.", file=sys.stderr)
        return 1
    finally:
        if store is not None:
            store.close()


if __name__ == "__main__":
    raise SystemExit(main())
