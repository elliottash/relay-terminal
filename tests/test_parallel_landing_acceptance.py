# SPDX-License-Identifier: AGPL-3.0-or-later
"""Independent acceptance checks for isolated development and verified landing (card #8J0A,
C1 of #3MH4, contract docs/TREES-AND-LANDING.md).

Written by the C1 verifier without reading the implementers' tests. Every check drives the
landed modules or the `relay-land` / `relay-tree` command lines against throwaway Git
repositories with their own state, cache, land and config roots, and asserts on Git and on
durable state rather than on return values alone. No check activates a real project, starts a
real model or guest, or touches the user's state: the reconciler is either disabled by the
project's accepted config or injected with a fake model call.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT / "backend"
sys.path.insert(0, str(BACKEND))

from relay_core import (integration_service, integration_slots, landq,  # noqa: E402
                        projectconf, trees)

PY = sys.executable
CLI_TIMEOUT = 120


def _scrubbed(extra=None) -> dict:
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("GIT_", "RELAY_", "PYTHON"))}
    env.update(extra or {})
    return env


def git(cwd, *args, env=None, check=True) -> str:
    proc = subprocess.run(["git", "-c", "core.hooksPath=/dev/null", *args], cwd=cwd,
                          env=_scrubbed(env), text=True, capture_output=True, timeout=60)
    if check and proc.returncode:
        raise AssertionError("git %s failed in %s: %s" % (" ".join(args), cwd, proc.stderr))
    return proc.stdout.strip()


CONFIG = """\
version = 1
[project]
target = "main"
[workspace]
exclude = [".board"]
max_workspaces = {max_workspaces}
[verification]
commands = {commands}
timeout_seconds = 120
environment = {{ PYTHONDONTWRITEBYTECODE = "1" }}
[resources]
memory_bytes = 16777216
disk_bytes = 1048576
cpus = 1
[reconcile]
enabled = {reconcile}
max_attempts = 2
tokens_per_case = {tokens_per_case}
tokens_per_day = 1000000
{main}"""

PYTEST = [PY, "-m", "pytest", "-q", "-p", "no:cacheprovider"]

APP = "".join("def f%02d():\n    return %d\n\n\n" % (n, n) for n in range(30))

TEST_APP = textwrap.dedent("""\
    import app


    def test_f00():
        assert app.f00() == 0


    def test_f29():
        assert app.f29() == 29
    """)

CARD = textwrap.dedent("""\
    ---
    id: AB12
    type: work
    status: executing
    labels: [feature]
    rank: m
    created: '2026-09-26'
    links: {{plans: [], commits: [], evidence: [], related: [], github: null}}
    ---
    # {title}

    ## Issue
    {issue}

    ## Done means
    - both sides' functions keep returning their own values
    """)


class Project:
    """One throwaway repository with its own state/cache/land/config roots."""

    def __init__(self, root: Path, name="project", *, commands=None, reconcile=False,
                 main_section="", max_workspaces=50, tokens_per_case=200000, board=True,
                 shared: Path | None = None):
        self.root = root
        self.repo = root / name
        roots = shared or root
        # The worker and guest launcher resolve the default roots, so the explicit ones are
        # exactly those defaults under this project's own XDG homes.
        self.xdg_state = roots / "xdg-state"
        self.xdg_cache = roots / "xdg-cache"
        self.state = self.xdg_state / "relay"
        self.cache = self.xdg_cache / "relay"
        self.land_root = roots / "land"
        self.config_home = roots / "config"
        for d in (self.repo, self.state, self.cache, self.land_root, self.config_home):
            d.mkdir(parents=True, exist_ok=True)
        git(self.repo, "init", "-q", "-b", "main")
        git(self.repo, "config", "user.name", "C1 Verifier")
        git(self.repo, "config", "user.email", "c1@example.invalid")
        (self.repo / "app.py").write_text(APP)
        (self.repo / "test_app.py").write_text(TEST_APP)
        (self.repo / ".gitignore").write_text("build/\n__pycache__/\n.pytest_cache/\n")
        (self.repo / ".relay").mkdir()
        self.write_config(commands=commands, reconcile=reconcile, main_section=main_section,
                          max_workspaces=max_workspaces, tokens_per_case=tokens_per_case)
        if board:
            from relay_core import board as board_mod
            b = board_mod.Board(self.repo / ".board", self.repo)
            board_mod.scaffold(b)
            (self.repo / ".board" / "features").mkdir(exist_ok=True)
            (self.repo / ".board" / "threads").mkdir(exist_ok=True)
            self.card_path = self.repo / ".board/features/2026-09-26-acceptance.md"
            self.card_path.write_text(CARD.format(title="Acceptance card",
                                                  issue="Change f02 without losing f25."))
            (self.repo / ".board/threads/AB12.md").write_text(
                "<!-- relay:entry 20260926T000000Z-a1 author=owner kind=comment -->\n"
                "### Owner · 2026-09-26 00:00\nfirst entry\n")
        self.commit("baseline")
        self.base = self.head()

    # -- git ------------------------------------------------------------------------------
    def write_config(self, *, commands=None, reconcile=False, main_section="",
                     max_workspaces=50, tokens_per_case=200000):
        commands = commands if commands is not None else [PYTEST]
        (self.repo / ".relay/project.toml").write_text(CONFIG.format(
            commands=json.dumps(commands), reconcile="true" if reconcile else "false",
            main=main_section, max_workspaces=max_workspaces, tokens_per_case=tokens_per_case))

    def commit(self, message, cwd=None):
        cwd = cwd or self.repo
        git(cwd, "add", "-A")
        git(cwd, "commit", "-q", "-m", message)
        return git(cwd, "rev-parse", "HEAD")

    def head(self, cwd=None):
        return git(cwd or self.repo, "rev-parse", "HEAD")

    def main(self):
        return git(self.repo, "rev-parse", "refs/heads/main")

    def show(self, rev, path):
        return git(self.repo, "show", "%s:%s" % (rev, path))

    # -- entry points -----------------------------------------------------------------------
    def env(self, extra=None):
        return _scrubbed({"XDG_STATE_HOME": str(self.xdg_state),
                          "XDG_CACHE_HOME": str(self.xdg_cache),
                          "XDG_CONFIG_HOME": str(self.config_home),
                          "RELAY_LAND_ROOT": str(self.land_root), **(extra or {})})

    def service(self, **kw):
        kw.setdefault("state_root", self.state)
        kw.setdefault("cache_root", self.cache)
        kw.setdefault("land_root", self.land_root)
        return integration_service.IntegrationService(self.repo, **kw)

    def cli_argv(self, *args, scripts=None):
        scripts = Path(scripts or ROOT / "scripts")
        return [PY, str(scripts / "relay-land"), "--repo", str(self.repo), "--state-root",
                str(self.state), "--cache-root", str(self.cache), *args]

    def cli(self, *args, codes=(0,), scripts=None, timeout=CLI_TIMEOUT, env=None):
        proc = subprocess.run(self.cli_argv(*args, scripts=scripts), cwd=self.root,
                              env=self.env(env), text=True, capture_output=True, timeout=timeout)
        if proc.returncode not in codes:
            raise AssertionError("relay-land %s exited %d (wanted %s)\nstdout: %s\nstderr: %s"
                                 % (" ".join(args), proc.returncode, codes, proc.stdout,
                                    proc.stderr))
        try:
            data = json.loads(proc.stdout) if proc.stdout.strip() else None
        except ValueError:
            data = proc.stdout
        return proc.returncode, data

    def activate(self):
        rc, result = self.cli("activate")
        assert result["activated"], result
        return result

    def workspace(self, session, card=None):
        args = ["workspace", "create", session] + (["--card", card] if card else [])
        return self.cli(*args)[1]

    def edit(self, ws, path, old, new, message):
        cwd = Path(ws["execution_cwd"])
        target = cwd / path
        text = target.read_text()
        assert old in text, (old, path)
        target.write_text(text.replace(old, new))
        return self.commit(message, cwd=cwd)


class AcceptanceCase(unittest.TestCase):
    """Shared setup: a temporary directory released on teardown, never a daemon left behind."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="relay-c1-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.procs = []
        self.addCleanup(self._reap)

    def _reap(self):
        for proc in self.procs:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=30)

    def project(self, name="project", **kw):
        return Project(self.root / name, **kw)

    def spawn(self, argv, env, **kw):
        proc = subprocess.Popen(argv, env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, start_new_session=True, **kw)
        self.procs.append(proc)
        return proc

    def wait_for(self, predicate, timeout=60.0, what="condition"):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(0.05)
        self.fail("timed out waiting for %s" % what)


# --------------------------------------------------------------------------------------------
# Authors, submissions and receipts
# --------------------------------------------------------------------------------------------

