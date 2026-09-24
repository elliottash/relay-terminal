---
id: N5JJ
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: claude-code
rank: m7
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [0a02f666], evidence: [docs/qa_evidence/2026-09-20-perf-profile/, docs/qa_evidence/2026-09-20-perf-fixes/board/], related: [PF4K, 7M6E], github: null}
---
# The Switchboard's watcher misses appends to existing cards and threads

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Found while measuring refresh cost for #PF4K; a correctness fault, not a performance one. Detail: [docs/qa_evidence/2026-09-20-perf-profile/board/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/board/FINDINGS.md), "Incidental", and `board/measurements.txt`.

The pane's `QFileSystemWatcher` holds 12 inotify watches, 11 of them the board's directories. A directory watch fires when an entry is created, renamed or removed, not when an existing file's content changes, so an append to an existing card or thread often does not reach the pane: in a 60 s test writing once a second, about 21 of 60 writes produced a refresh.

## Plan
Decide what the pane should notice: either watch the open card's file and thread as well as the directories, or have writers that the pane cares about (Relay's own `board_*` tools, `relay-board.py`) replace files atomically (write + rename), which a directory watch does see. Guests editing files in place would still be missed by the second option.

## What landed
The orchestrator's decision was to do all three, and it is a defensible default: make Relay's own
writes visible first, watch the few files a foreign writer is likely to touch, and catch the rest
when somebody looks at the pane.

1. **Every writer Relay owns replaces its files.** `Board.save`, the index and the undo path
   already did (`_atomic_write`); the two that appended in place did not, and they were the ones
   that mattered — `Board.append_thread` and `carry_thread`, so *every* thread write (an agent's
   progress note, a comment, every `✦ …` event line) was invisible to a directory watch. Both go
   through a new `board.append_to_thread` now: read, render, write a temporary file, `os.replace`.
   The lock moved from the file to the **threads directory**, because `os.replace` gives the path
   a new inode and a lock on the old one would stop excluding anybody the moment the first writer
   landed its rename.
2. **The pane keeps a file watch on the cards being worked on**: the card the page is open on and
   its thread, then the cards in `in-progress` while a 32-file budget lasts (of the 200-watch
   allowance, 11 of which are this board's folders). A watched file is dropped by
   `QFileSystemWatcher` the moment it is replaced, so the set is rebuilt after every fire and
   whenever the open card or the executing set changes.
3. **One debounced refresh when the pane is looked at again** — on show and on focus-in — for
   everything the first two miss. No timer: #057J is taking idle wakeups out of Relay, and with
   the parse cache from #7M6E a refresh that finds nothing is about 6 ms of worker at 353 cards.

A guest editing a card file in place, outside the two sets in 2, is still seen only at 3 (or at
the next write that does change a directory). That is the residual the plan named, and it is the
reason 3 exists.

**60 writes at one a second: Relay's own writers 40 of 60 → 60 of 60; a foreign in-place writer on
the card the pane is working on 0 of 60 → 40 of 60** (the 20 still missed are `BOARD.md` touches —
a generated index, not a card). Harness and detail:
[docs/qa_evidence/2026-09-20-perf-fixes/board/WATCHER.md](../../docs/qa_evidence/2026-09-20-perf-fixes/board/WATCHER.md).

## QA checklist
- [ ] Open the Switchboard. In a terminal, `printf '\n- a note\n' >> issues/threads/<ID>.md` for a
      card that is **open in the card page**: the page and the row follow within about a second.
- [ ] Same for a card in **Executing** that is not open: the row follows.
- [ ] Ask an agent something on a card (or comment on one): the thread entry appears in the pane
      without touching anything. Before, a thread append often never showed until something else
      changed.
- [ ] Switch to another tab and back: the board catches up with whatever changed while it was away.
- [ ] `git pull` a branch that changes cards: the list follows, as it did before.
- [ ] Nothing polls: with the pane open and idle, `strace -c -p <relay pid>` for 30 s shows no
      periodic wakeup this added.
- [ ] Two Relay windows on the same board, both writing thread entries: every entry survives
      (`python3 scripts/relay-board.py check` reports no duplicate or lost entry ids).
