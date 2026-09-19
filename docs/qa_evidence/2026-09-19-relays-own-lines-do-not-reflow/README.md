# Relay's own lines re-wrap on resize — implementer evidence (card #R2WQ, 2026-09-19)

Commit `121fd9a1` on `main` (the change had also been swept onto main in-flight by
`8147cc55`; the landed commit carries the extraction of the collector into
`engine/view/ProseSpans.h`, the held-word-before-OSC-8-close ordering fix, the tests,
the CMake wiring and the docs).

`report-wide.png` / `report-narrow.png` (already here) are the owner's report of the bug.

## What the change is

The printed bytes are unchanged. Every inline block the pane prints is additionally
wrapped in an OSC 8 run `relay://prose/<pane>/<block>` covering exactly its rows, and the
block's logical lines — the rendered text *before* `relay::WordWrap`, styles kept as SGR
spans (`engine/view/ProseSpans.h`) — are handed to the engine with the width they were
printed at (`TerminalBackend::setProseBlock`). The fold layer treats it as a
**replacement fold**: away from the print width it hides the run's real rows and paints its
own wrap; at the print width it stands aside, byte-identical to before. The break rule is
one thing now (`relay::wrap` in `src/WordWrap.h`), shared by the streaming wrapper,
`FoldLayer::layout()` and `wrapFoldLines()` — which also stops expanded tool-call detail
breaking mid-word. A `relay://prose/` URI is never an interactive link.

## Automated evidence (this machine, libvterm core)

```
$ scripts/relay-build --target relay-engine-tests && \
  RELAY_ENGINE_TEST=FoldLayerTest QT_QPA_PLATFORM=offscreen ./build/engine/relay-engine-tests
Totals: 29 passed, 0 failed        (FoldLayerTest: wraps, replacement maths, prose spans)

$ QT_QPA_PLATFORM=offscreen ./build/engine/relay-engine-tests
Totals: 29+17+29+14+10+9+40 passed, 0 failed   (Core, FaintInk, FoldLayer, FoldSearch,
                                                Pty, Session, View — whole engine suite)

$ ./build/relay-wordwrap-tests
Totals: 14 passed, 0 failed        (shared rule vs the streamed wrapper, unchanged
                                     behaviour at the print width)

$ ./build/relay-markdown-tests
Totals: 15 passed, 0 failed
```

The regression this card exists for is
`ViewTest::proseReflowsOnResize` (`engine/tests/ViewTest.cpp`): it prints a paragraph
with inline code, a long word (`RELAY_ENGINE_WITH_GHOSTTY`) and a bulleted list at 80
columns — exactly as the pane does, pre-wrapped by `WordWrap` inside the OSC 8 run —
hands the block over with `setProseBlock`, then resizes to 48, to 96 and back to 80, and
asserts the visible rows are **the rows the wrapper would have printed at each width**,
with bullets hanging and no row ending mid-word:

```
$ RELAY_ENGINE_TEST=ViewTest QT_QPA_PLATFORM=offscreen \
  ./build/engine/relay-engine-tests proseReflowsOnResize
PASS   : ViewTest::proseReflowsOnResize(libvterm)
```

This machine has no libghostty-vt prefix (`RELAY_ENGINE_WITH_GHOSTTY=OFF` in every build
dir), so the ghostty row of that test has not run here; the test is written against
`availableVtCores()` and runs both when built with the ghostty core — part of the QA
checklist below.

## Boundary, as built

- Program output keeps the terminal's own character reflow untouched.
- A block whose anchor was trimmed out of the scrollback, or restored from the text-only
  restart snapshot, has no run: it stays frozen at the width it was printed, as before.
- At the print width the layer takes no rows and hides nothing (`FoldLayerTest::
  proseStandsAsideAtThePrintWidth`), so a pane that is never resized is unchanged.
- Search over a taken-over block: the core's matches on hidden rows are stepped past
  (their text is the fold's); the reported count still includes them — a known wart, the
  step lands on the visible match.

## QA checklist (live, per the card)

1. Ask for a long paragraph, a bulleted list and a line with inline code and a link.
   Shrink the pane by half: no row ends mid-word, no row is stranded short, bullets hang.
2. Widen past the original width: the text re-flows to the wider measure.
3. Drag the splitter continuously: the text keeps up, nothing is lost or duplicated.
4. Select and copy across a re-wrapped reply; search for a word a re-wrap moved.
5. A user-typed `✦` line keeps its role band across the resize; the theme recolours it.
6. Expanded tool-call detail no longer breaks mid-word.
7. Shell output above the reply (`ls`, `git log`) reflows the way it always did.
8. Both cores: libvterm and `RELAY_ENGINE_WITH_GHOSTTY=ON` (the ViewTest above runs both
   in such a build; the live checks should too).
