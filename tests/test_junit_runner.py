# SPDX-License-Identifier: AGPL-3.0-or-later
"""The stdlib JUnit writer: one `<testcase>` per Python test, with time, file, line and outcome.

Every case writes a throwaway test module into a temp directory and runs `junit_runner.main()`
on it in a subprocess, so the runner under test is never confused with the runner running it.
"""
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))

from relay_core import junit_runner as J  # noqa: E402

SAMPLE = '''
import unittest


class SampleTests(unittest.TestCase):
    def test_passes(self):
        self.assertTrue(True)

    def test_fails(self):
        self.assertEqual(1, 2, "one is not two")

    def test_errors(self):
        raise RuntimeError("boom")

    @unittest.skip("not today")
    def test_skipped(self):
        pass
'''


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "tests").mkdir()
        (self.root / "tests" / "test_sample.py").write_text(SAMPLE, encoding="utf-8")
        self.out = self.root / "out" / "junit.xml"
        self.addCleanup(self.tmp.cleanup)

    def run_runner(self, *args):
        done = subprocess.run(
            [sys.executable, "-m", "relay_core.junit_runner",
             "--junit", str(self.out), "-s", "tests", "-q", *args],
            cwd=str(self.root), capture_output=True, text=True, timeout=120,
            env={"PATH": "/usr/bin:/bin", "PYTHONPATH": str(REPO / "backend"),
                 "PYTHONDONTWRITEBYTECODE": "1", "HOME": str(self.root)})
        return done

    def test_writes_one_testcase_per_test_with_outcomes(self):
        done = self.run_runner()
        self.assertEqual(done.returncode, 1, done.stderr)     # the sample has a failure
        root = ET.parse(self.out).getroot()
        self.assertEqual(root.tag, "testsuite")
        self.assertEqual(root.get("tests"), "4")
        self.assertEqual(root.get("failures"), "1")
        self.assertEqual(root.get("errors"), "1")
        self.assertEqual(root.get("skipped"), "1")
        by_name = {c.get("name"): c for c in root.findall("testcase")}
        self.assertEqual(sorted(by_name),
                         ["test_errors", "test_fails", "test_passes", "test_skipped"])
        self.assertIsNone(by_name["test_passes"].find("failure"))
        self.assertIn("one is not two", by_name["test_fails"].find("failure").get("message"))
        self.assertIn("boom", by_name["test_errors"].find("error").get("message"))
        self.assertEqual(by_name["test_skipped"].find("skipped").get("message"), "not today")

    def test_classname_file_and_line_point_at_the_source(self):
        self.run_runner()
        root = ET.parse(self.out).getroot()
        case = {c.get("name"): c for c in root.findall("testcase")}["test_passes"]
        self.assertEqual(case.get("classname"), "tests.test_sample.SampleTests")
        self.assertEqual(case.get("file"), "tests/test_sample.py")
        line = int(case.get("line"))
        self.assertEqual(SAMPLE.splitlines()[line - 1].strip(), "def test_passes(self):")

    def test_classname_matches_the_probes_unittest_id(self):
        from relay_core import test_probe as P
        self.run_runner()
        root = ET.parse(self.out).getroot()
        case = {c.get("name"): c for c in root.findall("testcase")}["test_passes"]
        wire_id = f"unittest:{case.get('classname')}.{case.get('name')}"
        discovered = {t.id for t in P.discover_unittest(self.root)}
        self.assertIn(wire_id, discovered)

    def test_every_case_carries_a_duration(self):
        self.run_runner()
        root = ET.parse(self.out).getroot()
        times = [float(c.get("time")) for c in root.findall("testcase")]
        self.assertEqual(len(times), 4)
        self.assertTrue(all(t >= 0.0 for t in times))
        self.assertAlmostEqual(float(root.get("time")), sum(times), places=5)

    def test_exit_code_is_zero_when_everything_passes(self):
        (self.root / "tests" / "test_sample.py").write_text(
            "import unittest\n\n\nclass OkTests(unittest.TestCase):\n"
            "    def test_ok(self):\n        pass\n", encoding="utf-8")
        done = self.run_runner()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(ET.parse(self.out).getroot().get("failures"), "0")

    def test_named_tests_run_instead_of_discovery(self):
        done = subprocess.run(
            [sys.executable, "-m", "relay_core.junit_runner", "--junit", str(self.out), "-q",
             "tests.test_sample.SampleTests.test_passes"],
            cwd=str(self.root), capture_output=True, text=True, timeout=120,
            env={"PATH": "/usr/bin:/bin",
                 "PYTHONPATH": f"{self.root}:{REPO / 'backend'}",
                 "PYTHONDONTWRITEBYTECODE": "1", "HOME": str(self.root)})
        self.assertEqual(done.returncode, 0, done.stderr)
        names = [c.get("name") for c in ET.parse(self.out).getroot().findall("testcase")]
        self.assertEqual(names, ["test_passes"])

    def test_the_pattern_is_a_parameter(self):
        (self.root / "tests" / "check_other.py").write_text(
            "import unittest\n\n\nclass OtherTests(unittest.TestCase):\n"
            "    def test_other(self):\n        pass\n", encoding="utf-8")
        self.run_runner("-p", "check_*.py")
        names = [c.get("name") for c in ET.parse(self.out).getroot().findall("testcase")]
        self.assertEqual(names, ["test_other"])

    def test_no_junit_flag_writes_no_file(self):
        done = subprocess.run(
            [sys.executable, "-m", "relay_core.junit_runner", "-s", "tests", "-q"],
            cwd=str(self.root), capture_output=True, text=True, timeout=120,
            env={"PATH": "/usr/bin:/bin", "PYTHONPATH": str(REPO / "backend"),
                 "PYTHONDONTWRITEBYTECODE": "1", "HOME": str(self.root)})
        self.assertEqual(done.returncode, 1)
        self.assertFalse(self.out.exists())


