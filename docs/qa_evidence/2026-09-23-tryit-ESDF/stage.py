#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Stage the Try-it situation for card #ESDF: a small project whose Switchboard holds a day of
work spread over every stage, so "which card was touched last?" is a real question a person can
only answer from the board itself.

    python3 stage.py                 # stage ~/relay-qa/esdf-notes and say how to open it
    python3 stage.py --dir /tmp/x    # somewhere else
    python3 stage.py --fresh         # wipe a previous staging first

No network, no model: the cards are files, seeded with `updated` times from minutes to weeks ago
(written into the card threads, which is what `updated` reads) so the flat Recent list has
something honest to order. `STAGED.json` records where the project landed.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "backend"))
from relay_core import board as B  # noqa: E402

README = """# field-notes

A staged project for human QA of Relay card #ESDF. Safe to delete.
"""

# (status, title, minutes-ago-updated, one-line thread note). The updated times are the point:
# the newest card is a needs-qa one, the second an inbox one, and a week-old executing card sits
# far down the Recent list even though its stage would put it high in the sections.
CARDS = [
    ("needs-qa-human", "Export wizard loses the last page on iPad", 4,
     "the export wizard drops the final page on iPad Safari — reproducible on the 12.9\" fixture"),
    ("inbox", "Decide whether reminders sync to the phone", 26,
     "do reminders belong on the phone or stay on the desktop? need a call before the next build"),
    ("executing", "Search should rank open cards above done ones", 95,
     "open cards should outrank done ones in search; the rank function is half changed"),
    ("done", "Fix the double scrollbar in Options", 260,
     "options had two scrollbars on small windows; fixed and verified"),
    ("discussing", "Should the queue keep folded rows on restart?", 1500,
     "the queue forgets its folded rows on restart — keep them or is that a feature?"),
    ("needs-verification", "Rounding on the invoice preview", 2900,
     "invoice preview rounds half-cent totals the wrong way; fix is in, needs a verifier"),
    ("planning", "Offline mode for the phone board", 4400,
     "the phone board is useless on the tube; a plan for caching the last board is sketched"),
    ("ready", "Rename 'closed' to 'archived' in the docs", 6000,
     "docs still say closed where the UI says archived; mechanical, ready when someone is"),
    ("executing", "The tray menu forgets its checkmarks", 10300,
     "the tray menu loses its checkmarks after suspend; half a fix is in the tree"),
    ("inbox", "Bring the old shortcuts cheat sheet back", 15000,
     "the shortcuts cheat sheet vanished in the redesign; bring it back as a page"),
]


def stage(target: Path) -> dict:
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    (target / "README.md").write_text(README)
    (target / ".gitignore").write_text("build/\n")
    board = B.Board(target / B.BOARD_FOLDERS[0], target)
    B.scaffold(board)
    tabs = {t["id"]: t.get("folder", t["id"]) for t in board.config().get("tabs", []) if t.get("folder")}
    now = datetime.now(timezone.utc)
    made = {}
    for i, (status, title, minutes, note) in enumerate(CARDS):
        updated = now - timedelta(minutes=minutes)
        created = updated - timedelta(days=2 + i)
        card = B.new_card("work", title, status,
                          created=created.strftime("%Y-%m-%d"),
                          labels=["bug"] if i % 3 == 0 else [])
        folder = tabs.get("features", "features")
        path = B.write_new_card(board, card, folder)
        thread = board.root / B.THREADS_FOLDER / f"{card.id}.md"
        thread.parent.mkdir(parents=True, exist_ok=True)
        thread.write_text(
            f"# {card.id}\n\n"
            f"- [{updated.strftime('%Y%m%dT%H%M%SZ')}-ab owner note] {note}\n")
        # `updated` reads the card and thread files' mtimes (board_tools._updated_at, protocol
        # 19.2), so the stage sets those mtimes: the Recent list then orders the day honestly.
        stamp = updated.timestamp()
        os.utime(thread, (stamp, stamp))
        os.utime(path, (stamp, stamp))
        made[title] = card.id
    subprocess.run(["git", "init", "-q"], cwd=target, check=True)
    subprocess.run(["git", "add", "-A"], cwd=target, check=True)
    subprocess.run(["git", "-c", "user.name=stage", "-c", "user.email=stage@example.com",
                    "commit", "-qm", "staged project for #ESDF Try it"], cwd=target, check=True)
    return {"project": str(target), "cards": made}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=str(Path.home() / "relay-qa" / "esdf-notes"))
    ap.add_argument("--fresh", action="store_true")
    args = ap.parse_args()
    target = Path(args.dir)
    info = stage(target)
    (HERE / "STAGED.json").write_text(json.dumps(info, indent=2) + "\n")
    print(f"staged {info['project']} ({len(info['cards'])} cards)")


if __name__ == "__main__":
    main()
