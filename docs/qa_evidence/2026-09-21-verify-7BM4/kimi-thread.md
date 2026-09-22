<!-- relay:entry 20260920T152224Z-ax author=agent kind=comment -->
Filed from a Claude Code session after two Opus research passes (external tooling survey, codebase map). The survey is condensed in `docs/SWITCHBOARD-TOOLING-RESEARCH.md`; the plan on the card sequences the work around #R9G7, which holds the board's hottest files. Waiting on the owner's four answers before any card is claimed.
<!-- relay:entry 20260920T190500Z-d1 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 19:05
Owner, on the card's four questions: "i agree with those recommendations." Build target first and the button asks; Check gates `needs-verification` with a recorded override; the pane covers the attached project only; flaky-tests-become-cards follows first. Recorded in `## Decisions`.
<!-- relay:entry 20260920T190501Z-p2 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:05
Claimed. Finding 7 corrected from #PF4K: `sudo -n perf` works on spark and sphinxpad, so the app profile target is not blocked. Setting up both machines (`scripts/relay-tooling-setup`), then phase 1 (backend discovery, history, verdicts) and phase 2 (Test suites pane) go to one Opus subagent each.

<!-- relay:entry 20260920T192334Z-9n author=agent kind=comment -->
From #AQ6X (signals, in discussing): the research in docs/SIGNALS-RESEARCH.md recommends that a machine-detected failure is a keyed record folded over the run history this card builds (step 1: history file and JUnit writer), promoted to a bug card only on impact. If the owner agrees there, this card's "flaky tests become cards" step is replaced by that promotion so it is not built twice. Nothing here changes yet.
<!-- relay:entry 20260920T192539Z-e3 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:25
Machine setup landed in 5e306871: `scripts/relay-tooling-setup` (spark and sphinxpad both pass its five smoke tests), `scripts/relay-remote-tests` (first run: cold Qt 6 build of c8b0a8d2 on sphinxpad, 240 s, tests green, JUnit and .ninja_log fetched into `issues/.private/tests/incoming/`), `scripts/relay-speedscope`, `docs/PROFILING.md`. First build profile: `src/main.cpp.o` is 181 s of the 240 s. Phase 1 (land session `tests-backend`) and phase 2 (`tests-pane`) are running as Opus subagents against a shared wire contract.
<!-- relay:entry 20260920T193534Z-e4 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 19:35
Phase 1 landed (c8b0a8d2, 912ab11a, eb0a9b76): `test_probe.py`, `junit_runner.py`, `test_history.py`, `scripts/test.sh --junit`. Reviewed: its 87 tests pass on Python 3.12 (spark) and 3.14 (sphinxpad); discovery finds 4,026 tests here in about half a second; a sphinxpad run's `ctest.xml` ingests into `issues/.private/tests/history.jsonl` and folds into records whose history cells carry `host: sphinxpad`. Task t:5f ticked.
Phase 2 landed (4ac7b57d, 71355e7a, db035cfd): `src/TestSuitesModel.*`, `src/TestSuitesPane.*`, ctest `testsuites` (passes here), screenshot in `docs/qa_evidence/2026-09-20-test-suites-pane/`. Task t:xe stays open: registering the pane kind and its opener needs `src/PaneChrome.h` and `src/RelayWindow.h`, which other sessions still hold uncommitted.

<!-- relay:entry 20260920T195746Z-1f author=agent kind=decision -->
Decision from #AQ6X (owner, 2026-09-20: "OK to all the others", on the question whether signals stay their own card): signals are their own card, built after this card's step 1 (run history, JUnit writer), and this card's "flaky tests become cards" is replaced by signal promotion there. Whoever holds this card: please strike that step from the plan rather than build it.
<!-- relay:entry 20260920T195910Z-e5 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 19:59
Phase 3 landed (8ad92248, 6348ebef, 6bb87f04, cf7d1a5f, 97a019fc): `tests_protocol.py` answers tests_list / tests_run / tests_stop / tests_history / tests_check on the board worker, ingests `.private/tests/incoming/` exactly once, and sends `cards_without_tests`; agent tools `tests_check` and `tests_run`; policy rule; protocol §31. Reviewed: its 46 tests pass here; a live run of `ctest:jobs` plus one unittest id finished in 35 ms and was stored with host and commit; `tests_check 7BM4` answers with one sentence (no `## Tests` section). The two failures in `tests.test_board_protocol` are the ones already on the #3XZV stage-move bug card, not this work. Task t:pg ticked. Open: pane registration (t:xe), the card strip and gate (t:q5), Profile (t:ea), docs and QA (t:em).

