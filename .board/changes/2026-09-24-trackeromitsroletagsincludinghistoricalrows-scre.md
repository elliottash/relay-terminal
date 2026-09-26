---
id: HYBM
type: work
status: planned
labels: [bug]
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# trackerOmitsRoleTagsIncludingHistoricalRows screenshot test flakes in full-suite runs

## Issue
Unrelated fault noticed while landing #M5FZ: SubagentsTests::trackerOmitsRoleTagsIncludingHistoricalRows is flaky — it failed in one full-suite run of ./build/relay-subagents-tests (Compared QImages differ in size: actual historical 300x153 vs expected general 1499x153, tests/subagents_test.cpp:197) and passed when run alone and in the two following full-suite runs, same binary, no source change involved. Screenshot-comparison test on the subagent strip; looks order/timing dependent (a prior test's widget width leaking in).

## Done means
`SubagentsTests::trackerOmitsRoleTagsIncludingHistoricalRows` no longer depends on the host window manager: both strips render at the size the test sets, with no `show()`/window exposure in the render path. It passes alone, under `QT_QPA_PLATFORM=offscreen`, and in 10 consecutive full-suite runs of `./build/relay-subagents-tests` on the desktop where it previously failed roughly one run in three. Failure looks like today's symptom — `Compared QImages differ in size` (or pixels) in a full-suite run — or a blank/wrong-sized `RELAY_TRACKER_SCREENSHOT` capture, which would mean the windowless grab renders nothing.

## Plan
**Goal.** Make `SubagentsTests::trackerOmitsRoleTagsIncludingHistoricalRows` deterministic: the two tracker strips it compares must render at a size the test chooses, so a full-suite run cannot fail it by window-manager accident.

**Findings.**
- The test (`tests/subagents_test.cpp:207`) renders two `SubagentsPanel`s in a lambda that does `panel.resize(1000, panel.sizeHint().height()); panel.show(); QTest::qWaitForWindowExposed(&panel); return panel.grab().toImage();`, then `QCOMPARE(historical, general)`.
- `panel` is a top-level window, so `show()` hands its size to the host window manager. `minimumSizeHint()` returns `sizeHint()` (`src/SubagentsPanel.h:190`) and `sizeHint()` is width 200 (`src/SubagentsPanel.cpp:474`), so any WM width ≥ 200 is legal; `qWaitForWindowExposed` returns on *expose*, not on the WM's final size, and its return value is ignored. In the failing run the WM gave one window 1499 px and the other 300 (it failed full-suite only because the preceding tests map/unmap dozens of top-level windows that the WM is still settling).
- `SubagentsPanel::paintEvent` also branches on `hasFocus()` (`src/SubagentsPanel.cpp:762`) — a second WM-dependent input that can differ between the two renders even at equal size.
- Grabbing without a window is the established pattern here: `tests/diffview_test.cpp:190` ("the gutter paints without a window"), `tests/filterpopup_test.cpp:114`. `QWidget::grab()` renders the widget offscreen at its current geometry, so a hidden panel grabs at exactly the resized size, unfocused — identical on both sides of the compare.

**Steps.**
1. In `tests/subagents_test.cpp`, in the `render` lambda of `trackerOmitsRoleTagsIncludingHistoricalRows` (~line 220): delete `panel.show();` and `QTest::qWaitForWindowExposed(&panel);`, leaving `resize` + `grab().toImage()` untouched. Add a one-line comment at the grab saying why there is no window: size and focus must come from the test, not the WM (this card).
2. Nothing else. The `RELAY_TRACKER_SCREENSHOT` save still works — it saves the already-grabbed image, which now shows the unfocused strip (no focus band), which is what a doc screenshot wants.

**Orchestration.** None — one test file, two deleted lines.

**Risks.**
- A hidden-widget grab could render blank on some platform, making `QCOMPARE` pass vacuously; caught by the screenshot check in Verify.
- The flake is ~1-in-3, so 10 clean full-suite runs are evidence, not proof; the mechanism (no window in the render path) is the actual guarantee.
- No owner decision needed.

**Verify.**
- `scripts/relay-build --target relay-subagents-tests`.
- `./build/relay-subagents-tests trackerOmitsRoleTagsIncludingHistoricalRows`, and the same under `QT_QPA_PLATFORM=offscreen` — both pass.
- `RELAY_TRACKER_SCREENSHOT=/tmp/tracker.png ./build/relay-subagents-tests trackerOmitsRoleTagsIncludingHistoricalRows`; the PNG must be 1000 px wide and show three finished rows (non-blank grab).
- `for i in $(seq 1 10); do ./build/relay-subagents-tests >/dev/null || { echo "run $i failed"; break; }; done` — 10 clean full-suite runs on the desktop where it flaked.
