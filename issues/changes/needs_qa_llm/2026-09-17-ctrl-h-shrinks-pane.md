---
id: G152
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zzv
created: '2026-09-17'
acceptance: taking control with Ctrl+H (and returning) leaves every pane's size unchanged in a three-pane layout
source: '`issues/bug_intake.txt`, 2026-09-17: "something weird happened with the panes. when i did ctrl + H, it made the pane (the 3rd one) extremely tiny."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Ctrl+H shrinks a pane to almost nothing

## Report

Owner, 2026-09-17, three panes open: pressing Ctrl+H (take control) made the third pane extremely small.

## Suspects

- `setNative(true)` hides the whole composer (`m_composer->setVisible(false)`), which changes the pane's
  minimum height; the enclosing `QSplitter` then redistributes sizes and never restores them when the composer
  comes back.
- Splitter sizes are not saved around the visibility change; the new window-state saving (`src/WindowState.*`)
  may then persist the collapsed sizes.

## Fix direction

Keep the pane's size fixed across composer visibility changes: record the splitter sizes before hiding and
restore them after showing, or give the composer a zero minimum so hiding it does not change the pane's
minimum size.

## Cause

The first suspect, but through the **minimum width**, not the height — which is why it bites a row of panes
side by side.

The prompt box's chip row (directory, mode, tasks, context, model picker, microphone) is wider than a pane
in a three-pane split, so the composer's minimum width set the whole pane's minimum width. Three panes each
demanding ~500 px do not fit in a 1400 px window: the splitter is **over-constrained** and can only violate
the minimums. A splitter in that state redistributes all of its children the moment one of those minimums
changes — and hiding the prompt box drops that pane's minimum to the terminal's.

Measured under Xvfb with three panes, using the splitter sizes the saved window layout records:

```
01-after-splits        h[461, 461, 460]
02-took-control        h[525, 523, 334]     <- Ctrl+H
03-gave-it-back        h[461, 461, 460]
```

The third pane loses 126 px to its neighbours, and step 02 is what the saved layout stores if Relay is
quit while control is taken. With more panes, or a narrower window, the loss is proportionally worse —
"extremely tiny" in the report.

Recording and restoring the splitter sizes around the change was tried first and is **not** sufficient on
its own: the splitter re-clamps to the minimums on the next layout pass, and in an over-constrained layout
the old sizes are not a stable solution (`h[525, 523, 334]` only moved to `h[521, 519, 342]`). The
composer's minimum has to stop being the pane's.

## Implemented

- `src/main.cpp`, composer `buildUi()`: the composer frame gets `QSizePolicy::Ignored` horizontally, so it
  contributes nothing to the pane's minimum width (`qSmartMinSize` skips an Ignored axis). The chip row is
  squeezed in a narrow pane instead — which it already was, since the panes were below its minimum anyway —
  and the pane's minimum width stays the terminal's. Hiding and showing the prompt box then changes no
  minimum that the splitter cares about.
- `src/main.cpp`, `Pane::keepPaneSizes()`: belt and braces for the other axis and for any future child that
  comes and goes. `setNative()` runs the visibility change with every enclosing splitter's sizes recorded
  and puts them back, once straight away and once after the layout has run.
- `src/PaneLayout.{h,cpp}`: `enclosingSplitters(pane)` and `restoreSizes(splitters, sizes)` — the pure half,
  in the library that already owns splitter decisions with no window attached. `restoreSizes` skips a
  splitter that has gone away or whose child count changed, so stale sizes are never forced onto it.
- `tests/panelayout_test.cpp`: `enclosingSplittersAreListedInnermostFirst`,
  `restoreSizesPutsThePanesBackAfterAMinimumChanges` and `restoreSizesSkipsASplitterThatChanged`.

The saved layout needs no separate guard: with the sizes stable there is no collapsed size to persist, and
the harness reads the sizes back out of `windows.json` after every step, so the file is what is being
checked.

Build: `cmake --build build` with no new warnings. `ctest --test-dir build` 16/16; `./scripts/test.sh` 511
tests pass.

## Evidence

`docs/qa_evidence/2026-09-17-bugfix-batch1/`, harness `panes.sh <engine> <h|v> <panes>` (Xvfb). It splits,
then prints the splitter sizes from the saved window layout after Ctrl+H and Ctrl+Shift+H, twice each:

| run | after splits | took control | gave it back | again | again |
|---|---|---|---|---|---|
| relay, 3, horizontal | 461,460,461 | 461,460,461 | 461,460,461 | 461,460,461 | 461,460,461 |
| relay, 3, vertical | 274,275,274 | same | same | same | same |
| relay, 4, vertical | 205,205,205,205 | same | same | same | same |
| relay, 2, horizontal | 693,692 | same | same | same | same |
| relay, 2, vertical | 413,413 | same | same | same | same |
| konsole, 3, horizontal | 461,460,461 | same | same | same | same |
| konsole, 3, vertical | 274,275,274 | same | same | same | same |
| konsole, 2, vertical | 413,413 | same | same | same | same |

The before-the-fix numbers above (`h[525, 523, 334]`) come from the same harness on the unchanged build.

## QA checklist

1. Open three panes side by side (Ctrl+P twice), press Ctrl+H, then Ctrl+Shift+H, several times: no pane may
   change width at any point, and the prompt box must still disappear and come back.
2. Repeat with three panes stacked (Ctrl+Shift+P twice) and with a mixed layout (one horizontal split, then
   split one of those vertically): sizes must hold in both splitters.
3. Repeat 1 and 2 on the other engine (`--engine=konsole` and `--engine=relay`).
4. Drag a divider to make the panes deliberately uneven, then take control and give it back: the uneven
   sizes must be exactly what comes back, not an even split.
5. Take control, quit Relay, start again: the reopened layout must have the sizes from step 4, not a
   collapsed pane.
6. Check the other things that hide the prompt box: F12 (native input) and back, a password prompt
   (`sudo true`, then Esc), and a full-screen program (`vim`, then `:q`). None may resize a pane.
7. In a single-pane window, Ctrl+H must still give the terminal the whole space and Ctrl+Shift+H give the
   prompt box back.
8. Narrow the window until the panes are clearly too small for the chip row: the row may be clipped, but the
   panes must stay equal and the terminal must keep working. Widen it again and the row must come back.
9. On the owner's KDE desktop, repeat 1 with three panes and the Relay engine, since that is where it was
   reported.
