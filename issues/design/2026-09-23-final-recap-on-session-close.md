---
id: RCP9
type: work
status: needs-verification
labels: [feature, sessions, design]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [a90f5e423aef80a8a6a4e0f3ddf01b8193d1c2ae, 809855f5962a4ac7a637d2348eaa62bf39c7a7b0], evidence: [docs/qa_evidence/2026-09-23-final-recap-close/], related: [PV7W], github: null}
---
# Generate a final recap when a session closes

## Issue
we might need to discuss, that those should always be generated for closed sessions so they get saved for here

## Discussion points
Relay currently generates a saved summary after the first assistant reply and refreshes it after five more turns or a compaction. Closing a pane stops its worker, so guaranteeing a final recap needs a separate background path or a deliberate wait on close. This would make a model call for each eligible session and may affect close latency or usage.

## Done means
- Closing a Relay agent session with an assistant reply and a missing or stale saved recap schedules one final model call without delaying pane close.
- A successful final recap is saved in the session metadata and conversation index so the Sessions table shows it. An unusable reply or unavailable model leaves the previous recap intact.
- The job survives its pane worker exiting and does not overwrite a newer recap or a session that was resumed and advanced.
- Focused tests cover fresh, missing, stale, failure, and resume races.

## Decisions
- Owner: “yes, generate on close.” A missing or stale saved recap should be generated automatically after closing a session.

## Plan
**Goal.** Save a final recap when an agent session's pane closes.

**Findings.** `backend/worker.py` handles shutdown; `SessionCommands.maybe_summary` runs on a daemon thread that dies with that worker. `SessionStore.note_summary` already writes metadata and the index without altering the session file. `Agent.summary_turn` records both successful and failed attempts, so freshness needs a separate last successful turn.

**Steps.** 1. Determine close-time eligibility from the last successful summary and final saved turn. 2. Launch a standalone helper from worker shutdown with the chosen chores provider config over a private pipe. 3. Have the helper generate and conditionally save the recap after checking for newer session work. 4. Add focused tests for eligibility, persistence, failures, and races; document the behavior.

**Risks.** The model may be unavailable or the helper may fail to start; preserve the prior recap and log the failure. A resumed session may advance while the helper runs; reject that stale result.

**Verify.** Run the summary, worker protocol, and session tests plus a helper subprocess test; build through `scripts/relay-build` as needed.

## Execution Summary
The worker now starts a detached close job for an agent session with an assistant reply when its saved recap does not cover the last turn. The job receives its chores provider config on stdin, reads the saved session, and updates metadata plus the index. Successful summary turns are tracked separately from failed refresh attempts. On resume, a later metadata recap is adopted; an older job result is discarded if the session advanced or a newer recap landed. The behavior and test evidence are in `docs/qa_evidence/2026-09-23-final-recap-close/notes.md`.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_final_summary tests.test_summaries tests.test_titles tests.test_sessions tests.test_conv_index tests.test_session_protocol` — 258 passed.
- `scripts/relay-build --target relay` — passed.
- `python3 -m compileall -q backend/relay_core/final_summary.py backend/worker.py` — passed.
- Manual evidence: `docs/qa_evidence/2026-09-23-final-recap-close/notes.md`.
