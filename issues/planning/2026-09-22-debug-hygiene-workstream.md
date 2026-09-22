---
id: HG26
type: work
status: needs-verification
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

## Done means
The proposal ranks observed failure clusters, links existing cards, distinguishes confirmed facts from hypotheses, and gives a verification condition for each work package.
Analysis must account for test traffic, normal lifecycle/poll events, retained-log limits and signal ownership. No application fixes or existing-card takeovers are part of this request.

## Plan
**Goal:** Propose a bounded debug/hygiene workstream from local evidence.
**Findings:** Worker/GUI logs, private test history and signals, and the current cards are available locally; board MCP read/write tools are not exposed.
**Steps:** 1. Aggregate today's retained logs. 2. Inspect anomalous events, code paths and signal histories. 3. Cross-reference existing cards and prioritize work with acceptance checks.
**Risks:** Tests share diagnostic logs; rotated logs are incomplete; tool failures do not imply product bugs; other sessions own open signals.
**Verify:** Read-only log analysis, source inspection and card/thread comparison; preserve aggregate evidence in the linked report.

## Execution Summary
Completed the requested analysis and proposed five work packages in the linked evidence report: outcome/origin hygiene, model/provider reliability on #MSW7/#YJG7, collection-signal recovery on #AQ6X, targeted card verification, and a recurring review. The aggregate contains 3,938 tool results and 337 unsuccessful classifications; test traffic and normal poll/exit semantics make these unsuitable as raw bug counts. Existing signals and cards were not taken over or modified. Application fixes remain proposals.

## Tests
- manual: docs/qa_evidence/2026-09-22-debug-hygiene/report.md
- `python3 scripts/relay-events.py --since 2026-09-22 --json`
- `python3 scripts/relay-board.py signals`
