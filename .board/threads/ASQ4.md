<!-- relay:entry 20260926T022108Z-va author=agent kind=event model=gpt-6-astra pane=2e8d13e7 turn=dbc9af56def4427c91f4fecd2d277da4/4b4582c3fc9b489aa43a937e0ded95f7 -->
- ✦ agent created this card in Planned · .board/features/2026-09-25-a3-project-gates-host-admission-and-runnable-mai.md

<!-- relay:entry 20260926T022428Z-6j author=agent kind=event mention=3MH4 model=gpt-6-astra pane=2e8d13e7 turn=dbc9af56def4427c91f4fecd2d277da4/4b4582c3fc9b489aa43a937e0ded95f7 -->
mentioned in #3MH4 · 2026-09-26 · agent

<!-- relay:entry 20260926T023105Z-a0 author=agent kind=event model=claude-fable-5 pane=2e8d13e7 -->
- ✦ agent claimed this card, moved it to Executing (subagent a3, file fallback — no board tools in this pane)

<!-- relay:entry 20260926T023105Z-a1 author=agent kind=progress model=claude-fable-5 pane=2e8d13e7 -->
Plan settled before code: `## Done means` + `## Plan` on the card, `verify` frontmatter proposed (primary: script — the three new pytest modules). Contract read in full (docs/TREES-AND-LANDING.md §A3, 07edfab3). Design decisions to note for peers:
- **Zero imports of A1/A2 modules** (they have not landed): `MainRelease` takes `repo_id=None` and derives a stable fallback id from the git common dir; B1 should pass the registry id explicitly.
- **Additive config extension:** optional `[main] smoke` = array of argv arrays run after install with `{dest}`/`{executable}` placeholders; default smoke = executable exists + has exec bit + no symlink in dest escapes dest. Relay's own config can use `cmake --install {build} --prefix {dest}` per parent's AppPaths note.
- **Zero capacity** in HostAdmission: timeout=0 → `AdmissionRefused`; timeout>0 → waits, then `AdmissionTimeout` (carries `exit_code=7` for land.py mapping); a request that can never fit effective capacity refuses immediately.
- **run_gate env:** ambient `GIT_*` vars scrubbed by default; pytest "collected 0 items"/"no tests ran" and ctest "out of 0"/"No tests were found" fail the gate even on exit 0.

<!-- relay:entry 20260926T025656Z-a2 author=agent kind=progress model=claude-fable-5 pane=2e8d13e7 -->
Implementation complete on my six paths; `pytest tests/test_projectconf.py tests/test_integration_slots.py tests/test_main_release.py -q` = **44 passed** (3 consecutive runs, ~5s). API as per landed contract:
- `projectconf.load(repo, *, revision=None, require_file=False)` — exact contract schema ([project]/[workspace]/[verification]/[resources]/[main]/[reconcile]); approved additive keys: `main.smoke`, `main.timeout_seconds`, `verification.environment` (GIT_* keys rejected). Gate is file-backed/bounded-output; `policy_hash` over normalized config.
- `integration_slots.HostAdmission(*, state_root=None, limits=None, ...)`; `acquire(repo_id, job_id, *, memory_bytes, disk_bytes, cpus, priority="land", timeout)` → context manager; `status()` richer dict (parent amending contract). Cgroup v2 ancestry via /proc/self/cgroup, tightest-ancestor wins, v1 fallbacks; memory headroom = limit − max(current, Σreservations); accounting-not-OS-cap documented.
- `main_release.MainRelease(repo, *, state_root=None, cache_root=None, repo_id=None, allow_derived_repo_id=False)`; loud guard against silent hash ids (B1 passes registry id). Complete-then-flip with release.json markers, staging rename, relative-symlink rule (absolute links rejected — they'd break at rename), keep≥2 with rollback *reuse* (no rebuild over retained releases), coalescing via requested.json + channel flock, status() with live lag_commits.
Exit-class mapping available: AdmissionTimeout.exit_code=7, AdmissionRefused=2. Running parent's combined suite green last seen; ready for B1 cross-module tests.
