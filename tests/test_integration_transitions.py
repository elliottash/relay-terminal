# SPDX-License-Identifier: AGPL-3.0-or-later
"""Cutover, pause, rollback, crash windows and the daemon of relay_core.integration_service
(card #AMQQ). Real temporary repositories with pending work and dirty checkouts; never the
actual repository, never `git reset` on anything a person edits."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import integration_service as S  # noqa: E402
from relay_core import integration_slots, landq, trees  # noqa: E402

PY = sys.executable
MB = 1 << 20


def run_git(cwd, *args, check=True, env=None):
    environ = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    environ.update({"GIT_AUTHOR_NAME": "Test Author", "GIT_AUTHOR_EMAIL": "author@test.invalid",
                    "GIT_COMMITTER_NAME": "Test Author",
                    "GIT_COMMITTER_EMAIL": "author@test.invalid",
                    "RELAY_ALLOW_SHARED_COMMIT": "1"})
    if env:
        environ.update(env)
    proc = subprocess.run(["git", *args], cwd=str(cwd), env=environ, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
    if check and proc.returncode != 0:
        raise AssertionError("git %s failed: %s" % (" ".join(args), proc.stderr))
    return proc


def git_out(cwd, *args, **kw):
    return run_git(cwd, *args, **kw).stdout.strip()


GATE = ('version = 1\n[project]\ntarget = "main"\n[workspace]\nexclude = [".board"]\n'
        '[verification]\ncommands = [["%s", "-c", "import pathlib; '
        'assert pathlib.Path(\'shared.txt\').exists()"]]\n'
        '[resources]\nmemory_bytes = %d\ndisk_bytes = %d\ncpus = 1\n'
        '[main]\ninstall = [["sh", "-c", "mkdir -p {dest}/bin && cp {source}/app.sh {dest}/bin/app '
        '&& chmod +x {dest}/bin/app"]]\nexecutable = "bin/app"\n' % (PY, 10 * MB, 10 * MB))


class TransitionCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="relay-b1-transition-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = self.root / "state"
        self.cache = self.root / "cache"
        self.repo = self.root / "project"
        self.repo.mkdir()
        run_git(self.repo, "init", "-q", "-b", "main")
        run_git(self.repo, "config", "user.name", "Test Author")
        run_git(self.repo, "config", "user.email", "author@test.invalid")
        (self.repo / "shared.txt").write_text("one\ntwo\nthree\n")
        (self.repo / "app.sh").write_text("#!/bin/sh\necho ok\n")
        (self.repo / ".relay").mkdir()
        (self.repo / ".relay" / "project.toml").write_text(GATE)
        self.board = self.repo / ".board"
        (self.board / "threads").mkdir(parents=True)
        (self.board / "features").mkdir()
        (self.board / "board.yaml").write_text("folder: .board\n")
        run_git(self.repo, "add", "-A")
        run_git(self.repo, "commit", "-q", "-m", "baseline")
        self.base = git_out(self.repo, "rev-parse", "HEAD")
        env = {"XDG_CONFIG_HOME": str(self.root / "config"), "XDG_STATE_HOME": str(self.state),
               "XDG_CACHE_HOME": str(self.cache), "RELAY_KEYRING": "off",
               "RELAY_LAND_ROOT": str(self.root / "land")}
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)

    def admission(self):
        empty = self.root / "empty"
        (empty / "cgroup").mkdir(parents=True, exist_ok=True)
        (empty / "proc").mkdir(parents=True, exist_ok=True)
        return integration_slots.HostAdmission(
            state_root=self.state, cgroup_root=empty / "cgroup", proc_root=empty / "proc",
            disk_path=self.root, limits={"cpus": 4.0, "memory_bytes": 100 * MB, "disk_bytes": 100 * MB})

    def service(self, **kw):
        kw.setdefault("admission", self.admission())
        return S.IntegrationService(self.repo, state_root=self.state, cache_root=self.cache,
                                    land_root=self.root / "land", **kw)

    def author(self, service, session, text):
        ws = service.allocate_workspace(session)
        path = Path(ws["execution_cwd"])
        (path / "shared.txt").write_text(text)
        run_git(path, "commit", "-q", "-am", session)
        return ws, path, git_out(path, "rev-parse", "HEAD")

    def tip(self):
        return git_out(self.repo, "rev-parse", "refs/heads/main")

    def head(self):
        return git_out(self.repo, "symbolic-ref", "HEAD")


class CutoverTests(TransitionCase):
    def test_cutover_and_same_tip_rollback_leave_files_and_index_alone(self):
        (self.repo / "shared.txt").write_text("edited in the checkout\n")
        (self.repo / "staged.txt").write_text("staged\n")
        run_git(self.repo, "add", "staged.txt")
        (self.repo / "untracked.txt").write_text("untracked\n")
        before = (git_out(self.repo, "status", "--porcelain"), git_out(self.repo, "ls-files", "-s"))
        service = self.service()
        service.activate()
        self.assertEqual(self.head(), "refs/heads/human")
        self.assertEqual((git_out(self.repo, "status", "--porcelain"), git_out(self.repo, "ls-files", "-s")),
                         before)
        result = service.rollback()
        self.assertTrue(result["rolled_back"])
        self.assertIn("same commit", result["checkout"])
        self.assertEqual(self.head(), "refs/heads/main")
        self.assertEqual((git_out(self.repo, "status", "--porcelain"), git_out(self.repo, "ls-files", "-s")),
                         before)
        self.assertEqual(service.mode(), "legacy")
        self.assertEqual(S.publication_marker(self.repo)["mode"], "legacy")
        self.assertEqual(git_out(self.repo, "rev-parse", "human"), self.base, "the human branch stays")
        self.assertEqual([t["kind"] for t in service.transitions()], ["activate", "pause", "rollback"])
        # Activating again from legacy works, and rollback from legacy is a no-op.
        service.activate()
        self.assertEqual(service.mode(), "queue")
        service.rollback()
        self.assertFalse(service.rollback()["rolled_back"])

    def test_pause_stops_publication_but_keeps_workspaces_and_submissions(self):
        service = self.service()
        service.activate()
        ws, path, sha = self.author(service, "worker", "changed\n")
        job = service.submit(workspace_id=ws["workspace_id"], request_id="w-1")
        service.pause(reason="maintenance")
        self.assertEqual(service.mode(), "paused")
        self.assertEqual(trees.resolve_project(self.repo, state_root=self.state)["mode"], "paused")
        self.assertEqual(service.run_once()["skipped"], "mode paused")
        self.assertEqual(service.queue.status(job["id"])["status"], "queued")
        self.assertEqual(self.tip(), self.base)
        with self.assertRaises(S.ModeError):
            service.allocate_workspace("another")
        with self.assertRaises(S.ModeError):
            service.submit(sha, request_id="w-2")
        self.assertEqual(service.tree_status(ws["workspace_id"])["state"], "active")
        # Resume: activate from paused publishes the pending job.
        service.activate()
        self.assertEqual(service.mode(), "queue")
        self.assertEqual(service.run_once()["job"]["status"], "landed")
        self.assertEqual(self.tip(), sha)

    def test_rollback_with_pending_work_retains_jobs_and_fast_forwards_a_clean_checkout(self):
        service = self.service()
        service.activate()
        ws, path, sha = self.author(service, "worker", "landed\n")
        service.submit(workspace_id=ws["workspace_id"], request_id="w-1")
        self.assertEqual(service.run_once()["job"]["status"], "landed")
        ws2, path2, sha2 = self.author(service, "pending", "pending\n")
        pending = service.submit(workspace_id=ws2["workspace_id"], request_id="w-2")
        self.assertNotEqual(self.tip(), self.base, "the target advanced past the human branch")
        result = service.rollback()
        self.assertTrue(result["rolled_back"], result)
        self.assertIn("fast-forwarded", result["checkout"])
        self.assertEqual(self.head(), "refs/heads/main")
        self.assertEqual(git_out(self.repo, "rev-parse", "HEAD"), self.tip())
        self.assertEqual((self.repo / "shared.txt").read_text(), "landed\n")
        self.assertEqual(git_out(self.repo, "status", "--porcelain"), "")
        self.assertEqual(result["jobs_retained"], 2)
        self.assertEqual(service.queue.status(pending["id"])["status"], "queued", "the pending job is kept")
        self.assertEqual(git_out(path2, "rev-parse", "HEAD"), sha2, "the workspace is kept")
        self.assertEqual(service.tree_status(ws2["workspace_id"])["state"], "active")
        self.assertEqual(git_out(self.repo, "rev-parse", "human"), self.tip())
        self.assertEqual(service.mode(), "legacy")

    def test_rollback_fails_closed_on_a_dirty_checkout_the_target_overtook(self):
        service = self.service()
        service.activate()
        ws, path, sha = self.author(service, "worker", "landed\n")
        service.submit(workspace_id=ws["workspace_id"], request_id="w-1")
        self.assertEqual(service.run_once()["job"]["status"], "landed")
        (self.repo / "shared.txt").write_text("human's own edit\n")
        with self.assertRaises(S.TransitionRefused) as caught:
            service.rollback()
        self.assertIn("shared.txt", str(caught.exception))
        self.assertEqual(service.mode(), "paused", "fail closed: paused, not legacy")
        self.assertEqual(S.publication_marker(self.repo)["mode"], "paused")
        self.assertEqual(self.head(), "refs/heads/human")
        self.assertEqual((self.repo / "shared.txt").read_text(), "human's own edit\n")
        self.assertEqual(git_out(self.repo, "rev-parse", "human"), self.base)
        # The person syncs the checkout themselves; then rollback completes.
        (self.repo / "shared.txt").write_text("one\ntwo\nthree\n")
        result = service.rollback()
        self.assertTrue(result["rolled_back"])
        self.assertEqual(self.head(), "refs/heads/main")
        self.assertEqual(service.mode(), "legacy")

    def test_rollback_stages_board_files_the_queue_already_landed_identically(self):
        service = self.service()
        service.activate()
        card = self.board / "features" / "2026-09-25-card.md"
        card.write_text("---\nid: AB12\ntype: work\nstatus: inbox\n---\n# A card\n")
        job = service.submit_board_snapshot([str(card)], session="pane")
        self.assertEqual(service.run_once()["job"]["status"], "landed")
        self.assertIn("?? .board/features/2026-09-25-card.md",
                      git_out(self.repo, "status", "--porcelain", "--untracked-files=all"))
        result = service.rollback()
        self.assertTrue(result["rolled_back"], result)
        self.assertIn("staged 1 Board path", result["checkout"])
        self.assertEqual(self.head(), "refs/heads/main")
        self.assertEqual(git_out(self.repo, "status", "--porcelain"), "", "identical, now tracked")
        self.assertEqual(card.read_text(), git_out(self.repo, "show", "main:.board/features/2026-09-25-card.md") + "\n")
        # A Board file newer than the target is a pending snapshot: fail closed and say so.
        service.activate()
        card.write_text("---\nid: AB12\ntype: work\nstatus: executing\n---\n# A card\n")
        service.submit_board_snapshot([str(card)], session="pane")
        self.assertEqual(service.run_once()["job"]["status"], "landed")
        card.write_text("---\nid: AB12\ntype: work\nstatus: done\n---\n# A card\n")
        with self.assertRaises(S.TransitionRefused) as caught:
            service.rollback()
        self.assertIn("pending snapshot", str(caught.exception))
        self.assertEqual(service.mode(), "paused")

    def test_rollback_refuses_a_human_branch_with_its_own_commits(self):
        service = self.service()
        service.activate()
        ws, path, sha = self.author(service, "worker", "landed\n")
        service.submit(workspace_id=ws["workspace_id"], request_id="w-1")
        self.assertEqual(service.run_once()["job"]["status"], "landed")
        (self.repo / "mine.txt").write_text("committed on the human branch\n")
        run_git(self.repo, "add", "mine.txt")
        run_git(self.repo, "commit", "-q", "-m", "human commit")
        with self.assertRaises(S.TransitionRefused) as caught:
            service.rollback()
        self.assertIn("not on main", str(caught.exception))
        self.assertEqual(service.mode(), "paused")
        self.assertEqual(self.head(), "refs/heads/human")
        kept = service.rollback(keep_head=True)
        self.assertTrue(kept["rolled_back"])
        self.assertIn("kept", kept["checkout"])
        self.assertEqual(self.head(), "refs/heads/human")

    def test_transition_lock_drains_legacy_writers_and_refuses_concurrent_transitions(self):
        service = self.service()
        # A legacy publisher mid-swap holds the lock shared: activation waits, then refuses.
        with S.transition_lock(self.repo, exclusive=False):
            started = time.monotonic()
            with self.assertRaises(S.ServiceBusy):
                service.activate(wait_seconds=0.3)
            self.assertGreaterEqual(time.monotonic() - started, 0.25)
            self.assertEqual(service.mode(), "legacy")
        service.activate()
        # A transition in progress makes a legacy publisher refuse rather than wait forever.
        with S.transition_lock(self.repo, exclusive=True):
            with self.assertRaises(S.ServiceBusy):
                with S.transition_lock(self.repo, exclusive=False, timeout=0.1):
                    pass
            with self.assertRaises(S.ServiceBusy):
                service.pause()

    def test_inventory_lists_legacy_sessions_and_attachment(self):
        land_root = self.root / "land"
        land_root.mkdir()
        (land_root / "live-session").mkdir()
        (land_root / "registry.json").write_text(json.dumps({"version": 1, "sessions": {
            "live-session": {"repo": str(self.repo), "claims": ["shared.txt"],
                             "started": S._now(), "updated": S._now(), "contact": "pane 1"},
            "stale-session": {"repo": str(self.repo), "claims": ["x"],
                              "started": "2020-01-01T00:00:00+00:00",
                              "updated": "2020-01-01T00:00:00+00:00"},
            "other-repo": {"repo": "/elsewhere", "claims": ["y"], "updated": S._now()}}}))
        service = self.service()
        inv = service.inventory()
        self.assertEqual([s["name"] for s in inv["legacy"]["sessions"]], ["live-session"])
        self.assertEqual(inv["legacy"]["sessions"][0]["contact"], "pane 1")
        self.assertEqual(inv["target_attached"], [str(self.repo)])
        self.assertEqual(inv["head"]["branch"], "main")
        self.assertIsNone(inv["accepted_policy"])
        self.assertTrue(inv["config"]["present"])
        self.assertEqual(inv["mode"], "legacy")
        self.assertFalse(inv["daemon"]["running"])


class CrashTests(TransitionCase):
    def test_crash_before_update_ref_is_reverified_and_landed_by_the_next_run(self):
        service = self.service()
        service.activate()
        ws, path, sha = self.author(service, "worker", "changed\n")
        job = service.submit(workspace_id=ws["workspace_id"], request_id="w-1")
        service.queue.faults.add("before_update_ref")
        with self.assertRaises(landq.InjectedCrash):
            service.run_once()
        self.assertEqual(self.tip(), self.base)
        self.assertEqual(service.queue.status(job["id"])["status"], "publishing", "intent was durable")
        fresh = self.service()
        result = fresh.run_once()
        self.assertEqual(result["job"]["status"], "landed", result)
        self.assertEqual(len(fresh.queue.verifications(job["id"])), 2, "verified again, never a stale pass")
        self.assertEqual(self.tip(), sha)
        self.assertEqual(fresh.main_status()["installed_sha"], sha)
        self.assertEqual([h["kind"] for h in fresh.handoffs(session="worker")], ["landed"])

    def test_crash_after_update_ref_is_recognised_and_still_delivers_the_handoff(self):
        service = self.service()
        service.activate()
        ws, path, sha = self.author(service, "worker", "changed\n")
        job = service.submit(workspace_id=ws["workspace_id"], request_id="w-1")
        service.queue.faults.add("after_update_ref")
        with self.assertRaises(landq.InjectedCrash):
            service.run_once()
        self.assertEqual(self.tip(), sha, "the ref moved before the crash")
        self.assertIsNone(service.queue.receipt(job["id"]))
        self.assertEqual(service.handoffs(), [])
        fresh = self.service()
        result = fresh.run_once()
        self.assertTrue(result.get("idle"), result)
        self.assertEqual(fresh.queue.status(job["id"])["status"], "landed")
        self.assertTrue(fresh.queue.receipt(job["id"])["verified"])
        self.assertEqual([h["kind"] for h in fresh.handoffs(session="worker")], ["landed"])
        self.assertEqual(len(fresh.queue.verifications(job["id"])), 1, "nothing verified twice")

    def test_handoff_delivery_failure_is_retried_from_the_durable_outbox(self):
        calls = []

        def wake(handoff):
            calls.append(handoff["kind"])
            if len(calls) == 1:
                raise RuntimeError("pane not reachable yet")

        service = self.service(wake=wake)
        service.activate()
        ws, path, sha = self.author(service, "worker", "changed\n")
        job = service.submit(workspace_id=ws["workspace_id"], request_id="w-1")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "landed")
        self.assertEqual(result["delivered"][0]["delivered"], False)
        self.assertIn("not reachable", result["delivered"][0]["last_error"])
        self.assertEqual(len(service.queue.outbox()), 1, "the delivery is still pending")
        self.assertEqual(len(service.handoffs(session="worker")), 1,
                         "the durable row is there for a poller even though the wake failed")
        # Backoff: not due yet; then due, and only the wake is repeated.
        self.assertEqual(service.deliver_handoffs(), [])
        with mock.patch("relay_core.landq.time.time", return_value=time.time() + 60):
            delivered = service.deliver_handoffs()
        self.assertEqual([d["delivered"] for d in delivered], [True])
        self.assertEqual(calls, ["landed", "landed"])
        self.assertEqual(len(service.handoffs(session="worker")), 1, "still one row")
        self.assertEqual(service.queue.outbox(), [])


class DaemonTests(TransitionCase):
    def test_run_loop_lands_submissions_coalesces_main_and_refuses_a_second_loop(self):
        service = self.service()
        service.activate()
        ws, path, sha = self.author(service, "worker", "changed\n")
        service.submit(workspace_id=ws["workspace_id"], request_id="w-1")
        stop = threading.Event()
        lines = []
        outcome = {}

        def loop():
            outcome["result"] = service.run(interval=0.05, stop=stop, max_ticks=40,
                                            log=lines.append)
        thread = threading.Thread(target=loop)
        thread.start()
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline and self.tip() != sha:
                time.sleep(0.05)
            self.assertEqual(self.tip(), sha)
            other = self.service()
            with self.assertRaises(S.ServiceBusy):
                other.run(interval=0.05, stop=threading.Event(), max_ticks=1)
            self.assertTrue(other.daemon_state()["running"])
            with self.assertRaises(S.ServiceBusy):
                other.rollback()
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline and service.main_status()["installed_sha"] != sha:
                time.sleep(0.05)
            self.assertEqual(service.main_status()["installed_sha"], sha)
        finally:
            stop.set()
            thread.join(30)
        self.assertFalse(thread.is_alive())
        self.assertEqual(outcome["result"]["landed"], 1)
        self.assertFalse(service.daemon_state()["running"])
        self.assertTrue(any("main release" in line for line in lines), lines)
        self.assertEqual([h["kind"] for h in service.handoffs(session="worker")], ["landed"])
        # A stale daemon record (a killed loop) is reported as not running.
        (service.root / "daemon.json").write_text(json.dumps({"pid": 1, "running": True}))
        self.assertFalse(service.daemon_state()["running"])
        self.assertTrue(service.daemon_state()["stale"])

    def test_main_updater_coalesces_a_burst_into_the_newest_sha(self):
        service = self.service()
        service.activate()
        built = []
        original = service._update_main
        started, release = threading.Event(), threading.Event()

        def slow(sha):
            started.set()
            release.wait(20)
            built.append(sha)
            return original(sha)
        service._update_main = slow
        shas = []
        for n in range(3):
            ws, path, sha = self.author(service, "w%d" % n, "change %d\n" % n)
            service.submit(workspace_id=ws["workspace_id"], request_id="w-%d" % n)
            self.assertEqual(service.run_once(main=False)["job"]["status"], "landed")
            shas.append(self.tip())
        updater = S.MainUpdater(service)
        updater.start()
        service._updater = updater
        try:
            service.request_main_update(shas[0])      # the builder takes this one and blocks
            self.assertTrue(started.wait(10))
            service.request_main_update(shas[1])      # these two arrive during the build ...
            service.request_main_update(shas[2])      # ... and coalesce into the newest
            self.assertEqual(updater.state()["pending_sha"], shas[2])
            release.set()
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline and service.main_status()["installed_sha"] != self.tip():
                time.sleep(0.05)
        finally:
            release.set()
            updater.stop(timeout=30)
            service._updater = None
        self.assertEqual(service.main_status()["installed_sha"], self.tip())
        self.assertEqual(built, [shas[0], shas[2]], "one build for the burst, the newest sha")
        self.assertEqual(service.main_status()["lag_commits"], 0)


if __name__ == "__main__":
    unittest.main()
