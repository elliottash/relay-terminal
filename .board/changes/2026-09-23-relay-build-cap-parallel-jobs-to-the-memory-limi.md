---
id: 04EC
type: work
status: needs-verification
labels: [bug, build]
assignee: agent
implemented_by: glm/glm-5.3
session: 0ca5e03e-71d8-4fa4-9748-9b995718f37d
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: pane 1, 2026-09-24
links: {plans: [], commits: [], evidence: [tests/test_relay_build.py (15 OK, commit d4eb5f17)], related: [F9SD, MEMS], github: null}
---
# relay-build: cap parallel jobs to the memory limit so agent builds cannot OOM the pane

## Issue
i got a note this agent ran out of memory:
bb89ea17

but it wasnt doing anything compute intensive, can you find out whats happening

(investigation: agent bb89ea17 was killed at 20:53:56 EDT while running scripts/relay-build, whose default is 8 parallel cc1plus jobs; inside the pane's systemd scope (MemoryMax=8G, MemorySwapMax=2G) that peaks past the limit and systemd kills the pane.)

can you change the build process to prevent that

## Done means
- `scripts/relay-build` chooses its parallelism from the memory limit it is actually running under (the pane's systemd scope cgroup when present, else the host), so a default build inside an 8 GiB agent pane no longer spawns 8 `cc1plus` jobs that systemd OOM-kills.
- The cap also bounds `scripts/land.py`'s verify build, which hardcodes the same `--parallel 8`.
- An explicit `RELAY_JOBS` above what the memory limit allows is clamped with a printed note, not silently obeyed into an OOM; a lower explicit value still wins.
- Failure would show as: a build inside a scope with a small `memory.max` still passing `--parallel 8` to cmake, or `tests/test_relay_build.py`'s job-cap tests failing.

## Execution Summary
Landed d4eb5f17d6f4781008b29f26dcc73ccc210098c3 on main (land.py session `jobcap`, no conflicts, no verify build needed — no C++ paths).

- `scripts/relay-build`: new `memory_limit_bytes()` (cgroup v2, walking from the pane scope to its nearest limiting ancestor; cgroup v1 fallback; bounded by MemTotal) and `jobs_for_environment()` — `--parallel` is now RELAY_JOBS (default 8) clamped to about one 2 GiB compile job per 2 GiB of limit; an explicit value above the cap is lowered with a printed note, below it wins.
- `scripts/land.py`: `run_verify` gets its jobs from `relay_build_jobs()`, which executes the wrapper's own logic (no copy to drift).
- CLAUDE.md documents the clamp.

On this pane's own scope the wrapper now reports: `RELAY_JOBS=8 lowered to 4: this build may use 8.0 GiB and one compile job peaks near 2 GiB`.

## Tests
`python3 -m unittest tests.test_relay_build` — 15 tests, OK (2026-09-24, this pane).

- passed — `test_the_pane_scopes_memory_max_is_the_limit`: an 8 GiB `memory.max` on the scope is read through the fake cgroup tree
- passed — `test_a_slice_ancestor_can_hold_the_limit`: scope unlimited, `user.slice`'s 6 GiB wins
- passed — `test_without_a_cgroup_limit_the_hosts_ram_is_the_limit`: v1 sentinel falls through to MemTotal
- passed — `test_jobs_are_clamped_to_the_memory_limit`: 8 jobs → 4 with a note; explicit 2 wins; never below 1
- passed — `test_without_any_limit_the_requested_count_stands`: unconstrained hosts keep RELAY_JOBS
- passed — `test_land_verify_build_applies_the_same_cap`: land.py's `relay_build_jobs` returns the capped 4
- passed — `test_the_building_line_carries_the_capped_count`: a real build prints `--parallel 4` and the lowering note, never `--parallel 99`
- passed — the 8 pre-existing wrapper tests (lock, restamp, stale-object, --check, --fast) still pass
- not applicable — full `ctest`/`./scripts/test.sh` suites: owner runs those; this change is build tooling only
