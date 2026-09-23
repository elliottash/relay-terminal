---
id: GPF7
type: work
status: planned
labels: [bug, tests, planning]
assignee: null
rank: mgpf7
created: '2026-09-21'
source: 'Codex regression run for #XP7N'
links: {plans: [], commits: [], evidence: [], related: [XP7N], github: null}
---
# Guest planning test assumes PLAN MODE is the first prompt text

## Issue
Measured during #XP7N: test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back fails at line 382, asserting sent.startswith("PLAN MODE."). The failure reproduces on a clean git archive of a63f33f1542f, before the exit-plan changes. Guest instruction injection now precedes the planning prompt.

## Tests
`PYTHONPATH=backend:tests python3 -m unittest test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back -q`

## Done means
The guest plan-turn test asserts the real prompt contract — the PLAN MODE directive is present and comes before "The request to plan:", not that it is byte 0 of the sent prompt — and the named test plus the whole `test_plan_turns` module pass offline. Failure looks like: the test still asserts `sent.startswith("PLAN MODE.")`, or it fails because the relay_board MCP preamble again lands ahead of the planning directive in a way the assertions do not tolerate.

## Plan
**Goal** — Make `test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back` assert what the guest plan prompt actually guarantees (directive present, correctly ordered), and land the result green.

**Findings**
- The prompt order changed downstream of the prompt builder, not in it. `planning.guest_plan_prompt` (`backend/relay_core/planning.py:93`) still puts `GUEST_PLAN_NOTE` ("PLAN MODE. …") first. But `HarnessProvider.complete` (`backend/relay_core/guest_harness_provider.py`, in the `complete` method, ~line 791) applies `provider.opening` — set to `guest_plan_prompt` by `Agent._start_plan_guest` (`backend/relay_core/agent.py:2580`) — and *then* prepends the relay_board MCP preamble ("Relay offers an MCP server named relay_board…") whenever `self.board_bridge is not None`. So the sent prompt begins with the MCP preamble and "PLAN MODE." follows it. This matches both thread evidence entries.
- The working tree's `tests/test_plan_turns.py` (~lines 399–401) already carries the tolerant form — `assertIn("PLAN MODE.", sent)` and `assertLess(sent.index("PLAN MODE."), sent.index("The request to plan:"))` with a `# Project instructions may precede the planning directive (#GPF7).` comment. The `assertTrue(sent.startswith("PLAN MODE."))` from the card's Issue no longer exists. First job is to establish whether that edit is committed and whether the module is green as-is.

**Steps**
1. `git log --oneline -3 -- tests/test_plan_turns.py` and `git status --short tests/test_plan_turns.py` — find whether the tolerant assertions are committed or an uncommitted edit, and by which change.
2. Run the named test: `PYTHONPATH=backend:tests python3 -m unittest test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back -q`.
3. If it fails, reconcile the assertions with the actual prompt order from `HarnessProvider.complete`: "PLAN MODE." present, before "The request to plan:", prompt still ends with the request. Do not assert on the MCP preamble's exact position — it is not this card's contract.
4. Run the whole module (`PYTHONPATH=backend:tests python3 -m unittest test_plan_turns -q`) — the sibling guest tests also inspect the sent prompt and must stay green. Also run `python3 -m unittest test_sessions -q` (its PLAN MODE assertions are unrelated but cheap insurance).
5. Land through `python3 scripts/land.py begin <me> tests/test_plan_turns.py` / `land.py commit <me> -m …` per the repo rules, and move this card to done.

**Risks**
- The likely outcome is that the test already passes and the card is verify-and-close; the work is then steps 1, 2, 4 and the landing. If the tolerant edit turns out to belong to another in-flight card's commit, coordinate rather than double-committing it.
- Open question for the owner (not blocking this card): the MCP preamble prepended in `HarnessProvider.complete` tells the guest to delegate through the board server's `agent` tools — noise, arguably wrong, for a one-turn read-only planning guest. Suppressing it for plan turns is a code change beyond this card's test scope; file a separate bug card if the owner wants it.

**Verify** — The named unittest passes; the full `test_plan_turns` module passes offline (no API key, FakeHarness only). The card's `## Tests` line is the named-test invocation above.
