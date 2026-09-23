---
id: 00G1
type: work
status: planned
labels: [feature, models, ui, design]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 31883d9c-ec40-4d0e-a526-58a4648ecccd
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Owner in a Relay guest session (Claude Code), 2026-09-23
links: {plans: [], commits: [af112403cc9f14debea973f1396a276db41d0ccd], evidence: [docs/qa_evidence/2026-09-23-00G1-labels/], related: [RND7, Y2B9, N4PW, AVR8, BXMS, 4BPE], github: null}
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

## Decisions
- Owner, 2026-09-23: "no i want the 5 tabs still". Preserve five distinct pages and their responsibilities: Providers, Available, Priorities, Effort, and Jobs.
- Owner, 2026-09-23: "but change the tab headers to make them more intuitive, especially available and priorities; jobs too". Rename their visible headers. Proposed labels: **Sources, Enabled, Pick order, Effort, Agent jobs**. Keep stable internal IDs so saved tab selection still opens the same page.

## Execution Summary
2026-09-23, first slice: commit `af112403` changed the five visible tab headers to Sources, Enabled, Pick order, Effort, and Agent jobs; at 420 px, Order and Roles replace the two longer labels. The internal tab IDs remain unchanged. A 2 px reduction on each side lets all five tabs fit. The broader per-tab control redesign remains open on this card.

![Five tabs at 420 px on the Pick order page](docs/qa_evidence/2026-09-23-00G1-labels/03-priorities.png)
![Five tabs at 420 px on the Agent jobs page](docs/qa_evidence/2026-09-23-00G1-labels/05-jobs.png)

## Tests
- `scripts/relay-build --target relay-modelspane-tests` — passed.
- `xvfb-run -a build/relay-modelspane-tests` — 24 passed, 0 failed.
- Isolated config screenshot run: `RELAY_N4PW_EVIDENCE=docs/qa_evidence/2026-09-23-00G1-labels xvfb-run -a build/relay-modelspane-tests everyTabKeepsItsMainControlsInANarrowPane` — passed; five images captured.
