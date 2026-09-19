# Reset to defaults, per Options page — implementer evidence, 2026-09-18

Owner: "also add a reset to defaults button on options pages". Every page of the Options pane that
holds values ends with a **Reset to defaults** row; the button asks first, names the page, and puts
only that page's options back to what Relay ships with. A page whose rows stand for things rather
than values — Local models, which lists the servers you saved — has no button.

`drive.sh [build-dir]` reproduces everything below under Xvfb with an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`. It needs Xvfb, xdotool and ImageMagick; it needs
no provider account, because the Options pane asks nothing of a model. The profile it starts from
has eight values away from the defaults, spread over four pages.

There is no window manager under Xvfb, so nothing hands the keyboard back when a modal closes: the
script clicks the search box once after the confirmation before using the arrow keys again. On a
desktop the focus comes back by itself.

| Shot | What it shows |
| --- | --- |
| `implementer-a-general-top.png` | General, with the reset row last on the page: "Puts the 8 options on this page back to what Relay ships with, at once. No other page changes, and your API keys, saved servers and custom shortcuts are left alone". |
| `implementer-c-confirmation.png` | ↑ from the search box lands on the row, Enter opens the confirmation: "Reset the General options to what Relay ships with?" · "Only this page changes. It cannot be undone." Cancel is the default button. |
| `implementer-e-after-reset-root.png` | After pressing Reset General: every General row is back (thinking on, tool output off, notifications and hints on, reopen windows on, Log detail at Normal), and the toast reads "General: 8 options are back to Relay's defaults." |
| `implementer-g-terminal.png` | Terminal, untouched by the General reset — "Copy on select" is still on — with its own button over its 7 options. |
| `implementer-h-keyboard.png` | Keyboard: 2 options. "Edit keyboard shortcuts" and the mouse paragraph are not values and are not counted. |
| `implementer-i-local-models.png` | Local models: no button at all, because nothing on it is a value with a default. |

The rest are the steps in between: `-b-` the row highlighted before Enter, `-d-` the page after
Cancel (unchanged), `-c2-` the confirmation opened a second time from the button itself, `-e-` the
same moment as `-e-...-root` cropped to the window, and `-f-` Appearance with a button of its own.
`relay-stderr.log` is Relay’s output for the run: empty, as it should be.

`relay.conf.before`, `relay.conf.after-cancel` and `relay.conf.after-reset` are the settings file at
each step. Cancel changes nothing at all. The reset removes `agent/show_thinking`,
`agent/show_tool_output` and `windows/restore` (a fresh install has no such key) and writes
`hints/enabled=true`, `notifications/desktop=true` and `logging/level=info`, whose writers apply
them on the spot. `agent/max_steps=17`, `terminal/copy_on_select=true` and `theme/name=relay-light`
— the Agent, Terminal and Appearance pages — are still there afterwards.

Headless coverage is in `tests/settingspane_test.cpp` (`ctest -R settings`): which pages get a row,
that a reset restores a toggle, a number, a choice and a text row and touches no other page, that
cancelling does nothing and announces nothing, that the row is the last on its page, is reachable
with ↑ and Enter and is found by searching "reset defaults", and that every row helper in
`RelayWindow::settingsSections()` still declares a default.
