# SPDX-License-Identifier: AGPL-3.0-or-later
"""The JSONL execution store, its JUnit ingest, the statistics fold and `check_card`.

Every fixture is hand-written here: the CTest JUnit sample is in the shape `ctest --output-junit`
documents and really writes (root `<testsuite>`, `classname` equal to the test name,
`status="run"`), and the unittest one in the shape `relay_core.junit_runner` writes.  No network,
no build, no subprocess.
"""
import json
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))

from relay_core import board as B          # noqa: E402
from relay_core import test_history as H   # noqa: E402
from relay_core import test_probe as P     # noqa: E402


def at(minutes: int) -> str:
    """A timestamp `minutes` after a fixed epoch, so ordering in a test is obvious."""
    base = datetime(2026, 9, 1, 12, 0, 0, tzinfo=timezone.utc)
    return (base + timedelta(minutes=minutes)).strftime("%Y-%m-%dT%H:%M:%SZ")


def ex(test_id, result="pass", *, minutes=0, duration=0.01, commit="c0", host="spark",
       run_id="r", source_hash=""):
    return H.Execution(ts=at(minutes), id=test_id, result=result, duration=duration,
                       runner=test_id.split(":", 1)[0], run_id=run_id, commit=commit,
                       host=host, source_hash=source_hash)


def discovered(test_id, **kw):
    runner, rest = test_id.split(":", 1)
    out = {"id": test_id, "name": rest, "runner": runner, "file": kw.pop("file", ""),
           "labels": [], "invocation": kw.pop("invocation", rest)}
    out.update(kw)
    return out


CTEST_JUNIT = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="Linux-c++"
\ttests="3"
\tfailures="1"
\tdisabled="0"
\tskipped="1"
\thostname="spark-dcc9"
\ttime="0"
\ttimestamp="2026-09-20T15:20:25"
\t>
\t<testcase name="prompthistory" classname="prompthistory" time="0.0191262" status="run">
\t\t<properties/>
\t\t<system-out>PASS   : PromptHistoryTests::trimKeepsTheNewest()</system-out>
\t</testcase>
\t<testcase name="panelayout" classname="panelayout" time="1.5" status="fail">
\t\t<failure type="Failed" message="Assertion failed: rows == 3">
FAIL!  : PaneLayoutTests::split() Compared values are not the same
   Actual   (rows): 2
   Expected (3)   : 3
\t\t</failure>
\t</testcase>
\t<testcase name="disabled-thing" classname="disabled-thing" time="0" status="notrun">
\t\t<skipped/>
\t</testcase>
</testsuite>
"""

UNITTEST_JUNIT = """<?xml version='1.0' encoding='utf-8'?>
<testsuite name="unittest" tests="2" failures="1" errors="0" skipped="0" disabled="0"
 hostname="spark-dcc9" time="0.2" timestamp="2026-09-20T15:30:00">
<testcase classname="tests.test_board.CardTests" name="test_roundtrip" time="0.01"
 status="run" file="tests/test_board.py" line="12" />
<testcase classname="tests.test_board.CardTests" name="test_merge" time="0.19" status="fail"
 file="tests/test_board.py" line="20"><failure message="1 != 2">Traceback…</failure></testcase>
