---
id: WM4K
type: work
status: needs-verification
labels: [feature, actions, sessions]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 6323395d-d98b-4e09-844a-7b6747db6b0a
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-23'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-palette-WM4K/palette-results.png], related: [MAGP], github: null}
---
# Search active conversations from the Actions palette

## Issue
make the action pallette search box also search active convos, matching on the header / summary first then full text content

## Done means
Typing in Actions shows open conversations whose title or summary matches, ahead of conversations matched only in their text. Selecting a conversation result focuses its open pane, including one in another tab or window. Nonmatching and closed conversations stay out; action search still works.

## Plan
Goal: Add active conversation results to the Actions search.
Findings: `ActionPalette` ranks local action rows; `ConversationIndex.search` already ranks title, summary, then body matches; `Pane` owns the worker protocol and `RelayWindow` knows open panes.
Steps:
1. Allow indexed search to constrain results to explicit active session IDs.
2. Send palette queries through a pane worker, display matching conversations above action matches, and focus their live panes on selection.
3. Add focused index and palette tests, build the affected targets, and capture UI evidence.
Risks: Index results can lag an unsaved turn; query responses need a stale-result guard. No user decision needed.
Verify: focused Python index tests, `actionpalette` CTest, and visual check.

## Execution Summary
The Actions palette now sends debounced searches of currently open session IDs through the conversation index. It displays up to 12 conversation results ahead of action matches, sorted by title, summary, then transcript match, and selecting one reveals its live pane across tabs or windows. Stale query replies are ignored. The result row shows the summary or a matched transcript line.

![Conversation result above an action match](docs/qa_evidence/2026-09-23-palette-WM4K/palette-results.png)

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_conv_index`
`ctest --test-dir build -R '^actionpalette$' --output-on-failure`
`scripts/relay-build --target relay-actionpalette-tests relay`
manual: docs/qa_evidence/2026-09-23-palette-WM4K/palette-results.png
