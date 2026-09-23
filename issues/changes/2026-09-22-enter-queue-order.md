---
id: QFF1
type: work
status: needs-verification
labels: [bug, queue]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mqfif
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [124781856813f371cd066034ef4ad7bb90103be3], evidence: [docs/qa_evidence/2026-09-22-enter-queue-order/], related: [7JD1], github: null}
---
# Repeated Enter sends the head of the queue

## Issue
issue with the queue -- i had two things queued, i pressed enter twice, and it sent the second message, not the first

## Done means
- With two agent prompts queued, empty Enter steers the first, leaving the second queued.
- The next Enter escalates that same first prompt, without promoting the second.
- Explicit remote steering still sends the prompt supplied by the remote user.

## Plan
Fix the empty-Enter selector in src/Pane.h and keep explicit remote steering targeted by id. Exercise real composer key events in tests/consolemode_test.cpp, build, and record the wire requests and remaining queue.

## Tests
- `ctest -R consolemode`
- manual: docs/qa_evidence/2026-09-22-enter-queue-order/NOTES.md

### Check 2026-09-22 22:51
- failed · ctest:consolemode — ctest -R consolemode failed for this revision on spark-dcc9
- not-applicable · manual:docs/qa_evidence/2026-09-22-enter-queue-order/NOTES.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-enter-queue-order/NOTES.md
- notice · ctest:consolemode — ctest -R consolemode is slow: p95 1.63 s, p50 0.88 s
history: thread
## Execution Summary
Empty Enter promotes the FIFO head and then escalates that same steer before considering another queued prompt. Explicit remote steering remains id-targeted. Updated the queue hint. Composer-key regression passes through CTest and under Xvfb with isolated settings. tests_check has no findings; evidence records this run (the board's historical test store is older).
Landed as 12478185. The exact commit tree built relay-consolemode-tests and its consolemode CTest passed (1/1), independent of other sessions' working-tree edits.
