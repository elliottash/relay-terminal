---
id: 9ACN
type: work
status: planned
labels: [bug, models, tests]
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: '#HJ1T session, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [N4PW, 00G1], github: null}
---
# Models pane narrow-layout test: the fifth tab overflows the tab bar

## Issue
(found by the #HJ1T session, not a user request) `xvfb-run -a build/relay-modelspane-tests -silent` on main at d6f769f6: 23 passed, 1 failed. `ModelsPaneTests::everyTabKeepsItsMainControlsInANarrowPane()`: `pane.tabBar()->tabRect(4).right() <= pane.tabBar()->width()` returned FALSE (tests/modelspane_test.cpp:235). The Jobs tab no longer fits in the narrow pane's tab bar.

## Done means
At a 420 px-wide Models pane, all five tabs (Sources, Enabled, Order, Effort, Roles — the narrow short labels) are fully visible inside the tab bar: no scroll buttons, no clipped or overflowing fifth tab, per #00G1's "no second row of tabs, no scroll buttons" requirement. The full set of narrow-pane controls keeps working as the existing test enumerates. Failure looks like: `xvfb-run -a build/relay-modelspane-tests -silent` failing `ModelsPaneTests::everyTabKeepsItsMainControlsInANarrowPane()` on `tabRect(4).right() <= tabBar()->width()`, or any of the five tabs unreachable at 420 px.

## Plan
**Goal** — make the five Models pane tabs fit their tab bar at a 420 px pane width again, so `ModelsPaneTests::everyTabKeepsItsMainControlsInANarrowPane()` passes on main.

**Findings**
- The test is `everyTabKeepsItsMainControlsInANarrowPane()` in `tests/modelspane_test.cpp` (currently ~line 236–291; the issue cites line 235 at d6f769f6). It resizes the pane to 420 px, shows it, asserts the short tab labels are applied, then asserts `tabBar()->tabRect(4).right() <= tabBar()->width()`.
- Narrow-label substitution lives in `ModelsPane::resizeEvent()` (`src/ModelsPane.cpp`, ~line 450): below 500 px, tabs 2 and 4 become "Order" and "Roles". This still works — the failure was on the geometry assert, not the labels assert.
- Tab chrome comes from the global stylesheet `QTabBar::tab { padding: 5px 8px; margin: 3px 1px 0 1px; … }` in `src/Theme.cpp` (~line 642). That is ~18 px of horizontal chrome per tab, applying to every QTabBar in the app.
- The tab bar is built in `src/ModelsPane.cpp` (~line 160) with objectName `modelsPaneTabs`, `setExpanding(false)`. Nothing scopes narrower padding to it.
- The test file has moved since d6f769f6 (line 235 → ~291), so main has changed around this test; the jobs tab also saw later work (see #N4PW's thread: 8bf46bde, e9f709ef, 43be0b0a). The overflow may already be fixed, or the cause may be in those commits.

**Steps**
1. Reproduce on current main: build, then `xvfb-run -a build/relay-modelspane-tests -silent -functions` (or the single test via QTest's argument form `… everyTabKeepsItsMainControlsInANarrowPane`). If it passes on main now, close this card as already-fixed with the passing run as evidence and stop.
2. If it fails, diagnose before changing anything: in the failing state, print each `tabRect(i)` width and `tabBar()->width()` (a temporary qDebug in the test or a one-off probe). Identify what is eating the width: per-tab padding/margins from the stylesheet, font metrics, or a tab-bar width smaller than expected.
3. Fix, in order of preference (pick the first that passes without touching other widgets):
   a. Scope a narrower tab padding to this bar only in `src/Theme.cpp`, e.g. `QTabBar#modelsPaneTabs::tab { padding: 5px 6px; margin: 3px 1px 0 1px; }` — mirror of the earlier fix (af112403) that took 2 px per side to make five tabs fit.
   b. Shorten the narrow labels further in `ModelsPane::resizeEvent()` (e.g. "Prio", "Jobs"), keeping the test's expected-labels list in `tests/modelspane_test.cpp` in sync.
   c. Only if neither suffices: reduce the tab bar's own horizontal margins in the pane layout.
4. Do **not** touch the global `QTabBar::tab` rule in a way that changes other tab bars (Conversations, SettingsPane, SubagentTranscript, window tabs); do not enable scroll buttons or eliding — #00G1 forbids both.
5. If the test itself changed on main since d6f769f6 (its line numbers moved), read the current assertions before editing it; the test is the spec and only the labels list may change, in step 3b.

**Risks**
- A stylesheet rule for `#modelsPaneTabs` also affects any future pane reusing that objectName — none exists today; keep the selector exact.
- Shortening labels below "Order"/"Roles" hurts legibility; prefer 3a. No owner decision needed unless 3a/3b both fail.

**Verify**
- `xvfb-run -a build/relay-modelspane-tests -silent` — all pass, including `everyTabKeepsItsMainControlsInANarrowPane()`.
- Eyeball: run `./build/relay`, open the Models pane, drag it to ~420 px wide; all five tabs fully visible, no scroll arrows.
