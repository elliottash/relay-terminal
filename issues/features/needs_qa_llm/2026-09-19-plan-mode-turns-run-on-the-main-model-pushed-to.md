---
id: Z0VG
type: work
status: needs-qa-llm
labels: [feature, agent]
component: [agent, gui, providers]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Oz agent (Warp session), 2026-09-19
rank: zzzzzzi
created: '2026-09-19'
acceptance: plan-mode turns run on the `planning` role — by default the pane's own model pushed to max reasoning — and the pane returns to its own provider when the turn ends, however it ends; the role is settable in Settings › Models (Advanced options) and over configure/set_agent_options; build turns and providers without an effort knob are untouched; a non-Claude model QA session runs the checklist below and records it under `docs/qa_evidence/`
source: '`issues/feature_intake.txt`, 2026-09-19: "allow a separate planning agent with higher reasoning. change to max reasoning by default."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-19-plan-mode-turns-run-on-the-main-model-pushed-to/'], related: [EM1E, MQ9C], github: null}
---
# Plan-mode turns run on the main model pushed to max reasoning (the "planning" role)

## Issue
allow a separate planning agent with higher reasoning. change to max reasoning by default.

## Behavior as implemented (2026-09-19)

Protocol: [`docs/AGENT-SESSIONS-PROTOCOL.md` section 13.11](../../../docs/AGENT-SESSIONS-PROTOCOL.md).
Backend: `backend/relay_core/roles.py` (the role and its default), `backend/relay_core/agent.py`
(the per-turn swap). GUI: `src/Pane.h`. Phone: `remote/wire.py`.

**The role.** `planning` sits in the roles table right under Agent turns, labelled **Plan mode**,
with the hint "investigating and writing plans; max reasoning by default". Like vision and route
assist it follows no tier: its default is *the pane's own model with its effort pushed to `max`*
through the provider's effort style — the "separate planning agent" is the same model thinking
harder, not a different one.

**When the default changes nothing, nothing happens.** If applying max leaves the request's
`extra` unchanged — the provider has no effort knob (Anthropic's compat layer, MiniMax: style
"none"), or the pane is already at max — the role resolves back to the main agent and no swap is
made and no event sent, exactly like an image turn the main model can read. On Relay Free every
level above medium is clamped by the gateway, so there too the role resolves as main.

**A hand-picked planning model wins.** Set `roles.planning` (configure / set_agent_options, or the
row in Settings › Models › Advanced options) to a preset or a base URL + model, with an optional
effort, and every plan-mode turn runs there instead.

**The turn swap.** A turn sent in plan mode (`mode: "plan"`) swaps the provider for that turn
only, before the vision decision — an image inside a plan turn nests inside the plan swap. The
pane prints `◆ Plan mode · this turn runs on <model> at max reasoning.` (or "…runs on <model>,
then back to <model>." when a planning model was pinned), the model chip reads
`◆ <model> · this turn`, and `plan_route` / `plan_route_ended` carry the same on the wire
(forwarded to a phone in `remote/wire.py`, like `vision_route`). The provider goes back in
`_end_turn` and in a `finally` backstop — done, error and cancelled all restore it before the
terminal event. A `set_model` during a plan turn defers to the turn's end (the image turns' rule,
now shared), and failover refuses to move a turn the plan swap owns.

**Build turns are untouched.** Executing a plan (`plan_execute`) is not a plan-mode turn; it runs
on the pane's own model as before.

## Implementer check (not a QA verdict)

Oz agent via Warp, 2026-09-19. Evidence:
[`docs/qa_evidence/2026-09-19-plan-mode-turns-run-on-the-main-model-pushed-to/`](../../../docs/qa_evidence/2026-09-19-plan-mode-turns-run-on-the-main-model-pushed-to/).

- `./scripts/test.sh`: 2554 tests, OK. `ctest --test-dir build`: 55/55 passed. Both run and green
  in the implementing session on 2026-09-19; their full output was not captured to a file (the
  capture run was cut short), so the counts stand on this note — see the evidence README.
- `tests/test_plan_turns.py` (7 new): the swap and restore, the next turn back on the pane's own
  model, restore after an error, the configured planning model, build-mode no-op, already-max
  no-op, no-effort-knob no-op — offline against a provider that records what it was sent.
- `tests/test_roles.py`: 6 new planning-resolution tests (default, configured, Relay Free,
  no-knob, already-max, tiered-fallback skipping).
- `tests/test_remote_wire.py`: `plan_route` / `plan_route_ended` classified as forwarded.
- Live under Xvfb with an isolated `HOME`/`XDG_*` on the stored `glm-coding` key: Shift+Tab turned
  the mode chip to PLAN, the pane printed "◆ Plan mode · this turn runs on glm-5.3 at max
  reasoning.", the chip read "◆ glm-5.3 · this turn" mid-turn and was back to `glm-5.3` when the
  turn ended. Screenshots `implementer-01` … `implementer-06`; the roles modal's Advanced list
  shows the Plan mode row.
- Not verified live: a pinned planning model on a real endpoint (unit-tested), a provider with no
  effort knob live (unit-tested), the ◆ line rendered on a phone (classification tested).

## QA checklist

1. **The default swap.** On a pane running `glm-5.3`: Shift+Tab (mode chip: PLAN), send a prompt.
   The pane prints "◆ Plan mode · this turn runs on glm-5.3 at max reasoning.", the model chip
   reads "◆ glm-5.3 · this turn" while it runs, and after the turn the chip is back to `glm-5.3`.
   Confirm from `~/.local/share/relay/logs/` that the plan turn's request carried
   `reasoning_effort: max` and the next ordinary turn did not.
2. **Off is off.** With plan mode off (mode chip: auto/agent), the same prompt produces no ◆ line
   and no chip change.
3. **A pinned planning model.** Settings › Models › Advanced options › Plan mode: set it to
   another keyed provider. A plan turn must run there ("◆ Plan mode · this turn runs on <model>,
   then back to glm-5.3.") and come back; an ordinary turn after it stays on `glm-5.3`. Set it
   back to the default afterwards.
4. **Restore on stop.** Esc during a plan turn: the chip goes back before "Stopped." and the next
   turn runs on the pane's own model.
5. **No effort knob.** On MiniMax (or an Anthropic preset): a plan turn runs with no routing line
   — the model's own default stands, nothing is swapped.
6. **Relay Free.** A plan turn on Relay Free shows no routing line (every effort is clamped to
   medium server-side).
7. **Build turns.** Execute a written plan (plan_execute): it runs on the pane's own model, no ◆.
8. **Model change mid-turn.** Pick another model while a plan turn runs: it must defer to the end
   of the turn ("this turn finishes on …"), not interrupt it.