class AuthorsAndReceipts(AcceptanceCase):

    def test_two_same_file_authors_land_through_the_cli_with_exact_verified_receipts(self):
        p = self.project()
        p.activate()
        alice = p.workspace("alice-session", card="AB12")
        bob = p.workspace("bob-session")
        self.assertNotEqual(alice["execution_cwd"], bob["execution_cwd"])
        # Both edit app.py (and alice the test too); neither sees the other.
        a_sha = p.edit(alice, "app.py", "return 2\n", "return 2  # alice\n", "alice #AB12")
        b_sha = p.edit(bob, "app.py", "return 25\n", "return 25  # bob\n", "bob")
        self.assertNotIn("bob", (Path(alice["execution_cwd"]) / "app.py").read_text())
        self.assertNotIn("alice", (Path(bob["execution_cwd"]) / "app.py").read_text())
        _, a_job = p.cli("submit", a_sha, "--request-id", "alice-1", "--workspace-id",
                         alice["workspace_id"], "--card", "AB12")
        _, b_job = p.cli("submit", b_sha, "--request-id", "bob-1", "--workspace-id",
                         bob["workspace_id"])
        for _ in range(2):
            p.cli("run", "--once", "--no-main")
        main = p.main()
        text = p.show(main, "app.py")
        self.assertIn("# alice", text)
        self.assertIn("# bob", text)
        policy = p.service().accepted_policy()["hash"]
        for job, sha in ((a_job, a_sha), (b_job, b_sha)):
            _, receipt = p.cli("receipt", job["id"])
            self.assertEqual(sha, receipt["submitted_sha"])
            self.assertTrue(receipt["verified"])
            self.assertEqual(policy, receipt["policy_hash"])
            published = receipt["published_sha"]
            # The receipt names a commit on main, the tree is that commit's, and a passing
            # verification exists for exactly that candidate under the accepted policy.
            git(p.repo, "merge-base", "--is-ancestor", published, main)
            self.assertEqual(git(p.repo, "rev-parse", published + "^{tree}"), receipt["tree"])
            passes = [v for v in p.service().queue.verifications(job["id"])
                      if v["candidate_sha"] == published and v["ok"] and v["verified"]]
            self.assertTrue(passes, "no verification bound to the published sha")
            self.assertEqual(policy, passes[-1]["policy_hash"])
        # The first job fast-forwarded: the author's exact commit is what landed.
        _, first = p.cli("receipt", a_job["id"])
        self.assertEqual(a_sha, first["published_sha"])
        # Neither author's branch was rewritten.
        self.assertEqual(a_sha, git(p.repo, "rev-parse", "refs/heads/" + alice["branch"]))
        self.assertEqual(b_sha, git(p.repo, "rev-parse", "refs/heads/" + bob["branch"]))

    def test_same_line_conflict_goes_back_to_its_author_not_the_owner(self):
        p = self.project()
        p.activate()
        alice = p.workspace("alice-session")
        bob = p.workspace("bob-session", card="AB12")
        a_sha = p.edit(alice, "app.py", "return 7\n", "return 7  # alice\n", "alice")
        b_sha = p.edit(bob, "app.py", "return 7\n", "return 7  # bob\n", "bob #AB12")
        p.cli("submit", a_sha, "--request-id", "a", "--workspace-id", alice["workspace_id"])
        _, b_job = p.cli("submit", b_sha, "--request-id", "b", "--workspace-id",
                         bob["workspace_id"], "--card", "AB12")
        p.cli("run", "--once", "--no-main")
        landed = p.main()
        p.cli("run", "--once", "--no-main", codes=(0, 3))
        self.assertEqual(landed, p.main(), "a conflicting job moved main")
        status = p.cli("status", b_job["id"])[1]
        self.assertIn(status["status"], ("conflict", "failed"))
        _, handoffs = p.cli("handoffs", "--session", "bob-session")
        kinds = {h["kind"] for h in handoffs}
        self.assertTrue(kinds & {"author_required", "conflict"}, handoffs)
        self.assertTrue(all(h["session"] == "bob-session" for h in handoffs))
        # The card thread gets a note; nothing on the Board asks the owner a question.
        thread = (p.repo / ".board/threads/AB12.md").read_text()
        self.assertIn("landq:%s" % b_job["id"], thread)
        self.assertNotIn("kind=question", thread)
        self.assertEqual(b_sha, git(p.repo, "rev-parse", "refs/heads/" + bob["branch"]))

    def test_post_submit_commits_and_dirty_edits_stay_in_the_author_workspace(self):
        p = self.project()
        p.activate()
        ws = p.workspace("author")
        sha = p.edit(ws, "app.py", "return 3\n", "return 3  # submitted\n", "submitted")
        p.cli("submit", sha, "--request-id", "r1", "--workspace-id", ws["workspace_id"])
        later = p.edit(ws, "app.py", "return 4\n", "return 4  # later commit\n", "later")
        cwd = Path(ws["execution_cwd"])
        (cwd / "app.py").write_text((cwd / "app.py").read_text() + "# dirty tail\n")
        (cwd / "notes.txt").write_text("untracked author notes\n")
        p.cli("run", "--once", "--no-main")
        main_text = p.show(p.main(), "app.py")
        self.assertIn("# submitted", main_text)
        self.assertNotIn("later commit", main_text)
        self.assertNotIn("dirty tail", main_text)
        self.assertEqual(later, p.head(cwd))
        self.assertIn("# dirty tail", (cwd / "app.py").read_text())
        self.assertTrue((cwd / "notes.txt").exists())

    def test_a_landed_workspace_does_not_report_unlanded_work(self):
        p = self.project()
        p.activate()
        ws = p.workspace("lands")
        self.assertFalse(p.cli("workspace", "status", ws["workspace_id"])[1]["unlanded"])
        sha = p.edit(ws, "app.py", "return 13\n", "return 13  # landed\n", "landed")
        self.assertTrue(p.cli("workspace", "status", ws["workspace_id"])[1]["unlanded"])
        p.cli("submit", sha, "--request-id", "l1", "--workspace-id", ws["workspace_id"])
        p.cli("run", "--once", "--no-main")
        self.assertFalse(p.cli("workspace", "status", ws["workspace_id"])[1]["unlanded"],
                         "a workspace whose tip landed still reports unlanded work")
        p.edit(ws, "app.py", "return 14\n", "return 14  # later\n", "later")
        self.assertTrue(p.cli("workspace", "status", ws["workspace_id"])[1]["unlanded"])

    def test_duplicate_submit_is_one_job_even_from_racing_processes(self):
        p = self.project()
        p.activate()
        ws = p.workspace("dup")
        sha = p.edit(ws, "app.py", "return 5\n", "return 5  # once\n", "once")
        argv = p.cli_argv("submit", sha, "--request-id", "same-request", "--workspace-id",
                          ws["workspace_id"])
        procs = [self.spawn(argv, p.env()) for _ in range(6)]
        outs = []
        for proc in procs:
            out, err = proc.communicate(timeout=CLI_TIMEOUT)
            self.assertEqual(0, proc.returncode, err)
            outs.append(json.loads(out))
        self.assertEqual(1, len({o["id"] for o in outs}))
        self.assertEqual(1, len(p.cli("status")[1]))
        # Same request id, different commit: refused, not a second job.
        other = p.edit(ws, "app.py", "return 6\n", "return 6  # other\n", "other")
        rc, refused = p.cli("submit", other, "--request-id", "same-request", codes=(2,))
        self.assertIn("already submitted", refused["error"])
        p.cli("run", "--once", "--no-main")
        p.cli("run", "--once", "--no-main")
        self.assertEqual(1, git(p.repo, "log", "--oneline", "%s..main" % p.base).count("\n") + 1)


# --------------------------------------------------------------------------------------------
# Workspace retention
# --------------------------------------------------------------------------------------------

class Retention(AcceptanceCase):

    def test_dirty_clean_unlanded_and_pending_work_are_never_removed(self):
        p = self.project()
        p.activate()
        dirty = p.workspace("dirty-session")
        clean = p.workspace("clean-unlanded")
        pending = p.workspace("pending")
        landed = p.workspace("landed")
        # dirty: uncommitted and ignored files; clean-unlanded: a commit nobody submitted.
        dcwd = Path(dirty["execution_cwd"])
        (dcwd / "app.py").write_text((dcwd / "app.py").read_text() + "# wip\n")
        (dcwd / "build").mkdir()
        (dcwd / "build" / "cache.bin").write_bytes(b"ignored but someone's")
        c_sha = p.edit(clean, "app.py", "return 8\n", "return 8  # unsubmitted\n", "unsubmitted")
        p_sha = p.edit(pending, "app.py", "return 9\n", "return 9  # pending\n", "pending")
        p.cli("submit", p_sha, "--request-id", "pending-1", "--workspace-id",
              pending["workspace_id"])
        l_sha = p.edit(landed, "app.py", "return 10\n", "return 10  # landed\n", "landed")
        # The pane dies: every lease is released.
        for ws, owner in ((dirty, "dirty-session"), (clean, "clean-unlanded"),
                          (pending, "pending"), (landed, "landed")):
            p.cli("workspace", "release", ws["workspace_id"], "--owner", owner)
        for ws in (dirty, clean, pending):
            rc, out = p.cli("workspace", "remove", ws["workspace_id"], codes=(2,))
            self.assertTrue(Path(ws["execution_cwd"]).is_dir(), out)
        self.assertEqual("# wip\n", (dcwd / "app.py").read_text()[-6:])
        self.assertTrue((dcwd / "build/cache.bin").exists())
        self.assertEqual(c_sha, git(p.repo, "rev-parse", "refs/heads/" + clean["branch"]))
        # Restart the service: the retained work is visible as such.
        status = p.cli("workspace", "status", clean["workspace_id"])[1]
        self.assertTrue(status["unlanded"])
        # A landed workspace with its receipt can go, and only then.
        p.cli("submit", l_sha, "--request-id", "landed-1", "--workspace-id",
              landed["workspace_id"])
        while p.cli("run", "--once", "--no-main")[1].get("job"):
            pass
        p.cli("workspace", "remove", landed["workspace_id"])
        self.assertFalse(Path(landed["execution_cwd"]).exists())
        # The pending one landed too, but its dirty-free tree still needs its own receipt.
        p.cli("workspace", "remove", pending["workspace_id"])
        self.assertIn("# pending", p.show(p.main(), "app.py"))


# --------------------------------------------------------------------------------------------
# Competing publishers, crashes and target movement
# --------------------------------------------------------------------------------------------

SLOW_GATE = [PY, "-c", "import os, time, pathlib\n"
             "flag = pathlib.Path(os.environ['C1_FLAG_DIR'])\n"
             "(flag / ('started-%d' % os.getpid())).write_text('')\n"
             "deadline = time.time() + 60\n"
             "while not (flag / 'go').exists() and time.time() < deadline:\n"
             "    time.sleep(0.05)\n"]


