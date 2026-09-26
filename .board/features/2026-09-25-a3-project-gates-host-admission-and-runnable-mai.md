---
id: ASQ4
type: work
status: executing
labels: [feature, workflow, land]
assignee: agent
parent: 3MH4
discovered_from: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: 'pytest on the three new test modules passes and shows: a zero-test gate fails closed, over-capacity admission refuses/waits explicitly, and a main release flips the current symlink only after a smoke pass', sign_off: none, effort: medium}
source: 'Approved #3MH4 implementation workstream, 2026-09-26'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A3: Project gates, host admission and runnable main

## Issue
Implement reusable project configuration, bounded host build admission and atomic runnable-main releases.

## Done means
- `backend/relay_core/projectconf.py` loads/normalizes/validates the `.relay/project.toml` v1 schema from the contract, exposes `load`, `detect`, `policy_hash`, and runs the gate via `run_gate`; a gate whose recognized runners report zero tests fails closed (never silently approves), as does a gated project with no verification commands.
- `backend/relay_core/integration_slots.py` `HostAdmission` is a SQLite-WAL ledger under `state_root/integration/` whose reservations die with the holder process (flock reaping), enforce cgroup-v2/v1-aware effective CPU/memory limits plus live `MemAvailable`/disk headroom, and explicitly refuse (timeout=0) or time out (exit-classifiable `AdmissionTimeout`, maps to land exit 7) on zero/over capacity — never silently admit.
- `backend/relay_core/main_release.py` builds a target SHA from a persistent, coalesced source worktree under `cache_root/integration/<repo-id>/tip/`, installs via the project's install commands into `state_root/.../tip/run/<sha>/`, smoke-gates, flips `current` by atomic symlink rename, keeps >= 2 releases, and records real duration/bytes in `status()`.
- Failure is recognised by: any silent gate pass on zero tests, any admission that exceeds effective capacity, any `current` flip to a release that never smoke-passed, or tests failing.
- Contract signatures from `docs/TREES-AND-LANDING.md` §A3 kept exactly; optional extensions (e.g. `[main] smoke`) are additive and documented in the card thread.

## Plan
**Goal:** land the A3 contract modules with real behavior, not skeletons, in the shared checkout on `main`.

**Findings (exact paths):**
- Contract: `docs/TREES-AND-LANDING.md` §A3 (commit 07edfab3) — schema, APIs, invariants.
- New files only: `backend/relay_core/{projectconf,integration_slots,main_release}.py`, `tests/test_{projectconf,integration_slots,main_release}.py`. A1/A2 peers have not landed `trees.py`/`landq.py` yet, so A3 must not import them; `repo_id` is accepted as a parameter with a stable local derivation fallback.
- Style anchors: `backend/relay_core/test_probe.py` (bounded, docstring-first, no raising on unreadable input), `scripts/land.py` (flock + cgroup reading idioms).
- Parent guidance: `cmake --install <build> --prefix {dest}` yields a self-contained runtime (AppPaths prefers `<exe>/../share/relay` over `RELAY_SOURCE_DIR`); main config may invoke `scripts/relay-build` in a persistent source.

**Steps:**
1. `projectconf.py`: TOML v1 normalizer (`tomllib`), type/placeholder/danger validation, `detect()` suggestions, `policy_hash()` (sha256 of canonical JSON), `run_gate()` with per-command timeout, env scrub of `GIT_*`, pytest/ctest zero-test parsing, `selected_tests` additive-only.
2. `integration_slots.py`: schema-versioned SQLite WAL ledger, flock-backed liveness, cgroup v2+v1 and `/proc/meminfo`/`statvfs` effective-capacity resolution, waiter rows with land-priority + anti-starvation, transactional admit, context-manager `acquire`, `status()`.
3. `main_release.py`: coalescing `update(sha, config)` under a flock, persistent detached source worktree + build dir in cache, placeholder substitution (whole-argv-entry or literal), completeness + smoke gate, atomic `current` symlink flip, keep>=2 pruning, crash-safe `release.json` markers, `status()` with lag and real duration/bytes.
4. Tests: one file per module using tmp git repos and fake build/install/smoke commands; zero-test gate, refusal paths, timeout path, crash-resume, symlink atomicity, priority/fairness, cgroup override injection.
5. Run focused `pytest tests/test_projectconf.py tests/test_integration_slots.py tests/test_main_release.py`, inspect `land.py status` diff, commit via `land.py commit -m "... #ASQ4 #3MH4" --wait-seconds 900`.

**Risks:** peer A1/A2 interfaces may drift — mitigated by zero imports of their modules and a documented `repo_id` fallback; real-repo activation is out of scope (isolated temp repos only, per owner).

**Verify:** `python3 -m pytest tests/test_projectconf.py tests/test_integration_slots.py tests/test_main_release.py -q` — green; thread note records resource-policy tradeoffs.
