# Usage chart behind the Models pane's usage… button (#62TG)

Run: the `relay` built from the `land.py` verify tree for this change, under Xvfb with an isolated
home. The sandbox holds the **real** account registry (read-only) and a copy of the real usage log,
so the figures the chart prints are the real ones. The Claude Code and Codex binaries on `PATH` are
stubs answering `--version`; no turn was run against any real login.

- `00-models-pane.png` — the Models pane opened, its first tab (labelled `Sources`, the providers
  page) not yet selected.
- `01-usage-row.png` — the providers page: the `usage` row now carries two buttons, `refresh` and
  `usage…` (OCR: `refresh` at x=2185, `usage...` at x=2270).
- `02-usage-chart.png` — the whole screen after pressing `usage…`.
- `03-chart-only.png` — the dialog on its own, `Subscription usage and draw odds`, 960×536.

The dialog reads back (tesseract, from `03-chart-only.png`):

```
Every account the main list draws on, with each window it reports — 5h and weekly — the share of
that window still unspent, and the time until it resets. The weight is the tightest window's share
left over the hours until it resets; the draw column is that weight as a share of every account at
rank 1. Red rows are exhausted and get nothing until they reset.

elliott.ash@gess.ethz.ch  opus
  5h     75% left · resets in 4.0 h · Sat 03:50
  weekly 54% left · resets in 66 h · Mon 18:00
  weight 0.82 · draw 47.4%

ashe@ethz.ch  opus
  5h     94% left · resets in 3.3 h · Sat 03:10
  weekly 99% left · resets in 109 h · Wed 13:00
  weight 0.91 · draw 52.6%

elliott.t.ash@gmail.com  opus
  weekly 0% left · resets in 12 h · Sat 11:59
  exhausted · out of the draw

e@elliottash.com  opus
  weekly 0% left · resets in 29 h · Sun 05:00
  exhausted · out of the draw
```

Both windows appear per account (5h and weekly), the two live accounts' draw shares sum to 100%,
and the two accounts with a spent weekly window stay in the picture as red zero rows.

Note on the sandbox: its fresh configuration has no saved list of its own, so the chart shows the
main list's Claude Code candidates and no Codex rows — a stub Codex CLI reports no model list for
the tier's `gpt-6-sol` tag to match. In the real app the same draw includes the three Codex accounts
(`routing-draws.jsonl`, 22:52: `guest:codex|gpt-6-sol` at 9.8%, `guest:codex:ashe-ethz-ch` 15.3%,
`guest:codex:elliott-t-ash-gmail-com` 15.8%).

Targeted tests: `ModelCatalogTests::usageChartRowsCarryBothWindowsAndKeepTheZeroRow` passes; the
`modelcatalog` ctest case passes in the verify slot.