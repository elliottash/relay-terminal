# #DC4J implementer evidence — /swap shortcut hint + audit of retry preemption

## Audit (plan steps 1–2)

- Retry preemption and `/swap` were already committed: `7824689d` (agent: a model switch
  during retries takes effect at once, #DC4J) and `c7dccfa0` (pane: model box, /model, /swap,
  Ctrl+Shift+M on the catalog). No unclaimed uncommitted work for this card.
- `backend/relay_core/provider.py`: `ProviderPreempted` (l.491), `preempt_check` asked every
  `RETRY_WAIT_TICK = 0.1` s inside `_wait_retry` (l.1030–1050) — only between attempts of a
  refused request, never once a response is open.
- `backend/relay_core/agent.py`: `_switch_waiting` gate (l.1130) matches `apply_pending_model`'s
  `at="step"` rule (a routed/failover turn does not preempt); `defer_model` (l.1195) documents
  that a streaming reply finishes on the old model; `ProviderPreempted` is caught at the step
  boundary (l.1883) and `_preempted_step` (l.2762) emits
  `provider_retry {reason: "switch", status, attempt, from_model, to_model}` before the
  `model_applied` lands. Matches `docs/AGENT-SESSIONS-PROTOCOL.md` section 2. No divergence found.

## Tests

`PYTHONPATH=$PWD/backend python3 -m unittest tests.test_model_switch -v` — 19/19 OK, including
both `RetryPreemptionTests`
(`test_a_switch_during_a_retry_wait_re_issues_the_step_on_the_new_model_at_once`,
`test_a_switch_while_the_reply_is_streaming_still_waits_and_says_so` — the streaming case
unchanged and passing).

## The change

`src/Pane.h`: new `hintSwapForPick(key)` beside `openModelPicker` — when a mouse pick (model box
`entry:` row in `modelBoxPicked`, or the picker in `openModelPicker`) moves the pane between the
ranked Main (rank 1) and its fallback (rank 2), it shows the `model.swap.slash` hint
("Next time: /swap … swapping main ↔ fallback") through the existing `hint(id, …)` gate.
`/swap` itself and typed `/model <name>` never trigger it.

## Build

`scripts/relay-build` — built in 49s, `[100%] Built target relay` (2026-09-20.20H.07). The one
warning (`ForkText::guest` missing initializer, Pane.h:7320) is pre-existing and unrelated.
