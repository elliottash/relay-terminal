---
id: HRF6
type: work
status: executing
labels: [feature, land, scratch]
assignee: agent
implemented_by: glm/glm-5.3
session: 97149268-97e8-4cc9-bf13-249118806781
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: 'pane 1, 2026-09-25, follow-up to #BHJZ'
links: {plans: [], commits: [aaecaa7c3fda], evidence: [], related: [BHJZ, DVV2], github: null}
---
# land.py's default root moves to a Relay-owned location ($XDG_STATE_HOME/relay/land), not /tmp/claude-<uid>/land

## Issue
Follow-up to #BHJZ: the shared land root should be a Relay-specific path instead of the Claude-named /tmp/claude-1000/land. The default becomes $XDG_STATE_HOME/relay/land (~/.local/state/relay/land), with fallbacks so sessions begun under either old claude root can still finish.

> yes, i want a relay-specific location, not claude.
> — elliott · [session:9698842186c7475288ae58fe80f77b2e](relay://session/9698842186c7475288ae58fe80f77b2e) · 2026-09-25

## Tests
`python3 -m unittest tests.test_land` — 91 tests, all pass. `SharedRoot` covers the new default root, the `$XDG_STATE_HOME` override, TMPDIR-independence, adoption from both old roots (shared and per-TMPDIR), newer-begin precedence and idle verify-slot reclaim. Evidence: `docs/qa_evidence/2026-09-25-hrf6-relay-land-root/` (also shows the live migration: 26 sessions adopted, the old root holding only one in-flight verify build, which drains 30 minutes after it goes idle).
