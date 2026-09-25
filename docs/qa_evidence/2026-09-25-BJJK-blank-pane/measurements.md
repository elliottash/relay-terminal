# #BJJK: a pane drew blank over a buffer holding its conversation

Input: the reopened pane's own saved buffer (`~/.local/state/.../scrollback/c08c8613-….txt`, 487 rows
plus 11 prose records; not committed, it is the owner's conversation). Prose blocks
`af0737e5/1, /2, /13, /16, /23, /26` each anchor two separate row ranges (e.g. `/2` at rows 31–68
and 394–401), because the conversation's text had been replayed into a pane that already held it.

`repro.cpp` feeds those rows to a `VTermBackend` offscreen, registers the prose records, resizes, and
counts non-background pixels and non-empty visible rows (`CORE=libvterm ./repro file 79 1`).

| columns | before the fix | after the fix |
|---|---|---|
| 79 (print width) | 68876 px, 56/60 rows | 68876 px, 56/60 rows |
| 29 | 45012 px, 58/60 | 45012 px, 58/60 |
| 60 | **0 px, 0/60 rows** | 99694 px, 54/60, prompt shown |
| 76 | **0 px, 0/60 rows** | 103755 px, 51/60, prompt shown |
| 100 | 96430 px, 33/60, no prompt | 125600 px, 50/60, prompt shown |

Without the prose records the same rows drew normally at every width before the fix too: the
overlapping ranges were the cause.

Regression: `RELAY_ENGINE_TEST=ViewTest relay-engine-tests proseBlockPrintedTwiceStillDraws` —
fails with the fix disabled (`'shown.contains("PROMPT$")' returned FALSE`, every visible row empty),
passes with it. Whole ViewTest: 79 passed, 0 failed (libvterm core; ghostty is not built here).

## Where the doubled text came from

`save-cycle.cpp` prints a single-copy text, saves it the way `Pane::saveScrollback` does
(`formattedScrollbackText` + `formattedScreenText`), replays the save into a fresh backend at another
width, and repeats. At the bottom of the pane every cycle keeps one copy of each block. With the view
scrolled up before the save (`SCROLLUP=1`), before the fix:

```
cycle 0: hist=238 screen=60  'I have the ground truth' x1  'Previous conversation' x2  PROMPT x0
```

`formattedScreenText()` read the core's *viewport*: scrolled up, it returned history rows, so a
stretch of history was saved twice and the live screen (the prompt) was not saved at all. Every
restart and every reopen replays that file. The owner's pane is 29 columns wide and about 100 rows
tall, which is why a screenful of history went in twice.

Fix: `formattedScreenText()` scrolls the core to the bottom, reads the live screen (what the plain
`screenText()` of both cores already returns), and puts the viewport back.

Regression: `savedScreenIsTheLiveScreenWhenScrolledUp` — fails with the fix disabled (`line N`
saved twice), passes with it. Whole ViewTest: 80 passed, 0 failed.