class PublishersAndCrashes(AcceptanceCase):

    def slow_project(self):
        p = self.project(commands=[SLOW_GATE, PYTEST])
        self.flags = self.root / "flags"
        self.flags.mkdir()
        self.flag_env = {"C1_FLAG_DIR": str(self.flags)}
        p.activate()
        return p

    def started(self):
        return [f for f in self.flags.iterdir() if f.name.startswith("started-")]

    def test_competing_run_processes_publish_each_job_exactly_once(self):
        p = self.slow_project()
        jobs = []
        for n in (11, 12, 13):
            ws = p.workspace("author-%d" % n)
            sha = p.edit(ws, "app.py", "return %d\n" % n, "return %d  # c%d\n" % (n, n), "c%d" % n)
            jobs.append(p.cli("submit", sha, "--request-id", "c%d" % n, "--workspace-id",
                              ws["workspace_id"])[1])
        env = p.env({"C1_FLAG_DIR": str(self.flags)})
        runners = [self.spawn(p.cli_argv("run", "--once", "--no-main"), env) for _ in range(4)]
        self.wait_for(self.started, what="a gate to start")
        time.sleep(0.5)
        self.assertEqual(1, len(self.started()), "two publishers verified at once")
        (self.flags / "go").write_text("")
        codes = [r.wait(timeout=CLI_TIMEOUT) for r in runners]
        self.assertTrue(all(c in (0, 7) for c in codes), codes)
        # Drain the rest with a single publisher.
        while p.cli("run", "--once", "--no-main", env=self.flag_env)[1].get("job"):
            pass
        statuses = [p.cli("status", j["id"])[1]["status"] for j in jobs]
        self.assertEqual(["landed"] * 3, statuses)
        log = git(p.repo, "log", "--format=%s", "%s..main" % p.base).splitlines()
        subjects = [s for s in log if s.startswith("c1") or s.startswith("Land ")]
        self.assertEqual(len(subjects), len(set(subjects)))
        for n in (11, 12, 13):
            self.assertEqual(1, p.show(p.main(), "app.py").count("# c%d" % n))

    def test_second_daemon_is_refused(self):
        p = self.project()
        p.activate()
        first = self.spawn(p.cli_argv("run", "--no-main", "--interval", "0.2"), p.env())
        self.wait_for(lambda: p.service().daemon_state().get("running"), what="daemon")
        rc, out = p.cli("run", "--no-main", "--max-ticks", "1", codes=(7,))
        self.assertIn("already running", out["error"])
        first.send_signal(signal.SIGTERM)
        self.assertEqual(0, first.wait(timeout=30))
        self.assertFalse(p.service().daemon_state().get("running"))

    def test_sigkill_during_verification_requeues_and_reverifies(self):
        p = self.slow_project()
        ws = p.workspace("crash")
        sha = p.edit(ws, "app.py", "return 14\n", "return 14  # crash\n", "crash")
        job = p.cli("submit", sha, "--request-id", "crash", "--workspace-id",
                    ws["workspace_id"])[1]
        env = p.env({"C1_FLAG_DIR": str(self.flags)})
        runner = self.spawn(p.cli_argv("run", "--once", "--no-main"), env)
        self.wait_for(self.started, what="the gate")
        os.killpg(runner.pid, signal.SIGKILL)
        runner.wait(timeout=30)
        self.assertEqual(p.base, p.main())
        self.assertEqual("verifying", p.cli("status", job["id"])[1]["status"])
        self.assertIsNone(p.service().queue.receipt(job["id"]))
        (self.flags / "go").write_text("")
        p.cli("run", "--once", "--no-main", env=self.flag_env)
        final = p.cli("status", job["id"])[1]
        self.assertEqual("landed", final["status"])
        verifications = p.service().queue.verifications(job["id"])
        # The killed run left no verdict; the one that published was made after the restart.
        self.assertEqual(1, len(verifications))
        self.assertTrue(verifications[0]["ok"])

    def test_crash_before_and_after_the_ref_move(self):
        for fault in ("before_update_ref", "after_update_ref"):
            with self.subTest(fault=fault):
                p = self.project(fault)
                p.activate()
                ws = p.workspace("s-" + fault)
                sha = p.edit(ws, "app.py", "return 15\n", "return 15  # %s\n" % fault, fault)
                service = p.service()
                job = service.submit(sha, request_id=fault, workspace_id=ws["workspace_id"])
                service.queue.faults.add(fault)
                with self.assertRaises(landq.InjectedCrash):
                    service.run_once(main=False)
                moved = p.main() != p.base
                self.assertEqual(fault == "after_update_ref", moved)
                self.assertEqual("publishing", service.queue.status(job["id"])["status"])
                # A fresh process (new objects) settles it.
                fresh = p.service()
                fresh.run_once(main=False)
                fresh.run_once(main=False)
                final = fresh.queue.status(job["id"])
                self.assertEqual("landed", final["status"])
                receipt = fresh.queue.receipt(job["id"])
                self.assertEqual(p.main(), receipt["published_sha"])
                self.assertTrue(receipt["verified"])
                self.assertEqual(1, p.show(p.main(), "app.py").count("# %s" % fault))

    def test_target_movement_during_verification_forces_a_new_verification(self):
        p = self.project()
        p.activate()
        ws = p.workspace("mover")
        sha = p.edit(ws, "app.py", "return 16\n", "return 16  # mover\n", "mover")
        service = p.service()
        job = service.submit(sha, request_id="mover", workspace_id=ws["workspace_id"])
        seen = []
        real = service.verifier

        def verifier(job_, candidate, path):
            seen.append((candidate, git(path, "rev-parse", "HEAD^{tree}")))
            if len(seen) == 1:
                # Someone else's commit lands on main while this gate runs.
                other = git(p.repo, "commit-tree", git(p.repo, "rev-parse", "main^{tree}"),
                            "-p", p.main(), "-m", "external #XT01")
                git(p.repo, "update-ref", "refs/heads/main", other)
            return real(job_, candidate, path)

        service.verifier = verifier
        service.run_once(main=False)
        self.assertEqual(2, len(seen), "the moved target was not reverified")
        self.assertNotEqual(seen[0][0], seen[1][0])
        receipt = service.queue.receipt(job["id"])
        self.assertEqual(seen[1][0], receipt["published_sha"])
        self.assertEqual(receipt["target_before"],
                         git(p.repo, "rev-parse", receipt["published_sha"] + "^1"))


# --------------------------------------------------------------------------------------------
# Policy, gates and stale artifacts
# --------------------------------------------------------------------------------------------

class PolicyAndGates(AcceptanceCase):

    def test_candidate_config_cannot_weaken_the_accepted_gate(self):
        p = self.project()
        p.activate()
        ws = p.workspace("weakener")
        cwd = Path(ws["execution_cwd"])
        (cwd / "app.py").write_text(APP.replace("return 0\n", "return -1\n"))
        (cwd / ".relay/project.toml").write_text(
            (cwd / ".relay/project.toml").read_text().replace(
                json.dumps([PYTEST]), json.dumps([[PY, "-c", "pass"]])))
        sha = p.commit("weaken the gate and break f00", cwd=cwd)
        job = p.cli("submit", sha, "--request-id", "weak", "--workspace-id",
                    ws["workspace_id"])[1]
        p.cli("run", "--once", "--no-main", codes=(5,))
        self.assertEqual(p.base, p.main())
        self.assertEqual("failed", p.cli("status", job["id"])[1]["status"])

    def test_landed_config_change_is_not_in_force_until_captured(self):
        p = self.project()
        p.activate()
        old = p.service().accepted_policy()["hash"]
        ws = p.workspace("config-author")
        cwd = Path(ws["execution_cwd"])
        toml = (cwd / ".relay/project.toml").read_text()
        (cwd / ".relay/project.toml").write_text(
            toml.replace("timeout_seconds = 120", "timeout_seconds = 300"))
        sha = p.commit("longer timeout", cwd=cwd)
        p.cli("submit", sha, "--request-id", "cfg", "--workspace-id", ws["workspace_id"])
        p.cli("run", "--once", "--no-main")
        self.assertEqual(sha, p.main())
        self.assertEqual(old, p.service().accepted_policy()["hash"])
        rc, captured = p.cli("capture-policy")
        self.assertNotEqual(old, captured["hash"])
        self.assertEqual(captured["hash"], p.service().accepted_policy()["hash"])

    def test_failing_and_zero_test_gates_cannot_publish(self):
        cases = {
            "failing": ([PYTEST], "return 0\n", "return 99\n"),
            "zero-tests": ([PYTEST + ["-k", "no_such_test_name"]], "return 1\n", "return 1 \n"),
            "empty-gate": ([], "return 1\n", "return 1 \n"),
        }
        for name, (commands, old, new) in cases.items():
            with self.subTest(name):
                p = self.project(name, commands=commands)
                if not commands:
                    # A project whose accepted config has no gate cannot even activate a
                    # publishing path that approves: its gate refuses every candidate.
                    pass
                p.activate()
                ws = p.workspace(name)
                sha = p.edit(ws, "app.py", old, new, name)
                job = p.cli("submit", sha, "--request-id", name, "--workspace-id",
                            ws["workspace_id"])[1]
                p.cli("run", "--once", "--no-main", codes=(5,))
                self.assertEqual(p.base, p.main())
                self.assertIsNone(p.service().queue.receipt(job["id"]))
                # One actionable handoff per failed job (B1, 65ca8c46 and c8dce251): the
                # author is woken for exactly one pending row, the kind the service names
                # actionable for the job; the gate's `failed` row is kept as history.
                service = p.service()
                history = service.handoffs(workspace_id=ws["workspace_id"], pending_only=False)
                self.assertIn("failed", [h["kind"] for h in history], history)
                pending = service.handoffs(workspace_id=ws["workspace_id"])
                self.assertEqual(1, len(pending), pending)
                self.assertEqual(service.actionable_kind(service.queue.status(job["id"])),
                                 pending[0]["kind"])
                self.assertIn(job["id"], pending[0]["text"])
                self.assertTrue(pending[0]["payload"].get("reason"), pending[0])

    def test_stale_ignored_artifact_cannot_mask_a_failing_candidate(self):
        # The gate's own build step writes an ignored artifact that the tests read.
        build = [PY, "build.py"]
        p = self.project(commands=[build, PYTEST])
        (p.repo / "build.py").write_text(
            "import pathlib\npathlib.Path('build').mkdir(exist_ok=True)\n"
            "pathlib.Path('build/generated.txt').write_text('ok')\n")
        (p.repo / "test_build.py").write_text(
            "import pathlib\n\n\ndef test_generated():\n"
            "    assert pathlib.Path('build/generated.txt').read_text() == 'ok'\n")
        p.commit("build step")
        p.base = p.main()
        p.activate()
        first = p.workspace("first")
        sha1 = p.edit(first, "app.py", "return 3\n", "return 3  # first\n", "first")
        p.cli("submit", sha1, "--request-id", "first", "--workspace-id", first["workspace_id"])
        p.cli("run", "--once", "--no-main")
        self.assertEqual(sha1, p.main())
        # The regression: the build step stops producing the artifact.
        second = p.workspace("second")
        cwd = Path(second["execution_cwd"])
        (cwd / "build.py").write_text("print('forgot to generate')\n")
        sha2 = p.commit("regress the build", cwd=cwd)
        # On a clean checkout of that commit the gate fails.
        clean = self.root / "clean"
        git(p.repo, "worktree", "add", "--detach", str(clean), sha2)
        self.assertFalse(projectconf.run_gate(p.service().accepted_policy()["config"],
                                              clean)["ok"])
        git(p.repo, "worktree", "remove", "--force", str(clean))
        job = p.cli("submit", sha2, "--request-id", "second", "--workspace-id",
                    second["workspace_id"])[1]
        p.cli("run", "--once", "--no-main", codes=(0, 5))
        self.assertEqual(sha1, p.main(),
                         "a stale ignored artifact from an earlier candidate let a failing "
                         "candidate publish")
        self.assertEqual("failed", p.cli("status", job["id"])[1]["status"])


# --------------------------------------------------------------------------------------------
# Cancellation
# --------------------------------------------------------------------------------------------

