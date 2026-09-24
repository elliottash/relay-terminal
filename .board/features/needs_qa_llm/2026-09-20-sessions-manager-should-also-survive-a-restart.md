---
id: 8EXS
type: work
status: needs-qa-llm
labels: [feature, sessions]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Oz (Warp agent, GPT-5.1-class), 2026-09-23
rank: zzzzzzzzzzzy
created: '2026-09-20'
acceptance: quitting Relay with the Sessions & Projects pane open (on any of its tabs, with a search typed) and reopening it brings the pane back beside a terminal pane, on the same tab, with the same search
source: 'split out of #XAME (pane /deliver, 2026-09-22)'
links: {plans: [], commits: [], evidence: [], related: [XAME], github: null}
---
# Sessions manager should also survive a restart

## Issue
when you close and re-open, if the options (or other menu) was open, it should re-open where you were

## Notes
Split out of #XAME, which made the Options and Actions panes survive a restart (`settings` node in the saved layout, restored by `RelayWindow::buildNode`). The Sessions manager (`Ctrl+Shift+Y`, `ToolPane::Kind::Sessions`) is still transient — `ToolPane::node()` returns `{}` for `m_hosted`.

Restoring it is more than the same three lines: creation and wiring live inline in `RelayWindow::openSessionsFor` (helper panel, closed-list feed, `bindSessionManager` to an owner pane, resume/close callbacks), so a restored pane needs that factored into something `buildNode` can call once the pane is placed and an owner pane can be found in the page. The node shape would mirror `subagents`/`internals`: `{"sessions": {"cwd": ..., "query": ...}}`, bound to a neighbour via a queued link. That function was contested by another session on 2026-09-20, which is why this is its own card.

## Implemented
The node: `{"sessions": {"cwd": "...", "tab"?, "query"?}}` (`tab` omitted for the default "sessions" list, `query` omitted when empty) — documented in `src/WindowState.h` beside the `settings` node.

- `src/PaneChrome.h` `ToolPane::node()`: a `Kind::Sessions` branch ahead of the generic `m_hosted` early-return, reading the hosted `SessionManager`'s `currentTab()`/`query()`. The ⓘ Info pane, which shares the same constructor, is untouched and stays transient (out of scope for this card).
- `src/WindowState.cpp` `isUsableNode()`: a `sessions` case treated like `models`/`internals` — any object is usable, since the pane refills itself from the tab/query it is given.
- `src/RelayWindow.h`: `openSessionsFor` was split into `createSessionsPane(cwd)` (the `SessionManager`, its Projects/Globals tabs and every callback that resolves its owner dynamically through `workspaceOwner(guard)`, unchanged in substance from before), `linkSessionsPane(tool, owner)` (the parts that need an owner: the `workspaceOwner` property, `bindSessionManager`, the `onResume`/`onOpenInfo` overrides), and `finishSessionsTab(tool, tab, query)` (show the tab, restore the search, refresh). `openSessionsFor` now calls all three; `buildNode()` gained a `sessions` branch that calls `createSessionsPane` and queues `linkRestoredSessionsPane(tool, tab, query)`, which binds to the first terminal pane of the tab it landed in (the same rule `linkRestoredModelsPane` uses) and drops the pane if there is none (the same rule `linkRestoredInternalsPane` uses).

## Automated tests
- `tests/windowstate_test.cpp`: `sessions` node cases mirroring the existing `models` coverage — a usable node with/without `tab`/`query`, an unusable one, and survival inside a split through `usableWindows()`. `ctest --test-dir build -R windowstate`: 1/1 passed.
- `cmake --build build --target relay`: builds clean (one pre-existing, unrelated warning in `Pane::handleSessionEvent`).

## Live evidence
None captured this session — no GUI/Xvfb verification pass was run (unlike the sibling #64KE evidence under `docs/qa_evidence/2026-09-17-restore-windows/`). A QA pass should open Sessions & Projects on a non-default tab with a search typed, quit, restart, and confirm it reopens on that tab with the search restored, beside a terminal pane.

## QA checklist
1. Open Sessions (`Ctrl+Shift+Y`), switch to Projects or Globals, type a search on the Sessions tab. Quit Relay and restart with no arguments: the pane reopens beside a terminal pane, on the same tab, with the search restored (Sessions tab only — `query` is not meaningful on Projects/Globals).
2. A window whose only Sessions pane's tab has no terminal pane left in it: the Sessions pane is dropped rather than restored with nothing to serve, exactly as a saved Activity pane is.
3. Options/Actions (#XAME) and the other restorable panes (Models, Switchboard, Activity, Subagents, Explorer/Preview/Plan) are unaffected.
4. The ⓘ Info pane stays transient: it does not reappear after a restart even if it was open at quit.
