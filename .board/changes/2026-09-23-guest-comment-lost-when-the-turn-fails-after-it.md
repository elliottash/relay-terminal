---
id: GBN4
type: work
status: needs-verification
labels: [bug, guests, switchboard]
assignee: agent
implemented_by: kimi/kimi-k3
session: 857ae200-ed0f-46b2-85d4-1c9065bb0a08
rank: m
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Found by Claude Code in a Relay pane, 2026-09-23, while running the board tests for the write-limit change (bd48f2c4)
links: {plans: [], commits: [], evidence: [], related: [MEMS], github: null}
---
# A guest's board comment is missing from the thread when its turn fails afterwards

## Issue
Found while running the board tests; not a user request. `tests.test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure` fails on main.

## Planning notes
Reproduced on a clean `git archive` export of HEAD at bd48f2c4 (after the #MEMS commits 28e74f5e and f4cd1def touched the bridge), so it is not the write-limit change:

```
PYTHONPATH=backend python3 -m unittest tests.test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure
AssertionError: 'native identity' not found in '<!-- relay:entry … author=agent kind=event turn=setup -->
- ✦ agent created this card in Inbox · issues/features/2026-09-23-bridge-test.md
'
```

The guest calls `board_comment` (no error returned), then the turn raises. The thread holds only the setup entry, so the comment either never reached disk or was rolled back with the failed turn. Decide which is intended: the test says a write made before the failure stays and carries `model=claude-live-model`.

## Done means
`PYTHONPATH=backend python3 -m unittest tests.test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure` passes on a clean checkout of current main, and the rest of `test_guest_board_bridge` / `test_board_tools` still pass.

The contract the test states stands: a guest's board write made before its turn fails stays in the card's thread, stamped `model=` from the pane's live config (`claude-live-model` in the test), and board calls after the failed turn answer `unavailable` because `complete()`'s `finally` ends the bridge turn. The prompt question the first plan raised is settled: #GPF7 made the guest instructions a harness-start channel, and the test already asserts `'relay_board' in harness.instructions`.

Failure shows as: the comment missing from the thread, a wrong or missing `model=` stamp, or the bridge still accepting writes after the turn ended.

## Plan
**Goal:** Make `test_provider_turn_binds_native_context_and_revokes_on_failure` pass on current main, keeping the contract it states: a guest's board write made before its turn fails is kept with its live model stamp, and the bridge is revoked afterwards.

**Findings** (re-read 2026-09-24; the tree has moved since the first plan):

- The first plan's open question is **already settled**: the scripted turn's first line is now `self.assertIn('relay_board', harness.instructions)` (`tests/test_guest_board_bridge.py`, inside `test_provider_turn_binds_native_context_and_revokes_on_failure`, marked `#GPF7`) — plan option 3b was applied. The instructions genuinely name `relay_board` (`backend/relay_core/guest_instructions.py:14-21, 55, 65`), `start_provider()` passes them to `harness.start(instructions=...)` (`backend/relay_core/guest_harness_provider.py`, `start_provider`), and FakeHarness stores them (`tests/guest_harness_fake.py`, `self.instructions`). So that assertion should pass today.
- The reported symptom (`'native identity' not found`, thread holds only the setup entry) was produced at bd48f2c4 with the *old* first line (`assertIn('relay_board', prompt)`), which raised before `board_comment` was ever called. The exception propagates through `FakeHarness.send`, is wrapped into a `ProviderError` by `HarnessProvider.complete()`'s `except Exception`, and `Agent.ask` records an error turn without re-raising — so the outer assertion fails three lines away from the real cause.
- The durability half of the contract is the design and was never the regression: BoardTools writes land immediately with no rollback on turn failure; the `model=` stamp comes from `Bridge.request` calling `agent.sign_board()` before `agent._execute` (`backend/relay_core/guest_board_bridge.py:262,274`); revocation is `board_bridge.end()` in `complete()`'s `finally`.

**Steps:**

1. Run the single test on current main: `PYTHONPATH=backend python3 -m unittest tests.test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure`. If it passes, go to step 4 — #GPF7 already fixed this card and only verification evidence is needed.
2. If it still fails, surface *which* line inside the scripted `turn` raises before touching anything: temporarily print the traceback inside the callable, or read the error event `Agent.ask` records. Do not trust the outer `AssertionError`. The remaining suspects, in order: the `board_comment` exchange erroring mid-turn (then `assertNotIn('error', result)` is what raises), a missing/wrong `model=` stamp from `sign_board()`, or `harness.instructions` arriving empty.
3. Fix whichever step 2 names: a dispatch error goes in `Bridge.request` / `Agent._execute` so the write lands and is stamped; an empty-instructions path goes in `start_provider()`'s harness construction. The test stays the spec — do not edit its assertions to make it pass.
4. Make the inner failure visible for the next person: let the scripted callable's exception text survive into the turn's error event (or assert on it in the test), so a future regression points at the real line instead of `'native identity' not found`.
5. Run the Verify list below; land with the test run as evidence.

**Risks:** the only behaviour change contemplated is in the bridge dispatch or provider start path, both covered by the `test_guest_board_bridge` module; the prompt-assembly and handover-brief code (`#1V4F`) is not to be touched. No owner decision remains — #GPF7 settled the instructions-channel question.

**Verify:** the single unittest; then the whole `tests.test_guest_board_bridge` module, `tests.test_board_tools`, and `tests.test_guest_memory` (the suspect commits were #MEMS's). All with `PYTHONPATH=backend python3 -m unittest …`.

## Execution Summary
The revocation bug itself was already fixed by #GPF7: on a clean export of HEAD (235befc4) the named test and the full verify list (test_guest_board_bridge, test_board_tools, test_guest_memory — 360 tests) all pass.

The plan's step 4 gap was real and is fixed in f984f1b1: `HarnessProvider.complete()`'s generic catch dropped the inner exception's text, so a guest dying after its board write surfaced only "harness failed (RuntimeError)". The ProviderError now carries `str(exc)`, and the test asserts the pane's error event contains the guest's own words ('guest failed after write'). 23/23 bridge tests pass after the change.
