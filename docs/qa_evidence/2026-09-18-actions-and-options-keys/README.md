# Evidence: Actions, Options and Resume each get one Ctrl+Shift key

Implementer run, 2026-09-18, `./build/relay --clean-shell --fresh` under Xvfb with
`XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME XDG_CACHE_HOME TMPDIR` pointed at
fresh directories and `RELAY_KEYRING=off`; keys sent with `xdotool key`.

| File | Key pressed | What it shows |
|---|---|---|
| implementer-01-ctrl-shift-o-options.png | Ctrl+Shift+O | The Settings pane opens beside the terminal on the General tab |
| implementer-02-ctrl-shift-a-moves-to-actions.png | Ctrl+Shift+A, pane open on General | The same pane moves to the Actions tab; it does not close |
| implementer-03-ctrl-shift-a-again-closes.png | Ctrl+Shift+A again | The pane closes and the terminal pane has the window |
| implementer-04-ctrl-shift-y-reaches-resume.png | Ctrl+Shift+Y | `/resume` runs: with no provider in the isolated profile it answers "No agent provider is configured." |

Tests: `python3 -m unittest tests.test_keybindings` — 18 pass. `ctest --test-dir build`: 28 of 29
pass; `backend-and-bash` fails only in `tests/test_remote_host.py` (`SecretInputTests`,
`TransportSwitchTests`: "cannot unpack non-iterable coroutine object"), a file another session had
uncommitted changes in at the time, together with `remote/host.py`. Nothing in this change touches
either.
