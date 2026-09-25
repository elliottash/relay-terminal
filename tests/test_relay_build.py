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

    @unittest.skipUnless(shutil.which("ninja"), "fast builds need Ninja")
    def test_fast_build_is_separate_and_uses_developer_flags(self):
        root = self.make_project()
        self.first_build(root, slow=0)
        normal_binary = self.binary(root)
        normal_mtime = normal_binary.stat().st_mtime_ns

        fast = self.build(root, "--fast", "--check", "MARKER-ONE", slow=0)
        self.assertEqual(fast.returncode, 0, fast.stdout)
        self.assertTrue((root / "build-fast" / "relay").is_file())
        self.assertEqual(normal_binary.stat().st_mtime_ns, normal_mtime)
        cache = (root / "build-fast" / "CMakeCache.txt").read_text()
        self.assertIn("CMAKE_GENERATOR:INTERNAL=Ninja", cache)
        self.assertIn("CMAKE_CXX_FLAGS_RELWITHDEBINFO:STRING=-O0 -g1 -DNDEBUG", cache)

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
        self.assertIn("binary predates the change", missing.stdout)

    def test_check_only_names_a_stale_binary_with_its_build_id_and_head(self):
        """Card #J6MF: a hand-off must not run a binary that predates the change.

        The binary is built with MARKER-ONE; the "landed change" added MARKER-ABSENT, which
        this binary does not hold. --check-only builds nothing and says so by name.
        """
        root = self.make_project()
        self.first_build(root, slow=0)
        (root / "build" / "relay.build-id").write_text("0924.07.3\n")
        git = ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"]
        subprocess.run([*git, "init", "-q"], cwd=str(root), check=True)
        subprocess.run([*git, "add", "main.cpp"], cwd=str(root), check=True)
        subprocess.run([*git, "commit", "-qm", "change"], cwd=str(root), check=True)
        sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=str(root),
                             capture_output=True, text=True, check=True).stdout.strip()
        # Make the source newer than the binary: a build here would relink, --check-only must not.
        binary_mtime = self.binary(root).stat().st_mtime_ns
        self.write_marker(root, "MARKER-ABSENT")

        stale = self.build(root, "--check-only", "--check", "MARKER-ABSENT", slow=0)
        self.assertEqual(stale.returncode, 5, stale.stdout)
        self.assertIn("binary predates the change", stale.stdout)
        self.assertIn("build id 0924.07.3", stale.stdout)
        self.assertIn("HEAD " + sha, stale.stdout)
        self.assertNotIn("building:", stale.stdout)
        self.assertEqual(self.binary(root).stat().st_mtime_ns, binary_mtime)

        fresh = self.build(root, "--check-only", "--check", "MARKER-ONE", slow=0)
        self.assertEqual(fresh.returncode, 0, fresh.stdout)
        self.assertNotIn("building:", fresh.stdout)

        bare = self.build(root, "--check-only", slow=0)
        self.assertEqual(bare.returncode, 1, bare.stdout)

    def test_check_finds_a_qstringliteral_which_is_utf16_in_the_binary(self):
        # No cmake needed: load the wrapper as a module and ask its search directly.
        from importlib.machinery import SourceFileLoader
        wrapper = SourceFileLoader("relay_build_under_test", str(SCRIPT)).load_module()
        with tempfile.TemporaryDirectory() as tmp:
            blob = Path(tmp) / "relay"
            pad = b"\x7fELF" + b"\0" * ((1 << 20) - 9)      # puts the text across a chunk boundary
            blob.write_bytes(pad + "open the task list".encode("utf-16-le") + b"\0\0narrow text\0")
            self.assertTrue(wrapper.binary_contains(blob, "open the task list"))   # UTF-16LE
            self.assertTrue(wrapper.binary_contains(blob, "narrow text"))          # UTF-8
            self.assertFalse(wrapper.binary_contains(blob, "not in there"))

    def test_a_broken_configure_stops_the_build(self):
        root = self.make_project()
        (root / "CMakeLists.txt").write_text(CMAKELISTS + "\nmessage(FATAL_ERROR \"no\")\n")
        done = self.build(root, slow=0)
        self.assertEqual(done.returncode, 3, done.stdout)
        self.assertIn("cmake configure failed", done.stdout)
        self.assertFalse(self.binary(root).exists())

