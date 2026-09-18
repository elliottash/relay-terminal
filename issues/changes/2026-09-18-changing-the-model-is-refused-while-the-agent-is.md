---
id: 3ES1
type: work
status: inbox
labels: [bug]
rank: zzzzzr
created: '2026-09-18'
source: issues/bug_intake.txt, 2026-09-18
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Changing the model is refused while the agent is working

## Issue
i couldnt change the model while the agent was working. that should be allowed and it should cross over just like warp.

Note from filing: the protocol refuses `set_model` while a turn runs (`agent_busy`, docs/AGENT-SESSIONS-PROTOCOL.md section 2), so this is a designed refusal the owner wants lifted: the switch should be accepted mid-turn and take effect as the turn carries on.
