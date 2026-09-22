---
id: SW1D
type: work
status: planned
labels: [feature, switchboard, qa]
component: [gui]
parent: YZ8G
rank: zzzzzzzzzzzzzzzzf
created: '2026-09-21'
source: 'owner, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [YZ8G, 1CXD, SJTR], github: null}
---
# Hygiene and Performance: the board's tool row goes from four buttons to three

## Issue
"also what is check vs clean up in there? we can rethink the buttons on the board as part of
this" — then, on the proposal: "i would suggest to merge check and clean up but call it hygiene.
change profile to Performance. change Tests to Validation." — then, correcting the placement:
"but i dont want performance in the gear menu" — then, dropping the Tests rename: "lets keep
Tests for now."

## Decisions
- 2026-09-21, owner: merge the board's **Check** (the format check, `relay-board.py check` —
  folder against status, front matter, ids, ranks, thread entries; no agent call) and **Clean
  up** (an agent turn over the whole board: merges, splits, missing labels and sections, dry run
  then apply) into one button, **Hygiene**. This also resolves the name collision the question
  started from: the card page's own **Check** button (the tests check from #7BM4/#PR4Q) is
  untouched and unambiguous once the board-level Check is folded into Hygiene.
- 2026-09-21, owner: **Profile** is renamed **Performance**, and stays on the tool row — not the
  gear menu ("i dont want performance in the gear menu").
- 2026-09-21, owner: **Tests keeps its name for now.** The Validation rename is dropped. Tests is
  instead the subject of its own card, #SJTR, about becoming the entry point to a broader QA
  system.
- The row becomes **Hygiene · Tests · Performance**, in that order.

## Done means
- The tool row shows exactly Hygiene, Tests, Performance, in that order — no more Check or Clean
  up as separate buttons.
- Pressing Hygiene runs the format check first (deterministic, no agent call) and shows its
  findings; a second stage in the same panel — labelled Clean up, or folded into one dry-run/apply
  flow — starts the existing agent cleanup turn, unchanged in what it does.
- Pressing Performance opens exactly what Profile opens today (the four-target menu: Build this
  machine, Build another machine, Python tests, The app) — same behaviour, new label, on the row.
- The card page's own Check button (the per-card tests check) is untouched: same label, same
  behaviour, same wiring.
- Product strings say Hygiene / Performance wherever a person reads them (button, tooltip,
  palette row and its aliases, notice lines); wire names, event names and settings keys keep
  their existing identifiers unless a docstring needs the new word — the same split the Board
  rename (#1CXD) used.

Failure would show as: two controls where one was promised; Performance in a menu; the card-level
Check button's label or behaviour changing when nothing asked it to; a docstring-only rename that
missed a string a person actually sees.

## Plan
Sequence after #1CXD's Area B (the GUI product word and folder) lands and releases its claim on
`src/BoardPane.cpp` — this card edits the same tool-row construction code and would otherwise
collide with it.
