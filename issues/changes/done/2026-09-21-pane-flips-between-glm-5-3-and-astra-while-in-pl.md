---
id: HR5E
type: work
status: done
labels: [bug, models, agent]
assignee: agent
implemented_by: kimi/kimi-k3
verified_by: kimi/kimi-k3
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-21'
source: pane bcb3f818, 2026-09-21
links: {plans: [], commits: [08cd7f37, 7f9b59e4], evidence: [], related: [Z0VG, GPF7], github: null}
---
# Pane flips between glm-5.3 and astra while in plan mode (session bde50672)

## Issue
i observed a related weird bug in session bde50672e69443a48e42855349d1893a

it was switching between glm 5.3 and astra while in plan mode.

## Execution Summary
**Root cause** (from `~/.local/share/relay/logs/worker.log`, session bde50672): every plan-mode turn fired `plan_route` gpt-6-astra → glm-5.3 (effort=max, source=configured) and back at turn end — 9 plan turns, 18 visible model-box flips. The `planning` role's default followed the High tier (`ROLE_TIERS["planning"] == "high"`, the 2026-09-20 change), and the default-filled High list names glm-5.3, so a Codex pane never planned on Codex.

**Fix** (owner: "it should run on codex astra in xhigh"), two commits — `08cd7f37` (its message is mistakenly "x"; this is the real one) and `7f9b59e4`:

- `roles.py`: `ROLE_TIERS["planning"] = None`; the unpinned default is `_high_default` — the pane's own model pushed to max reasoning — and never consults `tiers.high`. A hand pin still routes: an endpoint pin, a `{"tier": …}` pin (a `{"tier": "high"}` pin is the pre-#HR5E behaviour, guest entries included), and — new in `_configured` — a `guest:` pin, which starts that guest's harness for the turn. `_high_default` on a guest main config resolves as main instead of writing a bogus `reasoning_effort` into the harness config.
- `agent.py`: `_begin_guest_plan_boost` — an unpinned plan turn on a guest pane stages the harness's top level for the running model (`xhigh` for astra) through the harness's `set_effort`, emits the usual same-model `plan_route` ("Plan mode · this turn runs on gpt-6-astra at xhigh."), and `_end_plan_turn` restages the pane's own level (or the model's default when none was picked) however the turn ends.
- Docs: protocol 13.7/13.11 updated; ARCHITECTURE.md already agreed.

Not done (out of scope, carded): #GPF7's pre-existing guest-prompt failure reproduced and evidenced on that card; two `DefaultsTests` failures in test_tier_lists/test_model_ranking are another session's uncommitted model-ranking churn, not this change.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_roles tests.test_presets tests.test_plan_turns tests.test_tier_lists tests.test_guest_harness_provider tests.test_model_switch tests.test_configure_recovery` — 285 run, green except 3 foreign/pre-existing failures (#GPF7's `PLAN MODE.` prefix; two `DefaultsTests` from another session's uncommitted model-ranking.md).

New/updated: `PlanTurnTests.test_a_filled_high_list_does_not_pull_a_plan_turn_off_the_panes_model` (the regression), `GuestPlanTurnTests` now pins `roles.planning` (a `guest:` pin and a `{"tier": "high"}` pin), `test_a_pane_that_is_itself_a_guest_plans_at_its_harnesss_top_level` (the xhigh boost + restore), `PlanRoleTests.test_planning_is_not_tiered` / `test_a_high_override_does_not_move_planning` (+2), the tier-list guest/High tests, and `test_presets.test_role_tiers_cover_every_role`.
