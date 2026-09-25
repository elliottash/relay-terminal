---
id: 7QH0
type: work
status: needs-verification
labels: [feature, models, ui]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 965becf6-52ea-4a18-bf5e-4f8b641d663b
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: terminal pane, 2026-09-25
links: {plans: [], commits: [45d9c240], evidence: [], related: [YZZT, JDN4], github: null}
---
# Queue a model change with the next prompt

## Issue
allow queing a model change with the next prompt

## Discussion points
Map of today's behaviour (verified 2026-09-25, `backend/relay_core/agent.py`):

- Idle pane: a picker change applies at once (`request_model` → `apply_now`, ~agent.py:1700).
- Mid-turn pick: accepted since issue #3ES1, but **deferred to the next step boundary of the running turn** (`defer_model`, ~agent.py:1738: "Accept a set_model while a turn runs; it lands at the next step boundary. A request that has started answering is never aborted") — so the running turn itself changes models partway through. Only routed (plan/vision) or failed-over turns hold the switch until turn end (`apply_pending_model`: `at == "step" and (self._routed or self._failover)` → `"turn"`); the desktop tooltip already promises exactly that case ("from the end of this turn", `servingTooltip` in `src/Pane.h` ~11405).
- So on an ordinary turn there is today no way to say "let this turn finish on the current model; switch for the next prompt" — which is this card.
- The queue holds prompts/steers/commands as rows (desktop `QueueRowDelegate` in `src/Pane.h`; phone row kinds `steer`/`agent`/`command` in `src/PaneState.h` + `app/pane.js`); a model change is not a queue entry, and a pending switch has no row anywhere yet.
- The landing machinery a turn-end switch needs already exists (`apply_model` / `_land_switch`, `model_switch_deferred` / `model_applied` on the wire — the same path #YZZT's guest fix went through), so the backend change is a landing-point choice plus a pending state; most of the work is the affordance and showing it.

Open design choice for the owner:

1. Make **turn end** the default landing for every mid-turn pick (the running turn stays whole on one model — matches what the routed-turn tooltip already says), or
2. keep the step-boundary default and add an explicit "with next prompt" hold — a modifier in the picker, or a `/model x` line in the composer that rides with the message.

Either way the pending switch wants a visible place before it lands: a queue strip row (ties into #JDN4) and/or a chip state in the pane header, on desktop and phone.

## Done means
- Changing the model during an active turn queues a `/model <name>` entry in the queue strip (desktop and phone); the running turn finishes on its current model. Picking while idle still applies at once.
- The queued entry walks the same ladder as a queued prompt: Enter → steer, delivered at the next tool call without aborting a request that is answering; Enter again → interrupt and change now.
- Ordering is the queue's: prompts queued before the entry run on the old model, ones after it on the new; the transcript records the landing (`model_applied`).
- Tests cover: queued landing at turn end; the steer rung; the interrupt rung; a routed/failover turn's existing turn-end hold unchanged; a second pick replaces a pending entry instead of stacking another.

## Decisions
**2026-09-25, owner:** "when you change model during an active agent job, it goes into the agent queue as "/model fable" or whatever, and you can press enter again to steer at next tool call, or enter 2x to interrupt and change now"

A mid-turn pick is no longer an invisible deferred switch: it becomes a queue entry that walks the same ladder a queued prompt already walks (src/Pane.h:9175 toast, `escalateSteerToInterrupt`):

1. queued `/model fable` — lands after the running turn, with the next prompt (new default);
2. Enter again — steer: delivered at the agent's next tool call (what `defer_model` does today, now an explicit rung);
3. Enter 2× — interrupt the turn and change now.

## Planning notes
Touchpoints the chosen design needs (none of it new machinery):

- **Queue row**: a `/model <name>` row kind — desktop `QueueRowDelegate` (src/Pane.h), phone row list in `src/PaneState.h` + `app/pane.js`, alongside the existing `command` rows. It joins delivery order (running → steers → queued).
- **Backend landing**: `defer_model` / `apply_pending_model` (backend/relay_core/agent.py) already has both landing points — `"step"` and `"turn"` (used for routed/failover turns). The queued rung is `at="turn"`, the steer rung is today's `at="step"`, the interrupt rung is an immediate apply. Mostly plumbing a landing choice from the queue entry.
- **Ladder**: reuse the prompt ladder — Enter on the row promotes to steer, Enter again escalates to interrupt (`escalateSteerToInterrupt`, the toast at src/Pane.h:9175 is the exact wording).
- **State while pending**: the header chip keeps showing the *current* model; the pending change lives in the row. A second pick while an entry is pending should replace that entry, not stack a second one. A phone pick mid-turn produces the same entry.

## Execution Summary
Landed in `45d9c240` (2026-09-25), through `land.py`'s build gate: the exact tree was built and `ctest -R "modelqueue|panestate|queuecontract"` passed on it (3/3).

**Worker** (`backend/relay_core/agent.py`, `session_protocol.py`, `queue.py`): `set_model` takes `when`:
- `"steer"`: the next step boundary. An absent `when` means the same, so other clients behave as before.
- `"queue"`: held to the end of the running turn (`applies: "turn_end"`). It never pre-empts a retry wait.
- `"now"`: held, and the running turn is stopped for it (`TurnSupervisor.interrupt_running`). The queue is not paused, and an exclusive compaction is not stopped.

`model_changed` echoes `when`. New `model_withdraw` → `model_switch_withdrawn` takes back a switch that has not landed. Documented in `docs/AGENT-SESSIONS-PROTOCOL.md` §2.

**Pane** (`src/Pane.h`, `PaneRuntime.cpp`, `PaneSession.cpp`, `PaneEvents.cpp`):
- A pick while the agent works goes through `sendModelSwitch`, so it becomes a `/model <name>` entry in the agent lane. This covers the model chip / `selectModel`, `setMainModel` and a guest model pick.
- Prompts queued before the entry run on the old model, ones after it on the new. The chip stays on the model in force until the entry runs.
- When its turn comes the entry is sent idle, and the lane holds until the worker answers (`after_compaction` holds it until `model_applied`).
- Empty Enter right after the pick steers it: a `↪ next tool call ↻ /model …` row, sent as `when: "steer"`. Enter again interrupts (`when: "now"`).
- × withdraws, → switches now, a second pick replaces the entry in place, and picking the model in force drops it.

**Phone**: `queueRows()` and `PaneState` publish kind `model` rows with actions remove / steer / send_now / up / down. A steered row offers remove and send_now; `app/pane.js` labels its remove "Withdraw".

**Tests**:
- `tests/test_model_switch.py`, 8 new cases: queued lands at turn end with the turn whole on the old model; a held switch does not pre-empt a retry; the steer rung lands at the step; the now rung stops the turn, lands and leaves the queue running; a second queued pick wins; withdraw; `when` is validated; idle ignores `when`. The whole file passes (34).
- `tests/panestate_test.cpp`: model row actions and labels. 27 pass.
- `tests/modelqueue_cases.h` (ctest `modelqueue`, offscreen Pane): the full ladder with a prompt queued ahead; queued dispatch holding the lane until answered; picking the model in force drops the entry; withdraw with ×.
- Routed and failover turn-end holds are unchanged: `tests/test_plan_turns.py` and `tests/test_failover.py` pass except one failover case.

**Unrelated pre-existing failures**, which fail identically on a clean export of `HEAD` 459c2e59 without this change: `test_failover.FailoverTests.test_the_twin_is_tried_once_even_when_the_list_names_openrouter` and `test_queue.ConsoleFieldTests.test_a_card_turn_is_refused_the_writers_at_call_time_and_told_why`.

Not exercised: the real app with a live model, because the shared checkout does not build at the moment (other sessions' uncommitted `BoardModel.h` / `RelayWindow.h` edits). Covered by the offscreen pane cases instead.
