# SPDX-License-Identifier: AGPL-3.0-or-later
"""`scripts/relay-profile` and the arithmetic behind it (card #7BM4 phase 5).

Two halves, and both run offline:

* `relay_core.profile_convert` against fixtures written here — a `.ninja_log`, folded stacks, a
  speedscope document and a real `cProfile` file — because the numbers in a profile table are the
  thing that has to be right, and none of them needs a profiler to check.
* the **real script**, `build` mode, over a throwaway three-target CMake project built with Ninja:
  the whole path from `.ninja_log` to `summary.md`, `rows.json`, `build.json`, `build.trace.json`
  and `meta.json`. Skipped where cmake or ninja is missing, never pointed at this repo's shared
  `build/`, and it compiles three one-line files, so it costs a couple of seconds.
"""
import cProfile
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import profile_convert as C          # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "relay-profile"

NINJA_LOG = """# ninja log v5
0\t3000\t0\tCMakeFiles/alpha.dir/src/alpha.cpp.o\taaa
100\t9100\t0\tCMakeFiles/beta.dir/src/beta.cpp.o\tbbb
3000\t3500\t0\tlibalpha.a\tccc
9100\t9600\t0\ttiny\tddd
"""

FOLDED = """main;work;inner 30
main;work;other 10
main;idle 10
"""


