---
id: SWCP
type: work
status: done
labels: [feature, models, compaction]
assignee: claude-code
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: mswcp
created: '2026-09-23'
source: 'Claude Code in a Relay pane, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [1V4F, P1DG, PH9G], github: null}
---
# Compact a conversation above 128K tokens on any model change

## Issue
when changing between models, i would say, compact if there are more than 128K tokens in the context

## Done means
- A model change with more than 128K tokens in the context compacts first, even when the new window holds it; failure shows as `model_applied` without `compacted` at 128K+.
- Below 128K, a switch that fits compacts nothing.
- A compaction that only the 128K rule asked for and that cannot run (a guest in force with no summaries role) never blocks the switch: it lands with the whole conversation and says so. A compaction needed to fit the new window still refuses the switch when it fails.

## Execution Summary
`agent.SWITCH_COMPACT_TOKENS = 128_000`. `Agent.switch_fit` now reports `required` (over the new window's limit, as before), `compacts` (required, or over 128K) and `goal` (`min(window limit, 128K)`); `apply_pending_model` compacts towards `goal` through `compact(target_limit=…)`, since `context.compact` only summarises while `over()` holds. On a failed compaction that was not required, the switch lands whole with a `status`. Protocol §2 `set_model` notes gained the rule.

## Tests
- `tests/test_model_switch.py`: `test_a_switch_above_128k_compacts_even_when_the_new_window_holds_it`, `test_a_switch_below_128k_that_fits_does_not_compact`, `test_a_128k_compaction_that_cannot_run_still_lands_the_switch_whole`, `test_a_required_compaction_that_cannot_run_is_still_refused` (the first and third fail on the parent tree).
- Green: test_model_switch, test_guest_handover, test_session_protocol, test_failover, test_plan_turns, test_sessions, test_guest_harness_provider, test_guest_harness_codex, test_card_model_selection, test_customproviders, test_provider.
