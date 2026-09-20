# SPDX-License-Identifier: AGPL-3.0-or-later
"""Offline test discovery: ctest through `--show-only=json-v1`, unittest through `ast`.

Every case builds a throwaway project in a temp directory; the ctest side uses a fake `ctest`
executable that prints a recorded listing, so nothing here needs CMake, a build, or a network.
"""
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import test_probe as P  # noqa: E402


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def fake_ctest(directory: Path, payload: dict | str, *, exit_code: int = 0) -> str:
    """A `ctest` stand-in that prints `payload` and exits `exit_code`.  Returns its path."""
    body = payload if isinstance(payload, str) else json.dumps(payload)
    script = directory / "fake-ctest"
    script.write_text(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        f"sys.stdout.write({body!r})\n"
        f"sys.exit({exit_code})\n",
        encoding="utf-8")
    script.chmod(script.stat().st_mode | stat.S_IXUSR)
    return str(script)


LISTING = {
    "kind": "ctestInfo",
    "version": {"major": 1, "minor": 0},
    "tests": [
        {"name": "editor",
         "command": ["/build/relay-editor-tests"],
         "properties": [{"name": "WORKING_DIRECTORY", "value": "/build"}]},
        {"name": "panelayout",
         "command": ["/build/relay-panelayout-tests"],
         "properties": [{"name": "LABELS", "value": ["panes", "gui"]},
                        {"name": "TIMEOUT", "value": 120.0}]},
        {"name": "legacy",
         "command": ["/build/relay-legacy-tests"],
         "properties": [{"name": "DISABLED", "value": "ON"}]},
    ],
}


class CtestDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.project = Path(self.tmp.name) / "proj"
        self.build = self.project / "build"
        self.build.mkdir(parents=True)
        (self.build / "CTestTestfile.cmake").write_text("# ctest\n", encoding="utf-8")
        write(self.project / "tests" / "editor_test.cpp", "int main() { return 0; }\n")
        write(self.project / "tests" / "panelayout_test.cpp", "int main() { return 0; }\n")
        self.addCleanup(self.tmp.cleanup)

    def test_names_ids_labels_and_disabled(self):
        ctest = fake_ctest(Path(self.tmp.name), LISTING)
        found = P.discover_ctest(self.project, self.build, ctest=ctest)
        by_id = {t.id: t for t in found}
        self.assertEqual(sorted(by_id), ["ctest:editor", "ctest:legacy", "ctest:panelayout"])
        self.assertEqual(by_id["ctest:panelayout"].labels, ["panes", "gui"])
        self.assertEqual(by_id["ctest:panelayout"].invocation, "ctest -R panelayout")
        self.assertTrue(by_id["ctest:legacy"].disabled)
        self.assertFalse(by_id["ctest:editor"].disabled)

    def test_source_from_the_command_path_convention(self):
        ctest = fake_ctest(Path(self.tmp.name), LISTING)
        by_id = {t.id: t for t in P.discover_ctest(self.project, self.build, ctest=ctest)}
        self.assertEqual(by_id["ctest:editor"].file, "tests/editor_test.cpp")
        self.assertEqual(by_id["ctest:panelayout"].file, "tests/panelayout_test.cpp")
        self.assertEqual(by_id["ctest:legacy"].file, "")          # no such source: honest ""
        self.assertTrue(by_id["ctest:editor"].source_hash.startswith("sha256:"))
        self.assertEqual(by_id["ctest:legacy"].source_hash, "")

    def test_source_from_add_executable_when_the_name_does_not_match(self):
        write(self.project / "CMakeLists.txt",
              "add_executable(relay-odd-tests tests/weird_name.cpp)\n"
              "add_test(NAME odd COMMAND relay-odd-tests)\n")
        write(self.project / "tests" / "weird_name.cpp", "int main() { return 0; }\n")
        listing = {"tests": [{"name": "odd", "command": ["/build/relay-odd-tests"]}]}
        ctest = fake_ctest(Path(self.tmp.name), listing)
        found = P.discover_ctest(self.project, self.build, ctest=ctest)
        self.assertEqual([t.file for t in found], ["tests/weird_name.cpp"])

    def test_source_from_a_subdirectory_cmakelists(self):
        write(self.project / "engine" / "CMakeLists.txt",
              "add_executable(relay-engine-tests\n"
              "    tests/main.cpp tests/CoreTest.cpp)\n")
        listing = {"tests": [{"name": "relay-engine-tests",
                              "command": ["/build/engine/relay-engine-tests"]}]}
        ctest = fake_ctest(Path(self.tmp.name), listing)
        found = P.discover_ctest(self.project, self.build, ctest=ctest)
        self.assertEqual([t.file for t in found], ["engine/tests/main.cpp"])

    def test_absent_build_dir_is_no_tests_not_an_error(self):
        ctest = fake_ctest(Path(self.tmp.name), LISTING)
        self.assertEqual(P.discover_ctest(self.project, self.project / "nope", ctest=ctest), [])

    def test_a_directory_without_ctesttestfile_starts_no_process(self):
        bare = self.project / "bare"
        bare.mkdir()
        self.assertEqual(P.discover_ctest(self.project, bare, ctest="/nonexistent/ctest"), [])

    def test_missing_ctest_binary_is_no_tests(self):
        self.assertEqual(
            P.discover_ctest(self.project, self.build, ctest="/nonexistent/ctest-binary"), [])

    def test_non_zero_exit_and_garbage_are_no_tests(self):
        bad = fake_ctest(Path(self.tmp.name), LISTING, exit_code=1)
        self.assertEqual(P.discover_ctest(self.project, self.build, ctest=bad), [])
        junk = fake_ctest(Path(self.tmp.name), "not json at all")
        self.assertEqual(P.discover_ctest(self.project, self.build, ctest=junk), [])

    def test_a_slow_ctest_is_bounded_by_the_timeout(self):
        script = Path(self.tmp.name) / "slow-ctest"
        script.write_text("#!/usr/bin/env python3\nimport time\ntime.sleep(30)\n",
                          encoding="utf-8")
        script.chmod(script.stat().st_mode | stat.S_IXUSR)
        self.assertEqual(
            P.discover_ctest(self.project, self.build, ctest=str(script), timeout=0.4), [])


