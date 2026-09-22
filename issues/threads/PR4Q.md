<!-- relay:entry 20260921T233640Z-a1 author=claude-code kind=comment -->
Filed from #YZ8G's plan on the owner's "i agree with all, go ahead with it". `## Done means` written before implementation, as the plan requires; the implementer will not write the QA checklist.

<!-- relay:entry 20260922T001154Z-q1 author=agent kind=progress -->
Landed, and moved to needs-verification. Five commits: `bebbe33677a3` the four statuses and what
makes a run evidence for a revision (`test_history.applicable`, `line_status`, `card_statuses`),
`60b3fa43ece4` the gate on `failed`/`missing-evidence` with overrides scoped to (check, revision),
`tests_accept`, the one `### Check` status and the refresh after a run, `2d1901e6613c` the card
page's status rows, "Use this existing result", "Replace retired check" and the gate's one
question, `630db31e05e3` a layout fix the live drive showed, `c3a88caada6f` SWITCHBOARD-FORMAT
§2.6, AGENT-SESSIONS-PROTOCOL §31 and the evidence.

Proof is in `docs/qa_evidence/2026-09-21-check-gate-statuses/` — the #7BM4 staged scenario driven
under Xvfb on the binary these commits build: the four statuses on one card, the laptop's run
offered and accepted, the retired check replaced, the override asked once and not asked again on
a second attempt, and a card with no `## Tests` asked which checks prove it, once.

No `## QA checklist` here: that is the verifying session's own record (board policy rule 12).

