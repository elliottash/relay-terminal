---
id: SBT2
type: work
status: planned
labels: [bug, tests, build]
rank: msbt2
created: '2026-09-22'
source: Found during SPB2 package validation by Codex, 2026-09-22
links: {plans: [], commits: [f589f8c57bb5efdefa4b4bdbceb6bf064b874ebf], evidence: [docs/qa_evidence/2026-09-22-sphinxpad-build/], related: [SPB2, 3BPH, 99T0], github: null}
---
# Sphinxpad package gate fails on clean main

## Issue
During the requested fresh build on sphinxpad, clean revision fff7eb8fdf4617e2cc845805c209f32e264dd77d compiled successfully with Qt6 and system Python 3.14 on Ubuntu 26.04 amd64, but the test gate did not pass.

## Tests
- `boardworkspace`: anOptionOrSessionLinkOpensWhereItNames fails because its source assertion cannot find Pane::openOutputTarget().
- `boardexecute`: three missing-button failures; already tracked by #3BPH.
- `backend-and-bash`: timed out at 600.23 seconds, with earlier FAIL results in malformed-request handling, two guest-delegation cases, and the browser install-prompt case. Automatic repeat stopped after the first timeout; related timeout history is #99T0.
- `relay-engine-tests`: separate run failed; exact cases are recorded in engine-test.log.
- Evidence: docs/qa_evidence/2026-09-22-sphinxpad-build/build-system-python.log and engine-test.log.

## Done means
The native package gate (`scripts/package-deb.sh`, which runs `./scripts/test.sh`) finishes green on sphinxpad — Ubuntu 26.04, system Python 3.14, clean clone of main.
Each failure the 2026-09-22 gate reported is diagnosed as product regression, stale test, or environment requirement, with evidence on the card; fixes land here unless the work already belongs to #3BPH or #99T0, in which case the hand-off is explicit.
Failure is recognised by: the same tests failing again on a fresh sphinxpad gate run, or a "stale test" diagnosis that rewrites expectations without first checking the behaviour they guard.
Verify: primary is a sphinxpad gate rerun (fresh clone of main, `scripts/relay-build`, `./scripts/test.sh`, `scripts/package-deb.sh` all green); before that, `ctest -R boardworkspace`, `relay-engine-tests --core ghostty` on the two named tests, and the named pytest files green on a dev machine. Effort: about half a day of diagnosis plus one gate rerun.

## Plan
**Goal.** Get the native package gate green on sphinxpad (Ubuntu 26.04, system Python 3.14, clean clone of main) by clearing the five failure groups the 2026-09-22 gate reported — fixing what is this card's, and making the hand-off to #3BPH / #99T0 explicit for what is theirs.

**Findings.**
- `boardworkspace` has two **stale source-grep assertions**, not product failures found so far:
  - `anOptionOrSessionLinkOpensWhereItNames` anchors on the old 3-argument signature `"void openOutputTarget(const QString &target, int line, bool fromMouse) {"` (tests/boardworkspace_test.cpp:802, used again at :807). The live declaration at src/Pane.h:3387 takes a fourth `modifiers` argument, so the anchor matches nothing.
  - `aCardTurnsEventsReachThatCardsConsoleAndNoOther` asserts `deliverToConsoles` contains the literal `entry.context->spec().surface != card`. #KSKH refactored that filter: src/RelayWindow.h:5837-5838 now reads `const QString mine = entry.context ? entry.context->spec().surface : QString(); if (!card.isEmpty() && (!entry.context || mine != card)) continue;` — the predicate survives under a local name, so the behaviour likely still holds and only the literal is stale. This must be verified, not assumed.
