---
id: D8VN
type: work
status: needs-qa-llm
labels: [bug]
component: [worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-17
rank: t2
created: '2026-09-17'
acceptance: '`docs/qa_evidence/2026-09-17-tasks-are-todos-only/eval/RESULTS.md` (implementer run: the nudge fires once and is correctly ignored for a single instruction); a QA session confirms a multi-part turn that skips the list gets the note and that no turn gets it twice'
source: 'measured while landing card `H3QW`, 2026-09-17'
links: {plans: [], commits: [], evidence: [], related: [H3QW, 9PGA], github: null}
---
# A turn that writes no todo list is checked by nothing — add a no-list nudge

> **History, all 2026-09-17.** Filed as "GLM-5.3 abandons the todo list when a steer arrives", which
> the event logs disproved (the steer arrives *after* the response that skipped the list). Refiled as
> "GLM-5.3 skips `update_todos` on ~1 in 3 runs", which further runs also weakened — 2 skips in 12
> runs, not 4 in 12. What survived both corrections is the code-level hole below, which is
> model-independent. Renamed from `glm-drops-todo-list-on-steer` → `glm-skips-todo-list` → this.

## The problem

When a model answers a multi-part request without calling `update_todos`, **nothing in Relay checks
whether the parts were done.** Three mechanisms each decline to look:

1. `Agent._open_items()` counts a request as open only when an open todo points at it
   (`agent.py:707-712`, the `item["id"] in linked` test). No todos → no requests counted → the
   end-of-turn completion check never re-prompts.
2. `RequestLedger.finish_turn()` then marks the request `done`, because the turn ended normally and
   there are no open linked todos (`requests.py:186-190`).
3. The stale-todo reminder cannot fire: it requires `self.todos.open_items()` to be non-empty
   (`agent.py:585`). It covers "you have a list and stopped updating it", never "you should have
   started one".

So a five-part ask where the model does three parts and stops talking is recorded as `done`, with no
re-prompt and nothing flagged. Since card [`H3QW`](needs_qa_llm/2026-09-17-tasks-are-todos-only.md)
it also shows no task UI at all. That is an improvement on what it did before — the old
request-as-task backfill displayed a green `Tasks 1/1`, an affirmative claim of completion on a turn
nothing had checked — but silence is not the same as a check.

## What the measurement did and did not establish

`scripts/eval-requests.py` on the identical five-ask prompt (scenarios 1 and 9), glm-5.3:

| Batch | Runs | Wrote a list |
|---|---|---|
| Before the nudge, scenario 1 | 3 | 3 |
| Before the nudge, scenario 9 | 3 | 1 |
| After the nudge (which never fired — see below) | 6 | 6 |
| **Total** | **12** | **10** |

kimi-k3 and deepseek-v4.1-flash wrote a list in every run of that prompt. **2 skips in 12 GLM runs is
too infrequent to characterise, and the swing from 1/3 to 6/6 on the same prompt shows the sample was
never big enough to support the rate this card first claimed.** No mechanism was found; batching was
checked and ruled out (GLM issues all five writes in one parallel batch in the list-writing runs too).

The hole in items 1–3 above does not depend on any of that. It is there for any model on any turn
that skips the list.

## Change

`backend/relay_core/agent.py`: `NO_LIST_TOOL_CALLS = 4`. Once a turn has made four tool calls and
`ctx["todos_touched"]` is still false, one user note is added before the next model call
(`todos.no_list_reminder_text`): the turn's tool-call count, a request for one todo per part if the
request has several, and an explicit "ignore this if it is a single simple ask". At most one per turn
(`ctx["no_list_note"]`), and the branch is an `elif` under the stale reminder, so a turn that has a
list never sees it. Prompt text only — no event, no protocol change, no extra model call.

**Counted in tool calls, not steps.** A step threshold fires too late: these models issue every write
of a multi-part job in one parallel batch, so by step 4 the work is finished. Tool calls trip while
there is still turn left, and a single simple ask stays under four.

`scripts/eval-requests.py` gains `no_list_nudges` (the note is not an event, so it is only visible in
the message history) and scenario 10 — one instruction that needs many tool calls, which is the only
shape that reliably trips the nudge.

## Verification

Unit (`tests/test_requests.py`): `test_no_list_reminder_when_the_model_never_writes_one` (a turn of
six tool calls with no list gets exactly one note, right text and `relay_kind`) and
`test_no_list_reminder_is_silent_for_a_short_turn_or_once_a_list_exists` (a one-call turn gets
nothing; a turn that does write a list gets the 8-step stale reminder and never this one).
`./scripts/test.sh` 487 OK.

Live (scenario 10, "make sure every .txt ends with a newline", six files):

| Preset | Nudge fired | Wrote a list | Files correct |
|---|---|---|---|
| openrouter | 1 | no | 6/6 |
| glm-coding | 1 | no | 6/6 |

**The nudge fires exactly once, and real models honour the escape clause**: for what is genuinely one
instruction they ignored it and finished correctly. That is the harmlessness case, and it passes.

**Not verified: whether a nudge rescues a genuinely multi-part turn.** That needs a model to skip the
list on a multi-part prompt while under observation, and no skip recurred in the six runs after the
change. Both skips on record predate it.

## Left to do

1. **Confirm the rescue case.** The honest way is to wait for a skip in real use rather than hunt for
   one: `no_list_nudges` now makes it visible whenever the eval runs.
2. **Revisit the threshold once there is data.** Four tool calls may be too high to ever fire in
   practice — all three presets solved scenario 10's six files in three or four calls by using a
   shell loop rather than per-file tools. If the nudge turns out never to fire, it is doing nothing.
3. **Consider making the state visible in the UI.** A running turn with open requests and no todos
   currently reads as "nothing is happening". Lower value than the nudge and purely cosmetic.
4. **Dropped: prompt tuning for GLM.** With 2 skips in 12 runs there is no effect to tune against.
