---
id: H8SS
type: work
status: needs-verification
labels: [feature, ssh, actions]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Codex in Relay, 2026-09-21
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-ssh-host-picker/evidence.md], related: [], github: null}
---
# Open SSH hosts in a modal instead of expanding Actions

## Issue
third, dont show all the ssh hosts in the actions. you could have a connect to ssh action, but it should then open the list in a modal for example

## Plan
Replace the SSH submenu with one Connect to SSH action. Open saved/recent hosts in a searchable modal, allow entering a new host, preserve the existing connection path and typed Actions shortcut, and verify the list and cancellation under Xvfb.

## Execution Summary
Actions now contains one Connect to SSH entry instead of expanded host rows. It opens a searchable modal of saved and recent hosts, with validated new-host entry and Connect/Cancel buttons. Selecting a host retains the existing new-tab SSH command path. Explicit ssh host/user@host searches in Actions remain available; the shortcut hint and SSH docs name the new action.

## Tests
ctest:sshconfig
manual: docs/qa_evidence/2026-09-21-ssh-host-picker/evidence.md

## QA checklist
- [ ] Actions shows a single Connect to SSH entry and no saved-host list.
- [ ] Open it: saved/recent hosts appear in a modal; filtering selects the matching host.
- [ ] Enter a new user@host: a Connect row appears. Invalid text with spaces leaves Connect disabled.
- [ ] Escape/Cancel closes the picker without connecting; accepting a host opens its SSH session in a new tab.
