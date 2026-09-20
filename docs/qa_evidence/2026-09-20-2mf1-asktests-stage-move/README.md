# #2MF1 evidence — AskTests made stage-lifecycle-proof

Code commit: `44f89c30` "Switchboard: make the two AskTests stage-lifecycle-proof (#2MF1)"

What changed in `tests/test_board_protocol.py`:
- `AskTests.test_the_question_is_recorded_before_the_agent_sees_it` now asserts
  the question is the last *owner comment* via `self.asked(card_id)` and checks
  the trailing thread entry contains "the discussion started" (the #3XZV stage
  move that follows the question).
- `AskTests.test_a_second_turn_on_the_same_card_is_refused_while_the_first_runs`
  now asserts the first question is the last owner comment and that the refused
  "two" question left no trace on the card.

No production code changed.
