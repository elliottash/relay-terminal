---
id: YVTW
type: work
status: needs-verification
labels: [feature, board]
assignee: agent
implemented_by: kimi/k3
session: f0e59a00-99e9-4342-8360-5205ae23a81e
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [], human: none, criteria: 'sections() lists no active/deferred extra and no memory-card status; a fresh BoardView''s hiddenSections() holds verified, done and dropped; the checks row carries an Uncheck all button that unticks every section; board pane tests pass', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane 1, 2026-09-25
links: {plans: [], commits: [4a4df89650aa], evidence: [docs/qa_evidence/2026-09-25-yvtw-uncheck-all-defaults/notes.md], related: [], github: null}
---
# Board cards list: Uncheck all button, closed stages off by default, no Active/Deferred sections

## Issue
The cards list's section checkbox row should grow an "Uncheck all" button, the closed stages (Verified, Done — which folds dropped cards — and dropped itself) should start unticked when a pane opens with no saved choice, and the Active and Deferred extra sections (memory cards' and parked cards' statuses) should no longer be drawn at all.

> add a uncheck all button on the board. make verified, done, dropped unchecked by default. remove active and deferred.
> — elliott · [session:9f484f7620a242a7ad8000f577f4d9fa](relay://session/9f484f7620a242a7ad8000f577f4d9fa) · 2026-09-25

## Done means
- A fresh cards list opens with Verified, Done and dropped unticked (Done folds dropped cards; a saved choice still replaces the default).
- The section checkbox row carries an **Uncheck all** button that unticks every section in one click; ticking one back on is enough to see it again.
- No Active, Deferred (or any memory-status) section is drawn on the cards list any more; a column that collects such a status still shows it.
- A card reached by `#ID` in a section that is unticked ticks its section back on and unfolds it, so a reference always finds what it names.
- `openCount()` and the count label only count cards a section can actually draw, so "N of M open" stays honest.

## Tests
- `build/relay-board-tests` — 96 passed, 0 failed (includes new `theClosedStagesStartUntickedAndUncheckAllClearsTheRow`; updated `closedCardsGoToTheDoneSectionAndExtrasKeepTheListWhole`, `memoriesKeepTheirOwnStatuses`, `aSectionCheckboxTakesItsSectionOffThePageAndTheCountSaysSo`, `aSelfClosedCardReachedByIdUnfoldsItsGroup`, `theFoldRowToggles…`, `theViewRendersOneListFromAnEvent`).
- `build/relay-boardsections-tests` — 19 passed, 0 failed (the two deferred-extra examples rewritten to a status that still earns a section).
- `build/relay-boardpane-tests` — 20 passed, 0 failed.
- `land.py` verify slot: the exact landed tree (`4a4df89650aa`) compiles the `relay` target.
- Landed via `scripts/land.py` with only this card's hunks of `src/BoardPane.cpp` (1, 4, 8); the six hunks belonging to #TBRH/#9FX8/#Y2BA stayed uncommitted in the tree.