class ConverterTests(unittest.TestCase):
    def test_ninja_log_becomes_a_per_output_table(self):
        folded = C.fold_ninja(NINJA_LOG)
        self.assertEqual(folded["kind"], "build")
        self.assertEqual(folded["steps"], 4)
        self.assertAlmostEqual(folded["wall"], 9.6)        # first start to last end
        self.assertAlmostEqual(folded["sum"], 3.0 + 9.0 + 0.5 + 0.5)
        self.assertEqual([row["name"] for row in folded["rows"]][:2],
                         ["CMakeFiles/beta.dir/src/beta.cpp.o",
                          "CMakeFiles/alpha.dir/src/alpha.cpp.o"])
        self.assertAlmostEqual(folded["rows"][0]["self"], 9.0)
        self.assertAlmostEqual(folded["rows"][0]["self_pct"], 100 * 9.0 / 13.0, places=1)

    def test_an_output_built_twice_counts_once_at_its_last_time(self):
        log = NINJA_LOG + "9600\t9700\t0\tCMakeFiles/beta.dir/src/beta.cpp.o\tbbb\n"
        folded = C.fold_ninja(log)
        self.assertEqual(folded["steps"], 4)
        beta = [row for row in folded["rows"] if row["name"].endswith("beta.cpp.o")][0]
        self.assertAlmostEqual(beta["self"], 0.1)

    def test_an_empty_log_is_a_sentence_not_a_traceback(self):
        with self.assertRaises(C.ProfileError) as caught:
            C.fold_ninja("# ninja log v5\n")
        self.assertIn("no completed build steps", str(caught.exception))

    def test_the_trace_lays_overlapping_steps_in_lanes(self):
        trace = C.ninja_trace(C.fold_ninja(NINJA_LOG))
        events = {event["name"]: event for event in trace["traceEvents"]}
        self.assertEqual(len(events), 4)
        # alpha (0–3000) and beta (100–9100) overlap, so they are on two lanes; libalpha starts
        # at 3000, when alpha's lane is free again, so it reuses it.
        self.assertNotEqual(events["CMakeFiles/alpha.dir/src/alpha.cpp.o"]["tid"],
                            events["CMakeFiles/beta.dir/src/beta.cpp.o"]["tid"])
        self.assertEqual(events["libalpha.a"]["tid"],
                         events["CMakeFiles/alpha.dir/src/alpha.cpp.o"]["tid"])
        self.assertEqual(events["CMakeFiles/beta.dir/src/beta.cpp.o"]["dur"], 9_000_000)

    def test_folded_stacks_separate_self_from_total(self):
        folded = C.fold_folded(FOLDED)
        rows = {row["name"]: row for row in folded["rows"]}
        self.assertAlmostEqual(rows["main"]["self"], 0.0)
        self.assertAlmostEqual(rows["main"]["total"], 50.0)
        self.assertAlmostEqual(rows["work"]["total"], 40.0)
        self.assertAlmostEqual(rows["inner"]["self"], 30.0)
        self.assertAlmostEqual(rows["inner"]["self_pct"], 60.0)
        self.assertEqual(folded["rows"][0]["name"], "inner")     # sorted by self

    def test_a_recursive_frame_is_counted_once_per_sample(self):
        folded = C.fold_folded("a;b;a;b 10\n")
        rows = {row["name"]: row for row in folded["rows"]}
        self.assertAlmostEqual(rows["a"]["total"], 10.0)
        self.assertAlmostEqual(rows["b"]["total"], 10.0)
        self.assertAlmostEqual(rows["b"]["self"], 10.0)

    def test_speedscope_weights_are_read_in_their_own_unit(self):
        document = {"shared": {"frames": [{"name": "a"}, {"name": "b", "file": "x.py", "line": 4}]},
                    "profiles": [{"type": "sampled", "unit": "milliseconds",
                                  "samples": [[0, 1], [0]], "weights": [500, 1500]}]}
        folded = C.fold_speedscope(document)
        rows = {row["name"]: row for row in folded["rows"]}
        self.assertAlmostEqual(folded["wall"], 2.0)
        self.assertAlmostEqual(rows["a"]["self"], 1.5)
        self.assertAlmostEqual(rows["b"]["self"], 0.5)
        self.assertEqual(rows["b"]["file"], "x.py")
        self.assertEqual(rows["b"]["line"], 4)

    def test_pstats_round_trips_through_speedscope(self):
        def leaf():
            return sum(i * i for i in range(60000))

        def middle():
            return leaf() + leaf()

        with tempfile.TemporaryDirectory() as tmp:
            prof = Path(tmp) / "run.prof"
            cProfile.runctx("middle()", {"middle": middle, "leaf": leaf}, {}, str(prof))
            document = C.pstats_to_speedscope(prof)
            self.assertEqual(document["$schema"], C.SCHEMA)
            self.assertEqual(len(document["profiles"]), 1)
            names = [frame["name"] for frame in document["shared"]["frames"]]
            self.assertTrue(any(name.startswith("leaf ") for name in names), names)
            folded = C.fold_speedscope(document)
            rows = {row["name"].split(" ")[0]: row for row in folded["rows"]}
            self.assertIn("leaf", rows)
            # The stack is caller-first, so `leaf` is inside `middle`: its total is the larger.
            self.assertGreaterEqual(
                rows["middle"]["total"], rows["leaf"]["self"],
                # #PF14: preserve the failed input, not just its rounded output totals.
                # This profile contains only this test's generated workload.
                msg=f"raw pstats: {C._pstats_entries(prof)!r}; speedscope: {document!r}")
            self.assertGreater(folded["wall"], 0.0)
            # And the file that is not a profile is a sentence.
            other = Path(tmp) / "not.prof"
            other.write_text("hello")
            with self.assertRaises(C.ProfileError):
                C.pstats_to_speedscope(other)

    def test_detection_is_by_content(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "anything"
            log.write_text(NINJA_LOG)
            self.assertEqual(C.fold_file(log)["kind"], "build")
            stacks = Path(tmp) / "whatever.json.txt"
            stacks.write_text(FOLDED)
            self.assertEqual(C.fold_file(stacks)["kind"], "profile")

    def test_the_markdown_table_says_what_kind_it_is(self):
        build = C.markdown_table(C.fold_ninja(NINJA_LOG))
        self.assertIn("| Output | Seconds | Share |", build)
        self.assertIn("4 steps", build)
        profile = C.markdown_table(C.fold_folded(FOLDED))
        self.assertIn("| Function | Self | Self % | Total | Total % |", profile)


TINY = {
    "CMakeLists.txt": """cmake_minimum_required(VERSION 3.16)
project(tiny CXX)
add_library(alpha src/alpha.cpp)
add_library(beta src/beta.cpp)
add_executable(tiny src/main.cpp)
target_link_libraries(tiny alpha beta)
""",
    "src/alpha.cpp": "#include <string>\nint alpha() { return (int)std::string(\"a\").size(); }\n",
    "src/beta.cpp": "#include <vector>\nint beta() { return (int)std::vector<int>{1}.size(); }\n",
    "src/main.cpp": "int alpha(); int beta();\nint main() { return alpha() + beta() - 2; }\n",
}


@unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"),
                     "cmake and ninja are needed for the build target")
