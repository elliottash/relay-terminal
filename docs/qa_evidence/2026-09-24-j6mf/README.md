# #J6MF — hand-offs never run a binary that predates the change

Implementer evidence, 2026-09-25.

## What changed

- `scripts/relay-build`: `--check-only` (no lock, no configure, no build; runs the `--check`
  list against `--check-binary` or `build/relay`). A miss, from `--check` or `--check-only`, is now
  `binary predates the change: <binary> does not contain '<marker>' (build id <relay.build-id>;
  HEAD <sha date>); the binary is stale, or the marker is not a literal this change adds`, exit 5.
- `scripts/relay-qa-run`: `--check STRING` (repeatable) and `--check-binary PATH`. When the command
  is this checkout's `build/relay`, or a `--check` is given, it builds `build/relay` through
  `scripts/relay-build` (output to stderr) and then runs `relay-build --check-only`. A failed
  build exits 4, a stale binary exits 5, and in both cases the command never runs. A binary outside
  the checkout is checked but not built. A plain probe (`relay-qa-run python …`) is unchanged.
- `backend/relay_core/tryit_protocol.py`: the Try-it prompt head carries `_binary_proof_rule`:
  build `build/relay` only through `scripts/relay-build --check`, prove any binary with
  `--check-only --check "<marker>" --check-binary <binary>`, and on exit 5 stop with
  `Try it could not be staged: binary predates the change (build id …, commit …)`.
- `backend/relay_core/board_tryit_brief.md` v2: the same requirement in step 3 (also in
  `stage.sh`), and the stale binary is one of step 7's honest reasons to stop.

## Tests

```
python3 -m pytest tests/test_relay_build.py tests/test_relay_qa_run.py \
    tests/test_tryit_protocol.py tests/test_event_report.py
```

All pass (16 + 6 + 38 + 8). New cases:
`test_check_only_names_a_stale_binary_with_its_build_id_and_head` (real throwaway CMake build,
fake `relay.build-id`, git HEAD; asserts the named failure and that nothing was built),
`tests/test_relay_qa_run.py` (stale binary never run, fresh binary runs, drive script with
`--check` checks `build/relay`, `build/relay` is always built first, outside binary checked not
built, plain probe never builds), and
`test_the_prompt_requires_proving_the_binary_holds_the_change`.

## Live runs against this checkout's `build/relay`

```
$ scripts/relay-build --check-only --check 'definitely-absent-marker-j6mf'
relay-build: binary predates the change: /home/elliott/repos/relay-terminal/build/relay does not contain 'definitely-absent-marker-j6mf' (build id 2026-09-25.17H.02; HEAD 98bca3a5 2026-09-25T17:56:39-04:00); the binary is stale, or the marker is not a literal this change adds
exit 5

$ scripts/relay-qa-run --check 'definitely-absent-marker-j6mf' --check-binary "$PWD/build/relay" ./build/relay --version
relay-build: building: cmake --build …/build --parallel 4
relay-build: built in 210s; 63 artifact(s) stamped back to 17:57:23, …
relay-build: binary predates the change: …/build/relay does not contain 'definitely-absent-marker-j6mf' (build id 2026-09-25.18H.01; HEAD 98bca3a5 …)
relay-qa-run: not running ./build/relay: the binary predates the change
exit 5

$ scripts/relay-build --check-only --check 'The guest agent'
relay-build: --check: relay contains 'The guest agent'
exit 0
```

The build id moved from `17H.02` to `18H.01`: the shared binary was out of date when the run
started, and `relay-qa-run` rebuilt it before checking, which is what the card asks for.

A second `relay-qa-run` a few minutes later found the shared tree broken by another session's
half-written `tests/pane_waits.h` (`redefinition of xcxdHasButton`). It printed
`relay-qa-run: build failed; nothing was run` and exited 4 without running the command.

Incidentally, `--check 'Fold thinking'`, then the example in `relay-build`'s usage text, misses on
today's binary. That literal is no longer in the source, so the check is correct; the message
already says the marker may not be a literal this change adds. The usage text now shows a
placeholder instead.
