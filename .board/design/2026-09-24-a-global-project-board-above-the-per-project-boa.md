---
id: V3R3
type: work
status: discussing
labels: [feature, board, switchboard, research]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 1c659dc9-43be-4638-a284-b180573a3b37
waiting_on: owner
priority: 2
rank: zzzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: decision, primary: person, also: [ai-text], human: required, criteria: The proposal describes a reusable Relay feature and its product decisions without requiring access to any personal tracker, sign_off: none, effort: medium, stakes: rework}
source: owner in Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [JN7X, 1QKM, GRT2], github: null}
---
# A Relay-wide Projects feature above per-project Boards

## Issue
Build a Relay feature for tracking projects across per-project Boards: mark critical projects,
set optional time budgets, and keep cross-project work in one place. Existing personal organizers
were offered as design examples, not as data to import or migrate. The owner clarified this on
2026-09-25. Private source links and details have been removed from this public card.

## Done means
The research and proposal describe a reusable Relay Projects feature with links to per-project
Boards, cross-project work, priority, optional budgets, privacy boundaries and a phased product
slice. The proposal does not depend on migrating one person's trackers or exposing their data.

## Discussion points
The corrected product proposal is [docs/GLOBAL-PROJECT-BOARD-RESEARCH.md](../../docs/GLOBAL-PROJECT-BOARD-RESEARCH.md), with three general research passes under [docs/research/global-project-board/](../../docs/research/global-project-board/). The 2026-09-25 correction supersedes the earlier personal migration questions in the thread. Product decisions are listed in section 6 of the proposal. The examples were audited for private content in this repository.

## Decisions
2026-09-25 — Owner: “i want to build a feature for relay, not build my own board. those were just examples of organizing structures.” The proposal is for all Relay users; personal imports and migration are outside this card.
