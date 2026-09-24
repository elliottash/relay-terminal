---
id: 9A8B
type: work
status: needs-verification
labels: [bug, board, ui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: a7c2aca3-20ce-4af7-82ac-f5d320e3c2c3
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-24'
verify: {artifact: visual, primary: probe, also: [script], human: optional, criteria: The empty Board prompt has no dead band and a live turn remains readable., sign_off: none, effort: low}
source: Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-board-empty-console/board-empty-console.png], related: [], github: null}
---
# Collapse empty Board agent transcript above prompt

## Issue
bug: there is a bug in the board where thre is a big block of unused space above the agent prompt. can you see that in this screenshot and fix it? @/home/elliott/.cache/RelayTerminal/relay/images/relay-paste-20260924-095519.png

## Done means
On a Board with no agent output, the action row and prompt sit directly below the card list, without a large empty transcript panel.
When the agent prints output, the transcript opens with enough room for the turn and queue strip.
The same behavior holds on a card page.

## Plan
Goal: remove unused vertical space above the Board agent prompt.
Findings: `BoardView::updateConsoleHeight` in `src/BoardPane.cpp` forces both consoles to at least 20 text lines. `Pane::setTranscriptHiddenUntilUsed` already hides an empty transcript.
Steps:
1. Enable empty transcript hiding for Board consoles and let their layout shrink before output.
2. Preserve a readable transcript height after output starts.
3. Run the targeted Board pane test and capture the Board layout in an isolated profile.
Risks: First output must reopen the transcript without clipping the turn.
Verify: targeted Board pane test plus an isolated live screenshot.

## Execution Summary
The Board and card consoles now hide their terminal host until it receives output. The 20-line minimum applies only while that host is visible; an empty console follows the prompt's size. Switching to an empty card surface also hides its transcript.

![Board list reaches the agent prompt without an empty transcript band](docs/qa_evidence/2026-09-24-board-empty-console/board-empty-console.png)

## Tests
`QT_QPA_PLATFORM=offscreen build-fast/relay-boardpane-tests` — 15 passed, including `emptyAgentTranscriptDoesNotReserveConversationHeight` (compact before output, expanded after output).
`/tmp/relay-board-gap-drive.sh build-fast/relay … sb` — isolated Xvfb visual capture; the Board has no empty transcript band in `docs/qa_evidence/2026-09-24-board-empty-console/board-empty-console.png`. The old drive's OCR labels and shortcut were stale, so its text assertions were not used as a gate.
`python3 scripts/relay-board.py check` — project-wide check reports 2 pre-existing errors and 1280 warnings on other cards; no finding names #9A8B.
