---
id: C8WX
type: work
status: needs-verification
labels: [bug, context, guests]
assignee: codex
implemented_by: opus/opus via claude-code
rank: m
created: '2026-09-21'
source: User request in Relay, 2026-09-21
links: {plans: [], commits: [f94f669d64ba057b35f68ad99213c9511f87fbe3], evidence: [docs/qa_evidence/2026-09-21-guest-context-meter/], related: [GT7X], github: null}
---
# Show the running guest’s context in Relay’s meter

## Issue
why is gpt astra saying my max context is 128K

its saying 128K in relay's context meter

/deliver trace and fix the bug and see if others turn up. it affects claude as well

## Plan
**Goal:** Display the running guest's measured context for both Codex and Claude.

**Findings:** src/Pane.h ignores context.guest_context. backend/relay_core/guest_harness_provider.py only identifies the guest once measurements exist. The tooltip incorrectly promises Relay's compaction threshold.

**Steps:**
1. Preserve guest identity before the first usage report; distinguish unknown context from Relay's fallback.
2. Use a tested context-display selector in the pane, including guest compaction wording, /context and remote state.
3. Exercise both adapters and guest/native transitions; inspect related stale-usage paths.
4. Build, run targeted tests and an isolated GUI probe, record evidence and land.

**Risks:** Shared checkout has concurrent Pane.h and provider edits; land only this change's hunks. Guest limits must come from runtime usage, never advertised API limits.

**Verify:** Focused C++ display tests, Python guest context and adapter tests, isolated GUI evidence; exact landed-tree build.

## Tests
`ctest -R ^contextmeter$`
`tests/test_guest_context_meter.py`
manual: docs/qa_evidence/2026-09-21-guest-context-meter/README.md
manual: docs/qa_evidence/2026-09-21-guest-context-meter/gui.txt

## Execution Summary
The context meter, tooltip, /context and remote percentage use runtime guest_context for Codex and Claude; unreported usage is explicitly unknown, and guest compaction no longer shows Relay's threshold. Fixed Claude's cumulative-versus-current-request accounting and model-window selection. Usage events refresh the meter live; model switches clear the measurement and ignore old-turn reports. Codex zero occupancy is retained. Targeted adapter/wiring/worker tests and contextmeter pass. The real GUI was driven under isolated Xvfb with both guest readings and unknown usage; evidence: docs/qa_evidence/2026-09-21-guest-context-meter/.

## Tasks

- [x] Trace Codex and Claude context data through adapters, worker and pane <!-- t:cr -->
- [x] Fix display, unknown state, Claude occupancy and stale/live update paths <!-- t:ey -->
- [x] Run targeted tests and isolated GUI probe; record evidence and land <!-- t:rq -->

## QA checklist
- [ ] In a Codex session, compare the meter and /context with its runtime usage report; no 128K fallback.
- [ ] In Claude, check a turn with several tool calls: context occupancy is the final request, not summed requests.
- [ ] Before first usage, after switching models and after a new conversation, no stale or fabricated percentage appears.
- [ ] Guest tooltips identify guest-managed compaction; returning to a native provider restores Relay's accounting.
