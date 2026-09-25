# #SZHQ — agent scratch no longer fills the disk

Evidence for `land.py` pooled verify slots + automatic `gc`, `relay-scratch`,
the bundled `disk-hygiene` skill, and the in-app scratch monitor.

## Measurements (this machine, 20 agents, 3.7 TB disk)

| When | What | Value |
| --- | --- | --- |
| 2026-09-24, before | `/tmp/claude-1000` | **380 GB** (`land/` 156 GB across 255 per-session verify builds; `-home-elliott-repos-relay-terminal` 112 GB; ~600 more scratch trees) |
| 2026-09-24, before | `land/` session count with a verify dir | 255, none ever reclaimed |
| 2026-09-25, after | `/tmp/claude-1000/land` | **4.3 GB** — two shared build slots + live sessions' snapshots only |
| 2026-09-25, after | verify slots on disk | 2 (`verify-slots/relay-terminal-<id>-{1,2}`), bounded by `RELAY_LAND_VERIFY_SLOTS` regardless of session count |
| 2026-09-25 | `relay-scratch check` | `agent scratch takes 64.2 GB (budget 20.0 GB)` — exit 1, live sessions keep their own trees warm (idle < 24 h), 382 MB idle-reclaimable |

The 64 GB still present belongs mostly to live sessions and is deliberately not
reclaimed: `gc` removes only entries idle past `--idle-hours` that no live process
uses. The pre-existing trees beyond `land/` were removed outside this card's work;
what the landed code guarantees is that `land/` can no longer grow per session.

## Commands to reproduce

```
python3 -m unittest tests.test_land tests.test_scratch tests.test_skills
scripts/relay-scratch gc            # dry run: lists every entry it would remove
scripts/relay-scratch check         # exit 1 over budget / low free space
python3 scripts/land.py who         # prints the land root's disk use
python3 scripts/land.py gc --dry-run
ctest --test-dir <build> -R '^(scratchmonitor|notifications)$'
```

## Results

- `tests.test_land` — 72 passed (5 new: bounded slot pool, second slot under lock,
  gc stale/fresh sessions, gc legacy verify dirs, gc dry-run, hourly auto-gc, `who` disk line).
- `tests.test_scratch` — 12 passed.
- `tests.test_skills` — 38 passed (bundled `disk-hygiene` discovered and loadable).
- Build gate on the landed monitor tree (`67083c2f`): `scratchmonitor` and
  `notifications` ctest suites passed in the verify slot before the swap.
- Build gate on `01a449fa`: exact tree compiled before the swap.

## Commits

- `8b9410ad` — `land.py`: verify builds share a bounded pool of slots; `gc` reclaims stale sessions; root follows the user's uid.
- `10ecec2e` — `relay-scratch` (`report` / `gc` / `check`) on `relay_core.scratch`.
- `5112bc20` — bundled `disk-hygiene` skill.
- `67083c2f` — in-app monitor: bell notification with a Clean up action (`src/ScratchMonitor.{h,cpp}`), `RELAY_SCRATCH_MONITOR=0` to disable.
- `01a449fa` — install `scripts/relay-scratch`.
