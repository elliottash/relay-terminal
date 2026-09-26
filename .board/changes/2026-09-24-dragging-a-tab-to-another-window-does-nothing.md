---
id: W6ES
type: work
status: executing
labels: [bug, ui]
assignee: agent
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [person], human: none, criteria: 'relay-tabtearoff-tests passes (tear-off math and drop-window resolution), relay builds, and dragging a tab label out of one window onto another moves the tab there', sign_off: none, effort: low, stakes: nuisance, blast: capability}
links: {plans: [], commits: [3ea1dc7650e7], evidence: [], related: [], github: null}
---
# Dragging a tab to another window does nothing

## Issue
buggish: dragging tabs between windows doesnt work

## Done means
Dragging a tab label past its window's edge and dropping it on another Relay window moves the tab (whole page, splits included) into that window; dropping on empty space gives it a window of its own; releasing back inside the source window changes nothing. Tab reorder within a window keeps working.

## Execution Summary
Tab labels never had a drag-out gesture: Qt's QTabBar owns reorder inside the bar, `ThemeTabBar` adds no mouse handling, and the cross-window drag that existed (`dragPaneEnd`) is a pane-header gesture. Added the tear-off: `RelayWindow::tabDrag` watches the tab bar's mouse events from the event filter (`RelayWindowCore.cpp`), and past Qt's drag threshold and outside the window's `frameGeometry()` (`relay::tabs::leavesWindow`, new `src/TabTearOff.h`) the gesture becomes a tab move — the whole page, splits included. The window under the cursor (`relayWindowAt`) gets a drop-zone highlight over its tab row; the release moves the page via `moveTabToWindow` (the extracted `moveTabToNewWindow` sequence), which also serves drops on empty space by centring a new window on the release point. Inside the source window nothing changed — the watcher never consumes an event, so Qt's internal reorder drag keeps the mouse grab to its own release, and a window still keeps its last tab.

Landed as `17b297afe41e` through `scripts/land.py` (exact-tree build gate passed; another session's uncommitted #E85D hunks in `RelayWindowCore.cpp` were left out via `--only-hunk` and remain in the working tree for it).

## Tests
- `tests/tabtearoff_test.cpp` → `ctest -R tabtearoff`: when a press on a tab label leaves its window (threshold + frameGeometry, against real QWidget geometry) and which window a release lands in (source never chosen, overlap → first listed, empty desktop → a window of its own). Passed on the landed tree.
- `ctest -R '^panes$'` (pane movement — `dragPaneMove`'s drop-zone creation was refactored into the shared `dropZoneOverlay`): passed.
- `scripts/relay-build --target relay`: builds; land.py's exact-tree build gate re-verified the committed tree.
- `ctest -R panestatus` fails on main before and after this change — filed as #WMX7, not this card's code.
