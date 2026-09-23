---
id: W1B8
type: work
status: needs-verification
labels: [bug, switchboard, guests]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: ae66326a-9944-4f14-acd1-324eacb1d56e
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-w1b8/README.md], related: [], github: null}
---
# Board Run starts a default guest immediately

## Issue
bug -- when you execute from switchboard, but thge mdeol is a guest, it doesnt start immediately, i had to enter a command. fix that where it will start immediately

## Done means
Choosing Run on a Board card opens a pane whose ranked default is a guest harness and starts its agent on the card without any extra command. The card remains attached to that first turn. A plain new guest pane still waits for its first prompt. Failure is a Run pane that stays at “starts on your first prompt” until the user types.

## Execution Summary
`src/Pane.h`: Board Run now starts a deferred default guest harness when the card task is handed to the pane, including when presets arrive after the task. The existing `configured` event sends the parked task with its card attachment. An ordinary new guest pane remains deferred. Full compilation is currently blocked by unrelated in-progress background task changes in the shared checkout (`Pane::agentReady` and `WindowManager` declarations missing).

## Tests
`python3 -m unittest tests.test_verify_runner_source` — passed, 3 tests.
`scripts/relay-build --target relay` — blocked by unrelated shared-checkout background task changes: `Pane::agentReady` missing and `WindowManager::refreshBackgroundTasks` / `backgroundCount` declarations missing.
`git diff --check -- src/Pane.h` — passed.
