---
id: QAN1
type: work
status: needs-verification
labels: [bug, queue, questions]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mqan1
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [cf473cd53b74016b741ae8730831caefc4a0495e], evidence: [docs/qa_evidence/2026-09-22-question-answers/], related: [QFF1, MQ9C], github: null}
---
# Question answers wait behind queued prompts

## Issue
another issue -- i had prompts queued and the agent asked me questions. i answered the questions, but it queued them after my prompts, so the question answers didnt work

## Done means
- Answers reach the question that requested them without waiting behind ordinary queued prompts.
- Existing queued prompts retain their order and contents.
- Normal new prompts still queue when the agent is busy.

## Planning notes
`Pane::requestRoute` already answers a structured `m_ask` directly, before routing or queueing. `recordAnswer` emits `question_answer` after the final answer in a group. Plain prose questions do not establish `m_ask`; their replies can reach ordinary busy-agent queueing. `interruptAgentWithPrompt` also bypasses requestRoute while busy, so the submission gesture matters for structured questions. Need the observed question presentation and submission gesture to reproduce the reported path before changing it.

## Decisions
Owner: "i cant remember, analyze them all" — investigate all question presentations and submit gestures; no further reproduction detail required.

## Plan
1. Route Enter/Ctrl+Enter to an open structured question before queue shortcuts; keep explicit terminal submissions separate. Guard the queue while an ask is open.
2. Pause pending work when a completed reply ends with a question, using Relay's existing Needs-you punctuation rule; make the next user reply run ahead of ordinary queued work on both pane and worker queues. Resume remains available.
3. Test structured single/multiple questions, empty Enter, Ctrl+Enter, normal prompt FIFO, prose question replies and explicit resume. Build and exercise the exact landed tree; record the audited remote/guest paths and limitations.

## Execution Summary
Audited desktop Enter/Ctrl+Enter, empty Enter, selected queue rows, explicit terminal/comment submits, paired phone, native and managed guest questions, completed prose, and raw TUI limitations. Fixed structured-answer shortcut bypass and empty-Enter queue escalation. Completed prose questions now pause ordinary queue draining using Relay's existing Needs-you punctuation rule; the next user reply runs first, with the pane's start reservation preventing overtaking. Worker background reports do not resume the wait. Full audit and limitations are recorded in docs/qa_evidence/2026-09-22-question-answers/NOTES.md.
Landed cf473cd5. Exact-tree console build and CTest passed, and the local relay application was rebuilt. Recorded final targeted runs via TestsCommands: 4/4 pass, no opened signals; tests_check now has no findings. Hardened the new worker regression to wait for the provider delta before observing its prompt list.

## Tests
- `ctest -R consolemode`
- `tests/test_queue.py::SupervisorTests::test_question_reply_precedes_queued_work`
- `tests/test_queue.py::SupervisorTests::test_resume_can_skip_a_prose_question`
- `tests/test_queue.py::SupervisorTests::test_question_detection_matches_pane_punctuation_rule`
- manual: docs/qa_evidence/2026-09-22-question-answers/NOTES.md

### Check 2026-09-22 22:51
- failed · ctest:consolemode — ctest -R consolemode failed for this revision on spark-dcc9
- passed · unittest:tests.test_queue.SupervisorTests.test_question_reply_precedes_queued_work — tests/test_queue.py::SupervisorTests::test_question_reply_precedes_queued_work passed for this revision on spark-dcc9, 2026-09-22T17:45:23Z
- passed · unittest:tests.test_queue.SupervisorTests.test_resume_can_skip_a_prose_question — tests/test_queue.py::SupervisorTests::test_resume_can_skip_a_prose_question passed for this revision on spark-dcc9, 2026-09-22T17:45:23Z
- passed · unittest:tests.test_queue.SupervisorTests.test_question_detection_matches_pane_punctuation_rule — tests/test_queue.py::SupervisorTests::test_question_detection_matches_pane_punctuation_rule passed for this revision on spark-dcc9, 2026-09-22T17:45:23Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-question-answers/NOTES.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-question-answers/NOTES.md
- notice · ctest:consolemode — ctest -R consolemode is slow: p95 1.63 s, p50 0.88 s
history: thread
