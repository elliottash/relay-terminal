---
id: AMQQ
type: work
status: needs-verification
labels: [feature, workflow, land]
assignee: claude-code
parent: 3MH4
discovered_from: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: cutover leaves the human checkout files and index untouched and legacy publishers refuse; the service publishes only gate-passed candidates; rollback fails closed on a stale checkout, effort: medium, stakes: high}
source: 'Approved #3MH4 implementation workstream, 2026-09-26'
links: {plans: [], commits: [792cf7b18fcb], evidence: [docs/qa_evidence/2026-09-25-amqq-b1/NOTES.md], related: [], github: null}
---
# B1: Wire integration coordinator and sole publication path

## Issue
Connect trees, queue, gates, resources and AI reconciliation; route legacy and Board writers safely at controlled cutover.

## Done means
- `relay_core.integration_service.IntegrationService` wires A1 trees, A2 queue, A3 config/admission/main releases and A4 reconciliation into one publisher per repository: immutable submissions (a workspace submits its branch tip), the accepted policy captured from the target at activation (never the candidate's own config), required gates run under host admission with the granted CPUs fed to `RELAY_JOBS`, Board metadata jobs schema-checked, reconciliation enriched with both sides' card intents from the canonical Board, and every gate-passing candidate published automatically.
- Outbox deliveries are durable: each `landed`/`failed`/`conflict`/`author_required` notification becomes a handoff row addressed to the author workspace's session plus an idempotent note on the card thread, retried through the queue outbox until written; nothing is ever routed to an owner prompt.
- Runnable main updates coalesce outside the publication lock (a background updater) and `main-status` shows the installed sha and its lag behind the target.
- CLI (`scripts/relay-land`, also `python -m relay_core.integration_service`): `run [--once]`, `project-init`, `activate [--dry-run]`, `inventory`, `pause`, `rollback`, `try`, `main-status`, `main-run`, `snapshot`, `handoffs`, `board-submit`. Activation is registry mode plus a publication marker in the Git common dir, separate from the config file; default stays `legacy`.
- Cutover creates a human branch at the target tip and moves HEAD symbolically (files and index untouched); it refuses when another worktree has the target attached or a legacy land.py publisher is still running; legacy `land.py commit`, `repair` and `board-sync` are guarded through the marker and a shared transition lock (board-sync routes to the queue in queue mode); the Relay pre-commit hook lets a linked worktree's private index commit while still refusing the main checkout's shared index and chaining any non-Relay hook. Rollback drains the publisher, keeps jobs, refs and the human branch, syncs the checkout only by a fast-forward git itself accepts, and fails closed otherwise.
- Failure is recognised by `tests/test_integration_service.py` and `tests/test_integration_transitions.py` failing: real temporary repositories, crash before/after publication, cutover and rollback with pending work, never the actual repo.

## Plan
**Goal** B1 of #3MH4: the coordinator and the sole publication path, with a clean API for B2/B3.

**Findings** A1 `trees` (registry, `configure_repo` modes), A2 `landq.Queue` (verifier/reconcile callbacks, outbox), A3 `projectconf.run_gate`/`HostAdmission`/`MainRelease`, A4 `Reconciler.reconcile_sync` are on disk with stable contracts. Legacy `land.py` has three publishers (`cmd_commit`, `cmd_repair`, `cmd_board_sync`) and a hook that refuses every default index.

**Steps**
1. `integration_service.py`: service DB (policies, transitions, handoffs, events), verifier and reconcile adapters, run loop with coalesced main updater, workspace allocation/submit API, snapshot/status shapes for the protocol, inventory/activate/pause/rollback/try, CLI.
2. `landq.py`/`relay-land`: dispatch the service verbs; keep A2 verbs.
3. `land.py`: publication marker guard + shared transition lock in the three publishers, board-sync delegation, worktree-aware hook.
4. `agent._sync_board_writes`: run in the canonical repo.
5. Tests on temporary repos: service flow, crash windows, cutover, rollback, hook.

## Execution Summary
Landed `792cf7b18fcb`: `backend/relay_core/integration_service.py` (the coordinator: accepted policy, verifier under admission, Board schema checks, reconcile enrichment with both cards' intents, durable handoffs to the author session plus idempotent card notes, coalesced runnable-main updater outside the publisher lock, workspace/submit/snapshot API for B2/B3, try, inventory, activate/pause/rollback, hook install, CLI); `landq.py`/`scripts/relay-land` dispatch the service verbs and `python -m relay_core.integration_service` works; `scripts/land.py` guards all three legacy publishers through the publication marker and a shared transition lock, routes board-sync to the queue in queue mode, and installs the version-2 hook (linked worktrees commit; the shared index is still refused; a project hook is chained); `agent._sync_board_writes` runs in the canonical repository. Evidence: `docs/qa_evidence/2026-09-25-amqq-b1/NOTES.md`. Nothing activated the real repository.

## Tests
- `tests/test_integration_service.py` — 20 pass: hook parity and behaviour, activation (dry run, dirty checkout untouched, refusals), weaker candidate config still runs the accepted gate, two same-file authors with receipts/handoffs/notes/main release, failed gate handoff, admission shortfall defers, Board snapshots and schema refusals, try, reconciliation with both cards' intents in the prompt and the gate on the resolution, refused reconciliation returning to the author session, legacy land.py refusal and board-sync routing, CLI dispatch and installed layout, project-init.
- `tests/test_integration_transitions.py` — 13 pass: same-tip rollback, pause/resume, rollback with pending work (fast-forward), fail-closed rollback staying paused, Board-identical staging, own-commits refusal and `--keep-head`, transition-lock drain, inventory of legacy sessions, crash before/after `update-ref`, handoff retry from the outbox, the daemon loop with a second-loop refusal, main-updater coalescing.
- `tests/test_landq.py` + `tests/test_parallel_landing_flow.py` — 46 pass; `tests/test_land.py` — 149 pass. All of the above also pass on a clean `git archive 792cf7b18fcb` export (79 for the four service/queue files).

## Try it
In a scratch copy of any repository with a committed `.relay/project.toml`: `relay-land --repo <copy> project-init`, `activate --dry-run`, `activate`, `workspace create s1`, commit in the returned `execution_cwd`, `submit <sha> --request-id r1 --workspace-id <id>`, `run --once`, `main-status`, `handoffs --session s1`, `rollback`. On this repository only `inventory` and `activate --dry-run` are for now; the cutover itself is the owner's review step.
