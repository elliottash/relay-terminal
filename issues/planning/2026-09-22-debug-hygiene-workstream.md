---
id: HG26
type: work
status: executing
labels: [feature, diagnostics, qa]
assignee: codex
rank: zh26
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [b1e58b5ee771dba09ce7fc541595dbb8ef5fa46d], evidence: [docs/qa_evidence/2026-09-22-debug-hygiene/report.md], related: [AQ6X, MSW7, YJG7, WEVT, SW1D, SJTR, 25XG], github: null}
---
# Debug and hygiene workstream grounded in runtime logs and signals

## Issue
analyze the logs and associated signals / cards to propose a debug / hygiene workstream

## Decisions
2026-09-22, owner: "deliver all the work with subagents". Implementation of the proposed workstream is authorized; use Relay delegation.

## Done means
Tool logs distinguish normal polling/refusal from failures and identify test/QA origin; expected worker exits are not errors. Reports preserve unknown historical classifications and provide latency, origin and failure breakdowns plus a local snapshot/review workflow.
Model changes commit only after valid transport construction, preserve truthful fallback errors, and distinguish exhausted quota from transient limits. Collection signals track runnable scope and resolve only after same-scope evidence. Existing WEVT/40SN/SW1D work gets bounded independent verification rather than reimplementation.
Targeted tests and isolated GUI evidence establish delivery; unrelated work and owned signals remain preserved.

## Plan
**Goal:** Deliver the five packages in the evidence report using Relay subagents.
**Steps:** 1. Delegate outcome/origin/exit logging. 2. Delegate model/provider fixes on existing cards. 3. Delegate collection signal recovery. 4. Parent implements report and manual snapshot/review workflow. 5. Independently verify existing QA cards and new changes, reconcile evidence and index.
**Ownership:** Logging agent owns logging/classification and worker-exit hunks; model agent owns model/provider switching/retry hunks; signal agent owns collection/folding code. Shared agent.py/Pane.h hunks must be coordinated, not overwritten. Parent owns report scripts and HG26.
**Risks:** Concurrent shared checkout, logs contain historical unknowns, GUI builds serialize, existing signal claims belong to codex-hq.
**Verify:** Targeted unit tests for each package, exact-tree build gates for C++ changes, isolated GUI scenarios and review of aggregate output. No full suite or automatic review scheduling is required.

## Execution Summary
Completed the requested analysis and proposed five work packages in the linked evidence report: outcome/origin hygiene, model/provider reliability on #MSW7/#YJG7, collection-signal recovery on #AQ6X, targeted card verification, and a recurring review. The aggregate contains 3,938 tool results and 337 unsuccessful classifications; test traffic and normal poll/exit semantics make these unsuitable as raw bug counts. Existing signals and cards were not taken over or modified. Application fixes remain proposals.

## Tests
- manual: docs/qa_evidence/2026-09-22-debug-hygiene/report.md
- `python3 scripts/relay-events.py --since 2026-09-22 --json`
- `python3 scripts/relay-board.py signals`
