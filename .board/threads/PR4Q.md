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


<!-- relay:entry 20260922T002717Z-v1 author=claude-code kind=evidence -->
Verified at revision `d6682f97` by a separate session, on a clean export of it
(`git archive d6682f97 | tar -x -C $X`) and never in the shared checkout. Staged, rerunnable:
`docs/qa_evidence/2026-09-21-verify-PR4Q/` (stage.sh, drives/run.sh, gui-start.sh, 23 captures).
One line per `## Done means` bullet:

- four statuses per listed test — passed: one Check answers passed/failed/not-applicable/missing-evidence on the staged card, and missing-evidence says "from any host, and no attached result", not "never run here" (03-four-statuses.png, drive1, StatusTests 12/12).
- an attached result counts, and "Use this existing result" is offered — passed: a `relay-remote-tests` folder at the card's commit ingests and flips missing-evidence to passed on sphinxpad; accepting the laptop's run writes `<!-- relay:accept … -->` into the `### Check` block and no front-matter field (drive5, drive4 G, 04-accepted.png, AcceptResultTest 4/4).
- a retired check reads not-applicable, offers "Replace retired check" and does not block — passed: the refusal names only totals_large_order and test_invoice (05-replace-retired.png, 07-gate-refusal.png, drive2/3).
- the gate blocks on failed/missing evidence only, the override is scoped to (check, revision), expires, and is asked once — passed: refused, then landed with markers `rev=307ac063fe7b until=2026-10-05`; a second Needs verification → Done asks nothing; `live_overrides` is empty for another revision (08/10/13, drive3 A–D, GateTest 13/13).
- a card with no `## Tests` is asked once, then moves — passed: "Which checks prove #5XQ5?" with None apply, `<!-- relay:tests-none … -->` recorded, 2nd and 3rd attempts silent (17/18, drive2).
- one `### Check` status replaced in place, ending `history: thread` — passed: three checks leave one heading, the `## Tests` lines untouched (drive4 F, CheckBlockTest 7/7).
- a finished run refreshes the strip without another Check — passed: "1 passed · 1 failed · 1 missing evidence · 1 not applicable" → "2 passed · 1 failed · 1 not applicable" on one "Run these" click (21/22-run-started.png, drive4 H, RefreshAfterRunTest 2/2).

Tests: both modules together `Ran 170 tests … OK`; `ctest -R cardtests` built from the export,
`Totals: 15 passed, 0 failed`; the implementer's own evidence folder read capture by capture and
reproduced on a different staging. No findings. One thing is left for the owner in
`## QA checklist`: the agent's `board_move_card` tool is not gated (drive6) — pre-existing from
#7BM4, and `## Done means` names no move path. Moved to needs-qa-llm.
