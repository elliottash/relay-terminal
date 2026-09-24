---
id: QVGQ
type: work
status: needs-verification
labels: [feature, panes]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: acce7191-6ff1-48e4-a089-ef2ec05f4c1b
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: 'From a ~700 px pane, Sessions, Settings, Review and a file all open to the right', sign_off: none, effort: low}
links: {plans: [], commits: [0c853332, e52c90de], evidence: [tests/panelayout_test.cpp], related: [803C, EQM2], github: null}
---
# One rule for where a pane opens: to the right, below only when the pane is too narrow to share

## Issue
sessions and projects opens below a pane, while pretty much everything else opens to the right. investigate the open-location logic across the app and lets try to make it logical and consistent.

## Plan
**Goal.** Every surface Relay opens *beside* an existing pane (not an explicit split/move/drag) lands in the same place by the same rule.

**Findings** (`src/RelayWindow.h`, `src/RelayWindowSharing.cpp`). Two rules coexist in ~24 call sites of `insertBeside`:
- *Always right* (`Qt::Horizontal`): files/documents, forks, background panes, Settings, Models, turn pane, tests card, joined share, Switchboard, notification source, remote board.
- *Right only if the anchor is >= 900 px, else below*: Sessions & Projects, subagent tabs, ⓘ Info, Review, Test suites, Profile, Approvals, Sharing, shared-pane dialog. 900 px was copied from the Switchboard's own list/card threshold, so in any two-pane layout under ~1800 px these open below — which is what the user saw.

**Steps.**
1. `relay::panes::dockOrientation(anchorWidth)` in `PaneLayout`: right when the anchor is at least `kDockBesideMinWidth` (600 px: two panes of 300 px), below otherwise. Unit test in `tests/panelayout_test.cpp`.
2. `RelayWindow::dockBeside(anchor, pane)` wraps `insertBeside(..., dockOrientation(anchor->width()), false)`.
3. Every open-a-surface call site uses `dockBeside`. Unchanged on purpose: explicit direction (split keys, move keys, drag, beneath-dock chord), the terminal that replaces a closing tool pane, and Execute/Verify docks beside the Switchboard (they carry the board's width floor).

**Risks.** Surfaces that always went right now go below in a pane under 600 px — the same rule for all, where splitting would leave two unusable slivers.

**Verify.** `ctest -R panelayout`; build; open Sessions and Settings from a ~700 px pane and see both open to the right.

## Done means
- Every pane Relay opens *for* another pane (Sessions & Projects, Settings, Models, a file, Review, Test suites, Profile, Info, Approvals, Sharing, subagents, forks, Switchboard) docks by one rule: to the right, beneath only when the anchor is under 600 px.
- From a two-pane layout (~700 px each) Sessions opens to the right, like Settings does. Failure: it still opens below.
- Explicit splits, moves and drags still go where they are aimed.
- Adding any pane to a tab (terminal, Sessions, Settings, a file, Execute beside the Switchboard) leaves the tab evenly spread, the Switchboard at its floor. Failure: opening Sessions beside one pane of three leaves the other two at their old widths.
- Moving, dragging and closing a pane keep hand-set sizes (closing: the rest grow in proportion).

## Execution Summary
Commit `0c853332`. `relay::panes::dockOrientation(width)` (`src/PaneLayout.{h,cpp}`, `kDockBesideMinWidth = 600`) and `RelayWindow::dockBeside(anchor, pane)` (`src/RelayWindow.h`). All 23 open-for-a-pane sites now call it: 22 in `RelayWindow.h` (openPath, openDocument, forks, background panes, Settings, Models, subagents, turn pane, Info, Review, Test suites, Profile, tests card, Sessions & Projects, Approvals, joined share, shared-pane dialog, Switchboard, notification source, remote board) and Sharing in `RelayWindowSharing.cpp`. Left as they were on purpose: split/move keys, drag, the beneath-dock chord, the terminal that replaces a closed tool pane, and the Execute/Verify docks beside the Switchboard (they carry its width floor). Not yet seen in the running app; that is the verifier's `person` check.

Space (commit `e52c90de`): `RelayWindow::spreadAfterAdding(pane)` queues `equalizePage` (Switchboard floor included) and is called on every add path: `dockBeside`, `splitToward` (replacing #EQM2's inline copy), the ←↑↓ placement, the terminal Sessions restores when none is left, and the three Execute/Verify/Try it docks beside the Switchboard. Moves, drags, the beneath-dock chord and closing are unchanged (keep hand sizes / proportional). The model is the comment block "how space moves when the panes in a tab change" in `src/PaneLayout.h`.

## Tests
`ctest --test-dir build -R panes`
`build/relay-panes-tests openedPanesDockRightUnlessTooNarrow`
manual: from a ~700 px pane, open Sessions (Ctrl+Shift+S), Settings and a file; each docks to the right and the tab is evenly spread

## Planning notes
2026-09-24, owner: "also lets take this opportunitty to organize how space is reallocated in a window when a new pane is added"

How space moves today, measured:
| Event | Space |
|---|---|
| New terminal (split key, + button) | whole tab equalized (#EQM2) |
| Pane opened for a pane (Sessions, Settings, file, Review, …) | only the anchor's share halved; the rest keep their sizes |
| Execute / Verify / Try it beside the Switchboard | anchor's share halved, with the board's floor |
| ←↑↓ placement after a new pane | anchor's share halved, no re-spread |
| Move keys, drag | anchor's share halved (owner 2026-09-19: keep hand sizes) |
| Close | Qt gives the freed space to the rest in proportion (probe: 596:298 → 797:399) |
| Restore closed | the recorded sizes |

So opening Sessions and opening a terminal share space differently. Model adopted: **adding spreads, rearranging keeps, closing gives back in proportion.** Every pane that is *added* to a tab (new terminal, anything opened for a pane, Execute/Verify/Try it, the ←↑↓ placement that finishes an add) equalizes the tab with the Switchboard floor, as #EQM2 already does for terminals. Moves, drags and closing keep the person's sizes. Written down in `src/PaneLayout.h`.
