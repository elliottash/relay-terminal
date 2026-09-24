---
id: A7SC
type: work
status: needs-verification
labels: [feature, actions, ui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Codex in Relay, 2026-09-21
links: {plans: [], commits: [7c86a599c23ac8e81716ce3607fe9fd4d50f3e73], evidence: [docs/qa_evidence/2026-09-21-action-slash-commands/evidence.md], related: [], github: null}
---
# Show slash commands in Actions

## Issue
in the action menu, it should show the / commands as well, if the action has one.

## Plan
Add a shared mapping from action IDs to their existing slash commands. Show those commands in action rows and submenu headings, include them in search, and teach a slash command when a menu action has no keyboard shortcut. Include the newly added swap action in the menu. Verify rendered rows, search and activation with the settings-pane test and an isolated GUI.

## Execution Summary
Actions show their existing slash commands below the description while retaining keyboard shortcuts. Slash commands and aliases are searchable. Menu actions without a bound key teach their slash command through the normal limited hint registry. Added Swap models to the Actions catalog, displaying /swap and Alt+S. Existing saved aliases already display their /name in their description.
Committed as 7c86a599; the exact committed tree passed the C++ build gate.

## Tests
ctest:settings
manual: docs/qa_evidence/2026-09-21-action-slash-commands/evidence.md

## QA checklist
- [ ] Open Actions and search /swap: Swap models shows /swap and Alt+S.
- [ ] Search /clear: New chat displays /new and /clear, and executes the existing action.
- [ ] Actions without slash commands retain their existing appearance and keyboard shortcuts.
- [ ] Commands remain readable in a narrow Actions pane; hidden /todos is not advertised.
