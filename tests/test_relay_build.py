"""`scripts/relay-build`: one build at a time, and no object left newer than the
header it was compiled from.

The incident these tests stand for (2026-09-19): one session's compile of the
`src/main.cpp` translation unit was running -- it takes minutes -- while another
session edited a header. The object file was written 35 seconds *after* that
edit but from the text as it was before it, so the next `cmake --build` saw an
up-to-date object, rebuilt nothing, reported success, and `./build/relay` ran the
old code.

Everything here runs against a throwaway CMake project with a compile made slow
on purpose, never against the shared `build/` directory. The slow compile is a
compiler launcher that runs the real compiler first (so the header is read now)
and touches the object again N seconds later (so the object lands after an edit
made in between) -- which is exactly the shape of the incident.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRIPT = REPO / "scripts" / "relay-build"

CMAKELISTS = """cmake_minimum_required(VERSION 3.16)
project(tinyrelay CXX)
add_executable(relay main.cpp)
target_include_directories(relay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
"""

MAIN_CPP = """#include "marker.h"
#include <cstdio>
const char *relay_marker = RELAY_MARKER;
int main() { std::puts(relay_marker); return 0; }
"""

SLOW_COMPILE = """#!/bin/sh
# A long compile: the compiler reads the header now, and the object file only
# finishes (mtime) RELAY_TEST_SLOW seconds later.
"$@" || exit $?
sleep "${RELAY_TEST_SLOW:-2}"
out=""
prev=""
for a in "$@"; do
  if [ "$prev" = "-o" ]; then out="$a"; fi
  prev="$a"
done
if [ -n "$out" ] && [ -e "$out" ]; then touch "$out"; fi
exit 0
"""

HAVE_CMAKE = bool(shutil.which("cmake")) and bool(
    shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
)


@unittest.skipUnless(HAVE_CMAKE, "needs cmake and a C++ compiler")
class RelayBuildTest(unittest.TestCase):
    # ------------------------------------------------------------------ helpers

    def make_project(self, marker="MARKER-ONE"):
        """A tiny CMake project with its own copy of the wrapper in scripts/.

        The wrapper finds the repository from its own path, so a copy in
        <tmp>/scripts builds <tmp> -- which is also how it works from any cwd.
        """
        root = Path(tempfile.mkdtemp(prefix="relay-build-test-"))
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        (root / "scripts").mkdir()
        shutil.copy2(SCRIPT, root / "scripts" / "relay-build")
        (root / "CMakeLists.txt").write_text(CMAKELISTS)
        (root / "main.cpp").write_text(MAIN_CPP)
        self.write_marker(root, marker)
        launcher = root / "slow-compile.sh"
        launcher.write_text(SLOW_COMPILE)
        launcher.chmod(0o755)
        return root

    def write_marker(self, root, marker):
        (root / "marker.h").write_text('#define RELAY_MARKER "%s"\n' % marker)

    def launcher_arg(self, root):
        # `--cmake-arg=` with the equals sign: the value starts with a dash.
        return "--cmake-arg=-DCMAKE_CXX_COMPILER_LAUNCHER=%s" % (root / "slow-compile.sh")

    def command(self, root, *args):
        return [sys.executable, str(root / "scripts" / "relay-build"), *args]

    def environ(self, slow):
        env = dict(os.environ)
        env["RELAY_JOBS"] = "2"
        env["RELAY_TEST_SLOW"] = str(slow)
        return env

    def build(self, root, *args, slow=0, cwd=None, timeout=300):
        done = subprocess.run(
            self.command(root, *args), cwd=str(cwd or root), env=self.environ(slow),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout,
        )
        return done

    def plain_cmake_build(self, root, slow=0, timeout=300):
        return subprocess.run(
            ["cmake", "--build", str(root / "build"), "--parallel", "2"],
            cwd=str(root), env=self.environ(slow),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout,
        )

    def first_build(self, root, slow=0):
        """Configure with the slow launcher and build once."""
        done = self.build(root, self.launcher_arg(root), slow=slow)
        self.assertEqual(done.returncode, 0, done.stdout)
        return done

    def binary(self, root):
        return root / "build" / "relay"

    def binary_has(self, root, marker):
        return marker.encode() in self.binary(root).read_bytes()

    def object_file(self, root):
        objects = sorted((root / "build").rglob("main.cpp.o"))
        self.assertTrue(objects, "no main.cpp.o under build/")
        return objects[0]

    def edit_mid_compile(self, root, builder_args, slow=4, pause=1.5):
        """The incident, reproduced.

        Build with the header saying TWO; while the compiler is running, edit it
        to THREE. The object lands after that edit but without it.
        """
        self.write_marker(root, "MARKER-TWO")
        if builder_args is None:
            command = ["cmake", "--build", str(root / "build"), "--parallel", "2"]
        else:
            command = self.command(root, *builder_args)
        started = time.time()
        proc = subprocess.Popen(
            command, cwd=str(root), env=self.environ(slow),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        time.sleep(pause)
        self.write_marker(root, "MARKER-THREE")
        edited = time.time()
        out = proc.communicate(timeout=300)[0]
        self.assertEqual(proc.returncode, 0, out)
        self.assertGreater(time.time() - started, slow, "the compile was not slow")
        # What the running compile produced: TWO, not the THREE now on disk.
        self.assertTrue(self.binary_has(root, "MARKER-TWO"), out)
        self.assertFalse(self.binary_has(root, "MARKER-THREE"), out)
        return edited, out

    # -------------------------------------------------------------------- tests

    def test_artifacts_are_stamped_back_to_the_start_of_the_build(self):
        root = self.make_project()
        started = time.time()
        done = self.first_build(root, slow=3)
        finished = time.time()
        self.assertGreater(finished - started, 3, done.stdout)

        obj = self.object_file(root).stat()
        binary = self.binary(root).stat()
        # Every artifact gets the same time, so make never sees the binary as
        # older than the objects it was linked from.
        self.assertEqual(obj.st_mtime_ns, binary.st_mtime_ns, done.stdout)
        # And that time is the start of the build, not its end.
        self.assertGreater(obj.st_mtime, started - 1, done.stdout)
        self.assertLess(obj.st_mtime, finished - 2, done.stdout)
        self.assertIn("stamped back to", done.stdout)

    def test_plain_cmake_misses_a_header_edited_during_the_compile(self):
        """The hazard itself: without the wrapper, the stale object wins."""
        root = self.make_project()
        self.first_build(root, slow=0)
        self.assertTrue(self.binary_has(root, "MARKER-ONE"))

        edited, _ = self.edit_mid_compile(root, builder_args=None)
        # The object is newer than the edit it does not contain.
        self.assertGreater(self.object_file(root).stat().st_mtime, edited)

        again = self.plain_cmake_build(root)
        self.assertEqual(again.returncode, 0, again.stdout)
        self.assertFalse(
            self.binary_has(root, "MARKER-THREE"),
            "plain cmake was expected to skip the rebuild: %s" % again.stdout,
        )
        self.assertTrue(self.binary_has(root, "MARKER-TWO"))

    def test_the_wrapper_rebuilds_a_header_edited_during_the_compile(self):
        root = self.make_project()
        self.first_build(root, slow=0)

        edited, out = self.edit_mid_compile(root, builder_args=[])
        # Restamped: the object is now older than the edit, so make must redo it.
        self.assertLess(self.object_file(root).stat().st_mtime, edited, out)

        again = self.build(root, "--check", "MARKER-THREE", slow=0)
        self.assertEqual(again.returncode, 0, again.stdout)
        self.assertTrue(self.binary_has(root, "MARKER-THREE"), again.stdout)
        self.assertFalse(self.binary_has(root, "MARKER-TWO"), again.stdout)

    def test_a_second_build_waits_for_the_first_and_says_whose(self):
        root = self.make_project()
        self.first_build(root, slow=0)

        self.write_marker(root, "MARKER-SLOW")
        env = self.environ(slow=8)
        env["RELAY_SESSION"] = "slowpoke"
        first = subprocess.Popen(
            self.command(root), cwd=str(root), env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        self.addCleanup(first.kill)
        lock = root / "build" / ".relay-build.lock"
        deadline = time.time() + 30
        while time.time() < deadline and not (lock.exists() and lock.read_text().strip()):
            time.sleep(0.05)
        holder = lock.read_text()
        self.assertIn("session slowpoke", holder)
        self.assertEqual(int(re.search(r"pid (\d+)", holder).group(1)), first.pid)

        second = self.build(root, "--wait-seconds", "1", slow=0)
        self.assertEqual(second.returncode, 2, second.stdout)
        self.assertIn("waiting for", second.stdout)
        self.assertIn("pid %d" % first.pid, second.stdout)
        self.assertIn("session slowpoke", second.stdout)
        self.assertIn("nothing was built", second.stdout)

        first_out = first.communicate(timeout=300)[0]
        self.assertEqual(first.returncode, 0, first_out)
        self.assertNotIn("waiting for", first_out)

        # The lock is free again, and the build dir stays configured.
        third = self.build(root, "--wait-seconds", "60", slow=0)
        self.assertEqual(third.returncode, 0, third.stdout)
        self.assertNotIn("configuring", third.stdout)

    def test_runs_from_any_cwd_and_checks_the_binary(self):
        root = self.make_project()
        self.first_build(root, slow=0)
        elsewhere = root / "src" / "deep"
        elsewhere.mkdir(parents=True)

        found = self.build(root, "--check", "MARKER-ONE", slow=0, cwd=elsewhere)
        self.assertEqual(found.returncode, 0, found.stdout)
        self.assertIn("contains 'MARKER-ONE'", found.stdout)

        missing = self.build(root, "--check", "MARKER-NOWHERE", slow=0, cwd=elsewhere)
        self.assertEqual(missing.returncode, 5, missing.stdout)
        self.assertIn("did not pick up your change", missing.stdout)

    def test_a_broken_configure_stops_the_build(self):
        root = self.make_project()
        (root / "CMakeLists.txt").write_text(CMAKELISTS + "\nmessage(FATAL_ERROR \"no\")\n")
        done = self.build(root, slow=0)
        self.assertEqual(done.returncode, 3, done.stdout)
        self.assertIn("cmake configure failed", done.stdout)
        self.assertFalse(self.binary(root).exists())


if __name__ == "__main__":
    unittest.main()
