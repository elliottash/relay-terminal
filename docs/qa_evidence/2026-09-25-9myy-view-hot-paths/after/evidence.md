# #9MYY — view hot paths: after

The same probe, grab and benchmark runs as `../before/evidence.md`, on the tree
with the hover `linkAt` cache, `paintRow` colours-once and the accessibility
text cache landed (card #9MYY steps 3–5). One core runs here (`availableVtCores()`
answers libvterm only); the before run is the same single core.

Commands, matching the before run:

```sh
RELAY_ENGINE_TEST=ViewTest \
  RELAY_VIEW_EVIDENCE=$PWD/docs/qa_evidence/2026-09-25-9myy-view-hot-paths/after \
  RELAY_PAINT_GOLDEN=$PWD/docs/qa_evidence/2026-09-25-9myy-view-hot-paths/after \
  RELAY_VIEW_BENCH=1 QT_QPA_PLATFORM=offscreen \
  build/engine/relay-engine-tests \
  hoverSweepProbesOncePerFrame paintGrabGolden benchHoverSweep benchPaintHighlights \
  > docs/qa_evidence/2026-09-25-9myy-view-hot-paths/after/bench-run.txt 2>&1

cmp ../before/paint-libvterm.png paint-libvterm.png
```

## Probe counts

- `hover-sweep-libvterm.txt`: **14 probe calls** per 88-cell sweep (before:
  1232), and 14 again after the frame version moves — one scan per frame, the
  ceiling the test asserts (≥ the 6 candidates, ≤ `6 * 6 + 12`).

## Benchmarks (msecs per iteration)

- `benchHoverSweep`: **0.014** (before 1.6) — the logical row is built and
  scanned once per frame version, not per hovered cell.
- `benchPaintHighlights`: **0.093** (before 0.11) — one `colorsFor` per cell per
  row paint instead of two, highlight ranges painted onto a column array once.
  A first combined run printed 0.19 for this bench (warm-up after the golden
  grab); the recorded run is steady-state, and three standalone runs agree on
  0.093.

## Golden grab

- `paint-libvterm.png`: **byte-identical** with
  `../before/paint-libvterm.png` (`cmp` clean) — reverse video, wide cells,
  find matches, a selection, rest links, a banded role row and a fold chevron
  all paint exactly as before.
