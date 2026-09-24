---
id: RKP3
type: work
status: needs-verification
labels: [feature, models]
assignee: agent
implemented_by: kimi/kimi-k3
session: a352a041-d9f2-4234-9a03-49182f601b42
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-22'
source: pane, 2026-09-22
links: {commits: [02c32b7813fcc5f0dceea730bab65d23834fe6ee], evidence: [docs/qa_evidence/2026-09-22-rank-reorder-repro/], related: [YX8Q], plans: [], github: null}
---
# Priorities page: move buttons, cross-section drags, honest footer

## Issue
ok, i think the issue was, the UI wasnt that responsive. can you improve that?

add up / down buttons at the left in addition to alt up/down and dragging.

and yes allow cross section dragging, and allow adding into each section. 

and review the instructions at the bottom, alt+numbers dont work any more. and they could be simpler and more targeted to the priorities pane usage

## Plan
**Goal:** make rank changes obvious and responsive on the priorities page.

**Findings:** all picker logic is `src/ModelPicker.cpp` (+`src/ModelPicker.h`): `addListRow` draws listed rows, `moveSelected` does alt+↑↓, `commitDragOrder` stores drags (currently groups by TierRole, which a cross-section drag leaves stale — the moved row keeps its source tier role, which is also why the old code silently did nothing), `updateFooter` writes the hints. Tests in `tests/modelpicker_test.cpp` mirror the column enum.

