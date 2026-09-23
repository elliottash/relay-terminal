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
links: {plans: [], commits: [645b9fdf46e6fb7ae2e32216459ef95db0550826, f13bf691220e2a89f5ffb54ab6b354e66f06cda4], evidence: [docs/qa_evidence/2026-09-23-w1b8/README.md], related: [], github: null}
---
# Board Run starts a default guest immediately

## Issue
bug -- when you execute from switchboard, but thge mdeol is a guest, it doesnt start immediately, i had to enter a command. fix that where it will start immediately
it also didnt add the card code into the pane heading like it used to, not sure if that regressed

## Done means
Choosing Run on a Board card opens a pane whose ranked default is a guest harness and starts its agent on the card without any extra command. The card’s `#ID` appears beside the pane title from handoff through any startup or queue delay and while the card turn runs; clicking it opens the card. A plain new guest pane still waits for its first prompt. Failure is a Run pane that stays idle or lacks the card chip while waiting.

## Execution Summary
`src/Pane.h`: Board Run starts a deferred default guest harness when the card task arrives, including when presets arrive after the task (`645b9fdf`). The pane now keeps the card ID beside the title while that task waits through guest startup or the agent queue; the running turn takes over the same chip when it begins (`f13bf691`). An ordinary new guest pane remains deferred. Both commits passed Relay’s isolated exact-tree build.

## Tests
`manual: docs/qa_evidence/2026-09-23-w1b8/README.md`
`python3 -m unittest tests.test_verify_runner_source` — passed, 3 tests after the follow-up change.
`scripts/land.py commit w1b8-chip ...` — isolated exact-tree `relay` build passed for commit `f13bf691`.
`scripts/land.py commit w1b8-run ...` — isolated exact-tree `relay` build passed for commit `645b9fdf`.
`git diff --check -- src/Pane.h` — passed.