class Cancellation(AcceptanceCase):

    def test_cancel_queued_in_flight_and_landed(self):
        p = self.project()
        p.activate()
        ws = p.workspace("canceller")
        sha = p.edit(ws, "app.py", "return 17\n", "return 17  # cancelled\n", "c")
        queued = p.cli("submit", sha, "--request-id", "q", "--workspace-id",
                       ws["workspace_id"])[1]
        self.assertEqual("cancelled", p.cli("cancel", queued["id"])[1]["status"])
        p.cli("run", "--once", "--no-main")
        self.assertEqual(p.base, p.main())

        # In flight: cancelled while its gate runs, before the publishing intent.
        service = p.service()
        job = service.submit(sha, request_id="in-flight", workspace_id=ws["workspace_id"])
        real = service.verifier

        def verifier(job_, candidate, path):
            p.cli("cancel", job_["id"])
            return real(job_, candidate, path)

        service.verifier = verifier
        service.run_once(main=False)
        self.assertEqual("cancelled", service.queue.status(job["id"])["status"])
        self.assertEqual(p.base, p.main())
        kinds = {h["kind"] for h in service.handoffs(workspace_id=ws["workspace_id"])}
        self.assertIn("cancelled", kinds)

        # After landing, cancel reports landed and undoes nothing.
        done = service.submit(sha, request_id="landed", workspace_id=ws["workspace_id"])
        service.verifier = real
        service.run_once(main=False)
        landed_main = p.main()
        self.assertEqual("landed", p.cli("cancel", done["id"])[1]["status"])
        self.assertEqual(landed_main, p.main())



# --------------------------------------------------------------------------------------------
# The canonical Board through the same publisher
# --------------------------------------------------------------------------------------------

class BoardThroughTheQueue(AcceptanceCase):

    def test_board_snapshot_is_immutable_while_the_canonical_board_keeps_changing(self):
        p = self.project()
        p.activate()
        service = p.service()
        self.assertEqual(p.repo / ".board", Path(service.board_root))
        ws = p.workspace("board-author", card="AB12")
        # The development workspace has no second Board.
        self.assertFalse((Path(ws["execution_cwd"]) / ".board").exists())
        card = p.card_path
        card.write_text(card.read_text().replace("status: executing", "status: needs-verification"))
        rc, job = p.cli("board-submit", ".board/features/2026-09-26-acceptance.md",
                        "--session", "board-author", "-m", "board: #AB12 to verification")
        self.assertEqual("metadata", job["kind"])
        # A second submit of the same bytes is the same job.
        self.assertEqual(job["id"], p.cli("board-submit", ".board/features/2026-09-26-acceptance.md",
                                          "--session", "board-author")[1]["id"])
        real = service.verifier

        def verifier(job_, candidate, path):
            # A Board write arrives while the metadata job is being verified.
            card.write_text(card.read_text().replace("## Done means", "## Done means\n- later"))
            return real(job_, candidate, path)

        service.verifier = verifier
        result = service.run_once(main=False)
        self.assertEqual("landed", result["job"]["status"], result)
        landed = p.show(p.main(), ".board/features/2026-09-26-acceptance.md")
        self.assertIn("status: needs-verification", landed)
        self.assertNotIn("- later", landed)
        self.assertIn("- later", card.read_text(), "the later canonical edit was lost")
        self.assertEqual(p.base, git(p.repo, "rev-parse", "main~1"))
        self.assertEqual([".board/features/2026-09-26-acceptance.md"],
                         git(p.repo, "diff", "--name-only", "main~1", "main").splitlines())
        # The later edit becomes a later job.
        rc, second = p.cli("board-submit", ".board/features/2026-09-26-acceptance.md",
                           "--session", "board-author")
        self.assertNotEqual(job["id"], second["id"])

    def test_board_path_and_schema_guards(self):
        p = self.project()
        p.activate()
        # Paths outside the Board, climbing paths and absolute paths elsewhere are refused.
        for bad in ("app.py", ".board/../app.py", str(p.repo / "app.py")):
            rc, out = p.cli("board-submit", bad, "--session", "s", codes=(1, 2))
            self.assertIn("error", out)
        # A metadata job whose commit touches code is refused before verification.
        ws = p.workspace("smuggler")
        sha = p.edit(ws, "app.py", "return 18\n", "return 18  # smuggled\n", "smuggle")
        job = p.cli("submit", sha, "--request-id", "smuggle", "--kind", "metadata")[1]
        p.cli("run", "--once", "--no-main", codes=(0, 3))
        self.assertEqual("conflict", p.cli("status", job["id"])[1]["status"])
        self.assertEqual(p.base, p.main())
        self.assertEqual([], p.service().queue.verifications(job["id"]))
        # A symlink in the Board cannot carry code content into main.
        link = p.repo / ".board/features/link.md"
        link.symlink_to(p.repo / "app.py")
        out = p.cli("board-submit", ".board/features/link.md", "--session", "s")[1]
        if out.get("id"):
            p.cli("run", "--once", "--no-main", codes=(0, 3, 5))
        tree = git(p.repo, "ls-tree", "-r", "main", "--", ".board/features/link.md")
        self.assertFalse(tree.startswith("120000"), tree)
        self.assertNotIn("def f00", git(p.repo, "show", "main:.board/features/link.md",
                                        check=False))
        link.unlink()
        # A card that loses its id and a thread that loses an entry fail the schema check.
        thread = p.repo / ".board/threads/AB12.md"
        original_card, original_thread = p.card_path.read_text(), thread.read_text()
        p.card_path.write_text(original_card.replace("id: AB12\n", ""))
        job = p.cli("board-submit", ".board/features/2026-09-26-acceptance.md",
                    "--session", "s")[1]
        p.cli("run", "--once", "--no-main", codes=(5,))
        self.assertEqual("failed", p.cli("status", job["id"])[1]["status"])
        p.card_path.write_text(original_card)
        thread.write_text("")
        job = p.cli("board-submit", ".board/threads/AB12.md", "--session", "s")[1]
        p.cli("run", "--once", "--no-main", codes=(5,))
        self.assertEqual("failed", p.cli("status", job["id"])[1]["status"])
        self.assertEqual(original_thread.strip(), p.show(p.main(), ".board/threads/AB12.md"))


# --------------------------------------------------------------------------------------------
# Host admission across repositories
# --------------------------------------------------------------------------------------------

HOLDER = """import sys, time
sys.path.insert(0, sys.argv[1])
from relay_core import integration_slots
adm = integration_slots.HostAdmission(state_root=sys.argv[2])
cap = adm.capacity()
grant = adm.acquire(sys.argv[3], "hog", memory_bytes=0, disk_bytes=0, cpus=cap.cpus,
                    priority="land", timeout=5)
print("held", cap.cpus, flush=True)
time.sleep(120)
"""


class Admission(AcceptanceCase):

    def test_a_full_host_defers_another_repositorys_job_until_capacity_returns(self):
        shared = self.root / "shared"
        a = Project(self.root / "a", shared=shared)
        b = Project(self.root / "b", shared=shared)
        for p in (a, b):
            p.activate()
        a_id, b_id = a.service().repo_id, b.service().repo_id
        self.assertNotEqual(a_id, b_id)
        holder = self.spawn([PY, "-c", HOLDER, str(BACKEND), str(a.state), a_id],
                            a.env())
        self.assertTrue(holder.stdout.readline().startswith("held"))
        ws = b.workspace("b-author")
        sha = b.edit(ws, "app.py", "return 19\n", "return 19  # b\n", "b")
        job = b.cli("submit", sha, "--request-id", "b1", "--workspace-id", ws["workspace_id"])[1]
        rc, out = b.cli("run", "--once", "--no-main")
        self.assertIn("admission", out.get("skipped", ""), out)
        self.assertEqual("queued", b.cli("status", job["id"])[1]["status"])
        status = b.service().admission.status()
        self.assertTrue(any(r.get("repo_id") == a_id for r in status.get("reservations", [])),
                        status)
        # The holder dies without releasing: its reservation goes with it.
        os.killpg(holder.pid, signal.SIGKILL)
        holder.wait(timeout=30)
        b.cli("run", "--once", "--no-main")
        self.assertEqual("landed", b.cli("status", job["id"])[1]["status"])


# --------------------------------------------------------------------------------------------
# Author handoff and wake
# --------------------------------------------------------------------------------------------

class Handoffs(AcceptanceCase):

    def test_failed_gate_wakes_the_author_durably_and_retries_a_failed_wake(self):
        p = self.project()
        p.activate()
        ws = p.workspace("wake-me", card="AB12")
        sha = p.edit(ws, "app.py", "return 29\n", "return 0\n", "breaks test_f29")
        calls = []

        def wake(handoff):
            calls.append(handoff)
            if len(calls) == 1:
                raise RuntimeError("pane not reachable yet")

        service = p.service(wake=wake)
        job = service.submit(sha, request_id="wake", workspace_id=ws["workspace_id"], card="AB12")
        service.run_once(main=False)
        self.assertEqual("failed", service.queue.status(job["id"])["status"])
        kinds = [c["kind"] for c in calls]
        self.assertEqual(1, len(kinds), "one failed gate woke the author %d times: %s"
                         % (len(kinds), kinds))
        self.assertTrue(all(c["session"] == "wake-me" for c in calls))
        self.assertIn("test_f29", json.dumps(calls[0]["payload"]) + calls[0]["text"],
                      "the author's handoff does not carry the gate diagnostic")
        pending = service.queue.outbox()
        self.assertEqual(1, len(pending), pending)
        self.assertEqual(1, pending[0]["attempts"])
        self.assertEqual(calls[0]["kind"], pending[0]["kind"])
        # A restarted service (new object, same state) delivers the refused wake once, later.
        again = p.service(wake=wake)
        again.queue.deliver_outbox(again._deliver, now=time.time() + 3600)
        self.assertEqual(len(kinds) + 1, len(calls))
        self.assertEqual([], again.queue.outbox())
        again.queue.deliver_outbox(again._deliver, now=time.time() + 7200)
        self.assertEqual(len(kinds) + 1, len(calls))
        rows = again.handoffs(session="wake-me")
        self.assertEqual(sorted(kinds), sorted(r["kind"] for r in rows))
        thread = (p.repo / ".board/threads/AB12.md").read_text()
        for kind in kinds:
            self.assertEqual(1, thread.count("landq:%s:%s" % (job["id"], kind)))
        self.assertNotIn("kind=question", thread)


# --------------------------------------------------------------------------------------------
# Automatic reconciliation: weighted High tier, high effort, attempt and token limits
# --------------------------------------------------------------------------------------------

HIGH = [{"preset": "guest:claude", "model": "claude-fable-5-1", "rank": 1},
        {"preset": "guest:codex", "model": "gpt-6-astra", "rank": 1}]


