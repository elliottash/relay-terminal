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
