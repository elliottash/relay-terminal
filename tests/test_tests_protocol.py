# SPDX-License-Identifier: AGPL-3.0-or-later
"""The `tests_*` worker protocol: discovery, runs, stop, history, Check and the incoming ingest.

Everything happens in a throwaway project under `/tmp`:

* a **fake `ctest`** on `PATH` — a real executable that answers `--show-only=json-v1` with two
  tests, prints CTest's own progress lines for a run and writes the JUnit file it is asked for.
  It is a shim rather than a mock because this module's whole job is to drive `ctest` from the
  command line and read what comes back, and a mock of `subprocess` would test nothing;
* a **real one-file unittest module**, run through `relay_core.junit_runner` exactly as the
  worker runs it, so the unittest half is end to end too;
* a board with cards, so the cards index, `cards_without_tests` and `tests_check` are the real
  readers over real card files.

No network, no model, no keyring, and no test here runs the repository's own suite.
"""
import datetime
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))

from relay_core import board as B                  # noqa: E402
from relay_core import board_protocol as BP        # noqa: E402
from relay_core import board_tools as BT           # noqa: E402
from relay_core import test_history as H           # noqa: E402
from relay_core import test_probe as P            # noqa: E402
from relay_core import tests_protocol as TP        # noqa: E402

BOARD_CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, ready, in-progress, needs-qa, done]
agent: {autonomy: auto}
"""

#: The fake `ctest`.  `alpha` passes, `beta` fails with a message, `slow` sleeps until it is
#: killed — which is how the stop test proves the process group really dies.  Every invocation
#: is appended to `$CTEST_SHIM_LOG`, so a test can assert on the command line that was built.
CTEST_SHIM = '''#!/usr/bin/env python3
import json, os, re, sys, time, xml.etree.ElementTree as ET

ARGS = sys.argv[1:]
log = os.environ.get("CTEST_SHIM_LOG")
if log:
    with open(log, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(ARGS) + "\\n")

TESTS = ["alpha", "beta", "slow"]
#: `$CTEST_SHIM_FIXED` is how a test says "somebody has fixed it now": the named tests pass on
#: this and every later invocation, which is what a signal resolving needs to be provable.
FIXED = [n for n in (os.environ.get("CTEST_SHIM_FIXED") or "").split(",") if n]

if "--show-only=json-v1" in ARGS:
    print(json.dumps({"kind": "ctestInfo", "version": {"major": 1, "minor": 0}, "tests": [
        {"name": "alpha", "command": ["/bin/true"],
         "properties": [{"name": "LABELS", "value": ["fast"]}]},
        {"name": "beta", "command": ["/bin/true"], "properties": []},
        {"name": "slow", "command": ["/bin/true"], "properties": []}]}))
    sys.exit(0)

def value(flag):
    return ARGS[ARGS.index(flag) + 1] if flag in ARGS else ""

pattern = value("-R") or ".*"
chosen = [name for name in TESTS if re.search(pattern, name)]
suite = ET.Element("testsuite", {"name": "shim", "tests": str(len(chosen))})
failed = 0
for index, name in enumerate(chosen, 1):
    print("    Start %d: %s" % (index, name), flush=True)
    if name == "slow":
        time.sleep(120)
    ok = name != "beta" or name in FIXED
    seconds = 0.25 if ok else 0.5
    print("%d/%d Test #%d: %s .......   %s    %.2f sec"
          % (index, len(chosen), index, name, "Passed" if ok else "***Failed", seconds),
          flush=True)
    case = ET.SubElement(suite, "testcase", {"classname": name, "name": name,
                                             "time": "%.6f" % seconds,
                                             "status": "run" if ok else "fail"})
    if not ok:
        failed += 1
        child = ET.SubElement(case, "failure", {"message": "beta broke on purpose"})
        child.text = "assertion failed in beta"
junit = value("--output-junit")
if junit:
    ET.ElementTree(suite).write(junit, encoding="utf-8", xml_declaration=True)
sys.exit(1 if failed else 0)
'''

SAMPLE_TESTS = '''# a real unittest module, run by relay_core.junit_runner
import unittest


class SampleTests(unittest.TestCase):
    def test_ok(self):
        self.assertEqual(2 + 2, 4)

    def test_broken(self):
        self.assertEqual(1, 2, "one is not two")
'''


def card_text(card_id, title, status, body="", tab_links=""):
    return (f"---\nid: {card_id}\ntype: work\nstatus: {status}\nlabels: [feature]\n"
            f"assignee: agent\nrank: m\ncreated: '2026-09-20'\n"
            f"links: {{plans: [], commits: [{tab_links}], evidence: [], related: [], github: null}}\n"
            f"---\n# {title}\n\n## Issue\nsomething\n{body}")


class TestsProtocolTest(unittest.TestCase):
    """One temporary project, one board, the fake ctest on PATH."""

    maxDiff = None

    def setUp(self):
        # A short path: the run environment puts XDG_RUNTIME_DIR and TMPDIR under /tmp for the
        # 108-byte socket limit, and the project itself should not be long either.
        self.tmp = tempfile.TemporaryDirectory(prefix="tp-", dir="/tmp")
        self.project = Path(self.tmp.name).resolve()
        self.root = self.project / "issues"
        (self.root).mkdir()
        (self.root / B.BOARD_CONFIG).write_text(BOARD_CONFIG, encoding="utf-8")
        (self.root / "features").mkdir()
        self.board = B.Board(self.root, self.project)

        # The build directory ctest is pointed at: `CTestTestfile.cmake` is what makes
        # `test_probe.ctest_listing` willing to start a process at all.
        self.build = self.project / "build"
        self.build.mkdir()
        (self.build / "CTestTestfile.cmake").write_text("# shim\n", encoding="utf-8")

        # The fake ctest, first on PATH for this test and for every child it starts.
        self.bin = self.project / "bin"
        self.bin.mkdir()
        shim = self.bin / "ctest"
        shim.write_text(CTEST_SHIM, encoding="utf-8")
        shim.chmod(shim.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        self.shim_log = self.project / "ctest-invocations.log"
        self._env_before = dict(os.environ)
        os.environ["PATH"] = f"{self.bin}{os.pathsep}{os.environ.get('PATH', '')}"
        os.environ["CTEST_SHIM_LOG"] = str(self.shim_log)

        # A real unittest module, and `backend` symlinked so the child can import relay_core
        # from the project's own PYTHONPATH the way it does in a real checkout.
        (self.project / "tests").mkdir()
        (self.project / "tests" / "test_sample.py").write_text(SAMPLE_TESTS, encoding="utf-8")
        os.symlink(REPO / "backend", self.project / "backend")

        self.events = []
        self.tests = TP.TestsCommands(self.project, self.root, self.events.append)

    def tearDown(self):
        self.tests.shutdown()
        run = self.tests._run
        if run is not None:
            run.finished.wait(30)
        os.environ.clear()
        os.environ.update(self._env_before)
        self.tmp.cleanup()

    # ---- helpers ---------------------------------------------------------------
    def card(self, card_id, title="A card", status="executing", body="", commits=""):
        path = self.root / "features" / f"2026-09-20-{card_id.lower()}.md"
        path.write_text(card_text(card_id, title, status, body, commits), encoding="utf-8")
        return path

    def of(self, event_name):
        return [e for e in self.events if e.get("event") == event_name]

    def send(self, **request):
        self.assertTrue(self.tests.dispatch(request), request)

    def wait_for_run(self, seconds=90):
        """Wait until the run in flight has finished and its closing tests_list has been sent."""
        run = self.tests._run
        self.assertIsNotNone(run, "no run was started")
        self.assertTrue(run.finished.wait(seconds), "the run never finished")
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if any(e.get("event") == "tests_list" for e in self.events[-3:]):
                break
            time.sleep(0.05)
        return run

    def incoming(self, name, run_id, commit="c0ffee", host="sphinxpad", results=("alpha",)):
        folder = self.root / ".private" / "tests" / "incoming" / name
        folder.mkdir(parents=True)
        (folder / "meta.json").write_text(json.dumps({
            "run_id": run_id, "commit": commit, "host": host,
            "finished": "2026-09-20T19:32:56Z"}), encoding="utf-8")
        cases = "".join(f'<testcase classname="{n}" name="{n}" time="0.5" status="run"/>'
                        for n in results)
        (folder / "ctest.xml").write_text(
            f'<?xml version="1.0"?><testsuite name="ctest" tests="{len(results)}">{cases}'
            "</testsuite>", encoding="utf-8")
        return folder


class DiscoveryAndListTest(TestsProtocolTest):

    def test_list_carries_the_contract_keys_and_nothing_surprising(self):
        self.send(type="tests_list", id="r1")
        events = self.of("tests_list")
        self.assertEqual(len(events), 1)
        event = events[0]
        self.assertEqual(set(event),
                         {"event", "id", "project", "tests", "summary", "cards_without_tests"})
        self.assertEqual(event["id"], "r1")
        self.assertEqual(event["project"], str(self.project))
        self.assertEqual(set(event["summary"]),
                         {"total", "passed", "failed", "skipped", "never_run", "slow", "flaky",
                          "duration", "line"})
        ids = {row["id"] for row in event["tests"]}
        # Both runners, from one call: the shim's three ctest names and the sample module's two.
        self.assertLessEqual({"ctest:alpha", "ctest:beta", "ctest:slow",
                              "unittest:tests.test_sample.SampleTests.test_ok",
                              "unittest:tests.test_sample.SampleTests.test_broken"}, ids)
        row = next(r for r in event["tests"] if r["id"] == "ctest:alpha")
        for key in ("id", "name", "runner", "file", "labels", "runs", "pass", "fail", "skip",
                    "flake_score", "slow", "flaky", "stale", "cards", "history", "last_result"):
            self.assertIn(key, row)
        self.assertEqual(row["stale"], ["never-run"])
        self.assertIn("never run", event["summary"]["line"])

    def test_cards_without_tests_lists_work_in_flight_only(self):
        self.card("AAA1", "Being worked", "executing")
        self.card("BBB2", "Waiting for a verifier", "needs-verification")
        self.card("CCC3", "Still an idea", "inbox")
        self.card("DDD4", "Has its tests", "executing",
                  body="\n## Tests\n- `ctest -R alpha` — tests/alpha_test.cpp\n")
        self.send(type="tests_list")
        listed = self.of("tests_list")[0]["cards_without_tests"]
        self.assertEqual([row["id"] for row in listed], ["AAA1", "BBB2"])
        self.assertEqual(set(listed[0]), {"id", "title", "status"})
        self.assertEqual(listed[0]["title"], "Being worked")
        self.assertEqual(listed[1]["status"], "needs-verification")

    def test_a_cards_tests_section_names_the_test_on_its_row(self):
        self.card("DDD4", "Has its tests", "executing",
                  body="\n## Tests\n- `ctest -R alpha`\n- prose that names nothing\n")
        self.send(type="tests_list")
        row = next(r for r in self.of("tests_list")[0]["tests"] if r["id"] == "ctest:alpha")
        self.assertEqual(row["cards"], ["DDD4"])

    def test_discovery_survives_a_build_directory_that_is_not_one(self):
        tests = TP.TestsCommands(self.project, self.root, self.events.append,
                                 build_dir=self.project / "nowhere")
        payload = tests.inventory()
        self.assertTrue(all(row["runner"] == "unittest" for row in payload["tests"]))


class IncomingIngestTest(TestsProtocolTest):

    def test_a_folder_is_folded_in_exactly_once(self):
        self.incoming("sphinxpad-run1", "20260920T193105Z-abc", results=("alpha", "beta"))
        first = self.tests.ingest_incoming()
        self.assertEqual(first["executions"], 2)
        self.assertEqual(len(H.read(self.tests.store_path())), 2)
        marker = self.root / ".private/tests/incoming/sphinxpad-run1/ingested"
        self.assertTrue(marker.is_file())
        self.assertEqual(json.loads(marker.read_text())["run_id"], "20260920T193105Z-abc")
        # Twice more, including through the request that calls it: still two executions.
        self.assertEqual(self.tests.ingest_incoming()["executions"], 0)
        self.send(type="tests_list")
        self.assertEqual(len(H.read(self.tests.store_path())), 2)

    def test_a_run_already_in_the_store_is_marked_rather_than_doubled(self):
        H.append([H.Execution(ts="2026-09-20T19:32:56Z", id="ctest:alpha", result="pass",
                              run_id="20260920T193105Z-abc", runner="ctest", host="sphinxpad")],
                 self.tests.store_path())
        self.incoming("sphinxpad-run1", "20260920T193105Z-abc")
        self.assertEqual(self.tests.ingest_incoming()["executions"], 0)
        self.assertEqual(len(H.read(self.tests.store_path())), 1)
        marker = self.root / ".private/tests/incoming/sphinxpad-run1/ingested"
        self.assertEqual(json.loads(marker.read_text())["note"], "already in the store")

    def test_the_host_and_the_commit_come_from_meta_json(self):
        self.incoming("laptop-run2", "20260920T200000Z-def", commit="deadbeef", host="sphinxpad")
        self.tests.ingest_incoming()
        row = H.read(self.tests.store_path())[0]
        self.assertEqual((row.host, row.commit, row.ts),
                         ("sphinxpad", "deadbeef", "2026-09-20T19:32:56Z"))

    def test_a_folder_without_meta_json_still_ingests_under_its_own_name(self):
        folder = self.incoming("spark-run3", "20260920T210000Z-ghi")
        (folder / "meta.json").unlink()
        self.assertEqual(self.tests.ingest_incoming()["executions"], 1)
        self.assertEqual(H.read(self.tests.store_path())[0].run_id, "spark-run3")


class RunTest(TestsProtocolTest):

    def run_events(self):
        return self.of("tests_run")

    def test_a_ctest_run_reports_started_progress_and_finished(self):
        self.send(type="tests_run", ids=["ctest:alpha", "ctest:beta"])
        self.wait_for_run()
        events = self.run_events()
        self.assertEqual(events[0]["state"], "started")
        self.assertEqual(set(events[0]), {"event", "run_id", "state", "done", "total", "message"})
        self.assertEqual(events[0]["total"], 2)
        self.assertTrue(events[0]["run_id"])
        verdicts = {e["id"]: e for e in events if e.get("state") == "progress" and e.get("result")}
        self.assertEqual(verdicts["ctest:alpha"]["result"], "pass")
        self.assertEqual(verdicts["ctest:beta"]["result"], "fail")
        self.assertEqual(set(verdicts["ctest:alpha"]),
                         {"event", "run_id", "state", "done", "total", "id", "result", "duration"})
        # `Start n:` lines put a row in Running: an id with no result.
        running = [e for e in events if e.get("state") == "progress" and "result" not in e]
        self.assertIn("ctest:alpha", {e["id"] for e in running})
        last = events[-1]
        self.assertEqual(last["state"], "finished")
        self.assertEqual((last["done"], last["total"]), (2, 2))
        self.assertIn("1 passed", last["message"])
        self.assertIn("1 failed", last["message"])
        self.assertIn(" ms", last["message"])         # never "in 0.0 s" for a sub-second run

    def test_a_run_stores_its_executions_and_ends_with_a_fresh_list(self):
        self.send(type="tests_run", ids=["ctest:alpha"])
        self.wait_for_run()
        rows = H.read(self.tests.store_path())
        self.assertEqual([r.id for r in rows], ["ctest:alpha"])
        self.assertEqual(rows[0].result, "pass")
        self.assertEqual(rows[0].host, TP._hostname())
        self.assertEqual(rows[0].run_id, self.run_events()[0]["run_id"])
        self.assertEqual(self.events[-1]["event"], "tests_list")
        row = next(r for r in self.events[-1]["tests"] if r["id"] == "ctest:alpha")
        self.assertEqual((row["runs"], row["last_result"]), (1, "pass"))

    def test_the_whole_selection_is_one_ctest_invocation(self):
        self.send(type="tests_run", ids=["ctest:alpha", "ctest:beta"], repeat_until_fail=3)
        self.wait_for_run()
        runs = [json.loads(line) for line in
                self.shim_log.read_text().splitlines() if "--show-only=json-v1" not in line]
        self.assertEqual(len(runs), 1, runs)
        argv = runs[0]
        self.assertEqual(argv[argv.index("-R") + 1], "^(alpha|beta)$")
        self.assertEqual(argv[argv.index("--repeat") + 1], "until-fail:3")
        self.assertEqual(argv[argv.index("--test-dir") + 1], str(self.build))

    def test_a_unittest_run_goes_through_junit_runner(self):
        self.send(type="tests_run",
                  ids=["unittest:tests.test_sample.SampleTests.test_ok",
                       "unittest:tests.test_sample.SampleTests.test_broken"])
        self.wait_for_run()
        rows = {r.id: r for r in H.read(self.tests.store_path())}
        self.assertEqual(rows["unittest:tests.test_sample.SampleTests.test_ok"].result, "pass")
        broken = rows["unittest:tests.test_sample.SampleTests.test_broken"]
        self.assertEqual(broken.result, "fail")
        self.assertIn("one is not two", broken.message + broken.excerpt)
        self.assertEqual(self.run_events()[-1]["state"], "finished")

    def test_a_run_isolates_the_environment_it_hands_the_child(self):
        tmp = Path(tempfile.mkdtemp(prefix="rt-", dir="/tmp"))
        try:
            env = self.tests._environment(tmp)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        self.assertEqual(env["RELAY_KEYRING"], "off")
        self.assertEqual(env["QT_QPA_PLATFORM"], "offscreen")
        for name in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME",
                     "XDG_RUNTIME_DIR", "TMPDIR"):
            self.assertTrue(env[name].startswith(str(tmp)), name)
            self.assertNotEqual(env[name], os.environ.get(name))
        self.assertTrue(env["RELAY_LOCAL_MODELS"].startswith(str(tmp)))
        self.assertTrue(env["PYTHONPATH"].startswith(str(self.project / "backend")))

    def test_stop_ends_the_process_group_and_says_stopped(self):
        self.send(type="tests_run", ids=["ctest:slow"])
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and not self.of("tests_run"):
            time.sleep(0.05)
        run_id = self.of("tests_run")[0]["run_id"]
        # Wait until the shim is really in its sleep, so the stop has a process group to end.
        time.sleep(0.6)
        self.send(type="tests_stop", run_id=run_id)
        self.wait_for_run(30)
        last_run = [e for e in self.of("tests_run") if e.get("state") in
                    ("finished", "stopped", "error")][-1]
        self.assertEqual(last_run["state"], "stopped")
        self.assertIn("stopped after", last_run["message"])

    def test_stopping_a_run_that_is_not_there_is_one_sentence(self):
        self.send(type="tests_stop", run_id="20260920T000000Z-zz")
        event = self.of("tests_run")[-1]
        self.assertEqual(event["state"], "error")
        self.assertIn("no test run", event["message"])


class RefusalTest(TestsProtocolTest):

    def refusal(self, **request):
        self.send(type="tests_run", **request)
        event = self.of("tests_run")[-1]
        self.assertEqual(event["state"], "error", event)
        self.assertEqual(set(event), {"event", "run_id", "state", "done", "total", "message"})
        self.assertEqual(event["message"].count("."), 1, event["message"])
        return event["message"]

    def test_no_ids_at_all(self):
        self.assertIn("name the tests to run", self.refusal(ids=[]))
        self.assertIn("name the tests to run", self.refusal(ids=None))

    def test_more_than_two_hundred_without_all(self):
        ids = [f"ctest:t{n}" for n in range(201)]
        self.assertIn("at most 200", self.refusal(ids=ids))

    def test_a_missing_build_directory(self):
        shutil.rmtree(self.build)
        self.assertIn("no configured build directory", self.refusal(ids=["ctest:alpha"]))

    def test_only_manual_entries(self):
        self.assertIn("recorded by hand",
                      self.refusal(ids=["manual:docs/qa_evidence/2026-09-20-thing/"]))

    def test_a_manual_entry_beside_a_real_one_is_skipped_not_refused(self):
        self.send(type="tests_run", ids=["ctest:alpha", "manual:docs/evidence/"])
        self.wait_for_run()
        started = self.of("tests_run")[0]
        self.assertEqual(started["total"], 1)
        self.assertIn("1 not runnable", started["message"])

    def test_a_second_run_while_one_is_in_flight(self):
        self.send(type="tests_run", ids=["ctest:slow"])
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and not self.of("tests_run"):
            time.sleep(0.05)
        message = self.refusal(ids=["ctest:alpha"])
        self.assertIn("already going", message)
        self.tests.stop_run()
        self.wait_for_run(30)

    def test_the_whole_suite_is_never_implied(self):
        # There is no request that means "everything": `all: true` only raises the id ceiling.
        self.assertIn("name the tests to run", self.refusal(ids=[], all=True))
        ids = [f"ctest:t{n}" for n in range(201)]
        self.send(type="tests_run", ids=ids, all=True)
        self.wait_for_run()
        argv = [json.loads(line) for line in self.shim_log.read_text().splitlines()
                if "--show-only=json-v1" not in line][0]
        self.assertTrue(argv[argv.index("-R") + 1].startswith("^(t0|t1|"))


class HistoryTest(TestsProtocolTest):

    def test_history_answers_newest_first_and_bounded(self):
        rows = [H.Execution(ts=f"2026-09-20T10:0{n}:00Z", id="ctest:alpha", result="pass",
                            duration=0.1 * n, runner="ctest", run_id=f"r{n}", host="spark")
                for n in range(5)]
        H.append(rows, self.tests.store_path())
        self.send(type="tests_history", id="ctest:alpha", limit=3)
        event = self.of("tests_history")[0]
        self.assertEqual(set(event), {"event", "id", "executions"})
        self.assertEqual(event["id"], "ctest:alpha")
        self.assertEqual([e["run_id"] for e in event["executions"]], ["r4", "r3", "r2"])
        self.assertEqual(set(event["executions"][0]),
                         {"ts", "run_id", "id", "runner", "result", "duration", "commit", "host"})

    def test_history_of_a_test_with_none(self):
        self.send(type="tests_history", id="ctest:alpha")
        self.assertEqual(self.of("tests_history")[0]["executions"], [])

    def test_history_without_an_id(self):
        with self.assertRaises(ValueError):
            self.tests.dispatch({"type": "tests_history"})


class CheckTest(TestsProtocolTest):

    def test_a_card_with_no_tests_section_gets_one_clear_sentence(self):
        self.card("AAA1", "No tests named", "needs-verification")
        self.send(type="tests_check", card="AAA1", id="r9")
        event = self.of("tests_check")[0]
        self.assertEqual(set(event), {"event", "id", "card", "findings", "actions",
                                      "ids", "files", "failing",
                                      # #PR4Q: the four statuses, and the revision they are about
                                      "statuses", "revision",
                                      # protocol 32 (#AQ6X step 5): what a signal says about it
                                      "blocks", "open_before"})
        self.assertEqual(event["card"], "AAA1")
        self.assertEqual(len(event["findings"]), 1)
        finding = event["findings"][0]
        self.assertEqual(set(finding), {"test", "verdict", "message", "severity"})
        self.assertEqual(finding["verdict"], "no-tests")
        self.assertIn("has no `## Tests` section", finding["message"])
        self.assertEqual(event["actions"], ["Add the tests this card's commits touched"])

    def test_a_card_whose_test_is_retired(self):
        self.card("AAA2", "Names a test that is not there", "needs-verification",
                  body="\n## Tests\n- `ctest -R vanished`\n")
        self.send(type="tests_check", card="AAA2")
        # No request id, so the event carries no `id` key at all rather than a null one.
        self.assertNotIn("id", self.of("tests_check")[0])
        event = self.of("tests_check")[0]
        self.assertEqual([f["verdict"] for f in event["findings"]], ["retired"])
        self.assertEqual(event["findings"][0]["severity"], "notice")
        # #PR4Q: retired is `not-applicable`, and the action replaces it.
        self.assertEqual([row["status"] for row in event["statuses"]], ["not-applicable"])
        self.assertTrue(event["statuses"][0]["retired"])
        self.assertIn("Replace retired check", event["actions"])

    def test_a_card_whose_tests_have_never_run(self):
        self.card("AAA3", "Lists a real test", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.send(type="tests_check", card="AAA3")
        findings = self.of("tests_check")[0]["findings"]
        self.assertEqual([f["verdict"] for f in findings], ["never-run"])
        self.assertIn("Run these", self.of("tests_check")[0]["actions"])

    def test_a_card_whose_test_has_passed_is_silent(self):
        self.card("AAA4", "Lists a test that ran", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id="ctest:alpha", result="pass",
                              duration=0.2, runner="ctest", run_id="r1", host="spark")],
                 self.tests.store_path())
        self.send(type="tests_check", card="AAA4")
        self.assertEqual(self.of("tests_check")[0]["findings"], [])

    def test_an_unknown_card(self):
        with self.assertRaises(ValueError) as caught:
            self.tests.dispatch({"type": "tests_check", "card": "ZZZ9"})
        self.assertIn("No card #ZZZ9", str(caught.exception))

    def test_the_changed_files_of_a_cards_commits_are_read_from_git(self):
        subprocess.run(["git", "init", "-q", str(self.project)], check=True)
        subprocess.run(["git", "-C", str(self.project), "config", "user.email", "t@example.com"],
                       check=True)
        subprocess.run(["git", "-C", str(self.project), "config", "user.name", "T"], check=True)
        (self.project / "src.txt").write_text("one\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(self.project), "add", "src.txt"], check=True)
        subprocess.run(["git", "-C", str(self.project), "commit", "-qm", "a change (#AAA5)"],
                       check=True)
        self.card("AAA5", "Has a commit", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.assertEqual(self.tests.card_files("AAA5"), ["src.txt"])
        self.assertTrue(self.tests.head_commit())


class CheckPayloadTest(TestsProtocolTest):
    """The three keys Check's answer carries beside its findings (#7BM4 phase 4)."""

    def test_the_event_carries_the_ids_a_run_would_use(self):
        self.card("BBB1", "Two real tests", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n- `ctest -R beta`\n")
        self.send(type="tests_check", card="BBB1")
        event = self.of("tests_check")[0]
        self.assertEqual(event["ids"], ["ctest:alpha", "ctest:beta"])
        # `Run these` is pressable: every id it would send is a runnable runner.
        self.assertTrue(all(i.split(":")[0] in TP.RUNNABLE for i in event["ids"]))

    def test_a_manual_line_is_not_an_id_to_run(self):
        (self.project / "docs").mkdir()
        self.card("BBB2", "Manual evidence", "needs-verification",
                  body="\n## Tests\n- manual: docs\n- `ctest -R alpha`\n")
        self.send(type="tests_check", card="BBB2")
        self.assertEqual(self.of("tests_check")[0]["ids"], ["ctest:alpha"])

    def test_files_and_failing_name_what_the_actions_open(self):
        self.card("BBB3", "One that failed", "needs-verification",
                  body="\n## Tests\n- `ctest -R beta` — tests/beta_test.cpp\n")
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id="ctest:beta", result="fail",
                              duration=0.5, runner="ctest", run_id="r1", host="spark")],
                 self.tests.store_path())
        self.send(type="tests_check", card="BBB3")
        event = self.of("tests_check")[0]
        self.assertEqual(event["failing"], ["ctest:beta"])
        self.assertEqual(event["files"].get("ctest:beta"), "tests/beta_test.cpp")
        self.assertIn("Open the failing one", event["actions"])

    def test_a_test_that_last_failed_is_a_finding_and_not_only_an_action(self):
        """What the landing gate refuses on has to be what Check says, or the two disagree."""
        self.card("BBB5", "Its test failed", "needs-verification",
                  body="\n## Tests\n- `ctest -R beta`\n")
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id="ctest:beta", result="fail",
                              duration=0.5, runner="ctest", run_id="r1", host="spark")],
                 self.tests.store_path())
        self.send(type="tests_check", card="BBB5")
        findings = self.of("tests_check")[0]["findings"]
        failing = [f for f in findings if f["verdict"] == "failing"]
        self.assertEqual(len(failing), 1, findings)
        self.assertEqual(failing[0]["test"], "ctest:beta")
        self.assertEqual(failing[0]["severity"], "notice")     # the status carries the weight
        self.assertIn("failed the last time it ran", failing[0]["message"])
        # And it is not said twice: a test that is *gone* keeps its own single finding.
        self.card("BBB6", "Gone and never run", "needs-verification",
                  body="\n## Tests\n- `ctest -R vanished`\n")
        self.send(type="tests_check", card="BBB6")
        self.assertEqual([f["verdict"] for f in self.of("tests_check")[1]["findings"]],
                         ["retired"])

    def test_a_card_without_a_tests_section_still_carries_the_three_keys(self):
        self.card("BBB4", "No tests named", "needs-verification")
        self.send(type="tests_check", card="BBB4")
        event = self.of("tests_check")[0]
        self.assertEqual((event["ids"], event["files"], event["failing"]), ([], {}, []))