class Reconciliation(AcceptanceCase):

    def conflicted(self, **kw):
        p = self.project(reconcile=True, **kw)
        p.activate()
        alice = p.workspace("alice")
        bob = p.workspace("bob", card="AB12")
        a_sha = p.edit(alice, "app.py", "def f20():\n    return 20\n",
                       "def f20():\n    return 20\n\n\ndef alice():\n    return 'a'\n", "alice")
        b_sha = p.edit(bob, "app.py", "def f20():\n    return 20\n",
                       "def f20():\n    return 20\n\n\ndef bob():\n    return 'b'\n", "bob #AB12")
        p.cli("submit", a_sha, "--request-id", "a", "--workspace-id", alice["workspace_id"])
        p.cli("run", "--once", "--no-main")
        return p, bob, b_sha

    def reconciler(self, p, model_call):
        from relay_core import reconcile
        return reconcile.Reconciler(state_root=p.state, tiers=HIGH, guest_check=lambda _g: True,
                                    key_lookup=lambda _p: "", model_call=model_call)

    def test_resolution_is_drawn_from_high_at_high_effort_and_still_gated(self):
        p, bob, b_sha = self.conflicted()
        requests = []

        def model_call(request):
            requests.append(request)
            cwd = Path(request["cwd"])
            text = p.show("main", "app.py").replace(
                "def alice():\n    return 'a'\n",
                "def alice():\n    return 'a'\n\n\ndef bob():\n    return 'b'\n")
            return (json.dumps({"files": [{"path": "app.py", "content": text}],
                                "notes": "kept both"}),
                    {"input_tokens": 900, "output_tokens": 300})

        service = p.service(reconciler=self.reconciler(p, model_call))
        gated = []
        real = service.verifier
        service.verifier = lambda j, c, path: gated.append(c) or real(j, c, path)
        job = service.submit(b_sha, request_id="b", workspace_id=bob["workspace_id"], card="AB12")
        result = service.run_once(main=False)
        self.assertEqual("landed", result["job"]["status"], result)
        self.assertEqual(1, len(requests))
        req = requests[0]
        # A family row stands for each of its signed-in accounts (guest:claude:<account>).
        family = ":".join(req["preset"].split(":")[:2])
        self.assertIn(family, {e["preset"] for e in HIGH})
        from relay_core import reconcile
        wanted = reconcile.with_high_effort({"preset": family,
                                             "model": req["model"]}).get("effort")
        self.assertTrue(req["effort"])
        self.assertEqual(wanted, req["effort"])
        self.assertLessEqual(req["budget_tokens"], 200000)
        receipt = service.queue.receipt(job["id"])
        self.assertIn(receipt["published_sha"], gated, "the resolved candidate was not gated")
        message = git(p.repo, "log", "-1", "--format=%B", receipt["published_sha"])
        self.assertIn("Reconciled-From:", message)
        text = p.show("main", "app.py")
        self.assertIn("def alice", text)
        self.assertIn("def bob", text)
        ledger = service.reconciler.history(job["id"])
        self.assertEqual(1, len(ledger))
        self.assertEqual(req["model"], ledger[0].get("model"))

    def test_bad_answers_stop_after_two_attempts_and_return_to_the_author(self):
        p, bob, b_sha = self.conflicted()
        calls = []

        def model_call(request):
            calls.append(request["attempt"])
            return ("not json at all", {"input_tokens": 100, "output_tokens": 10})

        service = p.service(reconciler=self.reconciler(p, model_call))
        job = service.submit(b_sha, request_id="b", workspace_id=bob["workspace_id"], card="AB12")
        service.run_once(main=False)
        self.assertLessEqual(len(calls), 2)
        self.assertEqual(p.main(), git(p.repo, "rev-parse", "main"))
        self.assertNotEqual("landed", service.queue.status(job["id"])["status"])
        kinds = {h["kind"] for h in service.handoffs(session="bob")}
        self.assertTrue(kinds & {"author_required", "conflict"}, kinds)
        # A resubmission of the same job after a restart gets no further attempts.
        n = len(calls)
        restarted = p.service(reconciler=self.reconciler(p, model_call))
        restarted.queue.status(job["id"])
        restarted.run_once(main=False)
        self.assertEqual(n, len(calls))

    def test_token_budget_refuses_before_any_model_call(self):
        p, bob, b_sha = self.conflicted(tokens_per_case=1000)
        calls = []
        service = p.service(reconciler=self.reconciler(p, lambda r: calls.append(r)))
        job = service.submit(b_sha, request_id="b", workspace_id=bob["workspace_id"], card="AB12")
        service.run_once(main=False)
        self.assertEqual([], calls)
        self.assertNotEqual("landed", service.queue.status(job["id"])["status"])
        handoffs = service.handoffs(session="bob")
        self.assertTrue(handoffs)
        self.assertIn("tokens_per_case", json.dumps(handoffs))



# --------------------------------------------------------------------------------------------
# Runnable main: immutable releases, keep two, lag, restart and an idle daemon
# --------------------------------------------------------------------------------------------

LAUNCHER = """#!/bin/sh
d=$(cd "$(dirname "$0")" && pwd)
if [ "$1" = wait ]; then
    while [ ! -e "$2" ]; do sleep 0.05; done
fi
grep -o '# rel[0-9]*' "$d/app.py" | tr '\\n' ' '
echo
"""

MAIN = """[main]
install = [["sh", "-c", "mkdir -p \\"$0/bin\\" && cp launcher.sh \\"$0/bin/demo\\" && cp app.py \\"$0/bin/app.py\\" && chmod +x \\"$0/bin/demo\\"", "{dest}"]]
executable = "bin/demo"
keep = 2
smoke = [["{executable}"]]
"""


class RunnableMain(AcceptanceCase):

    def test_daemon_installs_complete_releases_keeps_two_and_shows_lag(self):
        p = self.project(main_section=MAIN)
        (p.repo / "launcher.sh").write_text(LAUNCHER)
        (p.repo / "launcher.sh").chmod(0o755)
        p.commit("launcher")
        p.activate()
        daemon = self.spawn(p.cli_argv("run", "--interval", "0.2"), p.env(), cwd=p.root)
        self.wait_for(lambda: p.service().daemon_state().get("running"), what="daemon")

        def land(n):
            ws = p.workspace("rel-%d" % n)
            sha = p.edit(ws, "app.py", "return %d\n" % n, "return %d  # rel%d\n" % (n, n),
                         "rel%d" % n)
            job = p.cli("submit", sha, "--request-id", "rel%d" % n, "--workspace-id",
                        ws["workspace_id"])[1]
            self.wait_for(lambda: p.cli("status", job["id"])[1]["status"] == "landed",
                          what="landing %d" % n)
            self.wait_for(lambda: p.cli("main-status")[1].get("lag_commits") == 0,
                          what="release %d" % n)
            return p.cli("main-status")[1]

        first = land(21)
        self.assertEqual(p.main(), first["installed_sha"])
        out = subprocess.run(p.cli_argv("main-run"), env=p.env(), text=True,
                             capture_output=True, timeout=CLI_TIMEOUT)
        self.assertEqual("# rel21", out.stdout.strip())
        # Release 1, started through main-run, keeps reading its own assets across more
        # updates than `keep`: it is pinned while it runs, and reaped after it exits.
        go = self.root / "go"
        old = self.spawn(p.cli_argv("main-run", "--", "wait", str(go)), p.env())
        self.wait_for(lambda: any(r["sha"] == first["installed_sha"] for r in
                                  p.cli("main-status")[1]["releases"]), what="release 1")
        land(22)
        pinned = land(23)
        shas = [r["sha"] for r in pinned["releases"] if r["complete"]]
        self.assertIn(first["installed_sha"], shas, "a running release was pruned")
        self.assertEqual(3, len(shas), pinned["releases"])
        go.write_text("")
        self.assertEqual("# rel21", old.communicate(timeout=30)[0].strip())
        self.assertEqual(0, old.returncode)
        third = land(24)
        complete = [r for r in third["releases"] if r["complete"]]
        self.assertEqual(2, len(complete), third["releases"])
        self.assertNotIn(first["installed_sha"], [r["sha"] for r in complete])
        self.assertTrue(any(r["current"] and r["sha"] == p.main() for r in complete))
        for release in complete:
            self.assertTrue((Path(release["path"]) / "release.json").exists())
        # A release that fails to install leaves the previous one current and shows the lag.
        ws = p.workspace("broken-install")
        cwd = Path(ws["execution_cwd"])
        (cwd / "launcher.sh").unlink()
        sha = p.commit("drop the launcher", cwd=cwd)
        job = p.cli("submit", sha, "--request-id", "broken", "--workspace-id",
                    ws["workspace_id"])[1]
        self.wait_for(lambda: p.cli("status", job["id"])[1]["status"] == "landed",
                      what="broken landing")
        status = self.wait_for(lambda: (lambda st: st if st.get("error") else None)(
            p.cli("main-status")[1]), what="release error")
        self.assertEqual(third["installed_sha"], status["installed_sha"])
        self.assertEqual(1, status["lag_commits"])
        out = subprocess.run(p.cli_argv("main-run"), env=p.env(), text=True,
                             capture_output=True, timeout=CLI_TIMEOUT)
        self.assertIn("# rel24", out.stdout)
        # Stop the idle daemon; a restart resumes from durable state.
        daemon.send_signal(signal.SIGTERM)
        self.assertEqual(0, daemon.wait(timeout=30))
        self.assertFalse(p.service().daemon_state().get("running"))
        again = self.spawn(p.cli_argv("run", "--interval", "0.2"), p.env(), cwd=p.root)
        self.wait_for(lambda: p.service().daemon_state().get("running"), what="restart")
        self.assertEqual(third["installed_sha"], p.cli("main-status")[1]["installed_sha"])
        again.send_signal(signal.SIGTERM)
        self.assertEqual(0, again.wait(timeout=30))


# --------------------------------------------------------------------------------------------
# Cutover, rollback and the legacy writers
# --------------------------------------------------------------------------------------------

def checkout_state(repo: Path) -> dict:
    files = {}
    for path in sorted(repo.rglob("*")):
        rel = path.relative_to(repo)
        if rel.parts[0] == ".git" or path.is_dir():
            continue
        files[str(rel)] = path.read_bytes()
    return {"index": git(repo, "ls-files", "-s", "--debug"),
            "staged": git(repo, "diff", "--cached", "--binary"),
            "unstaged": git(repo, "diff", "--binary"), "files": files}


