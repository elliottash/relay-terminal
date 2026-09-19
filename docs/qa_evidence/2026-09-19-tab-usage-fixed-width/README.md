# The tab label's fixed-width usage suffix — implementer evidence (#MERX)

The tab header now carries **`  ·  cpu 30% · mem 05%`** with both halves always printed and each
percent **two digits wide**, and it takes its reading **once every 5 s, from the mean of the 5 s
before it**. Owner, 2026-09-19: *"for the tab headers, always show cpu 00% mem 00% and 01% or 05%,
always use 2 digits, so they dont keep on widening and narrowing. update only once every ~5 secs
or so, using the 5 sec average."*

The pane chip, the Sessions tag and the tooltips are unchanged: a half with nothing to say is
still left out there, at the poll's 2.5 Hz. Only the tab label has its own clock
(`relay::usage::RollingMean`, `kTabUpdateMs` = `kTabWindowMs` = 5 s) and its own fixed-width
wording (`formatPercent2`), because only its width is the whole tab bar's layout.

`drive.sh` is the capture — the recipe of
`docs/qa_evidence/2026-09-19-usage-meter-words/drive.sh` (#6BGA), cut down to the tab: Xvfb, an
isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`, `RELAY_KEYRING=off`, nothing
typed into the prompt box; the load is started by the sandbox's own `~/.bashrc`, so the busy
processes are children of the pane's shell. Six `yes` loops are ~30 % of this machine's 20 cores
and a 6 GB `bytearray` ~5 % of its 121 GB — two digits on both axes. On a signal the shell kills
three of the six loops, halving the CPU reading mid-run. Nine-plus crops are taken 1.25 s apart
and compared **pixel for pixel over the label's region** — the tab icon beside it pulses while
the pane is busy, so the strip as a whole always differs; `cadence.log` is that comparison with
each crop's OCR beside it (tesseract reads the middle dot as `-`; the `= x +` at the end is the
tab bar's buttons).

## The shots

| file | what it shows |
|---|---|
| `implementer-tab-busy-0.png` | the label at steady load, 3×: `project  ·  cpu 30% · mem 05%` — both halves, two digits |
| `implementer-tab-halved-0.png` | the moment the load halved: still `cpu 30%` — the label has not moved yet |
| `implementer-tab-halved-1.png` | one take later: `cpu 28%` — the 5 s window's mean, not the 15 % the pane reads now |
| `implementer-tab-halved-4.png` | the take after: `cpu 15%`, where it stays |
| `implementer-tab-cadence.png` | those four in a sheet, one row per distinct reading |
| `implementer-tab-idle.png` | the load stopped: `project  ·  cpu 00% · mem 00%` — the suffix **stays**, padded, the same width |
| `implementer-idle-header.png` | the same moment in the pane's header: no chip at all — the chip's rules (quiet halves left out) are unchanged |

## What was checked, and what it proves

- **Two digits, both halves, always.** Every crop OCRs as `cpu NN% · mem NN%`: `30`/`05` under
  load, `28`, `15`, and `00`/`00` idle. No crop shows a one-digit percent, a missing half, or a
  bare number, and no crop's label region changes width — the string is the same shape
  (`…cpu NN% · mem NN%`) in every reading, which is what stops the tab bar's layout shuffling.
- **The 5 s clock.** `cadence.log`, `busy` stage: ten crops over 11.25 s at steady load, the
  label region **pixel-identical in every one** — a label repainting at the poll's 2.5 Hz with a
  wobbling number on it could not do that. `halved` stage: the reading halves mid-run and the
  label steps exactly twice in 11.25 s — `30%` (held from before) → `28%` at t+1.25 s → `15%` at
  t+5.00 s — two takes in a row without a move in between, and the two moves ≥ 5 s apart.
- **The 5 s average.** The first take after the load halved reads `28%`, not the `15%` the pane
  measures at that moment: the label shows the mean of its 5 s window (mostly 30 % samples, a
  few 15 % ones), which is the averaging the owner asked for. The next take, with the window slid
  past the 30 % samples, reads `15%`.
- **Idle keeps the suffix.** With every load dead the tab still reads `cpu 00% · mem 00%`
  (`implementer-tab-idle.png`) — the leading zeros are the point — while the pane header's chip
  is gone (`implementer-idle-header.png`): `worthShowing()`'s floors still rule the chip, and
  only the tab prints idle on purpose.
- **The rest is unit-tested.** `build/relay-paneusage-tests` (`ctest -R paneusage`) pins
  `formatPercent2` (00, 01, 05, 10, 99, 100), `tabSuffix`'s both-halves-two-digits form
  (including `Sample{}`), and `RollingMean`'s window: the mean, the drop of entries older than
  `kTabWindowMs`, invalid samples left out of the numbers, and `clear()`. All pass.

Re-run with `docs/qa_evidence/2026-09-19-tab-usage-fixed-width/drive.sh` (theme `relay-dark`
by default; `THEME=…` to change).