class ResultUnitTests(unittest.TestCase):
    """The `TestResult` on its own, without a subprocess."""

    def make_result(self):
        import io
        return J.JUnitResult(io.StringIO(), True, 0, root=REPO)

    def test_the_outcome_lands_on_the_test_it_belongs_to(self):
        """unittest calls addFailure before stopTest; the case must still be the failing one."""
        class Pair(unittest.TestCase):
            def test_a(self):
                self.fail("a fails")

            def test_b(self):
                pass

        result = self.make_result()
        suite = unittest.TestSuite([Pair("test_a"), Pair("test_b")])
        suite.run(result)
        outcomes = {c["name"]: c["result"] for c in result.cases}
        self.assertEqual(outcomes, {"test_a": "fail", "test_b": "pass"})

    def test_an_excerpt_is_bounded(self):
        class Big(unittest.TestCase):
            def test_big(self):
                raise RuntimeError("x" * (J.MAX_EXCERPT * 3))

        result = self.make_result()
        unittest.TestSuite([Big("test_big")]).run(result)
        case = result.cases[0]
        self.assertEqual(case["result"], "error")
        self.assertLessEqual(len(case["excerpt"]), J.MAX_EXCERPT)
        self.assertLessEqual(len(case["message"]), 200)

    def test_expected_failure_and_unexpected_success(self):
        class Odd(unittest.TestCase):
            @unittest.expectedFailure
            def test_expected(self):
                self.fail("as planned")

            @unittest.expectedFailure
            def test_unexpected(self):
                pass

        result = self.make_result()
        unittest.TestSuite([Odd("test_expected"), Odd("test_unexpected")]).run(result)
        outcomes = {c["name"]: c["result"] for c in result.cases}
        self.assertEqual(outcomes, {"test_expected": "skip", "test_unexpected": "fail"})

    def test_xml_is_well_formed_with_no_tests(self):
        result = self.make_result()
        text = ET.tostring(result.to_xml().getroot(), encoding="unicode")
        root = ET.fromstring(text)
        self.assertEqual(root.get("tests"), "0")
        self.assertEqual(root.findall("testcase"), [])


if __name__ == "__main__":                                # pragma: no cover
    unittest.main()
