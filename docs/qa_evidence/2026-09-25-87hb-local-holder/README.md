# #87HB — the local half of the tmux gap: a shell that survives Relay quitting

Commit: `0fbf2cac` (main), all 14 paths, `Implemented-By: glm/glm-5.3`.

## What the machine ran (2026-09-25, this checkout)

| Run | Result |
| --- | --- |
| `scripts/relay-build` (whole `relay` target, after every source change) | green |
| `scripts/land.py commit …` verify-slot build of the exact landed tree | green (commit landed) |
| `ctest --test-dir build -R sshconfig` — includes new `readsTheLocalHolder` cases for the local argv shapes and `localHolderName` | 1/1 passed |
| `TMPDIR=/tmp python3 -m unittest discover -s tests -p test_ssh_shell.py` — 45 tests, including the four new local-holder ones | 45/45 OK |
| New `HolderTests.test_local_holder_runs_the_session_command` | OK |
| New `HolderTests.test_local_holder_fallback_without_tmux` | OK |
| New `HolderTests.test_local_holder_marks_reach_the_pane` (holder=1 marks arrive; holder=0 raw marks die in tmux) | OK |
| New `HolderTests.test_local_holder_restore_line_reattaches` (client dies, session lives, the saved line re-attaches and redraws) | OK |

Notes for whoever re-runs: `WrapperTests` (15 tests) fail if `TMPDIR` is Relay's scratch
directory — they build ssh ControlPath strings under `$TMPDIR` and the expected argv embeds
it; that is environmental and predates this card. Run the suite with `TMPDIR=/tmp`.

## What only a person can check (from the card's Verify)

- `top` in a local pane → quit Relay → relaunch → the pane re-attaches at its first prompt
  with `top` alive and its screen back; pre-restart scrollback replays first.
- Close a pane normally → `tmux -L relay ls` still lists its session; "Close and end this
  pane's session" leaves it empty; the sessions list (⌘K "Persistent sessions…") lists and
  kills leftover local sessions.
- Options › Terminal › "Persistent local panes" off → new panes behave exactly as today.
- Marks, prompt and cwd tracking in a wrapped pane (cyan bands, prompt detection); with
  "Shell integration (OSC 7/133)" on, cwd tracking survives the wrap too.
