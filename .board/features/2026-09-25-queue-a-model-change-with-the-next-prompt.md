---
id: 7QH0
type: work
status: executing
labels: [feature, models, ui]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 965becf6-52ea-4a18-bf5e-4f8b641d663b
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: terminal pane, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [YZZT, JDN4], github: null}
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
