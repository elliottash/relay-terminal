---
id: 7QH0
type: work
status: inbox
labels: [feature, models, ui]
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
- A model change queued while a turn runs lets the running turn finish entirely on its current model, and the new model serves the next prompt — the one typed next or the first already sitting in the queue.
- The pending switch is visible before it lands (queue strip and/or model chip, desktop and phone) and the transcript records the moment it lands (`model_applied`).
- Tests cover: the switch lands between queued prompts; a request already answering is never aborted; a routed/failover turn's existing turn-end hold is unchanged.