class CutoverAndRollback(AcceptanceCase):

    def dirty_human_checkout(self, p):
        (p.repo / "app.py").write_text(APP.replace("return 1\n", "return 1  # staged\n"))
        git(p.repo, "add", "app.py")
        (p.repo / "test_app.py").write_text(TEST_APP + "\n# unstaged\n")
        (p.repo / "notes.txt").write_text("untracked\n")
        (p.repo / "build").mkdir()
        (p.repo / "build/out.bin").write_bytes(b"ignored")

    def comparable(self, state):
        # `--debug` carries stat data git may legitimately refresh; the rest must be identical.
        state = dict(state)
        state["index"] = "\n".join(line for line in state["index"].splitlines()
                                   if not line.startswith(("  ctime", "  mtime", "  dev",
                                                           "  uid", "  size", "  flags")))
        return state

    def test_activation_and_rollback_keep_a_dirty_checkout_and_pending_work(self):
        p = self.project()
        self.dirty_human_checkout(p)
        before = self.comparable(checkout_state(p.repo))
        dry = p.cli("activate", "--dry-run")[1]
        self.assertTrue(dry["can_activate"], dry["blockers"])
        self.assertEqual(before, self.comparable(checkout_state(p.repo)))
        p.activate()
        self.assertEqual("refs/heads/human", git(p.repo, "symbolic-ref", "HEAD"))
        self.assertEqual(before, self.comparable(checkout_state(p.repo)))
        ws = p.workspace("pending-author")
        sha = p.edit(ws, "app.py", "return 26\n", "return 26  # pending\n", "pending")
        # Pending work, recorded before the pause; the paused daemon publishes nothing.
        job = p.cli("submit", sha, "--request-id", "pending", "--workspace-id",
                    ws["workspace_id"])[1]
        p.cli("pause")
        daemon = self.spawn(p.cli_argv("run", "--no-main", "--interval", "0.2"), p.env())
        self.wait_for(lambda: p.service().daemon_state().get("running"), what="daemon")
        time.sleep(1.0)
        self.assertEqual("queued", p.cli("status", job["id"])[1]["status"])
        self.assertEqual(p.base, p.main())
        rc, refused = p.cli("rollback", codes=(7,))
        self.assertIn("still running", refused["error"])
        self.assertEqual("paused", p.service().mode())
        daemon.send_signal(signal.SIGTERM)
        daemon.wait(timeout=60)
        rc, rolled = p.cli("rollback")
        self.assertTrue(rolled["rolled_back"])
        self.assertEqual("legacy", p.service().mode())
        self.assertEqual("refs/heads/main", git(p.repo, "symbolic-ref", "HEAD"))
        self.assertEqual(before, self.comparable(checkout_state(p.repo)))
        # Nothing pending was dropped: the job, its retained ref and the workspace remain.
        self.assertEqual("queued", p.cli("status", job["id"])[1]["status"])
        self.assertEqual(sha, git(p.repo, "rev-parse", "refs/landq/jobs/%s/submitted" % job["id"]))
        self.assertEqual(sha, git(p.repo, "rev-parse", "refs/heads/" + ws["branch"]))
        self.assertTrue(Path(ws["execution_cwd"]).is_dir())
        # Queue-mode verbs now refuse instead of publishing.
        p.cli("workspace", "create", "late", codes=(2,))
        # Re-activation resumes and publishes the retained job.
        p.cli("activate")
        p.cli("run", "--once", "--no-main")
        self.assertEqual("landed", p.cli("status", job["id"])[1]["status"])

    def test_rollback_fails_closed_when_the_checkout_would_be_overwritten(self):
        p = self.project()
        p.activate()
        ws = p.workspace("lander")
        sha = p.edit(ws, "app.py", "return 1\n", "return 1  # landed\n", "landed")
        p.cli("submit", sha, "--request-id", "l", "--workspace-id", ws["workspace_id"])
        p.cli("run", "--once", "--no-main")
        # The human checkout edits the same file, staged, without committing it.
        (p.repo / "app.py").write_text(APP.replace("return 1\n", "return 1  # human\n"))
        git(p.repo, "add", "app.py")
        before = self.comparable(checkout_state(p.repo))
        rc, out = p.cli("rollback", codes=(2,))
        self.assertIn("stays paused", out["error"])
        self.assertEqual("paused", p.service().mode())
        self.assertEqual(before, self.comparable(checkout_state(p.repo)))
        self.assertEqual("refs/heads/human", git(p.repo, "symbolic-ref", "HEAD"))

    def test_pause_drains_the_tick_in_flight_and_nothing_publishes_after_it(self):
        p = self.project()
        p.activate()
        ws = p.workspace("in-flight")
        sha = p.edit(ws, "app.py", "return 28\n", "return 28  # drained\n", "drained")
        service = p.service()
        job = service.submit(sha, request_id="drained", workspace_id=ws["workspace_id"])
        started, release = threading.Event(), threading.Event()
        real = service.verifier

        def verifier(job_, candidate, path):
            started.set()
            release.wait(60)
            return real(job_, candidate, path)

        service.verifier = verifier
        tick = threading.Thread(target=service.run_once, kwargs={"main": False})
        tick.start()
        self.assertTrue(started.wait(60))
        p.cli("pause", codes=(7,))                      # no wait: refused while the tick runs
        pauser = self.spawn(p.cli_argv("pause", "--wait-seconds", "60"), p.env())
        time.sleep(0.5)
        self.assertIsNone(pauser.poll(), "pause returned while a tick was publishing")
        release.set()
        tick.join(60)
        self.assertEqual(0, pauser.wait(timeout=60))
        self.assertEqual("landed", service.queue.status(job["id"])["status"])
        after_pause = p.main()
        rc, out = p.cli("run", "--once", "--no-main")
        self.assertIn("paused", out.get("skipped", ""), out)
        self.assertEqual(after_pause, p.main())

    def test_paused_is_a_hold_that_refuses_new_work_and_keeps_old_work(self):
        p = self.project()
        p.activate()
        kept_ws = p.workspace("kept")
        kept_sha = p.edit(kept_ws, "app.py", "return 27\n",
                          "return 27  # kept\n", "kept")
        kept = p.cli("submit", kept_sha, "--request-id", "kept", "--workspace-id",
                     kept_ws["workspace_id"])[1]
        p.cli("pause")
        # Every door for new work refuses with the reason: allocation, both submit paths, Board.
        rc, out = p.cli("workspace", "create", "while-paused", codes=(2,))
        self.assertIn("paused", out["error"])
        later = git(p.repo, "commit-tree", git(p.repo, "rev-parse", kept_sha + "^{tree}"),
                    "-p", kept_sha, "-m", "submitted while paused")
        rc, out = p.cli("submit", later, "--request-id", "paused-cli", codes=(2,))
        self.assertIn("paused", out["error"])
        with self.assertRaises(integration_service.ServiceError):
            p.service().submit(later, request_id="paused-api")
        p.card_path.write_text(p.card_path.read_text().replace("executing", "planned"))
        rc, out = p.cli("board-submit", ".board/features/2026-09-26-acceptance.md",
                        "--session", "s", codes=(2,))
        self.assertIn("paused", out["error"])
        self.assertEqual(["kept"], [j["request_id"] for j in p.cli("status")[1]])
        self.assertEqual(p.base, p.main())
        # What was recorded is kept, and activate resumes it.
        self.assertEqual("queued", p.cli("status", kept["id"])[1]["status"])
        self.assertTrue(Path(kept_ws["execution_cwd"]).is_dir())
        p.cli("activate")
        p.cli("run", "--once", "--no-main")
        self.assertEqual("landed", p.cli("status", kept["id"])[1]["status"])

    def land_py(self, p, *args, codes=(0,)):
        proc = subprocess.run([PY, str(ROOT / "scripts" / "land.py"), *args], cwd=p.repo,
                              env=p.env(), text=True, capture_output=True, timeout=CLI_TIMEOUT)
        if proc.returncode not in codes:
            raise AssertionError("land.py %s exited %d: %s%s" % (" ".join(args), proc.returncode,
                                                               proc.stdout, proc.stderr))
        return proc.returncode, proc.stdout + proc.stderr

    def test_legacy_writers_refuse_in_queue_mode_and_on_a_malformed_marker(self):
        p = self.project()
        (p.repo / "README").write_text("second commit, so repair has a parent\n")
        p.commit("readme")
        self.land_py(p, "begin", "legacy", "app.py")
        (p.repo / "app.py").write_text(APP.replace("return 2\n", "return 2  # legacy\n"))
        p.activate()
        before = p.main()
        rc, out = self.land_py(p, "commit", "legacy", "-m", "legacy commit", codes=(2,))
        self.assertIn("relay-land", out)
        rc, out = self.land_py(p, "repair", before, "--paths", "app.py", codes=(2,))
        self.assertIn("relay-land", out)
        self.assertEqual(before, p.main())
        # The shared index refuses a plain commit (the hook), even for the owner's paths.
        proc = subprocess.run(["git", "commit", "-q", "-m", "plain"], cwd=p.repo, env=p.env(),
                              text=True, capture_output=True, timeout=60)
        self.assertNotEqual(0, proc.returncode)
        self.assertEqual(before, p.main())
        # A malformed marker: nothing publishes, from either side.
        marker = Path(git(p.repo, "rev-parse", "--path-format=absolute",
                          "--git-common-dir")) / "relay-publication.json"
        for bad in ('{"mode": "queue"', '{"mode": "sideways"}', "[]"):
            with self.subTest(marker=bad):
                marker.write_text(bad)
                rc, out = self.land_py(p, "commit", "legacy", "-m", "legacy commit", codes=(2,))
                self.assertIn("marker", out)
                self.assertNotEqual("queue", p.service().mode())
                ws_refused = p.cli("workspace", "create", "while-broken", codes=(2,))
                self.assertEqual(before, p.main())
        # The legacy writer's uncommitted edit is still in the checkout.
        self.assertIn("# legacy", (p.repo / "app.py").read_text())


# --------------------------------------------------------------------------------------------
# The installed layout
# --------------------------------------------------------------------------------------------

class InstalledLayout(AcceptanceCase):

    def test_installed_commands_run_a_full_landing_outside_the_source_tree(self):
        prefix = self.root / "prefix"
        share = prefix / "share" / "relay"
        shutil.copytree(BACKEND, share / "backend",
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "tests"))
        (share / "scripts").mkdir(parents=True)
        for name in ("relay-land", "relay-tree"):
            shutil.copy2(ROOT / "scripts" / name, share / "scripts" / name)
        p = self.project()
        p.activate()
        ws = p.workspace("installed")
        sha = p.edit(ws, "app.py", "return 27\n", "return 27  # installed\n", "installed")
        p.cli("submit", sha, "--request-id", "inst", "--workspace-id", ws["workspace_id"],
              scripts=share / "scripts")
        p.cli("run", "--once", "--no-main", scripts=share / "scripts")
        self.assertEqual(sha, p.main())
        proc = subprocess.run([PY, str(share / "scripts" / "relay-tree"), "list", "--repo",
                               str(p.repo), "--state-root", str(p.state)], cwd=self.root,
                              env=p.env(), text=True, capture_output=True, timeout=60)
        self.assertEqual(0, proc.returncode, proc.stderr)
        self.assertIn(ws["workspace_id"], proc.stdout)



# --------------------------------------------------------------------------------------------
# Native worker and guest startup in a temporary activated repository
# --------------------------------------------------------------------------------------------

class Worker:
    """The real `backend/worker.py`, driven over its stdin/stdout protocol."""

    def __init__(self, case, env, cwd):
        import queue as queue_mod
        self.proc = case.spawn([PY, str(BACKEND / "worker.py")], env, cwd=str(cwd),
                               stdin=subprocess.PIPE)
        self.events = queue_mod.Queue()
        self.seen = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            try:
                self.events.put(json.loads(line))
            except ValueError:
                pass

    def send(self, message):
        self.proc.stdin.write(json.dumps(message) + "\n")
        self.proc.stdin.flush()

    def wait(self, predicate, timeout=30.0):
        import queue as queue_mod
        for event in self.seen:
            if predicate(event):
                return event
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                event = self.events.get(timeout=0.2)
            except queue_mod.Empty:
                continue
            self.seen.append(event)
            if predicate(event):
                return event
        raise AssertionError("worker event not seen; got %s"
                             % [e.get("event") for e in self.seen][-30:])

    def stop(self):
        try:
            self.send({"type": "shutdown"})
        except (BrokenPipeError, ValueError):
            pass
        self.proc.wait(timeout=30)


