---
id: WC3E
type: work
status: needs-qa-llm
labels: [feature, switchboard, agent]
component: [gui, worker]
parent: YZ8G
rank: zzzzzzzzzzzzzzzzb
created: '2026-09-21'
source: 'owner, 2026-09-21: "i agree with all, go ahead with it" (#YZ8G plan)'
links: {plans: [], commits: [0ef3ee1311f9, e7ce0f13b8c2], evidence: [docs/qa_evidence/2026-09-21-verify-WC3E/], related: [YZ8G, 7BM4], github: null}
---
# Expectations before implementation, and a verification record written by a separate session

## Issue
Step 2 of #YZ8G's plan. Codex: "Also missing: acceptance expectations recorded before implementation. The verifier should report what it independently checked against those expectations. Writing the entire checklist afterwards invites selecting criteria the implementation happens to satisfy." And: "Resolve the contradictory rules first. Decisions permit agent movement and require a separate session; the Plan demands a different model family, and phase 5 forbids verifier movement before calibration. Policy requires the implementer to supply a checklist; the proposal forbids it. Policy says invent no headings, yet omits Human QA and Profile." Owner (2026-09-21): the implementer may not write the checklist; agents keep moving cards; a card with an open judgement waits for the person.

## Done means
- A Plan turn (and Execute on a card with no plan) leaves `## Done means` on the card before work starts: the intended outcome and how failure would be recognised, two to five lines; Execute warns, not refuses, when it is missing.
- For a card about the app, Verify is (1) the tests and (2) an AI simulation: the verifier stages the situation as a disposable fixture (rerunnable `stage.sh` under `docs/qa_evidence/<date>-verify-<ID>/`), plays the mechanical steps in the real app under an isolated display, and keeps one capture per step; its record carries `tests:` and `simulation:` lines and a `staged:` line naming the directory. For a card not about the app, simulation is the request/response or before/after reproduction, or `not applicable`. (Owner, 2026-09-21.)
- The Verify session — never the implementing pane, and recommended from a different model family as today — writes `## QA checklist` as its own record: the revision it checked, each expectation with passed / failed / missing evidence / not applicable and the evidence path, what is unresolved, and a positive line that the review happened, dated and named ("reviewed, no findings" differs from "not reviewed").
- The implementer moves its card to needs-verification with tests and evidence but no checklist; the policy text, POLICY.md, the plan/verify briefs and protocol §19/§31 say so and no longer contradict each other; the fixed heading list includes Done means, Human QA and Profile.
- Agents move cards within that authority; a card whose `## Human QA` has an unanswered question stays out of done.
Failure would show as: an implementer still writing its own checklist; a Verify record with no revision; a card reaching done with an open human question; POLICY.md disagreeing with board_policy.md.

## Tests
- `PYTHONPATH=backend:tests RELAY_KEYRING=off python3 -m unittest tests.test_board_tools.DoneMeansSectionTests` — tests/test_board_tools.py
- `PYTHONPATH=backend:tests RELAY_KEYRING=off python3 -m unittest tests.test_board_protocol.DoneMeansAndHumanQATests` — tests/test_board_protocol.py
- `PYTHONPATH=backend:tests RELAY_KEYRING=off python3 -m unittest tests.test_board.PolicyFileTests` — tests/test_board.py
- `PYTHONPATH=backend:tests RELAY_KEYRING=off python3 -m unittest tests.test_system_prompt.SizeTests` — tests/test_system_prompt.py
- `ctest --test-dir build -R '^(board|boardpane)$'` — tests/boardmodel_test.cpp

## QA checklist
Checked at `486852e0` (newest of links.commits is `0ef3ee13`, the code; `486852e0` is this card's
own move to needs-verification and the revision everything below was exported from). Verified on a
clean `git archive` export, never in the shared checkout.
tests: passed (revision 486852e0)
simulation: played — the not-about-the-app form, request and response (evidence docs/qa_evidence/2026-09-21-verify-WC3E/drives.txt)
staged: docs/qa_evidence/2026-09-21-verify-WC3E/
No app pass: this card is a policy and protocol change whose whole app surface is one notice line
(`BoardView::handleEvent`, src/BoardPane.cpp), so the simulation is the tool calls and the answers
the board gave, not Xvfb and xdotool (SWITCHBOARD-FORMAT 2.8).

Done means
- A Plan turn (and Execute) leaves `## Done means` before work; Execute warns, not refuses — passed —
  a Plan turn's `board_update_card` took `Done means` and `Plan` and refused `Execution Summary` and
  `QA checklist` with `board_mode_refused` (docs/qa_evidence/2026-09-21-verify-WC3E/drives.txt, "P:");
  `board_claim` on a card with none answered `board_written` with notice "#MJQ4 has no Done means; the
  verifier will have nothing to check against.", no `error` event, status `executing` (same file,
  drive-execute-notice); with the section present there is no notice. `board_plan_brief.md` step 2 asks
  for it before the plan, `board::verifyTask`'s sibling `executeTask` (src/BoardModel.cpp:413) asks the
  implementer for it before it changes code, and `CARD_SECTIONS` puts `done means` before `plan`.
