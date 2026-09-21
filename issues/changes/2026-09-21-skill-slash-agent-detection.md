---
id: D7AV
type: work
status: needs-verification
labels: [bug, input, skills]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [98c308c1298e32f85c6e7d39dcb876cd26f058ea], evidence: [docs/qa_evidence/2026-09-21-skill-slash-agent-detection/], related: [R9G7], github: null}
---
# Detect skill slash commands as agent input

## Issue
bug -- if i do /deliver, it should automatically detect its an agent prompt (check if there are other agent slash commands like that for autodetection)

## Plan
Recognize the existing SKILL verdict in Pane::refreshDestinationColor. This covers every discovered skill, including /deliver and /clean-commit; built-in commands and /skill already produce COMMAND. Preserve existing dispatch and explicit mode precedence. Build and check the destination indicator in an isolated GUI.

## Execution Summary
Added SKILL to the agent destination verdicts in Pane::refreshDestinationColor. All discovered /name skills now update the auto-mode indicator; built-ins and /skill already use COMMAND. Existing skill dispatch and shortcut hints are unchanged. Commit 98c308c1298e32f85c6e7d39dcb876cd26f058ea; exact committed tree built successfully.

## Tests
`ctest -R slash`
`ctest -R input`
manual: docs/qa_evidence/2026-09-21-skill-slash-agent-detection/

## QA checklist
- [ ] In Auto mode type ls, replace it with /deliver, and confirm the indicator changes to agent (purple).
- [ ] Repeat with another installed skill, with /skill deliver, and with /compact.
- [ ] Press Enter on /deliver plus a request and confirm the agent receives the skill.
