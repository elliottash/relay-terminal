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

## Amendment, same day (after the Try-it rehearsal)

The Try-it staging (docs/qa_evidence/2026-09-25-tryit-ZPWT/) caught one real defect: a job could
swap past its own memory bound — a 900M hog survived a 600M memory.max, because memory.max caps
RAM and swap needed MemorySwapMax. `scoped_argv` now pins `MemorySwapMax=0`, and a live
enforcement test was added (`test_memory_max_kills_only_the_command_when_passed`: 700M hog under
memory_max=400M → exit -9, killed_for_memory, note names the bound). Suites after the fix:
`pytest tests/test_isolation.py tests/test_jobs.py` — 35 passed (shared tree and clean export).
