---
id: CSMK
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Alt+I opens the conversation-info pane in every preset, and a session row whose conversation is in the recently-closed list says "closed N min ago" and offers "Reopen where it was"'
source: 'owner, 2026-09-18: "the (i) view hotkey could be alt+i or alt+1?" and "session rows \"closed mins ago\" should be added"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/'], related: [Y63Z, R6J0, SM4R, RC7Z], github: null}
---
# Alt+I opens the ⓘ view, and a session row says when it was closed

## Issue

the (i) view hotkey could be alt+i or alt+1?

session rows "closed mins ago" should be added

## Change

**Alt+I.** `agent.info` — the conversation-info pane, also `/status` and `/info` — was registered
with no key. It is Alt+I now: the mnemonic, and free in every preset (Relay's own Alt keys are
Alt+A subagents, Alt+F flash, Alt+R reasoning; Konsole's Ctrl+Alt+I and VS Code's
Ctrl+Shift+Alt+I are different combinations). The ⓘ button's shortcut hint teaches Alt+I instead of
`/status`, the Actions row and the pane's own footer line say Alt+I, and docs/ARCHITECTURE.md's
shortcut table lists it.

**"closed N min ago".** A row in the session manager whose conversation is in the recently-closed
list (#RC7Z) carries a tag saying so, and a "Reopen where it was" button beside Resume, which goes
through `WindowManager::restoreClosed()` — the same path Ctrl+Shift+Z uses, so a conversation
already open in a live pane can never get a second worker. The tag is a clock, not a stamp: a timer
re-reads it while anything is in the closed list, and stops when the list empties, so a row that
said "closed just now" catches up without the list changing. When the item is reopened or pushed off
the end of the 25, the tag and the button go.

## Evidence

`docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/implementer-info-relay-dark-alt-i.png`
— Alt+I opening the info pane beside its terminal, with the footer teaching Alt+I.
`tests/conversations_test.cpp::theClosedTagAgesAndThenGoes` drives the clock: a row closed 59 s ago
reads "closed just now", says "closed 1 min ago" once the minute turns without any new data, and
loses both tag and button when the closed list drops it.

## QA checklist

1. Alt+I in an agent pane opens the info pane beside it; Esc closes it; Alt+I again reopens it.
2. `/status` and `/info` still open the same pane, and the ⓘ button's hint now teaches Alt+I.
3. Actions › "Conversation info" says Alt+I, and a rebinding in Options › Keyboard follows through
   to the tooltip and the hint.
4. Alt+I is free in each shipped preset (relay, warp, vscode, konsole) — nothing else answers it.
5. Close a pane holding a conversation, open the session manager: that row says "closed N min ago"
   and offers "Reopen where it was".
6. Leave the pane open across a minute boundary: the tag ages by itself.
7. "Reopen where it was" brings the pane back where it was, and the tag goes.
8. Reopening a conversation that is already open in another pane focuses it rather than starting a
   second worker.
9. `ctest -R conversations` passes.
