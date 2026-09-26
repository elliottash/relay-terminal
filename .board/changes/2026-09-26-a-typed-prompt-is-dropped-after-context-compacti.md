---
id: 4XEC
type: work
status: inbox
labels: [bug, context, agent]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
source: relay-terminal pane (Claude Code guest, taken over from session cfad5dd0), 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [CP3M, BCJF], github: null}
---
# A typed prompt is dropped after context compaction: the agent compacts, then does nothing

## Issue
The user typed a command in an agent pane. Relay compacted the conversation and then never acted on the prompt: no reply and no tool calls. This was seen in the relay-terminal pane on 2026-09-26, where the session had been handed from Codex (openai/gpt-6-sol) to a Claude Code guest. Suspected cause: the pending prompt is lost, or never re-submitted, when compaction runs just before the turn. Not reproduced yet; the pane's transcript around the compaction should show whether the prompt reached the model.

> potential bug to file: I typed a command and it didn't run, it compacted and then didn't do anything
> — elliott · [session:f32c83e1970648d581feee4ae3447445](relay://session/f32c83e1970648d581feee4ae3447445) · 2026-09-26
