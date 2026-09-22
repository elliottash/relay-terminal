---
id: P1CP
type: work
status: needs-verification
labels: [feature, gui]
component: [gui]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in a Relay pane, 2026-09-21'
links: {plans: [], commits: [5ce35f2d443b7ba68f95b4971d3cf86dd5fc01a8], evidence: [docs/qa_evidence/2026-09-21-pane-info-popover/], related: [], github: null}
---
# Put pane identity and dimming controls in the info hover popover

## Issue
i think we should show the pane id in the header, and clicking it copies it.

heres an alternative.

when you hoever over the circle i in the top right, it shows a toast with the pane id with the copy button next to it, which you can hover over and click to copy. would that work?

yes.

what do you think about showing the dim button on hover over (i) as well? i think most people wont use that so we shouldnt use real estate for it

## Done means
- Hovering or keyboard-focusing a terminal pane's circle-i opens an interactive popover showing that pane's eight-character ID and a Copy button.
- The popover remains open while the pointer moves between the circle-i and its controls; Copy places the exact displayed ID on the clipboard and confirms success.
- The existing Dim pane action lives in the popover instead of consuming permanent pane-chrome width; its Alt+D shortcut and dimming behavior remain unchanged.
- Clicking the circle-i still opens Conversation info.

## Plan
**Goal.** Move the two low-frequency pane utilities behind the existing circle-i without making its current click action harder to reach.

**Findings.** `RelayWindow::syncChrome()` inserts `sessioninfo::InfoButton`; `PaneChrome` currently constructs the permanent `pane.dimToggle` button; the visible pane ID used by logs and Board execution links is `Pane::sessionToken().left(8)`.

**Steps.**
1. Add a tested `PaneInfoPopover` beside `InfoButton`, with hover/focus persistence, pane-ID copying, and a Dim pane control.
2. Stop constructing Dim in the permanent chrome row; wire the popover's Dim control through the existing `pane.dimToggle` action and live state refresh.
3. Style and document the popover, then build and run the focused conversation/UI tests.

**Risks.** A hover surface can disappear while the pointer crosses the gap; use a short delayed close and re-check whether either surface is hovered or focused before hiding it. The circle-i click path must remain untouched.

**Verify.** `ctest --test-dir build -R '^conversations$'`; `scripts/relay-build`; an offscreen widget test exercises hover persistence, exact clipboard copying, dim dispatch, and the unchanged circle-i click.

## Tasks
- [x] Build and wire the interactive popover <!-- t:ui -->
- [x] Add focused regression coverage <!-- t:test -->
- [x] Build, verify, and record evidence <!-- t:verify -->

## Execution Summary
Added `sessioninfo::PaneInfoPopover`, opened by pointer hover or keyboard focus on a terminal pane's circle-i. It shows and copies the pane's eight-character ID, keeps itself open while its controls are in use, and hosts the existing manual Dim action. The permanent dim button was removed from `PaneChrome`; Alt+D and Alt+wheel still use the same dimming state and action paths. Clicking circle-i still opens Conversation info. Styling and architecture documentation were updated with the new surface.

## Tests
- `RELAY_SESSION=codex-pane-info scripts/relay-build --target relay`
- `RELAY_SESSION=codex-pane-info scripts/relay-build --target relay-conversations-tests`
- `ctest --test-dir build -R '^(conversations|themeswitch|panedimming)$' --output-on-failure`
- `scripts/relay-build --check 'Pane ID'`
