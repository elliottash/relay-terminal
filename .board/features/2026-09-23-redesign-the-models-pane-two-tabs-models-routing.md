---
id: 00G1
type: work
status: planned
labels: [feature, models, ui, design]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
blocked_by: [WBFM, 4SHY, D49C]
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
verify: {artifact: visual, primary: probe, also: [script, ai-visual], human: optional, criteria: Open all five tabs in a narrow pane and confirm the controls are legible and edits persist., sign_off: none, effort: medium}
source: Owner in a Relay guest session (Claude Code), 2026-09-23
links: {plans: [], commits: [af112403cc9f14debea973f1396a276db41d0ccd, e9f709ef, 5e265e54, 9f3ce6e0, 8bf46bde, 3718b34e, 89e46943, 02331664735940255aa39d6bd7994e21e0c5596f, 6f2cd70bc7a12bfce58f242f3a8b73d2a1475616], evidence: [docs/qa_evidence/2026-09-23-00G1-labels/, docs/qa_evidence/2026-09-23-00G1-redesign/, docs/qa_evidence/2026-09-23-00G1-copy/], related: [RND7, Y2B9, N4PW, AVR8, BXMS, 4BPE, HJ1T, WBFM, 4SHY, D49C, SYTR], github: null}
---
# Redesign the five Models tabs and clarify their labels

## Issue
in general,  the available, priorities, effort, and jobs tabs are all terrible. i thought we had some recent edits that tried to improve those. were there some edits that havent laneded or werent committed or maybe were overwritten

they are just not intuitive, and they are actually slow as well, the checkboxes are not responsive and they are laggy. and its just a small and simple finicky table. are there analogous pages eleswhere we can look at and borrow good design

yes,
no i want the 5 tabs still
but change the tab headers to make them more intuitive, especially available and priorities; jobs too

when i am clicking in it, its alggy and things keep flipping back after i change them
another issue i noticed is, it is still mentioing "this pane", but we changed that right? the model pane doesnt assign specific pane models

## Done means
The Models pane still has exactly five tabs: Providers, Available, Priorities, Effort, and Jobs, with their current responsibilities and saved tab selection preserved.
- Available makes on/off state obvious and responds immediately to a click; quick successive clicks persist correctly and other panes receive the final state.
- Priorities visibly explains ordering, fallbacks, ties/randomization, and the Alt+M box cutoff. Adding, moving, tying, and removing models use discoverable controls.
- Effort shows each model's supported levels and selected level clearly, with a direct way to change it.
- Jobs clearly distinguishes inherited routing from a custom list, and its list can be edited without a hidden interaction or modal rank dialog.
- Providers remains a distinct place to manage accounts and model sources; all five tabs fit and work in a narrow pane.
- Verification includes interaction timing, storage/routing behavior, targeted tests, and screenshots from the running UI.
- The five visible tab headers describe their tasks in plain language. In particular, Available becomes **Enabled**, Priorities becomes **Pick order**, and Jobs becomes **Agent jobs** (or shorter wording with the same meaning if narrow-pane testing requires it). Stable internal tab IDs and saved selection continue to work.

## Plan
**Freshened 2026-09-26 (stale-card review, HEAD `6514be0c`).** The five-tab redesign landed on 2026-09-23 and 2026-09-24. The reopened lag and flip-back report now has a fix that another session verified. Three newer cards, all held by the live pane `cae84571`, own the remaining Models-pane fixes. This card no longer plans redesign work. It owns only the umbrella acceptance: one separate session checks the result against `## Done means`. The original plan and the 2026-09-23 execution split are in the thread and in git history (this file at `6514be0c`).

