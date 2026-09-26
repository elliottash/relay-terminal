# SPDX-License-Identifier: AGPL-3.0-or-later
"""relay_core.integration_service (card #AMQQ, B1 of #3MH4): the coordinator and the sole
publication path.

Every test builds its own Git repository, state root and cache root under a TemporaryDirectory
and activates *that* repository; nothing here reads or changes the real checkout's mode, marker,
hook or state. Host admission is constructed with explicit limits and empty cgroup/proc roots so
no verdict depends on the machine; the reconciler gets a fake model.
"""
from __future__ import annotations

import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import integration_service as S  # noqa: E402
from relay_core import integration_slots, landq, projectconf, reconcile, trees  # noqa: E402

PY = sys.executable
MB = 1 << 20


def run_git(cwd, *args, check=True, env=None, stdin=None):
    environ = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    environ.update({"GIT_AUTHOR_NAME": "Test Author", "GIT_AUTHOR_EMAIL": "author@test.invalid",
                    "GIT_COMMITTER_NAME": "Test Author",
                    "GIT_COMMITTER_EMAIL": "author@test.invalid"})
    if env:
        environ.update(env)
    proc = subprocess.run(["git", *args], cwd=str(cwd), env=environ, input=stdin,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and proc.returncode != 0:
        raise AssertionError("git %s failed: %s" % (" ".join(args), proc.stderr))
    return proc


def git_out(cwd, *args, **kw):
    return run_git(cwd, *args, **kw).stdout.strip()


GATE = ('version = 1\n[project]\ntarget = "main"\n[workspace]\nexclude = [".board"]\n'
        '[verification]\ncommands = [["%s", "-c", "import pathlib, os; '
        'assert len(pathlib.Path(\'shared.txt\').read_text().splitlines()) >= 40; '
        'pathlib.Path(\'gate-env.txt\').write_text(os.environ.get(\'RELAY_JOBS\', \'\'))"]]\n'
        '[resources]\nmemory_bytes = %d\ndisk_bytes = %d\ncpus = 2\n'
        '[main]\ninstall = [["sh", "-c", "mkdir -p {dest}/bin && cp {source}/app.sh {dest}/bin/app '
        '&& chmod +x {dest}/bin/app"]]\nexecutable = "bin/app"\n'
        '[reconcile]\nenabled = true\nmax_attempts = 2\n' % (PY, 10 * MB, 10 * MB))

CARD = """---
id: %s
type: work
status: executing
labels: [feature]
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# %s

## Issue
%s

## Done means
- %s
"""


def write_card(board, ident, title, issue, done):
    (board / "features").mkdir(parents=True, exist_ok=True)
    path = board / "features" / ("2026-09-25-%s.md" % ident.lower())
    path.write_text(CARD % (ident, title, issue, done))
    return path


class ServiceCase(unittest.TestCase):
    """A repository with a config on `main`, a Board, and an activated service."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="relay-b1-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = self.root / "state"
        self.cache = self.root / "cache"
        self.land_root = self.root / "land"
        self.repo = self.root / "project"
        self.repo.mkdir()
        run_git(self.repo, "init", "-q", "-b", "main")
        run_git(self.repo, "config", "user.name", "Test Author")
        run_git(self.repo, "config", "user.email", "author@test.invalid")
        self.original = "\n".join("line %d" % n for n in range(40)) + "\n"
        (self.repo / "shared.txt").write_text(self.original)
        (self.repo / "app.sh").write_text("#!/bin/sh\necho relay-main-ok\n")
        (self.repo / ".relay").mkdir()
        (self.repo / ".relay" / "project.toml").write_text(GATE)
        self.board = self.repo / ".board"
        (self.board / "threads").mkdir(parents=True)
        (self.board / "board.yaml").write_text("folder: .board\n")
        write_card(self.board, "AA11", "Alice's card", "Alice changes line 2.", "line 2 says alice")
        write_card(self.board, "BB22", "Bob's card", "Bob changes line 35.", "line 35 says bob")
        run_git(self.repo, "add", "-A")
        run_git(self.repo, "commit", "-q", "-m", "baseline")
        self.base = git_out(self.repo, "rev-parse", "HEAD")
        env = {"XDG_CONFIG_HOME": str(self.root / "config"), "XDG_STATE_HOME": str(self.state),
               "XDG_CACHE_HOME": str(self.cache), "XDG_DATA_HOME": str(self.root / "data"),
               "RELAY_KEYRING": "off", "RELAY_LAND_ROOT": str(self.land_root)}
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)

    # ------------------------------------------------------------- helpers
    def admission(self, **kw):
        empty = self.root / "empty"
        (empty / "cgroup").mkdir(parents=True, exist_ok=True)
        (empty / "proc").mkdir(parents=True, exist_ok=True)
        kw.setdefault("limits", {"cpus": 4.0, "memory_bytes": 100 * MB, "disk_bytes": 100 * MB})
        return integration_slots.HostAdmission(state_root=self.state, cgroup_root=empty / "cgroup",
                                               proc_root=empty / "proc", disk_path=self.root, **kw)

    def reconciler(self, model_call=None):
        # No test ever reaches a real provider: the default model declines.
        model_call = model_call or (lambda request: ('{"give_up": "test stub"}',
                                                     {"prompt_tokens": 1, "completion_tokens": 1}))
        return reconcile.Reconciler(state_root=self.state, model_call=model_call,
                                    tiers=[{"preset": "kimi", "model": "kimi-k3", "rank": 1}],
                                    key_lookup=lambda preset: "k" if preset == "kimi" else "",
                                    guest_check=lambda guest: False)

    def service(self, *, activate=True, **kw):
        kw.setdefault("admission", self.admission())
        kw.setdefault("reconciler", self.reconciler())
        service = S.IntegrationService(self.repo, state_root=self.state, cache_root=self.cache,
                                       land_root=self.land_root, **kw)
        if activate:
            service.activate()
        return service

    def author(self, service, session, old, new, *, card=None, extra=None, base=None):
        ws = service.allocate_workspace(session, card=card, base=base)
        path = Path(ws["execution_cwd"])
        (path / "shared.txt").write_text(self.original.replace(old, new))
        for name, text in (extra or {}).items():
            (path / name).parent.mkdir(parents=True, exist_ok=True)
            (path / name).write_text(text)
        run_git(path, "add", "-A")
        run_git(path, "commit", "-q", "-m", "%s: %s" % (session, new.strip()))
        return ws, path, git_out(path, "rev-parse", "HEAD")

    def tip(self):
        return git_out(self.repo, "rev-parse", "refs/heads/main")


# ----------------------------------------------------------------------------- the hook

class HookTests(ServiceCase):
    def land_module(self):
        spec = importlib.util.spec_from_file_location("landpy_under_test", ROOT / "scripts" / "land.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_hook_text_is_the_same_in_land_py_and_the_service(self):
        land = self.land_module()
        self.assertEqual(land.HOOK, S.HOOK)
        self.assertEqual(land.HOOK_VERSION, S.HOOK_VERSION)
        self.assertEqual(land.HOOK_REFUSAL, S.HOOK_REFUSAL)

    def test_hook_refuses_shared_index_and_allows_a_linked_worktree(self):
        S.install_hook(self.repo)
        state = S.hook_state(self.repo)
        self.assertTrue(state["relay"])
        self.assertEqual(state["version"], S.HOOK_VERSION)
        (self.repo / "shared.txt").write_text("edited\n")
        run_git(self.repo, "add", "shared.txt")
        refused = run_git(self.repo, "commit", "-q", "-m", "shared", check=False)
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn(S.HOOK_REFUSAL, refused.stderr)
        self.assertEqual(self.tip(), self.base, "the refused commit moved nothing")
        allowed = run_git(self.repo, "commit", "-q", "-m", "shared", check=False,
                          env={"RELAY_ALLOW_SHARED_COMMIT": "1"})
        self.assertEqual(allowed.returncode, 0, "the owner's escape hatch still works")
        wt = self.root / "linked"
        run_git(self.repo, "worktree", "add", "-q", "-b", "feature", str(wt), "main")
        (wt / "new.txt").write_text("from a workspace\n")
        run_git(wt, "add", "new.txt")
        ok = run_git(wt, "commit", "-q", "-m", "workspace commit", check=False)
        self.assertEqual(ok.returncode, 0, ok.stderr)
        self.assertNotEqual(git_out(wt, "rev-parse", "HEAD"), git_out(self.repo, "rev-parse", "HEAD"))

    def test_hook_upgrades_version_one_and_preserves_a_project_hook(self):
        hooks = S.hooks_dir(self.repo)
        hooks.mkdir(parents=True, exist_ok=True)
        marker = self.root / "project-hook-ran"
        (hooks / "pre-commit").write_text("#!/bin/sh\ntouch %s\nexit 0\n" % marker)
        os.chmod(str(hooks / "pre-commit"), 0o755)
        result = S.install_hook(self.repo)
        self.assertTrue(result["changed"])
        self.assertTrue(result["project_hook"])
        self.assertTrue((hooks / S.HOOK_PROJECT_NAME).exists())
        wt = self.root / "linked"
        run_git(self.repo, "worktree", "add", "-q", "-b", "feature", str(wt), "main")
        (wt / "new.txt").write_text("x\n")
        run_git(wt, "add", "new.txt")
        run_git(wt, "commit", "-q", "-m", "chained")
        self.assertTrue(marker.exists(), "the project's hook still ran after Relay's check")
        # A version-1 Relay hook is upgraded in place; the current one is left alone.
        (hooks / "pre-commit").write_text("#!/bin/sh\necho '%s' >&2\nexit 1\n" % S.HOOK_REFUSAL)
        self.assertEqual(S.hook_state(self.repo)["version"], 1)
        self.assertTrue(S.install_hook(self.repo)["changed"])
        self.assertEqual(S.hook_state(self.repo)["version"], S.HOOK_VERSION)
        self.assertFalse(S.install_hook(self.repo)["changed"])
        land = self.land_module()
        self.assertFalse(land.install_hook(self.repo, lambda line: None))
        # land.py refuses a foreign hook by default and preserves+chains it under --force.
        (hooks / "pre-commit").write_text("#!/bin/sh\nexit 0\n")
        (hooks / S.HOOK_PROJECT_NAME).unlink()
        with self.assertRaises(land.Fail):
            land.install_hook(self.repo, lambda line: None)
        self.assertTrue(land.install_hook(self.repo, lambda line: None, force=True))
        self.assertEqual((hooks / S.HOOK_PROJECT_NAME).read_text(), "#!/bin/sh\nexit 0\n")
        self.assertEqual((hooks / "pre-commit").read_text(), S.HOOK)


# ----------------------------------------------------------------------------- activation

class ActivationTests(ServiceCase):
    def test_default_is_legacy_and_nothing_publishes_without_an_accepted_policy(self):
        service = self.service(activate=False)
        self.assertEqual(service.mode(), "legacy")
        self.assertIsNone(service.accepted_policy())
        self.assertEqual(S.publication_marker(self.repo)["mode"], "legacy")
        with self.assertRaises(S.ModeError):
            service.allocate_workspace("s1")
        with self.assertRaises(S.ModeError):
            service.submit(self.base, request_id="r1")
        result = service.run_once()
        self.assertEqual(result["skipped"], "mode legacy")
        verdict = service.verifier({"id": "x", "kind": "code", "selected_tests": []}, self.base,
                                   str(self.repo))
        self.assertFalse(verdict["ok"])
        self.assertIn("no accepted policy", verdict["reason"])

    def test_activation_captures_policy_moves_head_symbolically_and_keeps_the_checkout(self):
        (self.repo / "shared.txt").write_text("dirty working copy\n")
        (self.repo / "staged.txt").write_text("staged\n")
        run_git(self.repo, "add", "staged.txt")
        (self.repo / "untracked.txt").write_text("untracked\n")
        status_before = git_out(self.repo, "status", "--porcelain")
        index_before = git_out(self.repo, "ls-files", "-s")
        service = self.service(activate=False)
        plan = service.activate(dry_run=True)
        self.assertTrue(plan["can_activate"], plan["blockers"])
        self.assertEqual(service.mode(), "legacy", "a dry run changes nothing")
        self.assertFalse(S.hook_state(self.repo)["installed"])
        record = service.activate()
        self.assertTrue(record["activated"])
        self.assertTrue(record["moved_head"])
        self.assertEqual(git_out(self.repo, "symbolic-ref", "HEAD"), "refs/heads/human")
        self.assertEqual(git_out(self.repo, "rev-parse", "human"), self.base)
        self.assertEqual(self.tip(), self.base)
        self.assertEqual(git_out(self.repo, "status", "--porcelain"), status_before,
                         "files and index are exactly as they were")
        self.assertEqual(git_out(self.repo, "ls-files", "-s"), index_before)
        self.assertEqual((self.repo / "shared.txt").read_text(), "dirty working copy\n")
        self.assertEqual(service.mode(), "queue")
        self.assertEqual(trees.resolve_project(self.repo, state_root=self.state)["mode"], "queue")
        marker = S.publication_marker(self.repo)
        self.assertEqual(marker["mode"], "queue")
        self.assertEqual(marker["repo_id"], service.repo_id)
        accepted = service.accepted_policy()
        self.assertEqual(accepted["hash"], projectconf.policy_hash(projectconf.load(self.repo)))
        self.assertEqual(accepted["target_sha"], self.base)
        self.assertEqual(S.hook_state(self.repo)["version"], S.HOOK_VERSION)
        self.assertEqual(service.queue.target_attached(), None, "main is checked out nowhere")
        baseline = git_out(self.repo, "for-each-ref", "--format=%(objectname)", S.TRANSITION_REF)
        self.assertEqual(baseline, self.base)
        kinds = [t["kind"] for t in service.transitions()]
        self.assertEqual(kinds, ["activate"])

    def test_activation_refuses_without_config_or_with_an_attached_worktree(self):
        run_git(self.repo, "rm", "-q", ".relay/project.toml")
        run_git(self.repo, "commit", "-q", "-m", "no config")
        service = self.service(activate=False)
        plan = service.activate(dry_run=True)
        self.assertFalse(plan["can_activate"])
        self.assertTrue(any("project.toml" in b for b in plan["blockers"]), plan["blockers"])
        with self.assertRaises(S.TransitionRefused):
            service.activate()
        self.assertEqual(service.mode(), "legacy")
        self.assertEqual(git_out(self.repo, "symbolic-ref", "HEAD"), "refs/heads/main")
        run_git(self.repo, "revert", "--no-edit", "HEAD")
        wt = self.root / "other"
        run_git(self.repo, "worktree", "add", "-q", "--detach", str(wt), "main")
        run_git(wt, "switch", "-q", "human2") if False else None
        run_git(wt, "checkout", "-q", "-b", "keeper", "main")
        run_git(self.repo, "switch", "-q", "--detach", "main")
        run_git(wt, "checkout", "-q", "main")   # the *other* worktree now holds main
        run_git(self.repo, "switch", "-q", "main") if False else None
        plan = service.activate(dry_run=True)
        self.assertTrue(any("checked out" in b for b in plan["blockers"]), plan["blockers"])
        with self.assertRaises(S.TransitionRefused):
            service.activate()

    def test_candidate_config_change_still_runs_the_accepted_policy(self):
        service = self.service()
        ws = service.allocate_workspace("weaker")
        path = Path(ws["execution_cwd"])
        (path / "shared.txt").write_text("broken\n")
        (path / ".relay" / "project.toml").write_text(
            'version = 1\n[verification]\ncommands = [["%s", "-c", "pass"]]\n' % PY)
        run_git(path, "add", "-A")
        run_git(path, "commit", "-q", "-m", "weaken the gate")
        job = service.submit(workspace_id=ws["workspace_id"], request_id="weak-1")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "failed", result)
        self.assertEqual(self.tip(), self.base)
        self.assertEqual(service.queue.status(job["id"])["policy_hash"],
                         service.accepted_policy()["hash"])


# ----------------------------------------------------------------------------- the flow

class FlowTests(ServiceCase):
    def test_two_same_file_authors_land_with_receipts_handoffs_notes_and_a_main_release(self):
        service = self.service()
        alice, apath, asha = self.author(service, "alice", "line 2\n", "alice change\n", card="AA11")
        bob, bpath, bsha = self.author(service, "bob", "line 35\n", "bob change\n", card="BB22")
        self.assertFalse((apath / ".board").exists(), "the canonical Board is not in a workspace")
        ajob = service.submit(workspace_id=alice["workspace_id"], request_id="alice-1")
        bjob = service.submit(workspace_id=bob["workspace_id"], request_id="bob-1")
        self.assertEqual(ajob["submitted_sha"], asha)
        self.assertEqual(ajob["base_sha"], self.base)
        first = service.run_once()
        self.assertEqual(first["job"]["status"], "landed", first)
        self.assertEqual(first["granted_cpus"], 2.0)
        second = service.run_once()
        self.assertEqual(second["job"]["status"], "landed", second)
        main = git_out(self.repo, "show", "main:shared.txt")
        self.assertIn("alice change", main)
        self.assertIn("bob change", main)
        # Author trees are untouched; the human checkout is untouched.
        self.assertEqual(asha, git_out(apath, "rev-parse", "HEAD"))
        self.assertEqual(bsha, git_out(bpath, "rev-parse", "HEAD"))
        self.assertEqual(self.original, (self.repo / "shared.txt").read_text())
        # Receipts bind the exact verified candidate.
        for job in (ajob, bjob):
            receipt = service.queue.receipt(job["id"])
            self.assertTrue(receipt["verified"])
            self.assertEqual(receipt["policy_hash"], service.accepted_policy()["hash"])
        # The gate saw RELAY_JOBS from the granted cpus.
        log = Path(service.queue.verifications(bjob["id"])[-1]["log_path"]).read_text()
        self.assertIn("RELAY_JOBS=2", log)
        # Handoffs: addressed to the author's session, with the card note written once.
        handoffs = service.handoffs()
        self.assertEqual({h["session"] for h in handoffs}, {"alice", "bob"})
        self.assertEqual({h["kind"] for h in handoffs}, {"landed"})
        self.assertEqual({h["card"] for h in handoffs}, {"AA11", "BB22"})
        self.assertEqual(service.handoffs(session="alice")[0]["job_id"], ajob["id"])
        service.deliver_handoffs()
        service.deliver_handoffs()
        thread = (self.board / "threads" / "AA11.md").read_text()
        self.assertEqual(thread.count("landq:%s:landed" % ajob["id"]), 1)
        self.assertIn("landed", thread)
        acked = service.ack_handoff(handoffs[0]["id"])
        self.assertIsNotNone(acked["acked_at"])
        self.assertEqual(len(service.handoffs()), 1)
        # Events carry main_moved for each publication.
        moved = [e for e in service.events() if e["kind"] == "main_moved"]
        self.assertEqual(len(moved), 2)
        self.assertEqual(moved[0]["previous_sha"], self.base)
        self.assertEqual(moved[1]["sha"], self.tip())
        # The runnable main was installed and reports no lag.
        status = service.main_status()
        self.assertTrue(status["configured"])
        self.assertEqual(status["installed_sha"], self.tip())
        self.assertEqual(status["lag_commits"], 0)
        exe = service.main_executable()
        self.assertEqual(subprocess.run([str(exe)], capture_output=True, text=True).stdout.strip(),
                         "relay-main-ok")
        # tree_status / queue_status shapes for B2/B3.
        ts = service.tree_status(alice["workspace_id"])
        self.assertEqual(ts["board_root"], str(self.board))
        self.assertEqual(ts["job"]["status"], "landed")
        self.assertTrue(ts["unlanded"], "the workspace tip is past its base until removed")
        qs = service.queue_status()
        self.assertEqual([j["status"] for j in qs["jobs"]], ["landed", "landed"])
        self.assertEqual(qs["main_release"]["installed_sha"], self.tip())
        snap = service.snapshot()
        self.assertEqual(snap["mode"], "queue")
        self.assertEqual(len(snap["workspaces"]), 2)
        self.assertEqual(snap["outbox_pending"], 0)
        # Cleanup with the receipt the service finds itself.
        service.release_workspace(alice["workspace_id"], owner="alice")
        removed = service.remove_workspace(alice["workspace_id"])
        self.assertEqual(removed["status"], "removed")

    def test_failed_gate_keeps_main_and_hands_off_to_the_author_session(self):
        service = self.service()
        ws, path, sha = self.author(service, "broken", "line 2\n", "candidate\n", card="AA11")
        (path / "shared.txt").write_text("only one line\n")   # the gate wants 40 lines
        run_git(path, "commit", "-q", "-am", "break the invariant")
        job = service.submit(workspace_id=ws["workspace_id"], request_id="broken-1")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "failed")
        self.assertEqual(self.tip(), self.base)
        self.assertIsNone(service.queue.receipt(job["id"]))
        handoff = service.handoffs(session="broken")
        self.assertEqual(len(handoff), 1)
        self.assertEqual(handoff[0]["kind"], "failed")
        self.assertIn("target was not moved", handoff[0]["text"])
        self.assertIn("landq:%s:failed" % job["id"], (self.board / "threads" / "AA11.md").read_text())
        self.assertEqual(service.main_status()["installed_sha"], None, "nothing was released")

    def test_admission_shortfall_defers_the_job_instead_of_failing_it(self):
        service = self.service(admission=self.admission(limits={"cpus": 1.0, "memory_bytes": 100 * MB,
                                                                 "disk_bytes": 100 * MB}))
        ws, path, sha = self.author(service, "big", "line 2\n", "needs two cpus\n")
        job = service.submit(workspace_id=ws["workspace_id"], request_id="big-1")
        result = service.run_once()
        self.assertIn("admission", result["skipped"])
        self.assertEqual(service.queue.status(job["id"])["status"], "queued")
        self.assertEqual(self.tip(), self.base)

    def test_metadata_snapshot_lands_board_files_and_schema_failures_are_refused(self):
        service = self.service()
        write_card(self.board, "CC33", "A new card", "Written after activation.", "it lands")
        thread = self.board / "threads" / "AA11.md"
        thread.write_text("<!-- relay:entry 20260925T000000Z-aa author=owner kind=comment -->\nfirst\n")
        job = service.submit_board_snapshot([".board/features/2026-09-25-cc33.md", str(thread)],
                                            session="pane-1", message="board: pane-1 turn-1")
        self.assertEqual(job["kind"], "metadata")
        again = service.submit_board_snapshot([".board/features/2026-09-25-cc33.md", str(thread)],
                                              session="pane-1")
        self.assertEqual(again["id"], job["id"], "the same snapshot is the same job")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "landed", result)
        self.assertIsNone(service.submit_board_snapshot([".board/features/2026-09-25-cc33.md"],
                                                        session="pane-1"),
                          "a landed snapshot is nothing to submit")
        self.assertIn("A new card", git_out(self.repo, "show", "main:.board/features/2026-09-25-cc33.md"))
        self.assertEqual(git_out(self.repo, "show", "main:.board/threads/AA11.md").splitlines()[-1],
                         "first")
        # The landing note went onto the card's thread in the canonical Board: a later edit,
        # which is a later job — the snapshot that landed was not rewritten.
        self.assertIn("landq:%s:landed" % job["id"], thread.read_text())
        self.assertIsNotNone(service.submit_board_snapshot([str(thread)], session="pane-1"))
        self.assertEqual(service.run_once()["job"]["status"], "landed")
        handoff = service.handoffs(session="pane-1")
        self.assertEqual([h["kind"] for h in handoff], ["landed", "landed"])
        # A card without an id is refused by the schema check; main is unchanged.
        landed = self.tip()
        (self.board / "features" / "2026-09-25-cc33.md").write_text("---\ntype: work\nstatus: x\n---\n# no id\n")
        bad = service.submit_board_snapshot([".board/features/2026-09-25-cc33.md"], session="pane-1")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "failed")
        self.assertIn("no id", service.queue.status(bad["id"])["reason"])
        self.assertEqual(self.tip(), landed)
        # A thread that lost an entry is refused: threads only grow.
        thread.write_text("<!-- relay:entry 20260925T000001Z-bb author=owner kind=comment -->\nsecond only\n")
        lost = service.submit_board_snapshot([str(thread)], session="pane-1")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "failed")
        self.assertIn("lost entries", service.queue.status(lost["id"])["reason"])
        # Code paths cannot ride in a Board snapshot at all.
        with self.assertRaises(S.ServiceUsageError):
            service.submit_board_snapshot(["shared.txt"], session="pane-1")

    def test_try_runs_the_accepted_gate_without_publishing(self):
        service = self.service()
        ws, path, sha = self.author(service, "trier", "line 2\n", "try me\n")
        result = service.try_candidate(None, workspace_id=ws["workspace_id"])
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["target_sha"], self.base)
        self.assertEqual(result["note"], "fast-forward")
        self.assertEqual(self.tip(), self.base)
        self.assertEqual(service.queue.status(), [])
        self.assertEqual(git_out(self.repo, "for-each-ref", S.TRY_REF), "")
        self.assertFalse(list((self.cache / "integration" / service.repo_id / "try").iterdir()))
        # A conflicting try reports the conflict rather than a gate result.
        other, opath, osha = self.author(service, "other", "line 2\n", "other change\n")
        service.submit(workspace_id=other["workspace_id"], request_id="other-1")
        service.run_once()
        clash = service.try_candidate(sha)
        self.assertTrue(clash["conflict"])
        self.assertEqual(clash["conflicts"], ["shared.txt"])


# ----------------------------------------------------------------------------- reconciliation

class ReconcileTests(ServiceCase):
    def land_first(self, service):
        ws, path, sha = self.author(service, "first", "line 2\n", "first change\n", card="AA11")
        service.submit(workspace_id=ws["workspace_id"], request_id="first-1")
        self.assertEqual(service.run_once()["job"]["status"], "landed")
        return sha

    def test_conflict_is_reconciled_with_both_cards_intents_and_gated_before_landing(self):
        requests = []

        def model(request):
            requests.append(request)
            merged = self.original.replace("line 2\n", "first change\nsecond's line 2\n").replace(
                "line 3\n", "second change\n")
            return json.dumps({"files": [{"path": "shared.txt", "content": merged}],
                               "notes": "kept both"}), {"prompt_tokens": 500, "completion_tokens": 100}

        service = self.service(reconciler=self.reconciler(model_call=model))
        first = self.land_first(service)
        # Based on the old tip, changing line 2 as well: a textual conflict with the first landing.
        ws, path, sha = self.author(service, "second", "line 3\n", "second change\n", card="BB22",
                                    base=self.base)
        (path / "shared.txt").write_text(self.original.replace("line 2\n", "second's line 2\n")
                                         .replace("line 3\n", "second change\n"))
        run_git(path, "commit", "-q", "-am", "second conflicts")
        sha = git_out(path, "rev-parse", "HEAD")
        job = service.submit(workspace_id=ws["workspace_id"], request_id="second-1")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "landed", result)
        self.assertEqual(len(requests), 1)
        prompt = requests[0]["user"]
        self.assertIn("Bob's card", prompt, "the submitted card's title reached the model")
        self.assertIn("Alice's card", prompt, "the target side's card reached the model too")
        self.assertIn("line 35 says bob", prompt)
        self.assertIn("line 2 says alice", prompt)
        landed = git_out(self.repo, "show", "main:shared.txt")
        self.assertIn("first change", landed)
        self.assertIn("second change", landed)
        message = git_out(self.repo, "log", "-1", "--format=%B", "main")
        self.assertIn("Reconciled-From: %s %s" % (git_out(self.repo, "rev-parse", "main^1"), sha), message)
        self.assertTrue(service.queue.receipt(job["id"])["verified"], "the gate ran on the resolution")
        for card in ("AA11", "BB22"):
            self.assertIn("Reconciled landing job %s" % job["id"],
                          (self.board / "threads" / ("%s.md" % card)).read_text())

    def test_refused_reconciliation_returns_to_the_author_session_never_the_owner(self):
        service = self.service(reconciler=self.reconciler(
            model_call=lambda request: ('{"give_up": "the two intents contradict"}',
                                        {"prompt_tokens": 10, "completion_tokens": 5})))
        self.land_first(service)
        ws, path, sha = self.author(service, "second", "line 2\n", "second's line 2\n", card="BB22",
                                    base=self.base)
        job = service.submit(workspace_id=ws["workspace_id"], request_id="second-1")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "conflict", result)
        self.assertNotIn("second's line 2", git_out(self.repo, "show", "main:shared.txt"))
        self.assertEqual(sha, git_out(path, "rev-parse", "HEAD"), "the author's branch is untouched")
        handoffs = service.handoffs(session="second")
        self.assertEqual([h["kind"] for h in handoffs], ["author_required"])
        text = handoffs[0]["text"]
        self.assertIn("could not be reconciled automatically", text)
        self.assertIn("contradict", text)
        self.assertIn("Nothing was changed in your workspace", text)
        self.assertEqual(handoffs[0]["workspace_id"], ws["workspace_id"])
        self.assertIn("landq:%s:author_required" % job["id"],
                      (self.board / "threads" / "BB22.md").read_text())
        # No handoff is addressed anywhere but the author's session.
        self.assertEqual({h["session"] for h in service.handoffs()}, {"first", "second"})

    def test_enrich_context_reads_both_sides_from_the_canonical_board(self):
        service = self.service()
        first = self.land_first(service)
        context = {"repo": str(self.repo), "repo_id": service.repo_id, "job_id": "j",
                   "base_sha": self.base, "target_sha": self.tip(), "submitted_sha": "x",
                   "candidate_path": str(self.repo), "cards": ["BB22"], "intents": {},
                   "conflicts": ["shared.txt"], "diagnostics": {}, "policy": None}
        enriched = service.enrich_context(context)
        self.assertEqual([c["id"] for c in enriched["cards"]], ["BB22", "AA11"])
        self.assertEqual([c["side"] for c in enriched["cards"]], ["submitted", "target"])
        self.assertEqual(enriched["cards"][0]["title"], "Bob's card")
        self.assertEqual(enriched["cards"][1]["summary"], "Alice changes line 2.")
        self.assertTrue(any("line 35 says bob" in i for i in enriched["intents"]))
        self.assertEqual(enriched["policy"]["reconcile"]["max_attempts"], 2)
        self.assertEqual(enriched["board_root"], str(self.board))


# ----------------------------------------------------------------------------- legacy guard

class LegacyGuardTests(ServiceCase):
    LAND = ROOT / "scripts" / "land.py"

    def land(self, *args, cwd=None, check=False):
        env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        env["RELAY_LAND_ROOT"] = str(self.land_root)
        return subprocess.run([PY, str(self.LAND), "--root", str(self.land_root), *args],
                              cwd=str(cwd or self.repo), env=env, capture_output=True, text=True)

    def setUp(self):
        super().setUp()
        # land.py board-sync in queue mode imports relay_core from <repo>/backend.
        (self.repo / "backend").mkdir()
        os.symlink(ROOT / "backend" / "relay_core", self.repo / "backend" / "relay_core")

    def test_legacy_commit_and_repair_refuse_and_board_sync_routes_to_the_queue(self):
        service = self.service(activate=False)
        begun = self.land("begin", "legacy-session", "shared.txt")   # snapshot, then edit
        self.assertEqual(begun.returncode, 0, begun.stderr)
        (self.repo / "shared.txt").write_text(self.original.replace("line 1\n", "legacy edit\n"))
        run_git(self.repo, "commit", "-q", "--allow-empty", "-m", "a second commit",
                env={"RELAY_ALLOW_SHARED_COMMIT": "1"})   # `begin` installed the hook
        second = self.tip()
        service.activate()
        refused = self.land("commit", "legacy-session", "-m", "legacy landing")
        self.assertEqual(refused.returncode, 2, refused.stdout + refused.stderr)
        self.assertIn("relay-land", refused.stderr)
        self.assertEqual(self.tip(), second, "legacy commit moved nothing")
        self.assertIn("legacy edit", (self.repo / "shared.txt").read_text(), "working tree kept")
        dry = self.land("commit", "legacy-session", "-m", "legacy landing", "--dry-run")
        self.assertEqual(dry.returncode, 0, dry.stderr)
        self.assertIn("would be refused", dry.stdout)
        repair = self.land("repair", second, "--paths", "shared.txt")
        self.assertEqual(repair.returncode, 2, repair.stdout + repair.stderr)
        # board-sync becomes a metadata job the service publishes.
        write_card(self.board, "DD44", "Synced card", "Came through board-sync.", "it lands")
        synced = self.land("board-sync", "pane-token", "-m", "board: pane turn",
                           ".board/features/2026-09-25-dd44.md")
        self.assertEqual(synced.returncode, 0, synced.stdout + synced.stderr)
        self.assertIn("landq job", synced.stdout)
        self.assertEqual(self.tip(), second, "board-sync itself published nothing")
        result = service.run_once()
        self.assertEqual(result["job"]["status"], "landed", result)
        self.assertEqual(result["job"]["kind"], "metadata")
        self.assertIn("Synced card", git_out(self.repo, "show", "main:.board/features/2026-09-25-dd44.md"))
        self.assertEqual(service.handoffs(session="pane-token")[0]["kind"], "landed")
        # Paused: board-sync refuses instead of publishing.
        service.pause(reason="test")
        paused = self.land("board-sync", "pane-token", "-m", "board: pane turn",
                           ".board/features/2026-09-25-dd44.md")
        self.assertEqual(paused.returncode, 2, paused.stdout + paused.stderr)
        # After rollback the legacy commit lands again.
        service.rollback()
        self.assertEqual(service.mode(), "legacy")
        landed = self.land("commit", "legacy-session", "-m", "legacy landing")
        self.assertEqual(landed.returncode, 0, landed.stdout + landed.stderr)
        self.assertIn("legacy edit", git_out(self.repo, "show", "main:shared.txt"))

    def test_activate_waits_for_a_legacy_publication_holding_the_shared_lock(self):
        service = self.service(activate=False)
        with S.transition_lock(self.repo, exclusive=False):
            with self.assertRaises(S.ServiceBusy):
                service.activate(wait_seconds=0.2)
            self.assertEqual(service.mode(), "legacy")
        service.activate()
        self.assertEqual(service.mode(), "queue")


# ----------------------------------------------------------------------------- CLI

class CliTests(ServiceCase):
    def cli(self, *args, cwd=None):
        out = io.StringIO()
        code = landq.main(["--repo", str(cwd or self.repo), "--state-root", str(self.state),
                           "--cache-root", str(self.cache), *args], out=out)
        text = out.getvalue()
        try:
            return code, json.loads(text)
        except ValueError:
            return code, text

    def test_service_verbs_dispatch_through_relay_land(self):
        code, init = self.cli("project-init")
        self.assertEqual(code, 0, init)
        self.assertTrue(init["config_present"])
        self.assertEqual(init["registry"]["mode"], "legacy")
        code, plan = self.cli("activate", "--dry-run")
        self.assertEqual(code, 0, plan)
        self.assertTrue(plan["can_activate"])
        code, activated = self.cli("activate")
        self.assertEqual(code, 0, activated)
        code, ws = self.cli("workspace", "create", "cli-session", "--card", "AA11")
        self.assertEqual(code, 0, ws)
        path = Path(ws["execution_cwd"])
        (path / "shared.txt").write_text(self.original.replace("line 2\n", "cli change\n"))
        run_git(path, "commit", "-q", "-am", "cli change")
        sha = git_out(path, "rev-parse", "HEAD")
        code, tried = self.cli("try", sha, "--admission-timeout", "0")
        self.assertEqual(code, 0, tried)
        code, job = self.cli("submit", sha, "--request-id", "cli-1", "--workspace-id", ws["workspace_id"])
        self.assertEqual(code, 0, job)
        with mock.patch.object(S.IntegrationService, "admission", new=self.admission()):
            code, ran = self.cli("run", "--once")
        self.assertEqual(code, 0, ran)
        self.assertEqual(ran["job"]["status"], "landed")
        code, status = self.cli("main-status")
        self.assertEqual(code, 0)
        self.assertEqual(status["installed_sha"], sha)
        self.assertEqual(status["lag_commits"], 0)
        code, snap = self.cli("snapshot")
        self.assertEqual(snap["mode"], "queue")
        self.assertEqual(snap["jobs"][0]["session"], "cli-session")
        code, handoffs = self.cli("handoffs", "--session", "cli-session")
        self.assertEqual([h["kind"] for h in handoffs], ["landed"])
        code, paused = self.cli("pause")
        self.assertEqual(paused["mode"], "paused")
        code, idle = self.cli("run", "--once")
        self.assertEqual(idle["skipped"], "mode paused")
        code, back = self.cli("rollback")
        self.assertEqual(code, 0, back)
        self.assertEqual(back["mode"], "legacy")
        code, usage = self.cli()
        self.assertEqual(code, 1)
        self.assertIn("activate", usage)

    def test_module_entry_point_and_installed_layout(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith(("PYTHON", "GIT_"))}
        env["PYTHONPATH"] = str(ROOT / "backend")
        result = subprocess.run([PY, "-m", "relay_core.integration_service", "--repo", str(self.repo),
                                 "--state-root", str(self.state), "inventory"],
                                cwd=str(self.root), env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        inventory = json.loads(result.stdout)
        self.assertEqual(inventory["mode"], "legacy")
        self.assertEqual(inventory["head"]["branch"], "main")
        self.assertTrue(inventory["config"]["present"])
        result = subprocess.run([PY, str(ROOT / "scripts" / "relay-land"), "run", "--help"],
                                cwd=str(self.root), env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--once", result.stdout)

    def test_project_init_writes_a_reviewable_suggestion_for_a_bare_project(self):
        bare = self.root / "bare"
        bare.mkdir()
        run_git(bare, "init", "-q", "-b", "main")
        (bare / "tests").mkdir()
        (bare / "tests" / "test_x.py").write_text("def test_x():\n    assert True\n")
        run_git(bare, "add", "-A")
        run_git(bare, "commit", "-q", "-m", "bare")
        record = S.project_init(bare, state_root=self.state, write=True)
        self.assertTrue(record["written"])
        text = (bare / ".relay" / "project.toml").read_text()
        self.assertIn("pytest", text)
        config = projectconf.load(bare)
        self.assertEqual(config["verification"]["commands"][0][:3], ["python3", "-m", "pytest"])
        again = S.project_init(bare, state_root=self.state, write=True)
        self.assertFalse(again["written"], "an existing config is never overwritten")
        self.assertEqual(trees.resolve_project(bare, state_root=self.state)["mode"], "legacy")


if __name__ == "__main__":
    unittest.main()
