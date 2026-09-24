---
id: 00G1
type: work
status: needs-verification
labels: [feature, models, ui, design]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 31883d9c-ec40-4d0e-a526-58a4648ecccd
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Owner in a Relay guest session (Claude Code), 2026-09-23
links: {plans: [], commits: [af112403cc9f14debea973f1396a276db41d0ccd, e9f709ef, 5e265e54, 9f3ce6e0, 8bf46bde, 3718b34e, 89e46943], evidence: [docs/qa_evidence/2026-09-23-00G1-labels/, docs/qa_evidence/2026-09-23-00G1-redesign/], related: [RND7, Y2B9, N4PW, AVR8, BXMS, 4BPE], github: null}
verify: {artifact: visual, primary: probe, also: [script, ai-visual], human: optional, criteria: "Open all five tabs in a narrow pane and confirm the controls are legible and edits persist.", sign_off: none, effort: medium}
---
# Redesign the five Models tabs and clarify their labels

## Issue
in general,  the available, priorities, effort, and jobs tabs are all terrible. i thought we had some recent edits that tried to improve those. were there some edits that havent laneded or werent committed or maybe were overwritten

they are just not intuitive, and they are actually slow as well, the checkboxes are not responsive and they are laggy. and its just a small and simple finicky table. are there analogous pages eleswhere we can look at and borrow good design

yes,
no i want the 5 tabs still
but change the tab headers to make them more intuitive, especially available and priorities; jobs too

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
**Goal:** retain the five existing tabs and make their controls clear and responsive. Use patterns from familiar model lists and sortable lists within each tab, while retaining Relay's existing curation and routing storage.

**Findings:** `ModelsPane` hosts Providers, Available, Priorities, Effort, and Jobs. `ModelPicker` shares a `QTreeWidget` across Available, Priorities, and Effort; `JobsTab` has a separate table and modal ranked-list editor. The synchronous cross-pane update after edits was addressed separately by #HJ1T. Rank editing and ties are currently hidden behind a rank-cell click.

**Steps:**
1. Profile an Available toggle and inspect the redraw path after #HJ1T. Keep the clicked control responsive and update only affected rows where possible; preserve immediate persistence and batched cross-pane notification.
2. **Providers:** simplify scan order and labels for providers, accounts, and offered models; preserve its own tab and existing actions.
3. **Available:** show a searchable, provider-grouped model list with an obvious on/off control and clear disabled state. Keep availability separate from effort.
4. **Priorities:** show each class as a readable ordered list with visible add, move, remove, and tie controls. Label groups of tied models as randomized and make fallbacks and the Alt+M box cutoff explicit. Preserve keyboard access and existing rank storage.
5. **Effort:** show supported reasoning levels and the current selection on each model row, with a direct selector; keep Effort as its own tab.
6. **Jobs:** show each job's inherited or custom routing plainly. Edit custom ranked lists inline with the same visible ordering and tie controls used in Priorities, while keeping Jobs as its own tab.
7. Keep all five tab IDs and saved layouts compatible. Add focused tests for availability, ranking/ties, effort, job inheritance/customization, keyboard access, and narrow-pane fit; capture real UI screenshots on an isolated profile.

**Design choices to validate while implementing:** Use explicit tie/untie actions alongside drag and keyboard moves, so randomization is discoverable without typing duplicate rank numbers. Represent the Alt+M cutoff with a labelled control that states which models are included. Check these controls in the actual narrow UI before settling the interaction.

**Risks:** Shared widgets serve multiple tabs and the Alt+M picker; changes must not alter the picker unexpectedly. Several sessions use these files, so claim files before editing and land only this card's hunks.

**Verify:** targeted model picker, Models pane, and Jobs tests; a measured rapid-toggle interaction; manual completion of each Done-means task in a narrow and normal pane; screenshots in `docs/qa_evidence/<date>-00G1/`.
**Visible tab headers:** Start with Sources | Enabled | Pick order | Effort | Agent jobs. Keep the existing `providers`, `available`, `priorities`, `effort`, and `jobs` IDs in tab data and layout state. Check the five labels together at the supported narrow width; shorten only the visible words if needed, and use tooltips to preserve the full meaning. Update tab-label assertions and screenshots accordingly.

**2026-09-23 execution split:** Dispatch Sources (`RelayWindow` provider section and related settings), Enabled (`ModelPicker` availability paths), and Agent jobs (`JobsTab`) in parallel. Pick order and Effort also live in `ModelPicker`, so dispatch them in sequence after Enabled to avoid simultaneous edits to that widget. Each subagent owns only its assigned files, uses `land.py`, runs focused checks without a full suite, and reports a commit. The parent integrates layout and behavior, then builds once at low parallelism after the machine has memory, runs targeted tests, and captures five live tab views.

## Decisions
- Owner, 2026-09-23: "no i want the 5 tabs still". Preserve five distinct pages and their responsibilities: Providers, Available, Priorities, Effort, and Jobs.
- Owner, 2026-09-23: "but change the tab headers to make them more intuitive, especially available and priorities; jobs too". Rename their visible headers. Proposed labels: **Sources, Enabled, Pick order, Effort, Agent jobs**. Keep stable internal IDs so saved tab selection still opens the same page.

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

## Tests
- `ctest -R modelpicker`
- `ctest -R modelspane`
- `ctest -R jobstab`
- `ctest -R settings`
- `manual: docs/qa_evidence/2026-09-23-00G1-redesign/`

`RELAY_JOBS=1 scripts/relay-build --target relay-modelpicker-tests relay-jobstab-tests relay-settings-tests relay-modelspane-tests` and `RELAY_JOBS=1 scripts/relay-build --target relay` passed. The four focused CTest suites passed together on the final revision (6.08 s). The 420 px widget capture test passed. The rapid-toggle test checks immediate state after each click, final persisted state, and notifications; a human-perceived latency threshold has not been measured.

### Check 2026-09-23 23:23
- passed · ctest:modelpicker — ctest -R modelpicker passed for this revision on spark-dcc9, 2026-09-24T03:23:08Z
- passed · ctest:modelspane — ctest -R modelspane passed for this revision on spark-dcc9, 2026-09-24T03:23:08Z
- passed · ctest:jobstab — ctest -R jobstab passed for this revision on spark-dcc9, 2026-09-24T03:23:08Z
- passed · ctest:settings — ctest -R settings passed for this revision on spark-dcc9, 2026-09-24T03:23:08Z
- not-applicable · manual:docs/qa_evidence/2026-09-23-00G1-redesign/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-00G1-redesign/
- notice · ctest:modelpicker — ctest -R modelpicker is slow: p95 2.59 s, p50 1.78 s
- notice · ctest:modelspane — ctest -R modelspane is slow: p95 1.86 s, p50 1.60 s
history: thread
