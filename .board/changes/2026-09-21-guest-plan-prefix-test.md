---
id: GPF7
type: work
status: needs-verification
labels: [bug, tests, planning]
assignee: agent
implemented_by: kimi/kimi-k3
session: 0c551c5d-2755-4d8b-af79-b52539aab4d5
rank: mgpf7
created: '2026-09-21'
source: 'Codex regression run for #XP7N'
links: {plans: [], commits: [347678b7, 5904a0e0], evidence: [docs/qa_evidence/2026-09-24-gpf7-plan-guest-double-start/], related: [XP7N, JKG2], github: null}
---
# Guest planning test assumes PLAN MODE is the first prompt text

## Issue
Measured during #XP7N: test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back fails at line 382, asserting sent.startswith("PLAN MODE."). The failure reproduces on a clean git archive of a63f33f1542f, before the exit-plan changes. Guest instruction injection now precedes the planning prompt.

## Tests
`PYTHONPATH=backend:tests python3 -m unittest test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back -q`

## Done means
Both tests that encode a stale prompt-placement assumption assert the real contract and pass offline. In `test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back`, the PLAN MODE directive is present and ordered before "The request to plan:" — not asserted as byte 0 of the sent prompt. In `test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure`, the relay_board guidance is asserted where Relay actually puts it (`harness.instructions`), not in the turn prompt text. The whole `test_plan_turns` and `test_guest_board_bridge` modules pass with no API key (FakeHarness only). Failure looks like: either test back on a `startswith("PLAN MODE.")` or prompt-text assertion, or a module run failing because the MCP preamble / harness-instructions placement moved again and the assertions do not tolerate it.

## Plan
**Goal** — Close out both stale prompt-placement assumptions this card now covers (the plan-turn PLAN MODE prefix and, per the 2026-09-23 evidence entry, the relay_board prompt-text assertion in the board-bridge test), verify the modules green, land whatever is still uncommitted, and move the card on.

**Findings**
- Plan-turn test: `planning.guest_plan_prompt` (`backend/relay_core/planning.py:93`) still puts `GUEST_PLAN_NOTE` ("PLAN MODE. …") first, but `HarnessProvider.complete` (`backend/relay_core/guest_harness_provider.py`, ~line 1169–1178) applies the opening and prepends the relay_board MCP preamble when `board_bridge` is set, so "PLAN MODE." is no longer byte 0. The working tree's `tests/test_plan_turns.py:397–399` already carries the tolerant form (`assertIn` + `assertLess` on ordering, tagged `#GPF7`); the `startswith("PLAN MODE.")` from the Issue is gone.
- Board-bridge test: Relay now supplies bridge guidance as harness instructions, not prompt text — `build_instructions` output is passed with `board_bridge=bridge.descriptor, instructions=instructions` (`backend/relay_core/guest_harness_provider.py:819–829`) onto `provider.instructions` (line 868). The working tree's `tests/test_guest_board_bridge.py:276–280` already asserts `relay_board` in `harness.instructions` with a `#GPF7` comment; the prompt-text assertion from the 2026-09-23 evidence is gone.
- So this card is very likely verify-and-close for both files. What remains is establishing whether those edits are committed, landing them if not, and proving both modules green.

**Steps**
1. `git log --oneline -3 -- tests/test_plan_turns.py tests/test_guest_board_bridge.py` and `git status --short tests/test_plan_turns.py tests/test_guest_board_bridge.py` — committed or uncommitted, and by which change. If the edits belong to another in-flight card's claim (`python3 scripts/land.py who`), coordinate rather than double-commit.
2. Run the two named tests: `PYTHONPATH=backend:tests python3 -m unittest test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure -q`.
3. If either fails, reconcile its assertions with the actual contract from `HarnessProvider` — directive present and ordered; bridge guidance in `harness.instructions`. Do not assert on the MCP preamble's exact position; it is not this card's contract.
4. Run both whole modules plus the cheap insurance: `PYTHONPATH=backend:tests python3 -m unittest test_plan_turns test_guest_board_bridge test_sessions -q`. Sibling tests inspect the sent prompt and must stay green.
5. Land any uncommitted edits through `python3 scripts/land.py begin <me> tests/test_plan_turns.py tests/test_guest_board_bridge.py` / `land.py commit <me> -m …` per the repo rules (claim only files you actually changed), then move this card to done.

**Risks**
- The likely outcome is both tests already pass and the card is verify-and-close; the work is then steps 1, 2, 4 and the landing.
- If the tolerant edits turn out to be another session's uncommitted work, do not commit them — wait or coordinate via `scripts/land.py who` contacts.
- Open question for the owner (not blocking): the MCP preamble prepended for a one-turn read-only planning guest is arguably noise. Suppressing it for plan turns is a code change beyond this card's test scope; file a separate bug card if wanted.

**Verify** — Both named unittests pass; the full `test_plan_turns` and `test_guest_board_bridge` modules pass offline (no API key, FakeHarness only); `test_sessions` stays green. The card's `## Tests` line is the named plan-turn invocation; the bridge test invocation is in step 2.

## Execution Summary
The two tolerant test edits from `## Plan` were already committed, but the named plan-turn test still failed — one line earlier, on `harness.starts`: 6063dff7 (#RND7) had turned the guest-start `if` in `Agent._begin_plan_turn` into a `while` for ranked-peer failover with no exit on success, so a guest that started fine was started a second time and then skipped. Fixed with a `break` on success (commit `347678b7`, `backend/relay_core/agent.py`). While running the plan's step-4 insurance, `test_sessions.PlanModeTests.test_the_execute_prompt_follows_the_plans_orchestration_block` failed on HEAD for an unrelated stale prefix left by 854c097d (#BGRN) — filed as #JKG2 and fixed there (`f7df0d36`). Verification, offline (FakeHarness): the two named tests pass; `test_plan_turns test_guest_board_bridge test_sessions` run 112 tests, OK. Evidence: docs/qa_evidence/2026-09-24-gpf7-plan-guest-double-start/.
