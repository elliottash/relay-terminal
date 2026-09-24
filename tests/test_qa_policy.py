"""The QA policy floor (card #C3Q2): `relay_core.qa_policy` as pure functions."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import board as B          # noqa: E402
from relay_core import qa_policy as QP     # noqa: E402

INDEPENDENT = {"recommended": {"family": "openai", "same_lineage": False}}
SAME_LINEAGE = {"recommended": {"family": "anthropic", "same_lineage": True}}


def block(**over):
    base = {"artifact": "code", "primary": "script", "effort": "low"}
    base.update(over)
    return B.validate_verify(base)


class ParseTest(unittest.TestCase):
    def test_defaults_are_conservative(self):
        policy = QP.parse(None, None)
        self.assertEqual((policy.verification, policy.ask_at_stakes, policy.ai_may_gate_after,
                          policy.sample_after), ("ask", "money", "never", "never"))
        self.assertEqual(set(policy.source.values()), {"default"})
        self.assertEqual(policy.problems, ())

    def test_project_overrides_global_overrides_default(self):
        policy = QP.parse({"verification": "ask", "sample_after": 20},
                          {"verification": "automatic", "ai_may_gate_after": "always"})
        self.assertEqual(policy.verification, "ask")
        self.assertEqual(policy.source["verification"], "project")
        self.assertEqual(policy.ai_may_gate_after, "always")
        self.assertEqual(policy.source["ai_may_gate_after"], "global")
        self.assertEqual(policy.sample_after, 20)
        self.assertEqual(policy.source["sample_after"], "project")
        self.assertEqual(policy.ask_at_stakes, "money")
        self.assertEqual(policy.source["ask_at_stakes"], "default")

    def test_global_alone_applies(self):
        policy = QP.parse(None, {"verification": "automatic"})
        self.assertEqual(policy.verification, "automatic")
        self.assertEqual(policy.source["verification"], "global")

    def test_a_bad_value_falls_through_and_is_named(self):
        policy = QP.parse({"verification": "maybe", "ask_at_stakes": "huge", "sample_after": -3},
                          {"verification": "automatic"})
        self.assertEqual(policy.verification, "automatic")     # the project value was skipped
        self.assertEqual(policy.ask_at_stakes, "money")
        self.assertEqual(policy.sample_after, "never")
        self.assertEqual(len(policy.problems), 3)
        self.assertIn("board.yaml qa.verification 'maybe'", policy.problems[0])
        self.assertIn("ignored:", QP.effective_line(policy))

    def test_words_and_numbers_normalize(self):
        policy = QP.parse({"ask_at_stakes": "ALWAYS", "ai_may_gate_after": "30",
                           "sample_after": True})
        self.assertEqual(policy.ask_at_stakes, "nuisance")
        self.assertEqual(policy.ai_may_gate_after, 30)
        self.assertEqual(policy.sample_after, "never")         # a boolean is not a count
        self.assertEqual(len(policy.problems), 1)

    def test_non_mapping_layers_are_empty(self):
        self.assertEqual(QP.parse("qa", 3).verification, "ask")


class ApplyTest(unittest.TestCase):
    def test_none_in_none_out(self):
        self.assertEqual(QP.apply(QP.parse(), None), (None, []))

    def test_a_block_under_the_floor_stands(self):
        verify = block(stakes="rework", also=["ai-text"])
        out, notes = QP.apply(QP.parse(), verify)
        self.assertEqual(out, verify)
        self.assertEqual(notes, [])

    def test_stakes_at_the_floor_raise_human_to_required(self):
        out, notes = QP.apply(QP.parse(), block(stakes="money", human="none"))
        self.assertEqual(out["human"], "required")
        self.assertTrue(out["criteria"])
        self.assertEqual(len(notes), 2)
        self.assertIn("human none → required", notes[0])
        self.assertIn("stakes money is at or above the floor money", notes[0])
        self.assertIn("criteria filled in", notes[1])
        B.validate_verify(out)                                 # still a valid block

    def test_stakes_above_the_floor_keep_their_own_criteria(self):
        out, notes = QP.apply(QP.parse(), block(stakes="harm", human="optional",
                                                criteria="the total reconciles"))
        self.assertEqual(out["human"], "required")
        self.assertEqual(out["criteria"], "the total reconciles")
        self.assertEqual(len(notes), 1)

    def test_the_floor_moves_with_the_policy(self):
        out, notes = QP.apply(QP.parse({"ask_at_stakes": "never"}), block(stakes="harm"))
        self.assertEqual(out["human"], "none")
        self.assertEqual(notes, [])
        out, notes = QP.apply(QP.parse({"ask_at_stakes": "rework"}), block(stakes="rework"))
        self.assertEqual(out["human"], "required")

    def test_ai_primary_is_downgraded_to_also_by_default(self):
        out, notes = QP.apply(QP.parse(), block(primary="ai-text", also=["ai-visual", "probe"]),
                              INDEPENDENT)
        self.assertEqual(out["primary"], "probe")
        self.assertEqual(out["also"], ["ai-text", "ai-visual"])
        self.assertEqual(out["human"], "none")
        self.assertEqual(len(notes), 1)
        self.assertIn("primary ai-text → probe", notes[0])
        self.assertIn("AI gating is off", notes[0])

    def test_ai_primary_with_no_other_rung_becomes_person(self):
        out, notes = QP.apply(QP.parse(), block(primary="ai-visual"), INDEPENDENT)
        self.assertEqual(out["primary"], "person")
        self.assertEqual(out["also"], ["ai-visual"])
        self.assertEqual(out["human"], "required")
        self.assertTrue(out["criteria"])
        B.validate_verify(out)

    def test_ai_may_gate_with_an_independent_verifier_when_allowed(self):
        policy = QP.parse({"ai_may_gate_after": "always"})
        verify = block(primary="ai-text", also=["probe"])
        out, notes = QP.apply(policy, verify, INDEPENDENT)
        self.assertEqual(out, verify)
        self.assertEqual(notes, [])
        # Allowed, but the verifier shares the author's lineage (or there is no qa block).
        out, notes = QP.apply(policy, verify, SAME_LINEAGE)
        self.assertEqual(out["primary"], "probe")
        self.assertIn("no verifier outside the author's lineage", notes[0])
        out, notes = QP.apply(policy, verify, None)
        self.assertEqual(out["primary"], "probe")

    def test_a_case_count_gates_ai_and_sampling(self):
        policy = QP.parse({"ai_may_gate_after": 10, "sample_after": 5})
        verify = block(primary="ai-text", also=["probe"], sample="1/10 after 30")
        out, notes = QP.apply(policy, verify, INDEPENDENT, cases=4)
        self.assertEqual(out["primary"], "probe")
        self.assertNotIn("sample", out)
        out, notes = QP.apply(policy, verify, INDEPENDENT, cases=10)
        self.assertEqual(out, verify)
        self.assertEqual(notes, [])

    def test_sample_is_dropped_when_sampling_is_off(self):
        out, notes = QP.apply(QP.parse(), block(sample="1/10 after 30"))
        self.assertNotIn("sample", out)
        self.assertEqual(notes, ["qa policy: sample '1/10 after 30' dropped (sampling is off)"])
        out, notes = QP.apply(QP.parse({"sample_after": "always"}), block(sample="1/10"))
        self.assertEqual(out["sample"], "1/10")
        self.assertEqual(notes, [])

    def test_all_three_rules_stack(self):
        out, notes = QP.apply(QP.parse(), block(primary="ai-text", stakes="money",
                                                sample="1/5"))
        self.assertEqual(out["primary"], "person")
        self.assertEqual(out["also"], ["ai-text"])
        self.assertEqual(out["human"], "required")
        self.assertNotIn("sample", out)
        self.assertEqual(len(notes), 4)
        B.validate_verify(out)

    def test_the_input_block_is_not_mutated(self):
        verify = block(primary="ai-text", stakes="money", sample="1/5")
        copy = dict(verify)
        QP.apply(QP.parse(), verify)
        self.assertEqual(verify, copy)


class CloseAndLineTest(unittest.TestCase):
    def test_closes_automatically_needs_the_switch_and_no_person(self):
        ask, auto = QP.parse(), QP.parse({"verification": "automatic"})
        plain = block(human="none")
        person = block(human="required", criteria="looks right")
        self.assertFalse(QP.closes_automatically(ask, plain))
        self.assertTrue(QP.closes_automatically(auto, plain))
        self.assertFalse(QP.closes_automatically(auto, person))
        self.assertFalse(QP.closes_automatically(auto, None))

    def test_effective_line_states_the_floor_and_its_source(self):
        line = QP.effective_line(QP.parse())
        self.assertEqual(line, "verification ask: every card waits in needs-verification for the "
                               "user to close; human required from stakes money; AI may gate "
                               "never; sampling never")
        line = QP.effective_line(QP.parse({"verification": "automatic", "ask_at_stakes": "never",
                                           "ai_may_gate_after": 30}))
        self.assertTrue(line.startswith("verification automatic (board.yaml): a verifier's pass "
                                        "closes a card whose plan needs no person; no stakes "
                                        "floor; AI may gate after 30 cases; sampling never"), line)
        line = QP.effective_line(QP.parse(None, {"verification": "automatic"}))
        self.assertIn("automatic (Options)", line)


if __name__ == "__main__":       # pragma: no cover
    unittest.main()
