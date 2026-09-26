---
id: G7M2
type: work
status: done
labels: [bug, context, guests]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [35769d5c72e3], evidence: [], related: [C8WX], github: null}
---
# Restore guest context readings when reopening panes

## Issue
Guest context readings live only in the harness provider and are lost when Relay saves and reopens a guest conversation, leaving the meter at context unknown until a new usage event.

> relay context available fir guest accounts broke, it says context unknwon now
> — elliott · [session:69c8c6896f204c79833d47ec55a8be57](relay://session/69c8c6896f204c79833d47ec55a8be57) · 2026-09-25

## Done means
A guest pane reopened on the same saved guest session shows its last measured context before another turn completes.
An unrelated guest, account, model, or new conversation does not inherit that reading.
Focused Python tests cover save, resume, and invalid/stale measurements.

## Plan
**Goal:** Restore the last valid guest context reading when reopening its saved conversation.

**Findings:** `guest_harness_provider.attach().session_data` saves guest identity but omits `guest_context`; `resume_session` reopens the harness and `session_protocol` emits context afterward. `switch_model` clears the live reading.

**Steps:**
1. Save a validated reading with the guest session and model identity.
2. Restore it only when the saved identity matches the resumed provider; clear it for mismatches or failed resumes.
3. Add targeted save/resume tests and run the guest provider tests.

**Risks:** A stale reading attached to a different model is misleading. Validate and bind readings to model and session.

**Verify:** Focused Python tests of valid restoration and mismatch/invalid paths.

## Tests
`PYTHONPATH=backend:. python3 -m unittest tests.test_guest_harness_provider.AgentWiringTests tests.test_guest_context_meter`

### Check
Pass: 16 tests in 0.292 s in the shared tree; `land.py try guestcontext-g7m2 --verify-cmd` passed on exact tree 1e6b66a3f492.

## Execution Summary
Saved guest context and its model with the guest session. On resume, restored a validated reading only for the matching guest, account, session and model; stale or invalid readings clear. Landed in commit 35769d5c72e3. Existing session files without the new fields gain a reading after their next completed guest turn.
