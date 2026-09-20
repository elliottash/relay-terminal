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
from relay_core import test_history as H           # noqa: E402
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
    ok = name != "beta"
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
        self.assertEqual(set(event) - {"id"},
                         {"event", "project", "tests", "summary", "cards_without_tests"})
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
        self.assertEqual(set(event) - {"id"}, {"event", "card", "findings", "actions"})
        self.assertEqual(event["card"], "AAA1")
        self.assertEqual(len(event["findings"]), 1)
        finding = event["findings"][0]
        self.assertEqual(set(finding), {"test", "verdict", "message", "severity"})
        self.assertEqual(finding["verdict"], "no-tests")
        self.assertIn("has no `## Tests` section", finding["message"])
        self.assertEqual(event["actions"], ["Add the tests this card's commits touched"])

    def test_a_card_whose_test_is_gone(self):
        self.card("AAA2", "Names a test that is not there", "needs-verification",
                  body="\n## Tests\n- `ctest -R vanished`\n")
        self.send(type="tests_check", card="AAA2")
        findings = self.of("tests_check")[0]["findings"]
        self.assertEqual([f["verdict"] for f in findings], ["gone"])
        self.assertEqual(findings[0]["severity"], "failure")

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


if __name__ == "__main__":                                    # pragma: no cover
    unittest.main()
