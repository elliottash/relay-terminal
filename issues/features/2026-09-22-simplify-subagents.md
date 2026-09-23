---
id: SBGN
type: work
status: needs-verification
labels: [feature, subagents, ui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: msbgn
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-simplify-subagents/], related: [R0PE, GD8K], github: null}
---
# General subagents without role tags, with explicit blocked outcomes

## Issue
i agree, make these changes -- so remove the [explore] and [signal] tags as well

## Decisions
"i agree, make these changes -- so remove the [explore] and [signal] tags as well"

The accepted recommendation is to use general subagents with task-specific investigation instructions, remove the special built-in explore sandbox path, and distinguish a blocked report from successful completion. Automatic signal repair remains available internally.

## Done means
- Delegation advertises general instead of a built-in explore agent; investigation tasks inherit ordinary guest permissions.
- Tracker summaries have no bracketed role tags, including saved explore and signal rows.
- An explicit blocked final report produces blocked subagent status and a blocked linked task, persists, and can resume.
- Successful reports still complete tasks; signal repair retains its existing behavior.

## Plan
Goal: simplify delegation and make completion truthful.
Findings: agents_defs.py supplies explore; subagents.py forces read-only guest permissions and maps every normal return to done; SubagentsPanel.cpp prefixes tracker summaries.
Steps:
1. Remove the built-in explore definition and update catalog expectations; retain explicit user-defined agents and internal signal workers.
2. Add an explicit final-report status convention shared by native and guest children, propagate blocked through tasks, persistence and UI.
3. Remove tracker role prefixes and update protocol documentation.
4. Run targeted backend and Qt tests, inspect an isolated rendered tracker, then land with evidence.
Risks: existing prompts may request retired explore; return actionable guidance rather than silently weakening custom read-only definitions. Blocked is explicitly reported, not inferred from incidental prose.
Verify: catalog, guest delegation, subagent lifecycle, task linking and signal tests; Qt subagent/strip tests and an isolated screenshot.

## Tests
manual: docs/qa_evidence/2026-09-22-simplify-subagents/backend-tests.txt
manual: docs/qa_evidence/2026-09-22-simplify-subagents/qt-tests.txt
manual: docs/qa_evidence/2026-09-22-simplify-subagents/tracker.png

Reproduction commands and clean-export details: docs/qa_evidence/2026-09-22-simplify-subagents/README.md

## Execution Summary
Removed the built-in explore definition, with actionable guidance for stale requests to use general; explicit custom agent restrictions remain intact. Added Status: blocked to the shared native/guest report contract, propagated through subagent outcome, durable thread, agent_wait, linked todo and UI; resumption can complete the same task. Removed tracker role prefixes, including historical explore and signal rows. Signal repair behavior remains intact. 105 targeted backend tests passed in a clean export; both targeted Qt suites and isolated Xvfb rendering passed. Protocol and evidence are updated. Unmarked reports retain legacy done behavior; no prose-based inference or historical status rewrite.