**Findings**
- Five tabs with stable IDs: `src/ModelsPane.cpp:165-171`. The visible labels are Sources | Enabled | Pick order | Effort | **Job rules**, shortened to Order / Rules at narrow width (`src/ModelsPane.cpp:458-462`). `## Done means` line 8 says **Agent jobs**. After the owner said "agent jobs is NOT pane specific. it should be for setting rules", #WBFM Track B (`ff7c872d`) renamed it.
- Lag and flip-back, reopened 2026-09-24: `d6f769f6` (#HJ1T) coalesces the cross-pane fan-out behind a 200 ms timer (`src/RelayWindow.h:2693-2703`). `6f2cd70b` (this card, salvaged through #3BM5) keeps a worker's role report from redrawing a control that is being clicked (`ModelsPane::setRoleSummaries`, `src/ModelsPane.h:137`; test `workerRoleReportDoesNotRedrawAnActiveModelControl`, `tests/modelspane_test.cpp:481`). On 2026-09-25, #HJ1T's verifier used rev `54018502` and `docs/qa_evidence/2026-09-25-verify-HJ1T/`. It clicked 4 times in 622 ms under Xvfb; each box changed at once and stayed changed, and neither symptom reproduced. No per-click latency figure has been recorded.
- "this pane" wording: `02331664` fixed the header and helper copy. Two leftovers remain. The Jobs "live (this pane)" column (`src/JobsTab.cpp:368-375, 796`) is being removed by #WBFM's follow-up; it is uncommitted in `src/JobsTab.*` under `cae84571`. The "· current" suffix on Enabled, Pick order and Effort (`src/ModelPicker.cpp:885, 954`) belongs to #4SHY and #D49C.
- Tests at HEAD: `modelpicker` and `jobstab` pass. `modelspane` and `settings` each fail one case: they still expect `"Helper Agent (Alt+Q)"` (`tests/modelspane_test.cpp:782`, `tests/settingspane_test.cpp:1675`) after #E8V1 renamed the button to "Agent (Alt+Q)". #SYTR (inbox) tracks that failure. This card did not cause it.

**Owned by other cards (not repeated here)**
- #WBFM (executing, `cae84571`) owns pick-order section headers and rank numbers, Effort spacing, Jobs as global rules with #WK7C, and the Sources removals. That work landed in `2db96643`…`ff7c872d`; the Jobs "live (this pane)" removal is still in progress.
- #4SHY (executing, `cae84571`) makes Enter and Space toggle the boxes on Enabled and Pick order. It also removes "· current" from the hosted tabs and says why Pick-order rows are muted.
- #D49C (executing, `cae84571`) removes the single "current" model and adds rank-1 draws on restore, mode switch, labels and `/swap`.
- #SYTR (inbox) fixes the stale helper-button test expectation.
- #9ACN and #15ZE (planned) are test faults filed in this card's first commit. #HJ1T's verifier reports both now pass, but each card still needs its own close.

**Steps:** see `## Tasks`. After the owner answers, `planned` → a separate session verifies → a QA lane.

**Risks:** Verifying before `cae84571` lands its three cards would record their open bugs against this card. `## Done means` line 8 names "Agent jobs"; until the owner answers Q1, a strict verifier would fail "Job rules".

**Verify:** `ctest -R '^(modelpicker|modelspane|jobstab|settings)$'` green. Walk each `## Done means` line live under Xvfb with an isolated profile, at 420 px and at normal width. Include one measured click-to-repaint time on Enabled and a check that the other panes receive the final state. Save screenshots of all five tabs to `docs/qa_evidence/<date>-verify-00G1/`. The owner's hands-on check of the lag is optional (`human: optional`).
**2026-09-26 scope update.** Keep Job rules as the fifth tab. The owner reports that click lag and checkbox flip-back are now okay. This umbrella awaits #WBFM, #4SHY and #D49C, then one independent verification pass; new redesign requests get separate cards.

## Tasks
- [x] Five plain-language tab headers, stable tab IDs, all five fit at 420 px — `af112403` <!-- t:g1 -->
- [x] Sources: accounts first, then local servers, then profiles — `e9f709ef`, `5e265e54` <!-- t:g2 -->
- [x] Enabled: searchable provider groups, immediate On/Off — `9f3ce6e0` <!-- t:g3 -->
- [x] Pick order: visible add/move/tie/remove, fallback and Alt+M cutoff help, narrow fit — `9f3ce6e0`, `3718b34e` (headers/rank polish: #WBFM) <!-- t:g4 -->
- [x] Effort: supported levels and direct per-row selection — `89e46943` (spacing: #WBFM) <!-- t:g5 -->
- [x] Jobs: inherited vs custom, inline ranked editing — `8bf46bde` (global rules and "Job rules" label: #WBFM `ff7c872d`) <!-- t:g6 -->
- [x] Copy: shared settings, no "opened from" pane — `02331664` <!-- t:g7 -->
- [x] Lag / flip-back: coalesced fan-out `d6f769f6` (#HJ1T, verified 2026-09-25) and no redraw under an active click `6f2cd70b` <!-- t:g8 -->
- [ ] Owner answers Q1–Q3 on the thread (2026-09-26) <!-- t:g9 -->
- [ ] #WBFM Jobs "live (this pane)" removal, #4SHY and #D49C land (session `cae84571`) <!-- t:gb -->
- [ ] #SYTR updates the stale "Helper Agent (Alt+Q)" expectations so `modelspane` and `settings` are green <!-- t:gc -->
- [ ] Separate verifying session: every `## Done means` line at 420 px and normal width, a measured click-to-repaint time, screenshots → `docs/qa_evidence/<date>-verify-00G1/`, then `## QA checklist` and `## Verdict` <!-- t:gd -->

## Decisions
- Owner, 2026-09-23: "no i want the 5 tabs still". Preserve five distinct pages and their responsibilities: Providers, Available, Priorities, Effort, and Jobs.
- Owner, 2026-09-23: "but change the tab headers to make them more intuitive, especially available and priorities; jobs too". Rename their visible headers. Proposed labels: **Sources, Enabled, Pick order, Effort, Agent jobs**. Keep stable internal IDs so saved tab selection still opens the same page.
- 2026-09-26 — Owner: “yes to all. 00g1 1 yes 3 its ok now”. Keep **Job rules** as the fifth tab. The earlier lag and checkbox flip-back are no longer observed. Close this umbrella after #WBFM, #4SHY and #D49C land and a separate verification pass.

## Execution Summary
2026-09-23, first slice: commit `af112403` changed the five visible tab headers to Sources, Enabled, Pick order, Effort, and Agent jobs; at 420 px, Order and Roles replace the two longer labels. The internal tab IDs remain unchanged. A 2 px reduction on each side lets all five tabs fit. The broader per-tab control redesign remains open on this card.

![Five tabs at 420 px on the Pick order page](docs/qa_evidence/2026-09-23-00G1-labels/03-priorities.png)
![Five tabs at 420 px on the Agent jobs page](docs/qa_evidence/2026-09-23-00G1-labels/05-jobs.png)
2026-09-23, five-tab rebuild: Relay subagents implemented Sources (account groups first, then local models and profiles), Enabled (searchable provider groups with immediate On/Off editing), Pick order (readable ranked lists, visible add/move/tie/remove actions, fallback and Alt+M cutoff help), Effort (supported levels and direct per-row selection), and Agent jobs (inherited/custom status and inline ranked-list editing). Internal tab IDs and stored routing formats remain stable. The final app build was captured from an isolated profile, including a 420 px narrow-pane check.

![Sources accounts in the running UI](docs/qa_evidence/2026-09-23-00G1-redesign/06-live-sources.png)
![Sources local-model section in the running UI](docs/qa_evidence/2026-09-23-00G1-redesign/12-live-local.png)
![Enabled model groups in the running UI](docs/qa_evidence/2026-09-23-00G1-redesign/07-live-enabled.png)
![Pick order actions in the running UI](docs/qa_evidence/2026-09-23-00G1-redesign/08-live-order.png)
![Effort selectors in the running UI](docs/qa_evidence/2026-09-23-00G1-redesign/09-live-effort.png)
![Agent jobs inline editor in the running UI](docs/qa_evidence/2026-09-23-00G1-redesign/11-live-jobs-selected.png)
2026-09-23 copy correction (`02331664`): The Models header now calls these shared settings and directs per-pane active model changes to each pane’s model box. The helper context no longer describes an “opened from” pane, and its tooltip names models and routing. Agent jobs now says “each agent pane’s active model” for agent turns and calls “runs on” the latest worker report. No routing or storage behavior changed in this commit. The separate report of lag and controls reverting remains open on this card.

![Shared-settings header and Enabled tab at 420 px](docs/qa_evidence/2026-09-23-00G1-copy/02-available.png)
![Agent jobs wording at 420 px](docs/qa_evidence/2026-09-23-00G1-copy/05-jobs.png)

## Tests
- `ctest -R modelpicker`
- `ctest -R modelspane`
- `ctest -R jobstab`
- `ctest -R settings`
- `manual: docs/qa_evidence/2026-09-23-00G1-redesign/`

`RELAY_JOBS=1 scripts/relay-build --target relay-modelpicker-tests relay-jobstab-tests relay-settings-tests relay-modelspane-tests` and `RELAY_JOBS=1 scripts/relay-build --target relay` passed. The four focused CTest suites passed together on the final revision (6.08 s). The 420 px widget capture test passed. The rapid-toggle test checks immediate state after each click, final persisted state, and notifications; a human-perceived latency threshold has not been measured.

### Check 2026-09-25 20:55
- passed · ctest:modelpicker — ctest -R modelpicker passed for this revision on spark-dcc9, 2026-09-26T00:55:41Z
- failed · ctest:modelspane — ctest -R modelspane failed for this revision on spark-dcc9
- passed · ctest:jobstab — ctest -R jobstab passed for this revision on spark-dcc9, 2026-09-26T00:55:41Z
- failed · ctest:settings — ctest -R settings failed for this revision on spark-dcc9
- not-applicable · manual:docs/qa_evidence/2026-09-23-00G1-redesign/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-00G1-redesign/
- notice · ctest:modelpicker — ctest -R modelpicker is slow: p95 2.62 s, p50 2.59 s
- notice · ctest:modelspane — ctest -R modelspane is slow: p95 1.86 s, p50 1.62 s
- notice · ctest:jobstab — ctest -R jobstab is slow: p95 1.34 s, p50 0.95 s
- warning · manual:docs/qa_evidence/2026-09-23-00G1-redesign/ — manual evidence docs/qa_evidence/2026-09-23-00G1-redesign/ is not there
- notice · ctest:settings — ctest -R settings failed the last time it ran, 2026-09-26T00:55:44Z
history: thread
