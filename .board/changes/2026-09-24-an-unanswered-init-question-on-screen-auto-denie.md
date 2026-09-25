---
id: NSYT
type: work
status: needs-verification
labels: [bug, switchboard, board]
assignee: agent
implemented_by: glm/glm-5.3
session: 10f88b29-bedc-4ba1-a026-3a7768cb830a
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'A board_init_request arriving while the pane''s own init question is on screen is answered by the user''s click on that question (yes creates the board and completes the parked card; no releases the tool call), never by an automatic accept:false from the pane; the auto-no remains only for a remembered no and a guest pane.', sign_off: none, effort: medium}
source: pane 1, 2026-09-25
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-nsyt-standing-question/], related: [], github: null}
---
# An unanswered init question on screen auto-denies the agent's first board tool call

## Issue
check out this pane in modalities: 56ed3b35 i tried to start a board there but it was denied, can you QA that to see if its a relay bug
**QA verdict: Relay bug, confirmed.** The user asked an agent in `/home/elliott/repos/modalities` (pane `56ed3b35`, session `c6f7263d`) to make a board; the `board_create_card` call came back "This project has no Board and the user declined to create one. Do not call the board tools again in this conversation" — but the user had declined nothing.

What actually happened (all times from `~/.local/share/relay/logs/relay.log`, 2026-09-24):

1. `23:13:52.902` — the pane's first prompt raised the init question, trigger `agent-work`: "Initialize a project and create a Board here?" as a block under the terminal (owner's rule: never a dialog). It sat there **unanswered**.
2. `23:15:57.674` — the agent's `board_create_card` raised `board_init_request` (`reason=agent-card`).
3. `23:15:57.687` — the tool result with the denial arrived: **13 ms later**. No human read and answered a question in 13 ms; nothing was shown for this second ask.
4. The user then tried the Board pane (`8dd7735d`): two errors `This project has no Board yet. Create one first (board_init)` (`23:18:02/03`), then `/card` raised the question there (`23:18:11`) — also never answered. No board was ever created.

Proof the user never declined:

- `~/.local/share/relay/state/projects.json` has `"declined": []` — a real "No" click persists there immediately, and it is empty.
- The pane scrollback (`sessions/83e6ce670835f99d/c6f7263dd6a14a30b266bceb6f674719.scrollback.txt` lines 66–73) has no "Not now"/declined inline line — `answerProjectInit` prints one on every click.

Root cause — a non-answer is converted into the user's "no", twice:

- `src/Pane.h` `handleBoardInitRequest` (~14056): when `projectinit::decide(AgentCard, situation)` returns `Nothing`, the pane **sends `board_init_answer {accept: false}` immediately**. With the 23:13 question still on screen, `situation.asking` is true and the decision table's row "a guest's pane, or a question already up → Nothing" fires — a row whose stated meaning is *wait for the answer*, implemented as an instant no.
- `backend/relay_core/board_tools.py` `BoardInit.answer()` (~1400): any `accept:false` sets `self.declined = True` — "the user said no: nothing asks again until the board is re-pointed" — so the refusal text ("the user declined … Do not call the board tools again") is also wrong about who declined, and poisons the rest of the conversation: later board calls raise `NO_BOARD_TEXT` from `self.init.declined` without ever asking again. `NO_BOARD_TEXT`'s own comment — "only reached if the user just declined the init dialog" — is false on this path.
- The standing question cannot satisfy the parked request even if the user clicks Yes on it: its `m_initRequestId` is empty (it came from `agent-work`, not the worker), so the yes is never forwarded to the worker's `bi-N` id; the tool call has already returned false.

Fix direction (for whoever takes it): when `decide()` says Nothing because a question is already up, link the new `bi-N` request to the standing question (Yes forwards to both; the worker then completes the parked card per protocol 19.12), and at minimum never latch `declined` on anything but an explicit user "No" — e.g. a distinct answer payload (`reason: superseded`) that releases the tool call without the latch and without claiming the user declined. Reproduces whenever an agent makes the first card while the agent-work init question is still unanswered on screen — exactly the "lets make a board" flow.

## Done means
An agent's `board_create_card` in a boardless project, raised while the pane's own init question about that project sits unanswered on screen, is answered by the user's click on that question — yes creates the board and completes the parked card, no/not-now releases it — and never by an automatic `accept:false`. The automatic false remains only for a remembered no and a guest's pane. No refusal text claims the user declined when they did not click.

## Execution Summary
`src/Pane.h` `handleBoardInitRequest` now links the worker's request to the standing question: inside the `!decision.asks()` branch, when the pane is `Probing` or `Asking` about the very project the worker named (`m_initProject == project`), it sets `m_initRequestId = id` and returns without sending — so the existing yes path (`InitStage::Creating`, `accept: true` → worker creates the board and completes the parked card per 19.12) and the no/not-now path (`accept: false`, a real user answer) both flow through the user's click. The instant `accept:false` stays for a remembered no and a guest pane, the two reasons that are the user's own no. The worker side is unchanged: it never receives a false that nobody chose.

Not taken: the alternative in the Issue (a `reason: superseded` payload plus a worker-side latch split) is unnecessary once the pane stops sending a false non-answer, and would widen the wire protocol; the snooze case ("Not now" then an agent call) still asks, per the decision table.

Landed via `scripts/land.py` (session `boardfix`): `5979eb7c` carried the Pane.h hunks (a concurrent session's commit swept them; verified present at that sha), `06f6282e` the test, `f10b1b83` the protocol doc. Two pre-existing projectinit failures at HEAD (0-occurrence assertions, unrelated) filed as #FV0P. Evidence: `docs/qa_evidence/2026-09-24-nsyt-standing-question/`.

## Tests
- `tests/projectinit_test.cpp` `theStandingQuestionAnswersTheWorkersParkedRequest()` — reads the `handleBoardInitRequest` wiring as text, as the neighbouring tests do: the link (`m_initRequestId = id` guarded by `Probing || Asking` and `m_initProject == project`) returns before any `board_init_answer` refusal is sent.
- `ctest --test-dir build -R projectinit` / `./build/relay-projectinit-tests`: 20 passed, 2 failed — both failures pre-exist at HEAD (#FV0P), both assert strings absent from HEAD itself.
- land.py's verify gate built the exact landed tree (`cmake --build … --target relay`) before both commits; `scripts/relay-build --target relay` green.
