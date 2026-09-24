---
id: BVK1
type: work
status: needs-verification
labels: [bug, theme]
component: [theme]
workstream: terminal
assignee: agent
implemented_by: glm/glm-5.3
session: 5587f7bf-a3a1-4d5a-a780-e267d86d5202
rank: zzzz103
created: '2026-09-19'
acceptance: a Bevel theme draws the pane's button row with the moulded edge every other raised chip in that theme has, and `hot="true"` appears nowhere in the stylesheet
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'ctest -R ''^themeswitch$'' passes with the new QVERIFY2, and hot="true" appears nowhere under src/', sign_off: none, effort: low, stakes: nuisance}
source: 'found while landing #0T2R, 2026-09-19'
links: {commits: ['0b18990'], evidence: [docs/qa_evidence/2026-09-18-pane-buttons-brighter-outline/], github: null, plans: [], related: [0T2R]}
---
# The bevel stylesheet still keys the pane's button row on a property nothing sets

## Issue

`1b270ef` made the pane's button row permanent and removed `QFrame#paneChrome[hot="true"]` from the
metal and plastic material stylesheets — but not from the bevel one, where it is still the selector
today (`src/Theme.cpp`, `bevelStylesheet`). Nothing has set the `hot` property since that commit, so
that rule has never fired: on a Bevel theme (IBM Beige is one) the row is the only raised chip in
the window without the moulded edge.

## Fix

One word — `QFrame#paneChrome[hot="true"]` → `QFrame#paneChrome` — plus the assertion that was held
back with it in `tests/themeswitch_test.cpp::thePaneButtonRowKeepsTheTileAndTheOutline`:

```cpp
QVERIFY2(!css.contains(QStringLiteral("hot=\"true\"")), id);
```

## Why it is not already done

It could not be landed with #0T2R, which is the rest of this change. Session `activepane` holds
`src/Theme.cpp` and its `QWidget#pane[relayActive="true"]` edit is seven lines below this one, so
`land.py` reads the two as a single hunk: landing mine would have carried half of theirs. Land this
as soon as that hunk clears (`land.py who`, then `commit --dry-run` to check the hunk has split).

Measured, so the size of what is missing is on the record: with the tile landed and this one word
left out, the IBM Beige button row still gets the tile and the outline; only the moulded edge
differs, 15792 pixels in the 4× crop
(`docs/qa_evidence/2026-09-18-pane-buttons-brighter-outline/`).

## Done means

- The bevel stylesheet's raised-chip rule selects `QFrame#paneChrome` unconditionally, so on a Bevel theme (IBM Beige) the pane's button row carries the same moulded two-tone edge as every other raised chip in the window.
- `hot="true"` appears in no stylesheet any theme produces: `thePaneButtonRowKeepsTheTileAndTheOutline` asserts `!css.contains("hot=\"true\"")` for dark-copper, ibm-beige and relay-dark, and the test passes.
- Failure would be recognised by that QVERIFY2 failing, or visually: on IBM Beige the button row is the one raised chip without the moulded edge.

## Execution Summary

The `activepane` claim had long cleared (`land.py who` no longer lists it; its `relayActive` edit landed), so the hunk stood alone: `src/Theme.cpp` was clean at tip before the edit. One word dropped from the bevel stylesheet's raised-chip selector, and the held-back QVERIFY2 added to `thePaneButtonRowKeepsTheTileAndTheOutline`. Two stale sessions (f8r7b, r5tc, 3–4 h old snapshots) still claimed `src/Theme.cpp`, so the commit went through the contested-hunk review: the dry run showed exactly this one-line hunk and nothing of theirs. Landed as `0b18990` after the verify build of the exact tree passed. `ctest -R themeswitch` green before and after landing.

## Tests

- `ctest --test-dir build -R '^themeswitch$'` — Passed (0.47 s), before and after the commit landed: the new `QVERIFY2(!css.contains(QStringLiteral("hot=\"true\"")), id)` holds for dark-copper, ibm-beige and relay-dark.
- `grep -rn 'hot="true"' src/` after the change: no matches (line 99 of `src/Theme.cpp` was the last).
- `scripts/land.py` verify build of the exact tree (`/tmp/claude-1000/land/bvk1/verify/`): `cmake --build … --target relay` exits 0.
