# #AMQQ B1 — integration coordinator and sole publication path

Landed as `792cf7b18fcb` (child of #3MH4). Implementer evidence; nothing here touched the
real repository's mode, marker, hook or state — every run below is on a temporary repository.

## What was built

- `backend/relay_core/integration_service.py` — `IntegrationService(repo, state_root=None,
  cache_root=None, admission=None, reconciler=None, wake=None)`: accepted-policy capture
  (`capture_policy`, `accepted_policy`), the queue's `verifier` (accepted config under host
  admission, `RELAY_JOBS` from the granted cpus; Board schema checks for metadata jobs), the
  `reconcile` adapter (`enrich_context` adds both sides' cards and intents from the canonical
  Board and the accepted policy; merge conflicts only, a failed gate returns to the author),
  `run_once`/`run` (admission before pick, recovery of in-flight rows, `main_moved` events,
  `MainUpdater` coalescing runnable-main builds outside the publisher lock), durable handoffs
  (`handoffs`, `ack_handoff`, `deliver_handoffs`, idempotent card notes, optional `wake`),
  workspace API for B2 (`allocate_workspace`, `submit`, `submit_board_snapshot`,
  `tree_status`, `queue_status`, `snapshot`, `events`), `try_candidate`, `main_status`/
  `main_executable`, `inventory`, `activate(dry_run=…)`, `pause`, `rollback`, hook install.
- `scripts/relay-land` / `relay_core.landq.main` dispatch the service verbs; `python -m
  relay_core.integration_service` works with the backend on `sys.path`.
- `scripts/land.py`: `publication_marker`, `require_legacy_publisher`, `legacy_publication`
  (shared flock on `<common dir>/relay-publication.lock`) around the ref swap of `commit`,
  `repair` and `board-sync`; `board-sync` routes to the queue in queue mode; hook version 2
  (`HOOK`, identical bytes to the service's) refuses only the main checkout's shared index and
  chains `pre-commit.project`; `hook install` upgrades a v1 hook, `--force` preserves a foreign one.
- `backend/relay_core/agent.py` `_sync_board_writes`: runs land.py with `cwd=<canonical repo>`.

## Test runs (2026-09-26, this checkout and a clean `git archive 792cf7b18fcb` export)

```
python3 -m pytest tests/test_integration_service.py -q        20 passed
python3 -m pytest tests/test_integration_transitions.py -q    13 passed
python3 -m pytest tests/test_landq.py tests/test_parallel_landing_flow.py -q   46 passed
python3 -m pytest tests/test_land.py -q                        149 passed
```

Coverage of the owner's review notes: 5 (all three legacy publishers guarded, `LegacyGuardTests`),
6 (hook v2 in a linked worktree, `HookTests`), 8 (`DaemonTests.test_main_updater_coalesces…`,
`lag_commits` in `main_status`), 9 (both-card intents in the model prompt,
`ReconcileTests`; `author_required` handoff addressed to the workspace session; outbox retry
`CrashTests.test_handoff_delivery_failure…`), 10 (`CutoverTests`: symbolic HEAD with a dirty
checkout, same-tip rollback, fast-forward rollback, fail-closed rollback staying paused,
Board-identical staging, own-commits refusal, `--keep-head`), crash before/after `update-ref`
(`CrashTests`), transition-lock drain (`test_transition_lock_drains…`).

## Manual smoke (temporary repo under /tmp, `XDG_STATE_HOME` isolated)

`project-init` → `activate --dry-run` (plan printed, nothing changed) → `activate` with a dirty
checkout (HEAD → `refs/heads/human`, `git status` unchanged, marker written, hook v2 installed)
→ `workspace create` → ordinary `git commit` in the workspace succeeds while a commit in the
main checkout is refused with the queue-mode sentence → `submit` → `run --once` lands, installs
the runnable main (`main-status` shows `lag_commits: 0`), delivers `landed` handoffs to the
workspace session → legacy `land.py commit` exits 2 with the relay-land message, `board-sync`
submits a metadata job that `run --once` lands → `rollback` with a pending job and an untracked
Board file fails closed (mode `paused`, HEAD still `human`, nothing reset).

## Not done here (owner's call)

- The actual repository was not activated; `relay-land --repo . inventory` and
  `activate --dry-run` are the review step before any cutover.
- B2/B3 wire `wake` and the pane/GUI surfaces; the service API above is what they call.
