# SPDX-License-Identifier: AGPL-3.0-or-later
"""Cutover rehearsal for card #3MH4: the migration guide's operator sequence, run end to end
through the shipped CLIs (`scripts/relay-land`, `scripts/land.py`) on a scratch repository.

`docs/PARALLEL-DEVELOPMENT-MIGRATION.md` is the script; this file follows it step by step with
the state a real cutover meets: a legacy `land.py` session holding an unlanded hunk, a dirty
human checkout (staged, unstaged, untracked and ignored files), jobs queued behind a running
publisher, a Board write, then pause, rollback and re-activation. Every step checks what the
guide promises: nothing is reset, nothing pending is dropped, the legacy hunk lands after
rollback exactly as its author left it, and only one publisher ever moves `main`.

It never touches the actual repository: everything lives under one temporary directory, with
its own land root, state root and cache root.
"""
from __future__ import annotations

import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import integration_service as S  # noqa: E402
from relay_core import projectconf  # noqa: E402

PY = sys.executable
RELAY_LAND = ROOT / "scripts" / "relay-land"
LAND_PY = ROOT / "scripts" / "land.py"
GUIDE = ROOT / "docs" / "PARALLEL-DEVELOPMENT-MIGRATION.md"
CONTRACT = ROOT / "docs" / "TREES-AND-LANDING.md"
CLI_TIMEOUT = 120

APP = ("def first():\n    return 'first'\n\n\n"
       "def second():\n    return 'second'\n")
DATA = "first=base\n" + "".join("spacer=%d\n" % n for n in range(38)) + "second=base\n"
TEST = ("import pathlib, unittest\n\n\n"
        "class DataTest(unittest.TestCase):\n"
        "    def test_data(self):\n"
        "        lines = pathlib.Path('data.txt').read_text().splitlines()\n"
        "        self.assertEqual(len(lines), 40)\n"
        "        self.assertTrue(all('=' in line and 'BAD' not in line for line in lines))\n")
CONFIG = ('version = 1\n[project]\ntarget = "main"\n'
          '[workspace]\nexclude = [".board"]\nmax_workspaces = 8\n'
          '[verification]\ncommands = [["%s", "-m", "unittest", "discover", "-s", "tests"]]\n'
          'timeout_seconds = 60\n'
          '[resources]\nmemory_bytes = 1048576\ndisk_bytes = 1048576\ncpus = 1\n'
          '[main]\ninstall = [["sh", "-c", "mkdir -p {dest}/bin && cp {source}/app.py '
          '{dest}/bin/app.py"]]\nexecutable = "bin/app.py"\n'
          '[reconcile]\nenabled = false\n' % PY)


def checkout_state(repo, env):
    """What a cutover must leave alone: HEAD, the working tree and the index."""
    def out(*args):
        return subprocess.run(["git", *args], cwd=str(repo), env=env, text=True,
                              capture_output=True, timeout=30).stdout
    return {"head": out("rev-parse", "HEAD").strip(),
            "status": out("status", "--porcelain", "--ignored", "--untracked-files=all"),
            "index": out("ls-files", "-s"),
            "files": {p: (repo / p).read_text() for p in ("app.py", "data.txt", "README.md",
                                                           "notes.txt", "build/out.bin")}}


