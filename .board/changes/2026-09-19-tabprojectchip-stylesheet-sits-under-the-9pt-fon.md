---
id: QAJQ
type: work
status: planned
labels: [bug]
rank: zzzzzzzzw
created: '2026-09-19'
source: 'pane 2 (execute #T7AH), 2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# tabProjectChip stylesheet sits under the 9pt font floor (buttonfit fails at HEAD)

## Issue
Unrelated fault noticed while executing #T7AH: `ctest --test-dir build` fails `buttonfit` at HEAD — `ButtonFitTest::stylesheetFontsStayAtOrAboveTheFloor()` reports `dark-copper: "font-size: 8.5pt" is under the 9pt floor`, from `QToolButton#tabProjectChip` at src/Theme.cpp:542 (present at HEAD 8b260cff, untouched by #T7AH's one-line terminal.conf diff). Measured: ctest run 2026-09-19, 1 of 63 tests failed; all other 62 pass.

## Done means
The `buttonfit` test suite passes at HEAD, in particular `ButtonFitTest::stylesheetFontsStayAtOrAboveTheFloor()`: every `font-size` declaration in every built-in theme's live stylesheet is in points and at or above `theme::FloorPt` (9pt). Failure is recognised exactly as originally reported: `./build/relay-buttonfit-tests` fails that test with `<theme>: "font-size: …" is under the 9pt floor`, and full `ctest --test-dir build` shows `buttonfit` as the one failing suite.

## Plan
**Goal** — Make `ButtonFitTest::stylesheetFontsStayAtOrAboveTheFloor()` pass at HEAD again by removing the sub-floor (8.5pt) `font-size` on `QToolButton#tabProjectChip`, or establishing that it has already been removed and closing with that evidence.

**Findings** — The original report (2026-09-19, HEAD 8b260cff): `src/Theme.cpp:542` held `QToolButton#tabProjectChip { … font-size: 8.5pt; }`, introduced by 7e3fb9ff (#916B); it was the only failure in 63 ctest suites. Planning re-check (2026-09-24, working tree): the selector `tabProjectChip` no longer appears anywhere in the workspace (`rg tabProjectChip` over `src/`, `tests/` and the repo root finds nothing), and all 57 `font-size` declarations in the current `src/Theme.cpp` are ≥ 9pt. So the most likely outcome is that the chip was removed or restyled in the five days since the report and the bug no longer reproduces; the floor test itself lives at `tests/buttonfit_test.cpp:124` and scans every built-in theme's live stylesheet plus per-widget `setStyleSheet` fonts in `src/*.{cpp,h}` (Theme.cpp excluded from the source scan).

**Steps**
1. Confirm the current state: `rg -n 'tabProjectChip' src/ tests/` and `rg -n 'font-size:\\s*[0-8](\\.[0-9]+)?pt' src/` — expect no sub-floor hits. Note the current HEAD sha.
2. Build through the wrapper (`scripts/relay-build`) and run the targeted suite: `./build/relay-buttonfit-tests` (or `ctest --test-dir build -R buttonfit --output-on-failure`). Do not run the full ctest suite.
3. If the floor test **fails**, the failure message names the theme and the exact rule; find that rule in `src/Theme.cpp` (or a per-widget `setStyleSheet` in `src/`), raise the size to at least `theme::FloorPt` (9pt, `src/Theme.h:115`), and adjust the widget's padding/min-height if the buttonfit clipping checks then complain about the taller text. Rebuild and re-run until the suite is green.
4. If the floor test **passes**, the fault was fixed incidentally since 2026-09-19: identify the commit with `git log --oneline -S 'tabProjectChip' -- src/Theme.cpp` (and `-S '8.5pt'` if needed), record it on the card, and close the card as done with the passing test output as evidence.
5. Either way, the card's `## Done means` is the same green test; land via `scripts/land.py` only if step 3 produced a diff.

**Risks** — If the chip still exists but was renamed, the failing test message gives the current selector; do not guess. Raising the font size could clip the chip's text in narrow tabs — the buttonfit suite's fit/clipping checks cover exactly that, so a new failure there after step 3 means the chip's geometry needs adjusting, not the font re-shrunk. No owner decision needed.

**Verify** — `./build/relay-buttonfit-tests` fully green (205/205), specifically `stylesheetFontsStayAtOrAboveTheFloor` passing in every built-in theme. Evidence for the card: the test binary's summary line plus, on the already-fixed path, the sha of the commit that removed the rule.
