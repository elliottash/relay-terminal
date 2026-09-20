# #VJDD — restored pane scrollback keeps its formatting and colours

Owner report (2026-09-20): "bug: when panes are re-opened, all the formatting and colors are lost
in the text."

## What changed

- `engine/core/AnsiSerializer.{h,cpp}` — turns `relay::Line` cells into ANSI SGR: bold, faint,
  italic, underline (single/double/curly), blink, reverse, conceal, strike, and default / indexed
  (0-255) / RGB foreground and background. Wide tails are skipped, blanks become spaces, trailing
  blanks are trimmed like `Line::text()`.
- `engine/TerminalBackend.h` — new `FormattedText` capability and virtual `formattedScreenText()` /
  `formattedScrollbackText(int)`; the defaults return the plain text so an engine that cannot
  reconstruct escapes is unchanged.
- `engine/backend/VTermBackend.{h,cpp}` — implements both from `VtCore::historyLines()` and
  `updateFrame()`, then `linesToAnsi()`.
- `src/Pane.h` — `paneFormattedHistoryLines()` / `paneFormattedScreenLines()` /
  `paneFormattedTextLines()`, used by `saveScrollback()` and `sessionTextLines()` with a plain-text
  fallback; `replayRestoredScrollback()` keeps CSI SGR through `sanitizeSgrOnly()` and strips every
  other control sequence; `dropRestoreMarks()` strips SGR before comparing.
- `src/WindowState.{h,cpp}` — `sessiontext::turnStart()` strips SGR before matching a turn marker;
  the header documents the new behaviour and the RGB-vs-theme tradeoff.
- `docs/ARCHITECTURE.md` — the scrollback section now describes SGR serialization and the replay
  filter.

Landed in `5e8bc8444e92fc5bca41682e935df157debffdfd` on `main`.

## Verification run

Build:

```
scripts/relay-build
...
[100%] Built target relay
relay-build: built in 5s; 20 artifact(s) stamped back to 19:27:55, the build's start, ...
```

Tests:

```
$ ctest --test-dir build -R '^(windowstate|relay-engine-tests)$' --output-on-failure
    Start 23: windowstate
1/2 Test #23: windowstate ......................   Passed    0.10 sec
    Start 81: relay-engine-tests
2/2 Test #81: relay-engine-tests ...............   Passed   15.05 sec

100% tests passed, 0 tests failed out of 2
```

`tests_check` on #VJDD: "every test its `## Tests` section names is collected, has run, and is
neither flaky nor slow. Nothing to fix."

## What the verifier should look at by hand

The QA checklist on #VJDD lists the manual checks: a coloured `ls`/`git diff` surviving a restart,
truecolour and 256-colour, the replay filter refusing an edited-in `ESC ]0;…` or `ESC[2J`, the
restore rules not stacking over two restarts, an old plain file still replaying, and rewind still
finding a turn whose marker was saved with colour.
