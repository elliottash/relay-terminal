---
id: JNYN
type: work
status: executing
labels: [feature, switchboard, qa]
component: [gui, worker]
assignee: codex-verify-jnyn-a1
parent: YZ8G
rank: zzzzzzzzzzzzzzzzc
created: '2026-09-21'
source: 'owner, 2026-09-21: "i agree with all, go ahead with it" (#YZ8G plan)'
links: {plans: [], commits: [1f3a7af0e7467dff0037cd56d285e3aef2d19727, 132d35235d29e9f5931c2391c9c4fb24026f0cc8, f819daa617fa7fc34bde95c9fa0bc1d59c974d36, 03701acf, 79ef050e, bca9a82e, a739dd45, 39bab9d9, 1f3a7af0e7467dff0037cd56d285e3aef2d19727, 132d3523], evidence: [docs/qa_evidence/2026-09-21-verify-JNYN-recheck/, docs/qa_evidence/2026-09-21-tryit/, docs/qa_evidence/2026-09-21-verify-JNYN-fresh/, docs/qa_evidence/2026-09-21-verify-JNYN-recheck/], related: [YZ8G, 7BM4, WC3E], github: null}
---
# Try it: stage the situation, complete the mechanical pass, hand the person one task and one question

## Issue
Step 3 of #YZ8G's plan and decision 1 of that card ("a button after verify … the agent would then run your app in a way that illustrates the feature or fix"). Codex: "One Try it action launches the pinned build and disposable fixture. If preparation fails, report that before requesting review"; "Automate [the mechanical steps]. Ask the person to perform only the task whose usability or interpretation needs observation"; "Give the problem without the answer. Observe whether they find it; reveal the expected result afterwards"; "Do not restrict it to cards carrying scenario". Worked example to generalise: docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ (stage.py, scenario.json, ai-pass.sh, HUMAN-QA.md).

## Done means
- Try it is step 3 of the owner's three (run tests; verify with an AI simulator; open the app for the human in a simulated environment that tests the issue): when Verify (#WC3E) has left a `staged:` directory, Try it reuses it and does not replay the mechanical steps; it stages and plays itself only when no Verify staging exists. (Owner, 2026-09-21.)
- A **Try it** action on the card page (available from needs-verification on) starts a bounded Switchboard-agent turn that: reads `## Issue` and `## Done means`; stages the situation as a disposable fixture (a throwaway project, seeded data, a pinned binary where the card is about the app), records how the staged environment differs from real use; plays every mechanical step itself in the real app or command and captures evidence; then writes `## Try it` on the card: how to open the staged thing (one command or one button), one short task, one question — with the expected result withheld until the person answers.
- If staging or the mechanical pass fails, the card gets that report and no request for review.
- The person's answer goes in the thread as their verdict, and the expected result is revealed after it; the card's `## Human QA` is generated from `## Try it`, not typed twice.
- It works for a card about the GUI (Xvfb-isolated app, xdotool), a backend behaviour (a request and a response or a failure reproduction), and a command-line tool (a before/after on a real file), on this repository's own board.
Failure would show as: the brief containing the answer; a person asked to run tests or attach evidence by hand; a Try it that needs the owner to set up a display or a profile; a staging failure surfacing as a review request.

## Execution Summary
Try it is protocol § 31.10. Three commits:

- **03701acf** — `backend/relay_core/tryit_protocol.py`: `try_run`, `try_stop` and `try_answer`
  on the board worker, built like `board_cleanup` (a request, a tagged agent turn, streamed
  `tryit` progress, one `finished {out, section_written}`). The turn's whole instruction is
  `backend/relay_core/board_tryit_brief.md`, versioned beside `board_policy.md`. A turn that
  could not stage leaves a `note` beginning `Try it could not be staged:` and **no** section, so
  a card never asks for a review of something that was never staged. `try_answer` puts the
  person's words on the thread as a `decision` that quotes them (policy rule 4 — there is no
  `verdict` entry kind), breaks the seal on `expected.md` under `## Try it`, and generates
  `## Human QA` from the section and the answer. `board_try` in `board_tools.py` hands a
  terminal-pane agent the same brief, gated during a cleanup exactly as `board_claim` is.
  Wired in `board_protocol.py` (`TRYIT_TYPES`, `_tryit`, one branch in `dispatch`, one in
  `observe`).
- **79ef050e** — the card page: **Try it (y)** on the action row beside Verify from
  needs-verification on, saying "Trying…" while the turn runs; a `## Try it` strip over the
  body with the unanswered question in amber, an **Open it** button for the one line the
  section names, and a one-line answer box that sends `try_answer` on Enter; the run as one
  line in the board's notice area, and the turn's own events swallowed so none of its
  commentary reaches the card's thread. `BoardView::onRunCommand` is wired in `RelayWindow.h`
  to `Pane::queueCommand`.
