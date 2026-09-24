---
id: BG2Y
type: work
status: needs-verification
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: bf8aa1b7-4720-412c-a295-89eceb8d3bae
rank: zzzzzzzzzzzzzzzzzr
created: '2026-09-23'
links: {plans: [], commits: [39af7325be6f369b622a07076b0d99117df81ebc], evidence: [docs/qa_evidence/2026-09-23-ctrl-enter-guest/], related: [SXF1], github: null}
---
# ctrl + enter should activate guest agents, like typing a first entry

## Issue
ctrl + enter should activate guest agents, like typing a first entry

## Done means
On a fresh guest-harness pane that says it starts on the first prompt, Ctrl+Enter with text starts the selected guest and sends that exact text as its first turn, as Enter does.
Ctrl+Enter with an empty box starts the same guest and sends `Continue` as its first turn. The first request waits for `configured`, then runs once.
A busy agent still uses Ctrl+Enter with text to interrupt and send now; an empty busy box sends nothing. Failure is a provider error, a discarded prompt, an incorrect provider, or a duplicate first turn.

## Plan
**Goal.** Ctrl+Enter starts a deferred guest on its first turn, with typed text or `Continue` from an empty box.

**Findings.** `src/Pane.h::interruptAgentWithPrompt()` routes typed text through `requestRoute(true, "agent")`, which already reaches `submitAgent()` and starts the deferred guest. For an empty box it calls `continueTurn()`, whose early `!m_configured` guard blocks that same deferred start. `submitAgent()` already queues the first prompt and `src/PaneEvents.cpp` pumps it after `configured`.

**Steps.** Remove the early configured guard from `continueTurn()` so its ordinary `submitAgent("Continue")` path can start a deferred guest; keep provider-missing handling in `submitAgent()`. Add a pane regression case for both first Ctrl+Enter forms, asserting selected guest `configure`, one queued prompt, and one `ask` after `configured`.

**Risks.** Keep the busy interrupt path and password-prompt exception intact. `src/Pane.h` is shared; land a minimal diff with `scripts/land.py`.

**Verify.** Build `relay-consolemode-tests` with `scripts/relay-build` and run `ctest -R '^(consolemode|continueturn)$'`. The pane regression uses intercepted worker messages, so it starts no real guest.

## Execution Summary
Removed the early `m_configured` rejection in `Pane::continueTurn()`. Ctrl+Enter on an idle empty box now reaches `submitAgent("Continue")`, which starts the selected deferred guest and queues that first prompt. Typed Ctrl+Enter already used the same submission path; the new pane regression proves both forms send one `ask` after `configured`.

Evidence: `docs/qa_evidence/2026-09-23-ctrl-enter-guest/README.md`.

## Tests
`ctest --test-dir build -R '^consolemode$' --output-on-failure`
`ctest --test-dir build -R '^continueturn$' --output-on-failure`
`manual: docs/qa_evidence/2026-09-23-ctrl-enter-guest/`