PY_SOURCE = '''
import unittest


class Helper:
    def test_not_a_case(self):
        pass


class CardTests(unittest.TestCase):
    def test_roundtrip(self):
        pass

    def test_merge(self):
        pass

    @unittest.skip("needs a board")
    def test_skipped(self):
        pass

    def helper(self):
        pass


class DerivedTests(CardTests):
    def test_derived(self):
        pass


@unittest.skipUnless(False, "never here")
class AllSkipped(unittest.TestCase):
    def test_one(self):
        pass
'''


class UnittestDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.project = Path(self.tmp.name)
        write(self.project / "tests" / "test_board.py", PY_SOURCE)
        self.addCleanup(self.tmp.cleanup)

    def test_ids_lines_and_skips(self):
        found = P.discover_unittest(self.project)
        by_id = {t.id: t for t in found}
        self.assertIn("unittest:tests.test_board.CardTests.test_roundtrip", by_id)
        self.assertIn("unittest:tests.test_board.DerivedTests.test_derived", by_id)
        self.assertNotIn("unittest:tests.test_board.Helper.test_not_a_case", by_id)
        self.assertNotIn("unittest:tests.test_board.CardTests.helper", by_id)
        one = by_id["unittest:tests.test_board.CardTests.test_roundtrip"]
        self.assertEqual(one.runner, "unittest")
        self.assertEqual(one.file, "tests/test_board.py")
        self.assertEqual(one.name, "CardTests.test_roundtrip")
        self.assertEqual(one.invocation, "tests/test_board.py::CardTests::test_roundtrip")
        self.assertGreater(one.line, 1)
        self.assertEqual(PY_SOURCE.splitlines()[one.line - 1].strip(), "def test_roundtrip(self):")
        self.assertTrue(by_id["unittest:tests.test_board.CardTests.test_skipped"].disabled)
        self.assertEqual(
            by_id["unittest:tests.test_board.CardTests.test_skipped"].skip_reason,
            "needs a board")
        self.assertTrue(by_id["unittest:tests.test_board.AllSkipped.test_one"].disabled)

    def test_nothing_is_imported(self):
        write(self.project / "tests" / "test_explodes.py",
              "import unittest\n"
              "raise SystemExit('import-time boom')\n"
              "class T(unittest.TestCase):\n"
              "    def test_a(self):\n        pass\n")
        ids = {t.id for t in P.discover_unittest(self.project)}
        self.assertIn("unittest:tests.test_explodes.T.test_a", ids)

    def test_a_syntax_error_is_skipped_not_raised(self):
        write(self.project / "tests" / "test_broken.py", "def (:\n")
        ids = {t.id for t in P.discover_unittest(self.project)}
        self.assertTrue(ids)
        self.assertFalse(any(i.startswith("unittest:tests.test_broken") for i in ids))

    def test_the_glob_and_roots_are_parameters(self):
        write(self.project / "suite" / "check_thing.py", PY_SOURCE)
        self.assertEqual(P.discover_unittest(self.project, roots=["suite"]), [])
        found = P.discover_unittest(self.project, roots=["suite"], glob="check_*.py")
        self.assertIn("unittest:suite.check_thing.CardTests.test_roundtrip",
                      {t.id for t in found})

    def test_source_hash_changes_when_the_file_changes(self):
        before = {t.id: t.source_hash for t in P.discover_unittest(self.project)}
        write(self.project / "tests" / "test_board.py", PY_SOURCE + "\n# a comment\n")
        after = {t.id: t.source_hash for t in P.discover_unittest(self.project)}
        self.assertTrue(before)
        self.assertNotEqual(before, after)
        self.assertTrue(all(h.startswith("sha256:") for h in after.values()))

    def test_nested_test_dirs_are_walked(self):
        write(self.project / "tests" / "deep" / "test_more.py", PY_SOURCE)
        ids = {t.id for t in P.discover_unittest(self.project)}
        self.assertIn("unittest:tests.deep.test_more.CardTests.test_merge", ids)


class DiscoverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.project = Path(self.tmp.name) / "proj"
        (self.project / "build").mkdir(parents=True)
        (self.project / "build" / "CTestTestfile.cmake").write_text("#\n", encoding="utf-8")
        write(self.project / "tests" / "editor_test.cpp", "int main(){return 0;}\n")
        write(self.project / "tests" / "test_board.py", PY_SOURCE)
        self.addCleanup(self.tmp.cleanup)

    def test_the_result_shape_and_counts(self):
        ctest = fake_ctest(Path(self.tmp.name), LISTING)
        out = P.discover(self.project, ctest=ctest)
        self.assertEqual(out["version"], P.PROBE_VERSION)
        self.assertEqual(out["project"], str(self.project))
        self.assertEqual(out["counts"]["ctest"], 3)
        self.assertEqual(out["counts"]["unittest"], 5)
        self.assertEqual(out["counts"]["total"], 8)
        self.assertFalse(out["truncated"])
        json.dumps(out)                                   # must be serialisable as it stands
        one = [t for t in out["tests"] if t["id"] == "ctest:editor"][0]
        self.assertEqual(set(one) >= {"id", "name", "runner", "file", "labels", "invocation"},
                         True)

    def test_an_empty_directory_is_an_empty_result_not_an_error(self):
        empty = Path(self.tmp.name) / "empty"
        empty.mkdir()
        out = P.discover(empty, ctest="/nonexistent/ctest")
        self.assertEqual(out["counts"]["total"], 0)
        self.assertEqual(out["tests"], [])

    def test_a_missing_project_is_an_empty_result(self):
        out = P.discover(Path(self.tmp.name) / "no-such-project", ctest="/nonexistent/ctest")
        self.assertEqual(out["tests"], [])

    def test_runners_can_be_narrowed(self):
        ctest = fake_ctest(Path(self.tmp.name), LISTING)
        out = P.discover(self.project, ctest=ctest, runners=["unittest"])
        self.assertEqual(out["counts"]["ctest"], 0)
        self.assertEqual(out["counts"]["unittest"], 5)

    def test_a_symlink_out_of_the_project_is_not_followed(self):
        outside = Path(self.tmp.name) / "outside"
        write(outside / "test_secret.py", PY_SOURCE)
        link = self.project / "tests" / "escape"
        try:
            os.symlink(outside, link)
        except OSError:                                   # pragma: no cover - no symlink support
            self.skipTest("symlinks unavailable")
        ids = {t["id"] for t in P.discover(self.project, ctest="/nonexistent/ctest")["tests"]}
        self.assertFalse(any("test_secret" in i for i in ids))


if __name__ == "__main__":                                # pragma: no cover
    unittest.main()
