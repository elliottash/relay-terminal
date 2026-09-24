---
id: A9QR
type: work
status: needs-verification
labels: [feature, actions]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Codex in Relay, 2026-09-21
links: {plans: [], commits: [b44a76049e83739c55a5e942ecb7e132e81db6ba], evidence: [docs/qa_evidence/2026-09-21-actions-organization/], related: [C8KM], github: null}
---
# Organize Actions around common tasks

## Issue
after that, just take a fresh look at the actions menu to make sure the ordering / organization is intuitive

## Plan
Review the catalog and rendered menu. Put common agent and conversation actions first, group model setup, project tools, remote connections and appearance separately, keep maintenance last, and remove the duplicate File explorer action. Preserve callbacks and section search. Build, run SettingsPane tests and inspect the GUI.

## Execution Summary
Reviewed and regrouped Actions around tasks: Agent, Conversations, Models, Terminal, Panes and tabs, Files and projects, Remote and sharing, Appearance, Shortcuts, Relay. Common commands lead each group; maintenance ends the list. Removed duplicate File explorer entry. Existing callbacks and section navigation are retained. GUI evidence: docs/qa_evidence/2026-09-21-actions-organization/.

## Tests
ctest:settings
manual: docs/qa_evidence/2026-09-21-actions-organization/evidence.md

## QA checklist
- [ ] Common commands lead Agent and Panes and tabs.
- [ ] Model, project, remote and appearance commands have coherent groups.
- [ ] Search a section and select it; search clears and scrolls to that group.
- [ ] File explorer appears once; SSH and Recently closed remain modal launchers.