class CheckBlockTest(TestsProtocolTest):
    """The **one** `### Check` status the worker leaves under `## Tests` (#7BM4, #PR4Q)."""

    def body_of(self, card_id):
        return self.board.card_by_id(card_id).body

    def test_a_check_writes_one_line_per_listed_test_and_its_status(self):
        self.card("CCC1", "Names a test that is gone", "needs-verification",
                  body="\n## Tests\n- `ctest -R vanished`\n")
        self.send(type="tests_check", card="CCC1")
        body = self.body_of("CCC1")
        self.assertRegex(body, r"### Check \d{4}-\d{2}-\d{2} \d{2}:\d{2}")
        self.assertIn("- not-applicable · ctest:vanished — ", body)
        self.assertIn("- notice · ctest:vanished — ", body)     # the advisory finding beside it
        # The history is the thread, and the block says so in its last line.
        self.assertTrue(body.rstrip().endswith("history: thread"), body)
        kinds = [(e.kind, e.text) for e in self.board.thread("CCC1")]
        self.assertTrue(any(k == "evidence" and "Check ·" in text for k, text in kinds), kinds)

    def test_a_clean_card_says_passed_not_no_findings(self):
        self.card("CCC2", "A test that passed", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id="ctest:alpha", result="pass",
                              duration=0.2, runner="ctest", run_id="r1", host="spark")],
                 self.tests.store_path())
        self.send(type="tests_check", card="CCC2")
        self.assertIn("- passed · ctest:alpha", self.body_of("CCC2"))

    def test_a_second_check_replaces_the_block_rather_than_adding_one(self):
        self.card("CCC3", "Checked twice", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.send(type="tests_check", card="CCC3")
        self.send(type="tests_check", card="CCC3")
        body = self.body_of("CCC3")
        self.assertEqual(body.count("### Check "), 1)
        self.assertEqual(body.count("history: thread"), 1)
        self.assertEqual(len(self.of("tests_check")), 2)
        # …and both answers are in the thread, which is where the history lives now.
        checks = [e for e in self.board.thread("CCC3") if e.kind == "evidence"]
        self.assertEqual(len(checks), 2)

    def test_the_block_stays_where_it_was_and_the_test_lines_are_untouched(self):
        stamp = datetime.datetime.now().strftime("%Y-%m-%d")
        self.card("CCC4", "Checked this morning", "needs-verification",
                  body=f"\n## Tests\n- `ctest -R alpha`\n\n### Check {stamp} 01:02\n"
                       f"- notice · ctest:alpha — an older answer\n")
        self.send(type="tests_check", card="CCC4")
        body = self.body_of("CCC4")
        self.assertEqual(body.count("### Check "), 1)
        self.assertNotIn("an older answer", body)
        self.assertNotIn("01:02", body)
        self.assertIn("- `ctest -R alpha`", body)
        self.assertLess(body.index("- `ctest -R alpha`"), body.index("### Check "))

    def test_a_pile_of_dated_blocks_is_collapsed_into_one(self):
        self.card("CCC5", "Checked every day last week", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n\n### Check 2026-09-01 09:00\n"
                       "- notice · ctest:alpha — last week's answer\n\n"
                       "### Check 2026-09-02 09:00\n- no findings\n")
        self.send(type="tests_check", card="CCC5")
        body = self.body_of("CCC5")
        self.assertEqual(body.count("### Check "), 1)
        self.assertNotIn("last week's answer", body)

    def test_the_block_is_not_read_back_as_a_test(self):
        self.card("CCC6", "Checked twice over", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n\n### Check 2026-09-01 09:00\n"
                       "- failure · ctest:alpha — `ctest -R alpha` is not in the project any more\n")
        self.send(type="tests_check", card="CCC6")
        # One listed test, not two: the block's own line names a test but is not one.
        self.assertEqual(self.of("tests_check")[0]["ids"], ["ctest:alpha"])

    def test_a_card_with_no_tests_section_gets_no_block(self):
        self.card("CCC7", "Nothing listed", "needs-verification")
        before = self.body_of("CCC7")
        self.send(type="tests_check", card="CCC7")
        self.assertEqual(self.body_of("CCC7"), before)
        self.assertNotIn("block", self.of("tests_check")[0])


class AcceptResultTest(TestsProtocolTest):
    """"Use this existing result": a run from another machine, accepted for this revision."""

    def remote_run(self, test_id="ctest:alpha", result="pass", run_id="sphinxpad-7"):
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id=test_id, result=result,
                              duration=0.2, runner="ctest", run_id=run_id, host="sphinxpad",
                              commit="deadbeefcafe")], self.tests.store_path())

    def test_the_event_offers_the_other_machines_result(self):
        self.card("DDD1", "Proven elsewhere", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.remote_run()
        self.send(type="tests_check", card="DDD1")
        row = self.of("tests_check")[0]["statuses"][0]
        self.assertEqual(row["status"], "passed")
        self.assertTrue(row["use_existing"])
        self.assertEqual(row["evidence"][0]["host"], "sphinxpad")
        self.assertEqual(row["evidence"][0]["run_id"], "sphinxpad-7")

    def test_accepting_it_records_it_in_the_check_status_and_the_thread(self):
        self.card("DDD2", "Proven elsewhere", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.remote_run()
        self.send(type="tests_accept", card="DDD2", id="ctest:alpha", run_id="sphinxpad-7")
        body = self.board.card_by_id("DDD2").body
        self.assertIn("- accepted · ctest:alpha — run sphinxpad-7 from sphinxpad", body)
        self.assertIn("<!-- relay:accept test=ctest:alpha run=sphinxpad-7 rev=", body)
        self.assertEqual(len(self.board.card_by_id("DDD2").front.get("links", {}).get("commits")
                              or []), 0)                       # no new front-matter field
        texts = [e.text for e in self.board.thread("DDD2") if e.kind == "evidence"]
        self.assertTrue(any("Accepted run `sphinxpad-7`" in t for t in texts), texts)
        # The answer is a fresh check, so the card page redraws without another click.
        self.assertTrue(self.of("tests_check"))
        self.assertFalse(self.of("tests_check")[-1]["statuses"][0]["use_existing"])

    def test_an_accepted_run_is_evidence_and_the_next_check_carries_it_forward(self):
        self.card("DDD3", "Accepted", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.remote_run()
        self.send(type="tests_accept", card="DDD3", id="ctest:alpha", run_id="sphinxpad-7")
        self.send(type="tests_check", card="DDD3")
        body = self.board.card_by_id("DDD3").body
        self.assertEqual(body.count("relay:accept"), 1)
        self.assertEqual(self.of("tests_check")[-1]["statuses"][0]["status"], "passed")
        self.assertTrue(self.of("tests_check")[-1]["statuses"][0]["accepted"])

    def test_a_run_nobody_has_is_one_sentence(self):
        self.card("DDD4", "Nothing to accept", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        with self.assertRaises(ValueError) as caught:
            self.tests.dispatch({"type": "tests_accept", "card": "DDD4", "id": "ctest:alpha",
                                 "run_id": "no-such-run"})
        self.assertIn("is not in this board's test history", str(caught.exception))


class SuggestTest(TestsProtocolTest):
    """`tests_suggest {card}`: what this card's commits touched, mapped to tests by convention."""

    def repo_with_commit(self, card_id, filename):
        subprocess.run(["git", "init", "-q", str(self.project)], check=True)
        for key, value in (("user.email", "t@example.com"), ("user.name", "T")):
            subprocess.run(["git", "-C", str(self.project), "config", key, value], check=True)
        path = self.project / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("one\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(self.project), "add", str(path)], check=True)
        subprocess.run(["git", "-C", str(self.project), "commit", "-qm", f"work (#{card_id})"],
                       check=True)

    def test_it_appends_the_tests_named_after_what_the_card_changed(self):
        self.card("DDD1", "Touched the sample module", "executing",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.repo_with_commit("DDD1", "tests/sample.py")
        self.send(type="tests_suggest", card="DDD1", id="r4")
        event = self.of("tests_suggest")[0]
        self.assertTrue(event["added"])
        self.assertTrue(event["ids"], event)
        self.assertTrue(all(i.startswith("unittest:tests.test_sample.") for i in event["ids"]),
                        event["ids"])
        body = self.board.card_by_id("DDD1").body
        for line in event["lines"]:
            self.assertIn(line, body)
        self.assertTrue(any(e.kind == "evidence" for e in self.board.thread("DDD1")))

    def test_a_test_the_section_already_lists_is_not_suggested_twice(self):
        self.card("DDD2", "Already lists it", "executing",
                  body="\n## Tests\n- `tests/test_sample.py::SampleTests::test_ok`\n")
        self.repo_with_commit("DDD2", "tests/sample.py")
        self.send(type="tests_suggest", card="DDD2")
        ids = self.of("tests_suggest")[0]["ids"]
        self.assertNotIn("unittest:tests.test_sample.SampleTests.test_ok", ids)

    def test_nothing_to_map_is_one_sentence_and_no_write(self):
        self.card("DDD3", "No commits", "executing", body="\n## Tests\n- `ctest -R alpha`\n")
        before = self.board.card_by_id("DDD3").body
        self.send(type="tests_suggest", card="DDD3")
        event = self.of("tests_suggest")[0]
        self.assertFalse(event["added"])
        self.assertEqual(event["lines"], [])
        self.assertIn("no commits", event["message"])
        self.assertEqual(self.board.card_by_id("DDD3").body, before)

    def test_apply_false_suggests_without_writing(self):
        self.card("DDD4", "Only asking", "executing", body="\n## Tests\n- `ctest -R alpha`\n")
        self.repo_with_commit("DDD4", "tests/sample.py")
        before = self.board.card_by_id("DDD4").body
        self.send(type="tests_suggest", card="DDD4", apply=False)
        self.assertTrue(self.of("tests_suggest")[0]["lines"])
        self.assertFalse(self.of("tests_suggest")[0]["added"])
        self.assertEqual(self.board.card_by_id("DDD4").body, before)

    def test_an_unknown_card(self):
        with self.assertRaises(ValueError) as caught:
            self.tests.dispatch({"type": "tests_suggest", "card": "ZZZ8"})
        self.assertIn("No card #ZZZ8", str(caught.exception))


class GateTest(TestsProtocolTest):
    """Leaving `needs-verification` while the tests a card names do not prove it (#7BM4)."""

    def commands(self):
        commands = BP.BoardCommands(None, self.events.append)
        commands.tools = BT.BoardTools(self.board, emit=self.events.append,
                                       state_path=self.project / ".relay" / "rate.json")
        return commands

    def move(self, card_id, status="done", **extra):
        self.commands().dispatch({"type": "board_move", "id": "m1", "card": card_id,
                                  "status": status, "reason": "landing", **extra})

    def errors(self):
        return [e for e in self.events if e.get("event") == "error"]

    def test_the_gate_and_the_move_path_agree_on_their_two_ends(self):
        self.assertEqual(BP.TP_GATE_FROM, TP.GATE_FROM_STATUS)
        self.assertEqual(tuple(BP.TP_GATE_TO), tuple(TP.GATE_TO_STATUSES))
        self.assertEqual(tuple(TP.GATE_STATUSES), ("failed", "missing-evidence"))

    def test_a_retired_test_does_not_block_the_landing(self):
        # #PR4Q: "gone" used to refuse the move. A check somebody retired on purpose is a thing
        # to replace, not evidence that the work is unfinished.
        self.card("EEE1", "Names a test that is gone", "needs-verification",
                  body="\n## Tests\n- `ctest -R vanished`\n")
        self.move("EEE1")
        self.assertEqual(self.errors(), [])
        self.assertEqual(self.board.card_by_id("EEE1").status, "done")

    def test_a_test_that_failed_for_this_revision_refuses_it(self):
        self.card("EEE2", "Its test failed", "needs-verification",
                  body="\n## Tests\n- `ctest -R beta`\n")
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id="ctest:beta", result="fail",
                              duration=0.5, runner="ctest", run_id="r1", host="spark")],
                 self.tests.store_path())
        self.move("EEE2", status="needs-qa-llm")
        error = self.errors()[0]
        self.assertEqual(error["code"], "tests_gate")
        self.assertEqual(error["tests"], ["ctest:beta"])
        self.assertIn("1 failed", error["text"])
        self.assertEqual(self.board.card_by_id("EEE2").status, "needs-verification")
        self.assertEqual([e for e in self.events if e.get("event") == "board_written"], [])

    def test_a_test_with_no_evidence_for_this_revision_refuses_it(self):
        self.card("EEE8", "Nothing has run", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.move("EEE8")
        error = self.errors()[0]
        self.assertEqual(error["code"], "tests_gate")
        self.assertIn("1 missing-evidence", error["text"])
        self.assertEqual([row["status"] for row in error["statuses"]], ["missing-evidence"])

    def test_a_card_whose_tests_passed_lands(self):
        self.card("EEE3", "Proven", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id="ctest:alpha", result="pass",
                              duration=0.2, runner="ctest", run_id="r1", host="spark")],
                 self.tests.store_path())
        self.move("EEE3")
        self.assertEqual(self.errors(), [])
        self.assertEqual(self.board.card_by_id("EEE3").status, "done")

    def test_a_run_from_another_host_proves_it_too(self):
        # Codex §C: "'never run here' is not 'never run'." A result fetched from the second
        # runner is an execution like any other, so the gate does not fire on it.
        self.card("EEE9", "Proven elsewhere", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id="ctest:alpha", result="pass",
                              duration=0.2, runner="ctest", run_id="sphinxpad-3",
                              host="sphinxpad")], self.tests.store_path())
        self.move("EEE9")
        self.assertEqual(self.errors(), [])
        self.assertEqual(self.board.card_by_id("EEE9").status, "done")

    def test_a_card_with_no_tests_section_is_asked_once_and_then_moves(self):
        # §C: "a card without `## Tests` is ungated. That rewards omitting evidence."
        self.card("EEE4", "Older than the section", "needs-verification")
        self.move("EEE4")
        error = self.errors()[0]
        self.assertEqual(error["code"], "tests_none")
        self.assertIn("which checks prove it", error["text"])
        self.assertEqual(self.board.card_by_id("EEE4").status, "needs-verification")
        notes = [e.text for e in self.board.thread("EEE4") if e.kind == "note"]
        self.assertTrue(any("relay:tests-none" in text for text in notes), notes)
        # Asked once: the second attempt goes through, whatever the answer was.
        self.move("EEE4")
        self.assertEqual(len(self.errors()), 1)
        self.assertEqual(self.board.card_by_id("EEE4").status, "done")

    def test_an_override_lands_it_and_records_what_it_waived(self):
        self.card("EEE5", "Missing evidence, overridden", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.move("EEE5", override="sphinxpad is down; the CI run of this commit is green")
        self.assertEqual(self.errors(), [])
        self.assertEqual(self.board.card_by_id("EEE5").status, "done")
        decisions = [e for e in self.board.thread("EEE5") if e.kind == "decision"]
        self.assertEqual(len(decisions), 1, [e.kind for e in self.board.thread("EEE5")])
        self.assertIn('"sphinxpad is down; the CI run of this commit is green"',
                      decisions[0].text)
        # Scoped to (check, revision) and given an expiry, so it cannot become a rubber stamp.
        self.assertRegex(decisions[0].text,
                         r"<!-- relay:override test=ctest:alpha rev=\S+ until=\d{4}-\d{2}-\d{2} -->")

    def test_the_same_override_is_never_asked_for_twice(self):
        self.card("EE10", "Overridden once", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.move("EE10", override="known flake on this runner")
        self.assertEqual(self.board.card_by_id("EE10").status, "done")
        self.commands().dispatch({"type": "board_move", "id": "m2", "card": "EE10",
                                  "status": "needs-verification", "reason": "back"})
        self.move("EE10")                     # no override this time
        self.assertEqual(self.errors(), [])
        self.assertEqual(self.board.card_by_id("EE10").status, "done")

    def test_an_override_recorded_for_another_revision_does_not_hold(self):
        self.card("EE11", "Overridden at an older revision", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.board.append_thread(
            "EE11",
            'Moved to `done` with the Check gate overridden: "it was fine last week"\n\n'
            "- `ctest:alpha` <!-- relay:override test=ctest:alpha rev=0123456789ab "
            "until=2099-01-01 -->",
            author="owner", kind="decision")
        self.assertEqual(self.tests.live_overrides("EE11", ""), set())
        self.move("EE11")
        self.assertEqual(self.errors()[0]["code"], "tests_gate")

    def test_an_expired_override_does_not_hold_either(self):
        self.card("EE12", "Overridden last month", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.board.append_thread(
            "EE12",
            'Check gate overridden: "a fortnight ago"\n\n'
            "- `ctest:alpha` <!-- relay:override test=ctest:alpha rev=none "
            "until=2020-01-01 -->",
            author="owner", kind="decision")
        self.assertEqual(self.tests.live_overrides("EE12", ""), set())

    def test_a_move_that_is_not_a_landing_is_never_gated(self):
        self.card("EEE6", "Back to work", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.move("EEE6", status="in-progress")
        self.assertEqual(self.errors(), [])
        self.assertEqual(self.board.card_by_id("EEE6").status, "in-progress")

    def test_a_card_that_is_not_in_needs_verification_is_never_gated(self):
        self.card("EEE7", "Straight from executing", "in-progress",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.move("EEE7")
        self.assertEqual(self.errors(), [])
        self.assertEqual(self.board.card_by_id("EEE7").status, "done")


class RefreshAfterRunTest(TestsProtocolTest):
    """A finished run updates every card the tests it ran belong to (Codex §B)."""

    def test_a_finished_run_re_checks_the_cards_whose_tests_it_ran(self):
        self.card("FFF1", "Its test just ran", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        self.card("FFF2", "About something else", "needs-verification",
                  body="\n## Tests\n- `ctest -R beta`\n")
        self.send(type="tests_run", ids=["ctest:alpha"])
        self.wait_for_run()
        cards = [e["card"] for e in self.of("tests_check")]
        self.assertIn("FFF1", cards)
        self.assertNotIn("FFF2", cards)
        # …and the card's own status is current, with nobody pressing Check.
        self.assertIn("- passed · ctest:alpha", self.board.card_by_id("FFF1").body)

    def test_a_run_of_a_test_no_card_names_re_checks_nothing(self):
        self.send(type="tests_run", ids=["ctest:alpha"])
        self.wait_for_run()
        self.assertEqual(self.of("tests_check"), [])


class WordingTest(unittest.TestCase):
    """The two things a person reads first: the run's wall time and the request id."""

    def test_a_short_run_is_milliseconds_not_zero_seconds(self):
        self.assertEqual(TP.format_seconds(0.035), "35 ms")
        self.assertEqual(TP.format_seconds(0.0), "0 ms")
        self.assertEqual(TP.format_seconds(1.44), "1.4 s")
        self.assertEqual(TP.format_seconds(240.0), "4.0 m")

    def test_an_unasked_event_carries_no_null_id(self):
        self.assertEqual(TP._rid(None), {})
        self.assertEqual(TP._rid("r1"), {"id": "r1"})


class CtestOutputTest(unittest.TestCase):
    """The live progress parser, on the lines CTest really prints."""

    def test_a_verdict_line(self):
        self.assertEqual(
            TP.parse_ctest_line("1/5 Test #1: board ...........................   Passed    0.90 sec"),
            {"id": "ctest:board", "result": "pass", "duration": 0.9})

    def test_a_failure_with_no_space_before_the_verdict(self):
        self.assertEqual(
            TP.parse_ctest_line("2/5 Test #2: jobs .........................***Failed    0.04 sec"),
            {"id": "ctest:jobs", "result": "fail", "duration": 0.04})

    def test_the_other_verdicts(self):
        for text, result in (("***Timeout", "timeout"), ("***Skipped", "skip"),
                             ("***Not Run", "skip")):
            line = f"3/5 Test #3: x ....................{text}   1.00 sec"
            self.assertEqual(TP.parse_ctest_line(line)["result"], result, line)

    def test_a_start_line_has_no_verdict(self):
        self.assertEqual(TP.parse_ctest_line("    Start 4: panelayout"),
                         {"id": "ctest:panelayout"})

    def test_prose_is_not_a_test(self):
        for line in ("", "Test project /home/x/build", "100% tests passed, 0 tests failed",
                     "Total Test time (real) =   4.21 sec"):
            self.assertIsNone(TP.parse_ctest_line(line), line)


class BlockingRunTest(TestsProtocolTest):
    """`run_and_wait`, which is what the agent's `tests_run` tool answers with."""

    def test_the_table_carries_every_verdict_and_a_failure_message(self):
        result = self.tests.run_and_wait(["ctest:alpha", "ctest:beta"], timeout=90)
        self.assertEqual(result["state"], "finished")
        self.assertEqual(result["counts"], {"pass": 1, "fail": 1, "skip": 0})
        by_id = {row["id"]: row for row in result["tests"]}
        self.assertEqual(by_id["ctest:alpha"]["result"], "pass")
        self.assertIn("beta broke on purpose", by_id["ctest:beta"]["message"])
        text = TP.format_run(result)
        self.assertIn("ctest:beta", text)
        self.assertIn("beta broke on purpose", text)

    def test_it_refuses_the_same_things_the_wire_does(self):
        with self.assertRaises(TP.TestsError):
            self.tests.run_and_wait([])
        with self.assertRaises(TP.TestsError):
            self.tests.run_and_wait([f"ctest:t{n}" for n in range(60)])

    def test_a_timeout_stops_the_run_and_says_so(self):
        result = self.tests.run_and_wait(["ctest:slow"], timeout=1)
        self.assertEqual(result["state"], "timed-out")
        self.assertIn("timeout", result["message"])

    def test_findings_render_as_text(self):
        self.card("AAA1", "No tests named", "needs-verification")
        text = TP.format_findings(self.tests.check_card("AAA1"))
        self.assertIn("#AAA1", text)
        self.assertIn("no-tests", text)
        self.assertIn("Offered:", text)
        self.assertIn("Nothing to fix", TP.format_findings({"card": "AAA1", "findings": []}))


class WiringTest(TestsProtocolTest):
    """The five requests reach here through `board_protocol.BoardCommands.dispatch`."""

    def commands(self):
        commands = BP.BoardCommands(None, self.events.append)
        commands.tools = BT.BoardTools(self.board, emit=self.events.append,
                                       state_path=self.project / ".relay" / "rate.json")
        return commands

    def test_the_two_type_sets_are_the_same(self):
        self.assertEqual(set(BP.TESTS_TYPES), set(TP.TYPES))
        self.assertLessEqual(set(BP.TESTS_TYPES), BP.TYPES)

    def test_a_request_is_delegated_and_its_event_names_the_board(self):
        commands = self.commands()
        self.assertTrue(commands.handles("tests_list"))
        self.assertTrue(commands.dispatch({"type": "tests_list", "id": "r1"}))
        event = self.of("tests_list")[0]
        # `_send` tags every board event with the root a GUI routes by.
        self.assertEqual(event["root"], str(self.root))
        self.assertEqual(event["project"], str(self.project))

    def test_the_handlers_are_made_once_per_board(self):
        commands = self.commands()
        commands.dispatch({"type": "tests_list"})
        first = commands._tests()
        self.assertIs(commands._tests(), first)
        self.assertEqual(Path(first.project), self.project)

    def test_without_a_board_it_is_the_ordinary_no_board_error(self):
        commands = BP.BoardCommands(None, self.events.append)
        with self.assertRaises(ValueError) as caught:
            commands.dispatch({"type": "tests_check", "card": "AAA1"})
        self.assertIn("no Switchboard", str(caught.exception))


# ------------------------------------------------------- signals (protocol 32, #AQ6X)

class SignalProtocolTest(TestsProtocolTest):
    """The `signals_*` messages, the fold after every ingest, the re-run rule and `opened`.

    The fake `ctest` of this module fails `beta` every time, so a run of `beta` is how a signal
    is opened here — end to end, through the same handler a pane drives.
    """

    def setUp(self):
        super().setUp()
        from relay_core import signals as S
        self.S = S
        self.tests.pane_token = "3f2504e0-4f89-11d3-9a0c-0305e82c3301"

    def signals(self):
        return self.S.state(self.project, self.root)

    def changed(self):
        return self.of("signals_changed")

    def written(self):
        return self.of("signals_written")

    # ---- the fold after a run --------------------------------------------------
    def test_a_failing_run_opens_the_signal_at_once_because_the_failures_are_re_run(self):
        self.send(type="tests_run", ids=["ctest:alpha", "ctest:beta"])
        self.wait_for_run()
        run = self.tests._run
        self.assertEqual(run.rerun, ["ctest:beta"])           # decision 3: short set, re-run once
        signal = self.signals()["ctest:beta"]
        self.assertEqual(signal.state, "open")                # fail-fail is broken and opens now
        self.assertEqual(signal.count, 2)
        self.assertEqual(signal.kind, "broken")
        self.assertNotIn("ctest:alpha", self.signals())
        self.assertEqual(run.opened, ["ctest:beta"])

    def test_the_re_run_is_its_own_run_in_the_store_and_carries_the_tree_digest(self):
        self.send(type="tests_run", ids=["ctest:beta"])
        self.wait_for_run()
        rows = [r for r in H.read(self.tests.store_path()) if r.id == "ctest:beta"]
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1].run_id, rows[0].run_id + TP.RERUN_SUFFIX)
        # The project is not a git repository, so the digest is empty rather than wrong.
        self.assertEqual({r.tree_digest for r in rows}, {""})

    def test_a_run_that_repeated_is_not_re_run_again(self):
        self.send(type="tests_run", ids=["ctest:beta"], repeat_until_fail=3)
        self.wait_for_run()
        self.assertEqual(self.tests._run.rerun, [])

    def test_the_run_line_names_the_pane_so_the_gate_knows_whose_run_it_was(self):
        self.send(type="tests_run", ids=["ctest:beta"])
        self.wait_for_run()
        runs = [e for e in self.S.read_events(self.tests.signals_path())
                if e["action"] == "run"]
        self.assertEqual({e["session"] for e in runs}, {self.tests.pane_token})
        self.assertEqual(self.signals()["ctest:beta"].first_session, self.tests.pane_token)

    def test_the_fold_pushes_signals_changed_once_per_change_and_not_again(self):
        self.send(type="tests_run", ids=["ctest:beta"])
        self.wait_for_run()
        events = self.changed()
        self.assertTrue(events)
        self.assertEqual([row["key"] for row in events[-1]["open"]], ["ctest:beta"])
        self.assertEqual(set(events[-1]) - {"event"},
                         {"open", "dismissed", "pending_count", "dismissed_count", "promoted",
                          # The worker's own two, beside the fold's (#AQ6X step 7b): the board's
                          # `signals.auto_work`, and the signal threads running right now — which
                          # is how a pane that opened after one started can still draw its chip.
                          "auto_work", "threads"})
        before = len(self.changed())
        self.send(type="tests_list")                          # nothing changed: nothing re-sent
        self.assertEqual(len(self.changed()), before)

    def test_the_ingest_of_another_machines_run_folds_signals_too(self):
        # Two incoming folders, each with `beta` failing: two consecutive failing executions.
        for name, run_id in (("sphinxpad-1", "remote-1"), ("sphinxpad-2", "remote-2")):
            folder = self.incoming(name, run_id, results=())
            (folder / "ctest.xml").write_text(
                '<?xml version="1.0"?><testsuite name="ctest" tests="1">'
                '<testcase classname="beta" name="beta" time="0.5">'
                '<failure message="assertion failed">at line 12</failure></testcase>'
                "</testsuite>", encoding="utf-8")
        self.send(type="tests_list")
        signal = self.signals()["ctest:beta"]
        self.assertEqual(signal.state, "open")
        self.assertEqual(signal.fingerprint, "assertion failed")

    def test_a_key_that_left_discovery_is_removed_by_the_fold_that_knows_what_is_collected(self):
        H.append([H.Execution(ts=f"2026-09-0{day}T10:00:00Z", id="ctest:vanished", result="fail",
                              runner="ctest", run_id=f"r{day}", commit=f"c{day}")
                  for day in (1, 2)], self.tests.store_path())
        self.assertEqual(self.signals()["ctest:vanished"].state, "open")   # no discovery here
        # `tests_list` discovers, and the fold it runs is the only one that can say `removed`:
        # the fake ctest collects alpha, beta and slow, and never `vanished`.
        self.send(type="tests_list")
        folded = self.tests.signal_state(discovered=P.discover(self.project,
                                                               build_dir=self.build))
        self.assertEqual(folded["ctest:vanished"].state, "removed")

    # ---- the five requests ----------------------------------------------------
    def open_one(self):
        self.send(type="tests_run", ids=["ctest:beta"])
        self.wait_for_run()
        self.events.clear()
        return "ctest:beta"

    def test_signals_list_answers_with_signals_changed_and_the_request_id(self):
        key = self.open_one()
        self.send(type="signals_list", id="r1")
        event = self.changed()[-1]
        self.assertEqual(event["id"], "r1")
        self.assertEqual([row["key"] for row in event["open"]], [key])

    def test_signals_claim_writes_the_panes_token_and_refuses_a_second_claimant(self):
        key = self.open_one()
        self.send(type="signals_claim", key=key, pane_token="pane-a")
        self.assertEqual(self.written()[-1], {"event": "signals_written", "kind": "claim",
                                              "key": key, "session": "pane-a"})
        self.assertEqual(self.signals()[key].session, "pane-a")
        self.send(type="signals_claim", key=key, pane_token="pane-b")
        refused = self.written()[-1]
        self.assertEqual(refused["code"], "board_claimed_elsewhere")
        self.assertEqual(self.signals()[key].session, "pane-a")
        self.send(type="signals_claim", key=key, pane_token="pane-b", force=True)
        self.assertEqual(self.signals()[key].session, "pane-b")

    def test_signals_release_frees_it_and_records_the_reason(self):
        key = self.open_one()
        self.send(type="signals_claim", key=key, pane_token="pane-a")
        self.send(type="signals_release", key=key, reason="the user asked for something else")
        self.assertEqual(self.written()[-1]["kind"], "release")
        self.assertEqual(self.signals()[key].session, "")

    def test_the_owners_dismissal_reaches_all_four_reasons_unlike_the_agents(self):
        key = self.open_one()
        self.send(type="signals_dismiss", key=key, reason="wont-fix",
                  comment="the test is wrong", until="2027-01-01")
        written = self.written()[-1]
        self.assertEqual(written["kind"], "dismiss")
        self.assertEqual(written["reason"], "wont-fix")
        self.assertEqual(self.signals()[key].state, "dismissed")
        self.assertEqual(self.changed()[-1]["dismissed_count"], 1)

    def test_a_dismissal_with_no_expiry_is_refused_here_too(self):
        key = self.open_one()
        self.send(type="signals_dismiss", key=key, reason="wont-fix", comment="c")
        self.assertEqual(self.written()[-1]["code"], "signal_until")
        self.assertEqual(self.signals()[key].state, "open")

    def test_signals_promote_writes_the_card_and_names_it_on_the_event(self):
        key = self.open_one()
        self.send(type="signals_promote", key=key)
        written = self.written()[-1]
        self.assertEqual(written["kind"], "promote")
        self.assertTrue(written["card"])
        card = self.board.card_by_id(written["card"])
        self.assertEqual(card.front["links"]["signal"], key)
        self.assertEqual(B.section_text(card.body, self.S.SIGNAL_HEADING).count("`ctest:beta`"), 1)
        self.assertEqual([row["card"] for row in self.changed()[-1]["promoted"]],
                         [written["card"]])

    def test_an_unknown_or_resolved_key_is_refused_by_name(self):
        for kind in ("signals_claim", "signals_release", "signals_dismiss", "signals_promote"):
            with self.subTest(kind=kind):
                self.send(type=kind, key="ctest:nothing")
                self.assertEqual(self.written()[-1]["code"], "signal_not_found")
                self.send(type=kind)
                self.assertEqual(self.written()[-1]["code"], "signal_refused")

    # ---- the verification gate (step 5) ---------------------------------------
    def test_tests_check_separates_what_blocks_this_card_from_what_was_open_before(self):
        key = self.open_one()
        self.card("BBB1", "A card whose pane broke beta", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        card = self.board.card_by_id("BBB1")
        card.set("session", self.tests.pane_token)
        self.board.save(card)
        result = self.tests.check_card("BBB1")
        self.assertEqual([row["key"] for row in result["blocks"]], [key])
        self.assertEqual(result["open_before"], [])
        self.assertIn("stop this card leaving needs-verification", TP.format_findings(result))

    def test_a_signal_another_pane_opened_is_listed_as_open_before_and_blocks_nothing(self):
        key = self.open_one()
        self.card("BBB2", "A card that did not break it", "needs-verification",
                  body="\n## Tests\n- `ctest -R alpha`\n")
        card = self.board.card_by_id("BBB2")
        card.set("session", "some-other-pane")
        self.board.save(card)
        result = self.tests.check_card("BBB2")
        self.assertEqual(result["blocks"], [])
        self.assertEqual([row["key"] for row in result["open_before"]], [key])
        self.assertIn("do not block it", TP.format_findings(result))

    def test_a_card_with_no_tests_section_still_carries_the_two_lists(self):
        self.open_one()
        self.card("BBB3", "No tests named", "needs-verification")
        result = self.tests.check_card("BBB3")
        self.assertEqual(result["blocks"], [])
        self.assertEqual(len(result["open_before"]), 1)

    # ---- the whole life of one signal ------------------------------------------
    def test_a_run_opens_it_a_pane_claims_it_and_the_fix_resolves_it_after_two_passes(self):
        """The card's own Verify line, end to end through the real handler and a real `ctest`."""
        self.send(type="tests_run", ids=["ctest:beta"])
        self.wait_for_run()
        self.assertEqual(self.signals()["ctest:beta"].state, "open")

        self.send(type="signals_claim", key="ctest:beta", pane_token="pane-a")
        self.assertEqual(self.signals()["ctest:beta"].session, "pane-a")

        # Somebody fixes it. One passing run is not a fix (`RESOLVE_PASSES["broken"]` is 2) —
        # and the run's own re-run does not count as the second, because a run that had no
        # failures is not re-run at all.
        os.environ["CTEST_SHIM_FIXED"] = "beta"
        self.send(type="tests_run", ids=["ctest:beta"])
        self.wait_for_run()
        self.assertEqual(self.tests._run.rerun, [])
        self.assertEqual(self.signals()["ctest:beta"].state, "open")
        self.assertEqual(self.signals()["ctest:beta"].green_streak, 1)

        self.send(type="tests_run", ids=["ctest:beta"])
        self.wait_for_run()
        signal = self.signals()["ctest:beta"]
        self.assertEqual(signal.state, "resolved")
        self.assertEqual(signal.session, "")                  # a resolved signal is nobody's
        self.assertEqual([row["key"] for row in self.changed()[-1]["open"]], [])

    # ---- auto-promotion -------------------------------------------------------
    def test_a_signal_that_has_failed_three_runs_over_a_day_is_promoted_by_the_fold(self):
        H.append([H.Execution(ts=f"2026-09-0{day}T10:00:00Z", id="ctest:beta", result="fail",
                              runner="ctest", run_id=f"r{day}", commit=f"c{day}",
                              message="assertion failed")
                  for day in (1, 2, 3)], self.tests.store_path())
        signals = self.tests.fold_signals()
        signal = signals["ctest:beta"]
        self.assertTrue(signal.card, signal)
        card = self.board.card_by_id(signal.card)
        self.assertIn(self.S.SIGNAL_LABEL, card.front["labels"])
        self.assertIn("assertion failed", card.body)
        # …and folding again does not file a second card.
        again = self.tests.fold_signals()
        self.assertEqual(again["ctest:beta"].card, signal.card)



if __name__ == "__main__":                                    # pragma: no cover
    unittest.main()
