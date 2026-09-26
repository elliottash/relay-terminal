# #C8Z7 — land gate runs `land.py status` in the board repository

- `land-gate-tests.txt`: `LandGateTests` run against a scratch copy of the backend with the fix
  removed (4 failures: a dirty move from an unrelated cwd went through with a warning instead of
  being refused, and failed/timed-out/unreadable statuses left no trace in the thread), then
  against the fixed tree (11 passed).
- `real-land-status.txt`: the real `scripts/land.py status` fails from `/home/elliott` with the
  exact message #FKSN reported, and answers with `cwd=<board repo>`, which `_land_gate` now passes.
- Full file: `PYTHONPATH=backend python3 -m pytest tests/test_board_tools.py` → 340 passed.
