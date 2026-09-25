# #9MYY — before the hot-path fixes (pre-fix binary, tests/measurement landed first)

Date: 2026-09-25. Built with `scripts/relay-build --target relay-engine-tests` at the
tests-measurement commit (no `TerminalView` behaviour changes yet).

Run:

```
RELAY_ENGINE_TEST=ViewTest \
RELAY_VIEW_EVIDENCE=$PWD/docs/qa_evidence/2026-09-25-9myy-view-hot-paths/before \
RELAY_PAINT_GOLDEN=$PWD/docs/qa_evidence/2026-09-25-9myy-view-hot-paths/before \
RELAY_VIEW_BENCH=1 \
build/engine/relay-engine-tests hoverSweepProbesOncePerFrame paintGrabGolden \
    benchHoverSweep benchPaintHighlights
```

Full output: `bench-run.txt` (same directory).

## Hover sweep (`hoverSweepProbesOncePerFrame`, counting `setLinkProbe`)

One wrapped logical line, 6 path candidates, 44 cells swept on each of its 2 screen
rows (88 mouse moves):

- probe calls for the sweep: **1232** (14.0 per cell) — `hover-sweep-libvterm.txt`
- probe calls for the same sweep after new output: **1232** (re-scans per frame today;
  after the fix this must stay ≤ candidates per frame and rise again on new output)

## Benches (`RELAY_VIEW_BENCH`)

- `benchHoverSweep` (44 moves along the candidate line): **1.6 msecs per iteration**
- `benchPaintHighlights` (full-widget grab of a 12-row screen, 3 matches per row):
  **0.11 msecs per iteration**

## Paint golden (`paintGrabGolden`)

- `paint-libvterm.png`: an idle screen with a find (4 matches + current), a selection,
  reverse video, a wide character, rest-coloured links, a banded role row and a fold
  chevron.
- Byte-stability pre-check: a second run into a scratch directory produced a
  byte-identical PNG (`cmp` clean), so the after-run `cmp` is a real gate.
