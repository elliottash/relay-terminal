---
id: XDZP
type: work
status: needs-verification
labels: [feature, subagents]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 5b580547-ed17-49a1-9def-39ae7c42a1c3
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [person], human: optional, criteria: 'Open a running subagent''s pane: a violet "Relaying for main agent · <action>… · N s" line sits above its message box and goes away when the agent finishes.', sign_off: none, effort: low}
source: Claude Code guest pane, 2026-09-24
links: {plans: [], commits: [], evidence: [tests/subagents_test.cpp], related: [], github: null}
---
# Subagent pane shows "Relaying for main agent · …" above its message box while it works

## Issue
subagents need a relaying... notification, 

like, when you click on a subagent pane, it should show relaying... while its working above the prompt. but we could say subrouting... instead of relaying... as a cute alternative. 

what do you think about that? would another word be better?

relaying for parent sounds good

## Decisions
- Owner, 2026-09-24, choosing among Subrouting, Handing off and "Relaying for <parent>": "relaying for parent sounds good". The parent is named "main agent", as the subagent pane's own "← main agent" button already calls it.

## Execution Summary
`SubagentTranscriptView` (src/SubagentTranscript.{h,cpp}) gets a `subagentBusyLine` label over its message box, in the agent's violet, where the main pane has its "Relaying · …" line. While the row is `waiting` or `running` it reads "Relaying for main agent · <action>… · N s". The action is the running tool call's gerund ("reading src/Pane.h"), "thinking" between calls, or "waiting to start" while queued. A 1 s timer advances the seconds from the row's reported elapsed time. It refreshes after every `setRow`, every transcript event and `setEnded`. It is hidden once the agent is done, failed, stopped or restored from before a restart. No "Esc stops": Esc in this box goes back to the main agent, so the tooltip says the running-agents row stops it.

### Check
`scripts/relay-build --target relay-subagents-tests && QT_QPA_PLATFORM=offscreen ./build/relay-subagents-tests`: 34 passed, 0 failed, including the new `transcriptBusyLineSaysRelayingForMainAgent` (waiting → running/thinking → reading src/Pane.h → thinking → done hides the line).