@unittest.skipUnless(sys.platform.startswith("linux"), "queue publication is Linux-first")
class CutoverRehearsal(unittest.TestCase):
    """The guide's sequence, on a scratch project with every kind of pre-existing work."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="relay-cutover-rehearsal-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / "project"
        self.state = self.root / "state"
        self.land_root = self.root / "land"
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(("GIT_", "PYTHON", "RELAY_"))}
        self.env.update(XDG_STATE_HOME=str(self.state), XDG_CACHE_HOME=str(self.root / "cache"),
                        XDG_CONFIG_HOME=str(self.root / "config"), RELAY_KEYRING="off",
                        RELAY_LAND_ROOT=str(self.land_root),
                        GIT_AUTHOR_NAME="Rehearsal", GIT_AUTHOR_EMAIL="r@example.invalid",
                        GIT_COMMITTER_NAME="Rehearsal", GIT_COMMITTER_EMAIL="r@example.invalid")
        self.repo.mkdir()
        self.git("init", "-q", "-b", "main")
        (self.repo / "app.py").write_text(APP)
        (self.repo / "data.txt").write_text(DATA)
        (self.repo / "README.md").write_text("# scratch project\n")
        (self.repo / ".gitignore").write_text("build/\n")
        (self.repo / "tests").mkdir()
        (self.repo / "tests/test_app.py").write_text(TEST)
        (self.repo / ".board/threads").mkdir(parents=True)
        (self.repo / ".board/BOARD.md").write_text("# Board\n")
        (self.repo / ".relay").mkdir()
        (self.repo / ".relay/project.toml").write_text(CONFIG)
        # land.py board-sync in queue mode imports relay_core from <repo>/backend.
        (self.repo / "backend").mkdir()
        os.symlink(ROOT / "backend" / "relay_core", self.repo / "backend" / "relay_core")
        (self.repo / ".gitignore").write_text("build/\nbackend/\n")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "baseline with a committed .relay/project.toml")
        self.baseline = self.git("rev-parse", "HEAD")
        self.children = []
        self.addCleanup(self.stop_children)

    # ----------------------------------------------------------------- helpers

    def git(self, *args, cwd=None, check=True):
        proc = subprocess.run(["git", *args], cwd=str(cwd or self.repo), env=self.env,
                              text=True, capture_output=True, timeout=30)
        if check:
            self.assertEqual(proc.returncode, 0, "git %s: %s" % (" ".join(args), proc.stderr))
        return proc.stdout.strip() if check else proc

    def cli_argv(self, *args):
        return [PY, str(RELAY_LAND), "--repo", str(self.repo), "--state-root", str(self.state),
                *map(str, args)]

    def cli(self, *args, codes=(0,)):
        proc = subprocess.run(self.cli_argv(*args), cwd=str(self.root), env=self.env, text=True,
                              capture_output=True, timeout=CLI_TIMEOUT)
        self.assertIn(proc.returncode, codes, "relay-land %s exited %d\nstdout: %s\nstderr: %s"
                      % (" ".join(map(str, args)), proc.returncode, proc.stdout, proc.stderr))
        try:
            return proc.returncode, json.loads(proc.stdout)
        except json.JSONDecodeError:
            self.fail("relay-land %s printed no JSON: %r" % (" ".join(map(str, args)), proc.stdout))

    def land(self, *args, codes=(0,)):
        proc = subprocess.run([PY, str(LAND_PY), "--root", str(self.land_root), *args],
                              cwd=str(self.repo), env=self.env, text=True, capture_output=True,
                              timeout=CLI_TIMEOUT)
        self.assertIn(proc.returncode, codes, "land.py %s exited %d\n%s%s"
                      % (" ".join(args), proc.returncode, proc.stdout, proc.stderr))
        return proc.returncode, proc.stdout + proc.stderr

    def spawn(self, argv):
        proc = subprocess.Popen(argv, cwd=str(self.root), env=self.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.children.append(proc)
        return proc

    def stop_children(self):
        for proc in self.children:
            if proc.poll() is None:
                proc.kill()
            try:
                proc.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                pass

    def wait_for(self, predicate, *, what, timeout=60):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.2)
        self.fail("timed out waiting for %s" % what)

    def service(self):
        return S.IntegrationService(self.repo, state_root=self.state,
                                    cache_root=self.root / "cache", land_root=self.land_root)

    def author(self, name, old, new):
        rc, ws = self.cli("workspace", "create", name)
        path = Path(ws["execution_cwd"])
        data = path / "data.txt"
        data.write_text(data.read_text().replace(old, new))
        self.git("commit", "-q", "-am", "%s edits data.txt" % name, cwd=path)
        return ws, path, self.git("rev-parse", "HEAD", cwd=path)

    def comparable(self, state):
        # `ls-files -s` is stable; `status` may reorder nothing. Compare as is.
        return state

    # ----------------------------------------------------------------- the rehearsal

    def test_operator_sequence_preserves_legacy_work_queued_jobs_and_the_checkout(self):
        # --- Before cutover: a legacy author holds an unlanded hunk, and the human checkout
        # is dirty in every way the guide lists (staged, unstaged, untracked, ignored).
        rc, out = self.land("begin", "legacy-author", "app.py", "--contact", "rehearsal pane")
        (self.repo / "app.py").write_text(APP.replace("return 'first'", "return 'first'  # legacy hunk"))
        (self.repo / "README.md").write_text("# scratch project\n\nstaged by the human\n")
        self.git("add", "README.md")
        (self.repo / "notes.txt").write_text("untracked note\n")
        (self.repo / "build").mkdir()
        (self.repo / "build/out.bin").write_text("ignored build output\n")
        before = checkout_state(self.repo, self.env)
        self.assertEqual(before["head"], self.baseline)

        # --- Guide step 2: inventory. The legacy session is listed, no publisher runs, the
        # dirty checkout is counted, and the config at the target tip is accept-able.
        rc, inv = self.cli("inventory")
        self.assertEqual(inv["mode"], "legacy")
        self.assertEqual(inv["target_sha"], self.baseline)
        self.assertEqual(inv["head"]["branch"], "main")
        self.assertEqual([s["name"] for s in inv["legacy"]["sessions"]], ["legacy-author"])
        self.assertEqual(inv["legacy"]["sessions"][0]["claims"], 1)
        self.assertEqual(inv["legacy"]["sessions"][0]["contact"], "rehearsal pane")
        self.assertEqual(inv["legacy"]["processes"], [])
        # ignored: build/out.bin and the backend/ symlink the fixture adds for land.py.
        self.assertEqual({k: inv["dirty"][k] for k in ("staged", "unstaged", "untracked", "ignored")},
                         {"staged": 1, "unstaged": 1, "untracked": 1, "ignored": 2})
        self.assertTrue(inv["config"]["present"], inv["config"])
        self.assertTrue(inv["hook"]["relay"], "land.py begin installed the shared-index hook")
        self.assertIsNone(inv["accepted_policy"])
        self.assertEqual(before, checkout_state(self.repo, self.env), "inventory changed nothing")

        # --- Guide step 2, dry run: an idle legacy session with hunks is *not* a blocker
        # (the guide says so: it is the operator's readiness gate), and nothing changes.
        rc, dry = self.cli("activate", "--dry-run")
        self.assertTrue(dry["can_activate"], dry["blockers"])
        self.assertTrue(any("human" in step for step in dry["plan"]), dry["plan"])
        self.assertEqual(before, checkout_state(self.repo, self.env), "dry run changed nothing")

        # --- Guide step 4: activate. HEAD moves symbolically to `human` at the same SHA;
        # files and index untouched; the legacy session and its hunk are still there.
        rc, act = self.cli("activate")
        self.assertTrue(act["activated"])
        self.assertTrue(act["moved_head"])
        self.assertEqual(act["target_sha"], self.baseline)
        self.assertEqual(act["policy_hash"], inv["config"]["policy_hash"])
        self.assertEqual(self.git("symbolic-ref", "HEAD"), "refs/heads/human")
        self.assertEqual(before, checkout_state(self.repo, self.env), "activate rewrote the checkout")
        rc, who = self.land("who")
        self.assertIn("legacy-author", who)
        self.assertIn("app.py", who)
        rc, inv = self.cli("inventory")
        self.assertEqual((inv["mode"], inv["marker"]["mode"], inv["registry_mode"]),
                         ("queue", "queue", "queue"))
        self.assertEqual(inv["accepted_policy"]["hash"], act["policy_hash"])
        self.assertEqual(inv["human_branch"], {"name": "human", "sha": self.baseline})

        # --- Sole publisher: the legacy writer refuses, its hunk stays in the tree.
        rc, refused = self.land("commit", "legacy-author", "-m", "legacy landing", codes=(2,))
        self.assertIn("relay-land", refused)
        self.assertEqual(self.git("rev-parse", "main"), self.baseline)
        self.assertIn("# legacy hunk", (self.repo / "app.py").read_text())
        self.assertEqual(before, checkout_state(self.repo, self.env))

        # --- The publisher runs (as the systemd unit would), and a second tick is refused.
        daemon = self.spawn(self.cli_argv("run", "--no-main", "--interval", "0.2"))
        self.wait_for(lambda: self.cli("inventory")[1]["daemon"].get("running"), what="daemon")
        rc, second = self.cli("run", codes=(7,))
        self.assertIn("run loop", json.dumps(second).lower())

        # --- Author work lands through the queue while the human checkout stays behind.
        alice, alice_path, alice_sha = self.author("alice", "first=base", "first=alice")
        rc, job_a = self.cli("submit", alice_sha, "--request-id", "alice-1",
                             "--workspace-id", alice["workspace_id"])
        self.wait_for(lambda: self.cli("status", job_a["id"])[1]["status"] == "landed",
                      what="alice's job to land")
        rc, receipt = self.cli("receipt", job_a["id"])
        self.assertTrue(receipt["verified"])
        after_alice = self.git("rev-parse", "main")
        self.assertEqual(after_alice, receipt["published_sha"])
        self.assertNotEqual(after_alice, self.baseline)
        self.assertEqual(self.git("rev-parse", "HEAD"), self.baseline, "human checkout not moved")
        self.assertEqual(before, checkout_state(self.repo, self.env), "publication touched the checkout")

        # --- Guide "Roll back" step 1: stop the runner (SIGTERM, like systemctl stop) with
        # work still queued behind it: a code job and a Board snapshot routed by land.py.
        daemon.send_signal(signal.SIGTERM)
        self.assertEqual(daemon.wait(timeout=60), 0, daemon.stderr.read())
        self.wait_for(lambda: not self.cli("inventory")[1]["daemon"].get("running"), what="daemon exit")
        bob, bob_path, bob_sha = self.author("bob", "second=base", "second=bob")
        rc, job_b = self.cli("submit", bob_sha, "--request-id", "bob-1",
                             "--workspace-id", bob["workspace_id"])
        (self.repo / ".board/BOARD.md").write_text("# Board\n\nedited by a pane during cutover\n")
        rc, synced = self.land("board-sync", "pane-token", "-m", "board: pane turn", ".board/BOARD.md")
        self.assertIn("landq job", synced)
        self.assertEqual(self.git("rev-parse", "main"), after_alice, "board-sync published nothing itself")
        rc, inv = self.cli("inventory")
        self.assertEqual(inv["queue"]["pending"], 2)
        rc, paused = self.cli("pause", "--reason", "rehearsal rollback")
        self.assertEqual(paused["mode"], "paused")
        rc, late = self.cli("submit", bob_sha, "--request-id", "bob-late", codes=(2,))
        self.assertIn("paused", late["error"])
        dirty_before_rollback = checkout_state(self.repo, self.env)

        # --- Step 2: `rollback --keep-head`, because the checkout is dirty. Mode legacy,
        # HEAD still on `human`, nothing reset, both jobs retained.
        rc, rolled = self.cli("rollback", "--keep-head")
        self.assertTrue(rolled["rolled_back"])
        self.assertEqual(rolled["mode"], "legacy")
        self.assertEqual(rolled["jobs_retained"], 3)
        self.assertEqual(self.git("symbolic-ref", "HEAD"), "refs/heads/human")
        self.assertEqual(dirty_before_rollback, checkout_state(self.repo, self.env))
        self.assertEqual(self.cli("status", job_b["id"])[1]["status"], "queued")
        self.assertEqual(bob_sha, self.git("rev-parse", "refs/heads/" + bob["branch"]))
        self.assertTrue(bob_path.is_dir())

        # --- Step 3, the guide's own checks: HEAD is a branch, behind main, status not empty.
        self.assertEqual(self.git("symbolic-ref", "-q", "HEAD"), "refs/heads/human")
        self.assertEqual(self.git("merge-base", "--is-ancestor", "HEAD", "main", check=False).returncode, 0)
        self.assertNotEqual(self.git("status", "--short"), "")
        # The guide offers "run rollback again without --keep-head" as one way to move the
        # checkout onto main. In legacy mode that command is a no-op: it reports so and
        # moves nothing. (Finding for #3MH4; the other documented way, `git switch main`,
        # is the one that works.)
        rc, again = self.cli("rollback")
        self.assertFalse(again["rolled_back"])
        self.assertEqual(again["mode"], "legacy")
        self.assertEqual(self.git("symbolic-ref", "HEAD"), "refs/heads/human")
        # `git switch main` keeps the dirty files (none overlap what the queue landed).
        self.git("switch", "main")
        self.assertEqual(self.git("rev-parse", "HEAD"), after_alice)
        self.assertIn("# legacy hunk", (self.repo / "app.py").read_text())
        self.assertIn("staged by the human", self.git("show", ":README.md"))
        self.assertEqual((self.repo / "notes.txt").read_text(), "untracked note\n")
        self.assertEqual((self.repo / "build/out.bin").read_text(), "ignored build output\n")
        self.assertIn("first=alice", (self.repo / "data.txt").read_text(), "switch brought the landing")
        # `doctor` after a cutover always finds something: the human branch, the workspace
        # branches and worktrees, the staged README. All "report only"; no stale index entry.
        rc, doctor = self.land("doctor", codes=(0, 1, 2))
        self.assertIn("report only", doctor)
        self.assertIn("README.md", doctor)
        self.assertNotIn("stale", doctor.lower())

        # --- Only now, per the guide, may a legacy session resume: its hunk lands on the
        # moved tip, and only its hunk.
        rc, landed = self.land("commit", "legacy-author", "-m", "legacy hunk lands after rollback")
        new_tip = self.git("rev-parse", "main")
        self.assertNotEqual(new_tip, after_alice)
        self.assertEqual(self.git("diff", "--name-only", after_alice, new_tip), "app.py")
        self.assertIn("# legacy hunk", self.git("show", "main:app.py"))
        self.assertIn("first=alice", self.git("show", "main:data.txt"), "the queue's landing survived")
        self.assertEqual(self.git("rev-parse", "HEAD"), new_tip, "the checkout follows the tip")
        self.assertIn("staged by the human", self.git("show", ":README.md"), "staged work kept")

        # --- Re-activation with the retained jobs. The `human` branch is now behind the
        # tip, so the default activation refuses: the operator names a fresh human branch
        # (or moves the old one). Neither is in the guide's rollback section (finding).
        rc, dry = self.cli("activate", "--dry-run", codes=(2,))
        self.assertFalse(dry["can_activate"])
        self.assertTrue(any("human" in b and "not the target tip" in b for b in dry["blockers"]), dry["blockers"])
        rc, dry = self.cli("activate", "--dry-run", "--human-branch", "human-2")
        self.assertTrue(dry["can_activate"], dry["blockers"])
        rc, act = self.cli("activate", "--human-branch", "human-2")
        self.assertTrue(act["activated"])
        self.assertEqual(self.git("symbolic-ref", "HEAD"), "refs/heads/human-2")
        self.assertEqual(self.git("rev-parse", "HEAD"), new_tip)
        # The retained jobs publish on the new tip, one tick each; nothing was lost.
        rc, tick = self.cli("run", "--once", "--no-main")
        self.assertEqual(tick["job"]["status"], "landed", tick)
        rc, tick = self.cli("run", "--once", "--no-main")
        self.assertEqual(tick["job"]["status"], "landed", tick)
        self.assertEqual(self.cli("status", job_b["id"])[1]["status"], "landed")
        final = self.git("show", "main:data.txt")
        self.assertIn("first=alice", final)
        self.assertIn("second=bob", final)
        self.assertIn("# legacy hunk", self.git("show", "main:app.py"))
        self.assertIn("edited by a pane during cutover", self.git("show", "main:.board/BOARD.md"))
        rc, jobs = self.cli("status")
        self.assertEqual({j["status"] for j in jobs}, {"landed"})
        self.assertIn("# legacy hunk", (self.repo / "app.py").read_text())
        self.assertEqual((self.repo / "notes.txt").read_text(), "untracked note\n")


@unittest.skipUnless(sys.platform.startswith("linux"), "queue publication is Linux-first")
class GuideMatchesTheShippedCommands(unittest.TestCase):
    """Every `relay-land` and `land.py` invocation the guides print must parse as written."""

    def relay_land_invocations(self, text):
        """`relay-land [--repo X] VERB ARGS...` lines, backslash continuations joined,
        stopped at a pipe, backtick or closing quote."""
        text = text.replace("\\\n", " ")
        found = []
        for m in re.finditer(r"(?m)(?:^\s*|`|=\S*/)relay-land (?:--repo \S+ )?([a-z-]+)([^`|\n)]*)", text):
            found.append((m.group(1), m.group(2).split()))
        return found

    def test_relay_land_verbs_and_flags_in_the_guides_exist(self):
        """Every documented verb parses, with every documented flag, through the shipped CLI's
        own dispatch (landq's queue verbs and the service verbs alike): `--help` appended to
        the documented tokens must be answered by argparse, not by an unknown-verb refusal."""
        seen = 0
        for doc in (GUIDE, CONTRACT):
            for verb, rest in self.relay_land_invocations(doc.read_text()):
                if verb in ("run",):
                    rest = [t for t in rest if t != "--"]
                tokens = [verb] + [t for t in rest if t.startswith("--") or not t.startswith("-")]
                # positional placeholders (COMMIT_SHA, JOB_ID, '#ABCD') are fine for --help;
                # a `-- ARGS` remainder is dropped.
                if "--" in tokens:
                    tokens = tokens[:tokens.index("--")]
                with self.subTest(doc=doc.name, argv=tokens):
                    proc = subprocess.run([PY, str(RELAY_LAND), *tokens, "--help"], text=True,
                                          capture_output=True, timeout=60)
                    self.assertEqual(proc.returncode, 0, "relay-land %s --help: %s%s"
                                     % (" ".join(tokens), proc.stdout, proc.stderr))
                    self.assertIn("usage", proc.stdout.lower() + proc.stderr.lower())
                    seen += 1
        self.assertGreater(seen, 15, "the regex found too few invocations to be checking anything")

    def test_land_py_verbs_in_the_guide_exist(self):
        verbs = set(re.findall(r"land\.py ([a-z-]+)", GUIDE.read_text()))
        self.assertTrue(verbs >= {"who", "orphans", "doctor", "commit", "abandon", "reap"}, verbs)
        for verb in sorted(verbs):
            with self.subTest(verb=verb):
                proc = subprocess.run([PY, str(LAND_PY), verb, "--help"], text=True,
                                      capture_output=True, timeout=60)
                self.assertEqual(proc.returncode, 0, "land.py %s --help: %s" % (verb, proc.stderr))

    def test_inventory_keys_the_guide_reads_exist(self):
        # The guide's one-liner reads `inventory` JSON at ["legacy"] with `sessions`/`processes`.
        source = (ROOT / "backend/relay_core/integration_service.py").read_text()
        self.assertIn('"legacy": {"land_root"', source)
        self.assertIn('"sessions": self.legacy_sessions()', source)
        self.assertIn('"processes": self.legacy_processes()', source)


class PreparedRelayConfig(unittest.TestCase):
    """The guide's prepared `.relay/project.toml` for relay-terminal is not committed; it must
    at least parse under the shipped validator, and the service must export the warm build
    directory the guide says a gate receives."""

    def toml(self):
        text = GUIDE.read_text()
        match = re.search(r"```toml\n(.*?)```", text, re.S)
        self.assertIsNotNone(match, "the guide lost its prepared TOML block")
        import tomllib
        return tomllib.loads(match.group(1))

    def test_prepared_config_normalizes_with_a_gate_and_an_installable_main(self):
        cfg = projectconf.normalize_config(self.toml())
        self.assertEqual(cfg["project"]["target"], "main")
        self.assertTrue(cfg["verification"]["commands"], "no required gate")
        self.assertTrue(cfg["main"]["install"] and cfg["main"]["smoke"], cfg["main"])
        self.assertEqual(cfg["main"]["executable"], "bin/relay")
        self.assertIn(".board", cfg["workspace"]["exclude"])
        self.assertEqual(len(projectconf.policy_hash(cfg)), 64)

    def test_gate_environment_exports_the_warm_build_directory_the_guide_names(self):
        with tempfile.TemporaryDirectory(prefix="relay-gate-env-") as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
            for args in (("init", "-q", "-b", "main"), ("commit", "-q", "--allow-empty",
                                                        "-m", "x", "--author", "A <a@b.c>")):
                subprocess.run(["git", *args], cwd=str(repo), env={**env, "GIT_COMMITTER_NAME": "A",
                                                                   "GIT_COMMITTER_EMAIL": "a@b.c"},
                               check=True, capture_output=True)
            service = S.IntegrationService(repo, state_root=Path(tmp) / "state",
                                           cache_root=Path(tmp) / "cache", land_root=Path(tmp) / "land")
            gate_env = service.gate_env()
            self.assertEqual(gate_env["RELAY_BUILD_DIR"], gate_env["VERIFY_BUILD"])
            self.assertTrue(Path(gate_env["RELAY_BUILD_DIR"]).is_dir())
            self.assertTrue(gate_env["RELAY_BUILD_DIR"].startswith(str(Path(tmp) / "cache")))


if __name__ == "__main__":
    unittest.main()
