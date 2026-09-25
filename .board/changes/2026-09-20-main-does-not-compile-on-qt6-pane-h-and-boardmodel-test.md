---
id: 5BAS
type: work
status: needs-verification
labels: [bug, build]
assignee: agent
implemented_by: glm/glm-5.3
priority: 2
rank: m9
created: '2026-09-20'
verify: {artifact: system, primary: script, also: [], human: none, criteria: clean-export Qt6 build of main completes; ctest -R board runs and passes on the Qt5 build, sign_off: none, effort: medium}
source: Claude Code (#PF4K orchestrator), clean-export build of main 0beeadfc on sphinxpad, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [0TJ9, 8YQ9, GMCF], github: null}
---
# main does not compile on Qt6 (Pane.h:1028), and boardmodel_test.cpp does not compile at all

## Issue
build it on sphinxpad as well to see how performs there

## Findings
A clean export of `main` at 0beeadfc (`git archive`, RelWithDebInfo, `-k 0`) on sphinxpad (Ubuntu 26.04, g++ 15; Qt 5.15 and Qt 6.10):

1. **The app itself fails on Qt6** — `src/Pane.h:1028`: `m_sessionTextMark = std::min(m_sessionTextMark, paneHistoryLines(kSessionTextScan).size());` → `no matching function for call to 'min(int&, qsizetype)'`. Qt6's `size()` is `qsizetype`; Qt5's is `int`, so the shared `build/` on spark (Qt5) and land.py's build gate cannot see it. Landed in fed72147 (#0TJ9). This is the 26.04 package's build: the `.deb` for Ubuntu 26.04 and Debian 13 is Qt6, so the next release build fails here. Fix: `std::min<qsizetype>` or `int(...)` at that line.
2. **`tests/boardmodel_test.cpp:3635` fails on both Qt5 and Qt6** — `view.onModelPick = [&picked](const QString &data) { picked = data; };` assigns a `void` lambda to `std::function<bool(const QString &)>` (`src/BoardPane.h:73`). Landed in 28b56483 (#8YQ9). `relay-board-tests` cannot build, so `ctest -R board` cannot run; land.py's gate builds only the `relay` target, which is why it passed. Fix: `return true;` (or whatever the box's contract is — the comment at `BoardPane.h:464` says the return value means "accepted").

The Ubuntu 26.04 CI leg landed today (dc091a86) fails on both of these on the next push. Not fixed here because both files belong to live sessions (`0tj9`, `picker-gui`/`helperpicker` hold `src/Pane.h`; the #8YQ9 test is that card's); this card is the notice, and a comment is on each card's thread.

## Done means
Current `main` builds cleanly under Qt6 — the configuration the Ubuntu 26.04 / Debian 13 package and the 26.04 CI leg (dc091a86) use — and `relay-board-tests` builds and runs, so `ctest -R board` executes instead of failing at compile time. Concretely: no `std::min(int&, qsizetype)` error from `src/Pane.h`'s session-text-mark code, and no void-lambda/`std::function<bool>` mismatch in `tests/boardmodel_test.cpp`. Failure is recognised as either compile error reappearing in a clean-export Qt6 build or in the `relay-board-tests` target, or the 26.04 CI leg going red on the next push.

## Plan
**Goal** — the two build breaks this card reported on 2026-09-20 (Qt6 failure in `src/Pane.h`, all-Qt failure in `tests/boardmodel_test.cpp`) are confirmed fixed on current `main`, with proof from a clean-export Qt6 build and a green `ctest -R board`; if either still breaks, fix it and land. The card was a notice because both files belonged to live sessions then; the code has moved since.

**Findings** — checked on the working tree on 2026-09-24 (this plan turn could not run a build):
- The Qt6 break looks **already fixed**: `src/Pane.h:1482` now reads `m_sessionTextMark = std::min(m_sessionTextMark, int(paneHistoryLines(kSessionTextScan).size()));` — the `int(...)` cast the card asked for. `src/Pane.h:1475` assigns `.size()` straight to the `int m_sessionTextMark` (implicit `qsizetype`→`int` conversion; compiles on Qt6, may warn).
- The test break looks **already fixed**: `tests/boardmodel_test.cpp` no longer contains the void `onModelPick` lambda, and `src/BoardPane.h` no longer declares `onModelPick` at all (the comment at `src/BoardPane.h:116` records that the view's model boxes and `onModelPick`/`onSlashCommand` were removed).
- So the work is verification first, repair only if a build says otherwise.

**Steps**
1. Find who fixed it: `git log --oneline -20 -- src/Pane.h` and `git log -S 'onModelPick' --oneline -- tests/boardmodel_test.cpp src/BoardPane.h`. Record the fixing commits in `links.commits` and a card comment.
2. Build the tests target on this machine (Qt5): `scripts/relay-build --target relay-board-tests` (or the target name from `CMakeLists.txt` if renamed), then `ctest --test-dir build -R board`. This proves the `boardmodel_test.cpp` half.
3. Prove the Qt6 half against a **clean export**, not the shared `build/` (the tree holds other sessions' uncommitted code): `git archive main | tar -x -C <scratch>` and configure with Qt6. If this machine has no Qt6, use the Ubuntu 26.04 CI leg landed in dc091a86: check its result on the newest push to `main` (a green leg that compiles `src/Pane.h` is the proof). If neither is reachable from this pane, say so in a card comment and leave the Qt6 leg to the next push rather than claiming it verified.
4. Only if a build still fails: fix the exact line the compiler names (the card's suggested `int(...)` cast / `return true;` pattern), rebuild through `scripts/relay-build`, and land with `scripts/land.py begin/commit`. Do not touch anything else in these files — both have been heavily edited by other sessions.
5. Move the card to `done` (medium: verified by build + test run, no design choice) with the evidence: build/CI output and the ctest table.

**Risks**
- The shared `build/` directory can be stale (the CLAUDE.md mid-compile incident); always verify on a clean export for the Qt6 claim.
- `src/Pane.h` remains a heavily contended file; if a repair edit is needed, claim it with `land.py begin` immediately before editing and keep the hunk minimal.
- No owner decision needed.

**Verify**
- Clean-export Qt6 configure+build of `main` completes (or the 26.04 CI leg is green on the newest push).
- `ctest --test-dir build -R board` runs and passes — the target building at all is the regression test for the `boardmodel_test.cpp` half.
