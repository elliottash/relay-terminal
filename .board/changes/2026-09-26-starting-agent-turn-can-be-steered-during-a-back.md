---
id: MMEF
type: work
status: executing
labels: [bug, queue, tests]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:ashe-ethz-ch
session: dc713c52-45bc-4954-ba65-72ecb17516de
discovered_from: C0Q8
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low}
links: {plans: [], commits: [c88dd9db9a53], evidence: [], related: [XCXD], github: null}
---
# Starting agent turn can be steered during a background wait

## Issue
`tests/queuesubmit_test.cpp::a_turn_still_starting_never_steers_even_if_a_wait_was_reported` fails because `queuesubmit::decide` returns Steer when agentBusy, agentTurnStarting and agentWaitingBackground are all true. A turn reservation must queue the new prompt until its start is settled.

## Done means
A prompt submitted while `agentTurnStarting` is true queues even if a stale background-wait signal is present. The focused `queuesubmit` test passes on a fresh build.

## Execution Summary
`src/QueueSubmit.cpp` checks the start reservation before allowing a background-wait steer. Commit `c88dd9db`, queue job `2b8ac70bbe6fd49f` (publication pending).

## Tests
- ctest -R queuesubmit
- Manual run: fresh target build and `ctest` — 1 passed.
