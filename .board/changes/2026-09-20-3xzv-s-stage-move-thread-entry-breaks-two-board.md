---
id: 2MF1
type: work
status: needs-verification
labels: [bug, switchboard, tests]
assignee: agent
implemented_by: kimi/kimi-k3
priority: 2
rank: zzzzzzzzzzzzzzw
created: '2026-09-20'
links: {plans: [], commits: [44f89c30], evidence: [docs/qa_evidence/2026-09-20-2mf1-asktests-stage-move/], related: [], github: null}
---
# #3XZV's stage-move thread entry breaks two board_ask protocol tests (bisected to 4f5acd43)

## Issue
check that the maximum agent cap in the switchboard is removed. i got this error that the agent was already working on 3 other cards so couldnt plan

## Evidence
Found 2026-09-20 while checking #0Z13 (the uncap). At HEAD (0c9cd894) two `tests/test_board_protocol.py` AskTests fail:

- `test_the_question_is_recorded_before_the_agent_sees_it` — asserts the question is the card's last thread entry; `4f5acd43`'s stage lifecycle now appends a move entry ("✦ owner moved this card · Inbox → Discussing · the discussion started") after it.
- `test_a_second_turn_on_the_same_card_is_refused_while_the_first_runs` — same assertion pattern.

Bisect (`git bisect start 0c9cd894 7edbb09e`, the two tests as the runner) pins **first bad commit: 4f5acd43** "Switchboard: manual sections and the stage lifecycle (#3XZV)". Both tests pass at 7edbb09e and at 7edbb09e~1, so this is not #0Z13's doing — the uncap only deleted cap logic.

The tests now need to read the thread expecting the stage-move entry after the question (or the ask path needs to record the question after the stage move — owner's call on which order the thread should read).

## Plan
**Goal**
Make the two failing AskTests in `tests/test_board_protocol.py` assert what the stage lifecycle (#3XZV) now guarantees — the owner's question is the last *owner comment*, with the stage-move event after it — instead of asserting it is the literal last thread entry. No production change.

**Findings**
- `BoardCommands._ask` (`backend/relay_core/board_protocol.py`, ~1270–1285) appends the owner's question first (`tools.board.append_thread(...)`), then calls `tools.stage_advance(card_id, "plan-started" if mode == "plan" else "discussed")` inside a try/except that swallows refusals, so the turn runs regardless.
- `BoardTools.stage_advance` (`backend/relay_core/board_tools.py:2064`) looks up `STAGE_MOVES[event]` = `(froms, to, why)` (`board_tools.py:2622`; `"discussed": (("inbox",), "discussing", "the discussion started")`) and returns None — no write, no event — when the card is missing, not `work`, not in `froms`, or has no path. Otherwise it moves the card and appends `"- ✦ {actor} moved this card · {old} → {new} · {why}"` (~2106–2107).
- Both tests create the card `inbox` and ask once, so exactly one move event ("Inbox → Discussing · the discussion started") lands after the question; the tests' `[-1]` assertions now see that event. A second ask while one runs is refused in `_busy_error` *before* anything is appended, so in the second test the move event from the first ask is what `[-1]` sees — the refusal itself still leaves no trace.
- `ProtocolTest.asked(card_id)` (test file, ~118) already returns the last `author="owner", kind="comment"` entry — stage-proof, and what `ModeTests` already use. The twin test at `tests/test_board_turns.py:133` asserts nothing about the thread tail, so it is unaffected (matches the bisect: only two tests fail).

**Steps**
1. `AskTests.test_the_question_is_recorded_before_the_agent_sees_it` (~373): replace the `[-1]` assertion with `self.assertEqual(self.asked(card_id).text, "where should this run?")`, and pin the order the thread now reads by also asserting the trailing event: `self.assertIn("the discussion started", self.board.thread(card_id)[-1].text)` — the question is recorded before the agent sees it, and the move it earned follows it. Leave the `board_thread_appended` event assertions unchanged.
2. `AskTests.test_a_second_turn_on_the_same_card_is_refused_while_the_first_runs` (~489): replace the `[-1]` assertion with `self.assertEqual(self.asked(card_id).text, "one")` plus `self.assertNotIn("two", [e.text for e in self.board.thread(card_id)])` — the first question is still the last owner comment and the refused ask left no trace.
3. Nothing else: no change to `board_protocol.py`, `board_tools.py`, or `test_board_turns.py`.

**Risks**
- Owner's call, per the Evidence: fix the tests (above) or reorder `_ask` to stage-move *before* appending the question so the question is literally last. I recommend the tests-only fix: #3XZV's lifecycle is the deliberate product ("each stage moves itself at the event that earns it"), and the thread reading question-then-move is chronological — the words caused the move. Reordering would change every ask thread's reading to paper over two assertions. If the owner prefers the reorder, this plan is the wrong one and `_ask` moves its `stage_advance` call above `append_thread` instead (note `stage_advance` re-reads the card, so the append must still land first if the move's event is to quote the same card version — that wrinkle is another reason to prefer the tests-only fix).
- `asked()` filters on author *and* kind, so it stays correct on boards where the card was already `discussing` and no move event appears.

**Verify**
Run the two tests, then the class, targeted only (no full suite per repo rules):
- `python3 -m unittest -v tests.test_board_protocol.AskTests.test_the_question_is_recorded_before_the_agent_sees_it tests.test_board_protocol.AskTests.test_a_second_turn_on_the_same_card_is_refused_while_the_first_runs`
- `python3 -m unittest -v tests.test_board_protocol.AskTests` (guards the neighbouring assertions the edit sits among)
Land through `python3 scripts/land.py begin` / `commit`, with the passing run as the evidence under `docs/qa_evidence/`.

## QA checklist
- [ ] `python3 -m unittest -v tests.test_board_protocol.AskTests` passes.
- [ ] The two previously failing tests (`test_the_question_is_recorded_before_the_agent_sees_it`, `test_a_second_turn_on_the_same_card_is_refused_while_the_first_runs`) pass individually.
- [ ] No production code changed (`backend/relay_core/board_protocol.py` and `board_tools.py` are untouched).
- [ ] Commit hash and evidence path are recorded in this card's `links`.
- [ ] Card thread reflects the stage move to `needs-verification`.
