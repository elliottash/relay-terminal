---
id: H6VQ
type: work
status: executing
labels: [bug, agent-app-control, sessions, switchboard]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'owner report, 2026-09-20 23:22, relayed to a Claude Code session'
links: {plans: [], commits: [], evidence: [], related: [FEJQ], github: null}
---
# The Sessions helper could not open a conversation, and nothing answered its app_open

## Issue
sessions helper didn't do anything when I asked to open a group of previous sessions in new panes.

Later, after the first fix:

cool, it partly worked. can you make that more formalized that it can do that? but it also needs
to reply in text that it is doing it.

message queue isn't working in the sessions helper, i can't interrupt.

## Decisions
- "can you make that more formalized that it can do that?" — opening a conversation is a
  documented ability of the Sessions helper: its own paragraph in the pane's brief, in the
  `agent.app` prompt section and in protocol §30.4, not a line in a tool schema.
- "it also needs to reply in text that it is doing it" — the helper states what it is doing in
  one line before or alongside any app action, and a turn that acts and answers with nothing has
  a summary appended from the tool results, because the panel draws text and never tool calls.
- "in new panes" means `new_pane: true`; that is also the default, since loading a conversation
  into the pane the person is sitting in takes that pane's own conversation away.