</testsuite>
"""


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "tests" / "history.jsonl"
        self.addCleanup(self.tmp.cleanup)

    def test_append_and_read_round_trip(self):
        self.assertEqual(H.read(self.path), [])           # absent store is empty, not an error
        written = H.append([ex("ctest:a", minutes=1), ex("ctest:b", "fail", minutes=2)],
                           self.path)
        self.assertEqual(written, 2)
        rows = H.read(self.path)
        self.assertEqual([r.id for r in rows], ["ctest:a", "ctest:b"])
        self.assertEqual(rows[1].result, "fail")
        self.assertEqual(rows[0].host, "spark")

    def test_a_batch_is_one_write_and_appends_never_rewrite(self):
        H.append([ex("ctest:a", minutes=1)], self.path)
        first = self.path.read_bytes()
        H.append([ex("ctest:b", minutes=2)], self.path)
        self.assertTrue(self.path.read_bytes().startswith(first))
        self.assertEqual(len(H.read(self.path)), 2)

    def test_every_line_is_one_json_object_in_the_wire_shape(self):
        H.append([ex("unittest:tests.t.C.test_x", minutes=1)], self.path)
        line = self.path.read_text(encoding="utf-8").strip()
        row = json.loads(line)
        self.assertEqual(set(row) >= {"ts", "run_id", "id", "runner", "result", "duration",
                                      "commit", "host"}, True)
        self.assertNotIn("\n", line)

    def test_a_torn_last_line_is_skipped_not_fatal(self):
        H.append([ex("ctest:a", minutes=1), ex("ctest:b", minutes=2)], self.path)
        with open(self.path, "a", encoding="utf-8") as handle:
            handle.write('{"ts": "2026-09-01T13:00:00Z", "id": "ctest:c", "resu')
        rows = H.read(self.path)
        self.assertEqual([r.id for r in rows], ["ctest:a", "ctest:b"])

    def test_garbage_in_the_middle_is_skipped(self):
        H.append([ex("ctest:a", minutes=1)], self.path)
        with open(self.path, "a", encoding="utf-8") as handle:
            handle.write("not json\n")
        H.append([ex("ctest:b", minutes=2)], self.path)
        self.assertEqual([r.id for r in H.read(self.path)], ["ctest:a", "ctest:b"])

    def test_retention_keeps_the_union_of_both_rules(self):
        old = datetime.now(timezone.utc) - timedelta(days=400)
        rows = []
        for i in range(5):                                # ancient, but the only runs it has
            rows.append(H.Execution(ts=(old + timedelta(minutes=i)).strftime(
                "%Y-%m-%dT%H:%M:%SZ"), id="ctest:rare"))
        for i in range(10):                               # recent
            rows.append(H.Execution(ts=datetime.now(timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%SZ"), id="ctest:busy", run_id=f"r{i:02d}"))
        H.append(rows, self.path)
        dropped = H.prune(self.path, per_test=200)
        self.assertEqual(dropped, 0)                      # the rare test is kept by the count rule
        dropped = H.prune(self.path, per_test=2)
        kept = H.read(self.path)
        self.assertEqual(dropped, 3)                      # only the 3 oldest ancient ones go
        self.assertEqual(sum(1 for r in kept if r.id == "ctest:rare"), 2)
        self.assertEqual(sum(1 for r in kept if r.id == "ctest:busy"), 10)

    def test_read_can_be_limited_and_filtered(self):
        H.append([ex("ctest:a", minutes=i) for i in range(5)]
                 + [ex("ctest:b", minutes=9)], self.path)
        self.assertEqual(len(H.read(self.path, limit=2)), 2)
        self.assertEqual({r.id for r in H.read(self.path, ids=["ctest:b"])}, {"ctest:b"})

    def test_default_path_is_under_the_boards_private_root(self):
        project = Path(self.tmp.name) / "proj"
        (project / ".switchboard").mkdir(parents=True)
        (project / ".switchboard" / B.BOARD_CONFIG).write_text("tabs: []\n", encoding="utf-8")
        path = H.default_path(project)
        self.assertEqual(path, project / ".switchboard" / ".private" / "tests" / "history.jsonl")
        # .private/ is what board.GITIGNORE_TEXT keeps out of git, so the store is gitignored.
        self.assertIn(".private/", B.GITIGNORE_TEXT)

    def test_default_path_for_a_project_with_no_board_yet(self):
        project = Path(self.tmp.name) / "bare"
        project.mkdir()
        self.assertEqual(H.default_path(project).parts[-4:],
                         (B.DEFAULT_BOARD_FOLDER, ".private", "tests", "history.jsonl"))


class IngestTests(unittest.TestCase):
    def test_a_real_ctest_output_junit_sample(self):
        rows = H.ingest_junit(CTEST_JUNIT, runner="ctest", commit="abc1234", run_id="run-1",
                              host="spark", ts=at(0))
        by_id = {r.id: r for r in rows}
        self.assertEqual(sorted(by_id), ["ctest:disabled-thing", "ctest:panelayout",
                                         "ctest:prompthistory"])
        self.assertEqual(by_id["ctest:prompthistory"].result, "pass")
        self.assertAlmostEqual(by_id["ctest:prompthistory"].duration, 0.0191262, places=6)
        self.assertEqual(by_id["ctest:panelayout"].result, "fail")
        self.assertEqual(by_id["ctest:panelayout"].message, "Assertion failed: rows == 3")
        self.assertIn("Compared values are not the same", by_id["ctest:panelayout"].excerpt)
        self.assertEqual(by_id["ctest:disabled-thing"].result, "skip")
        self.assertEqual(by_id["ctest:panelayout"].commit, "abc1234")
        self.assertEqual(by_id["ctest:panelayout"].host, "spark")

    def test_the_unittest_writers_shape(self):
        rows = H.ingest_junit(UNITTEST_JUNIT, runner="unittest", ts=at(0))
        by_id = {r.id: r for r in rows}
        self.assertEqual(sorted(by_id), ["unittest:tests.test_board.CardTests.test_merge",
                                         "unittest:tests.test_board.CardTests.test_roundtrip"])
        self.assertEqual(by_id["unittest:tests.test_board.CardTests.test_merge"].result, "fail")
        self.assertEqual(by_id["unittest:tests.test_board.CardTests.test_merge"].message,
                         "1 != 2")

    def test_ingest_takes_a_path_bytes_or_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "j.xml"
            path.write_text(CTEST_JUNIT, encoding="utf-8")
            self.assertEqual(len(H.ingest_junit(path, runner="ctest")), 3)
            self.assertEqual(len(H.ingest_junit(CTEST_JUNIT.encode(), runner="ctest")), 3)
        self.assertEqual(H.ingest_junit("/no/such/file.xml", runner="ctest"), [])
        self.assertEqual(H.ingest_junit("<not xml", runner="ctest"), [])

    def test_a_testsuites_wrapper_is_accepted_too(self):
        wrapped = ("<testsuites>" + CTEST_JUNIT.split("?>", 1)[1] + "</testsuites>")
        self.assertEqual(len(H.ingest_junit(wrapped, runner="ctest")), 3)

    def test_source_hashes_travel_into_the_executions(self):
        rows = H.ingest_junit(CTEST_JUNIT, runner="ctest",
                              source_hashes={"ctest:panelayout": "sha256:aa"})
        by_id = {r.id: r for r in rows}
        self.assertEqual(by_id["ctest:panelayout"].source_hash, "sha256:aa")
        self.assertEqual(by_id["ctest:prompthistory"].source_hash, "")

    def test_ctest_cost_data_is_a_weak_hint(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CTestCostData.txt"
            path.write_text("board 24 0.0682278\nrelay-engine-tests 4 3.79229\n---\n"
                            "failed-test-name\n", encoding="utf-8")
            hints = H.ingest_ctest_cost(path)
        self.assertEqual(hints["ctest:board"], {"runs": 24, "mean": 0.0682278})
        self.assertNotIn("ctest:failed-test-name", hints)
        self.assertEqual(H.ingest_ctest_cost("/no/such/cost.txt"), {})


class StatisticsTests(unittest.TestCase):
    def test_percentile_is_nearest_rank(self):
        values = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.assertEqual(H.percentile(values, 50), 5)     # ceil(0.5*10) = 5 -> the 5th value
        self.assertEqual(H.percentile(values, 95), 10)    # ceil(0.95*10) = 10
        self.assertEqual(H.percentile([7], 95), 7)
        self.assertEqual(H.percentile([], 50), 0.0)

    def test_flake_score_weights_recent_transitions_more(self):
        steady = ["pass"] * 10
        self.assertEqual(H.flake_score(steady), 0.0)
        self.assertEqual(H.flake_score(["fail"] * 10), 0.0)          # broken, not flaky
        recent = ["pass"] * 8 + ["fail", "pass"]
        old = ["pass", "fail"] + ["pass"] * 8
        self.assertGreater(H.flake_score(recent), H.flake_score(old))
        self.assertAlmostEqual(H.flake_score(["pass", "fail"]), 1.0, places=3)

    def test_skips_carry_no_flake_signal(self):
        self.assertEqual(H.flake_score(["pass", "skip", "pass"]), 0.0)

    def test_reliability_is_buildkites_formula(self):
        rows = [ex("ctest:a", "pass", minutes=i) for i in range(9)]
        rows.append(ex("ctest:a", "fail", minutes=9))
        out = H.records([discovered("ctest:a")], rows)[0]
        self.assertEqual(out["runs"], 10)
        self.assertEqual(out["pass"], 9)
        self.assertEqual(out["fail"], 1)
        self.assertEqual(out["reliability"], 90.0)

    def test_percentiles_and_last_failure(self):
        rows = [ex("ctest:a", "pass", minutes=i, duration=0.1) for i in range(9)]
        rows.append(H.Execution(ts=at(9), id="ctest:a", result="fail", duration=9.0,
                                commit="deadbeef", message="broke", excerpt="…stack…"))
        out = H.records([discovered("ctest:a")], rows)[0]
        self.assertEqual(out["duration_p50"], 0.1)
        self.assertEqual(out["duration_p95"], 9.0)
        self.assertEqual(out["duration_last"], 9.0)
        self.assertEqual(out["last_result"], "fail")
        self.assertEqual(out["last_failure"]["commit"], "deadbeef")
        self.assertEqual(out["last_failure"]["message"], "broke")

    def test_flaky_at_the_same_commit_even_with_a_low_score(self):
        rows = [ex("ctest:a", "pass", minutes=0, commit="c1"),
                ex("ctest:a", "fail", minutes=1, commit="c1")]
        out = H.records([discovered("ctest:a")], rows)[0]
        self.assertTrue(out["flaky"])
        other = H.records([discovered("ctest:b")],
                          [ex("ctest:b", "pass", minutes=0, commit="c1"),
                           ex("ctest:b", "fail", minutes=1, commit="c2")])[0]
        self.assertFalse(other["flaky"])                   # one fail after a change is a break

    def test_flaky_by_score(self):
        rows = [ex("ctest:a", "pass" if i % 2 else "fail", minutes=i, commit=f"c{i}")
                for i in range(10)]
        out = H.records([discovered("ctest:a")], rows)[0]
        self.assertGreaterEqual(out["flake_score"], H.FLAKE_FLAKY)
        self.assertTrue(out["flaky"])

    def test_slow_is_the_top_decile_and_at_least_a_second(self):
        rows = []
        for i in range(10):
            rows += [ex(f"ctest:fast{i}", minutes=i, duration=0.01)]
        rows += [ex("ctest:slow", minutes=11, duration=4.0)]
        found = [discovered(f"ctest:fast{i}") for i in range(10)] + [discovered("ctest:slow")]
        out = {r["id"]: r for r in H.records(found, rows)}
        self.assertTrue(out["ctest:slow"]["slow"])
        self.assertFalse(out["ctest:fast0"]["slow"])

    def test_a_fast_suites_slowest_test_is_not_slow(self):
        rows = [ex(f"ctest:t{i}", minutes=i, duration=0.01 * (i + 1)) for i in range(10)]
        out = {r["id"]: r for r in
               H.records([discovered(f"ctest:t{i}") for i in range(10)], rows)}
        self.assertFalse(any(r["slow"] for r in out.values()))

    def test_a_p95_regression_is_slow_even_in_a_slow_suite(self):
        rows = [ex("ctest:a", minutes=i, duration=2.0) for i in range(10)]
        rows += [ex("ctest:a", minutes=10 + i, duration=9.0) for i in range(5)]
        peers = [ex(f"ctest:peer{i}", minutes=i, duration=20.0) for i in range(12)]
        found = [discovered("ctest:a")] + [discovered(f"ctest:peer{i}") for i in range(12)]
        out = {r["id"]: r for r in H.records(found, rows + peers)}
        self.assertFalse(out["ctest:a"]["duration_p95"] >= H.percentile(
            [r["duration_p95"] for r in out.values()], H.SLOW_DECILE))
        self.assertTrue(out["ctest:a"]["slow"])            # …but its own p95 tripled


class SourceHashResetTests(unittest.TestCase):
    def test_history_resets_when_the_source_hash_changes(self):
        old, new = "sha256:old", "sha256:new"
        rows = [ex("ctest:a", "fail", minutes=i, duration=5.0, commit=f"c{i}",
                   source_hash=old) for i in range(6)]
        before = H.records([discovered("ctest:a", source_hash=old)], rows)[0]
        self.assertEqual(before["runs"], 6)
        self.assertEqual(before["stale"], [])

        after = H.records([discovered("ctest:a", source_hash=new)], rows)[0]
        self.assertEqual(after["runs"], 0)                 # the old test's runs are not this one's
        self.assertNotIn("reliability", after)
        self.assertNotIn("duration_p95", after)
        self.assertEqual(after["flake_score"], 0.0)
        self.assertIn("edited", after["stale"])
        self.assertIn("never-run", after["stale"])
        self.assertEqual(after["first_seen"], rows[0].ts)  # the test is not new, only its source

    def test_edited_is_said_once_and_the_new_runs_count(self):
        old, new = "sha256:old", "sha256:new"
        rows = [ex("ctest:a", "fail", minutes=i, source_hash=old) for i in range(4)]
        rows += [ex("ctest:a", "pass", minutes=10 + i, source_hash=new) for i in range(3)]
        out = H.records([discovered("ctest:a", source_hash=new)], rows)[0]
        self.assertEqual(out["runs"], 3)
        self.assertEqual(out["pass"], 3)
        self.assertEqual(out["reliability"], 100.0)
        self.assertNotIn("edited", out["stale"])
        self.assertEqual(len(out["history"]), 3)

    def test_executions_without_a_hash_are_always_counted(self):
        rows = [ex("ctest:a", minutes=i) for i in range(3)]
        out = H.records([discovered("ctest:a", source_hash="sha256:whatever")], rows)[0]
        self.assertEqual(out["runs"], 3)


class RecordsAndSummaryTests(unittest.TestCase):
    def test_a_test_with_history_but_no_discovery_is_gone(self):
        out = {r["id"]: r for r in H.records([discovered("ctest:a")],
                                             [ex("ctest:a"), ex("ctest:removed", minutes=1)])}
        self.assertIn("gone", out["ctest:removed"]["stale"])
        self.assertEqual(out["ctest:a"]["stale"], [])

    def test_never_run_and_skipped_forever(self):
        found = [discovered("ctest:a"), discovered("ctest:off", disabled=True),
                 discovered("ctest:skipper")]
        rows = [ex("ctest:a"), ex("ctest:skipper", "skip", minutes=1),
                ex("ctest:skipper", "skip", minutes=2)]
        out = {r["id"]: r for r in H.records(found, rows)}
        self.assertIn("never-run", out["ctest:off"]["stale"])
        self.assertIn("skipped-forever", out["ctest:off"]["stale"])
        self.assertIn("skipped-forever", out["ctest:skipper"]["stale"])
        self.assertEqual(out["ctest:a"]["stale"], [])

    def test_history_is_newest_first_and_capped(self):
        rows = [ex("ctest:a", minutes=i) for i in range(40)]
        out = H.records([discovered("ctest:a")], rows)[0]
        self.assertEqual(len(out["history"]), H.MAX_HISTORY_CELLS)
        self.assertEqual(out["history"][0]["ts"], at(39))
        self.assertGreater(out["history"][0]["ts"], out["history"][1]["ts"])

    def test_cards_index_is_reversed_onto_the_records(self):
        found = [discovered("ctest:panelayout")]
        index = {"SDXE": ["- `ctest -R panelayout` — tests/panelayout_test.cpp"],
                 "BXCN": ["ctest:panelayout"]}
        out = H.records(found, [], index)[0]
        self.assertEqual(sorted(out["cards"]), ["BXCN", "SDXE"])

    def test_records_accept_the_probes_own_result(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp)
            (project / "tests").mkdir()
            (project / "tests" / "test_x.py").write_text(
                "import unittest\n\n\nclass T(unittest.TestCase):\n"
                "    def test_a(self):\n        pass\n", encoding="utf-8")
            probe = P.discover(project, ctest="/nonexistent/ctest")
        out = H.records(probe, [])
        self.assertEqual([r["id"] for r in out], ["unittest:tests.test_x.T.test_a"])
        self.assertIn("never-run", out[0]["stale"])
        self.assertTrue(out[0]["source_hash"].startswith("sha256:"))

    def test_summary_line_is_the_nextest_header(self):
        found = [discovered(f"ctest:t{i}") for i in range(5)]
        rows = [ex("ctest:t0", "pass", minutes=0, duration=1.0),
                ex("ctest:t1", "pass", minutes=1, duration=2.0),
                ex("ctest:t2", "fail", minutes=2, duration=0.5),
                ex("ctest:t3", "skip", minutes=3, duration=0.0)]
        out = H.summary(H.records(found, rows))
        self.assertEqual(out["total"], 5)
        self.assertEqual(out["passed"], 2)
        self.assertEqual(out["failed"], 1)
        self.assertEqual(out["skipped"], 1)
        self.assertEqual(out["never_run"], 1)
        self.assertIn("5 tests", out["line"])
        self.assertIn("2 passed", out["line"])
        self.assertIn("1 never run", out["line"])
        self.assertRegex(out["line"], r" (ms|s|m)$")
        self.assertIn(" · ", out["line"])

    def test_summary_line_never_says_zero_seconds(self):
        self.assertEqual(H.format_seconds(0.004), "4 ms")
        self.assertEqual(H.format_seconds(1.44), "1.4 s")
        self.assertEqual(H.format_seconds(150), "2.5 m")

    def test_summary_of_nothing(self):
        out = H.summary([])
        self.assertEqual(out["total"], 0)
        self.assertEqual(out["line"], "0 tests · 0 passed · 0 ms")

    def test_records_are_json_serialisable(self):
        out = H.records([discovered("ctest:a", file="tests/a_test.cpp", line=3)],
                        [ex("ctest:a", "fail", minutes=1)])
        json.dumps(out)
        self.assertNotIn("_regressed", out[0])


class CardLineTests(unittest.TestCase):
    def test_the_three_spellings(self):
        ctest = H.parse_test_line("- `ctest -R panelayout` — tests/panelayout_test.cpp")
        self.assertEqual(ctest["id"], "ctest:panelayout")
        self.assertEqual(ctest["runner"], "ctest")
        self.assertEqual(ctest["file"], "tests/panelayout_test.cpp")

        py = H.parse_test_line("- `tests/test_board_chat.py::BoardChatTests::test_steer`")
        self.assertEqual(py["id"], "unittest:tests.test_board_chat.BoardChatTests.test_steer")
        self.assertEqual(py["file"], "tests/test_board_chat.py")

        manual = H.parse_test_line("- manual: docs/qa_evidence/2026-09-20-thing/")
        self.assertEqual(manual["runner"], "manual")
        self.assertEqual(manual["id"], "manual:docs/qa_evidence/2026-09-20-thing/")

    def test_forgiving_about_bullets_backticks_and_dashes(self):
        for line in ("* `ctest -R panelayout`",
                     "1. ctest -R panelayout",
                     "- `ctest -R ^panelayout$` -- tests/panelayout_test.cpp",
                     "  - `ctest --tests-regex panelayout`"):
            self.assertEqual(H.parse_test_line(line)["id"], "ctest:panelayout", line)

    def test_prose_is_not_a_test(self):
        self.assertIsNone(H.parse_test_line("These all pass on spark and sphinxpad."))
        self.assertIsNone(H.parse_test_line(""))
        self.assertIsNone(H.parse_test_line("### Check 2026-09-20"))

    def test_a_whole_file_or_class_resolves_to_a_prefix_id(self):
        self.assertEqual(H.parse_test_line("- `tests/test_board.py`")["id"],
                         "unittest:tests.test_board")
        self.assertEqual(H.parse_test_line("- `tests/test_board.py::CardTests`")["id"],
                         "unittest:tests.test_board.CardTests")


class CheckCardTests(unittest.TestCase):
    def records_for(self, *specs):
        found, rows = [], []
        for spec in specs:
            found.append(discovered(spec["id"], file=spec.get("file", ""),
                                    disabled=spec.get("disabled", False),
                                    source_hash=spec.get("source_hash", "")))
            for i, result in enumerate(spec.get("results", [])):
                rows.append(ex(spec["id"], result, minutes=i,
                               duration=spec.get("duration", 0.01),
                               commit=spec.get("commits", [f"c{i}"] * 99)[i],
                               source_hash=spec.get("run_hash", spec.get("source_hash", ""))))
        return H.records(found, rows)

    def test_silent_when_nothing_is_wrong(self):
        records = self.records_for({"id": "ctest:panelayout", "file": "tests/panelayout_test.cpp",
                                    "results": ["pass", "pass", "pass"]})
        out = H.check_card(["- `ctest -R panelayout` — tests/panelayout_test.cpp"],
                           ["src/PaneLayout.cpp"], records)
        self.assertEqual(out["findings"], [])
        self.assertEqual(out["actions"], [])

    def test_a_listed_test_that_does_not_exist_is_retired(self):
        # #PR4Q: "gone" was a failure that blocked the landing; a retired check is a notice
        # beside a `not-applicable` status, with the action that replaces it.
        out = H.check_card(["- `ctest -R vanished`"], [], self.records_for(
            {"id": "ctest:panelayout", "results": ["pass"]}))
        self.assertEqual([f["verdict"] for f in out["findings"]], ["retired"])
        self.assertEqual(out["findings"][0]["severity"], "notice")
        self.assertIn("Replace retired check", out["actions"])
        self.assertEqual([s["status"] for s in out["statuses"]], [H.STATUS_NA])
        self.assertTrue(out["statuses"][0]["retired"])

    def test_a_test_with_history_but_gone_from_discovery(self):
        records = H.records([], [ex("ctest:old", minutes=1)])
        out = H.check_card(["- `ctest -R old`"], [], records)
        self.assertEqual([f["verdict"] for f in out["findings"]], ["retired"])
        self.assertEqual([s["status"] for s in out["statuses"]], [H.STATUS_NA])

    def test_never_run_asks_to_run_them(self):
        out = H.check_card(["- `ctest -R fresh`"], [],
                           self.records_for({"id": "ctest:fresh", "results": []}))
        self.assertEqual([f["verdict"] for f in out["findings"]], ["never-run"])
        self.assertEqual(out["actions"], ["Run these"])

    def test_skipped_forever(self):
        out = H.check_card(["- `ctest -R off`"], [],
                           self.records_for({"id": "ctest:off", "disabled": True,
                                             "results": ["skip", "skip"]}))
        verdicts = [f["verdict"] for f in out["findings"]]
        self.assertIn("skipped-forever", verdicts)

    def test_flaky_and_slow_are_reported_with_their_numbers(self):
        records = self.records_for(
            {"id": "ctest:flip", "results": ["pass", "fail"] * 5,
             "commits": ["c0"] * 10, "duration": 4.0},
            {"id": "ctest:quick", "results": ["pass"] * 10, "duration": 0.01})
        out = H.check_card(["- `ctest -R flip`"], [], records)
        verdicts = [f["verdict"] for f in out["findings"]]
        self.assertIn("flaky", verdicts)
        self.assertIn("slow", verdicts)
        self.assertIn("Open the failing one", out["actions"])

    def test_orphaned_when_no_listed_test_is_named_after_a_changed_file(self):
        records = self.records_for({"id": "ctest:panelayout",
                                    "file": "tests/panelayout_test.cpp",
                                    "results": ["pass"]})
        out = H.check_card(["- `ctest -R panelayout` — tests/panelayout_test.cpp"],
                           ["backend/relay_core/hosted.py"], records)
        self.assertEqual([f["verdict"] for f in out["findings"]], ["orphaned"])
        self.assertIn("Add the tests this card's commits touched", out["actions"])

    def test_the_naming_convention_counts_as_coverage_enough(self):
        records = self.records_for({"id": "unittest:tests.test_hosted.T.test_a",
                                    "file": "tests/test_hosted.py", "results": ["pass"]})
        out = H.check_card(["- `tests/test_hosted.py::T::test_a`"],
                           ["backend/relay_core/hosted.py"], records)
        self.assertEqual(out["findings"], [])

    def test_a_card_that_changed_files_and_lists_nothing(self):
        out = H.check_card([], ["src/Pane.h"], [])
        self.assertEqual([f["verdict"] for f in out["findings"]], ["orphaned"])

    def test_at_most_three_actions(self):
        records = self.records_for(
            {"id": "ctest:fresh", "results": []},
            {"id": "ctest:flip", "results": ["pass", "fail"] * 5, "commits": ["c0"] * 10})
        out = H.check_card(["- `ctest -R fresh`", "- `ctest -R flip`", "- `ctest -R vanished`"],
                           ["docs/UNRELATED.md"], records)
        self.assertLessEqual(len(out["actions"]), 3)
        self.assertTrue(out["findings"])

    def test_a_line_naming_many_tests_reports_each_verdict_once(self):
        discovered = [P.DiscoveredTest(id=f"unittest:tests.test_x.T.test_{i}", name=f"test_{i}",
                                       runner="unittest", file="tests/test_x.py",
                                       invocation=f"tests/test_x.py::T::test_{i}").to_dict()
                      for i in range(5)]
        recs = H.records(discovered, [])
        out = H.check_card(["`tests/test_x.py`"], [], recs)
        never = [f for f in out["findings"] if f["verdict"] == "never-run"]
        self.assertEqual(len(never), 1)
        self.assertEqual(never[0]["test"], "unittest:tests.test_x")
        self.assertIn("5 of 5 never ran here", never[0]["message"])
        self.assertIn("test_0, test_1, test_2…", never[0]["message"])
        self.assertIn("Run these", out["actions"])

    def test_a_whole_file_line_resolves_to_every_case_in_it(self):
        records = self.records_for(
            {"id": "unittest:tests.test_board.CardTests.test_a", "file": "tests/test_board.py",
             "results": ["pass"]},
            {"id": "unittest:tests.test_board.CardTests.test_b", "file": "tests/test_board.py",
             "results": []})
        found = H.resolve(H.parse_test_line("- `tests/test_board.py`"), records)
        self.assertEqual(len(found), 2)
        out = H.check_card(["- `tests/test_board.py`"], [], records)
        self.assertEqual([f["verdict"] for f in out["findings"]], ["never-run"])
        # A line naming many tests reports the verdict once, under the line's own id.
        self.assertEqual(out["findings"][0]["test"], "unittest:tests.test_board")
        self.assertIn("1 of 2 never ran here (test_b)", out["findings"][0]["message"])

    def test_a_ctest_name_is_a_regex_exactly_as_ctest_treats_it(self):
        records = self.records_for({"id": "ctest:board", "results": ["pass"]},
                                   {"id": "ctest:boardpane", "results": []})
        found = H.resolve(H.parse_test_line("- `ctest -R board`"), records)
        self.assertEqual(sorted(r["id"] for r in found), ["ctest:board"])   # exact id wins
        wide = H.resolve(H.parse_test_line("- `ctest -R boa.d`"), records)
        self.assertEqual(sorted(r["id"] for r in wide), ["ctest:board", "ctest:boardpane"])
        out = H.check_card(["- `ctest -R boa.d`"], [], records)
        self.assertEqual([f["test"] for f in out["findings"]], ["ctest:boa.d"])
        self.assertIn("1 of 2 never ran here (boardpane)", out["findings"][0]["message"])

    def test_an_unparsable_pattern_matches_nothing(self):
        records = self.records_for({"id": "ctest:board", "results": ["pass"]})
        self.assertEqual(H.resolve({"id": "ctest:[", "runner": "ctest"}, records), [])

    def test_every_finding_has_the_wire_shape(self):
        out = H.check_card(["- `ctest -R vanished`"], ["src/Pane.h"], [])
        for finding in out["findings"]:
            self.assertEqual(set(finding), {"test", "verdict", "message", "severity"})
            self.assertIn(finding["severity"], ("failure", "warning", "notice"))
        json.dumps(out)

    def test_manual_evidence_that_is_not_there(self):
        out = H.check_card(["- manual: docs/qa_evidence/no-such-directory/"], [], [])
        self.assertEqual([f["verdict"] for f in out["findings"]], ["gone"])


class StatusTests(unittest.TestCase):
    """The four statuses, applicability and the attached result (card #PR4Q)."""

    REV = "a1b2c3d4e5f6"

    def records_for(self, *rows):
        found = [discovered(spec["id"], file=spec.get("file", ""),
                            source_hash=spec.get("source_hash", "")) for spec in rows]
        executions = []
        for spec in rows:
            executions.extend(spec.get("executions", []))
        return H.records(found, executions)

    def statuses(self, lines, records_list, **kw):
        return {row["test"]: row
                for row in H.card_statuses(lines, records_list, **kw)}

    def test_the_four_statuses_on_one_card(self):
        records = self.records_for(
            {"id": "ctest:totals", "executions": [ex("ctest:totals", "pass", minutes=10,
                                                     commit=self.REV, host="desktop")]},
            {"id": "ctest:large", "executions": [ex("ctest:large", "fail", minutes=11,
                                                    commit=self.REV, host="desktop")]},
            {"id": "ctest:fresh", "executions": []})
        out = H.check_card(["- `ctest -R totals`", "- `ctest -R large`", "- `ctest -R fresh`",
                            "- `ctest -R rounding`"], [], records, commits=[self.REV])
        self.assertEqual([row["status"] for row in out["statuses"]],
                         [H.STATUS_PASSED, H.STATUS_FAILED, H.STATUS_MISSING, H.STATUS_NA])
        self.assertEqual(sorted(H.STATUSES), sorted({H.STATUS_PASSED, H.STATUS_FAILED,
                                                     H.STATUS_MISSING, H.STATUS_NA}))

    def test_a_run_from_another_host_for_this_revision_is_evidence(self):
        # Codex, §C: "'never run here' is not 'never run'". The store is host-agnostic, so a
        # green run fetched from the second runner proves the card.
        records = self.records_for({"id": "ctest:totals", "executions": [
            ex("ctest:totals", "pass", minutes=5, commit=self.REV, host="sphinxpad",
               run_id="remote-1")]})
        rows = self.statuses(["- `ctest -R totals`"], records, commits=[self.REV], host="spark")
        row = rows["ctest:totals"]
        self.assertEqual(row["status"], H.STATUS_PASSED)
        self.assertEqual(row["evidence"][0]["host"], "sphinxpad")
        self.assertTrue(row["evidence"][0]["applicable"])
        # …and because the newest result is another machine's, the card offers to take it.
        self.assertTrue(row["use_existing"])
        self.assertNotIn("never", row["message"])

    def test_a_result_this_card_accepted_counts_whatever_its_commit(self):
        records = self.records_for({"id": "ctest:totals", "executions": [
            ex("ctest:totals", "pass", minutes=1, commit="9999999999", host="ci",
               run_id="ci-77")]})
        without = self.statuses(["- `ctest -R totals`"], records, commits=[self.REV],
                                since=at(100))
        self.assertEqual(without["ctest:totals"]["status"], H.STATUS_MISSING)
        with_accept = self.statuses(["- `ctest -R totals`"], records, commits=[self.REV],
                                    since=at(100), accepted=["ci-77"])
        self.assertEqual(with_accept["ctest:totals"]["status"], H.STATUS_PASSED)
        self.assertTrue(with_accept["ctest:totals"]["accepted"])
        self.assertFalse(with_accept["ctest:totals"]["use_existing"])   # never asked twice

    def test_a_pass_from_before_the_card_is_not_evidence_for_it(self):
        # "'passed last time' is not proof about this change" (§C).
        records = self.records_for({"id": "ctest:totals", "executions": [
            ex("ctest:totals", "pass", minutes=1, commit="0000000000", host="spark")]})
        rows = self.statuses(["- `ctest -R totals`"], records, commits=[self.REV],
                             since=at(100))
        self.assertEqual(rows["ctest:totals"]["status"], H.STATUS_MISSING)
        self.assertFalse(rows["ctest:totals"]["evidence"][0]["applicable"])

    def test_a_run_after_the_cards_last_commit_with_the_same_source_counts(self):
        records = self.records_for({"id": "ctest:totals", "source_hash": "sha256:aa",
                                    "executions": [ex("ctest:totals", "pass", minutes=120,
                                                      commit="feedface11", host="spark",
                                                      source_hash="sha256:aa")]})
        rows = self.statuses(["- `ctest -R totals`"], records, commits=[self.REV],
                             since=at(100))
        self.assertEqual(rows["ctest:totals"]["status"], H.STATUS_PASSED)

    def test_a_run_of_a_different_version_of_the_test_is_not_evidence(self):
        records = self.records_for({"id": "ctest:totals", "source_hash": "sha256:bb",
                                    "executions": [ex("ctest:totals", "pass", minutes=120,
                                                      commit="feedface11", host="spark",
                                                      source_hash="sha256:aa")]})
        rows = self.statuses(["- `ctest -R totals`"], records, commits=[self.REV],
                             since=at(100))
        self.assertEqual(rows["ctest:totals"]["status"], H.STATUS_MISSING)

    def test_a_card_with_no_commits_reads_every_run_of_todays_test(self):
        records = self.records_for({"id": "ctest:totals", "executions": [
            ex("ctest:totals", "pass", minutes=1, commit="0000000000")]})
        rows = self.statuses(["- `ctest -R totals`"], records)
        self.assertEqual(rows["ctest:totals"]["status"], H.STATUS_PASSED)

    def test_commits_match_by_prefix_either_way_round(self):
        self.assertTrue(H.same_commit("3f2a9c1e", "3f2a9c1e4d5b"))
        self.assertTrue(H.same_commit("3f2a9c1e4d5b", "3f2a9c1e"))
        self.assertFalse(H.same_commit("3f2a9c1e", "3f2a9c11"))
        self.assertFalse(H.same_commit("", "3f2a9c1e"))
        self.assertFalse(H.same_commit("3f2a", "3f2a"))        # too short to mean anything

    def test_a_manual_line_is_not_applicable_and_never_blocks(self):
        rows = self.statuses(["- manual: docs/qa_evidence/2026-09-21-thing/"], [])
        row = rows["manual:docs/qa_evidence/2026-09-21-thing/"]
        self.assertEqual(row["status"], H.STATUS_NA)
        self.assertNotIn(row["status"], H.BLOCKING_STATUSES)

    def test_a_line_is_as_proven_as_its_worst_test(self):
        records = self.records_for(
            {"id": "unittest:tests.test_x.T.test_a", "file": "tests/test_x.py",
             "executions": [ex("unittest:tests.test_x.T.test_a", "pass", minutes=3,
                               commit=self.REV)]},
            {"id": "unittest:tests.test_x.T.test_b", "file": "tests/test_x.py",
             "executions": []})
        rows = self.statuses(["- `tests/test_x.py`"], records, commits=[self.REV])
        self.assertEqual(rows["unittest:tests.test_x"]["status"], H.STATUS_MISSING)

    def test_a_failed_test_is_failed_even_when_an_older_run_passed(self):
        records = self.records_for({"id": "ctest:totals", "executions": [
            ex("ctest:totals", "pass", minutes=1, commit=self.REV),
            ex("ctest:totals", "fail", minutes=9, commit=self.REV)]})
        rows = self.statuses(["- `ctest -R totals`"], records, commits=[self.REV])
        self.assertEqual(rows["ctest:totals"]["status"], H.STATUS_FAILED)
        self.assertFalse(rows["ctest:totals"]["use_existing"])

    def test_the_statuses_are_json_and_carry_the_wire_keys(self):
        records = self.records_for({"id": "ctest:totals", "executions": [
            ex("ctest:totals", "pass", minutes=1, commit=self.REV, host="ci", run_id="ci-1")]})
        rows = H.card_statuses(["- `ctest -R totals`"], records, commits=[self.REV])
        self.assertEqual(set(rows[0]), {"test", "invocation", "status", "retired", "evidence",
                                        "use_existing", "accepted", "message"})
        self.assertEqual(set(rows[0]["evidence"][0]),
                         {"run_id", "host", "commit", "ts", "result", "applicable"})
        json.dumps(rows)


if __name__ == "__main__":                                # pragma: no cover
    unittest.main()