- Verify is the tests and an AI simulation, with `tests:` / `simulation:` / `staged:` and a rerunnable
  `stage.sh` — passed — `board::verifyTask` (src/BoardModel.cpp:623-746) carries all three fixed lines
  verbatim, the `ai-pass.sh` recipe, the isolated-display rule and `docs/qa_evidence/<today>-verify-<ID>/`;
  the not-about-the-app fallback and `not applicable` are there too. Asserted by
  `tests/boardmodel_test.cpp::theVerifyTaskIsTheQaChecklistAndAsksForTheVerifiedByTrailer`
  (docs/qa_evidence/2026-09-21-verify-WC3E/ctest.txt).
- The Verify session, never the implementing pane, writes `## QA checklist` — passed — `verifyTask`
  says "There is no checklist waiting for you: `## QA checklist` is yours to write", asks for the
  revision ("Name the revision you checked"), the four marks with an evidence path, what is unresolved,
  and the dated named line that is never omitted; and it states that a separate session is what makes
  the check independent while a different model family is "Relay's recommendation, not a requirement".
  Same in `board_move_card`'s description (backend/relay_core/board_tools.py:276).
- The implementer lands with tests and evidence and no checklist; policy, POLICY.md, the briefs and
  protocol §19/§31 agree; the heading list has Done means, Human QA and Profile — passed —
  `board_policy.md` rule 5 ("and no `## QA checklist`: a separate session verifies and writes it") and
  rule 10 (the complete list, with `Done means`); `issues/POLICY.md` regenerates byte-identical from it
  (docs/qa_evidence/2026-09-21-verify-WC3E/policy-regen.txt, "IDENTICAL"); `deliver/SKILL.md` step 5
  "**Write no `## QA checklist`**"; `executeTask` "write **no** `## QA checklist`"; protocol §19.20 and
  §31 both say so; `CARD_SECTIONS` holds `done means`, `human qa`, `profile`, `try it`, and `check`
  warned on none of the six new headings while still warning on an invented one (drives.txt, "C:").
- Agents move cards within that authority; an unanswered `## Human QA` question keeps a card out of
  done — passed — the agent's `board_move_card` to `done` over two numbered questions was refused with
  `board_refused` / `requires: human_qa_answer` naming both; with one answered it named the one left;
  with both answered the move went through; the owner actor's own close over an unanswered question
  still went through; prose with no numbered question gated nothing (drives.txt, "D" to "D5", and the
  same four through the protocol wire). Policy rule 10 keeps "move cards within your authority".
- Failure would show as: an implementer writing its own checklist / a Verify record with no revision /
  a card reaching done with an open human question / POLICY.md disagreeing with board_policy.md —
  passed — none of the four is reachable at this revision: the three briefs forbid the first, the
  verify brief demands the revision, `_human_qa_gate` refuses the third, and the regeneration diff is
  empty.

Tests
- `tests.test_board_tools.DoneMeansSectionTests` — passed — "Ran 10 tests ... OK" (docs/qa_evidence/2026-09-21-verify-WC3E/unittest.txt)
- `tests.test_board_protocol.DoneMeansAndHumanQATests` — passed — "Ran 5 tests ... OK"
- `tests.test_board.PolicyFileTests` — passed — "Ran 16 tests ... OK"
- `tests.test_system_prompt.SizeTests` — passed — "Ran 4 tests ... OK"
- `ctest --test-dir build -R '^(board|boardpane)$'` — passed — "100% tests passed, 0 tests failed out
  of 2", on a RelWithDebInfo build of the export (docs/qa_evidence/2026-09-21-verify-WC3E/ctest.txt)

Unresolved
- The four modules run together report one failure that is not this card's:
  `test_board_tools.SpecTests.test_the_designed_tools_are_offered_and_nothing_else`, because
  `board_try` is in `TOOL_SPECS` and not in the test's expected tuple. `board_try` landed in
  `03701acf` (#JNYN), before this card's parent `2d1901e6`, and the same test still fails at main's
  tip `3a5fa029`. It belongs to #JNYN; no card filed here, because that card is live.
- `docs/SWITCHBOARD-DESIGN.md` §6.2 item 4 still summarises landing as "`needs-qa-llm` with an
  evidence path and a QA checklist". That line is from `acb0def6` (2026-09-17) and was already stale
  on the lane name before #3XZV; the card's bullet names the policy, POLICY.md, the briefs and the
  protocol, and all of those agree, so this is a doc-debt note rather than a failure.
Reviewed by claude-opus-5[1m] (verifier session, separate from the implementing pane) on 2026-09-21: no findings

## Verdict
Passed at `486852e0`. All five `## Done means` lines and all five `## Tests` lines checked and passed;
evidence and a rerunnable `stage.sh` in `docs/qa_evidence/2026-09-21-verify-WC3E/`. Ran on a clean
export of that revision: the four Python modules, a request/response drive of the Human QA gate, the
Plan-turn scope, the `check` headings and the Execute notice, the POLICY.md regeneration diff, and
`ctest -R '^(board|boardpane)$'` on a RelWithDebInfo build. The one unittest failure in that run is
#JNYN's `board_try` spec test, present before this card and still present at main's tip. No findings.
