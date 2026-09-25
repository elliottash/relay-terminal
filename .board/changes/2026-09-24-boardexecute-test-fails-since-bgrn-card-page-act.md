---
id: DEH6
type: work
status: needs-verification
labels: [bug, switchboard, tests]
assignee: agent
implemented_by: glm/glm-5.3
session: 15e42790-4cc5-403f-9602-bc49a74c6e67
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'the three named BoardExecuteTests pass against the cardActions architecture, on a build of current main', sign_off: none, effort: low}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# boardexecute test fails since #BGRN: card-page action row no longer child buttons of the view

## Issue
Found while working #E728 (phone Run in background): `ctest -R boardexecute` fails at HEAD. BoardExecuteTests::{theButtonNamesTheExecutingPaneAndRevealsIt,thePlanButtonNamesThePlanningPaneAndRevealsIt,theVerifyButtonNamesTheVerifyingPaneAndRevealsIt} all fail at `QVERIFY(button)` — the test searches `view.findChildren<QPushButton *>` for the card page's action row, but since #BGRN (854c097d, "Run in background moves an active agent to the background") the row is QToolButtons rendered by the card's agent console (CardContext::actions() → BoardView::cardActions() → CardDetail::cardActions(), src/BoardPane.cpp:3250), not child widgets of BoardView. The labels also moved: Execute (x)→Run (r), Executing (…)→Running (…), Verify's key boardExecute→boardVerify, and a new "Run in pane" action exists. Confirmed pre-existing: `git show HEAD:src/BoardPane.cpp` already has `execute.label = "Run"`; BoardPane.cpp had no uncommitted changes. Fix means hosting the view's CardContext in a console in the test, or testing cardActions() directly. Evidence: ctest --test-dir build -R boardexecute output, 2026-09-24.

## Done means
`ctest -R '^boardexecute$'` passes on a build of current main: the tests exercise the cardActions architecture (CardContext::actions() → BoardView::cardActions()) instead of searching for child QPushButtons, and assert the current labels (Run (r), Running (…), Verify key boardVerify, Run in pane).
