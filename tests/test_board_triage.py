# SPDX-License-Identifier: AGPL-3.0-or-later
"""Semantic Board draft output is advisory and strictly drawn from the supplied choices."""
import unittest

from relay_core import board_triage


class ParseTests(unittest.TestCase):
    def test_discards_invented_values_and_overlapping_links(self):
        result = board_triage.parse(
            '{"duplicates":["k7q2","FAKE"],"related":["K7Q2","M3XJ"],'
            '"tab":"features","labels":["feature","unknown"]}',
            [{"id": "K7Q2"}, {"id": "M3XJ"}], ["features", "bugs"], ["feature"])
        self.assertEqual(result, {"duplicates": ["K7Q2"], "related": ["M3XJ"],
                                  "tab": "features", "labels": ["feature"]})

    def test_malformed_reply_has_no_suggestions(self):
        self.assertEqual(board_triage.parse("unrelated text", [], ["features"], []),
                         {"duplicates": [], "related": [], "tab": "", "labels": []})

    def test_one_no_tools_call_returns_validated_semantic_suggestions(self):
        class Provider:
            def complete(self, messages, tools, emit, cancel):
                self_tools = tools
                assert self_tools == []
                return {"content": '{"duplicates":[],"related":["K7Q2"],'
                                   '"tab":"bugs","labels":["bug"]}'}

        events = []
        board_triage.run(Provider(), "r1", "Clicking a path fails",
                         [{"id": "K7Q2", "title": "Open output paths"}],
                         ["features", "bugs"], ["bug"], events.append)
        self.assertEqual(events, [{"event": "board_triage", "id": "r1",
                                   "phase": "semantic", "duplicates": [],
                                   "related": ["K7Q2"], "tab": "bugs", "labels": ["bug"]}])


if __name__ == "__main__":
    unittest.main()