class JobCapTest(unittest.TestCase):
    """relay-build picks --parallel from the memory limit it actually runs under.

    The incident (2026-09-23, card #04EC): an agent pane built with the default 8
    parallel jobs inside its systemd scope (MemoryMax=8G). Eight cc1plus of this
    repo peak past 8 GiB, systemd killed the scope, and the pane's agent "ran out
    of memory" while doing nothing but compiling. The job count is therefore
    clamped to about one 2 GiB compile job per 2 GiB of limit.
    """

    def load(self):
        import importlib.util
        from importlib.machinery import SourceFileLoader
        loader = SourceFileLoader("relay_build_jobs_under_test", str(SCRIPT))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def limit_under(self, tmp, cgroup_line="0::/user.slice/relay-pane.scope",
                    meminfo_mb=16000):
        """memory_limit_bytes against a fake cgroup tree laid out in tmp."""
        wrapper = self.load()
        (Path(tmp) / "self.cgroup").write_text(cgroup_line + "\n")
        (Path(tmp) / "meminfo").write_text("MemTotal:       %d kB\n" % (meminfo_mb * 1024))
        return wrapper, wrapper.memory_limit_bytes(
            cgroup_root=str(Path(tmp) / "cgroup"),
            self_cgroup=str(Path(tmp) / "self.cgroup"),
            meminfo=str(Path(tmp) / "meminfo"))

    def write_cgroup(self, tmp, rel, memory_max):
        """A fake cgroup v2 tree; memory_max=None leaves the level unlimited."""
        node = Path(tmp) / "cgroup" / rel
        node.mkdir(parents=True, exist_ok=True)
        if memory_max is not None:
            (node / "memory.max").write_text("%d\n" % memory_max)
        return node

    def test_the_pane_scopes_memory_max_is_the_limit(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write_cgroup(tmp, "user.slice/relay-pane.scope", 8 * 1024 ** 3)
            wrapper, limit = self.limit_under(tmp)
            self.assertEqual(8 * 1024 ** 3, limit)

    def test_a_slice_ancestor_can_hold_the_limit(self):
        # The scope itself is unlimited ("max" in v2); user.slice's 6 GiB still counts.
        with tempfile.TemporaryDirectory() as tmp:
            self.write_cgroup(tmp, "user.slice", 6 * 1024 ** 3)
            self.write_cgroup(tmp, "user.slice/relay-pane.scope", None)
            wrapper, limit = self.limit_under(tmp)
            self.assertEqual(6 * 1024 ** 3, limit)

    def test_without_a_cgroup_limit_the_hosts_ram_is_the_limit(self):
        # cgroup v1's sentinel for "unlimited", and no v2 at all.
        with tempfile.TemporaryDirectory() as tmp:
            cgroup = Path(tmp) / "cgroup"
            (cgroup / "memory").mkdir(parents=True)
            (cgroup / "memory" / "memory.limit_in_bytes").write_text("9223372036854771712\n")
            wrapper, limit = self.limit_under(tmp, cgroup_line="12:memory:/",
                                              meminfo_mb=4000)
            self.assertEqual(4000 * 1024 * 1024, limit)

    def test_jobs_are_clamped_to_the_memory_limit(self):
        wrapper = self.load()
        wrapper.memory_limit_bytes = lambda **_: 8 * 1024 ** 3      # an 8 GiB pane
        notes = []
        self.assertEqual("4", wrapper.jobs_for_environment({}, note=notes.append))
        self.assertEqual("4", wrapper.jobs_for_environment({"RELAY_JOBS": "8"},
                                                           note=notes.append))
        self.assertIn("lowered to 4", notes[-1])
        self.assertIn("8.0 GiB", notes[-1])
        # A lower explicit value wins untouched...
        self.assertEqual("2", wrapper.jobs_for_environment({"RELAY_JOBS": "2"}))
        # ...and the count never goes below one job, even on a tiny limit.
        wrapper.memory_limit_bytes = lambda **_: 1024 ** 3
        self.assertEqual("1", wrapper.jobs_for_environment({}))

    def test_without_any_limit_the_requested_count_stands(self):
        wrapper = self.load()
        wrapper.memory_limit_bytes = lambda **_: None
        self.assertEqual("8", wrapper.jobs_for_environment({}))
        self.assertEqual("16", wrapper.jobs_for_environment({"RELAY_JOBS": "16"}))
        self.assertEqual("3", wrapper.jobs_for_environment({"RELAY_JOBS": "3"}))

    def test_land_verify_build_applies_the_same_cap(self):
        import importlib.util
        from importlib.machinery import SourceFileLoader
        loader = SourceFileLoader("land_under_test", str(REPO / "scripts" / "land.py"))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        land = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(land)
        notes = []
        # limit_bytes pins the test: same cap math, whatever this host really has.
        self.assertEqual("4", land.relay_build_jobs(notes.append, limit_bytes=8 * 1024 ** 3))
        self.assertTrue(any("lowered to 4" in note for note in notes), notes)

    def command(self, root, *args):
        return [sys.executable, str(root / "scripts" / "relay-build"), *args]

    def test_the_building_line_carries_the_capped_count(self):
        root_root = Path(tempfile.mkdtemp(prefix="relay-build-cap-"))
        self.addCleanup(shutil.rmtree, root_root, ignore_errors=True)
        (root_root / "scripts").mkdir()
        shutil.copy2(SCRIPT, root_root / "scripts" / "relay-build")
        (root_root / "CMakeLists.txt").write_text(CMAKELISTS)
        (root_root / "main.cpp").write_text(MAIN_CPP)
        (root_root / "marker.h").write_text('#define RELAY_MARKER "MARKER-ONE"\n')
        env = dict(os.environ)
        env["RELAY_JOBS"] = "99"          # far past any limit, to force the clamp
        env["RELAY_TEST_SLOW"] = "0"
        done = subprocess.run(self.command(root_root), cwd=str(root_root), env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stdout)
        wrapper = self.load()
        expected = wrapper.jobs_for_environment({"RELAY_JOBS": "99"})
        self.assertIn("--parallel %s" % expected, done.stdout)
        if expected != "99":              # this host actually has a limit to clamp to
            self.assertNotIn("--parallel 99", done.stdout)
            self.assertIn("lowered to", done.stdout)


if __name__ == "__main__":
    unittest.main()
