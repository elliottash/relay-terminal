# GhosttyCore parses the row-role OSC (#J5DN)

Built and verified on `sphinxpad` (the machine with `RELAY_ENGINE_WITH_GHOSTTY=ON`),
2026-09-19. `libghostty-vt` f9a3f24 built with Zig 0.16.0 into `~/vt-prefix` via
`engine/scripts/build-libghostty-vt.sh`; Relay configured in `~/relay-terminal/build-ghostty`.

## What the pictures show

`rowrole-evidence.cpp` (built ad hoc against that build; not in the CMake tree) writes the
rows `Pane::printInline` would — `OSC 7772;agent` + bold text, a plain reply,
`OSC 7772;shell` + bold text, plain rows around both — paints the pane, then switches the
scheme the way a theme switch does and paints again. Band/ink values are the ones
`EngineBackend::applyThemeColors` sets for "channel": the theme's `shell`/`agent`
destination colour with a chip ink (Relay Dark, then Relay Light).

| File | Core | Scheme | Measured (row-dominant colours, PIL) |
|---|---|---|---|
| `ghostty-1-relay-dark.png` | ghostty | Relay Dark | ground `#0f1115`; agent band `#b48ef7`, shell band `#3ec5f0` — one band row each, across the grid |
| `libvterm-1-relay-dark.png` | libvterm | Relay Dark | identical |
| `ghostty-2-relay-light.png` | ghostty | Relay Light | ground `#fbfbfd`; agent `#7c3aed`, shell `#006ab1` — the *same* rows, recoloured, nothing rewritten |
| `libvterm-2-relay-light.png` | libvterm | Relay Light | identical |

The two cores' PNGs are byte-identical: a Ghostty pane shows the same band a libvterm pane
does (the card's acceptance), and the band follows a scheme switch in scrollback.

## Tests

`tests-ghostty.txt` — `ctest --test-dir build-ghostty -R '^relay-engine-tests$'` passed
(20.7 s), and `relay-engine-tests theRowRoleOscMarksItsLine osc133PromptMarks` shows
`PASS … (ghostty)` next to `PASS … (libvterm)`, including the history round-trip and the
scrolled-viewport assertions added to `theRowRoleOscMarksItsLine`. The pixel-level
`ViewTest::aUserRowWearsItsRoleFromTheSchemeInForce` also ran green on the ghostty core as
part of the same binary.

Reproduce:

    ssh sphinxpad
    cd ~/relay-terminal
    ctest --test-dir build-ghostty -R '^relay-engine-tests$' --output-on-failure
    QT_QPA_PLATFORM=offscreen ~/vt-prefix/ev/rowrole-evidence ghostty  <outdir>
    QT_QPA_PLATFORM=offscreen ~/vt-prefix/ev/rowrole-evidence libvterm <outdir>
