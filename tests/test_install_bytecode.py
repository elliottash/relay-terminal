# SPDX-License-Identifier: AGPL-3.0-or-later
"""The installed backend carries its own bytecode (#TZWF).

`install(DIRECTORY backend …)` copies the sources without the checkout's `__pycache__`, and the
installed tree is root-owned, so the user's worker cannot write one: CPython silently recompiled
all 65 modules at every worker start — 259 ms to `ready` on spark instead of 71, nine times over
for a three-tab session, and again at every pane restart (docs/qa_evidence/2026-09-20-perf-profile/
worker/FINDINGS.md, finding 2).

The fix is an `install(CODE …)` that byte-compiles the staged tree, so the `.pyc` files are part
of the package. These tests pin the rule and then do what it does to a copy of `backend/`, make
the copy read-only as an installed tree is, and start the worker from it.
"""
import itertools
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKELISTS = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
# The install(CODE …) block that runs compileall, and the arguments it passes. The block's own
# quotes are backslash-escaped, so it ends at the first `")` that is not.
_RULE = re.search(r'install\(CODE "(.*?)(?<!\\)"\)', CMAKELISTS, re.S)
_COMPILEALL = re.search(r"-m compileall(.*?)RESULT_VARIABLE", _RULE.group(1), re.S) if _RULE else None


def compileall_flags() -> list[str]:
    """The flags the install rule gives compileall, up to its first path argument (`-s`)."""
    return list(itertools.takewhile(lambda word: word not in ("-s", "-p") and not word.startswith('\\"'),
                                    _COMPILEALL.group(1).split()))


def read_only(path: Path) -> None:
    for directory, _, names in os.walk(path, topdown=False):
        for name in names:
            os.chmod(Path(directory) / name, 0o444)
        os.chmod(directory, 0o555)


def writable(path: Path) -> None:
    for directory, _, names in os.walk(path, topdown=False):
        os.chmod(directory, 0o755)
        for name in names:
            os.chmod(Path(directory) / name, 0o644)


class InstallRuleTests(unittest.TestCase):
    def test_the_install_rule_byte_compiles_what_it_just_installed(self):
        self.assertIsNotNone(_RULE, "no install(CODE …) block in CMakeLists.txt")
        code = _RULE.group(1)
        self.assertIn("-m compileall", code)
        # Hash-based and not re-checked: a .pyc validated against an mtime is only as good as
        # whatever the packager did to that mtime, and the loader must not pay to re-hash.
        self.assertIn("--invalidation-mode", code)
        self.assertIn("unchecked-hash", code)
        # DESTDIR (the Arch PKGBUILDs) and CPack's temporary prefix both have to be honoured, and
        # the path baked into each .pyc rewritten to where the files will really be.
        self.assertIn("$ENV{DESTDIR}", code)
        self.assertIn("${CMAKE_INSTALL_PREFIX}", code)
        self.assertRegex(code, r"-s\s+\S*_relay_staged")
        self.assertRegex(code, r"-p\s+\S*_relay_final")
        self.assertIn("/backend", code)
        self.assertIn("FATAL_ERROR", code)      # a failed compile must fail the install
        # The developer's own __pycache__ still must not be copied: its .pyc name the checkout.
        self.assertRegex(CMAKELISTS, r"install\(DIRECTORY backend[^)]*PATTERN \"__pycache__\" EXCLUDE")


class ReadOnlyWorkerTests(unittest.TestCase):
    """What an installed, root-owned backend behaves like: read-only, and started from .pyc."""

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.mkdtemp(prefix="relay-pyc-")
        cls.tree = Path(cls.temp) / "usr" / "share" / "relay"
        cls.tree.mkdir(parents=True)
        shutil.copytree(ROOT / "backend", cls.tree / "backend",
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        cls.final = "/usr/share/relay"
        compiled = subprocess.run([sys.executable, "-m", "compileall", *compileall_flags(),
                                   "-s", str(cls.tree), "-p", cls.final,
                                   str(cls.tree / "backend")],
                                  capture_output=True, text=True)
        assert compiled.returncode == 0, compiled.stderr
        read_only(Path(cls.temp))

    @classmethod
    def tearDownClass(cls):
        writable(Path(cls.temp))
        shutil.rmtree(cls.temp, ignore_errors=True)

    def pycs(self) -> set[Path]:
        return set((self.tree / "backend").rglob("*.pyc"))

    def test_every_module_is_compiled_hash_based_and_names_its_installed_path(self):
        import importlib.util
        import marshal
        sources = list((self.tree / "backend").rglob("*.py"))
        self.assertGreater(len(sources), 50)
        self.assertEqual(len(self.pycs()), len(sources))
        for source in sources:
            cache = Path(importlib.util.cache_from_source(str(source)))
            self.assertTrue(cache.exists(), f"{source} was not compiled")
        raw = Path(importlib.util.cache_from_source(
            str(self.tree / "backend" / "relay_core" / "provider.py"))).read_bytes()
        self.assertEqual(raw[:4], importlib.util.MAGIC_NUMBER)
        # bit 0: hash-based; bit 1: check the source at import. Unchecked-hash is 1, timestamp 0.
        self.assertEqual(int.from_bytes(raw[4:8], "little"), 1)
        code = marshal.loads(raw[16:])
        self.assertEqual(code.co_filename, self.final + "/backend/relay_core/provider.py")

    def test_the_worker_starts_from_the_read_only_tree_and_writes_no_bytecode(self):
        home = Path(self.temp).parent / (Path(self.temp).name + "-home")
        for name in ("data", "config", "state", "cache"):
            (home / name).mkdir(parents=True, exist_ok=True)
        self.addCleanup(shutil.rmtree, home, True)
        env = dict(os.environ, RELAY_KEYRING="off",
                   PYTHONPATH=str(self.tree / "backend"),
                   XDG_DATA_HOME=str(home / "data"), XDG_CONFIG_HOME=str(home / "config"),
                   XDG_STATE_HOME=str(home / "state"), XDG_CACHE_HOME=str(home / "cache"))
        env.pop("PYTHONDONTWRITEBYTECODE", None)
        before = self.pycs()
        worker = subprocess.run([sys.executable, "-S", "-u", str(self.tree / "backend" / "worker.py")],
                                input='{"type":"shutdown"}\n', capture_output=True, text=True,
                                env=env, cwd=str(home), timeout=120)
        events = [json.loads(line) for line in worker.stdout.splitlines() if line.startswith("{")]
        self.assertIn("ready", [e.get("event") for e in events], worker.stderr[-2000:])
        # Nothing was compiled at start: the cache it ran from is the one the install wrote.
        self.assertEqual(self.pycs(), before)
        self.assertFalse(list(Path(self.temp).rglob("*.pyc.*")))   # no half-written cache either
        self.assertEqual(stat.S_IMODE((self.tree / "backend" / "worker.py").stat().st_mode), 0o444)


if __name__ == "__main__":
    unittest.main()