BOARD_TRIGGER = "C1-BOARD-WRITE"
BOARD_MARK = "C1 pane board write reaches the queue"


class StubModel:
    """An OpenAI-compatible streaming endpoint that answers every turn with one short line and
    records what the author was told. Nothing leaves the machine."""

    def __init__(self):
        from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
        prompts = self.prompts = []

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)))
                messages = body.get("messages", [])
                prompts.append(json.dumps(messages)[-4000:])
                usage = {"prompt_tokens": 10, "completion_tokens": 2, "total_tokens": 12}
                last = messages[-1] if messages else {}
                if last.get("role") == "user" and BOARD_TRIGGER in json.dumps(last.get("content")):
                    # The turn's one tool call: a Board comment on the project's card.
                    args = json.dumps({"id": "AB12", "kind": "progress", "text": BOARD_MARK})
                    chunks = [{"choices": [{"index": 0, "delta": {"role": "assistant",
                               "tool_calls": [{"index": 0, "id": "call_c1", "type": "function",
                                               "function": {"name": "board_comment",
                                                            "arguments": args}}]}}]},
                              {"choices": [{"index": 0, "delta": {},
                                            "finish_reason": "tool_calls"}], "usage": usage}]
                else:
                    chunks = [{"choices": [{"index": 0, "delta": {"role": "assistant",
                                                                  "content": "Noted."}}]},
                              {"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}],
                               "usage": usage}]
                data = "".join("data: %s\n\n" % json.dumps(c) for c in chunks) + "data: [DONE]\n\n"
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Content-Length", str(len(data.encode())))
                self.end_headers()
                self.wfile.write(data.encode())

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = "http://127.0.0.1:%d/v1" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class NativeAndGuestPanes(AcceptanceCase):
    """Startup is real (worker process, guest launch payload, workspace leases, the publisher);
    the model turns are not: a fake agent in each workspace does what the injected
    instructions say — edit, commit, `relay-land submit HEAD --request-id ID`."""

    def setUp(self):
        super().setUp()
        self.p = self.project()
        self.p.activate()
        bindir = self.root / "bin"
        bindir.mkdir()
        wrapper = bindir / "relay-land"
        wrapper.write_text("#!/bin/sh\nexec %s %s \"$@\"\n" % (PY, ROOT / "scripts/relay-land"))
        wrapper.chmod(0o755)
        self.home = self.root / "home"
        self.home.mkdir()
        self.env = self.p.env({"HOME": str(self.home), "RELAY_KEYRING": "off",
                               "PATH": "%s:%s" % (bindir, os.environ.get("PATH", ""))})
        self.model = StubModel()
        self.addCleanup(self.model.close)

    def start_worker(self, token):
        worker = Worker(self, {**self.env, "RELAY_SESSION_TOKEN": token}, self.p.repo)
        worker.send({"type": "configure", "workspace": str(self.p.repo), "pane_token": token,
                     "api_key": "k", "base_url": self.model.url, "model": "test/model"})
        return worker

    def lease(self, session):
        rows = [w for w in self.p.service().trees.list()
                if w["session"] == session and w["status"] == "active"]
        self.assertEqual(1, len(rows), rows)
        return self.p.service().tree_status(rows[0]["id"])

    def guest_launch(self, token):
        runtime = self.root / ("rt-" + token)
        runtime.mkdir()
        proc = subprocess.run([PY, "-m", "relay_core.guest_launch", "claude", "--runtime-dir",
                               str(runtime), "--cwd", str(self.p.repo), "--session", token,
                               "--home", str(self.home)], cwd=str(BACKEND), env=self.env,
                              text=True, capture_output=True, timeout=60)
        self.assertEqual(0, proc.returncode, proc.stdout + proc.stderr)
        return json.loads(proc.stdout)

    def agent_submits(self, cwd, env, old, new, request_id):
        cwd = Path(cwd)
        (cwd / "app.py").write_text((cwd / "app.py").read_text().replace(old, new))
        git(cwd, "commit", "-qam", request_id)
        proc = subprocess.run(["relay-land", "submit", "HEAD", "--request-id", request_id],
                              cwd=str(cwd), env=env, text=True, capture_output=True, timeout=60)
        self.assertEqual(0, proc.returncode, proc.stdout + proc.stderr)
        return json.loads(proc.stdout)

    def test_native_and_guest_panes_land_independently_and_the_author_is_woken(self):
        p = self.p
        native = self.start_worker("native-tok")
        native.wait(lambda e: e.get("event") == "configured")
        native.wait(lambda e: e.get("event") == "queue_status" and e.get("mode") == "queue")
        n_ws = self.lease("native-tok")
        guest = self.guest_launch("guest-tok")
        g_ws = self.lease("guest-tok")
        self.assertEqual(g_ws["execution_cwd"], guest["execution_cwd"])
        self.assertNotEqual(n_ws["execution_cwd"], g_ws["execution_cwd"])
        board = str(p.repo / ".board")
        self.assertEqual(board, guest["env"]["RELAY_BOARD_ROOT"])
        self.assertEqual(str(p.repo), guest["env"]["RELAY_PROJECT_ROOT"])
        self.assertEqual(g_ws["workspace_id"], guest["env"]["RELAY_WORKSPACE_ID"])
        note = Path(guest["memory_file"]).read_text()
        self.assertIn("relay-land submit HEAD", note)
        self.assertIn("Do not use scripts/land.py", note)
        for ws in (n_ws, g_ws):
            self.assertFalse((Path(ws["execution_cwd"]) / ".board").exists())
        # Each agent edits the same file in its own workspace and submits once.
        n_env = {**self.env, "RELAY_SESSION_TOKEN": "native-tok", "RELAY_PROJECT_ROOT": str(p.repo),
                 "RELAY_BOARD_ROOT": board, "RELAY_WORKSPACE_ID": n_ws["workspace_id"]}
        g_env = {**self.env, **guest["env"], "RELAY_SESSION_TOKEN": "guest-tok"}
        n_job = self.agent_submits(n_ws["execution_cwd"], n_env, "return 2\n",
                                   "return 2  # native\n", "native-1")
        g_job = self.agent_submits(g_ws["execution_cwd"], g_env, "return 25\n",
                                   "return 25  # guest\n", "guest-1")
        self.assertEqual(n_ws["workspace_id"], n_job["workspace_id"])
        self.assertEqual(g_ws["workspace_id"], g_job["workspace_id"])
        p.cli("run", "--once", "--no-main")
        p.cli("run", "--once", "--no-main")
        text = p.show(p.main(), "app.py")
        self.assertIn("# native", text)
        self.assertIn("# guest", text)
        for job in (n_job, g_job):
            receipt = p.cli("receipt", job["id"])[1]
            self.assertTrue(receipt["verified"])
        self.assertNotIn("# guest", (Path(n_ws["execution_cwd"]) / "app.py").read_text())
        self.assertNotIn("# native", (Path(g_ws["execution_cwd"]) / "app.py").read_text())
        # The live worker reports the landing in its queue status.
        native.wait(lambda e: e.get("event") == "queue_status" and any(
            j["id"] == n_job["id"] and j["status"] == "landed" for j in e.get("jobs", [])))
        # A failed gate goes back to the native author through its worker, durably.
        bad = self.agent_submits(n_ws["execution_cwd"], n_env, "return 0\n", "return -1\n",
                                 "native-bad")
        p.cli("run", "--once", "--no-main", codes=(5,))
        # The author's turn reads the diagnostic and ends; only then is the row acknowledged.
        native.wait(lambda e: e.get("event") == "handoff_delivered"
                    and e.get("job_id") == bad["id"], timeout=60)
        time.sleep(4)          # one more poll interval: a second handoff would be queued by now
        queued = [e for e in native.seen + list(native.events.queue)
                  if e.get("event") == "handoff_queued" and e.get("job_id") == bad["id"]]
        self.assertEqual(1, len(queued), "one failed gate started %d author turns" % len(queued))
        self.assertTrue(any(bad["id"] in prompt for prompt in self.model.prompts),
                        "the author's model never saw the handoff")
        self.wait_for(lambda: not [h for h in p.service().handoffs(pending_only=True)
                                   if h["job_id"] == bad["id"]], what="durable acknowledgement")
        native.stop()
        # A restarted native pane resumes on the same workspace, with its work.
        again = self.start_worker("native-tok")
        again.wait(lambda e: e.get("event") == "configured")
        self.assertEqual(n_ws["workspace_id"], self.lease("native-tok")["workspace_id"])
        again.stop()

    def test_a_native_panes_board_write_in_a_second_project_is_published(self):
        p = self.p
        self.assertFalse((p.repo / "scripts" / "land.py").exists())   # not the Relay repo
        native = self.start_worker("board-tok")
        native.wait(lambda e: e.get("event") == "configured")
        native.send({"type": "ask", "text": "%s: note progress on #AB12" % BOARD_TRIGGER,
                     "id": "ask-board"})
        native.wait(lambda e: e.get("event") == "agent_finished", timeout=60)
        thread = p.repo / ".board/threads/AB12.md"
        self.assertIn(BOARD_MARK, thread.read_text(), "the write did not reach the canonical Board")
        lease = self.lease("board-tok")
        self.assertFalse((Path(lease["execution_cwd"]) / ".board").exists())

        def metadata_job():
            jobs = p.cli("status")[1]
            return next((j for j in jobs if j["kind"] == "metadata"), None)

        job = self.wait_for(metadata_job, timeout=60, what="the turn's Board write in the queue")
        p.cli("run", "--once", "--no-main")
        self.assertEqual("landed", p.cli("status", job["id"])[1]["status"])
        self.assertIn(BOARD_MARK, p.show(p.main(), ".board/threads/AB12.md"))
        native.stop()

    def test_a_board_write_is_recorded_before_the_turn_ends_and_survives_the_workers_death(self):
        # The contract: a pane's Board write is durable in the queue before its turn is
        # reported finished, so a worker that is gone the instant after (no shutdown, no
        # background thread left to run) has still handed the write to the publisher.
        p = self.p
        self.assertFalse((p.repo / "scripts" / "land.py").exists())   # queue route, in process
        native = self.start_worker("dying-tok")
        native.wait(lambda e: e.get("event") == "configured")
        native.send({"type": "ask", "text": "%s: note progress on #AB12" % BOARD_TRIGGER,
                     "id": "ask-die"})
        native.wait(lambda e: e.get("event") == "agent_finished", timeout=60)
        # SIGKILL the moment the turn is finished: nothing the worker meant to do later runs.
        native.proc.kill()
        native.proc.wait(timeout=30)
        self.assertIn(BOARD_MARK, (p.repo / ".board/threads/AB12.md").read_text())
        jobs = [j for j in p.cli("status")[1] if j["kind"] == "metadata"]
        self.assertEqual(1, len(jobs), "the turn ended before its Board write was queued: %s"
                         % [(j["kind"], j["status"]) for j in p.cli("status")[1]])
        job = jobs[0]
        self.assertEqual("queued", job["status"])
        # The snapshot commit is retained under the queue's own refs and holds the write.
        self.assertEqual(job["submitted_sha"],
                         git(p.repo, "rev-parse", "refs/landq/jobs/%s/submitted" % job["id"]))
        self.assertIn(BOARD_MARK, p.show(job["submitted_sha"], ".board/threads/AB12.md"))
        self.assertEqual(p.main(), git(p.repo, "rev-parse", job["submitted_sha"] + "^"))
        # No worker is alive; the publisher alone lands it.
        p.cli("run", "--once", "--no-main")
        self.assertEqual("landed", p.cli("status", job["id"])[1]["status"])
        self.assertIn(BOARD_MARK, p.show(p.main(), ".board/threads/AB12.md"))

    def test_a_paused_project_refuses_development_launch_instead_of_sharing_the_checkout(self):
        self.p.cli("pause")
        worker = self.start_worker("paused-tok")
        event = worker.wait(lambda e: e.get("event") in ("configured", "error"))
        self.assertEqual("error", event["event"], event)
        self.assertEqual([], [w for w in self.p.service().trees.list()
                              if w["session"] == "paused-tok"])
        worker.stop()
        runtime = self.root / "rt-paused"
        runtime.mkdir()
        proc = subprocess.run([PY, "-m", "relay_core.guest_launch", "codex", "--runtime-dir",
                               str(runtime), "--cwd", str(self.p.repo), "--session", "g-paused",
                               "--home", str(self.home)], cwd=str(BACKEND), env=self.env,
                              text=True, capture_output=True, timeout=60)
        # Refused with the launcher's JSON contract, not a traceback; no workspace was
        # allocated and no shared-checkout command line was printed.
        self.assertNotEqual(0, proc.returncode)
        self.assertNotIn("Traceback", proc.stderr)
        answer = json.loads(proc.stdout)
        self.assertFalse(answer["ok"])
        self.assertIn("paused", answer["error"])
        self.assertNotIn("command", answer)
        self.assertEqual([], [w for w in self.p.service().trees.list()
                              if w["session"] == "g-paused"])



