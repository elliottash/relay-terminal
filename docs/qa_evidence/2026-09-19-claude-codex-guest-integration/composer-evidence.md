# Composer translator evidence (GT7X)
Date: 2026-09-19

## Registry and routing coverage
- `PYTHONPATH=$PWD/backend python3 -m unittest tests.test_guest_slash tests.test_guest tests.test_guest_hook tests.test_program_input tests.test_router -v`
  - passed: 122 tests.
- `tests/test_guest_slash.py` creates both user and project Claude skill directories, plus nested legacy `.claude/commands/*.md`, and verifies they join the Claude built-ins. It verifies the static Codex catalog and that publishing uses `guest-event.py slash <guest>` with the documented `{commands: [...]}` payload.
- The pane implementation consumes the atomic `slash` channel event, adds non-colliding rows after Relay entries with a `Claude Code · guest` / `Codex · guest` badge, and sends a selected guest command with bracketed paste. It clears the guest input line, dismisses its slash menu with Escape, then presses Enter; this covers the slash-menu-eats-Enter sequence.
- Direct composer submissions use the same delivery queue. A busy guest holds the entry until its `state {busy:false}` event calls `pumpQueue()`. `@file` no longer opens a Relay preview while a guest is active, and `!` remains an explicit terminal submission.

## Native build and GUI smoke
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON`
- `cmake --build build -j"$(nproc)"`
- `ctest --test-dir build --output-on-failure`
  - passed: 51 tests.
- Relay was launched under `xvfb-run` with a fresh, temporary `XDG_CONFIG_HOME` and `--clean-shell`; the app started and the composer slash popup rendered. The guest interaction itself is covered by the deterministic channel/catalog tests above, rather than relying on the user account state of an installed guest CLI.