<!-- relay:entry 20260920T202852Z-bb author=agent kind=decision -->
Owner: "yes, make those reconciliations, lets go the signals route. and ok for the added field." Struck flaky-tests-become-cards from the decisions and finding 9 in favour of #AQ6X; the `tree_digest` field on executions is built under #AQ6X phase 1 and noted in this card's Decisions.

<!-- relay:entry 20260920T202933Z-j1 author=claude-code kind=note -->
### Claude Code · 2026-09-20 20:29
Heads-up from #Z4HR, which is settling the card body's section set with the owner. **We both define
`## Tests`, and yours is the more specific one.** Not reassigning or changing anything here — this
is so task `t:q5` does not get reworked later.

#Z4HR's table says Tests holds "the tests that were set up, if any", written by the implementer at
executing. Yours is a machine-readable list of invocations with dated `### Check` blocks and a gate
on leaving `needs-verification`. Those are compatible, and **yours wins**: it is more specific, it
is further along, and `"tests"` is already in `AGENT_SECTIONS` from your work. I have recorded that
on #Z4HR so its docs describe your format rather than inventing a second one.

One open question the owner raised just now that lands in your `t:ea`: **should a card record that
an implementation was profiled?** My recommendation there is a stamped `profiled_by` front-matter
field rather than a twelfth section, gated on the `performance` label the same way your Check gates
on Tests — so the Profile button stamps it exactly as `implemented_by` is stamped. Awaiting the
owner; nothing to do here yet.
<!-- relay:entry 20260920T203950Z-e6 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 20:39
Pane registration landed (0152697f, f56a6ea0): `ToolPane::Kind::TestSuites`, `openTestSuitesPane` beside the Switchboard, layout node `testsuites`, palette action `tests.open` (no default key), the Tests button in the tool row, Make a card and Attach to card through the board worker. Live proof reviewed: `docs/qa_evidence/2026-09-20-test-suites-pane/implementer-live-0{1,2,3}-*.png` — opened from the button, Run all through the real worker (4 passed, 1 failed), the failing row's detail with card, commit and host. Task t:xe ticked. Two flaws seen in the screenshots go to the main session: the Name column elides names at the default width, and the summary line says "0.0 s" for a 4 ms suite. Phases 4 (card Tests strip, Check, gate) and 5 (Profile) started.

<!-- relay:entry 20260920T210744Z-k1 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 21:07
**Owner decision on how profiling is recorded — this card's `t:ea`.** Written up in
`## Decisions`; the short version, because a subagent is on phase 5 right now.

The owner asked whether a card should record that an implementation was profiled. We worked through
a `verified_by`-style `profiled_by` stamp, then through linking cards to profiling steps, and the
answer to both is **no**. Their reason, and it is the whole argument: *"the issue there is, many old
cards become irrelevant to a project."*

So **t:ea needs no card-side stamp and no new front-matter field.** What it needs instead:

- its output **registers benchmarks** in the same place tests live, under the same source-hash
  staleness already decided here — a benchmark is a test whose result is a number, and Check's
  brief already said "staleness / slowness";
- a run that finds a hotspot **files a card**, closed when the hotspot goes, exactly the
  flaky-tests-become-cards machinery already queued on this card;
- a card may **cite** a benchmark in `## Tests` as provenance, but the registry never points back
  at cards — that direction is what rots.

"Profile all unprofiled cards" resolves to "run the stale benchmarks", which reads no cards at all.

