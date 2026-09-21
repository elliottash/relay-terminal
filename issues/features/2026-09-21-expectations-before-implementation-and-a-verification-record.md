---
id: WC3E
type: work
status: planned
labels: [feature, switchboard, agent]
component: [gui, worker]
parent: YZ8G
rank: zzzzzzzzzzzzzzzzb
created: '2026-09-21'
source: 'owner, 2026-09-21: "i agree with all, go ahead with it" (#YZ8G plan)'
links: {plans: [], commits: [], evidence: [], related: [YZ8G, 7BM4], github: null}
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
