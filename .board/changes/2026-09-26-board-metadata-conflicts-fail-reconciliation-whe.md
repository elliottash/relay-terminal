---
id: GKMW
type: work
status: planned
labels: [bug, land, board, guest]
parent: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-26'
source: This conversation, 2026-09-26; queue jobs 3c9fd1e8ffbb43e5 and 7879feb36cc50b96
links: {plans: [], commits: [], evidence: [], related: [AMQQ, 80X1, P9ZA, BP15], github: null}
---
# Board metadata conflicts fail reconciliation when the guest has no session token

## Issue
In queue mode, stale Board metadata jobs can enter reconciliation, which launches a guest provider without a session token and returns WorkspacePreparationError. The Board update remains unpublished, so the author has no working automatic handoff.

> does this point to any issues with the new build system? file a card if needed.
> — elliott · [session:c3e05da713f34109bf1893fed2425e6f](relay://session/c3e05da713f34109bf1893fed2425e6f) · 2026-09-26

## Done means
- A stale Board metadata submission either reconciles safely or returns an actionable author handoff; it never fails while preparing a guest because a session token is missing.
- A focused queue/integration test reproduces a metadata collision after `main` moves and checks the resulting publication or handoff, including the guest launch context.
- The status and notification identify the actual conflict and the next action.

## Plan
1. Reproduce a stale Board metadata submission after `main` advances in a temporary registered queue repository. Assert the current `WorkspacePreparationError` handoff so the failure is pinned.
2. Trace the metadata conflict path through `integration_service`/`landq` into the reconciler's guest launch. Keep Board-only metadata safe: either provide a valid non-development reconciliation context or return a precise author handoff without starting a guest; choose based on the existing queue contract and test both the ordinary and collision paths.
3. Run focused queue, integration, reconciliation, and guest-launch tests. Verify the resulting status, notification and Board write are durable. Preserve conservative refusal for genuine binary or over-limit code conflicts.

No owner product decision is needed; this repairs an internal publication path. Related baseline gate failure is tracked separately by #BP15.
