# F11 fullscreen implementation evidence

- `scripts/relay-build --target relay`: passed, build 2026-09-22.20H.06.
- Live GUI: isolated Xvfb display :193, 1600x1000, KWin, fresh XDG_CONFIG_HOME and XDG_DATA_HOME; launched `build/relay --fresh --workspace <temporary directory>`.
- Sent F11 with xdotool to the focused Relay window. `_NET_WM_STATE_FULLSCREEN` appeared; geometry changed from (140,70), 1320x860 to (0,0), 1600x1000.
- Sent F11 again: fullscreen state cleared and the original geometry was restored exactly.
- Maximized through an EWMH client message, then sent F11 twice: fullscreen entered and exited, preserving both maximized state atoms.
- Source review: Actions entry uses `actionItem`, whose live Keymap shortcut is taught by the existing `palette.<action>` hint path, including its normal cooldown and show limits.
- `git diff --check`: passed.
- `python3 scripts/relay-board.py check`: no findings for F11S; existing unrelated board format errors remain.

The initial test's attempts to maximize by a guessed title-strip coordinate and desktop shortcut did not maximize the window. Replaced that test setup with an explicit EWMH maximize request; both fullscreen round trips then passed. No implementation change was needed.

Independent visual verification remains the board verifier's stage.
