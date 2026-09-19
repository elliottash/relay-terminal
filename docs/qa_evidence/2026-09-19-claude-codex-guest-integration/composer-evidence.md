# Composer translator evidence (GT7X)
Date: 2026-09-19

## Registry and routing coverage
- `PYTHONPATH=$PWD/backend python3 -m unittest tests.test_guest_slash tests.test_guest tests.test_guest_hook tests.test_program_input tests.test_router -v`
  - passed: 122 tests.
- `tests/test_guest_slash.py` creates both user and project Claude skill directories, plus nested legacy `.claude/commands/*.md`, and verifies they join the Claude built-ins. It verifies the static Codex catalog and that publishing uses `guest-event.py slash <guest>` with the documented `{commands: [...]}` payload.
- The pane implementation consumes the atomic `slash` channel event, adds non-colliding rows (declared after Relay entries, so Relay wins a collision) with a `Claude Code · guest` / `Codex · guest` badge, and sends a selected guest command with bracketed paste. It clears the guest input line, dismisses its slash menu with Escape, then presses Enter; this covers the slash-menu-eats-Enter sequence. When the box holds only `/`, the guest's rows open the list — the composer is that guest's input line, and Relay's 42 built-ins would otherwise fill all nine visible rows (fixed in the polish phase: the badged rows sat below the fold and QA's "the popup lists the guest's commands badged" could not pass). Typing a query restores the usual order, Relay's names first on a tie.
- Direct composer submissions use the same delivery queue. A busy guest holds the entry until its `state {busy:false}` event calls `pumpQueue()`. `@file` no longer opens a Relay preview while a guest is active, and `!` remains an explicit terminal submission.

## Native build and GUI smoke
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON`
- `cmake --build build -j"$(nproc)"`
- `ctest --test-dir build --output-on-failure`
  - passed: 51 tests.
- Relay was launched under `xvfb-run` with a fresh, temporary `XDG_CONFIG_HOME` and `--clean-shell`; the app started and the composer slash popup rendered. The guest interaction itself is covered by the deterministic channel/catalog tests above, rather than relying on the user account state of an installed guest CLI.

## Live popup with a guest classified (polish phase, `composer-drive.py`)
Same harness shape as the hooks track: Xvfb, isolated HOME/XDG/TMPDIR, and a stand-in named
`claude` (`!bash -c 'exec -a claude sleep 900'`) so the pane's own classifier, scan and channel
drive everything — no manual emit. Every shot is OCR-asserted by the script (`composer-run.log`):
- `implementer-composer-01-slash-popup-badges.png` — `/` with the guest active: the popup opens
  with the guest's commands, each badged `Claude Code · guest`.
- `implementer-composer-02-guest-command-filtered.png` — filtering to `doctor` offers the badged
  `/doctor` row; the guest chip is live in the status strip.
- `implementer-composer-03-guest-command-typed.png` — accepting the row types `/doctor` into the
  guest (bracketed paste + Enter; the canonical-mode pty echoes it).

Harness lessons encoded in the script: Relay binds Esc on an empty box, while a program runs, to
interrupting that program — an Escape pressed to "reset the box" killed the first stand-in
(`^C` on screen, prompt back, guest state cleared). And popup text is not cleared by closing the
popup, so the script selects all and deletes instead of retyping `/`.
