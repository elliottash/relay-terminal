# Evidence — #6YS5: board console's busy line follows the surface

Implementer: pane 9c518869 (kimi/k3) · 2026-09-25 · landed `95d53c84f7cf` on `main`.

## What was verified, and how

The change was built and gated by `scripts/land.py commit --verify-tests boardturnsurface`:
the commit's compare-and-swap ran only after a verify slot built the **exact** tree that was
about to land (tip `95d53c84^` + this change's hunks) and `boardturnsurface` passed there.

## The test run (verify slot `relay-terminal-1006c7a3-0`, exact landing tree)

```text
$ ctest -R "boardturnsurface|consolemode" --output-on-failure
Test #51: boardturnsurface .........   Passed    0.66 sec
```

`tests/boardturnsurface_test.cpp` drives a no-shell console pane (`Pane` + a card
`Context`) with the worker events the window would deliver, and pins the three behaviours
the card's Done means name:

1. **A surfaced turn's busy line drops when the console moves off its surface.**
   `agent_started {surface: card:A1}` → `agentActive()`; open card B1
   (`clearTranscript("card:B1")`) → idle; a late stray `agent_finished {card:A1}` (the
   event the window actually routes away) leaves it idle.
2. **Arriving at a running card restores the line.** After switching away from a busy
   card, the host's `turnRunning()` (what BoardPane calls from the `board_card` open site
   when `m_cardTurns` says the opened card is working) → `agentActive()`; that card's own
   `agent_finished` clears it; `queue_changed` with no running item clears a restored
   line as well.
3. **A tab turn (no surface) keeps its line across switches.** `agent_started` with no
   surface → busy; `clearTranscript` to another card → still busy; `agent_finished` →
   idle.

## Related, not by this change

`consolemode` fails on **different cases per run** (kill-timing `tests/234z_cases.h:87` in
one run; transcript-text `consolemode_test.cpp:2004/2007/720/721/733` in another), in the
verify slot and identically in the shared `build/` — with and without this change, so the
variable it fails on is not this commit. Filed separately; `--234z-only` passes in
isolation in the same slot.
