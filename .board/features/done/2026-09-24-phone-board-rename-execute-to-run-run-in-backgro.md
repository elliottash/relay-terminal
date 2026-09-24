---
id: E728
type: work
status: done
labels: [feature, remote, switchboard]
assignee: agent
implemented_by: kimi/kimi-k3
verified_by: kimi/kimi-k3
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Phone board: rename Execute to Run, run in background

## Issue
in the phone system, replace execute with run. and it should run in background.

## Done means
- The phone web UI shows **Run** where it showed **Execute**: the board card action button (and its hint/offline/sending strings) in `app/board.js`, and the plan-run button in `app/app.js`. No user-visible "Execute" string remains in `app/`.
- Running a card from the phone no longer switches the desktop's current tab or takes keyboard focus: the pane opens and, once its agent accepts the task, moves to the background window, exactly like the desktop's Run (src/BoardPane.cpp `execute(true)`).
- The wire protocol is unchanged (`board_action` `execute`, `plan_execute`): the desktop keeps accepting the existing action names from paired phones.
- Failure looks like: any 'Execute' label still rendered in `app/`, or a phone Run yanking the desktop's tab/focus.

## Plan
**Goal:** phone UI says Run, and a phone-triggered run works in the background (the desktop half of #BGRN's rename).

**Findings:**
- `app/board.js:865` — card action button labelled 'Execute' (with 'Tap Execute again…', 'Offline — Execute needs your desktop', 'Asking your desktop to execute…' strings nearby); `app/outbox.js:39` comment.
- `app/app.js:1702` — plan-run button labelled 'Execute'.
- Desktop already renamed: src/BoardPane.cpp `execute(bool background = true)`, tooltip "Run the card in the background…"; `onExecuteCard(id, task, runInBackground)` in src/RelayWindow.h:5300 marks the pane as a background task and moves it to the background window when the agent accepts.
- Phone path: `BoardRemote::handleBoardAction` (src/BoardRemote.cpp) → `host.executeCard` → `RelayWindow::remoteCardPane` (src/RelayWindow.h:5166) — calls `m_tabs->setCurrentWidget(page)`, and its no-board-pane fallback does `setActive` + `focusLeaf`: foreground. Its board-pane branch already passes `true` for background.

**Steps:**
1. `app/board.js`: rename button label and the three user-visible strings; update the two comments that say Execute.
2. `app/app.js`: plan-run button label → 'Run'; `app/outbox.js` comment → Run.
3. src/RelayWindow.h: factor the background-when-working timer out of `onExecuteCard` into a helper; give `remoteCardPane` a `background` flag (execute passes true, verify keeps foreground) that skips `setCurrentWidget`, and in the fallback branch marks the pane as a background task and moves it to the background window once the agent is working.
4. src/BoardRemote.cpp: refresh the 'the phone says Execute' comment.
5. Build (`scripts/relay-build --target relay`), run `ctest -R boardremote`, land with `scripts/land.py`.

**Risks:** verify-from-phone shares `remoteCardPane`; scope says run only, so verify keeps its current foreground behaviour. Other sessions have uncommitted work in this tree — land via `scripts/land.py` with explicit paths.

**Verify:** `ctest --test-dir build -R boardremote`; `rg -n "'Execute'|\"Execute\"" app/` returns nothing; build of `relay` target passes.

## Execution Summary
Landed as `9507ab03` (2026-09-24), build-gated by land.py (the exact committed tree builds `relay`).

- `app/board.js` — the board card action button now reads **Run** (was Execute); its hint, offline and sending strings updated ("Tap Run again…", "Offline — Run needs your desktop.", "Asking your desktop to run…"). Comments updated.
- `app/app.js` — the plan-run button now reads **Run**.
- `app/outbox.js` — comment updated.
- `src/RelayWindow.h` — `remoteCardPane` gained a `background` flag: `host.executeCard` passes `true` (a phone Run no longer switches the desktop's tab, does not take the focus, and the pane moves to the background window once its agent accepts the task); `host.verifyCard` passes `false` (Verify keeps the focus, unchanged). The move-to-background-when-working timer was factored out of the `onExecuteCard` lambda into `RelayWindow::backgroundPaneWhenWorking(Pane *)`, shared by both paths. The wire protocol (`board_action` `execute`, `plan_execute`) is unchanged, so already-paired phones keep working.
- `src/BoardRemote.cpp` — comments updated to the Run naming.
- `tests/boardremote_test.cpp` — claim-line expectation updated to `Run on #K7Q2 from iPhone` (stale since #BGRN changed the string; the test was red at HEAD).

Pre-existing failure found and filed, not fixed here: `ctest -R boardexecute` is red at HEAD because the desktop card page's action row is no longer child buttons of the view since #BGRN — filed as #DEH6 with the measured evidence.

## Tests
- `ctest --test-dir build -R boardremote` — **pass** (5/5, incl. the Run claim line from a phone).
- `rg -n "Execute" app/board.js app/app.js app/outbox.js` — no matches: no Execute string left in the phone UI.
- land.py verify build: the exact committed tree compiles the `relay` target — **pass**.
- Note: `ctest -R boardexecute` was already red at HEAD before this change (desktop card page, #BGRN fallout) — filed as #DEH6, out of scope here.
