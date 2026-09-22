---
id: QAN1
type: work
status: discussing
labels: [bug, queue, questions]
assignee: codex
waiting_on: owner
rank: mqan1
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [], related: [QFF1, MQ9C], github: null}
---
# Question answers wait behind queued prompts

## Issue
another issue -- i had prompts queued and the agent asked me questions. i answered the questions, but it queued them after my prompts, so the question answers didnt work

## Done means
- Answers reach the question that requested them without waiting behind ordinary queued prompts.
- Existing queued prompts retain their order and contents.
- Normal new prompts still queue when the agent is busy.

## Planning notes
`Pane::requestRoute` already answers a structured `m_ask` directly, before routing or queueing. `recordAnswer` emits `question_answer` after the final answer in a group. Plain prose questions do not establish `m_ask`; their replies can reach ordinary busy-agent queueing. `interruptAgentWithPrompt` also bypasses requestRoute while busy, so the submission gesture matters for structured questions. Need the observed question presentation and submission gesture to reproduce the reported path before changing it.
