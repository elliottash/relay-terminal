---
id: 243T
type: work
status: needs-verification
labels: [feature, build, performance]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: d203514e-c598-429e-be3a-f1824c862ad8
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Relay pane, 2026-09-23
links: {plans: [], commits: [f6480574d52885f40126ff6cfef4d5d433bddeb5], evidence: [docs/qa_evidence/2026-09-23-build-speed-243T/report.md], related: [PF4K], github: null}
---
# Reduce Relay rebuild time by separating Pane and RelayWindow implementations

## Issue
yes, do that in a separate directory to check it works before we merge it back.

## Done means
An isolated checkout builds Relay successfully after moving substantial Pane and RelayWindow method bodies into separate translation units. Targeted tests pass, and a measured edit to one implementation file no longer recompiles the other or main.cpp. The change is landed only after those checks; any build or behavior regression fails the experiment.

## Plan
**Goal.** Reduce the critical path of local Relay rebuilds while preserving behavior.

**Findings.** A separate Ninja profile took 84 s wall, with `src/main.cpp.o` taking 58.2 s. `src/main.cpp` includes the in-class implementations from `src/Pane.h` (19,996 lines) and `src/RelayWindow.h` (12,116 lines). The shared `build/` uses Makefiles and contains other sessions' work.

**Steps.** 1. Create a Git worktree and separate build directory at the committed tip. 2. Prototype moving method bodies to `src/Pane.cpp` and `src/RelayWindow.cpp`, updating `CMakeLists.txt`; keep a small reviewable diff where possible. 3. Build Relay, run focused tests, and measure an incremental edit to each implementation file against the prior 58.2 s `main.cpp` compile. 4. Land only a passing change into the shared checkout, preserving concurrent edits.

**Risks.** The classes have many inline methods and cross-class dependencies; a partial extraction may be more practical than moving every method at once. A worktree at HEAD excludes concurrent uncommitted edits, so landing must explicitly reconcile shared files.

**Verify.** Use `scripts/relay-build` for the worktree's local build, relevant CTest suites, and Ninja timing data in an isolated profiling directory. Record the exact commands and outputs on this card.

## Execution Summary
Built and tested in isolated worktree `/tmp/relay-build-speed-243T` at base `31e9ee10` before copying the patch to the shared checkout. Moved 31 byte-identical method bodies from `Pane.h` and `RelayWindow.h` into nine source files. Added `scripts/relay-build --fast`, using a separate Ninja `build-fast/` with `-O0 -g1`; normal builds remain optimized. Exact-baseline cold profile: 85 s before, 75 s after; fast cold build: 28.57 s before, 27.33 s after; fast incremental pane edit: 17.81 s before, 6.08 s after. Single-run measurements and exact commands: `docs/qa_evidence/2026-09-23-build-speed-243T/report.md`.

## Tests
- `ctest -R '^editor$' --test-dir build --output-on-failure`
- `ctest -R '^panetabnavigation$' --test-dir build --output-on-failure`
- `ctest -R '^windowstate$' --test-dir build --output-on-failure`
- `ctest -R '^panestate$' --test-dir build --output-on-failure`
- `ctest -R '^boardpane$' --test-dir build --output-on-failure`
- `python3 -m unittest tests.test_relay_build`
- manual: docs/qa_evidence/2026-09-23-build-speed-243T/report.md

### Check 2026-09-23 17:31
- passed · ctest:editor — ctest -R editor passed for this revision on spark-dcc9, 2026-09-21T13:42:11Z
- passed · ctest:panetabnavigation — ctest -R panetabnavigation passed for this revision on spark-dcc9, 2026-09-23T21:31:55Z
- passed · ctest:windowstate — ctest -R windowstate passed for this revision on spark-dcc9, 2026-09-22T01:12:46Z
- passed · ctest:panestate — ctest -R panestate passed for this revision on spark-dcc9, 2026-09-23T21:31:55Z
- passed · ctest:boardpane — ctest -R boardpane passed for this revision on spark-dcc9, 2026-09-22T01:37:20Z
- not-applicable · manual:docs/qa_evidence/2026-09-23-build-speed-243T/report.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-build-speed-243T/report.md
history: thread
