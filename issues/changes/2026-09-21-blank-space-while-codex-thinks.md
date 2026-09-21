---
id: B7SP
type: work
status: discussing
labels: [bug, terminal, guest]
assignee: codex
waiting_on: owner
rank: m7s
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Blank space at the bottom while Codex thinks

## Issue
i observed something weird though, that there was like reserved black space at the bottom of the pane, when codex was thinking -- even when no text was printing

## Planning notes
The guest adapter emits thinking events only for nonempty text. That alone does not rule out blank space caused by widget sizing or terminal fold layout. Determine whether the reported gap is inside the terminal viewport or between it and the composer before changing geometry.