# --------------------------------------------------------------------------------------------
# The real GUI binary (opt-in: RELAY_C1_RELAY_BINARY names a built `relay`)
# --------------------------------------------------------------------------------------------

GUI_BINARY = os.environ.get("RELAY_C1_RELAY_BINARY", "")


@unittest.skipUnless(GUI_BINARY and shutil.which("Xvfb") and shutil.which("xdotool")
                     and shutil.which("tmux"), "set RELAY_C1_RELAY_BINARY; needs Xvfb, xdotool, tmux")
class LiveGui(AcceptanceCase):
    """Two panes of one window on a queue-mode project, under Xvfb with an isolated profile and
    a private tmux socket (the local holder path: no systemd user session here)."""

    def setUp(self):
        super().setUp()
        self.p = self.project()
        self.p.activate()
        display = next(n for n in range(300, 400) if not Path("/tmp/.X11-unix/X%d" % n).exists())
        self.display = ":%d" % display
        xvfb = self.spawn(["Xvfb", self.display, "-screen", "0", "1400x900x24", "-nolisten", "tcp"],
                          _scrubbed())
        self.wait_for(lambda: Path("/tmp/.X11-unix/X%d" % display).exists(), what="Xvfb")
        self.tmux = Path(tempfile.mkdtemp(prefix="c1t", dir="/tmp"))   # socket path < 108 bytes
        self.addCleanup(shutil.rmtree, self.tmux, True)
        self.addCleanup(lambda: subprocess.run(["tmux", "-S", str(self.socket()), "kill-server"],
                                               capture_output=True))
        runtime = self.root / "run"
        runtime.mkdir(mode=0o700)
        for name in ("home", "data"):
            (self.root / name).mkdir()
        self.env = self.p.env({"DISPLAY": self.display, "HOME": str(self.root / "home"),
                               "XDG_DATA_HOME": str(self.root / "data"),
                               "XDG_RUNTIME_DIR": str(runtime), "TMUX_TMPDIR": str(self.tmux),
                               "RELAY_DATA_DIR": str(ROOT), "RELAY_KEYRING": "off",
                               "QT_QPA_PLATFORM": "xcb"})
        self.addCleanup(xvfb.kill)

    def launch(self, *extra, restore=False):
        # `-w` opens a window on that directory; a plain start restores the saved layout.
        where = [] if restore else ["-w", str(self.p.repo)]
        self.relay = self.spawn([GUI_BINARY, *extra, *where], self.env, cwd=str(self.p.repo))
        return self.relay

    def quit(self):
        self.relay.send_signal(signal.SIGTERM)
        self.assertIsNotNone(self.relay.wait(timeout=60))

    def socket(self):
        return self.tmux / ("tmux-%d" % os.getuid()) / "relay"

    def pane_leases(self):
        return {w["session"]: w for w in self.p.service().trees.list(include_removed=True)
                if len(w["session"]) == 36 and w["session"].count("-") == 4}

    def shell_cwd(self, session):
        out = subprocess.run(["tmux", "-S", str(self.socket()), "list-panes", "-a", "-F",
                              "#{session_name} #{pane_pid}"], capture_output=True, text=True).stdout
        for line in out.splitlines():
            name, _, pid = line.partition(" ")
            if name == "relay-" + session[:8]:
                return Path(os.readlink("/proc/%s/cwd" % pid)).resolve()
        return None

    def key(self, keys):
        env = {**_scrubbed(), "DISPLAY": self.display}
        window = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "Relay"],
                                env=env, capture_output=True, text=True).stdout.split()
        self.assertTrue(window, "no Relay window")
        subprocess.run(["xdotool", "windowfocus", "--sync", window[0]], env=env, timeout=30)
        subprocess.run(["xdotool", "key", keys], env=env, timeout=30)

    def test_each_pane_has_its_own_lease_its_shell_stays_in_the_checkout_and_leases_end(self):
        self.launch("--fresh")
        first = self.wait_for(lambda: list(self.pane_leases()), timeout=60, what="first pane")
        one = first[0]
        self.wait_for(lambda: self.shell_cwd(one), timeout=60, what="first shell")
        self.key("ctrl+t")
        two = self.wait_for(lambda: [t for t in self.pane_leases() if t != one],
                            timeout=60, what="second pane")[0]
        self.wait_for(lambda: self.shell_cwd(two), timeout=60, what="second shell")
        leases = self.pane_leases()
        # The lease is the agent's; the user's shell stays in the real checkout, where the
        # Board is (owner, 2026-09-26: a shell moved into a tree is not a terminal).
        for token in (one, two):
            self.assertEqual(self.p.repo.resolve(), self.shell_cwd(token),
                             "pane %s's shell left the checkout" % token[:8])
        self.assertNotEqual(leases[one]["path"], leases[two]["path"])
        # Closing the second pane ends its lease; quitting ends the first one's.
        self.key("ctrl+w")
        self.wait_for(lambda: self.pane_leases()[two]["status"] != "active", timeout=30,
                      what="the closed pane's lease to end")
        self.quit()
        self.wait_for(lambda: self.pane_leases()[one]["status"] != "active", timeout=30,
                      what="the quit pane's lease to end")
        for token in (one, two):
            self.assertIn(self.pane_leases()[token]["status"], ("released", "retained"))

    def test_the_shell_starts_when_no_workspace_can_be_had(self):
        # A full quota refused every new pane and the terminal never came up (2026-09-26).
        # Paused publication refuses the same way: the shell must still start, in the checkout.
        self.p.cli("pause")
        self.launch("--fresh")
        shells = lambda: [line for line in subprocess.run(
            ["tmux", "-S", str(self.socket()), "list-panes", "-a", "-F", "#{pane_pid}"],
            capture_output=True, text=True).stdout.split() if line]
        pids = self.wait_for(shells, timeout=60, what="a shell despite the refused workspace")
        self.assertEqual(self.p.repo.resolve(), Path(os.readlink("/proc/%s/cwd" % pids[0])).resolve())
        self.assertEqual(self.pane_leases(), {}, "no lease was taken while paused")
        self.quit()

    def test_a_restored_pane_takes_back_its_own_workspace_with_its_work(self):
        self.launch()
        token = self.wait_for(lambda: list(self.pane_leases()), timeout=60, what="pane")[0]
        self.wait_for(lambda: self.shell_cwd(token), timeout=60, what="shell")
        lease = self.pane_leases()[token]
        tree = Path(lease["path"])
        (tree / "app.py").write_text((tree / "app.py").read_text() + "# committed, unlanded\n")
        git(tree, "commit", "-qam", "unlanded work")
        tip = git(tree, "rev-parse", "HEAD")
        (tree / "scratch.txt").write_text("dirty, uncommitted\n")
        self.quit()
        self.wait_for(lambda: self.pane_leases()[token]["status"] != "active", timeout=30,
                      what="the lease to end at quit")
        self.assertEqual("retained", self.pane_leases()[token]["status"])
        # The restored layout brings the pane back on the same token and the same tree.
        before = set(self.pane_leases())
        self.launch(restore=True)
        self.wait_for(lambda: self.pane_leases()[token]["status"] == "active", timeout=60,
                      what="the restored pane to reacquire its tree")
        self.wait_for(lambda: self.shell_cwd(token), timeout=60, what="restored shell")
        self.assertEqual(self.p.repo.resolve(), self.shell_cwd(token))
        self.assertEqual(lease["path"], self.pane_leases()[token]["path"])
        self.assertEqual(tip, git(tree, "rev-parse", "HEAD"))
        self.assertEqual("dirty, uncommitted\n", (tree / "scratch.txt").read_text())
        self.assertEqual(before, set(self.pane_leases()), "the restored pane took a new tree")
        self.quit()


class ProtocolDocument(unittest.TestCase):

    def test_protocol_sections_have_unique_numbers(self):
        import re
        from collections import Counter
        text = (ROOT / "docs" / "AGENT-SESSIONS-PROTOCOL.md").read_text()
        numbers = Counter(re.findall(r"^## (\d+)\. ", text, re.M))
        self.assertEqual({}, {n: c for n, c in numbers.items() if c > 1},
                         "two protocol sections share a number")
        self.assertIn("workspace_context prepare", text)


if __name__ == "__main__":
    unittest.main()
