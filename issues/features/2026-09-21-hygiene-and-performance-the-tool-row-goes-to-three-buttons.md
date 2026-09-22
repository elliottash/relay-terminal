---
id: SW1D
type: work
status: needs-verification
labels: [feature, switchboard, qa]
component: [gui]
assignee: codex
parent: YZ8G
rank: zzzzzzzzzzzzzzzzf
created: '2026-09-21'
source: owner, 2026-09-21
links: {plans: [], commits: [e325845d59af145ea6b692d38e7c526ad48b9f2e, 7133156a0d1955c4ecf35c68ac705b2cd4a218d0, a83e139f22c0], evidence: [docs/qa_evidence/2026-09-21-sw1d/, docs/qa_evidence/2026-09-21-verify-SW1D-a1/], related: [YZ8G, 1CXD, SJTR], github: null}
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

**Goal:** Deliver the three-action row and a deterministic-first Hygiene flow.
**Findings:** `BoardContext::actions`, `showFindings`, and the existing cleanup preview/apply panel in `src/BoardPane.cpp` own the flow.
**Steps:** 1. Combine actions and expose cleanup after findings. 2. Rename Performance presentation. 3. Run targeted tests and isolated Xvfb evidence; land only owned hunks.
**Risks:** Shared parent driver edits are excluded; wire and settings identifiers stay stable.
**Verify:** Board and performance pane tests, application build, isolated GUI exercise.

## Execution Summary
The Board row is Hygiene · Tests · Performance. Hygiene runs the format check without an agent,
then offers Clean up beside its findings. Cleanup retains preview, Stop, Apply and changelog;
its result panel now shares the board-agent area with the format findings. Performance keeps
its four targets, with updated pane titles and notices. Card-page Check and wire/settings keys
are unchanged. Parent #74Y5 driver changes were excluded from the landing diff.
Follow-up updates the existing board-model cleanup tests to enter through Hygiene and registers
the stable `profile` pane type with the visible title Performance.
Session handoff, 2026-09-21: added owner request delivered after #1CXD B1. Implementation e325845d plus follow-up 7133156a: Hygiene · Tests · Performance, format check first, then cleanup preview/Stop/Apply; Performance pane chrome renamed too. 87 Board cases and four targeted suites passed. Independent bounded evidence a83e139f: docs/qa_evidence/2026-09-21-verify-SW1D-a1/report.md confirms exact-build row, format-first and Performance routing; deterministic preview/apply transitions pass. Live model-generated cleanup preview and successful Apply remain unverified because that isolated fixture lacked a supported provider. Next verifier should complete that bounded live path. Keep needs-verification; do not reimplement the row.

## Tests
- `ctest -R board --test-dir build` — tests/boardmodel_test.cpp
- `ctest -R panestatus --test-dir build` — tests/panestatus_test.cpp
- `QT_QPA_PLATFORM=offscreen build/relay-boardpane-tests hygieneChecksBeforeCleanup` — passed; order, deterministic request, preview, cancel, and explicit Apply.
- `ctest -R profilepane --test-dir build --output-on-failure` — tests/profilepane_test.cpp
- `scripts/relay-build --target relay relay-boardpane-tests relay-profilepane-tests` — passed.
- manual: docs/qa_evidence/2026-09-21-sw1d/README.md
- `python3 scripts/relay-board.py check` — no SW1D findings; existing board-wide errors remain outside this change.

### Check 2026-09-21 21:37
- passed · ctest:board — ctest -R board passed for this revision on spark-dcc9, 2026-09-22T01:37:20Z
- passed · ctest:panestatus — ctest -R panestatus passed for this revision on spark-dcc9, 2026-09-22T01:37:20Z
- passed · ctest:profilepane — ctest -R profilepane passed for this revision on spark-dcc9, 2026-09-22T01:37:20Z
- not-applicable · manual:docs/qa_evidence/2026-09-21-sw1d/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-21-sw1d/README.md
- notice · ctest:board — ctest -R board is slow: p95 2.58 s, p50 0.93 s
history: thread
