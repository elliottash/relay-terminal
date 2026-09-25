# #WZ3K — scratch sweep skips the guest harness's own home; `relay-scratch own`

Run 2026-09-25 on relay-terminal, from `PYTHONPATH=backend RELAY_KEYRING=off`.

## Targeted tests

- `python3 -m unittest discover -s tests -p test_guest_accounts.py` — 25 tests, OK. New:
  `OwnHomePathsTests` (default `~/.claude` + `~/.claude.json`; `CLAUDE_CONFIG_DIR` override;
  a registered account's dir; `~/.codex` / `CODEX_HOME`; a non-guest config owns nothing).
- `python3 -m unittest discover -s tests -p test_scratch_ledger.py` — 32 tests; the new ones
  (`test_unledgered_created_since_honours_skip`, `OwnPathTests.*`, the `relay-scratch own`
  assertion in `test_note_lists_entries_and_asks_only`) pass. One failure,
  `WriteGuardTests.test_deliverable_outside_workspace_refused`, fails the same way on a clean
  `git archive HEAD` export from before this change: filed as #B53G.
  `TmpdirHookTests.test_session_tmpdir_points_inside_session_root_and_is_ledgered` fails only
  when the shell exports `RELAY_SESSION_TOKEN` (any Relay pane); it passes with that unset.
- `scripts/relay-build` — built in 63s.

## CLI smoke (throwaway `RELAY_LEDGER`)

```
$ relay-scratch own $L/tool-home --purpose smoke   -> "scb2c12 live $L/tool-home", exit 0
$ relay-scratch own $L/tool-home                   -> same row id, exit 0; ledger still 1 line
$ relay-scratch own $L/missing                     -> "... does not exist; `own` records what
                                                       is already there", exit 2
unledgered_created_since(home=$L, tmp=$L)          -> $L/tool-home no longer listed
```

## Not done here

The live check, ending a turn in a Claude Code guest pane and seeing no sweep note that names
`~/.claude` or `~/.claude.json`, needs a restarted Relay backend running this commit. That is
the verifier's step.
