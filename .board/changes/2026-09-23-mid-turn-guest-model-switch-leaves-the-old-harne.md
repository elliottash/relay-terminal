---
id: YZZT
type: work
status: planned
labels: [bug, guest]
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-23'
source: issues/bug_intake.txt, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [B9V4], github: null}
---
# Mid-turn guest model switch leaves the old harness attached

## Issue
mid-turn set_model ONTO a guest preset is defective (found executing #B9V4, out of scope there): the harness is started at request time but the deferred landing never attaches it — on a native pane the harness:// config would reach _provider_for, on a guest pane the old harness keeps serving a pane whose config names the new guest

## Done means
A `set_model` onto a guest preset accepted while a turn runs starts the new harness only at the deferred landing (turn end / step boundary), attaches it, and from that moment the pane's config and its serving provider agree: the guest answers the next step, and whatever served before (native provider or an older harness) is closed or detached.

Failure looks like either symptom from the issue: `agent.config` names the new guest while the old harness or native provider still answers the next step, or a `harness://` config reaching `_provider_for` on a native pane. Equally a failure if the harness is started eagerly at request time and then never attached.

## Plan
**Goal** — Settle the card against current code: the defect was filed from a `bug_intake.txt` note written while executing #B9V4, and #MSW7 ("one landing path for direct picks and role picks, including guest harnesses") landed afterwards with exactly the machinery this card says is missing. Prove the defect is gone (and lock it with the two untested quadrants), or fix what still reproduces.

**Findings** — the wiring the issue says is absent is present today:
- `session_protocol.py` `switch_model` (:322) builds `apply_target()` (:347), which handles all four quadrants: native→guest (`guest_harness_provider.start_provider` + `attach`, :357-365), guest→same guest (`guest_harness_provider.switch_model` reuse, :348), guest→different guest (`start_provider`, then `previous.close()` :364), guest→native (`detach` + `_provider_for` on the *native* config, :366-377).
- `decide(idle)` (:389) passes `apply_model=apply_target` into `agent.request_model` (`agent.py:1555`); mid-turn it defers via `defer_model` (`agent.py:1593`) which stores `apply_model` in the pending dict; `_land_switch` (`agent.py:1778`) runs `pending["apply_model"]()` at the landing instead of bare `set_model(pending["config"], …)` — so a `harness://` config never reaches `_provider_for` on the deferred path.
- Tests already cover native→guest deferred (`tests/test_model_switch.py::test_deferred_guest_pick_starts_only_at_landing_and_serves_next_step` — asserts the harness is *not* started at request time, is attached at the landing, serves the next step), start-failure rollback (:86), and mid-turn guest→native (:659).
- **Genuinely untested quadrants**: mid-turn guest→*different* guest (old harness must be closed at the landing) and mid-turn guest→*same* guest with a model change (`switch_model` reuse). No test asserts the old harness stops serving there.

**Steps**
1. Run the existing coverage first: `XDG_DATA_HOME=$(mktemp -d) PYTHONPATH=$PWD/backend RELAY_KEYRING=off python3 -m unittest tests.test_model_switch -v`. If the three guest tests pass, the issue's two symptoms no longer reproduce on the covered quadrants — record that on the card.
2. Add the two missing tests to `tests/test_model_switch.py` (same `blocked_first_step` pattern as :62): (a) pane on guest A, mid-turn `set_model` onto `guest:other` — at the landing the new harness is attached, the old harness is closed, the next step is answered by the new guest, `agent.config` names the new preset; (b) pane on guest A, mid-turn `set_model` with a different model for the same guest — the harness is reused (`switch_model`, not a restart), the next step is answered by the same harness on the new model.
3. If either new test fails, fix in `session_protocol.py` `apply_target` / `agent.py` `_land_switch` so the deferred landing always goes through `apply_model` and the previous provider is closed/detached exactly once; keep the fix additive so role swaps and failover (which pass no `apply_model`) are untouched.
4. If everything passes, no code change: the card is closed by #MSW7. Say so in `## Outcome` naming the tests, and land the two new tests as the card's commit.

**Risks**
- If step 1 fails, the regression is newer than the card: `git log -p --follow backend/relay_core/session_protocol.py` around `apply_target` to find what dropped the threading, and restore it rather than re-inventing it.
- `defer_model`/`_land_switch` are shared with role swaps and failover — no behaviour change when `apply_model`/`pre_land` are absent.
- No owner decision needed.

**Verify**
- The two new tests plus all of `tests.test_model_switch` pass; run `tests.test_guest_harness` (or the nearest guest-harness test module) as a neighbour check.
- Manual smoke: on a Claude Code guest pane, mid-turn switch to a Codex guest preset — `model_applied` at the landing, the old harness's process exits, the new guest answers the rest of the turn.
