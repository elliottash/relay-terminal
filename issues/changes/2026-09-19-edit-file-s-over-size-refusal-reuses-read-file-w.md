---
id: SFP6
type: work
status: inbox
labels: [bug, agent-tools]
rank: zzzzzzzzi
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# edit_file's over-size refusal reuses read_file wording and gives no way out

## Issue
is this a bug? (in the tool call)

▸ edit src/BoardPane.cpp ✗ · File exceeds the 128 KiB preview/read …

## Decisions
- A deliberate, by-design refusal (a guard's `ValueError`, e.g. edit_file's over-size) keeps the ✗ but renders in dimmed/neutral ink, never red. Red stays strictly for "failed" per the colour table; amber stays for waiting-on-a-person. Backend marks guards' deliberate `ValueError`s as `refused: true` so the GUI can pick the ink. Owner, 2026-09-19: "i agree that non-actionable "errors" like this one should have the x but not in red. plan that". Split out to its own card for planning; #SFP6 keeps tracking the message wording.
