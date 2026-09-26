"""relay_core.landq (card #FW1C): the durable verification and publication queue.

Every test builds its own Git repository and its own state root under a TemporaryDirectory.
The repository's HEAD is on a `human` branch so the `main` target is not checked out anywhere —
the shape a cutover leaves behind — and commits are made with plumbing against a private index,
so no test depends on a checkout, and nothing here touches the real checkout or its state.
"""

import io
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import landq  # noqa: E402
from relay_core.landq import Queue  # noqa: E402


def run_git(repo, *args, env=None, check=True, stdin=None):
    environ = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    environ.update({"GIT_AUTHOR_NAME": "Test Author", "GIT_AUTHOR_EMAIL": "author@test.invalid",
                    "GIT_COMMITTER_NAME": "Test Author",
                    "GIT_COMMITTER_EMAIL": "author@test.invalid"})
    if env:
        environ.update(env)
    proc = subprocess.run(["git", *args], cwd=str(repo), env=environ, input=stdin,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and proc.returncode != 0:
        raise AssertionError("git %s failed: %s" % (" ".join(args), proc.stderr))
    return proc


def git_out(repo, *args, **kw):
    return run_git(repo, *args, **kw).stdout.strip()


def make_repo(root: Path, *, attached=False) -> Path:
    """A repository with one commit on `main`. Unless `attached`, HEAD is moved to a `human`
    branch at the same commit so `main` is not checked out (the post-cutover shape)."""
    repo = root / "repo"
    repo.mkdir(parents=True)
    run_git(repo, "init", "-q", "-b", "main")
    run_git(repo, "config", "user.name", "Test Author")
    run_git(repo, "config", "user.email", "author@test.invalid")
    (repo / "a.txt").write_text("alpha\n")
    (repo / "b.txt").write_text("one\ntwo\nthree\n")
    (repo / ".board").mkdir()
    (repo / ".board" / "card.md").write_text("# card\nstatus: inbox\n")
    run_git(repo, "add", "-A")
    run_git(repo, "commit", "-q", "-m", "base")
    if not attached:
        run_git(repo, "switch", "-q", "-c", "human")
    return repo


def plumb_commit(repo, parent, files, message="change", *, modes=None):
    """A commit on top of `parent` with `files` ({path: text, or None to delete}) applied,
    made through a private index so no checkout is involved. Returns its sha."""
    modes = modes or {}
    with tempfile.NamedTemporaryFile(prefix="idx-", delete=False) as tmp:
        index = tmp.name
    os.unlink(index)
    env = {"GIT_INDEX_FILE": index}
    run_git(repo, "read-tree", parent, env=env)
    for path, text in files.items():
        if text is None:
            run_git(repo, "update-index", "--force-remove", "--", path, env=env)
            continue
        blob = git_out(repo, "hash-object", "-w", "--stdin", stdin=text)
        mode = modes.get(path, "100644")
        run_git(repo, "update-index", "--add", "--cacheinfo", "%s,%s,%s" % (mode, blob, path),
                env=env)
    tree = git_out(repo, "write-tree", env=env)
    os.unlink(index)
    return git_out(repo, "commit-tree", tree, "-p", parent, stdin=message + "\n")


def tip(repo, branch="main"):
    return git_out(repo, "rev-parse", "refs/heads/%s" % branch)


def show(repo, rev, path):
    return git_out(repo, "show", "%s:%s" % (rev, path))


class Recorder:
    """A verifier that records every call and answers as told."""

    def __init__(self, ok=True, policy="policy-1", verified=True, on_call=None):
        self.calls = []
        self.ok = ok
        self.policy = policy
        self.verified = verified
        self.on_call = on_call

    def __call__(self, job, candidate_sha, candidate_path):
        self.calls.append((dict(job), candidate_sha, candidate_path))
        assert (Path(candidate_path) / ".git").exists(), "candidate_path is a git checkout"
        assert job["status"] == "verifying"
        if self.on_call:
            self.on_call(job, candidate_sha, candidate_path)
        return {"ok": self.ok, "policy_hash": self.policy, "verified": self.verified,
                "log": "verified %s" % candidate_sha,
                **({} if self.ok else {"reason": "gate said no"})}


class QueueCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="landq-")
        self.root = Path(self.temp.name)
        self.state = self.root / "state"
        self.repo = make_repo(self.root)
        self.base = tip(self.repo)

    def tearDown(self):
        self.temp.cleanup()

    def queue(self, **kw):
        kw.setdefault("repo_id", "test-repo")
        return Queue(self.repo, state_root=self.state, **kw)

    # ------------------------------------------------------------ submit

    def test_submit_records_job_and_retains_ref(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "alpha\nbeta\n"})
        job = q.submit(sha, request_id="r1", card="#FW1C", workspace_id="ws1",
                       selected_tests=["test_x"])
        self.assertEqual(job["status"], "queued")
        self.assertEqual(job["submitted_sha"], sha)
        self.assertEqual(job["card"], "#FW1C")
        self.assertEqual(job["selected_tests"], ["test_x"])
        self.assertEqual(job["target"], "main")
        self.assertEqual(git_out(self.repo, "rev-parse", "refs/landq/jobs/%s/submitted" % job["id"]),
                         sha)
        self.assertTrue((self.state / "integration" / "test-repo" / "queue.sqlite3").exists())
        # A fresh Queue object sees the same job.
        self.assertEqual(self.queue().status(job["id"])["submitted_sha"], sha)
        self.assertEqual([j["id"] for j in self.queue().status()], [job["id"]])

    def test_submit_is_idempotent_per_request_id(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "alpha\nbeta\n"})
        first = q.submit(sha, request_id="r1")
        again = q.submit(sha, request_id="r1")
        self.assertEqual(again["id"], first["id"])
        self.assertTrue(again.get("existing"))
        self.assertEqual(len(q.status()), 1)
        other = plumb_commit(self.repo, self.base, {"a.txt": "alpha\ngamma\n"})
        with self.assertRaises(landq.Refused):
            q.submit(other, request_id="r1")
        self.assertEqual(len(q.status()), 1)
        # No stray retention ref for the refused duplicate.
        refs = git_out(self.repo, "for-each-ref", "refs/landq/").splitlines()
        self.assertEqual(len(refs), 1)

    def test_submit_refuses_bad_input(self):
        q = self.queue()
        with self.assertRaises(landq.Refused):
            q.submit("0" * 40, request_id="r1")
        with self.assertRaises(landq.UsageError):
            q.submit(self.base, request_id="")
        with self.assertRaises(landq.UsageError):
            q.submit(self.base, request_id="r2", kind="docs")
        with self.assertRaises(landq.UsageError):
            q.status("nope")

    def test_queue_needs_a_verifier(self):
        q = self.queue()
        q.submit(plumb_commit(self.repo, self.base, {"a.txt": "x\n"}), request_id="r1")
        with self.assertRaises(landq.Refused):
            q.process_one(None)
        self.assertEqual(q.status()[0]["status"], "queued")
        self.assertEqual(tip(self.repo), self.base)

    def test_empty_queue(self):
        self.assertIsNone(self.queue().process_one(Recorder()))

    # ------------------------------------------------------------ publish

    def test_fast_forward_publishes_the_exact_submitted_commit(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "alpha\nbeta\n"})
        job = q.submit(sha, request_id="r1", card="FW1C")
        verifier = Recorder()
        done = q.process_one(verifier)
        self.assertEqual(done["status"], "landed", done)
        self.assertEqual(done["published_sha"], sha)
        self.assertEqual(tip(self.repo), sha)
        self.assertEqual(len(verifier.calls), 1)
        self.assertEqual(verifier.calls[0][1], sha)
        self.assertEqual(done["candidate_sha"], sha)
        self.assertEqual(done["policy_hash"], "policy-1")
        receipt = q.receipt(job["id"])
        self.assertEqual(receipt["published_sha"], sha)
        self.assertEqual(receipt["target_before"], self.base)
        self.assertEqual(receipt["submitted_sha"], sha)
        self.assertEqual(receipt["tree"], git_out(self.repo, "rev-parse", sha + "^{tree}"))
        self.assertEqual(receipt["policy_hash"], "policy-1")
        self.assertTrue(receipt["verified"])
        self.assertEqual(receipt["repo_id"], "test-repo")
        self.assertEqual(git_out(self.repo, "rev-parse", "refs/landq/jobs/%s/candidate" % job["id"]),
                         sha)
        rows = q.verifications(job["id"])
        self.assertEqual(len(rows), 1)
        self.assertEqual((rows[0]["candidate_sha"], rows[0]["target_sha"], rows[0]["ok"]),
                         (sha, self.base, True))
        self.assertTrue(Path(rows[0]["log_path"]).read_text().startswith("verified "))
        pending = q.outbox()
        self.assertEqual([r["kind"] for r in pending], ["landed"])
        self.assertEqual(pending[0]["payload"]["published_sha"], sha)
        # The human branch was never touched.
        self.assertEqual(tip(self.repo, "human"), self.base)

    def test_merge_preserves_ancestry_and_verifies_the_merge_commit(self):
        q = self.queue()
        moved = plumb_commit(self.repo, self.base, {"b.txt": "one\ntwo\nthree\nfour\n"}, "other")
        run_git(self.repo, "update-ref", "refs/heads/main", moved, self.base)
        sha = plumb_commit(self.repo, self.base, {"a.txt": "alpha\nbeta\n"}, "mine")
        q.submit(sha, request_id="r1", card="#FW1C")
        verifier = Recorder()
        done = q.process_one(verifier)
        self.assertEqual(done["status"], "landed", done)
        new = tip(self.repo)
        self.assertEqual(new, done["published_sha"])
        self.assertEqual(new, verifier.calls[0][1], "what was verified is what was published")
        parents = git_out(self.repo, "rev-list", "--parents", "-n1", new).split()[1:]
        self.assertEqual(parents, [moved, sha])
        self.assertEqual(show(self.repo, new, "a.txt"), "alpha\nbeta")
        self.assertEqual(show(self.repo, new, "b.txt"), "one\ntwo\nthree\nfour")
        message = git_out(self.repo, "log", "-1", "--format=%B", new)
        self.assertIn("Landq-Submitted: %s" % sha, message)
        self.assertIn("#FW1C", message)
        self.assertNotIn("Reconciled-From", message)
        committer = git_out(self.repo, "log", "-1", "--format=%cn <%ce>", new)
        self.assertEqual(committer, "Relay landq <landq@relay.invalid>")
        self.assertEqual(q.receipt(done["id"])["target_before"], moved)

    def test_already_contained_submission_lands_without_moving_target(self):
        q = self.queue()
        q.submit(self.base, request_id="r1")
        verifier = Recorder()
        done = q.process_one(verifier)
        self.assertEqual(done["status"], "landed")
        self.assertEqual(done["published_sha"], self.base)
        self.assertEqual(verifier.calls, [])
        self.assertIn("already contains", done["reason"])
        self.assertFalse(q.receipt(done["id"])["verified"])

    def test_target_moved_after_verification_reverifies_against_new_tip(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "alpha\nbeta\n"}, "mine")
        q.submit(sha, request_id="r1")
        intruder = {}

        def move_target_once(job, candidate, path):
            if intruder:
                return
            intruder["sha"] = plumb_commit(self.repo, self.base, {"b.txt": "moved\n"}, "intruder")
            run_git(self.repo, "update-ref", "refs/heads/main", intruder["sha"], self.base)

        verifier = Recorder(on_call=move_target_once)
        done = q.process_one(verifier)
        self.assertEqual(done["status"], "landed", done)
        self.assertEqual(len(verifier.calls), 2, "the moved target forced a second verification")
        self.assertEqual(verifier.calls[0][1], sha, "first candidate was the fast-forward")
        new = tip(self.repo)
        self.assertEqual(verifier.calls[1][1], new, "second candidate is what landed")
        parents = git_out(self.repo, "rev-list", "--parents", "-n1", new).split()[1:]
        self.assertEqual(parents, [intruder["sha"], sha])
        self.assertEqual(show(self.repo, new, "b.txt"), "moved")
        self.assertEqual(show(self.repo, new, "a.txt"), "alpha\nbeta")
        rows = q.verifications(done["id"])
        self.assertEqual([r["target_sha"] for r in rows], [self.base, intruder["sha"]])
        self.assertEqual(q.receipt(done["id"])["target_before"], intruder["sha"])
        self.assertEqual(done["attempts"], 2)

    def test_gate_failure_publishes_nothing(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "broken\n"})
        job = q.submit(sha, request_id="r1")
        done = q.process_one(Recorder(ok=False))
        self.assertEqual(done["status"], "failed")
        self.assertEqual(done["reason"], "gate said no")
        self.assertEqual(tip(self.repo), self.base)
        self.assertIsNone(q.receipt(job["id"]))
        rows = q.verifications(job["id"])
        self.assertEqual(len(rows), 1)
        self.assertFalse(rows[0]["ok"])
        self.assertEqual([r["kind"] for r in q.outbox()], ["failed"])
        # A failed job is not picked up again.
        self.assertIsNone(q.process_one(Recorder()))

    def test_verifier_that_raises_fails_the_job(self):
        q = self.queue()
        job = q.submit(plumb_commit(self.repo, self.base, {"a.txt": "x\n"}), request_id="r1")

        def explode(j, sha, path):
            raise OSError("no disk")

        done = q.process_one(explode)
        self.assertEqual(done["status"], "failed")
        self.assertIn("OSError: no disk", done["reason"])
        self.assertEqual(tip(self.repo), self.base)
        self.assertEqual(q.verifications(job["id"]), [], "no verdict was recorded")
        self.assertEqual([n["kind"] for n in q.outbox()], ["failed"])
        self.assertIsNone(q.process_one(explode), "not picked up again")

    def test_attached_target_is_never_moved(self):
        attached_repo = make_repo(self.root / "attached", attached=True)
        base = tip(attached_repo)
        q = Queue(attached_repo, state_root=self.state, repo_id="attached")
        sha = plumb_commit(attached_repo, base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        verifier = Recorder()
        with self.assertRaises(landq.TargetAttached) as ctx:
            q.process_one(verifier)
        self.assertIn(str(attached_repo.resolve()), str(ctx.exception))
        self.assertEqual(verifier.calls, [])
        self.assertEqual(tip(attached_repo), base)
        self.assertEqual(q.status(job["id"])["status"], "queued")
        # Move the human checkout off the target (what cutover does) and it publishes.
        run_git(attached_repo, "switch", "-q", "-c", "human")
        self.assertEqual(q.process_one(verifier)["status"], "landed")
        self.assertEqual(tip(attached_repo), sha)

    # ------------------------------------------------------------ conflicts

    def test_conflict_without_reconcile_reports_author_required(self):
        q = self.queue()
        theirs = plumb_commit(self.repo, self.base, {"a.txt": "theirs\n"}, "theirs")
        run_git(self.repo, "update-ref", "refs/heads/main", theirs, self.base)
        mine = plumb_commit(self.repo, self.base, {"a.txt": "mine\n"}, "mine")
        job = q.submit(mine, request_id="r1", card="#FW1C")
        verifier = Recorder()
        done = q.process_one(verifier)
        self.assertEqual(done["status"], "conflict")
        self.assertIn("a.txt", done["reason"])
        self.assertEqual(verifier.calls, [])
        self.assertEqual(tip(self.repo), theirs)
        notes = q.outbox()
        self.assertEqual([n["kind"] for n in notes], ["author_required"])
        self.assertEqual(notes[0]["payload"]["conflicts"], ["a.txt"])
        self.assertEqual(notes[0]["payload"]["card"], "#FW1C")
        log = self.state / "integration" / "test-repo" / "logs" / job["id"] / "merge.log"
        self.assertIn("a.txt", log.read_text())

    def test_reconcile_resolution_is_committed_verified_and_published(self):
        q = self.queue()
        theirs = plumb_commit(self.repo, self.base, {"a.txt": "theirs\n"}, "theirs")
        run_git(self.repo, "update-ref", "refs/heads/main", theirs, self.base)
        mine = plumb_commit(self.repo, self.base, {"a.txt": "mine\n"}, "mine")
        q.submit(mine, request_id="r1", card="#FW1C")
        contexts = []

        def reconcile(context):
            contexts.append(context)
            path = Path(context["candidate_path"]) / "a.txt"
            self.assertIn("<<<<<<<", path.read_text())
            path.write_text("theirs and mine\n")
            return {"status": "resolved", "trailer": "Reconciled-By: fake-model"}

        verifier = Recorder()
        done = q.process_one(verifier, reconcile=reconcile)
        self.assertEqual(done["status"], "landed", done)
        ctx = contexts[0]
        self.assertEqual(ctx["conflicts"], ["a.txt"])
        self.assertEqual((ctx["target_sha"], ctx["submitted_sha"], ctx["base_sha"]),
                         (theirs, mine, self.base))
        self.assertEqual(ctx["cards"], ["#FW1C"])
        self.assertEqual(ctx["repo_id"], "test-repo")
        self.assertEqual(ctx["diagnostics"]["kind"], "merge_conflict")
        self.assertIn("a.txt", ctx["diagnostics"]["merge_output"])
        new = tip(self.repo)
        self.assertEqual(verifier.calls[0][1], new)
        self.assertEqual(show(self.repo, new, "a.txt"), "theirs and mine")
        message = git_out(self.repo, "log", "-1", "--format=%B", new)
        self.assertIn("Reconciled-From: %s %s" % (theirs, mine), message)
        self.assertIn("Reconciled-By: fake-model", message)
        parents = git_out(self.repo, "rev-list", "--parents", "-n1", new).split()[1:]
        self.assertEqual(parents, [theirs, mine])
        # The author's submission and branches are untouched.
        self.assertEqual(show(self.repo, mine, "a.txt"), "mine")

    def test_reconcile_author_required_or_markers_left_is_a_conflict(self):
        q = self.queue()
        theirs = plumb_commit(self.repo, self.base, {"a.txt": "theirs\n"}, "theirs")
        run_git(self.repo, "update-ref", "refs/heads/main", theirs, self.base)
        mine = plumb_commit(self.repo, self.base, {"a.txt": "mine\n"}, "mine")
        q.submit(mine, request_id="r1")
        q.submit(plumb_commit(self.repo, self.base, {"a.txt": "mine2\n"}, "mine2"),
                 request_id="r2")
        q.submit(plumb_commit(self.repo, self.base, {"a.txt": "mine3\n"}, "mine3"),
                 request_id="r3")
        verifier = Recorder()
        done = q.process_one(verifier, reconcile=lambda ctx: {"status": "author_required",
                                                              "reason": "too risky"})
        self.assertEqual((done["status"], done["reason"]), ("conflict", "too risky"))

        def leaves_markers(ctx):
            return {"status": "resolved"}  # touched nothing: markers stay

        done = q.process_one(verifier, reconcile=leaves_markers)
        self.assertEqual(done["status"], "conflict")
        self.assertIn("left conflict markers in a.txt", done["reason"])

        def raises(ctx):
            raise RuntimeError("model unavailable")

        done = q.process_one(verifier, reconcile=raises)
        self.assertEqual(done["status"], "conflict")
        self.assertIn("model unavailable", done["reason"])
        self.assertEqual(verifier.calls, [])
        self.assertEqual(tip(self.repo), theirs)
        self.assertEqual(len([n for n in q.outbox() if n["kind"] == "author_required"]), 3)

    def test_reconcile_may_name_a_commit_but_the_queue_makes_the_candidate(self):
        q = self.queue()
        theirs = plumb_commit(self.repo, self.base, {"a.txt": "theirs\n"}, "theirs")
        run_git(self.repo, "update-ref", "refs/heads/main", theirs, self.base)
        mine = plumb_commit(self.repo, self.base, {"a.txt": "mine\n"}, "mine")
        q.submit(mine, request_id="r1")
        resolved = plumb_commit(self.repo, theirs, {"a.txt": "resolved\n"}, "by model")

        def reconcile(ctx):
            return {"status": "resolved", "candidate_sha": resolved}

        verifier = Recorder()
        done = q.process_one(verifier, reconcile=reconcile)
        self.assertEqual(done["status"], "landed", done)
        new = tip(self.repo)
        self.assertNotEqual(new, resolved, "the reconciler's commit is not published as-is")
        self.assertEqual(show(self.repo, new, "a.txt"), "resolved")
        parents = git_out(self.repo, "rev-list", "--parents", "-n1", new).split()[1:]
        self.assertEqual(parents, [theirs, mine])

    # ------------------------------------------------------------ gate-failure repair

    def test_failed_gate_is_repaired_once_and_reverified(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "brkoen\n"}, "typo")
        job = q.submit(sha, request_id="r1", card="#FW1C")
        contexts = []

        def gate(j, candidate, path):
            text = (Path(path) / "a.txt").read_text()
            ok = text == "broken\n"
            return {"ok": ok, "policy_hash": "p1", "verified": ok,
                    "log": "spelling check on a.txt: %r" % text,
                    **({} if ok else {"reason": "a.txt misspelt"})}

        def reconcile(context):
            contexts.append(context)
            (Path(context["candidate_path"]) / "a.txt").write_text("broken\n")
            return {"status": "resolved", "trailer": "Reconciled-By: fake-model"}

        done = q.process_one(gate, reconcile=reconcile)
        self.assertEqual(done["status"], "landed", done)
        self.assertEqual(len(contexts), 1)
        diag = contexts[0]["diagnostics"]
        self.assertEqual((diag["kind"], diag["round"], diag["candidate_sha"], diag["reason"]),
                         ("gate_failure", 1, sha, "a.txt misspelt"))
        self.assertIn("spelling check", diag["log"])
        self.assertEqual(contexts[0]["conflicts"], [])
        new = tip(self.repo)
        self.assertNotEqual(new, sha)
        self.assertEqual(show(self.repo, new, "a.txt"), "broken")
        parents = git_out(self.repo, "rev-list", "--parents", "-n1", new).split()[1:]
        self.assertEqual(parents, [sha], "a fast-forward repair is a child of the submission")
        message = git_out(self.repo, "log", "-1", "--format=%B", new)
        self.assertIn("Landq-Repair: 1 of %s" % sha, message)
        self.assertIn("Reconciled-From: %s %s" % (self.base, sha), message)
        self.assertIn("Reconciled-By: fake-model", message)
        rows = q.verifications(job["id"])
        self.assertEqual([(r["candidate_sha"], r["ok"]) for r in rows],
                         [(sha, False), (new, True)], "the repaired candidate was gated again")
        receipt = q.receipt(job["id"])
        self.assertEqual((receipt["published_sha"], receipt["submitted_sha"], receipt["verified"]),
                         (new, sha, True))
        self.assertEqual(git_out(self.repo, "rev-parse", "refs/landq/jobs/%s/repair-1" % job["id"]),
                         new)
        self.assertEqual([n["kind"] for n in q.outbox()], ["landed"])

    def test_repair_of_a_merge_keeps_both_parents(self):
        q = self.queue()
        moved = plumb_commit(self.repo, self.base, {"b.txt": "moved\n"}, "other")
        run_git(self.repo, "update-ref", "refs/heads/main", moved, self.base)
        sha = plumb_commit(self.repo, self.base, {"a.txt": "brkoen\n"}, "typo")
        q.submit(sha, request_id="r1")
        gate = lambda j, c, p: {"ok": (Path(p) / "a.txt").read_text() == "broken\n",  # noqa: E731
                                "policy_hash": "p1", "verified": True, "log": "", "reason": "typo"}

        def reconcile(context):
            (Path(context["candidate_path"]) / "a.txt").write_text("broken\n")
            return {"status": "resolved"}

        done = q.process_one(gate, reconcile=reconcile)
        self.assertEqual(done["status"], "landed", done)
        new = tip(self.repo)
        parents = git_out(self.repo, "rev-list", "--parents", "-n1", new).split()[1:]
        self.assertEqual(parents, [moved, sha])
        self.assertEqual(show(self.repo, new, "b.txt"), "moved")
        self.assertEqual(show(self.repo, new, "a.txt"), "broken")

    def test_repair_that_fails_the_gate_again_is_bounded(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "brkoen\n"}, "typo")
        job = q.submit(sha, request_id="r1", card="#FW1C")
        gate = Recorder(ok=False)
        rounds = []

        def reconcile(context):
            rounds.append(context["diagnostics"]["round"])
            (Path(context["candidate_path"]) / "a.txt").write_text("still wrong %d\n" % len(rounds))
            return {"status": "resolved"}

        done = q.process_one(gate, reconcile=reconcile)
        self.assertEqual(done["status"], "failed", done)
        self.assertEqual(rounds, [1], "one repair round by default")
        self.assertEqual(len(gate.calls), 2, "original and repaired candidate, nothing more")
        self.assertEqual(tip(self.repo), self.base)
        self.assertIsNone(q.receipt(job["id"]))
        kinds = sorted(n["kind"] for n in q.outbox())
        self.assertEqual(kinds, ["author_required", "failed"])
        author = [n for n in q.outbox() if n["kind"] == "author_required"][0]["payload"]
        self.assertEqual(len(author["verifications"]), 2)
        self.assertEqual(author["reason"], "gate said no")
        # Not picked up again, and a wider bound is honoured exactly.
        self.assertIsNone(q.process_one(gate, reconcile=reconcile))
        q.submit(sha, request_id="r2")
        gate2 = Recorder(ok=False)
        rounds.clear()
        done = q.process_one(gate2, reconcile=reconcile, reconcile_attempts=2)
        self.assertEqual((done["status"], rounds, len(gate2.calls)), ("failed", [1, 2], 3))

    def test_repair_refusals_end_as_failed_with_author_required(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "brkoen\n"}, "typo")
        cases = {
            "r1": lambda ctx: {"status": "author_required", "reason": "not sure"},
            "r2": lambda ctx: {"status": "resolved"},  # changed nothing
            "r3": lambda ctx: (_ for _ in ()).throw(RuntimeError("model down")),
        }
        for request_id, reconcile in cases.items():
            gate = Recorder(ok=False)
            q.submit(sha, request_id=request_id)
            done = q.process_one(gate, reconcile=reconcile)
            self.assertEqual(done["status"], "failed", (request_id, done))
            self.assertTrue(done["reason"].startswith("gate said no; reconcile: "), done["reason"])
            self.assertEqual(len(gate.calls), 1, "no second gate run without a new candidate")
        reasons = [n["payload"]["reason"] for n in q.outbox() if n["kind"] == "author_required"]
        self.assertEqual(len(reasons), 3)
        self.assertTrue(any("not sure" in r for r in reasons))
        self.assertTrue(any("changed nothing" in r for r in reasons))
        self.assertTrue(any("model down" in r for r in reasons))
        self.assertEqual(tip(self.repo), self.base)
        # No reconcile at all: plain failure, no author_required.
        q.submit(sha, request_id="r4")
        q.process_one(Recorder(ok=False))
        self.assertEqual(len([n for n in q.outbox() if n["kind"] == "author_required"]), 3)

    def test_repair_of_a_metadata_job_cannot_escape_the_board(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {".board/card.md": "bad\n"})
        q.submit(sha, request_id="m1", kind="metadata")

        def reconcile(context):
            (Path(context["candidate_path"]) / "a.txt").write_text("code change\n")
            return {"status": "resolved"}

        gate = Recorder(ok=False)
        done = q.process_one(gate, reconcile=reconcile)
        self.assertEqual(done["status"], "failed")
        self.assertIn("a.txt: outside .board/", done["reason"])
        self.assertEqual(len(gate.calls), 1)
        self.assertEqual(tip(self.repo), self.base)

    def test_tampering_and_raising_verifiers_are_not_repaired(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        q.submit(sha, request_id="r1")
        q.submit(sha, request_id="r2")
        called = []
        reconcile = lambda ctx: called.append(ctx) or {"status": "resolved"}  # noqa: E731

        def tamper(j, c, p):
            (Path(p) / "a.txt").write_text("patched\n")

        self.assertEqual(q.process_one(Recorder(on_call=tamper), reconcile=reconcile)["status"],
                         "failed")

        def explode(j, c, p):
            raise OSError("no disk")

        self.assertEqual(q.process_one(explode, reconcile=reconcile)["status"], "failed")
        self.assertEqual(called, [])

    # ------------------------------------------------------------ metadata jobs

    def test_metadata_job_accepts_board_only_changes(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {".board/card.md": "# card\nstatus: done\n",
                                                  ".board/threads/X.md": "entry\n"})
        q.submit(sha, request_id="m1", kind="metadata")
        done = q.process_one(Recorder())
        self.assertEqual(done["status"], "landed", done)
        self.assertEqual(tip(self.repo), sha)

    def test_metadata_job_cannot_touch_code_or_add_symlinks(self):
        q = self.queue()
        code = plumb_commit(self.repo, self.base, {".board/card.md": "x\n", "a.txt": "hacked\n"})
        link = plumb_commit(self.repo, self.base, {".board/link": "../a.txt"},
                            modes={".board/link": "120000"})
        q.submit(code, request_id="m1", kind="metadata")
        q.submit(link, request_id="m2", kind="metadata")
        verifier = Recorder()
        first = q.process_one(verifier)
        self.assertEqual(first["status"], "conflict")
        self.assertIn("a.txt: outside .board/", first["reason"])
        second = q.process_one(verifier)
        self.assertEqual(second["status"], "conflict")
        self.assertIn("symlink", second["reason"])
        self.assertEqual(verifier.calls, [], "refused before any verifier ran")
        self.assertEqual(tip(self.repo), self.base)
        # The same commit as a code job is fine: the restriction is the kind's.
        q.submit(code, request_id="c1", kind="code")
        self.assertEqual(q.process_one(verifier)["status"], "landed")

    def test_metadata_merge_is_checked_after_the_merge(self):
        q = self.queue()
        moved = plumb_commit(self.repo, self.base, {"b.txt": "moved\n"}, "code moved")
        run_git(self.repo, "update-ref", "refs/heads/main", moved, self.base)
        sha = plumb_commit(self.repo, self.base, {".board/card.md": "# card\nstatus: done\n"})
        q.submit(sha, request_id="m1", kind="metadata")
        done = q.process_one(Recorder())
        self.assertEqual(done["status"], "landed", done)
        self.assertEqual(show(self.repo, tip(self.repo), "b.txt"), "moved")
        self.assertEqual(show(self.repo, tip(self.repo), ".board/card.md"),
                         "# card\nstatus: done")

    # ------------------------------------------------------------ cancel

    def test_cancel_queued_and_landed(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        cancelled = q.cancel(job["id"])
        self.assertEqual(cancelled["status"], "cancelled")
        self.assertIsNone(q.process_one(Recorder()))
        self.assertEqual(tip(self.repo), self.base)
        job2 = q.submit(sha, request_id="r2")
        landed = q.process_one(Recorder())
        self.assertEqual(landed["status"], "landed")
        after = q.cancel(job2["id"])
        self.assertEqual(after["status"], "landed", "a publication is never undone")
        self.assertEqual(tip(self.repo), sha)

    def test_cancel_during_verification_stops_before_intent(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        other = self.queue()

        def cancel_from_elsewhere(j, candidate, path):
            asked = other.cancel(j["id"])
            self.assertEqual(asked["status"], "verifying")
            self.assertTrue(asked["cancel_requested"])

        done = q.process_one(Recorder(on_call=cancel_from_elsewhere))
        self.assertEqual(done["status"], "cancelled", done)
        self.assertEqual(tip(self.repo), self.base)
        self.assertIsNone(q.receipt(job["id"]))
        self.assertEqual(len(q.verifications(job["id"])), 1, "the verification still persisted")

    # ------------------------------------------------------------ crashes and recovery

    def test_crash_before_update_ref_requeues_and_reverifies(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        q.faults.add("before_update_ref")
        verifier = Recorder()
        with self.assertRaises(landq.InjectedCrash):
            q.process_one(verifier)
        self.assertEqual(q.status(job["id"])["status"], "publishing", "intent was durable")
        self.assertEqual(tip(self.repo), self.base)
        # A new process comes up and recovers.
        fresh = self.queue()
        recovered = fresh.recover()
        self.assertEqual([j["status"] for j in recovered], ["interrupted"])
        self.assertIsNone(recovered[0]["candidate_sha"], "no stale verification pass")
        self.assertIsNone(fresh.receipt(job["id"]))
        done = fresh.process_one(verifier)
        self.assertEqual(done["status"], "landed")
        self.assertEqual(len(verifier.calls), 2, "verified again after the interruption")
        self.assertEqual(tip(self.repo), sha)

    def test_crash_after_update_ref_is_recognised_as_landed(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        q.faults.add("after_update_ref")
        verifier = Recorder()
        with self.assertRaises(landq.InjectedCrash):
            q.process_one(verifier)
        self.assertEqual(tip(self.repo), sha, "the ref moved before the crash")
        self.assertEqual(q.status(job["id"])["status"], "publishing")
        self.assertIsNone(q.receipt(job["id"]))
        fresh = self.queue()
        recovered = fresh.recover()
        self.assertEqual([j["status"] for j in recovered], ["landed"])
        receipt = fresh.receipt(job["id"])
        self.assertEqual(receipt["published_sha"], sha)
        self.assertEqual(receipt["target_before"], self.base)
        self.assertTrue(receipt["verified"])
        self.assertEqual(len(verifier.calls), 1, "nothing was verified twice")
        self.assertIsNone(fresh.process_one(verifier))

    def test_crash_after_update_ref_recognised_even_when_target_moved_on(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        q.faults.add("after_update_ref")
        with self.assertRaises(landq.InjectedCrash):
            q.process_one(Recorder())
        later = plumb_commit(self.repo, sha, {"b.txt": "later\n"}, "later")
        run_git(self.repo, "update-ref", "refs/heads/main", later, sha)
        recovered = self.queue().recover()
        self.assertEqual([j["status"] for j in recovered], ["landed"])
        self.assertEqual(self.queue().receipt(job["id"])["published_sha"], sha)

    def test_recover_recognises_a_publishing_job_whose_ref_never_moved(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        # Simulate a death mid-verification: the row says verifying, nothing else happened.
        with q._tx() as conn:
            q._set(conn, job["id"], status="verifying", target_sha=self.base,
                   candidate_sha=sha, candidate_tree="deadbeef")
        recovered = q.recover()
        self.assertEqual([(j["status"], j["candidate_sha"]) for j in recovered],
                         [("interrupted", None)])
        self.assertEqual(q.process_one(Recorder())["status"], "landed")

    def test_ready_job_with_intact_verification_publishes_without_reverifying(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        q.faults.add("before_update_ref")
        verifier = Recorder()
        with self.assertRaises(landq.InjectedCrash):
            q.process_one(verifier)
        # Put the job back to `ready` by hand (B1's split verify/publish phases do this): the
        # bound verification is reused only when the caller names the policy in force and it
        # matches, and the target did not move.
        with q._tx() as conn:
            q._set(conn, job["id"], status="ready")
        q.faults.clear()
        done = q.process_one(verifier, accepted_policy_hash="policy-1")
        self.assertEqual(done["status"], "landed")
        self.assertEqual(len(verifier.calls), 1)

    def test_ready_job_is_reverified_unless_the_policy_matches(self):
        for accepted in (None, "policy-2"):
            with self.subTest(accepted=accepted):
                repo = make_repo(self.root / ("ready-%s" % accepted))
                base = tip(repo)
                q = Queue(repo, state_root=self.state, repo_id="ready-%s" % accepted)
                sha = plumb_commit(repo, base, {"a.txt": "x\n"})
                job = q.submit(sha, request_id="r1")
                q.faults.add("before_update_ref")
                verifier = Recorder()
                with self.assertRaises(landq.InjectedCrash):
                    q.process_one(verifier)
                with q._tx() as conn:
                    q._set(conn, job["id"], status="ready")
                q.faults.clear()
                done = q.process_one(verifier, accepted_policy_hash=accepted)
                self.assertEqual(done["status"], "landed")
                self.assertEqual(len(verifier.calls), 2, "the old pass was not trusted")
                self.assertEqual(tip(repo), sha)

    def test_verifier_that_alters_the_candidate_does_not_count(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")

        def patch_then_pass(j, candidate, path):
            (Path(path) / "a.txt").write_text("fixed by the gate\n")

        done = q.process_one(Recorder(on_call=patch_then_pass))
        self.assertEqual(done["status"], "failed", done)
        self.assertIn("a.txt", done["reason"])
        self.assertIn("cannot be bound", done["reason"])
        self.assertEqual(tip(self.repo), self.base)
        rows = q.verifications(job["id"])
        self.assertEqual([(r["ok"], r["verified"]) for r in rows], [(False, False)])

        def move_head(j, candidate, path):
            run_git(path, "checkout", "-q", "--detach", self.base)

        q.submit(sha, request_id="r2")
        done = q.process_one(Recorder(on_call=move_head))
        self.assertEqual(done["status"], "failed")
        self.assertIn("HEAD is", done["reason"])
        # Untracked build output is not drift.
        q.submit(sha, request_id="r3")
        done = q.process_one(Recorder(on_call=lambda j, c, p: (Path(p) / "build.o").write_text("o")))
        self.assertEqual(done["status"], "landed")

    def test_dispose_workspace_keeps_everything_durable(self):
        q = self.queue()
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        job = q.submit(sha, request_id="r1")
        q.process_one(Recorder())
        wt = q.service_dir / "candidate"
        self.assertTrue(wt.exists())
        self.assertTrue(q.dispose_workspace()["removed"])
        self.assertFalse(wt.exists())
        self.assertNotIn(str(wt), git_out(self.repo, "worktree", "list"))
        self.assertIsNotNone(q.receipt(job["id"]))
        self.assertEqual(git_out(self.repo, "rev-parse", "refs/landq/jobs/%s/candidate" % job["id"]),
                         sha)
        # The next job rebuilds it.
        q.submit(plumb_commit(self.repo, sha, {"a.txt": "y\n"}), request_id="r2")
        self.assertEqual(q.process_one(Recorder())["status"], "landed")
        self.assertFalse(q.dispose_workspace()["removed"] is None)

    # ------------------------------------------------------------ competing processes

    def test_publisher_lock_refuses_a_second_process(self):
        q = self.queue()
        q.submit(plumb_commit(self.repo, self.base, {"a.txt": "x\n"}), request_id="r1")
        lock = q.lock_path
        holder = subprocess.Popen([sys.executable, "-c", textwrap.dedent("""
            import fcntl, os, sys, time
            fd = os.open(sys.argv[1], os.O_CREAT | os.O_RDWR)
            fcntl.flock(fd, fcntl.LOCK_EX)
            print("held", flush=True)
            time.sleep(30)
            """), str(lock)], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(holder.stdout.readline().strip(), "held")
            verifier = Recorder()
            with self.assertRaises(landq.PublisherBusy):
                q.process_one(verifier)
            with self.assertRaises(landq.PublisherBusy):
                q.recover()
            self.assertEqual(verifier.calls, [])
            self.assertEqual(q.status()[0]["status"], "queued")
            # With a wait, the same call outlasts the holder.
            started = time.monotonic()
            holder.terminate()
            holder.wait(5)
            waited = Queue(self.repo, state_root=self.state, repo_id="test-repo", lock_timeout=5)
            self.assertEqual(waited.process_one(verifier)["status"], "landed")
            self.assertLess(time.monotonic() - started, 5)
        finally:
            if holder.poll() is None:
                holder.kill()
                holder.wait()

    def test_two_processes_publish_in_series(self):
        q = self.queue()
        one = plumb_commit(self.repo, self.base, {"a.txt": "one\n"}, "one")
        two = plumb_commit(self.repo, self.base, {"b.txt": "two\n"}, "two")
        q.submit(one, request_id="r1")
        q.submit(two, request_id="r2")
        script = textwrap.dedent("""
            import sys, time
            sys.path.insert(0, sys.argv[1])
            from relay_core.landq import Queue
            q = Queue(sys.argv[2], state_root=sys.argv[3], repo_id="test-repo", lock_timeout=20)
            def verifier(job, sha, path):
                time.sleep(0.5)  # long enough for the other process to contend for the lock
                return {"ok": True, "policy_hash": "p", "verified": True, "log": ""}
            job = q.process_one(verifier)
            print(job["status"] if job else "none")
            """)
        args = [sys.executable, "-c", script, str(ROOT / "backend"), str(self.repo),
                str(self.state)]
        procs = [subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                 for _ in range(2)]
        outputs = [p.communicate(timeout=60) for p in procs]
        for out, err in outputs:
            self.assertEqual(out.strip(), "landed", err)
        statuses = sorted(j["status"] for j in q.status())
        self.assertEqual(statuses, ["landed", "landed"])
        new = tip(self.repo)
        self.assertEqual(show(self.repo, new, "a.txt"), "one")
        self.assertEqual(show(self.repo, new, "b.txt"), "two")
        # Second landing was a merge onto the first: a linear ancestry of two publications.
        log = git_out(self.repo, "rev-list", "--first-parent", new).splitlines()
        self.assertEqual(len(log), 3)
        self.assertEqual(sorted(r["published_sha"] for r in
                                (q.receipt(j["id"]) for j in q.status())), sorted([log[1], new]))

    # ------------------------------------------------------------ outbox

    def test_outbox_delivery_retries_with_backoff(self):
        q = self.queue()
        q.submit(plumb_commit(self.repo, self.base, {"a.txt": "x\n"}), request_id="r1")
        q.process_one(Recorder())
        attempts = []

        def flaky(record):
            attempts.append(record["kind"])
            if len(attempts) == 1:
                raise RuntimeError("smtp down")

        first = q.deliver_outbox(flaky)
        self.assertEqual([r["delivered"] for r in first], [False])
        self.assertIn("smtp down", q.outbox()[0]["last_error"])
        self.assertEqual(q.deliver_outbox(flaky), [], "not due yet")
        second = q.deliver_outbox(flaky, now=time.time() + 60)
        self.assertEqual([r["delivered"] for r in second], [True])
        self.assertEqual(q.outbox(), [])
        self.assertEqual(len(q.outbox(pending_only=False)), 1)
        self.assertEqual(attempts, ["landed", "landed"])

    # ------------------------------------------------------------ identity

    def test_identity_comes_from_the_trees_registry(self):
        """With A1's registry importable the queue uses it; without it (a checkout where
        trees.py has not landed) the hashed common dir stands in. Either way, a registry that
        exists but fails is a refusal, and what the registry answers is what the queue uses."""
        from unittest import mock
        import relay_core
        try:
            from relay_core import trees
        except ImportError:
            trees = None
        a = Queue(self.repo, state_root=self.state)
        b = Queue(self.repo, state_root=self.state)
        self.assertEqual(a.repo_id, b.repo_id)
        self.assertEqual(a.target, "main")
        if trees is not None:
            self.assertEqual(a.identity["identity_source"], "trees")
            self.assertEqual(trees.resolve_project(self.repo, state_root=self.state)["id"],
                             a.repo_id)
        else:
            self.assertEqual(a.identity["identity_source"], "fallback-hash: no trees module")
            self.assertEqual(len(a.repo_id), 16)
        import types

        def with_trees(module):
            # `from relay_core import trees` reads the package attribute when the module was
            # already imported, and sys.modules otherwise: patch both, creating the attribute
            # when trees.py is not in this checkout.
            return (mock.patch.dict(sys.modules, {"relay_core.trees": module}),
                    mock.patch.object(relay_core, "trees", module, create=True))

        # A registry that exists but fails is a refusal, never a second identity.
        broken = types.SimpleNamespace(
            resolve_project=lambda path, state_root=None: None,
            register_repo=lambda path, state_root=None: (_ for _ in ()).throw(
                RuntimeError("registry locked")))
        p1, p2 = with_trees(broken)
        with p1, p2:
            with self.assertRaises(landq.Refused) as ctx:
                Queue(self.repo, state_root=self.state)
        self.assertIn("registry locked", str(ctx.exception))
        # What the registry answers is what the queue uses, target included.
        fake = types.SimpleNamespace(
            resolve_project=lambda path, state_root=None: None,
            register_repo=lambda path, state_root=None: {"id": "R1", "target": "trunk"})
        p1, p2 = with_trees(fake)
        with p1, p2:
            c = Queue(self.repo, state_root=self.state)
        self.assertEqual((c.repo_id, c.target, c.identity["identity_source"]),
                         ("R1", "trunk", "trees"))
        self.assertTrue((self.state / "integration" / "R1").is_dir())
        # Only a missing module falls back to the hashed common dir.
        p1, p2 = with_trees(None)
        with p1, p2:
            d = Queue(self.repo, state_root=self.state)
        self.assertEqual(d.identity["identity_source"], "fallback-hash: no trees module")
        self.assertEqual(len(d.repo_id), 16)

    # ------------------------------------------------------------ CLI

    def test_cli_round_trip_and_exit_codes(self):
        sha = plumb_commit(self.repo, self.base, {"a.txt": "x\n"})
        common = ["--repo", str(self.repo), "--state-root", str(self.state), "--repo-id", "cli"]

        def cli(*args):
            out = io.StringIO()
            code = landq.main([*common, *args], out=out)
            return code, json.loads(out.getvalue())

        code, job = cli("submit", sha, "--request-id", "r1", "--card", "#FW1C", "--test", "t1")
        self.assertEqual(code, 0)
        self.assertEqual((job["status"], job["selected_tests"]), ("queued", ["t1"]))
        code, listed = cli("status")
        self.assertEqual((code, [j["id"] for j in listed]), (0, [job["id"]]))
        code, one = cli("status", job["id"])
        self.assertEqual((code, one["id"]), (0, job["id"]))
        code, err = cli("submit", "0" * 40, "--request-id", "r2")
        self.assertEqual((code, err["kind"]), (2, "Refused"))
        code, err = cli("receipt", job["id"])
        self.assertEqual(code, 2)
        code, cancelled = cli("cancel", job["id"])
        self.assertEqual((code, cancelled["status"]), (0, "cancelled"))
        code, recovered = cli("recover")
        self.assertEqual((code, recovered), (0, []))
        code, pending = cli("outbox")
        self.assertEqual((code, [n["kind"] for n in pending]), (0, ["cancelled"]))
        self.assertEqual(landq.main([], out=io.StringIO()), 1)
        # The installed script runs the same code.
        proc = subprocess.run([sys.executable, str(ROOT / "scripts" / "relay-land"), *common,
                               "status", job["id"]], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["status"], "cancelled")

    def test_exit_codes_match_the_contract(self):
        self.assertEqual(landq.UsageError.exit_code, 1)
        self.assertEqual(landq.Refused.exit_code, 2)
        self.assertEqual(landq.ConflictError.exit_code, 3)
        self.assertEqual(landq.GateFailed.exit_code, 5)
        self.assertEqual(landq.PublisherBusy.exit_code, 7)


if __name__ == "__main__":
    unittest.main()
