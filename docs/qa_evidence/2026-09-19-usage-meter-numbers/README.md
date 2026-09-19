# The pane usage meter as numbers — mock-ups (#D03W)

Owner, 2026-09-19: *"the cpu / mem bar things are ugly and unintuitive. i think it should be
numbers. explore and mock it up."* Exploration only — nothing in `src/` was changed.

## What the chip is today

`PaneChrome::PaneUsageChip` (src/PaneChrome.h) paints two little **line drawings** followed by two
bare percentages: a processor die (a 13 px square with a smaller square inside and three pins each
side) then `20%`, a gap, a memory module (a body with four legs) then `1%`. The "bar things" are
those two glyphs — at 13 px the die's six pins read as two stacks of short bars and the module's
legs as a tiny bar chart, so the eye sees graphics where the meaning is entirely in the digits
(`live-relay-dark-chip-8x.png` is the chip at 8×). The ink is `text_muted`, going `warning` at 60 %
and `error` at 85 %; nothing is drawn while the pane is quiet (`live-relay-dark-idle-header-3x.png`
— the header is exactly as it was before the feature), and the tab carries a separate suffix
`· 20% / 1%` with a slash instead of the glyphs (`live-relay-dark-tab-3x.png`). So the two surfaces
already disagree about how to say the same thing, and neither says *which* number is which: the
Sessions pane is the only one that spells it out, as `cpu 20% · mem 1%` (`relay::usage::liveTag`).

Live captures: `live-relay-dark-busy.png` (full window, four `yes` loops and a 1.5 GB python child
in the pane's shell), `live-relay-dark-header-3x.png`, `live-relay-dark-tab-3x.png`,
`live-relay-dark-chip-8x.png`, and the same two idle. Taken under Xvfb with an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` (short path) and `RELAY_KEYRING=off` —
`mockup-harness/live.sh`.

## The candidates

Each sheet shows the same variant across: quiet, normal (12 % of the machine, 1.4 GB of 48 GB),
high (91 % / 62 %, so the warning and error inks), CPU alone, memory alone, a narrow pane (the
header ladder's last rung, `relay::panes::UsageForm::CpuOnly`), and the tab label's suffix. Both
palettes, 1× and `-2x` (a magnification of the real 1× pixels, like the live crops).
`mock-relay-{dark,light}-all.png` is all six side by side at normal and at high load — start there.

| | file | width in the header¹ | pro | con |
|---|---|---|---|---|
| **0** today | `mock-relay-*-0-today.png` | 88 / 43 px | narrowest of the labelled forms; the glyphs are language-free | the glyphs *are* the bar things: decorative, and they carry the only clue to which number is which |
| **a** `cpu 12% · mem 3%` | `mock-relay-*-a-words.png` | 115 / 52 px | reads with no tooltip and no learning; already the Sessions pane's wording, so all three surfaces converge on one string; the CPU-only rung is the same grammar shortened | 27 px wider than today, and "3 % of memory" is still the figure the owner calls unintuitive |
| **b** `12% cpu  3% mem` | `mock-relay-*-b-number-first.png` | 115 / 52 px | the digits land first, which is what the eye is scanning for | two number-then-word pairs with no separator read as a ragged four-token run; and it is the only one of these whose word order the tooltips and the Sessions row would have to be changed to match |
| **c** `12% · 3%` | `mock-relay-*-c-bare.png` | 54 / 52 px | by far the least chrome, and it is what the tab suffix already does | says nothing until you have learned it, and it cannot keep its own grammar: with one half quiet it has to print `12% cpu` anyway (row 4 and 5 of the sheet), so the label appears and disappears |
| **d** `cpu 12% · 1.4 GB` | `mock-relay-*-d-absolute-memory.png` | 102 / 52 px | a size is a fact a person can act on, where a percent of an unknown total is not; it names itself, so no "mem" is needed; and summing *bytes* over a tab is honest where summing percents is a rounding pile | asymmetric — one half a share, one half a size; and its width moves with the number (`986 MB` → `1.4 GB` → `30 GB`), which the hysteresis in `labelShouldFollow` was written to stop |
| **e** `cpu 12% · mem 1.4 GB` | `mock-relay-*-e-words-absolute-memory.png` | 137 / 52 px | every half named and the unintuitive half turned into a size: the only form that needs neither a tooltip nor a convention | the widest, and on the tab (elided at 260 px) `project · cpu 12% · mem 1.4 GB` starts crowding the pane count |

¹ the chip's text at the real face and size, wide form / narrow (CPU-only) form; today's chip adds
14 px of its own padding on top. All five number forms collapse to the same short rung, `cpu 12%`
or `12% cpu` — except **c**, which is the one that has to change grammar to do it.

## Recommendation

**e — `cpu 12% · mem 1.4 GB`**: it is the only candidate that answers both halves of the complaint,
since dropping the glyphs is what makes it numbers and turning the memory percent into a size is
what makes it intuitive, and it costs 35 px of header that the ladder already knows how to reclaim.
If the width is too much, **a** is the same sentence one step cheaper and can adopt the byte figure
later without changing anything else.

Two things that follow from either choice and are cheap while the code is open: the tab suffix
should carry the same string as the chip rather than its own `12% / 3%`, and `relay::usage::liveTag`
already *is* that string — so `tabSuffix`, `liveTag` and the chip become one function.

## Redoing this

```
cmake -S mockup-harness -B /tmp/um -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/um
QT_QPA_PLATFORM=offscreen /tmp/um/usage-mockup data/theme/themes <out-dir>
mockup-harness/live.sh            # the live capture, under Xvfb
```

`mockup-harness/main.cpp` is standalone Qt6 Widgets (never built in `build/`). It parses the real
`data/theme/themes/relay-{dark,light}.toml` for its palette, uses the application body face at
`theme::BodyPt`, and variant 0 copies `paintDie()`/`paintModule()` out of `src/PaneChrome.h` line
for line — so the first row of every sheet can be checked against the live capture and the rest
read against it. It prints each candidate's chip width to stderr.
