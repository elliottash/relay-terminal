# #WNKN — FOREIGN hunks, --take-foreign, auto-begin and the authorship journal

Implementer evidence, 2026-09-25. Both halves landed through `scripts/land.py`:

- `83a46dd9` — backend: `command_env()` passes `RELAY_SESSION_TOKEN` / `RELAY_PANE_ID` through to
  run_command children; `ToolExecutor.execute` auto-`begin`s a path in the pane's token session
  before its first write (`land.py begin <token> <path> --auto --contact "pane <id>[ #card]"`) and
  appends an authorship line to `<land root>/authors/<token>.jsonl` with both blobs under
  `<land root>/blobs/<sha256>`.
- `83218821` — land.py: `begin --auto`, `token` in meta and registry, a manual `begin` adopts the
  auto claim, FOREIGN attribution of held hunks from other panes' journals, `--take-foreign PATH:N`
  with the `Relay-take-foreign:` trailer and a note on the foreign hunk's card thread, the late-begin
  warning, journal/blob gc; `--help` and CLAUDE.md "Contested hunks" updated.

## Tests, run on a clean export of the landed tree (`git archive 6feff54f`), never in the checkout

```
PYTHONPATH=backend python3 -m pytest tests/test_land.py -q
100 passed in 19.09s        (91 pre-existing unchanged; 7 new in Foreign, 1 in Gc, helpers)

PYTHONPATH=backend python3 -m pytest tests/test_tools.py -q
48 passed in 7.62s          (12 new: LandAuthorship + command_env passthrough)
```

`python3 scripts/land.py --help` documents FOREIGN, `--take-foreign`, `--auto` and the token.

## Known limits (by design, on the card)
- Edits made by a guest's own harness tools (a Claude Code `Edit`) or from a shell leave no journal
  line; their hunks stay CONTESTED and the begin-time marker mechanism is what catches them.
- Editor-buffer writes (`_through_buffer`) are applied by the editor, not the executor's disk path,
  so they do not auto-begin or journal.
- Running workers load the new executor code at their next restart.
