---
id: HJ1T
type: work
status: needs-qa-llm
labels: [bug, models, ui, performance]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 3eb5e6f8-b4a5-4638-8963-891c0e17b166
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: Owner in a Relay guest session (Claude Code), 2026-09-23
links: {plans: [], commits: [d6f769f6d5444704e25a7ea1478649f97c2ed12f], evidence: [issues/changes/2026-09-23-models-pane-checkboxes-lag-every-tick-fans-out-t.md, docs/qa_evidence/2026-09-25-verify-HJ1T], related: [00G1], github: null}
---
# Models pane checkboxes lag: every tick fans out to every pane synchronously

## Issue
they are just not intuitive, and they are actually slow as well, the checkboxes are not responsive and they are laggy. and its just a small and simple finicky table.

## Done means
Ticking a box on Models › Available (or a Priorities cutoff) flips the tick at once, with no stall. A burst of ticks triggers one settings redraw and one per-pane tiers re-send, after the last tick. Failure shows as the click hanging before the tick flips, or as other panes never picking up the change.

## Execution Summary
Cause (read from code, not profiled): every list edit calls `RelayWindow::modelsCurated()` synchronously from the click via `ModelPicker::changed()` → `ModelsPane` → `Pane` target `listsChanged` → `onProfileApplied`. That call redrew every settings page (`SettingsWatch::notify`) and, for every pane, rebuilt its catalog, rewrote `models/fallbacks`, re-sent `tiers` to its worker and refreshed its pickers, once per tick.

Fix (`d6f769f6`, `src/RelayWindow.h`): `modelsCurated()` now restarts a 200 ms single-shot timer on the window, and `modelsCuratedNow()` does the fan-out. The edit itself is already in QSettings and the picker repaints its row in place (`refreshAvailability`), so only the notification waits. Every other caller (Options rows, `/profile`, fill-from-defaults) goes through the same path and is coalesced the same way.

Not yet measured: no before/after timing. The owner's hands-on check after restarting Relay is the proof that matters.

## Tests
- `python3 scripts/land.py commit` exact-tree build of `relay`: passed (d6f769f6).
- `xvfb-run -a build/relay-modelspane-tests -silent`: 23 passed, 1 failed (`everyTabKeepsItsMainControlsInANarrowPane`, tab-bar width). The failure predates this change: `ModelsPane` is untouched here.
- `xvfb-run -a build/relay-settings-tests -silent`: 40 passed, 8 failed. These are all source-text tests that look in `RelayWindow.h` for bodies #243T moved to `.cpp` files, and none reads `modelsCurated`. They predate this change and are filed separately.
- manual: restart Relay, open Models › Available, tick several boxes quickly. Each tick should flip immediately, and the other panes' boxes should pick up the change about 0.2 s after the last tick.

## QA checklist
Verified 2026-09-25 by a verifying session at rev `540185020a18ecc15080849f99b03c9e8dcff9ea` (HEAD; fix commit `d6f769f6` is an ancestor). Evidence: `docs/qa_evidence/2026-09-25-verify-HJ1T/` (screenshots, before/after settings files, NOTES.md).

**Done means, item by item:**
- "Ticking a box flips the tick at once, with no stall" — **passed**. Live drive under Xvfb with an isolated profile: one click on the claude-haiku-4.5 box flipped the row to "Off" and rewrote `models/available` in the isolated `relay.conf` (see `06-check.png`, `relay.conf.before`).
- "A burst of ticks triggers one settings redraw and one per-pane tiers re-send, after the last tick" — **passed**. Code at HEAD: `src/RelayWindow.h:2629` `modelsCurated()` only restarts a 200 ms single-shot timer; `modelsCuratedNow()` is the sole fan-out; every call site (`RelayWindowModels.cpp:42,915`, `RelayWindowCore.cpp:1064,1214`) uses the coalescing form. Live: 4 ticks in 622 ms, each flipped in place and stayed flipped, final settings file matches the UI exactly (`07-after-burst.png`). No stall, no flip-back — the reopened #00G1 symptoms.

**Tests, line by line:**
- land.py exact-tree build — **not applicable now** (superseded); `scripts/relay-build --target relay` at HEAD: built clean in 40 s.
- `xvfb-run -a build/relay-modelspane-tests -silent` — **passed with a note**: 25 passed, 1 failed. The failure (`theHelperIsOneRowUnderAllFiveTabsAndBuildsOneConsole`: expected "Helper Agent (Alt+Q)", actual "Agent (Alt+Q)") is caused by later commit `e6febb9f` (card #E8V1) renaming the button without updating `tests/modelspane_test.cpp:782`; not by this change. The narrow-pane failure the card recorded now passes.
- `xvfb-run -a build/relay-settings-tests -silent` — **passed with a note**: 51 passed, 1 failed (same #E8V1 cause, `tests/settingspane_test.cpp:1607`). The 8 failures the card recorded have been fixed since.
- Manual tick-burst check — **passed** (live drive above).

Unresolved: nothing for this card. The stale #E8V1 tests are reported separately (they fail on two suites but are unrelated to this fix). This verification also covers the reopened #00G1 report ("laggy clicks, controls flipping back"): neither symptom reproduced.

Reviewed 2026-09-25 by the verifying session (qa-verify-HJ1T), rev `54018502`.
