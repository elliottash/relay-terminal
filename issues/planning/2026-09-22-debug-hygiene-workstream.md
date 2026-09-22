---
id: HG26
type: work
status: needs-verification
labels: [feature, diagnostics, qa]
assignee: codex
rank: zh26
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [b1e58b5ee771dba09ce7fc541595dbb8ef5fa46d, 2f257ae6, 9bef2478, 24a6b202, 5304f3a2, a89d2199, 665814e7, b6d1999c, c26b0ec5, e280f0ec, 53f59865, 3e77ced3], evidence: [docs/qa_evidence/2026-09-22-debug-hygiene/report.md, docs/qa_evidence/2026-09-22-debug-hygiene/delivery.md], related: [AQ6X, MSW7, YJG7, WEVT, SW1D, SJTR, 25XG], github: null}
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
Delivered the complete proposed workstream with five Relay subagents and parent integration. Logging distinguishes pending/refused/command/timeout/transport/internal/unknown outcomes, appends origin/run/build identity, isolates test logs in scoped runners/fixtures, and classifies worker exits by lifecycle intent. Reports add classified rates, p50/p95, safe reason codes, explicit signal-card links, private retained snapshots and non-additive weekly review.
Existing model-switch/fallback fixes were independently verified; quota suppression is typed, bounded, interruptible and credential-aware. Collection failures now retain runnable scope and resolve only on same-scope evidence. Two real 177-test module runs naturally resolved the owned historical signals without manual state changes.
WEVT is verified/done; 40SN and SW1D advanced to QA with fresh startup and real Kimi preview/Apply evidence on a disposable board. Independent report review found and confirmed the fix for malformed snapshot timestamps. Full evidence and commit mapping: docs/qa_evidence/2026-09-22-debug-hygiene/delivery.md. No unrelated board cleanup or automatic scheduling was performed.

## Tests
- manual: docs/qa_evidence/2026-09-22-debug-hygiene/delivery.md
- manual: docs/qa_evidence/2026-09-22-hg26-logging/results.md
- manual: docs/qa_evidence/2026-09-22-quota-suppression/report.md
- manual: docs/qa_evidence/2026-09-22-debug-hygiene/collection-recovery.md
- manual: docs/qa_evidence/2026-09-22-hg26-verification/report.md
- manual: docs/qa_evidence/2026-09-22-hg26-report-review/report.md
