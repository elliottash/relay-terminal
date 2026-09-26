# Randomize the theme — implementer evidence (card #R4ND, 2026-09-20)

Owner's request: *under apperance / theme, add a "randomize" option.*

**Randomize** is a button under Options › Appearance › Theme. It takes a theme at random that is
*not* the one you are on and applies it exactly as choosing from the list above does — this tab
now, and the default for what Relay opens on and what a new tab starts with — then names it in a
notice, because a theme you cannot name is one you cannot ask for again. It is a button and not a
stored mode, so there is nothing on the page to reset and the "Reset to defaults" count is
unchanged. The same thing is on the fast path: `/theme random` (also `randomize`, `shuffle`), and
**Random** as the last row of the bare `/theme` picker. Pressing the button teaches the command
once (`theme.random` hint), as RELAY.md's standing rule asks.

| where | what |
|---|---|
| `src/Theme.{h,cpp}` | `relay::theme::randomThemeId(avoid)` — the draw, excluding the theme in use |
| `src/RelayWindow.h` | the Randomize row in the Appearance section; `RelayWindow::randomizeTheme()` |
| `src/Pane.h` | `/theme random`, the picker's Random row, `Pane::randomTheme()` |
| `tests/themeswitch_test.cpp` | `randomizeNeverGivesYouTheThemeYouAreOn` |
| `docs/ARCHITECTURE.md` | the per-tab themes section says what Randomize does |

## What was run

```
scripts/relay-build --target relay-themeswitch-tests
ctest --test-dir build -R themeswitch --output-on-failure      # 1/1 passed
./build/relay-themeswitch-tests                                # PASS randomizeNeverGivesYouTheThemeYouAreOn
scripts/relay-build                                            # the relay target, clean
docs/qa_evidence/2026-09-20-randomize-theme/drive.sh           # the screenshots below
```

The unit test draws 300 times against the shipped themes: every draw is a theme that exists, never
the one passed as `avoid`, and every other theme comes up at least once (a picker stuck on one file
fails it). It also switches to each drawn id, so the draw is checked to return something loadable.

## Screenshots (Xvfb 1400×880, isolated `HOME`/`XDG_*`, `RELAY_KEYRING=off`)

| shot | what it shows |
|---|---|
| `implementer-a-start.png` | the window as it opens, on the default theme (Dark Copper) |
| `implementer-b-appearance.png` | Options › Appearance: **Randomize** directly under **Theme** |
| `implementer-c-search.png` | the search box: typing "randomize" finds the row by label and aliases |
| `implementer-d-press-1.png` | one press — Relay Dark, and the notice "Theme: Relay Dark." |
| `implementer-e-press-2.png` | a second press — IBM Beige: the whole window, chrome and terminal |
| `implementer-f-picker.png` | `/theme`: the picker with **Random** as its last row |
| `implementer-g-command.png` | `/theme random` from the prompt box — Relay Dark, same notice |

`themes.txt` is `theme/name` read out of the sandbox profile after each of the three switches
(relay-dark, ibm-beige, relay-dark): the default follows the press, which is what makes it the
same act as picking from the list.
