---
id: D3WW
type: work
status: done
labels: [bug, queue, sessions]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [tests/consolemode_test.cpp], related: [], github: null}
---
# Restore pending pane queue after exit

## Issue
bug: issue with restore after exit -- if there is a sitting text in the queue , it is deleted. so that should be saved and restored

## Done means
Pending pane queue text survives exit and relaunch in the same order and remains visible in the queue.
Restored entries wait for an explicit resume, so reopening Relay does not execute old shell commands or prompts.
An empty or older saved layout still restores normally.

## Plan
Goal: retain pending pane queue entries through window restore.
Findings: `src/Pane.h` owns the pending `m_entries`; `src/RelayWindow.h::serializeNode` writes pane state and `Pane::initRestore` reads it. The queue is absent from that state.
Steps: (1) serialize safe pending entry fields into each pane node; (2) restore ordered entries paused and refresh the strip; (3) cover serialization and restore with a focused test.
Risks: avoid replaying a command without a deliberate resume and avoid restoring transient remote-login or Relay-generated work without its live context.
Verify: targeted C++ test and Relay build.

## Execution Summary
Saved pending pane-side queue entries in each pane's window record, restored their text and order, and held the restored queue paused until Resume. Queue changes now schedule a layout save. Relay-generated follow-ups are excluded because their live turn context ends on exit.

## Tests
`ctest --test-dir build-fast -R '^consolemode$' --output-on-failure`
`scripts/relay-build --fast --target relay`
