---
id: DV5Y
type: work
status: needs-verification
labels: [feature, workflow, land]
parent: 3MH4
discovered_from: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'Approved #3MH4 implementation workstream, 2026-09-26'
assignee: codex
verify: {artifact: system, primary: script, also: [probe, ai-visual], human: optional, criteria: "Workspace and queue state are visible; Relay (main) launches the installed release", effort: high}
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-b3-workspace-live/01-live.png], related: [], github: null}
---
# B3: Workspace visibility and main launcher in Relay

## Issue
Show workspaces, jobs and recovery in Live; surface queue state and launch the runnable main release.

## Done means
A queue project prepares one leased execution tree before any pane shell or worker starts; failures keep the pane stopped and show a retryable reason. Legacy panes keep their existing launch path.
Live rows show the execution branch, submitted job stage/reason, retained work and runnable-main lag. A Relay (main) action launches only the installed immutable release through an argument list.
Targeted GUI tests and an exact-tree land.py try pass; an isolated-profile capture shows the new UI.

## Plan
1. Trace pane construction, worker and shell starts, Live row data and launcher actions.
2. Add asynchronous workspace preparation and fail-closed restarts using the installed backend helper.
3. Wire queue/main status into Live and add the installed-main launcher.
4. Test the changed flows, capture the UI and land only #DV5Y hunks.

## Execution Summary
The pane prepares a leased tree asynchronously before worker, shell or guest startup, preserves the canonical project for Board actions, and refuses startup with a retry banner when allocation fails. Live reads the integration snapshot and durable main-moved events; it shows branch, job stage/reason, unlanded retained trees and runnable-main lag. Relay (main) invokes the installed release through the service CLI argument list.

![Live tab showing workspace, queued job, retained work and main lag](docs/qa_evidence/2026-09-25-b3-workspace-live/01-live.png)

## Tests
### Check
- `python3 scripts/land.py try b3-dv5y --tests '^boardpane$'` with only #DV5Y hunks: Relay build and BoardPane test passed.
- `python3 scripts/land.py try b3-dv5y --verify-cmd ...` with only #DV5Y hunks: focused BoardPane build and test passed after snapshot/event wiring.
- `manual: docs/qa_evidence/2026-09-25-b3-workspace-live/01-live.png`: isolated test profile, Qt offscreen, 1000 × 700.