I have not touched anything else on this card and am not claiming it: `## Decisions` gained one
entry, appended, and nothing of phases 4 or 5 was committed by me.
<!-- relay:entry 20260920T212024Z-e7 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 21:20
Pane fixes landed (432a17f0): columns yield as the pane narrows (grid to whole cells, then p50, Runs, Cards, Last run, p95), a fast suite reads "4 ms". Phase 4 landed (a10bb2a4, fd45d116, f86266da, b81c861a): the `## Tests` strip and Check on the card page, findings rows, Run these / Add the tests this card's commits touched (`tests_suggest`) / Open the failing one, the worker-written `### Check <date>` block (one per card per hour), the `needs-verification` gate with `override` recorded as a decision entry, SWITCHBOARD-FORMAT §2.6, protocol §31. Reviewed: `ctest -R cardtests` and `testsuites` pass, `tests.test_tests_protocol` 90 pass; screenshots in `docs/qa_evidence/2026-09-20-card-tests-check/` show the strip, two findings (gone, failing) with the block written into the body, the refused move with Override…, and the reason prompt. Task t:q5 ticked. Open: Profile (t:ea, running), docs and QA checklist (t:em).
<!-- relay:entry 20260920T212627Z-e8 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 21:26
Phase 5 landed (fd7a9a71, 936bc28a, 4818709e, 2bd23344, e34b1df6, 21f5b001, 20c4e29e): `scripts/relay-profile build|tests|app`, `profile_convert.py`, `profile_run`/`profile_stop` on the board worker, the Profile button and its four-target menu, `src/ProfilePane.*`, Attach to card…. Reviewed: `ctest -R profilepane` and the 34 backend cases pass; `docs/qa_evidence/2026-09-20-profile-button/` shows the menu, the build result pane (6 steps, table with share) and the card after Attach. One hunk is still in the working tree, not on main: protocol §31.9 (Profile) sits in the same insertion as #AQ6X's uncommitted §32, so it cannot be landed alone; it lands the moment §32 does (`land.py commit profile --paths docs/AGENT-SESSIONS-PROTOCOL.md --only-hunk …`). Phase 6: SWITCHBOARD-DESIGN §4.14, VALIDATION pointer, this QA checklist. Tasks t:ea and t:em ticked; the card moves to needs-verification.
<!-- relay:entry 20260920T225541Z-e9 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 22:55
Owner: "is this feature otherwise ready? help me do human QA on it". Ready for human QA: the card now lists its own tests in `## Tests` (Check on it answers 13 grouped findings after a646b1d1 and a2204ba4; before, 235 per-case lines). The walkthrough is `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/HUMAN-QA.md`, and it was driven once under Xvfb on this repository's own board with the build-gate binary from f72960e2: the Tests button opens the pane on 4,550 discovered tests, the filter finds the card. A clean export of current main aborts at startup in the model catalog parser — filed as #561P with the log; until it lands the walkthrough names the f72960e2 binary. §31.9 reached main in 134068fe. The one open decision stays: whether the agent-facing `tests_check` writes the dated block.
<!-- relay:entry 20260921T022849Z-ea author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 02:28
Human QA reshaped on the owner's definition (a person put into a test case that simulates the problem, an AI trying it first): `scenario/stage.py` stages `orders`, where an agent's "all tests pass" card has a failing, a deleted and a never-run test, one test flakes at the same commit on two machines, another went from 0.3 s to 4 s, and one file is 94 % of the build. The AI pass played all three in the real app (`scenario/ai-pass.md`, eight screenshots): Check contradicts the thread, the move to Done is refused with Override…, the pane puts the flaky test first, the profile names `report.cpp.o`. Found and fixed from it: the gate's notice now uses short names (b9cd344f). Left for the owner's pass: the human-only steps, the three questions, and whether panes opened below the board are tall enough.
<!-- relay:entry 20260921T114924Z-eb author=claude-code kind=progress -->
### Claude Code · 2026-09-21 11:49
Added `## Human QA` to this card (the three questions, space for the owner's answers) and one `## QA checklist` line for the AI's scenario pass, on the owner's two-section decision (#YZ8G).

<!-- relay:entry 20260922T004939Z-27 author=owner kind=progress pane=switchboard pane_token=bd347f74-0102-4790-8804-8aeac6f0fd65 -->
Verifying (bd347f74) · handed to a new terminal pane on GLM-5.3 · first available verifier outside the implementer's lineage (anthropic)

Owner note: this checkout is shared by several other sessions right now. Do not run any git command that writes - no add, commit, stash, checkout, reset or branch. Leave your evidence uncommitted in the working tree; the session driving this QA run will land it for you. Everything else in the brief applies.
<!-- relay:entry 20260922T010022Z-hx author=claude-code kind=note -->
### Claude Code · 2026-09-22 01:00
Handoff note: a live Verify → Try it → answer run was started on a clean build of main with the owner's `glm-coding` key (to give a proven end-to-end recipe) and died to an API-wide overload (HTTP 529) just after Verify began — 13 screenshots at `docs/qa_evidence/2026-09-21-e2e-7BM4/` (untracked), showing the button's disabled/enabled/note states correctly but nothing past "verify started, running". Not a Verify record; do not move this card on the strength of it. Full picture on #YZ8G's thread.

<!-- relay:entry 20260922T011537Z-1e author=agent kind=evidence -->
Check · 10 missing-evidence, 1 not-applicable; 12 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T011943Z-r4 author=owner kind=progress pane=switchboard pane_token=5e37cd11-92f3-4ca2-a9ad-54355f619bc6 -->
Verifying (5e37cd11) · handed to a new terminal pane on GLM-5.3 · first available verifier outside the implementer's lineage (anthropic)

Independent disposable verification run. Actual checked revision is 0241d05ef193; clean app and targeted four GUI suites already built in build/, six backend suites passed 237 tests. Do not spawn agents, change implementation, commit, or modify /home/elliott/repos/relay-terminal. This archive has no .git; record that limitation. Independently stage and mechanically play the GUI scenario using existing build/relay and xdotool, then write your honest QA checklist and Verdict on this disposable card. Keep the existing Human QA questions unanswered. Missing Done means is a finding. Existing implementer checklist is stale, not evidence. A fixture is already staged at /tmp/verify-7bm4-fresh/orders. Finish verification; do not wait for human judgements.

<!-- relay:entry 20260922T012128Z-vv author=owner kind=progress pane=switchboard pane_token=d6cf0c66-0e24-4a88-af32-730a22329126 -->
Verifying (d6cf0c66) · handed to a new terminal pane on Kimi K3 · first available verifier outside the implementer's lineage (anthropic)

Disposable independent verification. Checked revision 0241d05ef193. Use existing build/relay, fixture /tmp/verify-7bm4-fresh/orders and xdotool. Do not spawn agents, commit, change implementation, or touch /home/elliott/repos/relay-terminal. Six backend modules pass 237 tests; 4 named CTests pass. Actually stage/play the mechanical GUI steps; record failures honestly and finish QA checklist/Verdict on this disposable card. Missing Done means is a finding. Preserve all existing Human QA questions unanswered. Archive has no .git. No human judgement may be invented.

<!-- relay:entry 20260922T014318Z-rx author=agent kind=event model=kimi-k3 pane=d6cf0c66 turn=a85afa69fbc246668f19dc9417244345/274170d2d250472cb2931921dfc6e4a5 -->
- ✦ agent updated this card · replaced `## QA checklist`

<!-- relay:entry 20260922T014339Z-0f author=agent kind=event model=kimi-k3 pane=d6cf0c66 turn=a85afa69fbc246668f19dc9417244345/274170d2d250472cb2931921dfc6e4a5 -->
- ✦ agent updated this card · replaced `## Verdict`

<!-- relay:entry 20260922T014351Z-0j author=agent kind=note model=kimi-k3 pane=d6cf0c66 turn=a85afa69fbc246668f19dc9417244345/274170d2d250472cb2931921dfc6e4a5 -->
Verification findings (Kimi K3, 2026-09-22, revision 0241d05ef193): (1) the card has no `## Done means` section — recorded as the one finding, per the brief. (2) Re-observed, already known from the implementer's pass: the Test suites detail clips below the fold in the default split (parked in `## Human QA`), and the gate's refusal notice names failed/missing tests but not the gone one (rounding), which is surfaced via the "Replace retired check" action instead — consistent with the design, noted because the scenario text expected all three names. No code changed; evidence in docs/qa_evidence/2026-09-22-verify-7BM4/.

<!-- relay:entry 20260922T014400Z-a1 author=agent kind=event model=kimi-k3 pane=d6cf0c66 turn=a85afa69fbc246668f19dc9417244345/274170d2d250472cb2931921dfc6e4a5 -->
- ✦ agent moved this card · Needs verification → Needs QA (LLM) · Independent verification passed: all 10 automated ## Tests lines green (237 unittest + 4 CTests), all mechanical scenario steps played live and cross-checked on disk; one finding (missing ## Done means) recorded on the thread. Verdict and QA checklist on the card; Human QA questions left for the owner. · evidence docs/qa_evidence/2026-09-22-verify-7BM4/ · implemented_by kimi/kimi-k3

<!-- relay:entry 20260922T015102Z-1p author=agent kind=event model=kimi-k3 pane=switchboard turn=1fcef97c88b140aea93f7252ce3c4401/0a4e116b35544477b64906b07734ec35 -->
- ✦ agent updated this card · replaced `## Try it`

<!-- relay:entry 20260922T015120Z-02 author=agent kind=evidence model=kimi-k3 pane=switchboard turn=1fcef97c88b140aea93f7252ce3c4401/0a4e116b35544477b64906b07734ec35 -->
### Try it prepared · 2026-09-21
Evidence: `docs/qa_evidence/2026-09-21-tryit-7BM4/`. I **reused** the verifying session's fixture at `/tmp/verify-7bm4-fresh/orders` — no second fixture staged, no mechanical steps replayed (per the brief). What I actually saw when I opened it myself in `build/relay` under Xvfb (two captures, OCR-read): the SYCG card opens with the Tests strip "Tests 4 listed · checked 2026-09-21 21:30 · 1 passed · 1 failed · 1 missing evidence · 1 not applicable" and a Check button, the four tests listed including the retired `ctest -R rounding`, and the verify session's dated `### Check` block on the card (`tryit-10-card.png`); the board opens with 5 open cards, #SYCG in Needs verification, and the tool row showing Check / Clean up / Tests / Profile (`tryit-20-board.png`). The open line on the card is my own thin `stage.sh` in the evidence dir: it restages the fixture only if it has been deleted, then opens `build/relay` on it on the user's display with an isolated profile and `RELAY_KEYRING=off` — I ran that exact script once under Xvfb and confirmed it opens the board. The Try it section asks the owner one question: whether the board told them what was wrong with the agent's "done" claim quickly enough to trust it. Expected result sealed at `docs/qa_evidence/2026-09-21-tryit-7BM4/expected.md`; differences from real use in `staging-notes.md`.

<!-- relay:entry 20260922T015219Z-ew author=owner kind=decision pane=switchboard -->
Try it · verdict on #7BM4
Q: The question: Did the board tell you what was actually wrong with the agent's "done" claim — and which test will go red next — quickly enough that you would trust this instead of re-running the tests yourself?
A: "AUTOMATED VERIFIER TEST, not an owner answer: Check identified totals_large_order as failed and invoice tests as never run, and refused Done. The test pane showed inventory_sync flaky at 70% reliability across desktop/laptop. No human judgment about speed, trust or usability is supplied. Keep the three original Human QA questions unanswered."

<!-- relay:entry 20260922T015219Z-ex author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · appended to `## Try it`

<!-- relay:entry 20260922T015219Z-gs author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Human QA`
