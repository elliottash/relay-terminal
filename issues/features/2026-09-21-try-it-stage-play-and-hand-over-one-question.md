---
id: JNYN
type: work
assignee: codex-verify-jnyn-a1
status: ready
labels: [feature, switchboard, qa]
component: [gui, worker]
parent: YZ8G
rank: zzzzzzzzzzzzzzzzc
created: '2026-09-21'
source: 'owner, 2026-09-21: "i agree with all, go ahead with it" (#YZ8G plan)'
links: {plans: [], commits: [03701acf, 79ef050e, bca9a82e, a739dd45, 39bab9d9], evidence: [docs/qa_evidence/2026-09-21-tryit/, docs/qa_evidence/2026-09-21-verify-JNYN-fresh/], related: [YZ8G, 7BM4, WC3E], github: null}
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

## QA checklist
Independent Codex review, 2026-09-21 local / 2026-09-22 UTC. Fresh evidence:
`docs/qa_evidence/2026-09-21-verify-JNYN-fresh/report.md`.
- Done means 1 — PARTIAL: protocol reuse tests pass; live agent reuse not exercised.
- Done means 2 — PARTIAL/BLOCKED: actual isolated UI action/open/answer drive on pinned build-gate binary; supported provider absent, successful agent staging not established.
- Done means 3 — FAIL: successful retry still reports the previous run's staging failure (`backend.txt`).
- Done means 4 — FAIL: normal answer/reveal works, but `try_answer` erases existing Human QA decisions (`backend.txt`, `fixture-after.md`, `thread-after.md`). Real card's Human QA untouched.
- Done means 5 — NOT ESTABLISHED: full GUI/backend/CLI agent staging on own board remains unverified.
- Tests: `tests/test_tryit_protocol.py` — PASS 32/32 via unittest (`tests.txt`).
- Tests: `ctest -R cardtests` — PASS 1/1 existing build; source provenance unestablished (`tests.txt`).
- Tests: configured-model manual #7BM4 — NOT REPRODUCED; fresh #T9QA UI click refused without supported provider (`ui-try.png`).
Actual backend revision: unchanged tested files at `0241d05ef19393e84c0ef98655c9687466a92efc`.
GUI is exploratory `/tmp/claude-1000/land/jnyn/verify/build/relay`, not current main;
binary SHA256 and source manifest/blob identities are in `runtime.txt`.

## Verdict
2026-09-21 local / 2026-09-22 UTC — **FAIL; returned to Ready for implementation repair.**
Scope failure-note lookup to the current Try it run and preserve pre-existing Human QA decisions.
After repair, rerun the two fresh reproductions and complete a clean-revision, configured-provider
staging pass. Prior interrupted-verifier artifacts are not proof. No implementation changed.
