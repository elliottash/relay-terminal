---
id: AVR8
type: work
status: discussing
labels: [bug, models]
assignee: codex
waiting_on: owner
rank: mavr8
created: '2026-09-22'
source: User request in Relay, 2026-09-22
links: {plans: [], commits: [], evidence: [], related: [MPA2, VPR7], github: null}
---
# Refresh Priorities when Available changes

## Issue
the model priorities page needs to refresh when changing available. i thought i had asked that but its not fixed.

## Done means
- Changes made in Available are reflected in Priorities without restarting or reopening the Models pane.
- Existing unaffected rankings and reasoning levels are preserved.
- A focused GUI regression exercises the availability change and subsequent Priorities view.

## Plan
**Goal:** Make availability edits visible in Priorities.

**Findings:** `src/ModelPicker.cpp::buildTier` only draws saved rankings without a query and searches all usable models regardless of availability. `src/ModelCatalog.cpp::isAvailable` pins terminal-ranked models on. Previous #VPR7 fixed provider catalog delivery, a different path.

**Steps:**
1. Establish availability-to-priority behavior and reproduce with a focused Qt test.
2. Fix the relevant list behavior while preserving unaffected ranks.
3. Run targeted model tests under Xvfb, record evidence, and land.

**Risks:** Availability and saved rankings are currently separate steps; clarify intended membership behavior before changing it.

**Verify:** Model catalog, picker, and Models pane targeted tests with isolated settings; exercise the actual checkbox and tab switch.

## Planning notes
Source trace confirms ModelsPane::showTab calls ModelPicker::setTier, which rebuilds when switching Available → Priorities. The missing behavior is membership: buildTier ignores unranked entries with an empty query, searches allUsable (including unchecked candidates), and isAvailable lets terminal rankings override the checkbox. Clarification requested because assigning/removing saved ranks is different from refreshing candidate visibility; no production code changed yet.
