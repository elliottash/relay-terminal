---
id: BVK1
type: work
status: planned
labels: [bug, theme]
component: [theme]
workstream: terminal
assignee: agent
rank: zzzz103
created: '2026-09-19'
acceptance: a Bevel theme draws the pane's button row with the moulded edge every other raised chip in that theme has, and `hot="true"` appears nowhere in the stylesheet
source: 'found while landing #0T2R, 2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-pane-buttons-brighter-outline/], related: [0T2R], github: null}
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
