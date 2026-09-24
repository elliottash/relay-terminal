<!-- relay:entry 20260921T233640Z-a1 author=claude-code kind=comment -->
Filed from #YZ8G's plan on the owner's "i agree with all, go ahead with it". `## Done means` written before implementation, as the plan requires; the implementer will not write the QA checklist.
<!-- relay:entry 20260922T002104Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-22 00:21
Try it landed in 03701acf (the turn, the brief `board_tryit_brief.md`, `try_run`/`try_stop`/`try_answer`, and `board_try` for a terminal pane), 79ef050e (the **Try it (y)** action beside Verify, the `## Try it` strip with its Open it button and one-line answer box, the run in the board's notice area, and `onRunCommand` wired to `Pane::queueCommand`) and bca9a82e (protocol § 31.10, and `## Human QA` generated in the numbered-question / indented-`Answer:` shape SWITCHBOARD-FORMAT 2.8 fixes, so Try it and the close gate interlock). Owner's three steps are honoured: when Verify (#WC3E) leaves a `staged:` directory, `verify_staging` finds it and the turn reuses that fixture instead of replaying the mechanical pass. Tests: `tests/test_tryit_protocol.py` (28 cases) and `ctest -R cardtests`. Live proof in `docs/qa_evidence/2026-09-21-tryit/`: Try it pressed for real on #7BM4 from a Relay on an isolated Xvfb display and profile, with the Switchboard agent on its configured model. No `## QA checklist` here — that is the verifying session's own record. Moving to needs-verification.
<!-- relay:entry 20260922T003843Z-b2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-22 00:38
The live drive is in `docs/qa_evidence/2026-09-21-tryit/`. Try it pressed for real on #7BM4, from a Relay on an isolated Xvfb display and profile with the Switchboard agent on its configured model (`kimi|kimi-k3`): the button beside Verify, one press turning it into "Trying…", the notice line, and the turn's own task list read straight off the brief. The turn then ran for thirty minutes and sixty-nine tool calls — it reused the worked example's `stage.py` for its `stage.sh`, staged `orders` with the seeded card already in Needs verification, wrote and debugged its own xdotool driver, and captured the staged card's move to Done being refused — before `kimi-k3` stalled at the provider at step 53 and the turn ended `error`. **No `## Try it` was written and the card was never put up for review**, which is the rule this card exists for, exercised against a real provider failure. Two things found by looking at the screenshots and fixed (a739dd45): the strip drew the question with its list marker on it, and the revealed Expected lost its indent in `## Human QA`. One thing found by looking at #7BM4 afterwards and fixed (39bab9d9): the card carried no trace that Try it had been attempted, because the brief asks the *agent* for the "could not be staged" note and a stalled agent never reaches it — the worker writes it now. The section, the Open it button and `try_answer` were driven separately and deterministically on a disposable copy of the same fixture (`b*` screenshots, `b-card-after.md`).
<!-- relay:entry 20260922T010022Z-hx author=claude-code kind=note -->
### Claude Code · 2026-09-22 01:00
Handoff: the independent verifier for this card died to a platform-wide API overload (HTTP 529) mid-run, before landing anything or writing `## QA checklist`. It left real evidence at `docs/qa_evidence/2026-09-21-verify-JNYN/` (untracked: card/action-row/trying-state/strip/open-it/answer/revealed/thread-verdict screenshots, a backend drive script, stage.sh, relay.log) — a head start for whoever verifies this next, not a finished verification. Card stays in `needs-verification` until a verifier session actually writes the record and moves it.

<!-- relay:entry 20260922T010959Z-a1 author=codex kind=progress -->
### Codex · 2026-09-22 01:09
Claimed for fresh independent verification, explicitly authorized to replace the interrupted verifier. Plan: rerun targeted tests, stage and drive the real UI under isolated Xvfb, map every Done means and Tests item to fresh evidence and actual revision. No implementation changes. Relay board MCP unavailable; file fallback.

<!-- relay:entry 20260922T011420Z-a2 author=codex kind=evidence -->
### Codex · 2026-09-22 01:14
Independent verification FAIL; returned to Ready. Fresh report maps all Done means and Tests: docs/qa_evidence/2026-09-21-verify-JNYN-fresh/report.md. 32 protocol tests and existing cardtests pass, but successful retry reports a stale staging failure and try_answer erases pre-existing Human QA answers. UI open/answer/reveal driven under isolated Xvfb on recorded gate binary, exploratory only; real agent staging blocked without supported provider. No implementation or real Human QA decisions changed. Parent must repair the two concrete backend findings before approval.

<!-- relay:entry 20260922T011456Z-a3 author=codex kind=evidence -->
### Codex · 2026-09-22 01:14
Verification record landed as f819daa617fa7fc34bde95c9fa0bc1d59c974d36; linked in commits. Board check reports 13 existing errors / 745 warnings, none naming JNYN or its card path. Relay MCP unavailable, no delegation used.

<!-- relay:entry 20260922T011618Z-4r author=agent kind=progress model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/dae58e875189402db525b64e48092820 -->
Resuming implementation after independent failure report: scope staging failure notes to the active run and preserve pre-existing Human QA decisions when updating Try it answers; add regression tests, then return to the independent verifier.

<!-- relay:entry 20260922T013956Z-w3 author=agent kind=evidence -->
Check · 1 missing-evidence, 1 not-applicable, 1 passed; 2 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T014211Z-a4 author=codex kind=evidence -->
### Codex · 2026-09-22 01:42
Updated independent QA checklist and Verdict to clear both fixed defects using 132d3523 repair recheck. Kept broader staging pending a2, no status move and no Human QA changes. Original failure evidence retained; no blanket FAIL remains.

<!-- relay:entry 20260922T014212Z-6z author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/dae58e875189402db525b64e48092820 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["1f3a7af0e7467dff0037cd56d285e3aef2d19727", "132d35235… → {"plans": [], "commits": ["1f3a7af0e7467dff0037cd56d285e3aef2d19727", "132d35235…; appended to `## Execution Summary`

<!-- relay:entry 20260922T020007Z-fk author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/e17af86989a0419d9ff038915f2577f0 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260922T020029Z-dw author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/e17af86989a0419d9ff038915f2577f0 -->
- ✦ agent moved this card · Executing → Needs verification · Repairs landed and narrow independent recheck passes; next session must finish combined current-build staging verification. · evidence docs/qa_evidence/2026-09-21-verify-JNYN-recheck/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T020120Z-5m author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/e17af86989a0419d9ff038915f2577f0 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["1f3a7af0e7467dff0037cd56d285e3aef2d19727", "132d35235… → {"plans": [], "commits": ["1f3a7af0e7467dff0037cd56d285e3aef2d19727", "132d35235…

<!-- relay:entry 20260922T034618Z-7e author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/b2f9a50bb88e4c748ed6721b0ef13196 -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260922T034618Z-m5 author=agent kind=decision model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/b2f9a50bb88e4c748ed6721b0ef13196 -->
Owner proposed direction: "i think i want to expand the verification concept . a card has a designation of whether human QA is needed. in that case, \"verified\" will require that."

Record for the next design pass: each card designates whether human QA is required; when it is required, automated/AI verification alone is insufficient for the card to count as verified. This broadens verification beyond the app-oriented “Try it” concept. This is a proposed direction, not an implemented gate; designation details and the QA pane design remain to be worked out.
