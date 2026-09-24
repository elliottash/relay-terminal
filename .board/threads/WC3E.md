<!-- relay:entry 20260921T233640Z-a1 author=claude-code kind=comment -->
Filed from #YZ8G's plan on the owner's "i agree with all, go ahead with it". `## Done means` written before implementation, as the plan requires; the implementer will not write the QA checklist.

<!-- relay:entry 20260921T235955Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 19:59
Landed in `0ef3ee13`, and moved to needs-verification. `## Done means` is a recognised section and
the Plan turn writes it before `## Plan`; Execute on a card without one puts one sentence in the
board's notice line and goes on; `## Human QA`, `## Profile` and `## Try it` are recognised too;
`## QA checklist` is the verifying session's record (revision, each expectation and test line as
passed / failed / missing evidence / not applicable, what is unresolved, and a dated named review
line that is never omitted) with the tests-and-simulation shape the owner asked for on 2026-09-21;
and an agent's move to `done` is refused while a numbered `## Human QA` question has no indented
`Answer:` line. No `## QA checklist` on this card by design — a separate verifying session writes
that, which is this card's own rule.

<!-- relay:entry 20260922T001044Z-v1 author=claude-code kind=evidence -->
### Claude Opus 5 (verifier session) · 2026-09-22 00:10 UTC
Verified at `486852e0` on a clean `git archive` export, never in the shared checkout. Evidence and
a rerunnable `stage.sh`: `docs/qa_evidence/2026-09-21-verify-WC3E/`. `## QA checklist` and
`## Verdict` written on the card; no `## Human QA` — nothing here needs the owner's judgement.
One line per `## Done means` bullet:

- Plan turn / Execute leave `## Done means`, Execute warns not refuses — **passed**: a Plan turn took
  `Done means` and `Plan` and refused `Execution Summary` and `QA checklist` with
  `board_mode_refused`; `board_claim` on a card without the section answered `board_written` with
  the one-sentence notice, no `error`, status `executing` (drives.txt).
- Verify is the tests *and* a simulation, with `tests:` / `simulation:` / `staged:` and `stage.sh` —
  **passed**: all three fixed lines and the `ai-pass.sh` recipe are in `board::verifyTask`
  (src/BoardModel.cpp:623-746), asserted by `boardmodel_test.cpp` (ctest.txt).
- The verifying session, never the implementer, writes `## QA checklist` — **passed**: "There is no
  checklist waiting for you", the revision line, the four marks, the unresolved line and the dated
  named line are all in the brief; separate session required, different family recommended.
- Implementer lands with tests and evidence and no checklist; policy, POLICY.md, briefs and protocol
  §19/§31 agree; the heading list has Done means, Human QA, Profile — **passed**: POLICY.md
  regenerates byte-identical from `board_policy.md` (policy-regen.txt, "IDENTICAL"), and `check`
  warned on none of the six headings while still warning on an invented one (drives.txt).
- Agents move cards within their authority; an open `## Human QA` question keeps a card out of done —
  **passed**: refused for the agent actor with `requires: human_qa_answer`, allowed once answered,
  untouched for the owner actor, and prose with no numbered question gates nothing.

Tests: the four Python modules and `ctest -R '^(board|boardpane)$'` all pass at that revision
(unittest.txt, ctest.txt). The single failure in the combined Python run,
`test_board_tools.SpecTests.test_the_designed_tools_are_offered_and_nothing_else`, is #JNYN's
`board_try` spec test: it landed in `03701acf`, before this card's parent, and still fails at main's
tip `3a5fa029`. Not a finding against #WC3E. Moved to needs-qa-llm.
