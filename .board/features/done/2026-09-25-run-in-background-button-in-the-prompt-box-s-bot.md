---
id: 4CXY
type: work
status: dropped
labels: [feature, voice]
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [ai-visual], human: none, criteria: 'consolecorner passes: the button floats with no layout seat, shares the chips row at the one-line rest height without overlap, sits exactly at the input area''s bottom-right corner once the box grows, stays within the corner column''s footprint so text never runs underneath, and returns to the chips row when cleared. Screenshots in the evidence path show the themed rest and grown states.', sign_off: none, effort: low}
source: pane 1, 2026-09-25
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-4cxy-background-button-corner/], related: [], github: null}
---
# Run-in-background button in the prompt box's bottom-right corner, text wrapping around it

## Issue
move the run in background button into the bottom right corner of the text prompt edit box. the text wraps around it.

## Done means
The ↗ run-in-background button sits inside the prompt box, at its bottom-right corner whenever the box is tall enough to have one below the corner chips (a second line of prompt text), and rides the bottom edge as the box grows. Text never runs underneath it: it wraps before the corner column, which is never narrower than the button. At the box's one-line rest height — where the box's bottom-right corner and the chips row are the same row — the button keeps the seat beside the mode chip it had before, and the idle box gains no height for the button's sake.

## Execution Summary
The composer's input row (editor + corner column) became one widget, `promptInputArea`, so the button can float over it. `backgroundSend` left the corner row's layout and became a child of that widget, positioned by `Pane::placeBackgroundSend()`: bottom-right, flush under the mode chip, when the area has the button's height below the chips row; otherwise the seat beside the mode chip it always had. The geometry that moves it is watched in `Pane::eventFilter`: the input area's resize (the box auto-grows per keystroke and on pane resizes) and the mode chip's move/resize (the `!`/`*`/password chips and a mode retitle re-lay the corner row). It is deliberately not in a layout: the box is one text line tall at rest, and a second corner row would add the button's height to every pane's idle box.

The button's face changed from a bare Fusion QToolButton to a chip: `QToolButton#runInBackgroundButton` now rides beside `QToolButton#stripChip` in the main sheet and the three materials, with a touch less vertical padding (`padding: 1px 8px`) so a lone ↗ glyph sits level with the labelled chips instead of poking below them. The drawn box is now findable as `QFrame#promptBox` (the busy-line test used to find it as the editor's parent; the input-area widget now sits between them).

Text wrapping is unchanged in mechanism and stronger in effect: text already wrapped before the corner column (2026-09-17 design, comment at the `corner` layout), and the button now rides inside that column's footprint under the mode chip, so it can never sit over text at any box height.

## Tests
- `ctest -R consolecorner` (new): `relay-consolemode-tests --corner-only`, a themed run of `theBackgroundButtonTakesThePromptBoxCorner` — asserts the button floats (no layout seat), sits beside the mode chip without overlap at rest, sits exactly at the input area's bottom-right once the box grows, stays within the corner column's footprint (editor ends before the button; button left of the mode chip's left edge is impossible), keeps riding the bottom as the box grows further, and returns to the chips row after the text is cleared. Captures `rest.png`/`grown.png` into `$RELAY_CORNER_EVIDENCE` when set (committed under the evidence path below).
- `ctest -R "consolemode|consolecorner|queuecontract"` — 3/3 pass (the busy-line test updated to find the box by its new `promptBox` name).
- `ctest -R "boardworkspace|boardpane|conversations"` — 3/3 pass; full app target builds green through `scripts/relay-build`.
- `relay-panestatus-tests` fails 2 cases (whoGetsABand, byTypeDiffersByGroupShares) — measured pre-existing at clean HEAD (bug #S7ZM), not this change.
