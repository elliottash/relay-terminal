---
id: 7Z08
type: work
status: needs-verification
labels: [feature, keyboard, queue]
assignee: agent
implemented_by: openai/gpt-6-astra via codex
session: ad1a0484-9eab-4e96-b0d7-b17670fab17f
rank: zzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: Up and Esc restore the sent prompt in an empty composer while stopping its agent., sign_off: none, effort: medium}
links: {plans: [], commits: [044d7b13812e26cd07a10199e89dbb982e6b3a54], evidence: [docs/qa_evidence/2026-09-24-recall-7Z08/], related: [QRC1, XCXD, 5P0Q], github: null}
---
# Up and Esc stop the active agent message and restore it for editing

## Issue
in claude. if you press enter and then realize you made a mistake and press up, it cancels and you can edit.  can we do soemthing like that?
and we can do esc as well for that like codex

check the timing that claude and codex use so it feels natural

## Done means
Up from an empty composer stops the active agent turn and restores its full prompt for editing when no queued item takes precedence.
Esc stops the active agent and restores its prompt into an empty composer; existing drafts are preserved.
Restored text remains unsent until Enter; queued recall, idle history, and popup dismissal retain their behavior.

## Plan
Goal: one-key cancellation and immediate editable prompt restoration.
Findings: Pane.h owns composer key routing and prompt bookkeeping; PaneEvents.cpp consumes worker lifecycle events. Queue recall already exists (#QRC1); preserve its precedence. Current Claude docs describe queue-state recall, not a fixed time window. Codex originally restored only output-free interrupted prompts (c0ea566), then removed active-prompt restoration (70a0b1e). Neither establishes a milliseconds grace period to copy.
Steps: track the locally submitted active prompt across dispatch/start/finish; restore synchronously on Up or Esc while cancelling through the existing surface-aware stop; add focused real-Pane tests and screenshot evidence.
Risks: preserve unsent drafts, queued recall, popup dismissal, and other sessions' edits. Cancellation preserves already completed actions and transcript.
Verify: immediate-before-start, running, finished, late response, repeat keys, edited resubmission, and queue precedence in consolemode tests under an isolated Xvfb profile.

## Execution Summary
Implemented Up and Esc restoration into an empty composer with synchronous text recovery and asynchronous cancellation. Up retains queued-recall precedence; Esc preserves existing drafts. The full prompt survives dispatch/start races and can be edited and resubmitted before cancellation finishes. No fixed elapsed-time window: available while starting/running. Claude/Codex timing comparison and implementation evidence: docs/qa_evidence/2026-09-24-recall-7Z08/README.md.

![Up restores the sent multiline prompt while cancellation is pending](docs/qa_evidence/2026-09-24-recall-7Z08/01-restored-prompt.png)
Landed 044d7b13 on main. The exact committed tree built Relay and consolemode tests and passed --recall-only plus queuecontract. Shared-checkout consolemode and queuecontract also passed after the open-question fix (run 20260924T224855Z-ef88). Local build/relay updated to build 2026-09-24.18H.05; running user sessions were not restarted. Existing clean-tree H2KQ test race is #5P0Q.

## Tests
`ctest -R consolemode` — tests/consolemode_test.cpp
`ctest -R queuecontract` — tests/xcxd_ui_cases.h
`manual: docs/qa_evidence/2026-09-24-recall-7Z08/README.md`

Board run 20260924T224244Z-b7c1: both shared-checkout tests passed. Isolated Xvfb --recall-only also passed. Exact-tree build passed; consolemode exposed an unrelated existing H2KQ shell-label assertion (recorded separately).

### Check 2026-09-24 18:49
- passed · ctest:consolemode — ctest -R consolemode passed for this revision on spark-dcc9, 2026-09-24T22:49:05Z
- passed · ctest:queuecontract — ctest -R queuecontract passed for this revision on spark-dcc9, 2026-09-24T22:49:05Z
- not-applicable · manual:docs/qa_evidence/2026-09-24-recall-7Z08/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-24-recall-7Z08/README.md
- notice · ctest:consolemode — ctest -R consolemode is slow: p95 6.69 s, p50 1.62 s
- notice · ctest:queuecontract — ctest -R queuecontract is slow: p95 3.52 s, p50 3.49 s
history: thread
## Decisions
User: "and we can do esc as well for that like codex". Both Up and Esc stop and restore when the composer is empty.
User: "check the timing that claude and codex use so it feels natural". Checked official Claude documentation and Codex implementation history; use immediate, lifecycle-based restoration with no arbitrary timeout.
