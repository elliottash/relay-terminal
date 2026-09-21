# #Z00M Appearance font size

`scripts/relay-build --target relay`: PASS, build 2026-09-21.08H.08.

Live GUI under Xvfb (1280×900), with isolated XDG_CONFIG_HOME, XDG_DATA_HOME and XDG_RUNTIME_DIR under /tmp/relay-font-live2. Launched `build/relay --clean-shell --workspace /tmp/relay-font-live2`, resized to 1100×800.

1. Opened Options with Ctrl+comma; searched "font size". The Appearance row started at 11 pt.
2. Clicked the numeric control, selected its contents, typed 18 and pressed Tab. The open terminal immediately grew to 18 pt (appearance.png).
3. Closed Options (terminal.png). Ctrl+= twice then Ctrl+0 returned to the selected 18 pt size; observed the shortcut hint.
4. Quit and restarted with the same isolated settings. The terminal remained 18 pt (restarted.png). The saved config contains appearance/font_size=18.

Font range is 6–48 pt; the standard row and section reset callbacks remove the override and reapply 11 pt. Changing another terminal setting leaves per-pane zoom alone when the base font size is unchanged. All open Pane widgets, including consoles and panes in other windows, receive the update.
