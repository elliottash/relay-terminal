---
id: DEH6
type: work
status: inbox
labels: [bug, switchboard, tests]
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# boardexecute test fails since #BGRN: card-page action row no longer child buttons of the view

## Issue
Found while working #E728 (phone Run in background): `ctest -R boardexecute` fails at HEAD. BoardExecuteTests::{theButtonNamesTheExecutingPaneAndRevealsIt,thePlanButtonNamesThePlanningPaneAndRevealsIt,theVerifyButtonNamesTheVerifyingPaneAndRevealsIt} all fail at `QVERIFY(button)` — the test searches `view.findChildren<QPushButton *>` for the card page's action row, but since #BGRN (854c097d, "Run in background moves an active agent to the background") the row is QToolButtons rendered by the card's agent console (CardContext::actions() → BoardView::cardActions() → CardDetail::cardActions(), src/BoardPane.cpp:3250), not child widgets of BoardView. The labels also moved: Execute (x)→Run (r), Executing (…)→Running (…), Verify's key boardExecute→boardVerify, and a new "Run in pane" action exists. Confirmed pre-existing: `git show HEAD:src/BoardPane.cpp` already has `execute.label = "Run"`; BoardPane.cpp had no uncommitted changes. Fix means hosting the view's CardContext in a console in the test, or testing cardActions() directly. Evidence: ctest --test-dir build -R boardexecute output, 2026-09-24.
