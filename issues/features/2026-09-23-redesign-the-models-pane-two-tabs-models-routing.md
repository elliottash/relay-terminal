---
id: 00G1
type: work
status: discussing
labels: [feature, models, ui, design]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 3eb5e6f8-b4a5-4638-8963-891c0e17b166
waiting_on: owner
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Owner in a Relay guest session (Claude Code), 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [RND7, Y2B9, N4PW, AVR8, BXMS, 4BPE], github: null}
---
# Redesign the Models pane: two tabs (Models, Routing) borrowed from Cursor/Raycast/Linear

## Issue
in general,  the available, priorities, effort, and jobs tabs are all terrible. i thought we had some recent edits that tried to improve those. were there some edits that havent laneded or werent committed or maybe were overwritten

they are just not intuitive, and they are actually slow as well, the checkboxes are not responsive and they are laggy. and its just a small and simple finicky table. are there analogous pages eleswhere we can look at and borrow good design

yes,

## Done means
The Models pane has two tabs, **Models** and **Routing**, replacing Available, Priorities, Effort and Jobs. Providers stays as it is.
- A new user can, with no instructions: turn a model on or off, set its effort, reorder a class, tie two models so one is picked at random, and give Planning its own models.
- There are no modal dialogs (rank box, Jobs list dialog), and no editing that works only by clicking a hidden cell.
- Every control responds within a frame. Toggles and drags change the row in place, and the other panes hear about it on the coalesced path (#HJ1T).
- Failure looks like: any of those tasks needing a tooltip or a pop-up, or a whole-table rebuild on a toggle.

## Plan
**Goal:** replace the four finicky table tabs with two simple, fast surfaces, borrowing proven patterns:
- Cursor / VS Code Copilot "Manage Models": a flat toggle list.
- Raycast AI: effort shown on the model's own row.
- Linear / macOS service order / OpenRouter routing: short drag lists per class.
- Continue.dev / Zed roles: jobs that say "same as Main" until customised.

**Findings:**
- `src/ModelPicker.cpp` (1,869 lines) draws Available, Priorities and Effort from one `QTreeWidget` with 10 columns, switched by `m_tier`. `rebuild()` clears and redraws every row. Per-row widgets (`setItemWidget` for ▲▼) are expensive in a `QTreeWidget`.
- Ranks are edited through a `QInputDialog` opened by clicking the rank cell (`ModelPicker.cpp:370`).
- `src/JobsTab.cpp` (875 lines) is a second table, and its ranked lists live in a modal `QDialog` (`editRankedOverride`).
- `src/ModelsPane.cpp` hosts both. Storage is `models::curation` in `src/ModelCatalog.cpp`: `tierList`, `setTierRank`, `setAvailable`, box cutoff. The Jobs overrides are `rolestore`.
- Storage and routing (#RND7 ties, weighting) stay exactly as they are. This is a UI rewrite only.

**Steps:**
1. **Models tab** (new `src/ModelsListView.{h,cpp}`). One row per model, grouped under provider headers, with a search field on top. Each row reads: name · effort chips (low / med / high / xhigh, only the levels that model has) · context and speed in grey · on/off switch on the right. Build it with `QListView` and a delegate painting a `QAbstractListModel` (no item widgets). A toggle changes one row's data (`dataChanged`) and never rebuilds the view. This replaces Available and Effort.
2. **Routing tab** (new `src/ModelsRoutingView.{h,cpp}`). One card per class (Main, High/Plan, Flash, Local), then one per job (Planning, Subagents, Helper).
   - Each class card is a short vertical list with a ⋮⋮ grip and ⌥↑↓ to reorder. Dropping a row onto another row (not between rows) ties them: a bracket plus a "random" chip, and a drag out or "untie" in the context menu separates them. The top group is labelled "runs on" and the rest "fallbacks". A "+ add model" line at the end opens an inline filtered completer, not a dialog.
   - Each job card is collapsed to one line with a dropdown: "Same as Main (Fable 5.1)", or "Custom…". Custom expands the same drag list inline.
   - The "show in alt+m box" cutoff becomes a divider line you drag, rather than a checkbox on every row.
3. Wire both views to the existing `curation::` / `rolestore::` setters, and have them call `ModelPicker::changed()`'s equivalent so the coalesced `modelsCurated()` (#HJ1T) tells the panes.
4. Retire the old tabs from `ModelsPane::tabIds()`, and delete the dead paths in `ModelPicker` that only served them. The alt+m box picker, which also uses `ModelPicker`, keeps working. Old tab ids in saved layouts map to the new tabs.
5. Tests: model/view tests that toggling, effort and tie/untie write the right settings and do **not** rebuild the view (count `modelReset`). Also: drag reorder, a job going from "same as" to custom and back, and a narrow-pane layout. Capture isolated Xvfb screenshots of both tabs.

**Risks / decisions for the owner:**
- Q1: are two tabs (Models, Routing) right, or keep Effort as its own tab? Recommendation: two, with effort on the row.
- Q2: tie by drag-onto (Linear-style) plus an "untie" menu item, rather than typing rank numbers? Recommendation: yes. Numbers go away in the UI; storage keeps ranks.
- Q3: should the "in box" cutoff become one draggable divider per class? Recommendation: yes.
- Several other sessions touch `ModelPicker.cpp` and `JobsTab.cpp`. New views go in new files so the rewrite does not collide, and the old code is removed only in step 4.

**Verify:** the new view tests; `relay-modelspane-tests`; manual run on an isolated profile, doing each Done-means task with no instructions, with screenshots in `docs/qa_evidence/2026-09-2x-00G1/`.
