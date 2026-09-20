# #SDXE — pane sizes jiggle in response to content

> when agents / shells are working and content is coming into panes, they can become wider
> dynamically, in a jerky / ugly way. pane sizes should not change in response to content.

## What was happening

A pane grows when its **minimum** grows. Every pane sits in a `QSplitter` built with
`setChildrenCollapsible(false)`, and a splitter must satisfy every child's minimum size: the moment
one child's minimum passes the width it has, the splitter widens that child and takes the pixels
off its neighbours. Two widgets in the pane header had minimums that followed what they were
showing:

- `PaneUsageChip` — `QSizePolicy::Fixed`, so Qt takes its **size hint** as its minimum, and that
  hint is the width of "cpu 7%" or "cpu 100% · mem 42%", re-measured 2.5 times a second while the
  pane is busy. (An override of `minimumSizeHint()` on a Fixed widget counts for nothing; this is
  why the policy had to change too.)
- the pane title `QLabel` — a QLabel's own minimum is its whole text, and the model rewrites that
  text as the work moves on.

The same shape was in `PaneHeaderChip` (its minimum was its whole text until the ladder had granted
it a width, and for the phone chip, which the ladder never drives, for good) and in
`PaneSubagentBadge` (Fixed, hint follows the count) — neither is exercised by the run below, both
are fixed the same way.

## How it was measured

`RELAY_LAYOUT_LOG=1` (new, card #SDXE) makes every pane print one line a second for its header row
and one for its own column: the pane's width, its minimum, and what each visible widget contributes
to that minimum — `relay::panes::layoutMinimumWidth`, which is Qt's own `qSmartMinSize` rule written
down. `measure.py` drives the built Relay under Xvfb with an isolated profile, makes three panes in
a 1200 px window and then does nothing but produce **content**: a CPU burner (`yes > /dev/null`),
a long pane name of the kind an agent writes, and a short one again.

    Xvfb :59 -screen 0 1600x1000x24 &
    DISPLAY=:59 python3 measure.py > after.txt          # layout-log.txt beside it

## Before (`before.txt`, `layout-log-before.txt`)

    pane #1: widths [296, 335, 339], minimums [266, 292, 335, 339]
        width during idle:             [296]
        width during busy:             [296, 335]     <- the CPU burner alone widened it
        width during busy+rename:      [335, 339]     <- the long title widened it again
        width during busy+short-title: [296]
    pane #2: widths [563, 566, 592]                   <- the Switchboard, squeezed by its neighbour
    pane #3: widths [282, 283, 296]
    FAIL pane widths: moved in #1, #2, #3

The header lines say which widget did it: `paneUsageChip=61` appearing in pane #1's minimum, and
`paneTitle` going 33 → 80 → (in a wider pane, 455). An earlier run of the same scene in a 1500 px
window recorded `min=714` for a pane whose title was one sentence long.

## After (`after.txt`, `layout-log-after.txt`)

    pane #1 column: widths [296], minimums [270, 278]
    pane #2 column: widths [592], minimums [525]
    pane #3 column: widths [296], minimums [270]
    PASS pane widths: every pane kept one width

Same scene, same content, and no divider moves. The chips still show exactly what they showed
before — they ask for their text through `sizeHint()` and get it whenever the header has the room;
what they may no longer do is make the room.

The pane-column lines are the rest of the audit (plan step 3): a terminal pane's minimum is its
header plus the column's 16 px of margins (`QWidget=10` for the terminal host, `composer=0`), and
the Switchboard pane's 525 is its list/card floor from card #BXCN — a constant, not content.

## The 8 px that is left

Pane #1's minimum still moves by 8 px (270 ↔ 278) as the usage chip comes and goes: a `QHBoxLayout`
spends its spacing between every **visible** item, so a chip that takes no width still costs the
row one gap when it appears. Closing that means taking the 8 px gaps out of the header row and
giving every element its own padding, which re-spaces every pane header by hand — a change to how
the header looks, which is the owner's to make, not a side effect of a bug fix. It cannot move a
pane that is wider than its minimum: in this scene the narrowest pane is 296 px against a 278 px
minimum, and no width moved.

## Tests

`ctest --test-dir build -R panes` — `tests/panelayout_test.cpp` now reproduces the incident in two
throwaway panes: a chip whose minimum follows its text drags the divider (and takes the pixels off
its neighbour), and the same chip with the policy and floor the header's chips have now leaves it
alone. Plus the rule itself: a Fixed widget is held up by its size hint, a shrinkable one by its
minimum size hint, and an explicit `minimumWidth()` replaces both — which is how the title label
keeps the ladder's 80 px floor instead of its whole text.
