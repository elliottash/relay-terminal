---
id: N5JJ
type: work
status: planned
labels: [bug, switchboard]
assignee: null
rank: m7
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/], related: [PF4K, 7M6E], github: null}
---
# The Switchboard's watcher misses appends to existing cards and threads

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Found while measuring refresh cost for #PF4K; a correctness fault, not a performance one. Detail: [docs/qa_evidence/2026-09-20-perf-profile/board/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/board/FINDINGS.md), "Incidental", and `board/measurements.txt`.

The pane's `QFileSystemWatcher` holds 12 inotify watches, 11 of them the board's directories. A directory watch fires when an entry is created, renamed or removed, not when an existing file's content changes, so an append to an existing card or thread often does not reach the pane: in a 60 s test writing once a second, about 21 of 60 writes produced a refresh.

## Plan
Decide what the pane should notice: either watch the open card's file and thread as well as the directories, or have writers that the pane cares about (Relay's own `board_*` tools, `relay-board.py`) replace files atomically (write + rename), which a directory watch does see. Guests editing files in place would still be missed by the second option.
