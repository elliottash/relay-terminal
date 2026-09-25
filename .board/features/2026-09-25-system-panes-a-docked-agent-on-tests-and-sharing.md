---
id: 3B1B
type: work
status: needs-verification
labels: [feature, agent-ui, panes]
assignee: claude-code
rank: zzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (U4)'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-system-panes/], related: [P2W8, AGNT], github: null}
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

- [x] TestsContext with screen, actions and links <!-- t:we -->
- [x] SharingContext with screen, actions and links <!-- t:4e -->
- [x] wireConsoleHost on both panes, docs, evidence <!-- t:xr blocked_by=we,4e -->

## Execution Summary

Both contexts live in `src/SystemContexts.{h,cpp}` (`relay::agent::TestsContext`,
`relay::agent::SharingContext`, on the #AGNT `Context` base), hosted by a shared
`src/ContextDock.{h,cpp}` — a dock shaped like `ArtifactDock`, collapsed to an
"Agent (Alt+Q)" row until first expand, which is when the console host is built.

- `TestsContext`: the screen is the pane's model state — the filter, the selected row with
  its last result (`Selected: <id> · last result pass · 2 d ago · 50% reliable · gone`,
  plus `Last failure` and `Run it with:`), and the summary line (`5351 tests · 2052 passed
  (37 slow) · 2 failed · 3297 never run · 2.6 m`, with `Failing on screen:` naming the
  failing rows, long lines cut to 180 chars per row so the screen stays under the limit).
  Actions: Run selected (r, needs a selection), Run failed (f, only while a failing test is
  on screen), Attach to card (a, only with a workspace board). Links: `test:` ids select
  their row and claim nothing else. `runStarted` clears the selection's failure line.
- `SharingContext`: the screen is the pane's share state — `Sharing › People`, remote
  control, the opener (`Opened from: pane:<token>`), `Paired devices:` and
  `Shared panes:` with each pane's token and state, `Guests:`. Actions: Pair device (p),
  Stop sharing (e — only while a pane is shared). Links: `pane:` tokens focus the pane.
- Wiring: `createTestSuitesPane` and `openSharingPane` call `wireConsoleHost` with
  `tests.ask` / `sharing.ask`; `RelayWindow::focusAgentOn` expands the pane's own dock
  instead of the Board's panel when the Tests or Sharing pane has focus; the
  `onShortcutHint` note on both panes names the dock.
- Output links (`src/OutputLinks.{h,cpp}`): `test:` and `pane:` became link kinds, with
  context resolvers so a `test:` in any output selects the row in the Tests pane and a
  `pane:` token focuses that pane.
- Backend (`backend/relay_core/agent_context.py`): `tests` and `sharing` briefs — one
  paragraph each, into the system prompt once; `docs/AGENT-SESSIONS-PROTOCOL.md` names
  both. `docs/ARCHITECTURE.md`: the context table gains the two rows, and the
  `wireConsoleHost` prose covers the two system panes.

## Tests

- `ctest --test-dir build -R agentcontext` — pass. New cases:
  `theTestsContextIsAConsoleAboutTheSelectedRow`, `theTestsActionRowFollowsTheSelectionAndTheRun`,
  `aTestLinkSelectsItsRowAndNothingElseIsClaimed`, `theSharingContextNamesEverySharedPaneByItsToken`,
  `endSharingIsOfferedOnlyOnASharedPaneAndAPaneLinkFocusesIt`,
  `aLongFailureIsCutAndTheScreenStaysUnderTheLimit`.
- `ctest --test-dir build -R "testsuites|^sharing$|outputlinks"` — pass; the panes'
  `theDockedAgentIsAboutTheSelectedRow` / `theDockedAgentReadsThePaneAndEndsOnlyASharedOne`
  and the output-links cases for `test:` / `pane:` run there.
- `PYTHONPATH=backend python3 -m pytest tests/test_agent_context.py` — 28 passed (the
  `tests` and `sharing` briefs).
- Live pass under Xvfb `:109` with an isolated profile:
  `docs/qa_evidence/2026-09-25-system-panes/` — Alt+Q on both panes, and the answer to
  "why did the last run fail?" cites the pane's screen (the selected row, its last result,
  the summary and the failing rows), echoed by the stub provider and logged in
  `stub-requests.log`.
