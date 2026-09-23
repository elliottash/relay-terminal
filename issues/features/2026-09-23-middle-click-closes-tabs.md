---
id: 5Z6N
type: work
status: needs-verification
labels: [feature, gui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 49dbf51d-820d-4c71-8c2c-f411cacebb78
rank: m
created: '2026-09-23'
source: Elliott in Relay, 2026-09-23
links: {plans: [], commits: [], evidence: [tests/themetabbar_test.cpp], related: [], github: null}
---
# Middle-click closes tabs

## Issue
middle click should close tabs

## Done means
- Middle-clicking a visible top-level Relay tab closes that tab through the same confirmation and recovery path as its close control.
- Clicking the new-tab button, empty tab-bar space, or a pane-local tab does not close a Relay window tab.
- Closing the only tab retains the existing window-close behavior.

## Plan
1. Let `ThemeTabBar` identify middle-button releases on real window tabs.
2. Wire the tab index to `RelayWindow::requestCloseTab()` so all tab-close semantics remain centralized.
3. Build and run focused validation.

## Execution Summary
`ThemeTabBar` now reports a middle-button release only when it lands on a top-level window tab. `RelayWindow` routes that report through `requestCloseTab()`, retaining the existing close confirmation, backgrounding, restore, and last-tab behavior. `tests/themetabbar_test.cpp` covers a middle-clicked tab and a non-tab coordinate.

## Tests
- `scripts/relay-build --reconfigure --target relay-themetabbar-tests relay` — passed; application built.
- `ctest --test-dir build -R '^themetabbar$' --output-on-failure` — passed (1/1).
- Exact landed tree build by `scripts/land.py` — pending commit.
- Live GUI verification of the window close flow remains.
