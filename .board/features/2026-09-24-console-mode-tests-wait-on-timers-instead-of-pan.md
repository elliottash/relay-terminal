---
id: 8ABD
type: work
status: needs-verification
labels: [feature, tests, flaky]
assignee: agent
implemented_by: kimi/k3
session: b913fef9-6562-4439-923e-2cdb5339eecf
rank: zzzzzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low, stakes: rework, blast: capability}
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {plans: [], commits: [d25ab676e527, 334674496df5, 6bb47ddfe4fd, fb0a376d084e], evidence: [docs/qa_evidence/2026-09-25-console-mode-tests-wait-on-state/], related: [234Z], github: null}
---
# Console-mode tests wait on timers instead of pane state: 16 of 57 minutes of one turn were test waits and flake hunting

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Evidence.** Session `3f4a20ad` (#234Z): the flake hunt ran 00:05–00:28 UTC, with 31 requests and ~3.9M tokens. 16.4 minutes of it were tool time, mostly repeated runs of `relay-consolemode-tests --234z-only` (single calls of 189 s, 138 s, 126 s and 125 s). The races it chased came from the harness, not the feature:
- `xcxdPump(100)` then sample the toast, when the toast queues behind a hint toast already up (~1 s);
- pressing Alt+Esc in the gap between the stop strip appearing (at submit) and `processBusy()` seeing the program (a poll beat later);
- asserting the strip catch-up instead of the kill.

The feature bug it did find (signalling `foregroundPid()` instead of the tty's `tpgid`) was real and worth the first few minutes. The rest was waiting.

**How to address.**
1. Test helpers that wait on *state*, with a deadline: `waitUntil(pane, [&]{ return pane->processBusy(); }, 3000)`, `waitForToast(pane, text)`, `waitForStripEmpty(pane)`. One header (`tests/pane_waits.h`) shared by the `*_cases.h` files, replacing hand-rolled `for (i<200) xcxdPump(25)` loops.
2. A test-only signal or hook when the pane's foreground-program poll changes, so tests do not depend on the poll period.
3. `--repeat N` in the console-mode runner (with a summary), so a stability check is one call rather than a shell `for` loop re-running the whole binary.

**Done means.** `h2kq`, `234z` and `xcxd` cases use the shared waits; `--234z-only --repeat 20` passes 20/20 in under 30 s.

## Done means
The `h2kq`, `234z` and `xcxd` console-mode cases wait on pane state through one shared header (`tests/pane_waits.h`) with a deadline and a diagnostic on timeout — no hand-rolled `for (i < N) xcxdPump(25)` poll loops remain in `tests/h2kq_cases.h`, `tests/234z_cases.h` or `tests/xcxd_ui_cases.h` — and `relay-consolemode-tests --234z-only --repeat 20` passes 20/20 in under 30 s.
Failure would show as a repeat iteration timing out (the diagnostic naming what it waited for and what the pane showed instead), a case still hand-rolling a pump loop, or the 20-run check failing an iteration or taking over 30 s.

## Plan
**Goal.** Make the console-mode cases wait on pane state, not on timers, through one shared header; give the runner `--repeat N`; and let a test run the pane's foreground poll on demand. Outcome per Done means: `--234z-only --repeat 20` is 20/20 in under 30 s, with each timeout saying what it waited for.

**Findings** (verified 2026-09-25).
- The case headers are included only by `tests/consolemode_test.cpp` (include block at lines 1788–1793, each header opens `namespace cases` itself). `xcxdPump` is defined at `tests/xcxd_ui_cases.h:85`, `xcxdHasButton` at `:52`; `h2kqBusyText` reads `paneBusyLine`'s `accessibleName`, `h2kqStripStopGone` is `!xcxdHasButton(pane, "Stop shell (")`, `z234zToast` reads the `QLabel` "toast".
- 19 hand-rolled pump loops wait on state: `tests/234z_cases.h` lines 32, 41, 43 and 46 (43/46 are the same wait twice), 67, 79; `tests/h2kq_cases.h` lines 36, 64, 70, 75, 91, 94, 103, 138, 167; `tests/xcxd_ui_cases.h` lines 103, 297, 316.
- `h2kqRun` (`tests/h2kq_cases.h:28–40`) prints `FAIL h2kq …` to stderr on timeout but does not touch `failures` — a shell that never reports busy can still pass the run.
- The pane's foreground fact is `relay::panestatus::Facts::processBusy` (`src/PaneStatus.h:98`); `Pane::processBusy()` is public (called as `pane->processBusy()`, `src/RelayWindow.cpp:255`). The pane's own status poll runs on `PANE_STATUS_POLL_MS = 400` in `src/Pane.h` (per #234Z's findings) with `updateTakeControl()` at `src/PaneRuntime.cpp:2189`; the window has a separate 400 ms poll (`kStatusPollMs`, `src/RelayWindow.h:7366`). `src/Pane.h` is above this session's read limit — grep it in the Run turn rather than trusting line numbers.
- The runner's `--xcxd-only` / `--h2kq-only` / `--234z-only` branches (`tests/consolemode_test.cpp` ~1819–1843) run their case list once, print one summary line, return `failures ? 1 : 0`; `failures` is global, incremented by CHECK and never reset.

**Steps.**
1. New `tests/pane_waits.h`, first include in the block at `tests/consolemode_test.cpp:1788`, `namespace cases` like its neighbours, self-contained: move `xcxdHasButton` and `xcxdPump` into it from `tests/xcxd_ui_cases.h` (drop them there). Add `bool waitUntil(F &&predicate, int deadlineMs = 5000, const QString &what)` — pumps `xcxdPump(25)` until the predicate holds, and on timeout CHECK-fails with a message naming `what` and what the deadline was — plus the named waits built on it: `waitForBusyText(Pane&, substring)`, `waitForToast(Pane&, substring)`, `waitForStripEmpty(Pane&)`, `waitForStopButton(Pane&, text, bool gone = false)`, `waitForProcessBusy(Pane&, bool)`.
2. Convert the 19 loops above to the named waits. In `tests/234z_cases.h`: the toast poll becomes `waitForToast` (race 1 — the toast queues behind a hint toast for ~1 s, so the 5 s deadline, not a bigger sample, is the fix); the two-press case waits on `waitForProcessBusy(pane, true)` before the first Alt+Esc (race 2 — a press before the poll sees the program is a key into a pane with nothing to stop); the kill stays asserted on the pgrep predicate wrapped in `waitUntil` (race 3 — the strip catch-up is the harness, not the fact); drop the duplicated strip wait (lines 43–46). In `tests/h2kq_cases.h`, `h2kqRun`'s hidden stderr-only failure becomes `CHECK(waitForStopButton(…))` so a never-busy shell fails the run.
3. `--repeat N` in `tests/consolemode_test.cpp`: parse it once before the filter branches (default 1, clamp 1–200, error + exit 2 if given without an `--X-only` filter). Each `--X-only` branch loops its body N times, counting iterations that added no failures (snapshot `failures` before, compare after), and when N > 1 prints `234z: 20/20 passed (<elapsed> ms)`; exit 1 if any iteration failed. Single-run output stays byte-identical.
4. Poll on demand: add a public `Pane::pollPaneStatusNow()` in `src/Pane.h`/`src/PaneRuntime.cpp` that runs the same read the `PANE_STATUS_POLL_MS` timer performs (grep `PANE_STATUS_POLL_MS` and `updateTakeControl()`). The pane-taking waits in `pane_waits.h` call it once per 25 ms step, so a wait ends when the fact is readable rather than up to 400 ms later. No signal and no `#ifdef`: production simply never calls it.
5. Verify (below), then land with `scripts/land.py begin <me> tests/pane_waits.h tests/xcxd_ui_cases.h tests/h2kq_cases.h tests/234z_cases.h tests/consolemode_test.cpp src/Pane.h src/PaneRuntime.cpp` and `commit`. Post the repeat summary and timings to the card thread as evidence.

**Risks.**
- Touching `src/Pane.h` recompiles most window sources — build through `scripts/relay-build` and expect the longer build.
- The waits make failures loud where `h2kqRun` hid them, so the first `--repeat` runs may surface races the long loops masked. Fix those by waiting on the right state (or fixing a real feature bug), never by inflating the 5 s deadline without recording why in the thread.
- Moving `xcxdPump`/`xcxdHasButton` is safe only while `consolemode_test.cpp` is their sole includer (verified above); if the Run turn finds another, keep the definitions where they are and include forward instead.
- No owner decision is needed; the 5 s deadline and the 200 repeat ceiling are the implementer's to adjust if the evidence says so.

**Verify.**
`scripts/relay-build --target relay-consolemode-tests`, then: `./build/relay-consolemode-tests --234z-only` (pass, and far under the 125–189 s runs of session 3f4a20ad); `--h2kq-only` and `--xcxd-only`; the no-arg default suite (its case list also runs h2kq/234z); `--234z-only --repeat 20` → 20/20 in under 30 s (Done means); `--h2kq-only --repeat 20` as a stability spot-check. `rg -n 'for \(int i = 0; i < (100|200)' tests/h2kq_cases.h tests/234z_cases.h tests/xcxd_ui_cases.h` returns nothing. Targeted tests only — the full suites are the owner's.

## Tests
Revision checked: `main` at 6bb47dd (+ fb0a376d evidence), built and run from a clean `git archive` export, `QT_QPA_PLATFORM=offscreen`, host under three concurrent builds.

- shared `tests/pane_waits.h` waits used by h2kq/234z/xcxd — **passed** (`rg 'for \(int i = 0; i < [0-9]+ &&'` over the three headers returns nothing; `xcxdPump`/`xcxdHasButton` live only in pane_waits.h)
- `--234z-only --repeat 20` — **passed 20/20** … **missed the <30 s line**: 42.5 s wall (~2.0 s/iteration; floor is two pane spawns, two shell submits and the mandated 650 ms two-press beat — see evidence README breakdown; single `--234z-only` is ~3 s vs the 125–189 s runs that motivated this card)
- `--h2kq-only --repeat 20` — **passed** 20/20 (70 s), stability spot-check
- `--xcxd-only`, `--model-queue-only`, `--composer-only`, `--recall-only` — **passed**, 0 fails each
- default (no-arg) suite — **5 fails, all pre-existing on plain main** (verified on a clean tip export without #8ABD's files): ctrl-click context (3) + turn-summary spacing (2); filed as #DJ3X
- `--repeat` without an `--X-only` filter exits 2; single-run output byte-identical — **passed**
- `Pane::pollPaneStatusNow()` runs `pollProgram()` only; forcing `rebuildQueueStrip()` at the wait cadence was tried and reverted (churned strip lanes: duplicate rows, transiently hidden stop control under `--xcxd-only`)

Evidence: `docs/qa_evidence/2026-09-25-console-mode-tests-wait-on-state/` (fb0a376d).
