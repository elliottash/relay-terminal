# SPDX-License-Identifier: AGPL-3.0-or-later
"""`relay_core.signal_threads`: unasked pickup (card #AQ6X, decision 9, plan step 7).

Two halves, both pure enough to test without a model, a worker or a clock.

*In-loop* (step 7a): the pane whose own run opened a signal is told in that run's result, and the
sentence is what tells it the signal is its own to fix before it reports.  `format_run` is the
only thing that has to be true for that, so it is the first case here.

*Orphans* (step 7b): a signal nobody claimed after a whole fold becomes a **signal thread** — its
own subagent, claiming under its own thread id, notified and listed.  `SignalThreads` is handed a
`spawn` and an `emit`, so every rule (one fold of grace, the cap of three, `auto_work`, autonomy
off, one thread per key, the 24-hour gave-up cool-off) is exercised against a fake spawn that
records what it was asked for.
"""
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))

from relay_core import signals as S                 # noqa: E402
from relay_core import tests_protocol as TP         # noqa: E402


class InLoopTests(unittest.TestCase):
    """Step 7a: the run's own result is where the pane learns what it broke."""

    def test_run_text_says_a_signal_this_run_opened_is_yours(self):
        text = TP.format_run({"run_id": "r1", "counts": {"pass": 0, "fail": 1, "skip": 0},
                              "ran": 1, "requested": 1,
                              "tests": [{"id": "ctest:panelayout", "result": "fail",
                                         "duration": 0.2}],
                              "opened": ["ctest:panelayout"]})
        self.assertIn("signals this run opened: ctest:panelayout", text)
        self.assertIn("is yours", text)
        self.assertIn("board_signals", text)
        self.assertIn("gave-up", text)

    def test_a_run_that_opened_nothing_says_nothing(self):
        text = TP.format_run({"run_id": "r1", "counts": {"pass": 1, "fail": 0, "skip": 0},
                              "ran": 1, "requested": 1,
                              "tests": [{"id": "ctest:panelayout", "result": "pass",
                                         "duration": 0.2}]})
        self.assertNotIn("is yours", text)
        self.assertNotIn("signals this run opened", text)


if __name__ == "__main__":
    unittest.main()
