# #B9V4 — a mid-turn model switch keeps relaying (claude → glm)

**Bug:** switching models mid-turn (claude → glm) stopped the pane relaying; the owner had to
type "continue". Recorded session `eb2828d4f51d456191c55380bf2a620b`.

## Diagnosis of the recorded session

- The pane (92922d16) ran the **Claude Code guest** (`claude -p --model sonnet`,
  `guest_harness_started` 02:30:39), not the Anthropic preset.
- Turn 1 (R1 "is there a card for uncapping the number of switchboard agents",
  `requires_completion`) ended 02:31:10 on a **text-only reply** (a summary of card #0Z13) with
  `open_items=0` — no todo list existed, so `_open_items` counted nothing and the completion
  check never fired. That is the "stopped relaying".
- The switch to glm landed **idle** 02:31:13 (`guest_harness_closed`; no `model_applied` —
  `apply_now` emits none), after the turn had already ended. R2 "continue" (02:31:24) started
  turn 2 on glm-5.3, which worked the card.
- Reading the code found the deferred mid-turn path off a guest still defective (fixed here as
  plan step 4): `_land_switch`'s `set_model` cannot replace an injected provider, so a switch
  accepted while relaying would adopt glm's config while the claude harness kept serving.

## Changes (commit see card `links.commits`)

1. `backend/relay_core/agent.py` — `ask`'s loop captures `apply_pending_model`'s return; a
   landing at `"step"` marks `ctx["takeover"]` and appends a Relay handoff note
   (`_takeover_note`) after the landing (so `adapt_history` has already run).
   `_open_items` counts the turn's open unlinked `requires_completion` requests when the turn
   is a takeover, so a wrap-up in plain text draws the completion check (≤ 2) instead of ending
   the turn. `request_model`/`defer_model` carry a `pre_land` follow-up; `_land_switch` runs it
   under the model lock before `set_model` (absent ⇒ exactly today's behaviour).
2. `backend/relay_core/session_protocol.py` — `_set_model` passes `pre_land` (guest
   `detach`) and `on_applied` (`follow_agent_guest` + `on_model_changed`) when a switch **off a
   live guest harness** is accepted while a turn runs; the idle after-compaction landing carries
   them too. Switches onto a guest keep today's behaviour (separate intake note filed).
3. `docs/AGENT-SESSIONS-PROTOCOL.md` — §2 switch-landing bullet: the handoff note, the
   completion check, and the guest harness ending at the landing.

## Verification

- `logs/test_model_switch.log` — 17/17 OK, including:
  - `test_the_model_taking_over_is_told_so_and_a_wrap_up_draws_the_completion_check` — the
    stub replay of the report: the new model's first request carries the note after the old
    model's tool result; a text-only wrap-up emits `completion_check` with `open` naming R1
    (twice, then the turn ends `done`); the turn carried on (`old, new, new, new`).
  - `test_a_mid_turn_switch_off_a_guest_ends_the_harness_at_the_landing` — harness closed at
    the landing, new (native) provider served the takeover, one `model_applied {at: "step"}`,
    the note reached the native provider, the completion check held the turn open.
  - `test_an_idle_switch_and_a_turn_end_landing_add_no_takeover_note` — no note, no checks.
  - `test_a_role_switch_carries_its_own_follow_up` (existing) — role swaps unaffected.
- Neighbouring suites: `tests.test_sessions tests.test_session_protocol tests.test_requests
  tests.test_queue tests.test_guest_harness_provider` — 185/185 OK
  (`logs/neighbours.log`).

## QA checklist

- [ ] Switch models mid-turn on a native pane (claude → glm) while it is relaying: the busy line
      keeps relaying, the new model's first reply continues the work, no "continue" typed.
- [ ] The transcript shows the `model_applied` mark and a short Relay note about the takeover.
- [ ] A wrap-up in plain text right after the switch draws a completion check naming the open
      request; the turn ends after at most two checks (Esc still stops it at once).
- [ ] On a Claude Code guest pane, switch mid-turn to a native model: the harness closes at the
      landing (`guest_harness_closed` in worker.log), the new provider serves the rest of the
      turn, one `model_applied`, the live-guest tail stops.
- [ ] Idle switch and a switch that lands at turn_end: no handoff note, no completion check,
      `model_changed {applies: "now"/"turn_end"}` as before.
- [ ] Role swap (Main ↔ Flash) mid-turn still lands with its own follow-up (no guest detach).
