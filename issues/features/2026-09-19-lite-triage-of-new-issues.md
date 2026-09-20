---
id: 5KMQ
type: work
status: ready
labels: [feature, switchboard]
priority: 1
rank: zzzzzzzzzzzw
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# lite triage of new issues

## Issue
when you enter a new issue, gemini flash lite assigns it to features / bugs / etc.

Priority one is duplicates and related: as you type the first issue text — and right when you press Enter — the composer shows likely-duplicate cards and a related-cards prefill. Tab assignment and the other lite chores can ride the same pass afterwards.

## Decisions
- **2026-09-20 — scope:** duplicates and related are the most important lite chore, and they surface at entry time: live while typing the first issue text and right on Enter, before the card is saved. Tab/labels/title etc. are secondary and can follow in the same pass.
