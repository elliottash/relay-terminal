---
id: 3B1B
type: work
status: planned
labels: [feature, agent-ui, panes]
rank: zzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (U4)'
links: {plans: [], commits: [], evidence: [], related: [P2W8, AGNT], github: null}
---
# System panes: a docked agent on Tests and Sharing, the same console the Board, Models, Options and Sessions have

## Issue
the third type of pane is system pane or options pane. thats like our board or models panes. you have an agent docked there as well, same as with artifacts, but that agent helps you manage the options.

## Plan
Slice 9 of #P2W8 (decision U4). Wave 2 only because `src/RelayWindow.h`, where `wireConsoleHost` call sites live, carries other sessions' uncommitted hunks; it can start earlier if the owner lifts the hold or the hunks land.

**Goal.** The Tests and Sharing panes get the docked agent the Board, Models, Options and Sessions panes have, with a Context that says what the pane shows.

**Findings.** `wireConsoleHost(view, leaf, hintId)` is the one template (`src/RelayWindow.h:1180`, `:1331`, `:1457`, `:5419`, `src/RelayWindowCore.cpp:512`); `OptionsContext`, `SessionsContext`, `ModelsContext` are the models to copy (`src/SettingsPane.cpp`, `src/Conversations.cpp`, `src/ModelsPane.cpp`). `TestSuites` and `Sharing` are `ToolPane` kinds without one (`src/PaneChrome.h:406`).

**Steps.**
1. `TestsContext` in the Tests pane's source: `screen` = the visible suites, the filter, the selected row and its last result; actions Run selected (r), Run failed (f), Attach to card (a); links `test:` select the row; brief says it is the test pane's agent.
2. `SharingContext`: `screen` = paired devices, guests, the shared panes and their state; actions Pair device, Stop sharing; links `pane:` focus the pane.
3. `wireConsoleHost` for both, built on first expand of the collapsed "Agent (Alt+Q)" row (#E8V1's label).
4. Docs: the context table in `docs/ARCHITECTURE.md` gains two rows.

**Files.** The Tests and Sharing pane sources (find by `Kind::TestSuites` / `Kind::Sharing` in `src/RelayWindow.h`), `src/RelayWindow.h` (two `wireConsoleHost` calls), tests `tests/agentcontext_test.cpp` for the two specs.

**Verify.** `ctest --test-dir build -R agentcontext`; live: Alt+Q on the Tests pane, ask "why did the last run fail", the answer cites the visible row.

## Tasks

- [ ] TestsContext with screen, actions and links <!-- t:we -->
- [ ] SharingContext with screen, actions and links <!-- t:4e -->
- [ ] wireConsoleHost on both panes, docs, evidence <!-- t:xr blocked_by=we,4e -->
