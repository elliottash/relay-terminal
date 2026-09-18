# Evidence: the Actions pane and the Options pane

Implementer run, 2026-09-18: `./build/relay --clean-shell --fresh` under Xvfb on a display of its
own, with `XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME XDG_CACHE_HOME TMPDIR` on
fresh directories and `RELAY_KEYRING=off`; keys sent with `xdotool`. The header band ("⚡ ACTIONS",
"OPTIONS") in the pictures is the pane chrome of cards #SPBN/#XM0T, which was uncommitted in the
working tree at the time; this change only sets the `paneType` it reads.

| File | Keys | What it shows |
|---|---|---|
| implementer-01-actions.png | Ctrl+Shift+A | The Actions pane: a search box and one list, no tabs |
| implementer-02-actions-finds-an-option.png | type `appearance theme` | An option found from Actions is an "Options › Appearance › Theme" row, not a control |
| implementer-03-enter-opens-options-on-that-row.png | Enter | The pane swaps to Options, Appearance tab, Theme highlighted; the tab title follows |
| implementer-04-ctrl-shift-a-swaps-back.png | Ctrl+Shift+A | Options swaps back to Actions in place |
| implementer-05-options-is-an-action.png | type `options` | "Options…" is an action, with its key |
| implementer-06-options-via-the-action.png | Enter | Options, General tab; the hint teaches Ctrl+Shift+O. No verb rows under General |
| implementer-07-keyboard-tab.png | ← | The last tab is Keyboard: preset, keys inside programs, Edit…, the mouse note |
| implementer-08-ctrl-shift-o-closes.png | Ctrl+Shift+O | Its own key closes the pane; focus is back in the prompt box |
| implementer-09-ctrl-question-is-actions.png | Ctrl+? | Every action and its keys is the Actions pane; Recent now lists Options… |
| implementer-10-moved-verb-found.png | type `reset hints` | "Reset shortcut hints", moved out of Options › General, is under Relay; the option it relates to is offered below it as a jump row |
| implementer-11-esc-esc-closes.png | Esc Esc | First Esc clears the search, the second closes |

Tests: `relay-settings-tests` 15 pass (modes, swap in place, jump rows, ranking, several-word
search, scattered letters rejected). `ctest --test-dir build`: every C++ suite passes;
`backend-and-bash` hit the 5-minute ctest timeout with several other sessions building on the
machine, so the backend suites that touch the three Python files this change rewords were run on
their own: `python3 -m unittest tests.test_keybindings tests.test_provider tests.test_images
tests.test_configure_provider` — 79 pass.
