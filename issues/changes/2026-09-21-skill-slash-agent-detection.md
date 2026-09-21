---
id: D7AV
type: work
status: executing
labels: [bug, input, skills]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [R9G7], github: null}
---
# Detect skill slash commands as agent input

## Issue
bug -- if i do /deliver, it should automatically detect its an agent prompt (check if there are other agent slash commands like that for autodetection)

## Plan
Recognize the existing SKILL verdict in Pane::refreshDestinationColor. This covers every discovered skill, including /deliver and /clean-commit; built-in commands and /skill already produce COMMAND. Preserve existing dispatch and explicit mode precedence. Build and check the destination indicator in an isolated GUI.
