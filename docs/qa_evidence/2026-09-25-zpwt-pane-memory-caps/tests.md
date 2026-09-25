# Tests, 2026-09-25

- `ctest --test-dir build -R '^isolation$'` on commit 0bfe33ee's working tree: 7/7 slots pass,
  including the new `scaledDefaultsFollowTheMachine` (agent RAM/2 + 2G floor, shell 3*RAM/4 +
  4G floor, total RAM-max(8G,RAM/10) floored at RAM/2 with sized() rounding to "110G" on 122G,
  escapee clamp(RAM/16,2G,8G)).
- `python3 -m pytest tests/test_isolation.py tests/test_jobs.py`: 34 passed, both in the shared
  working tree and in a clean `git archive 0bfe33ee` export. New: raise-only oom_score_adj
  subprocess tests, scoped_argv shape/validation, prepare-time memory_max normalization
  refusal, and a live `run_command memory_max=1G` asserting `/proc/self/cgroup` shows
  `app-relay.slice`.
- land.py verify slot built the exact landed tree (`--target relay`) before the swap; RELAY_JOBS
  auto-lowered to 4 for the slot.
- `scripts/relay-build --check "app-relay.slice"` and `--check "total_memory_max"`: both
  literals present in the built binary.