- **bca9a82e** — § 31.10 in `docs/AGENT-SESSIONS-PROTOCOL.md`, and `## Human QA` generated in
  the shape `docs/SWITCHBOARD-FORMAT.md` 2.8 fixes (a numbered question with an indented
  `Answer:` line), so Try it and `board_tools.unanswered_human_qa`'s close gate interlock.

Owner's three steps (2026-09-21): Try it is the third. When Verify (#WC3E) has left a `staged:`
directory — `docs/qa_evidence/<date>-verify-<ID>/stage.sh` — `verify_staging` finds it and the
turn reuses it instead of replaying the mechanical pass; the `started` event says `reusing`.
2026-09-21 repair after fresh independent verification: 1f3a7af0e7467dff0037cd56d285e3aef2d19727 scopes failure-note lookup to entries created during the current run and updates only the generated Try it Human QA block, preserving prior owner decisions. Two regression cases added; all 34 protocol cases pass. Independent repair recheck: 132d3523, docs/qa_evidence/2026-09-21-verify-JNYN-recheck/report.md. Full configured-model staging is being exercised with #7BM4.

## Tests
- `tests/test_tryit_protocol.py` — 32 cases: the turn's started / progress / finished events,
  the staging-failure path (a note and no section), `try_answer` writing the verdict, the
  reveal and `## Human QA`, "already running", `try_stop`, reuse of a Verify staging, the
  section parser, `board_try`'s gate, and the worker's own failure note when a run ends
  `error`, `stopped` or `cancelled` without one.
- `ctest -R cardtests` — the card page's existing suite, for the `CardDetail` edits.
- `manual: docs/qa_evidence/2026-09-21-tryit/` — Try it pressed for real on #7BM4, from a
  Relay on an isolated Xvfb display and profile, with the Switchboard agent on its configured
  model.

### Check 2026-09-21 21:39
- passed · unittest:tests.test_tryit_protocol — tests/test_tryit_protocol.py passed for this revision on spark-dcc9, 2026-09-22T01:39:51Z
- missing-evidence · ctest:cardtests — no run of ctest -R cardtests for this revision, from any host, and no attached result
- not-applicable · manual:docs/qa_evidence/2026-09-21-tryit/ — manual evidence, recorded by hand: Try it pressed for real on #7BM4, from a
- notice · ctest:cardtests — ctest -R cardtests has never run here
- warning · manual:docs/qa_evidence/2026-09-21-tryit/ — manual evidence Try it pressed for real on #7BM4, from a is not there
history: thread
## QA checklist
Independent Codex verifier a1, updated 2026-09-21 local / 2026-09-22 UTC after repair.
- Done means 1 — PARTIAL: protocol reuse tests pass; configured-model reuse remains pending a2's end-to-end evidence.
- Done means 2 — PARTIAL: real UI open/answer/reveal passes on verified 82acbc04 GUI with clean fixed 1f3a7af0 backend; successful autonomous staging remains pending a2.
- Done means 3 — PASS for reported defect: original successful-retry reproduction now returns staging_failed=false and the successful report; original error-path coverage still passes.
- Done means 4 — PASS: original prior-decision-loss reproduction now preserves the existing answer; actual isolated UI confirms preservation, new verdict and reveal. Real card Human QA untouched.
- Done means 5 — PENDING: broad GUI/backend/CLI configured-model staging is not established by this narrow repair recheck; a2 owns the live #7BM4 run.
- Tests test_tryit_protocol.py — PASS 34/34 against clean 1f3a7af0 export.
- Tests ctest cardtests — earlier 1/1 PASS remains supporting evidence only; repair was backend-only.
- Tests configured-model manual #7BM4 — PENDING a2's independent record, not counted from old implementer captures.
Fresh repair evidence: docs/qa_evidence/2026-09-21-verify-JNYN-recheck/report.md (commit 132d35235d29e9f5931c2391c9c4fb24026f0cc8); original failure record retained in docs/qa_evidence/2026-09-21-verify-JNYN-fresh/.
Actual repair backend: 1f3a7af0e7467dff0037cd56d285e3aef2d19727; exact GUI gate 82acbc04, separately identified rather than claimed as a single rebuilt revision.

## Verdict
2026-09-21 local / 2026-09-22 UTC — **PASS for both repaired defects and actual UI answer flow; broader staging verdict PENDING a2.** The earlier blanket FAIL is superseded: independent original reproductions now pass and 34 protocol tests pass. Do not close from this limited recheck alone. Status deliberately unchanged while parent coordinates a2's end-to-end evidence.
