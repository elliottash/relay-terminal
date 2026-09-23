---
id: GBN4
type: work
status: planned
labels: [bug, guests, switchboard]
rank: m
created: '2026-09-23'
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
`PYTHONPATH=backend python3 -m unittest tests.test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure` passes on a clean checkout, and the rest of `test_guest_board_bridge` / `test_board_tools` still pass.

The contract the test states stands: a guest's board write made before its turn fails stays in the card's thread, stamped `model=` from the pane's live config (`claude-live-model` in the test), and board calls after the failed turn answer `unavailable` because `complete()`'s `finally` ends the bridge turn.

Failure shows as: the comment missing from the thread, a wrong or missing `model=` stamp, or the bridge still accepting writes after the turn ended. If the decision lands on changing the prompt contract instead of the code, the test is updated to say so and this section's first line still passes.

## Plan
**Goal:** Make `test_provider_turn_binds_native_context_and_revokes_on_failure` pass, keeping the contract it states: a guest's board write made before its turn fails is kept with its live model stamp, and the bridge is revoked afterwards.

**Findings** (from static reading; confirm at step 1):

- The quoted thread holds only the setup entry, so the scripted `turn` callable in the test (`tests/test_guest_board_bridge.py:199-211`) raised *before* `board_comment` was ever written. Its first line is `self.assertIn('relay_board', prompt)` — if that fails, or if the bridge call returns an error and `assertNotIn('error', result)` fails, the exception propagates through `FakeHarness.send` (`tests/guest_harness_fake.py`, callables re-raise), is wrapped by `HarnessProvider.complete()`'s `except Exception` into a `ProviderError` (`backend/relay_core/guest_harness_provider.py`, `complete()`), and `Agent.ask` records an error turn without re-raising. The outer test then fails at `assertIn('native identity', text)` — exactly the reported symptom, with the real failing line invisible.
- Strong suspect: nothing in the current send path puts `relay_board` into the prompt. `complete()` sends only `last_user_message(messages)` (plus `opening`, the #1V4F handover brief, and the todos snapshot — none apply here). The guest instructions, the text that mentions `relay_board` (`backend/relay_core/guest_instructions.py:14-21, 55, 65`), are handed to `harness.start(instructions=...)` in `start_provider()` and never enter a `send()` prompt. `agent.py` has no `policy_text` reference; `board_tools.policy_text()` is for the worker's own system prompt (`board.py:2105`). So with today's code the scripted turn's first assertion fails before any write.
- The durability half of the contract is already the design: BoardTools writes land immediately and are never rolled back on turn failure (undo is a 30-second GUI toast, `board_tools.py:27-28`, `undo()` at :3464); the model stamp comes from `Bridge.request` calling `agent.sign_board()` before `_execute` (`guest_board_bridge.py` dispatch; `agent.py:1268-1286` sets `board.context.model/preset` from the live config); revocation is `board_bridge.end()` in `complete()`'s `finally`. No rollback was added by #MEMS — the regression is in what the guest's first prompt carries, or in the test.

**Steps:**

1. Reproduce on the current tree: run the single failing test, then surface *which* line inside the scripted `turn` raises (temporarily print the traceback inside the callable, or inspect the error event `Agent.ask` emits). Do not trust the outer `AssertionError` — it is three inner failures away from the cause.
2. Read what the #MEMS commits changed here: `git log -p 28e74f5e f4cd1def -- backend/relay_core/guest_harness_provider.py backend/relay_core/guest_board_bridge.py backend/relay_core/guest_instructions.py tests/test_guest_board_bridge.py`. The question: did a fresh harness's first `send()` prompt previously carry the guest instructions (which name `relay_board`), and was that dropped?
3. Fix, exactly one of:
   a. **Restore the prompt contract (preferred if step 2 shows it existed):** in `HarnessProvider.complete()`, prepend `self.instructions` to the prompt on the first turn of a fresh harness (`not self.briefed`, alongside/before the handover brief), so a guest is told about its relay_board tools in the prompt it actually reads. Keep the test unchanged.
   b. **Fix the test (if instructions-at-`start()` is confirmed as the only intended channel):** change the scripted turn's first line to assert `'relay_board' in self.harness.instructions` (FakeHarness stores them) instead of `prompt`; keep every other assertion — comment persistence, `model=claude-live-model`, `bridge.active is None`, post-failure `unavailable` — untouched, since that behaviour is correct.
4. If step 1 instead shows the `board_comment` call itself returning an error mid-turn, fix that dispatch path in `Bridge.request`/`Agent._execute` so the write lands and is stamped; the test stays the spec either way.
5. While here: make the inner failure visible for the next person — e.g. let the scripted callable's exception text survive into the turn's error event, or assert on it in the test — so a future regression points at the real line.

**Risks / decision for the owner:** 3a duplicates the instructions for real guests (they already receive them via CLI flags at `start()` — harmless but redundant); 3b narrows what the test proves about what a guest is told. Step 2's history should settle it; if it does not, ask the owner whether a fresh guest's first prompt should carry the relay_board instructions. Touching `complete()`'s prompt assembly affects the #1V4F handover path — keep the handover brief ordering intact.

**Verify:** the single failing unittest; then the whole `tests.test_guest_board_bridge` module and `tests.test_board_tools` (the 272 neighbours the card's evidence names); plus `tests.test_guest_memory` because the suspect commits are #MEMS's. All with `PYTHONPATH=backend python3 -m unittest …`.
