"""`cmake/CompilerCache.cmake` (card #V52P): every Relay tree compiles through one shared cache.

Each test configures and builds a throwaway project that includes the module the way
`CMakeLists.txt` does, with a fake `ccache` (and `sccache`) first on PATH. The fake records the
environment it was started with and then runs the real compiler, so a build proves which launcher
ran, with which cache dir, config file and base dir. Nothing touches the real cache.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MODULE = REPO / "cmake" / "CompilerCache.cmake"

CMAKELISTS = """cmake_minimum_required(VERSION 3.16)
project(tinyrelay CXX)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerCache.cmake)
add_executable(tiny main.cpp)
"""

# Records its name and cache environment, then runs the compiler it was handed.
FAKE = """#!/bin/sh
{
  echo "tool=%s"
  echo "CCACHE_DIR=$CCACHE_DIR"
  echo "CCACHE_BASEDIR=$CCACHE_BASEDIR"
  echo "CCACHE_CONFIGPATH=$CCACHE_CONFIGPATH"
  echo "SCCACHE_DIR=$SCCACHE_DIR"
} >> "$RELAY_TEST_LAUNCH_LOG"
exec "$@"
"""

HAVE_CMAKE = bool(shutil.which("cmake")) and bool(
    shutil.which("c++") or shutil.which("g++") or shutil.which("clang++"))


@unittest.skipUnless(HAVE_CMAKE and os.name == "posix", "needs cmake, a C++ compiler and sh")
class CompilerCacheTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="relay-ccache-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.src = self.tmp / "src"
        (self.src / "cmake").mkdir(parents=True)
        shutil.copy2(MODULE, self.src / "cmake" / "CompilerCache.cmake")
        (self.src / "CMakeLists.txt").write_text(CMAKELISTS)
        (self.src / "main.cpp").write_text("int main() { return 0; }\n")
        self.bin = self.tmp / "fakebin"
        self.bin.mkdir()
        self.log = self.tmp / "launch.log"
        self.cache = self.tmp / "xdg" / "relay" / "ccache"

    def fake(self, name):
        path = self.bin / name
        path.write_text(FAKE % name)
        path.chmod(0o755)
        return path

    def build(self, build_dir, *args, env_extra=None):
        env = dict(os.environ, PATH="%s%s%s" % (self.bin, os.pathsep, os.environ["PATH"]),
                   XDG_CACHE_HOME=str(self.tmp / "xdg"), RELAY_TEST_LAUNCH_LOG=str(self.log))
        for key in ("RELAY_CCACHE_DIR", "RELAY_CCACHE_MAX", "CCACHE_DIR", "SCCACHE_DIR"):
            env.pop(key, None)
        env.update(env_extra or {})
        configure = subprocess.run(["cmake", "-S", str(self.src), "-B", str(build_dir), *args],
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True)
        self.assertEqual(configure.returncode, 0, configure.stdout)
        built = subprocess.run(["cmake", "--build", str(build_dir)], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(built.returncode, 0, built.stdout)
        return configure.stdout

    def launches(self):
        if not self.log.exists():
            return []
        runs, current = [], {}
        for line in self.log.read_text().splitlines():
            key, _, value = line.partition("=")
            if key == "tool" and current:
                runs.append(current)
                current = {}
            current[key] = value
        return runs + ([current] if current else [])

    # ------------------------------------------------------------------ tests

    def test_ccache_runs_every_compile_with_the_shared_dir_and_config(self):
        self.fake("ccache")
        out = self.build(self.src / "build")
        self.assertIn("Relay: compiler cache ccache", out)
        runs = self.launches()
        self.assertTrue(runs, "the fake ccache never ran")
        run = runs[0]
        self.assertEqual(run["tool"], "ccache")
        self.assertEqual(Path(run["CCACHE_DIR"]), self.cache)
        self.assertEqual(Path(run["CCACHE_CONFIGPATH"]), self.cache / "ccache.conf")
        # build/ inside the checkout: the base is the checkout itself.
        self.assertEqual(Path(run["CCACHE_BASEDIR"]), self.src)
        conf = (self.cache / "ccache.conf").read_text()
        self.assertIn("max_size = 10G", conf)
        self.assertIn("compression = true", conf)

    def test_a_verify_slot_layout_gets_the_slot_as_its_base(self):
        # land.py builds <slot>/src in <slot>/build: the base must hold both, so the autogen
        # include dir in the build tree is relative too and two slots hash alike.
        self.fake("ccache")
        self.build(self.tmp / "build")
        self.assertEqual(Path(self.launches()[0]["CCACHE_BASEDIR"]), self.tmp)

    def test_the_cap_and_the_dir_can_be_overridden(self):
        self.fake("ccache")
        other = self.tmp / "elsewhere"
        self.build(self.src / "build", env_extra={"RELAY_CCACHE_DIR": str(other),
                                                  "RELAY_CCACHE_MAX": "3G"})
        self.assertEqual(Path(self.launches()[0]["CCACHE_DIR"]), other)
        self.assertIn("max_size = 3G", (other / "ccache.conf").read_text())

    def test_off_means_no_launcher(self):
        self.fake("ccache")
        out = self.build(self.src / "build", "-DRELAY_COMPILER_CACHE=OFF")
        self.assertIn("compiler cache off", out)
        self.assertEqual(self.launches(), [])

    def test_a_launcher_given_on_the_command_line_wins(self):
        self.fake("ccache")
        mine = self.fake("mylauncher")
        self.build(self.src / "build", "-DCMAKE_CXX_COMPILER_LAUNCHER=%s" % mine)
        self.assertEqual({r["tool"] for r in self.launches()}, {"mylauncher"})

    def test_sccache_when_there_is_no_ccache(self):
        self.fake("sccache")
        # A real ccache may be installed on this machine, so name the tool.
        out = self.build(self.src / "build", "-DRELAY_COMPILER_CACHE_TOOL=sccache")
        self.assertIn("Relay: compiler cache sccache", out)
        run = self.launches()[0]
        self.assertEqual(run["tool"], "sccache")
        self.assertEqual(Path(run["SCCACHE_DIR"]), self.cache)


class SourceDirTest(unittest.TestCase):
    """A per-tree path on a common compile line makes every tree miss the cache (#V52P)."""

    def test_only_source_dir_cpp_reads_relay_source_dir(self):
        readers = sorted(str(p.relative_to(REPO)) for p in (REPO / "src").glob("*.[ch]*")
                         if "RELAY_SOURCE_DIR" in p.read_text(errors="replace"))
        self.assertEqual(readers, ["src/SourceDir.cpp", "src/SourceDir.h"])


if __name__ == "__main__":
    unittest.main()
