---
id: MH58
type: work
status: needs-qa-llm
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (GUI E1 subagent), 2026-09-17
rank: kb
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: owner decisions in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`; contract `docs/AGENT-SESSIONS-PROTOCOL.md`
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Instruction files onboarding and relay.md synthesis

## Behavior as implemented

- On the first configured agent pane (QSettings `instructions/onboarded` false) Relay sends `scan_instructions` and shows a dialog: files grouped by tool (checkboxes, scope, size), "Also include instruction files found in each project automatically", and "Create a global relay.md from the selected files" with the target path. Not now and Save both mark onboarding done.
- Save stores `instructions/files` and `instructions/project_auto`; every `configure` sends `instructions {files, project_auto}`. The change applies at once when the conversation is empty, otherwise at the next New chat.
- With "Create a global relay.md", Relay sends `synthesize_instructions {files, target: $XDG_CONFIG_HOME/relay/relay.md}`; on `instructions_synthesized` the selection becomes relay.md alone and relay.md opens in an editable pane (Save/Reload, no Execute buttons).
- Reachable later from Actions › Agent options › Instructions… and /instructions; an existing relay.md is listed first.

## Implementer check (not a QA verdict)

Under Xvfb (`docs/qa_evidence/2026-09-17-agent-onboarding/`): the first launch showed the project AGENTS.md and ~/.warp/WARP.md (`implementer-first-launch-dialog.png`); selecting AGENTS.md and Save wrote `instructions/files` and `instructions/project_auto` to the settings file. /instructions with synthesis (`implementer-instructions-synthesize-checked.png`) created `config/relay/relay.md` ("# Relay instructions / ## Project rules / - Use 4-space indentation.") and opened it (`implementer-relay-md-created-editable-pane.png`).

## Deviations

- The directive said "offer to open" relay.md; Relay opens it directly in an editable pane beside the agent pane (closing it is one click).

## QA checklist

1. Fresh config with CLAUDE.md, AGENTS.md and WARP.md present: the dialog groups them by tool.
2. Select some, Save; ask the agent what rules it follows.
3. Turn off project auto-include; a project AGENTS.md is no longer followed after New chat.
4. Create relay.md from several files; review and edit it; New chat uses it.
5. Restart: the dialog does not appear again.
