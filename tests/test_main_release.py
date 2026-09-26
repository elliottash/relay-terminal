# SPDX-License-Identifier: AGPL-3.0-or-later
"""MainRelease: atomic tip-channel releases from a temp git repo.

The fake project installs a tiny script "binary" plus assets, so releases exercise the real
machinery — persistent source worktree, placeholder substitution, completeness and smoke gates,
atomic `current` flip, keep>=2 retention, rollback reuse, crash leftovers — without compiling
anything.  Isolated state/cache roots; the actual repo's config and mode are never touched.
"""
import os
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import main_release as MR  # noqa: E402
from relay_core import projectconf as PC  # noqa: E402


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


class RepoFixture:
    def __init__(self, root: Path):
        self.repo = root / "repo"
        self.repo.mkdir()
        subprocess.run(["git", "init", "-q", "-b", "main"], cwd=self.repo, check=True)
        subprocess.run(["git", "config", "user.email", "t@example.com"], cwd=self.repo, check=True)
        subprocess.run(["git", "config", "user.name", "T"], cwd=self.repo, check=True)
        self.shas: list[str] = []

    def commit(self, marker: str, extra=None) -> str:
        write(self.repo / "app.py",
              "#!/usr/bin/env python3\nimport sys\n"
              f"print('app {marker} ' + (sys.argv[1] if len(sys.argv) > 1 else 'run'))\n")
        write(self.repo / "assets" / "data.txt", f"assets {marker}\n")
        if extra:
            extra(self.repo)
        subprocess.run(["git", "add", "-A"], cwd=self.repo, check=True)
        subprocess.run(["git", "commit", "-qm", marker], cwd=self.repo, check=True)
        sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=self.repo, check=True,
                             stdout=subprocess.PIPE).stdout.decode().strip()
        self.shas.append(sha)
        return sha


def make_config(**main_overrides) -> dict:
    install = main_overrides.pop("install", [[
        "sh", "-c",
        "mkdir -p {dest}/bin && cp {source}/app.py {dest}/bin/app "
        "&& chmod +x {dest}/bin/app && cp -r {source}/assets {dest}/assets"]])
    smoke = main_overrides.pop("smoke", [["{executable}", "--version"]])
    raw = {
        "main": {"build": [["sh", "-c", "echo built > {build}/marker"]],
                 "install": install,
                 "executable": "bin/app",
                 "smoke": smoke,
                 "keep": 2},
    }
    raw["main"].update(main_overrides)
    return PC.normalize_config(raw)


class MainReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.fx = RepoFixture(root)
        self.state = root / "state"
        self.cache = root / "cache"

    def tearDown(self):
        self.tmp.cleanup()

    def release(self) -> MR.MainRelease:
        return MR.MainRelease(self.fx.repo, state_root=self.state, cache_root=self.cache,
                              repo_id="test-repo")

    def test_repo_id_guard_is_loud_without_registry(self):
        with self.assertRaises(MR.MainReleaseError) as ctx:
            MR.MainRelease(self.fx.repo, state_root=self.state, cache_root=self.cache)
        self.assertIn("repo_id", str(ctx.exception))
        loud = MR.MainRelease(self.fx.repo, state_root=self.state, cache_root=self.cache,
                              allow_derived_repo_id=True)
        self.assertTrue(loud.repo_id.startswith("derived-"))

    def test_update_installs_and_flips_atomically(self):
        sha = self.fx.commit("one")
        rec = self.release().update(sha, make_config())
        self.assertTrue(rec["changed"])
        self.assertEqual(rec["sha"], sha)
        self.assertGreater(rec["bytes"], 0)
        self.assertGreaterEqual(rec["duration_seconds"], 0)
        run = self.state / "integration" / "test-repo" / "tip" / "run"
        self.assertEqual(os.readlink(run / "current"), sha)
        exe = run / "current" / "bin" / "app"
        out = subprocess.run([str(exe), "hi"], stdout=subprocess.PIPE, check=True)
        self.assertIn(b"app one hi", out.stdout)
        # assets are installed — the release stands alone
        self.assertEqual((run / "current" / "assets" / "data.txt").read_text().strip(),
                         "assets one")
        self.assertTrue((run / sha / "release.json").is_file())
        # same sha again: no rebuild
        rec2 = self.release().update(sha, make_config())
        self.assertFalse(rec2["changed"])

    def test_smoke_failure_keeps_previous_release(self):
        sha1 = self.fx.commit("one")
        self.release().update(sha1, make_config())
        sha2 = self.fx.commit("two")
        bad = make_config(smoke=[["sh", "-c", "exit 1"]])
        with self.assertRaises(MR.MainReleaseError):
            self.release().update(sha2, bad)
        run = self.state / "integration" / "test-repo" / "tip" / "run"
        self.assertEqual(os.readlink(run / "current"), sha1)  # untouched
        self.assertFalse((run / sha2).exists())               # staging cleaned up
        status = self.release().status()
        self.assertEqual(status["current"]["sha"], sha1)
        self.assertEqual(status["requested_sha"], sha2)
        self.assertEqual(status["lag_commits"], 1)            # lag visible while rebuilding
        self.assertIsNotNone(status["error"])

    def test_escaping_symlink_fails_completeness(self):
        sha = self.fx.commit("one")
        cfg = make_config(install=[[
            "sh", "-c",
            "mkdir -p {dest}/bin && cp {source}/app.py {dest}/bin/app "
            "&& chmod +x {dest}/bin/app && ln -s ../../../source/assets {dest}/assets"]])
        with self.assertRaises(MR.MainReleaseError) as ctx:
            self.release().update(sha, cfg)
        self.assertIn("not self-contained", str(ctx.exception))
        self.assertIsNone(self.release().status()["current"])

    def test_absolute_symlink_inside_dest_is_rejected(self):
        # a link into {dest} itself survives the completeness realpath check, but the staging
        # rename still breaks it: absolute links are rejected outright
        sha = self.fx.commit("one")
        cfg = make_config(install=[[
            "sh", "-c",
            "mkdir -p {dest}/bin && cp {source}/app.py {dest}/bin/app "
            "&& chmod +x {dest}/bin/app && cp -r {source}/assets {dest}/assets "
            "&& ln -s {dest}/assets/data.txt {dest}/assets/data.abs"]])
        with self.assertRaises(MR.MainReleaseError) as ctx:
            self.release().update(sha, cfg)
        self.assertIn("absolute symlink", str(ctx.exception))
        self.assertIsNone(self.release().status()["current"])

    def test_relative_symlink_inside_dest_is_accepted(self):
        sha = self.fx.commit("one")
        cfg = make_config(install=[[
            "sh", "-c",
            "mkdir -p {dest}/bin && cp {source}/app.py {dest}/bin/app "
            "&& chmod +x {dest}/bin/app && cp -r {source}/assets {dest}/assets "
            "&& ln -s data.txt {dest}/assets/data.rel"]])
        rec = self.release().update(sha, cfg)
        self.assertTrue(rec["changed"])
        run = self.state / "integration" / "test-repo" / "tip" / "run"
        # the relative link survives the staging rename and still reads the installed asset
        self.assertEqual((run / "current" / "assets" / "data.rel").read_text().strip(),
                         "assets one")
        self.assertEqual(os.readlink(run / "current" / "assets" / "data.rel"), "data.txt")

    def test_missing_executable_fails(self):
        sha = self.fx.commit("one")
        cfg = make_config(install=[["sh", "-c", "mkdir -p {dest}"]])
        with self.assertRaises(MR.MainReleaseError) as ctx:
            self.release().update(sha, cfg)
        self.assertIn("missing its executable", str(ctx.exception))

    def test_keep_two_and_rollback_reuse(self):
        shas = [self.fx.commit(m) for m in ("one", "two", "three")]
        rel = self.release()
        for sha in shas:
            rel.update(sha, make_config())
        run = self.state / "integration" / "test-repo" / "tip" / "run"
        present = sorted(p.name for p in run.iterdir()
                         if p.is_dir() and not p.is_symlink() and not p.name.startswith("."))
        self.assertEqual(present, sorted(shas[1:]))  # keep=2: oldest pruned, newest two kept
        self.assertEqual(os.readlink(run / "current"), shas[2])
        # rollback to the retained older release: reused, not rebuilt (immutable)
        rec = rel.update(shas[1], make_config(smoke=[["sh", "-c", "exit 1"]]))
        self.assertTrue(rec["changed"])
        self.assertTrue(rec["reused"])  # smoke config change cannot resurrect a rebuild
        self.assertEqual(os.readlink(run / "current"), shas[1])
        # and the retained release still runs
        out = subprocess.run([str(run / "current" / "bin" / "app")],
                             stdout=subprocess.PIPE, check=True)
        self.assertIn(b"app two", out.stdout)

    def test_incomplete_leftover_is_never_served(self):
        sha1 = self.fx.commit("one")
        rel = self.release()
        rel.update(sha1, make_config())
        run = self.state / "integration" / "test-repo" / "tip" / "run"
        fake = run / ("f" * 40)  # a crashed install: no release.json
        fake.mkdir()
        os.replace(run / "current", run / "current.old")  # not atomic, simulating mess
        os.symlink(fake.name, run / "current")
        status = rel.status()
        self.assertIsNone(status["current"])  # incomplete target: treated as no current
        sha2 = self.fx.commit("two")
        rel.update(sha2, make_config())
        self.assertFalse(fake.exists())  # pruned
        self.assertEqual(os.readlink(run / "current"), sha2)

    def test_concurrent_updates_publish_coherently(self):
        sha1 = self.fx.commit("one")
        sha2 = self.fx.commit("two")
        rel = self.release()
        results: list[dict] = []
        errors: list[BaseException] = []

        def go(sha):
            try:
                results.append(rel.update(sha, make_config()))
            except BaseException as exc:  # noqa: BLE001
                errors.append(exc)

        threads = [threading.Thread(target=go, args=(s,)) for s in (sha1, sha2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=60)
        self.assertEqual(errors, [])
        # whichever request won, current is a complete release consistent with the last
        # recorded request (coalescing: lag settles to 0 once both updates finished)
        status = rel.status()
        self.assertIn(status["current"]["sha"], (sha1, sha2))
        self.assertEqual(status["requested_sha"], status["current"]["sha"])
        self.assertIsNone(status["lag_commits"])  # current == requested: no lag to report

    def test_placeholder_whole_entry_and_substring(self):
        sha = self.fx.commit("one")
        seen = self.cache  # marker written through the build command
        cfg = make_config()
        cfg["main"]["build"] = [["sh", "-c", "printf '%s' {build} > {build}/where"]]
        rec = self.release().update(sha, cfg)
        build_dir = self.cache / "integration" / "test-repo" / "tip" / "build"
        self.assertEqual((build_dir / "where").read_text(), str(build_dir))
        self.assertTrue(rec["changed"])
        self.assertTrue(seen.exists())

    def test_default_state_cache_roots_helpers(self):
        self.assertEqual(MR._state_root().name, "relay")
        self.assertEqual(MR._cache_root().name, "relay")


if __name__ == "__main__":
    unittest.main()
