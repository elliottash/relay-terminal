#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The throwaway project the #SWPH hosted drive opens as Relay's workspace.

    fixture.py <project dir> <checkout root>

A small *real* Switchboard, made the way Relay makes one: `relay_core.board.scaffold()` writes the
hidden `.switchboard/` folder Relay has created since 2026-09-19 (board.yaml, .gitignore, threads/,
POLICY.md, the .gitattributes line), every card goes through `new_card()` / `write_new_card()` and
every thread entry through `Board.append_thread()` — the same calls the worker makes — and the lot
is committed to a git repository, because a project is one.

Ten cards across the stages. The ones the drive leans on:

  W8TQ  discussing, `waiting_on: owner`, and the agent's question with three numbered options as
        the last entry of its thread (steps 1-3)
  P7AN  planned, with a `## Plan`, an acceptance line and a task list whose items carry the
        `<!-- t:xx -->` markers a phone must never draw (steps 3, 7, 8)
  S3ZP  a bug whose *body* — not its title — has the word "zeppelin" (step 6)
  D4MV  the card that is moved (step 4)       N2BX  the card that gets a note (step 4)
  Q2HK  the card that is discussed (step 7)   R4SK  the card that starts waiting (step 9)
  V6RF  needs-verification (step 8's Verify)  E5XC  executing   C9DN  done
"""
import subprocess
import sys
from datetime import datetime
from pathlib import Path

project, checkout = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
sys.path.insert(0, str(checkout / "backend"))
from relay_core import board as B                                          # noqa: E402

project.mkdir(parents=True, exist_ok=True)
(project / "README.md").write_text("# Harbour\n\nA small tide-table service. The throwaway project of"
                                   " the #SWPH hosted drive.\n")
(project / "tides.py").write_text("def next_high_tide(port):\n    return '06:12'\n")
board = B.Board(project / B.DEFAULT_BOARD_FOLDER, repo=project)
B.scaffold(board)

CARDS = [
    # id, tab folder, status, title, the request, extra front matter, extra body
    ("W8TQ", "features", "discussing", "Tide table for the harbour page",
     "show the next three high tides on the harbour page, people keep phoning to ask",
     {"waiting_on": "owner", "labels": ["feature"], "assignee": "agent"}, ""),
    ("P7AN", "features", "planned", "Cache the tide feed for an hour",
     "the tide feed is slow, cache it",
     {"labels": ["feature"], "acceptance": "a second request within the hour does not call the feed"},
     "\n## Plan\n\n**Goal.** One feed call an hour per port.\n\n1. Wrap `next_high_tide` in a cache"
     " keyed by port.\n2. Expire entries after sixty minutes.\n\n## Tasks\n\n"
     "- [ ] 1 The cache and its expiry <!-- t:c1 -->\n- [ ] 2 A test with a fake clock <!-- t:c2 -->\n"),
    ("N2BX", "features", "inbox", "A printable tide card", "a printable card for the noticeboard",
     {"labels": ["feature"]}, ""),
    ("S3ZP", "changes", "inbox", "The harbour page is blank on Sundays",
     "on Sundays the page is blank. the feed sends the word zeppelin instead of a time and we choke on it",
     {"labels": ["bug"]}, ""),
    ("D4MV", "features", "planning", "Text the harbour master at spring tides",
     "text the harbour master when a spring tide is coming", {"labels": ["feature"]}, ""),
    ("Q2HK", "features", "discussing", "Which ports do we cover",
     "which ports do we cover, only ours or the whole coast", {"labels": ["feature"]}, ""),
    ("R4SK", "changes", "planning", "Times are an hour out in summer",
     "the times are an hour out since the clocks changed", {"labels": ["bug"]}, ""),
    ("E5XC", "features", "executing", "A favicon for the harbour page", "give it a favicon",
     {"labels": ["feature"], "assignee": "agent"}, ""),
    ("V6RF", "changes", "needs-verification", "The footer says 2024",
     "the footer still says 2024", {"labels": ["bug"], "implemented_by": "openai/gpt-6-astra"}, ""),
    ("C9DN", "features", "done", "Put the harbour page online", "put the page online",
     {"labels": ["feature"]}, ""),
]
ranks = B.initial_ranks(len(CARDS))
for (card_id, folder, status, title, request, front, extra), rank in zip(CARDS, ranks):
    card = B.new_card("work", title, status, card_id=card_id, rank=rank, request=request,
                      created="2026-09-20", **front)
    if extra:
        card.body = card.body.rstrip("\n") + "\n" + extra
    B.write_new_card(board, card, folder)

when = datetime(2026, 9, 20, 18, 0, 0)
board.append_thread("W8TQ", "Three tides is fine. Where should they go on the page?", author="owner",
                    kind="comment", when=when, mode="discuss")
board.append_thread(
    "W8TQ",
    "There are three places they could go, and they trade off differently:\n\n"
    "1. Above the map — the first thing anyone sees, but it pushes the map down on a phone.\n"
    "2. In the sidebar — out of the way, and invisible on a phone, where the sidebar folds.\n"
    "3. On a page of their own — room for the whole week, one more tap for the people phoning.\n\n"
    "Which one do you want?",
    author="agent", kind="question", when=when.replace(minute=1), model="fake")
board.append_thread("P7AN", "Planned from the issue: one cache, one expiry, one test.", author="agent",
                    kind="plan", when=when.replace(minute=2), model="fake")

git = ["git", "-C", str(project), "-c", "user.name=qa", "-c", "user.email=qa@example.invalid"]
subprocess.run([*git, "init", "-q", "-b", "main"], check=True)
subprocess.run([*git, "add", "-A"], check=True)
subprocess.run([*git, "commit", "-qm", "fixture: a small Switchboard"], check=True)
problems = board.check()
print(f"{len(board.cards())} cards in {board.root.name}/, {len(problems)} problem(s)")
for problem in problems:
    print("  ", problem)
