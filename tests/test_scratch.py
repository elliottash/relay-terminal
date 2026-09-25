"""relay_core.scratch (card #SZHQ): report, gc and check over agent scratch roots.

Every test builds its own scratch root and points RELAY_SCRATCH_ROOTS at it, so nothing under
the real temp directory is read or removed.
"""

import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import scratch  # noqa: E402


def make(path: Path, size: int = 4096, hours_old: float = 0.0) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"x" * size)
    if hours_old:
        when = time.time() - hours_old * 3600
        os.utime(path, (when, when))
        os.utime(path.parent, (when, when))
    return path


class ScratchCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "claude-test"
        self.root.mkdir()
        self.env = {k: os.environ.get(k) for k in ("RELAY_SCRATCH_ROOTS",
                                                   "RELAY_SCRATCH_BUDGET_GB",
                                                   "RELAY_SCRATCH_MIN_FREE_GB",
                                                   "RELAY_CCACHE_DIR")}
        os.environ["RELAY_SCRATCH_ROOTS"] = str(self.root)
        os.environ.pop("RELAY_CCACHE_DIR", None)
        os.environ.pop("RELAY_SCRATCH_BUDGET_GB", None)
        os.environ.pop("RELAY_SCRATCH_MIN_FREE_GB", None)
        self.cwd = os.getcwd()

    def tearDown(self):
        os.chdir(self.cwd)
        for key, value in self.env.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        self.temp.cleanup()

    def entry(self, entries, name):
        found = [e for e in entries if Path(e.path).name == name]
        self.assertEqual(len(found), 1, [e.path for e in entries])
        return found[0]


class Report(ScratchCase):
    def test_claude_project_folders_are_split_into_sessions(self):
        make(self.root / "-home-me-repo" / "aaaa" / "out.txt", hours_old=48)
        make(self.root / "-home-me-repo" / "bbbb" / "out.txt")
        entries = scratch.report()
        self.assertEqual(self.entry(entries, "aaaa").kind, "claude-session")
        self.assertTrue(self.entry(entries, "aaaa").removable)
        self.assertFalse(self.entry(entries, "bbbb").removable)
        self.assertIn("written", self.entry(entries, "bbbb").why_kept)

    def test_the_newest_file_decides_idleness_not_the_folder(self):
        make(self.root / "export" / "old.cpp", hours_old=100)
        make(self.root / "export" / "deep" / "new.o")         # one fresh object deep inside
        self.assertFalse(self.entry(scratch.report(), "export").removable)

    def test_land_root_is_managed_by_land_py(self):
        make(self.root / "land" / "s" / "meta.json", hours_old=500)
        e = self.entry(scratch.report(), "land")
        self.assertEqual(e.kind, "managed")
        self.assertFalse(e.removable)

    def test_an_entry_a_live_process_sits_in_is_kept(self):
        make(self.root / "busy" / "f", hours_old=100)
        proc = subprocess.Popen(["sleep", "30"], cwd=str(self.root / "busy"))
        try:
            e = self.entry(scratch.report(), "busy")
            self.assertTrue(e.in_use)
            self.assertFalse(e.removable)
        finally:
            proc.kill()
            proc.wait()

    def test_a_symlink_is_never_followed(self):
        target = Path(self.temp.name) / "precious"
        make(target / "keep.txt", hours_old=100)
        (self.root / "link").symlink_to(target)
        scratch.gc(apply=True, log=lambda *_: None)
        self.assertTrue((target / "keep.txt").exists())


