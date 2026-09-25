# #FYEY — land.py track B: uncommitted work is watched, not lost

Commit `88d7382336bc` (main) added `status`, `board-sync`, `reap`, `orphans`,
`begin --owner/--card`, the cancelled-marker refusal in `commit`/`try`, and
`--only-hunk`/`--exclude-hunk --by <pane id | #card>`. Tests:
`python3 -m pytest tests/test_land.py -q` → **146 passed** (113 pre-existing + 33 new).

Real commands on this checkout after landing. `RELAY_SESSION_TOKEN` is set per pane
process (`backend/relay_core/agent.py:646`); this subagent shell has it empty, so the
literal command returns an empty list — two live tokens of this repo are shown as well.

```
$ python3 scripts/land.py status --token 1c5596f1-c72f-4b89-8f98-132e75242b03 --json
{"token": "1c5596f1-c72f-4b89-8f98-132e75242b03", "sessions": [{"session": "1c5596f1-c72f-4b89-8f98-132e75242b03", "auto": true, "card": "", "claims": [{"path": "backend/relay_core/subagents.py", "hunks": 2, "snapshot_age_minutes": 37.9}, {"path": "tests/test_subagents.py", "hunks": 2, "snapshot_age_minutes": 37.5}]}, {"session": "jqqf", "auto": false, "card": "", "claims": []}, {"session": "jqqf2", "auto": false, "card": "", "claims": []}], "uncommitted": 4}
exit=0

$ python3 scripts/land.py status --token 89a58a8a-55cf-4032-8356-62f9b362f830 --json
{"token": "89a58a8a-55cf-4032-8356-62f9b362f830", "sessions": [{"session": "89a58a8a-55cf-4032-8356-62f9b362f830", "auto": true, "card": "", "claims": [{"path": "backend/relay_core/agent.py", "hunks": 9, "snapshot_age_minutes": 43.0}, {"path": "backend/relay_core/board_discuss_brief.md", "hunks": 1, "snapshot_age_minutes": 41.0}, {"path": "backend/relay_core/board_plan_brief.md", "hunks": 1, "snapshot_age_minutes": 40.8}, {"path": "docs/qa_evidence/2026-09-25-helper-agents-run-commands/NOTES.md", "hunks": 1, "snapshot_age_minutes": 36.4}, {"path": "tests/test_board_protocol.py", "hunks": 1, "snapshot_age_minutes": 39.8}, {"path": "tests/test_board_tools.py", "hunks": 2, "snapshot_age_minutes": 40.1}, {"path": "tests/test_queue.py", "hunks": 1, "snapshot_age_minutes": 39.1}]}], "uncommitted": 16}
exit=0

$ python3 scripts/land.py status --token $RELAY_SESSION_TOKEN --json
# (subagent shell: RELAY_SESSION_TOKEN is empty)
{"token": "", "sessions": [], "uncommitted": 0}
exit=0

$ python3 scripts/land.py orphans
no orphaned uncommitted work
exit=0

$ python3 scripts/land.py who | grep fyey-land
fyey-land — idle 0m, contact: subagent fyey-land, card #FYEY, card #FYEY
exit=0
```

`orphans` is empty right now: nothing is reaped and no session is past IDLE_HOURS
while holding hunks. `who` lists this session with its card; no reaped sessions to fold.

## Notes

- `begin` ran as `begin fyey-land --from-head scripts/land.py tests/test_land.py CLAUDE.md`
  because the implementation had already started when the session was claimed; `--from-head`
  is land.py's documented remedy (snapshots from the tip so the edits land as hunks).
- CLAUDE.md is also claimed by `zpwt-mem` (card #ZPWT), so its 3 hunks were CONTESTED and
  went through the two-step `--confirm <digest>` review; every hunk landed was read and is
  this card's work.
- `board-sync`, `reap` and the cancelled-marker refusal are exercised by the new test
  classes (`BoardSync`, `Reap`, `CancelledOwner`, `Orphans`, `ByHunks`,
  `UncommittedStatus`) rather than against live panes: no pane was reaped for real.

## Whole-card check on a clean export (parent session, 2026-09-25)

Commits: `78f1f80d` (pinned backend store, launch from the pin with RELAY_BUILD_ID, backend_rev in
pane info, Reload backend, reap call sites), `12ca8565` (land gate in `_move`, per-turn board-sync,
subagent stop kills its jobs and writes the cancelled marker, `agent_stop` tool, `/issues` ignored),
`baaefc87` (Resume card action on the card page from `land.py orphans --json`), `88d73823`
(land.py: `status`, `board-sync`, `reap`, `orphans`, `begin --owner/--card`, `--by`, cancelled-owner
refusal, `who`/`doctor` fold reaped sessions), `0dfb46a4` (this evidence).

Run on `git archive 4947ff81` in a scratch directory, never in the checkout:

```
PYTHONPATH=backend python3 -m pytest tests/test_land.py -q
146 passed in 28.77s
PYTHONPATH=backend python3 -m pytest tests/test_board_tools.py tests/test_subagents.py tests/test_agent.py tests/test_board_turns.py tests/test_guest_delegation.py -q
470 passed in 21.26s
PYTHONPATH=backend python3 tests/test_worker_backend_rev.py
OK
```

C++: `ctest --test-dir build -R runtimedirs` (17 checks) and `-R boardresume` passed in the
implementing sessions, and every commit's exact tree built in a land.py verify slot before its swap.