- `boardexecute`'s three missing-button failures are **#3BPH's**: planned there (missing since #DNGC landed in 99e9770 on 2026-09-21; the pytest input fix landed in 300d2b0). Nothing to duplicate here.
- `backend-and-bash` timed out at 600 s with earlier FAILs in malformed-request handling, two guest-delegation cases and the browser install-prompt case. That set overlaps #99T0's (two of its five reproduced on the owner's machine; fixes landed 2026-09-21, card in needs-qa-llm). The sphinxpad run was 2026-09-22 under **system Python 3.14**, so remaining failures are either #99T0 cases not yet fully fixed or Python-3.14-specific. Evidence: docs/qa_evidence/2026-09-22-sphinxpad-build/ — note the card names `build-system-python.log` but the directory holds `package.log`; read what is actually there.
- `relay-engine-tests`: `CoreTest::wrappedUserRolesSurviveReflow` (engine/tests/CoreTest.cpp:369) and `ViewTest::findCountsRewrappedMatchesOnce` (engine/tests/ViewTest.cpp:1886) fail on the **Ghostty core only**; both are reflow cases. GhosttyCore keeps row roles by grid ref (engine/core/GhosttyCore.cpp:180, :923), unlike the libvterm fork which widened `relay_marks`.

**Steps.**
1. **Diagnose before changing anything.** At current main, run each failing test on a dev machine and record pass/fail per test in this card's thread: `./build/relay-boardworkspace-tests anOptionOrSessionLinkOpensWhereItNames aCardTurnsEventsReachThatCardsConsoleAndNoOther`; `./build/engine/relay-engine-tests --core ghostty wrappedUserRolesSurviveReflow findCountsRewrappedMatchesOnce`; `python3 -m pytest tests/test_agent.py tests/test_guest_bridge.py -x` (guest-delegation and malformed-request cases live there). Local passes point at environment; local failures point at regression or stale test.
2. **boardworkspace anchors.** First verify the behaviour: `deliverToConsoles` (src/RelayWindow.h:5818-5842) must still drop a card-named event whose surface differs, and `openOutputTarget` must still route option/session links as the tests describe. Only then update the two anchors at tests/boardworkspace_test.cpp:802/:807 and the `!= card` literal to the current source text. If the behaviour check fails, this is a regression — file it on the card and fix the source instead of the test.
3. **Engine reflow.** If the two Ghostty tests reproduce locally, bisect the reflow path in engine/core/GhosttyCore.cpp (resize/reflow handling around row-role refs and find-match recomputation) and fix the regression. If they pass locally, diagnose as environment (libghostty-vt version on sphinxpad) and record which version each side builds against.
4. **backend-and-bash.** Read the evidence log and map each of the five FAILs against #99T0's landed fixes (commits 6ffa2b04, 3154c0c6, and the needs-qa-llm evidence). Failures already covered there: note the hand-off, no new work. Failures not covered — the likely Python-3.14-specific ones — get a minimal fix, checked under the newest Python available locally.
5. **boardexecute.** No work here; the gate goes green on those three when #3BPH lands. State that dependency in the thread so the verifier does not re-diagnose it.
6. **Gate rerun.** After the fixes land: fresh clone of main on sphinxpad, `scripts/relay-build`, `./scripts/test.sh`, `scripts/package-deb.sh`, replacing the logs in docs/qa_evidence/2026-09-22-sphinxpad-build/ with the green run (or a new dated evidence directory linked from this card).

**Risks.**
- The final gate needs a run **on sphinxpad itself**; the executing pane may not have ssh access to it. Owner question: who reruns the gate — this pane via ssh, or the owner by hand?
- tests/boardworkspace_test.cpp holds 27+ source-grep anchors of the same brittle kind (#MHWY's rule applies: source grep proves shape, not behaviour). Converting the suite is out of scope; only the two failing anchors change, and each gets a behaviour check first.
- The 600 s timeout may hide failures beyond the five recorded (the repeat-after-timeout never ran). The rerun in step 6 must use the post-#99T0 runner that continues past a timeout, so the full list is visible.

**Verify.**
- `ctest --test-dir build -R boardworkspace` green (both named tests).
- `./build/engine/relay-engine-tests --core ghostty` green for `wrappedUserRolesSurviveReflow` and `findCountsRewrappedMatchesOnce`.
- `python3 -m pytest tests/test_guest_bridge.py tests/test_agent.py -q` green, and under Python 3.14 if it can be had locally.
- Final: the sphinxpad package gate green end to end, logs in the evidence directory.