**Steps:**
1. New column `ColMove` right after `ColRank` (renumbers the enum; update the test file's mirror). Hidden on the `all` tab like drag is.
2. `addListRow`: a two-button (QToolButton arrows) widget via `setItemWidget`, click = the `moveSelected` edit for that row (refactor to `moveRow(row, delta)`); ▲ disabled at rank 1, ▼ at last rank.
3. Rewrite `commitDragOrder`: group rows by *physical* section (walk items, class headers set the current section), validate that every key that left a list arrived in exactly one other (else: redraw + notice, covers the filtered case), then store each changed tier preserving efforts; one undo step per changed tier.
4. Footer: drop "←→ alt+1…", write one short line naming ▲▼/alt+↑↓, drag between sections, del, ctrl+enter, ctrl+z; hosted prefix becomes tab / shift+tab.
5. Tests: cross-section drag stored for both lists, drop into empty section, filtered drop refused with notice, buttons move + disabled at ends, footer has no alt+1.

**Risks:** renumbering the column enum touches many lines (mechanical); item widgets on uniformRowHeights rows (fixed small size, margins 0).

**Verify:** `ctest --test-dir build -R modelpicker` (and modelspane), then Xvfb drive: cross-section drag end-to-end persisted to relay.conf, screenshot of the buttons.

## Done means
On the priorities page (and any single-class page) of the models picker:

1. Every listed row shows small ▲▼ buttons in a new column at the left; clicking one moves the row one rank (same edit as alt+↑↓, undoable, persisted); the button that would leave the list is disabled.
2. Dragging a row across a section header moves the model into the target section at the dropped rank — removed from the source list, inserted in the target list, persisted for both, one rebuild, selection follows the model. Dropping into an empty section adds it there.
3. A drop that still cannot be stored (a filter hiding listed rows) says so in the limits line instead of silently snapping back.
4. The footer no longer mentions alt+numbers; it names the buttons, drag-between-sections, del, ctrl+enter, ctrl+z in one short line.

Failure shows as: a cross-section drag snapping back or corrupting either list, buttons missing/dead, or stale footer text. Verified by `ctest -R modelpicker` and a live Xvfb drive (docs/qa_evidence/2026-09-22-rank-reorder-repro/).

## Tasks
- [x] ColMove column with ▲▼ buttons per listed row; click = same edit as alt+↑↓; disabled at list ends <!-- t:gz -->
- [x] commitDragOrder: physical-section grouping; cross-section drag moves between lists; empty-section drops add <!-- t:fv -->
- [x] Unstoreable drop (filter hiding rows) prints a notice instead of silent snap-back <!-- t:gt -->
- [x] Footer rewrite: no alt+numbers, short priorities-targeted line <!-- t:9s -->
- [x] Unit tests for buttons, cross-section drag, refusal notice, footer <!-- t:sy -->
- [x] Build, ctest -R modelpicker/modelspane, Xvfb live evidence <!-- t:3d -->


## Execution Summary
All in `src/ModelPicker.cpp` (+`.h`), tests in `tests/modelpicker_test.cpp` and `tests/modelspane_test.cpp`:

1. **▲▼ buttons** — new `ColMove` column after the rank: two 18px arrow buttons per listed row, the same `moveKey` edit as alt+↑↓ (click deferred a turn so the rebuild doesn't delete the signalling button; ▲ disabled at rank 1, ▼ at the last rank). Hidden on the `all` tab, like the drag. A click also fires the shortcut hint `models.move.buttons` (“Next time: Alt+↑ / Alt+↓”, on the limits line — the picker has no toast queue), per the standing hint rule.
2. **Cross-section drags** — `commitDragOrder` now groups rows by the section they are *physically* in (a dragged row keeps its source TierRole, which is why the old role-based grouping read a crossing drag as “nothing moved”). A crossing drag is stored as a move: out of the source list, into the target at the dropped rank, effort travelling with the key; empty sections take drops; an emptied list is stored empty. One undo step per changed tier.
3. **Honest refusal** — a drop whose drawn rows aren't exactly the listed rows (a filter hiding rows, or a row left above the first header) stores nothing, redraws, and says why on the limits line instead of snapping back silently (#YX8Q).
4. **Footer** — the hosted prefix is now “tab / shift+tab: the pane's tabs” (the alt+digits lie is gone); the priorities line is one short sentence: ▲▼ or alt+↑↓ moves · drag to reorder or into another section · del removes · type + ctrl+enter adds · ctrl+z undoes · the in-box ticks.

Live evidence (Xvfb, seeded lists, `docs/qa_evidence/2026-09-22-rank-reorder-repro/drive4.py`, screenshots r1–r3): ▼ on main rank 1 persisted main m1,m2,m3 → m2,m1,m3; dragging m1 onto high's rank 2 persisted high h1,h2 → h1,h2,m1 and main → m2,m3.

## Tests
- `QT_QPA_PLATFORM=offscreen ./build/relay-modelpicker-tests` — 51 passed (new: `theMoveButtonsMoveARowAndAreDisabledAtTheEnds`, `aDragAcrossASectionHeaderMovesTheModelBetweenLists`, `aDropIntoAnEmptySectionAddsItThere`, `aDropWithRowsFilteredAwayIsRefusedAndSaysWhy`, `theFooterNamesTheRealKeys`; `aDragInsideASectionRewritesItAndACrossingDragDoesNothing` updated to the move behavior)
- `QT_QPA_PLATFORM=offscreen ./build/relay-modelspane-tests` — 19 passed
- `manual: docs/qa_evidence/2026-09-22-rank-reorder-repro/` — drive4.py end-to-end under Xvfb: button click and cross-section drag both persisted to relay.conf; screenshots r1-priorities.png, r2-after-down-button.png, r3-after-cross-drag.png

## Try it
Open it: `bash docs/qa_evidence/2026-09-22-tryit-RKP3/stage.sh` — then press Ctrl+Shift+M in the Relay window that comes up (it opens on Priorities).

The staged Relay has three seeded classes — high, main, flash — with a few invented models each. Click a row's ▼ once, then drag any row up into the high section and let go. Glance at the bottom line of the pane while you do.

Question (about 2 minutes): could you tell, without being told, what the ▲▼ do and that dragging into another section would move the model there — and does the one-line footer tell you what you need, or is it still noise?

Expected: docs/qa_evidence/2026-09-22-tryit-RKP3/expected.md (sealed until you answer)
