---
id: 9K5H
type: work
status: needs-qa-llm
labels: [feature, switchboard]
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzi
created: '2026-09-19'
links: {commits: [8147cc55, b8eb2c45, 307727e5], evidence: [docs/qa_evidence/2026-09-19-switchboard-card-thinking-trace/], github: null, plans: [], related: []}
---
# show the thinking trace in the swtichboard as well

## Issue
in siwtchboard cards -- when discussing with the agent, and especially in plan mode, show the thinking traces same as in the terminals.

## QA checklist
Verified by the implementer; the QA runner should re-check independently.

1. `QT_QPA_PLATFORM=offscreen ./build/relay-board-tests theThinkingTraceRunsInTheCardsThread` passes (the trace streams in the thread, settles to `✦ thought for 4 s`, seals **above** a mid-turn `question` entry, survives leaving and reopening the card mid-turn, and stays after `done` with the answer after it).
2. The rest of `relay-board-tests` fails only on the two busy-strip wording assertions (`theBoxDiscussesAndTheRowPlansOrLeavesTheBoard`, `aCardKeepsItsOwnTurnWhileAnotherCardIsOnScreen`) — another session's in-flight "✦ Switchboarding · …" rewording, present before this change.
3. Live under Xvfb (`docs/qa_evidence/2026-09-19-switchboard-card-thinking-trace/drive.sh`): `implementer-discuss-stream.png` shows the tail under `✦ thinking…` in the thread; `implementer-question.png` shows the sealed block above the agent's mid-turn question; `implementer-plan-stream.png` shows the trace while a Plan turn runs; `implementer-plan-written.png` shows `## Plan` written with the trace sealed above the closing answer.
4. The trace never reaches disk: `card-after.md` and `thread-after.md` (the sandbox card and thread after the run) contain the question, the answers and the `## Plan`, and no reasoning text.
5. `ctest --test-dir build`: everything green except `buttonfit` (the in-flight dark-copper 8.5pt theme font, another session's) and `backend-and-bash`, which passed on rerun (the known flake-under-load card).

Not covered: a real provider's reasoning stream (the stub sends `reasoning_content` deltas); a block longer than the 4,000-character rendered tail (the cut is named in the view; the buffer holds 200,000).
