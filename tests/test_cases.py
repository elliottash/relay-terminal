# SPDX-License-Identifier: AGPL-3.0-or-later
"""The case ledger (#95VZ): `relay_core.cases` — the record, the file, the statistics.

Every test works in a temporary directory; nothing reads the repository's own board.
"""
import datetime as dt
import json
import os
import tempfile
import threading
import unittest
from pathlib import Path

from relay_core import cases


class RecordTests(unittest.TestCase):
    def test_a_record_has_every_field_in_order_and_sane_defaults(self):
        row = cases.new_record("referee-report", served_by="person", cost="3 h",
                               input="Referee-Work/EJ-1234.pdf", card="k7q2")
        self.assertEqual(tuple(row), cases.FIELDS)
        self.assertRegex(row["id"], r"^c-[0-9a-f]{8}$")
        self.assertIsNotNone(cases.parse_when(row["when"]))
        self.assertEqual(row["server_version"], "")
        self.assertEqual(row["card"], "K7Q2")
        self.assertEqual(row["cost"], {"seconds": 10800.0})
        self.assertEqual(row["signal"], {})
        self.assertEqual(row["escalated"], {"yes": False})
        self.assertEqual(row["verdict"], {"result": "pending", "who": "person", "revision": 1})
        self.assertFalse(row["confidential"])
        # A person is their own version.
        self.assertEqual(cases.new_record("person", served_by="person")["server_version"], "person")

    def test_the_row_text_is_one_json_line_in_field_order_without_empty_optionals(self):
        row = cases.new_record("rent", served_by="anthropic/claude-opus-5-5", verdict="pass",
                               signal={"mode": "probe", "result": "pass"}, escalated="the owner")
        line = cases.row_text(row)
        self.assertNotIn("\n", line)
        parsed = json.loads(line)
        self.assertEqual(list(parsed)[:5], ["id", "when", "server", "server_version", "served_by"])
        self.assertNotIn("card", parsed)
        self.assertNotIn("input", parsed)
        self.assertEqual(parsed["escalated"], {"yes": True, "to": "the owner"})
        self.assertEqual(parsed["verdict"]["who"], "anthropic/claude-opus-5-5")

    def test_a_confidential_row_carries_no_input(self):
        row = cases.new_record("referee-report", served_by="person", input="the patient's file",
                               confidential=True)
        self.assertIsNone(row["input"])
        self.assertNotIn("input", json.loads(cases.row_text(row)))
        self.assertNotIn("patient", cases.row_text(row))

    def test_costs_are_parsed_from_phrases_and_objects(self):
        self.assertEqual(cases.parse_cost("45 min"), {"seconds": 2700.0})
        self.assertEqual(cases.parse_cost("20s"), {"seconds": 20.0})
        self.assertEqual(cases.parse_cost("$12"), {"money": 12.0})
        self.assertEqual(cases.parse_cost("1200 tokens"), {"tokens": 1200})
        self.assertEqual(cases.parse_cost("2 d"), {"seconds": 172800.0})
        self.assertEqual(cases.parse_cost({"tokens": 5, "seconds": 1.5, "money": 0}), {"tokens": 5, "seconds": 1.5, "money": 0.0})
        self.assertEqual(cases.parse_cost(None), {})
        for bad in ("a while", "3 fortnights", {"tokens": -1}, {"euros": 3}, 12):
            with self.assertRaises(cases.CaseError):
                cases.parse_cost(bad)

    def test_what_is_refused_is_named(self):
        with self.assertRaisesRegex(cases.CaseError, "server is required"):
            cases.new_record("", served_by="person")
        with self.assertRaisesRegex(cases.CaseError, "never content"):
            cases.new_record("x", served_by="person", input="y" * 201)
        with self.assertRaisesRegex(cases.CaseError, "verdict.result"):
            cases.new_record("x", served_by="person", verdict="maybe")
        with self.assertRaisesRegex(cases.CaseError, "verdict.revision"):
            cases.new_record("x", served_by="person", verdict={"result": "pass", "revision": 0})
        with self.assertRaisesRegex(cases.CaseError, "card must be"):
            cases.new_record("x", served_by="person", card="not a card id")
        with self.assertRaisesRegex(cases.CaseError, "escalated"):
            cases.new_record("x", served_by="person", escalated=3)

    def test_versions(self):
        with tempfile.TemporaryDirectory() as temp:
            manifest = Path(temp) / "SKILL.md"
            manifest.write_text("---\nname: a\n---\nBody\n", encoding="utf-8")
            first = cases.skill_version(manifest)
            self.assertTrue(first.startswith("sha256:") and len(first) == 71)
            manifest.write_text("---\nname: a\n---\nBody changed\n", encoding="utf-8")
            self.assertNotEqual(first, cases.skill_version(manifest))
            self.assertEqual(cases.skill_version(Path(temp) / "missing"), "")
            # A program outside any repository is its path alone; inside one it carries HEAD.
            self.assertEqual(cases.program_version("scripts/land.py"), "scripts/land.py")
            self.assertEqual(cases.program_version("scripts/land.py", temp), "scripts/land.py")

    def test_a_verdict_is_read_from_the_first_decisive_word(self):
        self.assertEqual(cases.verdict_from_text("Pass. Every point is addressed."), "pass")
        self.assertEqual(cases.verdict_from_text("**FAIL**: the gate let it through"), "fail")
        self.assertEqual(cases.verdict_from_text("The tests passed; the strip reads."), "pass")
        self.assertEqual(cases.verdict_from_text("Failed to reproduce, so pass"), "fail")
        self.assertEqual(cases.verdict_from_text("Looks fine to me"), "pending")
        self.assertEqual(cases.verdict_from_text(""), "pending")


class FileTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.board = Path(self.tmp.name) / "issues"

    def test_append_creates_the_file_and_read_gives_the_rows_back_oldest_first(self):
        self.assertEqual(cases.read(self.board), [])
        first = cases.append(self.board, cases.new_record("a", served_by="person"))
        second = cases.append(self.board, cases.new_record("b", served_by="person"))
        path = self.board / "cases.jsonl"
        self.assertTrue(path.is_file())
        self.assertEqual(path.read_text(encoding="utf-8").count("\n"), 2)
        self.assertEqual([r["id"] for r in cases.read(self.board)], [first["id"], second["id"]])
        self.assertEqual(cases.count(self.board), 2)

    def test_a_file_without_a_trailing_newline_still_gets_one_row_per_line(self):
        path = self.board / "cases.jsonl"
        path.parent.mkdir(parents=True)
        path.write_bytes(cases.row_text(cases.new_record("a", served_by="person")).encode("utf-8"))
        cases.append(self.board, cases.new_record("b", served_by="person"))
        self.assertEqual([r["server"] for r in cases.read(self.board)], ["a", "b"])

    def test_concurrent_appends_all_land_under_the_lock(self):
        errors = []

        def worker(n):
            try:
                for i in range(20):
                    cases.append(self.board, cases.new_record(f"s{n}", served_by="person", input=f"{n}/{i}"))
            except Exception as exc:                     # pragma: no cover - the failure we test for
                errors.append(exc)

        threads = [threading.Thread(target=worker, args=(n,)) for n in range(6)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(errors, [])
        rows = cases.read(self.board)
        self.assertEqual(len(rows), 120)
        self.assertEqual(len({r["id"] for r in rows}), 120)
        for line in (self.board / "cases.jsonl").read_text(encoding="utf-8").splitlines():
            json.loads(line)                              # no interleaved halves

    def test_read_filters_by_server_and_card_and_takes_the_last_n(self):
        for i in range(5):
            cases.append(self.board, cases.new_record("a" if i % 2 else "b", served_by="person",
                                                      card="K7Q2" if i < 2 else None, input=str(i)))
        self.assertEqual([r["input"] for r in cases.read(self.board, server="a")], ["1", "3"])
        self.assertEqual([r["input"] for r in cases.read(self.board, card="k7q2")], ["0", "1"])
        self.assertEqual([r["input"] for r in cases.read(self.board, limit=2)], ["3", "4"])
        self.assertEqual(cases.read(self.board, limit=0), [])

    def test_confidential_rows_can_be_left_out_and_bad_lines_are_skipped(self):
        cases.append(self.board, cases.new_record("a", served_by="person", input="open"))
        cases.append(self.board, cases.new_record("a", served_by="person", input="x", confidential=True))
        with (self.board / "cases.jsonl").open("a", encoding="utf-8") as handle:
            handle.write("not json\n{\"id\": 3}\n[1, 2]\n")
        self.assertEqual(len(cases.read(self.board)), 2)
        visible = cases.read(self.board, include_confidential=False)
        self.assertEqual([r["input"] for r in visible], ["open"])


class StatisticsTests(unittest.TestCase):
    NOW = dt.datetime(2026, 9, 23, 12, 0, tzinfo=dt.timezone.utc)

    def row(self, server, days_ago, verdict="pending", served_by="person"):
        when = (self.NOW - dt.timedelta(days=days_ago)).strftime("%Y-%m-%dT%H:%M:%SZ")
        return cases.new_record(server, served_by=served_by, verdict=verdict, when=when)

    def test_stats_count_last_served_pass_rate_and_stale_by_rot(self):
        rows = [self.row("s", 40, "pass"), self.row("s", 10, "fail"), self.row("s", 2),
                self.row("other", 1, "pass")]
        out = cases.stats(rows, "s", {"rot": "medium"}, now=self.NOW)
        self.assertEqual(out["cases"], 3)
        self.assertEqual(out["last_served"], rows[2]["when"])
        self.assertEqual(out["pass_rate_30"], 0.5)
        self.assertTrue(out["stale"])                       # medium: 30 days; the last pass was 40 ago
        self.assertFalse(cases.stats(rows, "s", {"rot": "low"}, now=self.NOW)["stale"])   # 90 days
        self.assertTrue(cases.stats(rows, "s", {"rot": "high"}, now=self.NOW)["stale"])   # 7 days
        # No rot in the profile reads as low; no profile at all likewise.
        self.assertFalse(cases.stats(rows, "s", {}, now=self.NOW)["stale"])
        self.assertFalse(cases.stats(rows, "s", None, now=self.NOW)["stale"])
        # A skill with cases and no pass at all is stale; one with no cases is not.
        self.assertTrue(cases.stats([self.row("t", 1, "fail")], "t", {"rot": "low"}, now=self.NOW)["stale"])
        self.assertEqual(cases.stats(rows, "none", {"rot": "high"}, now=self.NOW),
                         {"cases": 0, "last_served": None, "pass_rate_30": None, "stale": False})

    def test_the_pass_rate_is_over_the_last_thirty_decided_cases(self):
        rows = [self.row("s", 100 - i, "fail") for i in range(10)] + \
               [self.row("s", 50 - i, "pass") for i in range(30)]
        self.assertEqual(cases.stats(rows, "s", {}, now=self.NOW)["pass_rate_30"], 1.0)
        self.assertEqual(cases.verified_count(rows, "s"), 30)
        self.assertEqual(cases.verified_count(rows), 30)
        self.assertEqual(cases.verified_count(rows, "other"), 0)

    def test_the_third_person_served_case_in_ninety_days_is_the_hint_and_only_the_third(self):
        rows = [self.row("referee", 100), self.row("referee", 20), self.row("referee", 5)]
        self.assertFalse(cases.third_case_hint(rows, "referee", now=self.NOW))     # one is too old
        rows.append(self.row("referee", 1))
        self.assertTrue(cases.third_case_hint(rows, "referee", now=self.NOW))
        rows.append(self.row("referee", 0))
        self.assertFalse(cases.third_case_hint(rows, "referee", now=self.NOW))     # the fourth: said once
        # Cases a model served do not count towards a "build a server" line.
        model = [self.row("rent", i, served_by="anthropic/claude-opus-5-5") for i in range(3)]
        self.assertFalse(cases.third_case_hint(model, "rent", now=self.NOW))
        self.assertIn("/deliver", cases.hint_line("referee"))


if __name__ == "__main__":
    unittest.main()