class CompilerCache(ScratchCase):
    """The shared ccache dir (#V52P): listed and counted, never removed."""

    def cache(self, hours_old=500):
        cache = Path(self.temp.name) / "xdg" / "relay" / "ccache"
        make(cache / "a" / "b" / "obj.o", size=64 * 1024, hours_old=hours_old)
        os.environ["RELAY_CCACHE_DIR"] = str(cache)
        return cache

    def test_it_is_its_own_kind_and_never_removable(self):
        cache = self.cache()
        e = self.entry(scratch.report(), "ccache")
        self.assertEqual(e.kind, "compiler-cache")
        self.assertFalse(e.removable)
        self.assertIn("max_size", e.why_kept)
        scratch.gc(idle_hours=0, apply=True, log=lambda *_: None)
        self.assertTrue((cache / "a" / "b" / "obj.o").exists())

    def test_it_counts_against_the_budget(self):
        self.cache()
        os.environ["RELAY_SCRATCH_BUDGET_GB"] = str(32 / (1024 * 1024))    # 32 KB
        os.environ["RELAY_SCRATCH_MIN_FREE_GB"] = "0"
        verdict = scratch.check()
        self.assertFalse(verdict.ok)
        self.assertGreaterEqual(verdict.scratch_bytes, 64 * 1024)
        self.assertEqual(verdict.reclaimable_bytes, 0)

    def test_the_default_dir_follows_xdg_cache_home(self):
        old = os.environ.get("XDG_CACHE_HOME")
        os.environ["XDG_CACHE_HOME"] = "/x/cache"
        try:
            self.assertEqual(scratch.compiler_cache_dir(), Path("/x/cache/relay/ccache"))
        finally:
            if old is None:
                os.environ.pop("XDG_CACHE_HOME", None)
            else:
                os.environ["XDG_CACHE_HOME"] = old

    def test_a_narrowed_report_leaves_the_real_cache_out(self):
        self.assertNotIn("compiler-cache", [e.kind for e in scratch.report()])


class Gc(ScratchCase):
    def test_dry_run_removes_nothing(self):
        make(self.root / "old" / "f", hours_old=100)
        count, freed = scratch.gc(log=lambda *_: None)
        self.assertEqual(count, 1)
        self.assertGreater(freed, 0)
        self.assertTrue((self.root / "old").exists())

    def test_apply_removes_only_the_idle_unused_entries(self):
        make(self.root / "old" / "f", hours_old=100)
        make(self.root / "fresh" / "f")
        make(self.root / "land" / "s" / "meta.json", hours_old=500)
        make(self.root / "-home-me-repo" / "gone" / "f", hours_old=100)
        scratch.gc(apply=True, log=lambda *_: None)
        self.assertFalse((self.root / "old").exists())
        self.assertFalse((self.root / "-home-me-repo" / "gone").exists())
        self.assertTrue((self.root / "fresh").exists())
        self.assertTrue((self.root / "land").exists())
        self.assertTrue((self.root / "-home-me-repo").exists())

    def test_idle_hours_is_the_threshold(self):
        make(self.root / "six" / "f", hours_old=6)
        scratch.gc(idle_hours=12, apply=True, log=lambda *_: None)
        self.assertTrue((self.root / "six").exists())
        scratch.gc(idle_hours=2, apply=True, log=lambda *_: None)
        self.assertFalse((self.root / "six").exists())


class Check(ScratchCase):
    def test_under_budget_is_ok(self):
        make(self.root / "small" / "f", size=1024)
        os.environ["RELAY_SCRATCH_BUDGET_GB"] = "1"
        os.environ["RELAY_SCRATCH_MIN_FREE_GB"] = "0"
        verdict = scratch.check()
        self.assertTrue(verdict.ok, verdict.line)

    def test_over_budget_names_the_size_and_the_reclaimable_part(self):
        make(self.root / "big" / "f", size=2 * 1024 * 1024, hours_old=100)
        os.environ["RELAY_SCRATCH_BUDGET_GB"] = "0.001"      # about 1 MB
        os.environ["RELAY_SCRATCH_MIN_FREE_GB"] = "0"
        verdict = scratch.check()
        self.assertFalse(verdict.ok)
        self.assertIn("budget", verdict.line)
        self.assertIn("gc --apply", verdict.line)
        self.assertGreater(verdict.reclaimable_bytes, 0)

    def test_low_free_space_fails_even_under_budget(self):
        os.environ["RELAY_SCRATCH_BUDGET_GB"] = "100"
        os.environ["RELAY_SCRATCH_MIN_FREE_GB"] = str(10 ** 9)   # more than any disk
        verdict = scratch.check()
        self.assertFalse(verdict.ok)
        self.assertIn("free", verdict.line)

    def test_the_cli_check_exits_one_when_over_budget(self):
        make(self.root / "big" / "f", size=2 * 1024 * 1024)
        env = dict(os.environ, RELAY_SCRATCH_BUDGET_GB="0.001", RELAY_SCRATCH_MIN_FREE_GB="0")
        proc = subprocess.run([sys.executable, str(ROOT / "scripts" / "relay-scratch"), "check"],
                              env=env, stdout=subprocess.PIPE, text=True)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("budget", proc.stdout)


if __name__ == "__main__":
    unittest.main()