class BuildModeTests(unittest.TestCase):
    """The real script, `build --build-dir`, over three one-line translation units."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="relay-profile-build-")
        cls.project = Path(cls.tmp.name) / "tiny"
        for name, text in TINY.items():
            path = cls.project / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        cls.build = Path(cls.tmp.name) / "build"
        subprocess.run(["cmake", "-S", str(cls.project), "-B", str(cls.build), "-G", "Ninja"],
                       check=True, capture_output=True)
        subprocess.run(["cmake", "--build", str(cls.build)], check=True, capture_output=True)
        cls.out = Path(cls.tmp.name) / "evidence"
        cls.result = subprocess.run(
            [str(SCRIPT), "build", "--build-dir", str(cls.build), "--out", str(cls.out)],
            capture_output=True, text=True, cwd=str(ROOT), timeout=300)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_it_succeeded_and_printed_where_it_wrote(self):
        self.assertEqual(self.result.returncode, 0, self.result.stderr)
        self.assertIn(str(self.out), self.result.stdout)
        self.assertIn("scripts/relay-speedscope", self.result.stdout)

    def test_the_five_files_are_there(self):
        for name in ("summary.md", "rows.json", "build.json", "build.trace.json", "meta.json",
                     "ninja_log"):
            self.assertTrue((self.out / name).is_file(), name)

    def test_the_summary_table_names_every_object(self):
        text = (self.out / "summary.md").read_text()
        self.assertIn("| Output | Seconds | Share |", text)
        for unit in ("alpha.cpp.o", "beta.cpp.o", "main.cpp.o"):
            self.assertIn(unit, text)
        self.assertIn("steps ·", text)

    def test_rows_json_is_the_table_the_wire_carries(self):
        rows = json.loads((self.out / "rows.json").read_text())
        self.assertEqual(rows["kind"], "build")
        self.assertLessEqual(len(rows["rows"]), 25)
        self.assertEqual(rows["total_rows"], json.loads((self.out / "build.json").read_text())
                         and len(json.loads((self.out / "build.json").read_text())["rows"]))
        for row in rows["rows"]:
            self.assertGreaterEqual(row["self"], 0.0)
            self.assertIn("self_pct", row)
        self.assertAlmostEqual(sum(row["self_pct"] for row in rows["rows"]), 100.0, places=0)

    def test_meta_json_says_what_ran_where(self):
        meta = json.loads((self.out / "meta.json").read_text())
        self.assertEqual(meta["mode"], "build")
        self.assertIn("relay-profile build", meta["command"])
        self.assertIn(str(self.build), meta["command"])
        for key in ("target", "host", "commit", "started", "finished", "out", "tool_versions"):
            self.assertIn(key, meta)
        self.assertIn("ninja", meta["tool_versions"])
        self.assertEqual(meta["raw"][0], "ninja_log")

    def test_it_builds_the_project_it_is_pointed_at(self):
        """`RELAY_PROFILE_PROJECT`: the script lives in Relay's checkout, not the project's.

        Without `--build-dir` it configures a Ninja directory of its own and builds; the project
        it builds is the one the board worker named, which is how a board on any other project
        gets a build profile at all.
        """
        out = Path(self.tmp.name) / "own-build"
        env = dict(os.environ, RELAY_PROFILE_PROJECT=str(self.project),
                   RELAY_PROFILE_BUILD_DIR=str(Path(self.tmp.name) / "own-ninja"))
        result = subprocess.run([str(SCRIPT), "build", "--out", str(out)],
                                capture_output=True, text=True, env=env, timeout=600)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("building everything", result.stdout)     # no `relay` target outside this repo
        rows = json.loads((out / "rows.json").read_text())
        names = [row["name"] for row in rows["rows"]]
        self.assertTrue(any(name.endswith("alpha.cpp.o") for name in names), names)
        self.assertTrue((Path(self.tmp.name) / "own-ninja" / ".ninja_log").is_file())

    def test_a_project_with_no_cmakelists_is_a_sentence(self):
        empty = Path(self.tmp.name) / "empty"
        empty.mkdir(exist_ok=True)
        env = dict(os.environ, RELAY_PROFILE_PROJECT=str(empty),
                   RELAY_PROFILE_BUILD_DIR=str(Path(self.tmp.name) / "empty-ninja"))
        result = subprocess.run([str(SCRIPT), "build", "--out", str(Path(self.tmp.name) / "empty-out")],
                                capture_output=True, text=True, env=env, timeout=120)
        self.assertEqual(result.returncode, 3)
        self.assertIn("has no CMakeLists.txt", result.stderr)

    def test_a_directory_that_is_not_a_ninja_build_is_a_sentence(self):
        result = subprocess.run(
            [str(SCRIPT), "build", "--build-dir", str(self.project),
             "--out", str(self.out.parent / "nope")],
            capture_output=True, text=True, cwd=str(ROOT), timeout=120)
        self.assertEqual(result.returncode, 3)
        self.assertIn("has no .ninja_log", result.stderr)

    def test_the_worker_reads_the_same_directory_back(self):
        from relay_core import profile_protocol as PP
        summary = PP.ProfileCommands(ROOT).read_summary(self.out)
        self.assertIsNotNone(summary)
        self.assertEqual(summary["kind"], "build")
        self.assertTrue(summary["line"].startswith(f"{summary['steps']} steps"))
        self.assertTrue(str(summary["flame"]).endswith("build.trace.json"))
        self.assertIn("| Output | Seconds | Share |", summary["markdown"])


@unittest.skipUnless(os.access(SCRIPT, os.X_OK), "scripts/relay-profile is not executable")
class UsageTests(unittest.TestCase):
    def test_help_lists_the_three_modes(self):
        result = subprocess.run([str(SCRIPT), "--help"], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0)
        for mode in ("relay-profile build", "relay-profile tests", "relay-profile app"):
            self.assertIn(mode, result.stdout)

    def test_a_mode_it_does_not_have_is_refused(self):
        result = subprocess.run([str(SCRIPT), "everything"], capture_output=True, text=True,
                                timeout=60)
        self.assertEqual(result.returncode, 2)
        self.assertIn("build, tests or app", result.stderr)


if __name__ == "__main__":
    unittest.main()
