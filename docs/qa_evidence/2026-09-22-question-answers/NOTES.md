# #QAN1: question-answer routing audit and implementation evidence

## Findings and changes

| Path | Finding / resulting behavior |
| --- | --- |
| Structured question, Enter | Existing direct answer path worked unless queue/completion key handling took Enter first. The open ask now owns Enter before those handlers. |
| Structured question, Ctrl+Enter | Busy-agent action bypassed requestRoute and started an interrupt prompt. Both the action and composer chord now use question_answer. |
| Empty Enter during an ask | Could steer an unrelated queued item. Now consumes the key without answering or sending anything. |
| Multiple-choice and free-text answers | Use the same handler; numbered choices resolve to labels, all answers return together after the last question. Queue contents and ids stay unchanged. |
| Queue row selected when question arrives | Leave selection before answering, so the answer cannot overwrite a queued row. Queue pumping is blocked while an ask is open. |
| Explicit terminal / card-comment submission | Remains distinct; Ctrl+Shift+Enter still reaches the context without answering the question. |
| Completed prose question | Needs-you status previously did not hold queued work. Pane and worker now pause ordinary queued work and run the next user reply first. The pane reserves the slot until busy arrives. |
| Background subagent report | Does not release the worker's question wait. Existing explicit Resume can skip the question. |
| Paired phone | pane.js sends question answers agent-bound before ordinary compose; submitRemote answers before steering/queueing. Existing real-browser tests pass. |
| Claude/Codex managed guests | worker routes question_answer to guest_harness_provider before native questions; existing guest round-trip tests pass. |
| Raw TUI guest / unmarked mid-turn prose | No reliable structured question signal: ordinary guest delivery cannot infer an answer from arbitrary text. This change does not parse a TUI screen or semantically classify streaming prose. |

Prose detection intentionally uses the existing Needs-you rule: the final nonblank
line ends in `?` or `？`, allowing trailing Markdown closers and excluding a final
fenced code block. It can pause on a rhetorical question and cannot recognize all
unpunctuated requests for input. Explicit Resume remains available; structured asks
are the reliable path. The protocol documents the optional awaiting_reply field.

## Validation

- `scripts/relay-build --target relay-consolemode-tests`: passed.
- `ctest --test-dir build -R '^consolemode$' --output-on-failure`: 1/1 passed.
- `XDG_CONFIG_HOME=/tmp/relay-question-config QT_QPA_PLATFORM=xcb xvfb-run -a build/relay-consolemode-tests`: passed. Desktop notification rate-limit message after the tests is environmental.
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p test_queue.py`: 40 passed.
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p test_questions.py`: 35 passed.
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p test_guest_harness_provider.py`: 68 passed.
- `PYTHONPATH=backend:tests python3 -m unittest test_pane_view.PaneViewTests.test_the_agents_question_is_drawn_and_a_tap_answers_it_agent_bound test_pane_view.PaneViewTests.test_answering_the_ask_empties_the_box_the_way_an_ordinary_send_does`: 2 real-browser tests passed.
- `git diff --check`: passed.
- Board check: 12 pre-existing unrelated errors, no findings for QAN1.

No live model-provider session was used; deterministic providers and real composer
events verify dispatch and ordering. Independent verification remains pending.
