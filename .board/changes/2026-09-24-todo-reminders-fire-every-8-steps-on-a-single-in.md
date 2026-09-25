---
id: VQXA
type: work
status: needs-verification
labels: [bug, agent, tokens]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 6ccc5d01-a1b3-4a97-b82c-aec0a9594eb5
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {plans: [], commits: [6747a8043db8, 89de054cf66d], evidence: [docs/qa_evidence/2026-09-25-todo-reminders-gated/], related: [SZHQ, 234Z], github: null}
---
# Todo reminders fire every 8 steps on a single in-progress todo: 27 injected into one 193-request turn

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Evidence.** Session `3f4a20ad` (#234Z, GLM 5.3, 193 requests, 19.5M prompt tokens): the todo list held one item, `T1 … implement, test, land`, `in_progress` the whole turn. Relay injected **27** reminders: 17 × `update_todos has not been used for 8 steps while tasks are open` (`backend/relay_core/todos.py:276`) and ~10 × `… Keep working through the open items above` (`backend/relay_core/agent.py:3981`). None carried information: the one task was obviously still in progress. Each one is extra prompt the model reads, and it breaks the cached prefix.

**How to address.**
1. Suppress the staleness reminder when the list has exactly one open item and it is `in_progress`: there is nothing to update.
2. More generally, fire it only when there is something to update: several open items and the turn's recent tool calls touch the area of one that is not `in_progress`, or the turn is about to end with open items.
3. The "keep working" recap: at most once per N steps *and* only after a user-visible event (compaction, interrupt, a long tool wait), not on a timer.
4. Log `reminder_injected kind=… step=…` in the worker log so the per-turn count can be measured; target under 3 per 100 steps on a single-task turn.

**Done means.** A single-task turn of 100+ steps gets no `update_todos` staleness reminder (test in `tests/test_todos*.py` or the agent loop tests); multi-task turns still get one when an item goes stale; the log line exists.

## Done means
- A turn whose open list is a single `in_progress` todo runs to any length with **no** `update_todos` staleness reminder; a turn with a `pending` open todo still gets one, at most one per further 8 steps. Note `test_stale_todo_reminder` (tests/test_requests.py:402) asserts exactly the single-task reminder this card removes — it is rewritten, not extended.
- The "Keep working through the open items above…" recital is injected only at the RECITE cadence **and** only after a user-visible event since the last one (mid-turn compaction, a steer, or a tool wait over the threshold) — never on the timer alone.
- Every injected reminder (staleness, no-list, recital, completion check) logs `reminder_injected kind=… step=…` to the agent worker log, so a #234Z-shaped turn can be counted afterwards.
- Failure looks like: replaying the #234Z shape (one `in_progress` todo, 100+ tool steps, no compaction) still shows `[Relay reminder: update_todos has not been used…]` or `relay_kind: "recitation"` notes in the transcript.
- `python3 -m pytest tests/test_requests.py -k reminder` passes.

## Plan
**Goal.** Stop the turn loop from injecting reminders that carry no information: a single-task turn (evidence #234Z: 27 injections across 193 requests) gets none, and every reminder that *is* injected is logged so the per-turn rate can be measured (target < 3 per 100 steps on a single-task turn).

**Findings.**
- Staleness reminder: `STALE_TODO_STEPS = 8` (`backend/relay_core/agent.py:82`); injected at `agent.py:2589-2594` whenever `ctx["since_todos"] >= 8` and *any* open todo exists (`open_items(include_delegated=False)`); the counter is incremented per step at `agent.py:2639`, reset at 2594 and on `update_todos` (`agent.py:4377`). Text built by `todo_tool.reminder_text()` (`backend/relay_core/todos.py:274`). This is the 17× offender.
- Recital: every `RECITE_STEPS = 25` steps or `RECITE_TOOL_CALLS = 50` calls (`agent.py:91-92`, injected at `agent.py:2604-2611`); `_recitation()` (`agent.py:3964`) appends "Keep working through the open items above…" whenever open items exist — pure timer, no event required. This is the ~10× offender.
- Not offenders, leave alone: `no_list_reminder_text` (`todos.py:259`, injected `agent.py:2600` — fires only when no list exists) and the completion-check reminder (already capped, `MAX_COMPLETION_REMINDERS = 2`, `agent.py:76`, `agent.py:2676-2681`).
- Logging pattern to follow: `logs.event(_log, "<name>", session=…, turn=…, step=…)`, with `_log = logs.get("agent")` (`agent.py:109`).
- The agent-loop test harness lives in `tests/test_requests.py`: `Script` provider, `self.agent(provider, completion_check=False)`, `todos_call`, `forever_tools` helpers. `test_stale_todo_reminder` (line 402) asserts the single-`in_progress`-todo reminder this card removes.

**Steps.**
1. `backend/relay_core/todos.py`: add `def update_reminder_needed(open_todos: list[dict]) -> bool` beside `reminder_text`: True iff at least one open item is `pending` — i.e. there is something the model never started and may have forgotten. An all-`in_progress` list gets no reminder: the model set those statuses itself, so the list is current by construction. Docstring names this card.
2. `backend/relay_core/agent.py:2589`: compute the open list once per step, gate the staleness reminder on `todo_tool.update_reminder_needed(open)`, and keep the `since_todos = 0` reset inside the branch that actually injects.
3. `backend/relay_core/agent.py`: gate the recital injection (2604) on a warrant. Add `"recite_warrant": False` to the ctx init (`agent.py:2483-2491`); set it True at the step boundary when, since the last recital, (a) a mid-turn compaction ran, (b) a steer was applied (`self.steer_source` / `_steer_message`), or (c) a tool call waited more than a new `LONG_TOOL_WAIT_S = 30` constant (beside the other constants at `agent.py:82-92`). Inject the recital only when the cadence fires **and** the warrant is set; clear the warrant on injection. `_recitation()` itself is unchanged.
4. `backend/relay_core/agent.py`: log every injection — `logs.event(_log, "reminder_injected", kind=…, session=…, turn=…, step=…, tool_calls=…)` at the four sites: staleness (2589), no-list (2600), recital (2609), completion reminder (2681). `kind` ∈ {`todos_stale`, `todos_missing`, `recitation`, `completion`}.
5. `tests/test_requests.py`, new cases beside the existing reminder tests:
   - single `in_progress` todo + 9 `forever_tools` steps → no staleness reminder and no recitation (rewrites `test_stale_todo_reminder`);
   - a `pending` open todo + steps → exactly one staleness reminder per 8-step window;
   - a long turn with no compaction/steer/slow tool → zero `relay_kind: "recitation"` messages; forcing a mid-turn compaction → at most one recital, after it;
   - a reminder that does fire logs `reminder_injected` — assert the way other `logs.event` calls are asserted in the suite (if none is asserted anywhere, assert on the emitted events and say so in the test docstring).
6. Land per repo rules: `python3 scripts/land.py begin <me> backend/relay_core/todos.py backend/relay_core/agent.py tests/test_requests.py`, build nothing (Python), run `python3 -m pytest tests/test_requests.py -k reminder` plus the existing todos tests, then `commit`. This is a medium card: move it to `done` with the commit in the card's record.

**Risks / owner decisions.**
- Rule choice: the card's option 2 also floated "recent tool calls touch the area of a non-`in_progress` item"; this plan takes the simpler "any pending open item" rule — the touch-area heuristic needs an area model the code does not have, and the end-of-turn completion check already catches forgotten items. Say the word if you want the heuristic instead.
- Gating the recital on events means a turn that drifts for hundreds of steps with no compaction, steer or slow tool gets no recital at all; loopdetect (`_loop_verdict`, `LOOP_CHECK_*`) remains the guard there. If that feels thin, add "loopdetect raised a nudge" as a fourth warrant.
- `LONG_TOOL_WAIT_S = 30` is a first guess; the `reminder_injected` log line will show real wait distributions to tune it later.

**Verify.**
- `python3 -m pytest tests/test_requests.py -k reminder` and any `tests/test_todos*.py`, green.
- Manual replay of the #234Z shape (Script: one `in_progress` todo + 30 tool steps, no compaction): transcript holds zero staleness `note`s and zero recitations, and the worker log holds no `reminder_injected` line.

## Execution Summary
Landed in `6747a804` and `89de054c` on `main`.

- `todos.update_reminder_needed(open_todos)`: true only when an open item is `pending`. The 8-step staleness note in `agent.py` is gated on it, so an all-`in_progress` list is never nagged. The no-list nudge still fires only when there are no open todos, as before.
- Recital: the 25-step / 50-call cadence now only makes it *due*. It is injected once `ctx["recite_warrant"]` is set by a steer, a model takeover, a mid-turn compaction that rewrote the transcript (`_maybe_compact` now returns the `compacted` event; `summary_chars` or `trimmed_tool_outputs` counts), or a tool call of at least `LONG_TOOL_WAIT_S = 30` s. The cadence marks wait until then, so after a warrant the recital fires at the next step boundary. Differences from the plan: the plan listed compaction, steer and slow tool; I added the takeover because it is a user-visible event of the same kind.
- `reminder_injected kind={todos_stale,todos_missing,recitation,completion} session=… turn=… step=… tool_calls=…` is logged to `relay.agent` at all four sites.
- `tests/test_agent.py`: the two #2CZP cadence tests now patch `LONG_TOOL_WAIT_S` to 0 so they still check the step and tool-call marks. A new test checks that the cadence alone recites nothing.
- Two hunks of #5NDQ's (`_note_reread`) were sitting in `agent.py` while I worked. They were excluded from `6747a804` and #5NDQ has since landed them itself.
- The owner decisions in the plan's Risks (pending-only rule, no loopdetect warrant) were taken as planned.

## Tests
On `89de054c`, 2026-09-25. Evidence: `docs/qa_evidence/2026-09-25-todo-reminders-gated/` (`README.md`, `targeted-tests.txt`).

- `test_single_in_progress_todo_gets_no_stale_reminder` replays the #234Z shape: one `in_progress` todo, 40 tool steps, no event. Result: zero staleness notes, zero recitals, and no `reminder_injected` log line (`assertLogs('relay.agent')`).
- `test_stale_todo_reminder_while_an_item_is_pending`: a `pending` item gets 2 notes in 17 steps, 8 steps apart, each logged `kind=todos_stale` (the first at `step=9`, eight steps after the `update_todos` at step 1).
- `test_recital_waits_for_an_event_after_the_cadence`: a steer at boundary 30 produces exactly one recital, right after the steer, logged `kind=recitation`.
- `test_a_mid_turn_compaction_warrants_the_recital`: a compaction that rewrote the transcript gives 1 recital; a no-op compaction gives 0.
- `test_a_slow_tool_call_warrants_the_recital`, and in `tests/test_agent.py` `test_the_cadence_alone_recites_nothing` plus the two #2CZP cadence tests.
- `python3 -m pytest tests/test_requests.py -k reminder` passes. `python3 -m pytest tests/test_requests.py tests/test_agent.py tests/test_todo_subagents.py -q` gives **110 passed**, both in the checkout and on a clean `git archive HEAD` export.
