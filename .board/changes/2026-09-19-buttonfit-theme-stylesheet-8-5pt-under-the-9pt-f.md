---
id: T9K3
type: work
status: planned
labels: [bug, theme]
rank: zzzzzzzzzw
created: '2026-09-19'
source: pane 2, 2026-09-19
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# buttonfit: theme stylesheet 8.5pt under the 9pt floor

## Issue
Noticed while testing an unrelated change (the "Relaying ⋅" separator): ctest's buttonfit test fails in the shared worktree.

## Done means
- `ctest --test-dir build -R buttonfit --output-on-failure` passes in the shared worktree, including `ButtonFitTest::stylesheetFontsStayAtOrAboveTheFloor()` (tests/buttonfit_test.cpp:124).
- No `font-size` below `relay::theme::FloorPt` (9.0pt, src/Theme.h:115) in any built-in theme's live stylesheet — theme-flag rules included — nor in any per-widget `setStyleSheet` in `src/*.cpp`/`src/*.h`.
- Failure is recognised by the 2026-09-19 symptom: the floor test red with e.g. 'dark-copper: "font-size: 8.5pt" is under the 9pt floor'.
- Note: a 2026-09-24 source scan shows no sub-9pt `font-size` anywhere in `src/` and the offending `QToolButton#tabProjectChip` rule is gone entirely — the light-themes work that introduced it has since landed fixed. Verification may be the whole job; a code change is only needed if the test is still red.

## Plan
**Goal.** Confirm — and if necessary restore — the 9pt legibility floor across every built-in theme so the buttonfit floor test is green again.

**Findings.** The 2026-09-19 failure was `QToolButton#tabProjectChip { … font-size: 8.5pt … }` at src/Theme.cpp:542, measured in a worktree where the light-themes session had Theme.cpp half-edited — i.e. in-flight work, not necessarily a landed fault. As of 2026-09-24 that rule no longer exists: a scan of `src/` finds no `font-size` below 9pt and no `tabProjectChip` at all, so whoever landed Theme.cpp appears to have fixed it already. The guard itself is intact: `ButtonFitTest::stylesheetFontsStayAtOrAboveTheFloor()` (tests/buttonfit_test.cpp:124) iterates every built-in theme via `relay::theme::availableThemes()`/`setActiveTheme()` (theme flags append rules of their own, so those are covered too), then scans `src/*.cpp`/`src/*.h` for per-widget `setStyleSheet` sizes. `FloorPt` is 9.0 (src/Theme.h:115).

**Steps.**
1. Build: `scripts/relay-build` (one build at a time; it holds the lock).
2. Run `ctest --test-dir build -R buttonfit --output-on-failure`.
3. If green: no code change — the fault was fixed when the light-themes work landed. Record the passing run as evidence on the card and move it on.
4. If red: the failure message names the theme and the exact rule. Find that rule in src/Theme.cpp (or the per-widget file the source scan names), raise it to at least `theme::FloorPt` (prefer an existing step — 9pt or 9.5pt — over a new value), rebuild, rerun the buttonfit test.
5. Either way, sanity-check the whole stylesheet scan with `rg -n 'font-size:\s*[0-8]' src/` — the ctest run is the verdict; the grep is a fast cross-check.

**Risks.** The shared worktree still holds other sessions' uncommitted edits, so a red test may again be someone else's in-flight Theme.cpp rather than main's — check `git status -- src/Theme.cpp` before editing, and if the file is dirty with someone else's sub-floor rule, comment on the card instead of committing over them (CLAUDE.md: never commit a file you did not write; commit via `scripts/land.py`). No owner decision needed.

**Verify.** `ctest --test-dir build -R buttonfit --output-on-failure` green, with `stylesheetFontsStayAtOrAboveTheFloor()` among the passes; paste the summary line as card evidence. If a rule was changed, also switch to the named theme in the running app and look at the affected chip/label to confirm it is still legible.
