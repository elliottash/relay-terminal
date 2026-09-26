---
id: 5P0Q
type: work
status: planned
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
source: 'Measured during #7Z08 exact-tree verification, 2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [H2KQ, 7Z08], github: null}
---
# H2KQ console test races the shell command label on a clean build

## Issue
Exact-tree verification for #7Z08 failed at tests/h2kq_cases.h:66: h2kqBusyText(pane).contains(QStringLiteral("sleep")). The wait at line 63 stops as soon as "Esc stops" appears, before the shell poll resolves the program name. The shared checkout already contains another session's uncommitted fix waiting for "sleep… · Esc stops". Queuecontract passed; no recall case failed.

## Done means
The esc-label case in `tests/h2kq_cases.h` waits for the busy line's full resolved text ("sleep… · Esc stops") before asserting the program name, and that wait is committed on `main` — not only sitting uncommitted in the shared checkout.
A clean export of `main`, built outside the shared checkout, passes `relay-consolemode-tests --h2kq-only` on at least 10 consecutive runs, and `ctest -R consolemode` passes there.
Failed if the same CHECK fails on an exact-tree build again: a label that says "Esc stops" but never got the program's name.

## Plan
**Goal.** Kill the clean-build race in the h2kq esc-label case: the case must wait until the shell poll has resolved the program name into the busy line before asserting it, and that wait must be on `main` — today it exists only as #H2KQ's session's uncommitted edit in the shared tree.

**Findings.**
- `tests/h2kq_cases.h`, first case ("esc-label"): `main`'s committed wait polls only for `"Esc stops"` (main line ~63), then CHECKs `contains("sleep")` (~line 66). The busy line (`paneBusyLine`, built in `src/PaneUi.cpp` ~line 270; wording per `src/PaneStatus.h`) appears as soon as the pane knows a program runs, but the program's *name* arrives a beat later on the shell poll — the wait can pass first and the CHECK fail. That is exactly what #7Z08's exact-tree verification hit (log: `/tmp/claude-1000/land/recall7z08/verify/build/Testing/Temporary/LastTest.log`).
- The shared working tree already holds the fix as #H2KQ's session's uncommitted edit: the wait polls for the full `QStringLiteral("sleep… · Esc stops")` with a comment saying the name resolves on the shell poll (`tests/h2kq_cases.h:63-65`). `main` still has the racy wait.
- Sibling sweep: no second instance. `tests/234z_cases.h:67` waits for `"stops"` and asserts only `"stops"` (its own comment says asserting the strip would only measure poll speed); h2kq's other waits (`"Alt+Esc stops"`, the strip buttons, `busyAction` visibility) each wait for exactly what they assert.

**Steps.**
1. Check whether the fix already landed: `git log --oneline -3 -- tests/h2kq_cases.h`, then `git show main:tests/h2kq_cases.h | grep -n 'sleep… · Esc stops'`. If present, skip to step 5 — only verification remains.
2. `python3 scripts/land.py who`. If the session claiming `tests/h2kq_cases.h` (#H2KQ's, pane aeb6ddec) is live, do **not** land its edit yourself: comment on #H2KQ asking it to land its one-hunk wait fix (its own edit; this card then waits on that) and note it here. Only if that session is stale (12 h idle, `who` says so) continue: `python3 scripts/land.py begin <me> tests/h2kq_cases.h` — the snapshot includes their fix, which is what this card lands.
3. Add the exhaustion diagnostic to that wait, in `h2kqRun`'s style (h2kq_cases.h:33): if the loop ends without the full label, `std::fprintf(stderr, "FAIL h2kq esc-label: busy line never named the program: %s\n", qPrintable(h2kqBusyText(pane)));` before the CHECKs — the next failure then distinguishes "poll never resolved" from "label changed shape".
4. Land: `python3 scripts/land.py commit <me> -m "h2kq: wait for the resolved program name in the esc-label case (#5P0Q)" --verify-target relay-consolemode-tests --verify-tests consolemode`. The build gate compiles and runs the consolemode suite on the exact tree; if it reports contested/stale hunks, review the digest (`--dry-run`) and land with `--confirm <digest>`. While iterating, build through `scripts/relay-build --target relay-consolemode-tests`.
5. Verify on a clean export whatever step 1 or 4 put on `main`: `git archive <sha> | tar -x -C $(mktemp -d)`, configure and build `relay-consolemode-tests` there, run `--h2kq-only` 10 times and `ctest -R consolemode` once; keep the log under `docs/qa_evidence/2026-09-25-h2kq-label-race/`.
6. Write `## Execution Summary` and `## Tests` (the exact `relay-consolemode-tests --h2kq-only` invocation plus the clean-export runs), run `tests_check`, then move this card to needs-verification with that evidence path.

**Risks.**
- The fix hunk is another session's edit; if that session is live this card waits for it to land its own fix rather than racing it — that wait is the plan, not a gap.
- The wait string pins the label's exact format ("sleep… · Esc stops", ellipsis and middle dot); a wording change in PaneStatus/PaneUi changes this test with it — which is what a UI case test is for.
- The 100 × 25 ms poll budget is the file's standard; a machine slower than 2.5 s to resolve the name would still fail, but so would `h2kqRun`'s own busy wait, so it is not this card's to widen.
- No owner decision is needed.

**Verify.** land.py's verify slot passes with `--verify-tests consolemode`, and the step-5 clean export passes 10/10 `--h2kq-only` runs plus `ctest -R consolemode`; the evidence log lands with the card.
