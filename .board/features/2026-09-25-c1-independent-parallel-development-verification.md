---
id: 8J0A
type: work
status: executing
labels: [feature, workflow, land]
assignee: claude-code
parent: 3MH4
discovered_from: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [probe], effort: high, criteria: The acceptance file passes on a clean export of main and the staged GUI shows real queue state; defects found are fixed by their owners and the run repeated.}
source: 'Approved #3MH4 implementation workstream, 2026-09-26'
links: {plans: [], commits: [585291ffd8ed], evidence: [docs/qa_evidence/2026-09-26-verify-3MH4/], related: [3MH4, AMQQ, 80X1, DV5Y, 2DP8, HEY7], github: null}
---
# C1: Independent parallel development verification

## Issue
Verify concurrency, cancellation, crash recovery, policy gates and native/guest pane flows independently of implementation.

## Done means
- A new acceptance file `tests/test_parallel_landing_acceptance.py`, written independently of the A1–A4/B1–B3 tests, drives the real `relay_core` modules and installed `relay-land`/`relay-tree` CLIs in throwaway Git repos with separate state/cache roots, and passes on the landed code for every scenario the contract's C1 paragraph names: two same-file authors (including a conflict), exact verified-commit receipts, post-submit edits untouched, dirty and clean-unlanded retention, duplicate submit, competing publisher processes and crash points before/after the ref move, target movement forcing reverification, accepted-old-policy gating, Board metadata jobs with writes during verification and path/schema guards, failed and zero-test gates, cross-repo resource exhaustion, stale artifacts unable to mask a failure, cancellation, author handoff instead of an owner question, reconciler attempt/token limits, runnable-main release retention/lag, rollback with pending work, and the legacy writers refusing in queue mode with malformed markers failing closed.
- Native worker and guest startup are staged in a temporary activated repo (no real project activated), and the GUI is captured under Xvfb with an isolated profile showing Live/workspace/queue status and the Relay (main) action, screenshots in `docs/qa_evidence/2026-09-26-verify-3MH4/`.
- Every defect found is reported to the owning agent through #3MH4 with a repro; the acceptance run is repeated after fixes, and the report names the exact commits tested and the limits (for example no live interactive guest subscription).

## Execution Summary
Independent acceptance by a Claude-family verifier (claude-opus-5-5); the A1–A4/B1–B3 implementers were mixed Claude/Codex, and the parent is Codex-family (gpt-6-astra). `tests/test_parallel_landing_acceptance.py` has 30 checks written from `docs/TREES-AND-LANDING.md` without reading the implementers' tests. They drive the landed modules, the `relay-land`/`relay-tree` CLIs, a real `backend/worker.py` process and the real `guest_launch` payload in throwaway repositories, each with its own XDG state/cache/config roots. Real model calls: none. The reconciler gets an injected fake `model_call`; the native author's model is a local OpenAI-compatible stub; the guest's turns are a fake agent that follows the injected queue instructions literally (edit, commit, `relay-land submit HEAD --request-id ID`). No live interactive guest subscription was exercised.

All 30 pass on a clean export of `main` at `c692bf22`. Three defects this suite caught on `f097f19d` were fixed by B1 in `c459f5d6` and are now regression checks: a stale ignored artifact in the reused candidate worktree let a failing candidate publish; `land.py` treated a malformed publication marker as legacy and published; and `pause` could return while a tick was still about to publish.

GUI: the real `relay` binary, built exactly at `4af516ea` through `land.py try --commit`, ran under Xvfb with an isolated profile against a staged, activated repository. `main` itself does not compile since `dd898eae` (#HEY7, already recorded there), and no #3MH4 GUI commit landed after it. `RELAY_DATA_DIR` pointed at a clean export of `main`.

![A queue-mode pane's shell starts in its own leased worktree, not the project root](docs/qa_evidence/2026-09-26-verify-3MH4/01-pane-shell-in-leased-workspace.png)

![Live reads the real snapshot: runnable main one commit behind, the main-moved notice, landed/failed/queued jobs and retained workspaces](docs/qa_evidence/2026-09-26-verify-3MH4/02-live-real-queue-main-lag.png)

![Live with the runnable main current; clicking Relay (main) ran the installed current release (run/0dd0208f…/bin, recorded by the launcher)](docs/qa_evidence/2026-09-26-verify-3MH4/03-live-main-current-launcher.png)

![Defect: a second tab's shell starts in the first pane's worktree while its worker uses its own lease](docs/qa_evidence/2026-09-26-verify-3MH4/04-second-pane-shell-in-first-pane-worktree.png)

Open defects (repros on the thread, 2026-09-26): shells under the local tmux holder inherit another pane's `RELAY_START_DIR`; pane leases are never released on close or quit, so `max_workspaces` fills; Live job ages always show `0 s`; landed workspaces read "work retained"; one failed gate wakes the author twice; pane Board writes in a second project never reach the queue; paused-mode behaviour disagrees with `pause()`'s docstring; a paused `guest_launch` prints a traceback instead of JSON; and the protocol doc has two sections numbered 37.

## Tests
`PYTHONPATH=backend python3 -m pytest -q -p no:cacheprovider tests/test_parallel_landing_acceptance.py` — 30 passed in 66.40s on a clean export of `c692bf22`
`manual: docs/qa_evidence/2026-09-26-verify-3MH4/` — Xvfb, isolated XDG profile and private tmux socket, relay built at 4af516ea

