"""Automatic AI reconciliation of landing conflicts (card #P9ZA, A4 of docs/TREES-AND-LANDING.md).

Every test builds a real temporary Git repository: a base commit, a target branch (ours), a
submitted branch (theirs) and a *candidate* worktree in which `git merge` has stopped on the
conflict, exactly what the landing queue hands the reconciler. Models are injected
(`model_call`), or driven through the real adapters against `tests/guest_harness_fake.FakeHarness`
and a loopback OpenAI-compatible server — nothing here starts a real guest or reads a keyring.
"""
from __future__ import annotations

import asyncio
import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock

from relay_core import reconcile as R
from relay_core import roles as model_roles
from relay_core.guest_harness import HarnessStart
from relay_core.provider import ProviderError

sys.path.insert(0, str(Path(__file__).parent))
from guest_harness_fake import FakeHarness, ev  # noqa: E402

BASE_APP = """import math


def area(r):
    return math.pi * r * r


def perimeter(r):
    return 2 * math.pi * r


def describe(r):
    return f"circle r={r}"
"""
OURS_APP = BASE_APP.replace('    return f"circle r={r}"', '    return f"circle r={r} area={area(r):.2f}"')
THEIRS_APP = BASE_APP.replace('    return f"circle r={r}"',
                              '    if r < 0:\n        raise ValueError("negative radius")\n    return f"circle r={r}"')
MERGED_APP = BASE_APP.replace('    return f"circle r={r}"',
                              '    if r < 0:\n        raise ValueError("negative radius")\n'
                              '    return f"circle r={r} area={area(r):.2f}"')

BASE_TEST = """import unittest

from app import area, describe


class AreaTests(unittest.TestCase):
    def test_area(self):
        self.assertAlmostEqual(area(1), 3.14159, places=4)

    def test_describe(self):
        self.assertEqual(describe(1), "circle r=1")
"""
OURS_TEST = BASE_TEST.replace('self.assertEqual(describe(1), "circle r=1")',
                              'self.assertEqual(describe(1), "circle r=1 area=3.14")')
THEIRS_TEST = BASE_TEST.replace('self.assertEqual(describe(1), "circle r=1")',
                                'self.assertEqual(describe(1), "circle r=1")\n\n'
                                '    def test_negative(self):\n'
                                '        with self.assertRaises(ValueError):\n'
                                '            describe(-1)')
MERGED_TEST = BASE_TEST.replace('self.assertEqual(describe(1), "circle r=1")',
                                'self.assertEqual(describe(1), "circle r=1 area=3.14")\n\n'
                                '    def test_negative(self):\n'
                                '        with self.assertRaises(ValueError):\n'
                                '            describe(-1)')


def git(cwd, *args) -> str:
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update({"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.com",
                "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.com"})
    return subprocess.run(["git", *args], cwd=str(cwd), env=env, capture_output=True, text=True,
                          check=False).stdout.strip()


class Fixture:
    """A repo with a conflict, and the candidate worktree the queue would hand over."""

    def __init__(self, root: Path, files=None):
        self.root = root
        self.repo = root / "repo"
        self.repo.mkdir()
        files = files or {"app.py": (BASE_APP, OURS_APP, THEIRS_APP)}
        git(self.repo, "init", "-q", "-b", "main")
        git(self.repo, "config", "user.email", "t@example.com")
        git(self.repo, "config", "user.name", "t")
        for path, (base, _ours, _theirs) in files.items():
            (self.repo / path).parent.mkdir(parents=True, exist_ok=True)
            (self.repo / path).write_text(base)
        (self.repo / "README.md").write_text("readme\n")
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "base")
        self.base = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "checkout", "-q", "-b", "submitted")
        for path, (_base, _ours, theirs) in files.items():
            (self.repo / path).write_text(theirs)
        git(self.repo, "commit", "-q", "-am", "theirs")
        self.submitted = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "checkout", "-q", "main")
        for path, (_base, ours, _theirs) in files.items():
            (self.repo / path).write_text(ours)
        git(self.repo, "commit", "-q", "-am", "ours")
        self.target = git(self.repo, "rev-parse", "HEAD")
        self.candidate = root / "candidate"
        git(self.repo, "worktree", "add", "-q", "--detach", str(self.candidate), self.target)
        git(self.candidate, "merge", "--no-commit", "--no-ff", self.submitted)
        self.paths = list(files)
        assert git(self.candidate, "ls-files", "-u"), "the fixture must conflict"

    def context(self, **extra) -> dict:
        ctx = {"repo": str(self.repo), "repo_id": "repo-1", "job_id": extra.pop("job_id", "job-1"),
               "base_sha": self.base, "target_sha": self.target, "submitted_sha": self.submitted,
               "candidate_path": str(self.candidate), "cards": [{"id": "AB12", "title": "Negative radii"}],
               "intents": ["describe() refuses a negative radius"], "conflicts": list(self.paths),
               "diagnostics": [], "policy": {"reconcile": {"enabled": True}}}
        ctx.update(extra)
        return ctx


def reply(files: dict, notes: str = "kept both") -> str:
    return json.dumps({"files": [{"path": p, "content": c} for p, c in files.items()], "notes": notes})


def usage(inp=1000, out=200) -> dict:
    return {"prompt_tokens": inp, "completion_tokens": out}


class ReconcileTestCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = self.root / "state"
        env = {"XDG_CONFIG_HOME": str(self.root / "config"), "XDG_STATE_HOME": str(self.state),
               "XDG_DATA_HOME": str(self.root / "data"), "RELAY_KEYRING": "off"}
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)

    def reconciler(self, **kw):
        kw.setdefault("tiers", [{"preset": "kimi", "model": "kimi-k3", "rank": 1}])
        kw.setdefault("key_lookup", lambda preset: "k" if preset == "kimi" else "")
        kw.setdefault("guest_check", lambda guest: False)
        return R.Reconciler(state_root=self.state, **kw)


class ResolveTests(ReconcileTestCase):
    def test_resolved_candidate_keeps_both_sides_and_is_staged(self):
        fx = Fixture(self.root)
        calls = []

        def model(request):
            calls.append(request)
            return reply({"app.py": MERGED_APP}), usage(1234, 321)

        result = self.reconciler().reconcile_sync(fx.context(), model_call=model)
        self.assertEqual(result["status"], "resolved", result)
        self.assertEqual(result["resolved_paths"], ["app.py"])
        self.assertEqual(result["model"], "kimi-k3")
        self.assertEqual(result["preset"], "kimi")
        self.assertEqual(result["effort"], "max")               # the model's top level: high effort
        self.assertEqual(result["tokens"], {"input": 1234, "output": 321, "total": 1555,
                                            "reserved": result["tokens"]["reserved"]})
        self.assertGreater(result["tokens"]["reserved"], 0)
        self.assertEqual(result["trailer"], f"Reconciled-From: {fx.target} {fx.submitted}")
        self.assertEqual((fx.candidate / "app.py").read_text(), MERGED_APP)
        self.assertEqual(git(fx.candidate, "ls-files", "-u"), "", "the resolution is staged")
        self.assertIn("+        raise ValueError", result["patch"])
        self.assertIn("--- a/app.py", result["patch"])
        self.assertEqual(len(result["attempts"]), 1)
        self.assertEqual(result["attempts"][0]["outcome"], "resolved")
        # The author's checkout is untouched: main still holds ours, submitted still holds theirs.
        self.assertEqual(git(fx.repo, "show", f"{fx.target}:app.py"), OURS_APP.rstrip("\n"))
        self.assertEqual(git(fx.repo, "show", f"{fx.submitted}:app.py"), THEIRS_APP.rstrip("\n"))
        # The prompt carried both diffs, the cards and the intent.
        prompt = calls[0]["user"]
        self.assertIn("base → ours", prompt)
        self.assertIn("base → theirs", prompt)
        self.assertIn("#AB12 Negative radii", prompt)
        self.assertIn("describe() refuses a negative radius", prompt)
        self.assertIn("<<<<<<<", prompt)
        self.assertEqual(calls[0]["completion_tokens"], R.DEFAULT_COMPLETION_TOKENS)
        # Recorded durably: one settled reservation with the model, the account and the usage.
        rows = self.reconciler().history("job-1")
        self.assertEqual([(r["status"], r["input_tokens"], r["output_tokens"], r["model"], r["outcome"]) for r in rows],
                         [("settled", 1234, 321, "kimi-k3", "resolved")])
        notes = result["card_notes"]
        self.assertEqual([n["card"] for n in notes], ["AB12"])
        self.assertIn("reconcile:job-1:resolved", notes[0]["text"])
        self.assertIn(result["trailer"], notes[0]["text"])

    def test_async_entry_point_and_conflicts_read_from_the_index(self):
        fx = Fixture(self.root)
        ctx = fx.context()
        ctx["conflicts"] = []                       # the queue named none: the index says
        result = asyncio.run(self.reconciler().reconcile(
            ctx, model_call=lambda req: (reply({"app.py": MERGED_APP}), usage())))
        self.assertEqual(result["status"], "resolved")
        self.assertEqual(result["conflicts"], ["app.py"])

    def test_second_attempt_gets_the_review_feedback_and_lands(self):
        fx = Fixture(self.root)
        calls = []

        def model(request):
            calls.append(request)
            if request["attempt"] == 1:
                return reply({"app.py": MERGED_APP, "README.md": "rewritten\n"}), usage()
            return reply({"app.py": MERGED_APP}), usage()

        result = self.reconciler().reconcile_sync(fx.context(), model_call=model)
        self.assertEqual(result["status"], "resolved", result)
        self.assertEqual([a["outcome"] for a in result["attempts"]], ["rejected", "resolved"])
        self.assertIn("README.md: outside the conflicted paths", result["attempts"][0]["diagnostics"][0])
        self.assertIn("rejected by the safety review", calls[1]["user"])
        self.assertIn("README.md: outside the conflicted paths", calls[1]["user"])
        self.assertEqual((fx.candidate / "README.md").read_text(), "readme\n", "never written")
        rows = self.reconciler().history("job-1")
        self.assertEqual([r["outcome"] for r in rows], ["rejected", "resolved"])


class RefusalTests(ReconcileTestCase):
    def test_dropping_the_submission_returns_to_the_author_with_the_patch(self):
        fx = Fixture(self.root)
        proposed = OURS_APP + "\n\n# reconciled: kept the area\n"
        result = self.reconciler().reconcile_sync(fx.context(), model_call=lambda req: (reply({"app.py": proposed}), usage()))
        self.assertEqual(result["status"], "author_required")
        self.assertEqual([a["outcome"] for a in result["attempts"]], ["rejected", "rejected"])
        self.assertIn("submission's", result["reason"])
        self.assertIn("raise ValueError", " ".join(result["diagnostics"]))
        self.assertTrue(result["patch"].startswith("--- a/app.py"), "the rejected patch rides along")
        self.assertIn("<<<<<<<", (fx.candidate / "app.py").read_text(), "the candidate is untouched")
        self.assertIn("Sync your workspace", result["handoff"])
        self.assertIn("attempt 2: kimi-k3", result["handoff"])
        self.assertIn("reconcile:job-1:author_required", result["card_notes"][0]["text"])
        rows = self.reconciler().history("job-1")
        self.assertEqual([r["status"] for r in rows], ["settled", "settled"])

    def test_dropping_the_target_side_is_refused_too(self):
        fx = Fixture(self.root)
        result = self.reconciler().reconcile_sync(fx.context(), model_call=lambda req: (reply({"app.py": THEIRS_APP}), usage()))
        self.assertEqual(result["status"], "author_required")
        self.assertIn("target's", result["reason"])

    def test_weakened_tests_are_refused(self):
        fx = Fixture(self.root, {"app.py": (BASE_APP, OURS_APP, THEIRS_APP),
                                 "tests/test_app.py": (BASE_TEST, OURS_TEST, THEIRS_TEST)})
        # The test file "resolved" by dropping test_negative, or by skipping it, or by losing an
        # assertion: each refused on its own, whatever the app file looks like.
        dropped = OURS_TEST
        skipped = MERGED_TEST.replace("    def test_negative(self):", "    @unittest.skip('flaky')\n    def test_negative(self):")
        fewer = MERGED_TEST.replace("        self.assertAlmostEqual(area(1), 3.14159, places=4)", "        area(1)")
        for label, bad in (("dropped", dropped), ("skipped", skipped), ("fewer", fewer)):
            with self.subTest(label):
                ctx = fx.context(job_id=f"job-{label}")
                result = self.reconciler().reconcile_sync(
                    ctx, model_call=lambda req, bad=bad: (reply({"app.py": MERGED_APP, "tests/test_app.py": bad}), usage()))
                self.assertEqual(result["status"], "author_required", label)
                text = " ".join(result["diagnostics"])
                if label == "dropped":
                    self.assertIn("test(s) removed: test_negative", text)
                elif label == "skipped":
                    self.assertIn("skip or expected-failure marker was added", text)
                else:
                    self.assertIn("assertions fell", text)
        # And the honest merge of both is accepted.
        ctx = fx.context(job_id="job-good")
        result = self.reconciler().reconcile_sync(
            ctx, model_call=lambda req: (reply({"app.py": MERGED_APP, "tests/test_app.py": MERGED_TEST}), usage()))
        self.assertEqual(result["status"], "resolved", result["reason"])
        self.assertEqual(result["resolved_paths"], ["app.py", "tests/test_app.py"])

    def test_review_file_catches_markers_elision_and_emptying(self):
        v = R.FileVersions("app.py", BASE_APP, OURS_APP, THEIRS_APP, "", True)
        self.assertIn("conflict markers remain", " ".join(R.review_file(v, MERGED_APP + "<<<<<<< HEAD\n")))
        elided = MERGED_APP.replace("def perimeter(r):\n    return 2 * math.pi * r\n", "# ... rest of file unchanged\n")
        self.assertIn("reads as elided", " ".join(R.review_file(v, elided)))
        self.assertIn("emptied", " ".join(R.review_file(v, "")))
        self.assertEqual(R.review_file(v, MERGED_APP), [])
        # Every conflicted path must come back, and nothing else may.
        self.assertIn("app.py: not resolved", " ".join(R.review_patch({}, {"app.py": v})))
        self.assertIn("outside the conflicted paths", " ".join(R.review_patch({"x.py": "", "app.py": MERGED_APP}, {"app.py": v})))

    def test_unusable_paths_from_the_model_are_refused_at_parse(self):
        for bad in ("/etc/passwd", "../app.py", ".git/config", "C:\\x.py"):
            files, notes, gave_up = R.parse_reply(json.dumps({"files": [{"path": bad, "content": "x"}]}))
            self.assertIsNone(files, bad)
            self.assertIn("unusable path", notes)
        files, notes, gave_up = R.parse_reply('{"give_up": "the sides contradict"}')
        self.assertIsNone(files)
        self.assertEqual(gave_up, "the sides contradict")
        self.assertEqual(R.parse_reply("I cannot help")[1], "the reply was not the JSON object asked for")

    def test_model_giving_up_and_unparseable_replies_hand_back(self):
        fx = Fixture(self.root)
        result = self.reconciler().reconcile_sync(fx.context(), model_call=lambda req: ('{"give_up": "contradictory"}', usage()))
        self.assertEqual(result["status"], "author_required")
        self.assertEqual([a["outcome"] for a in result["attempts"]], ["gave_up", "gave_up"])
        self.assertIn("the model declined: contradictory", result["reason"])
        (self.root / "two").mkdir()
        fx2 = Fixture(self.root / "two")
        result = self.reconciler().reconcile_sync(fx2.context(job_id="job-2"), model_call=lambda req: ("no json here", None))
        self.assertEqual([a["outcome"] for a in result["attempts"]], ["unparseable", "unparseable"])
        rows = self.reconciler().history("job-2")
        self.assertEqual([r["status"] for r in rows], ["uncertain", "uncertain"], "no usage: charged at the reservation")

    def test_disabled_policy_and_missing_conflicts_never_call_a_model(self):
        fx = Fixture(self.root)
        called = []
        model = lambda req: called.append(req) or (reply({"app.py": MERGED_APP}), usage())  # noqa: E731
        ctx = fx.context(policy={"reconcile": {"enabled": False}})
        result = self.reconciler().reconcile_sync(ctx, model_call=model)
        self.assertEqual(result["status"], "author_required")
        self.assertIn("disabled", result["reason"])
        git(fx.candidate, "merge", "--abort")
        result = self.reconciler().reconcile_sync(fx.context(conflicts=[]), model_call=model)
        self.assertIn("no conflicted paths", result["reason"])
        self.assertEqual(called, [])
        with self.assertRaises(R.ReconcileError):
            R.normalize_policy({"reconcile": {"max_attempts": "two"}})

    def test_oversized_conflicts_are_the_authors(self):
        big = "x = 1\n" * 40000
        fx = Fixture(self.root, {"app.py": (big, big + "y = 2\n", big + "z = 3\n")})
        called = []
        ctx = fx.context(policy={"reconcile": {"tokens_per_case": 20000}})
        result = self.reconciler().reconcile_sync(ctx, model_call=lambda req: called.append(req))
        self.assertEqual(result["status"], "author_required")
        self.assertIn("context cap", result["reason"])
        self.assertEqual(called, [])
        self.assertEqual(result["attempts"][0]["outcome"], "too_large")


class BudgetTests(ReconcileTestCase):
    def test_case_budget_refuses_the_second_call_before_it_is_made(self):
        # One attempt always fits (`normalize_policy` sizes context + completion under the case);
        # a first attempt that spent most of the case leaves no room for the second.
        fx = Fixture(self.root)
        calls = []

        def model(request):
            calls.append(request)
            return reply({"app.py": OURS_APP + "\n# dropped theirs\n"}), usage(190_000, 5_000)

        ctx = fx.context(policy={"reconcile": {"tokens_per_case": 200_000}})
        rec = self.reconciler()
        result = rec.reconcile_sync(ctx, model_call=model)
        self.assertEqual(result["status"], "author_required")
        self.assertEqual(len(calls), 1, "the second call was refused before it was made")
        self.assertEqual([a["outcome"] for a in result["attempts"]], ["rejected", "budget"])
        self.assertIn("case budget", result["reason"])
        self.assertIn("195,000 tokens charged", result["reason"])
        rows = rec.history("job-1")
        self.assertEqual(len(rows), 1, "a refused reservation is never written")
        self.assertEqual(rec.ledger.charged(case_key="repo-1:job-1"), 195_000)
        # A new job on the same repo starts with its own case budget.
        (self.root / "two").mkdir()
        fx2 = Fixture(self.root / "two")
        result = rec.reconcile_sync(fx2.context(job_id="job-2"), model_call=lambda req: (reply({"app.py": MERGED_APP}), usage()))
        self.assertEqual(result["status"], "resolved")

    def test_day_budget_is_shared_across_processes_and_days_are_utc(self):
        fx = Fixture(self.root)
        clock = [1_800_000_000.0]
        now = lambda: clock[0]  # noqa: E731
        first = self.reconciler(now=now)
        ctx = fx.context(policy={"reconcile": {"tokens_per_day": 60_000}})
        result = first.reconcile_sync(ctx, model_call=lambda req: (reply({"app.py": MERGED_APP}), usage(40_000, 5_000)))
        self.assertEqual(result["status"], "resolved")
        self.assertEqual(first.ledger.charged(day=R._utc_day(clock[0])), 45_000)
        (self.root / "two").mkdir()
        fx2 = Fixture(self.root / "two")
        second = self.reconciler(now=now)            # another publisher process, same state root
        called = []
        ctx2 = fx2.context(job_id="job-2", policy={"reconcile": {"tokens_per_day": 60_000}})
        result = second.reconcile_sync(ctx2, model_call=lambda req: called.append(req))
        self.assertEqual(result["status"], "author_required")
        self.assertIn("day budget", result["reason"])
        self.assertEqual(called, [])
        clock[0] += 86_400                             # tomorrow: the day's charge is gone
        result = second.reconcile_sync(ctx2, model_call=lambda req: (reply({"app.py": MERGED_APP}), usage()))
        self.assertEqual(result["status"], "resolved")
        status = second.budget_status()
        self.assertEqual(status["charged_today"], 1200)

    def test_a_crashed_reservation_stays_charged_and_is_labelled_uncertain(self):
        fx = Fixture(self.root)
        first = self.reconciler()
        rid = first.ledger.reserve(repo_id="repo-1", job_id="job-0", case_key="repo-1:job-0", attempt=1,
                                   tokens=150_000, per_case=200_000, per_day=10_000_000, preset="kimi", model="kimi-k3")
        # The process that held it died: forge a dead pid on the row.
        with sqlite3.connect(first.ledger.path) as db:
            db.execute("UPDATE reservations SET pid = 999999999 WHERE id = ?", (rid,))
        second = self.reconciler()                   # a fresh process sweeps on start
        rows = second.history("job-0")
        self.assertEqual(rows[0]["status"], "uncertain")
        self.assertEqual(rows[0]["outcome"], "process gone")
        self.assertEqual(second.ledger.charged(case_key="repo-1:job-0"), 150_000)
        # And the day still carries it: a job that would fit otherwise is refused.
        called = []
        ctx = fx.context(policy={"reconcile": {"tokens_per_day": 160_000}})
        result = second.reconcile_sync(ctx, model_call=lambda req: called.append(req))
        self.assertIn("day budget", result["reason"])
        self.assertEqual(called, [])
        # Two processes cannot both fit under the line: the reservation is one IMMEDIATE transaction.
        with self.assertRaises(R.BudgetExceeded):
            second.ledger.reserve(repo_id="repo-1", job_id="job-9", case_key="repo-1:job-9", attempt=1,
                                  tokens=20_000, per_case=200_000, per_day=160_000)

    def test_at_most_two_attempts_whatever_the_policy_says(self):
        fx = Fixture(self.root)
        calls = []
        ctx = fx.context(policy={"reconcile": {"max_attempts": 7}})
        result = self.reconciler().reconcile_sync(ctx, model_call=lambda req: calls.append(req) or ('{"give_up": "no"}', usage()))
        self.assertEqual(len(calls), 2)
        self.assertEqual(result["policy"]["max_attempts"], 2)
        self.assertEqual(result["status"], "author_required")


class RoutingTests(ReconcileTestCase):
    TWO_KEYED = [{"preset": "kimi", "model": "kimi-k3", "rank": 1},
                 {"preset": "glm", "model": "glm-5.3", "rank": 2}]

    def test_transport_error_excludes_the_provider_and_redraws(self):
        fx = Fixture(self.root)
        seen = []

        def model(request):
            seen.append(request["preset"])
            if request["preset"] == "kimi":
                raise ProviderError("502 from upstream")
            return reply({"app.py": MERGED_APP}), usage()

        rec = self.reconciler(tiers=self.TWO_KEYED, key_lookup=lambda p: "k" if p in ("kimi", "glm") else "")
        result = rec.reconcile_sync(fx.context(), model_call=model)
        self.assertEqual(result["status"], "resolved", result["reason"])
        self.assertEqual(seen, ["kimi", "glm"])
        rows = rec.history("job-1")
        self.assertEqual([(r["preset"], r["status"]) for r in rows], [("kimi", "uncertain"), ("glm", "settled")])
        self.assertEqual(result["attempts"][0]["outcome"], "error")

    def test_a_guest_that_will_not_start_releases_its_reservation(self):
        fx = Fixture(self.root)

        def model(request):
            raise ValueError("Claude Code could not be started.")

        rec = self.reconciler()
        result = rec.reconcile_sync(fx.context(), model_call=model)
        self.assertEqual(result["status"], "author_required")
        self.assertEqual(result["attempts"][0]["outcome"], "unavailable")
        self.assertEqual([r["status"] for r in rec.history("job-1")], ["released"])
        self.assertIn("no further usable model", result["reason"])

    def test_nothing_usable_in_the_list_hands_back_without_a_call(self):
        fx = Fixture(self.root)
        called = []
        rec = self.reconciler(key_lookup=lambda p: "", guest_check=lambda g: False)
        result = rec.reconcile_sync(fx.context(), model_call=lambda req: called.append(req))
        self.assertEqual(called, [])
        self.assertIn("no model in the high list can take the call", result["reason"])
        empty = self.reconciler(tiers=[])
        result = empty.reconcile_sync(fx.context(), model_call=lambda req: called.append(req))
        self.assertIn("high list is empty", result["reason"])

    def test_the_draw_is_weighted_by_subscription_standing(self):
        fx = Fixture(self.root)
        entries = [{"preset": "guest:claude", "model": "fable", "effort": "high", "rank": 1},
                   {"preset": "guest:codex", "model": "gpt-6-astra", "effort": "xhigh", "rank": 1}]
        seen = []
        model = lambda req: seen.append((req["preset"], req["model"], req["effort"])) or (reply({"app.py": MERGED_APP}), usage())  # noqa: E731
        rec = self.reconciler(tiers=entries, guest_check=lambda g: True)
        # Equal standing (no fresh quota report): the uniform draw decides between the tied ranks.
        with mock.patch.object(model_roles.random, "random", lambda: 0.05):
            rec.reconcile_sync(fx.context(job_id="j1"), model_call=model)
        git(fx.candidate, "checkout", "-m", "--", "app.py")
        with mock.patch.object(model_roles.random, "random", lambda: 0.95):
            rec.reconcile_sync(fx.context(job_id="j2"), model_call=model)
        self.assertEqual(seen, [("guest:claude", "fable", "high"), ("guest:codex", "gpt-6-astra", "xhigh")])
        # A subscription that reported itself exhausted is excluded from the draw altogether.
        now = 1_800_000_000
        limits = {"codex": {"status": "rejected", "updated_at": now, "windows": [
            {"kind": "five_hour", "used_percent": 100.0, "resets_at": now + 3600}]}}
        seen.clear()
        git(fx.candidate, "checkout", "-m", "--", "app.py")
        with mock.patch.object(model_roles.time, "time", lambda: now), \
             mock.patch("relay_core.guest_harness_provider.last_limits", lambda gid: limits.get(gid, {})), \
             mock.patch.object(model_roles.random, "random", lambda: 0.95):
            rec.reconcile_sync(fx.context(job_id="j3"), model_call=model)
        self.assertEqual(seen, [("guest:claude", "fable", "high")])
        rows = rec.history("j3")
        self.assertEqual((rows[0]["preset"], rows[0]["model"], rows[0]["effort"]), ("guest:claude", "fable", "high"))

    def test_high_list_sources_policy_then_stored_then_defaults(self):
        stored = [{"preset": "kimi", "model": "kimi-k3", "effort": "max"}]
        defaults = [{"preset": "glm", "model": "glm-5.3", "effort": "max"}]
        policy = {"reconcile": {}, "tiers": {"high": [{"preset": "guest:codex", "model": "gpt-6-astra", "effort": "xhigh"}]}}
        entries, source = R.high_entries(policy, stored=stored, defaults=defaults)
        self.assertEqual((source, entries[0]["preset"]), ("policy", "guest:codex"))
        entries, source = R.high_entries({"reconcile": {}}, stored=stored, defaults=defaults)
        self.assertEqual((source, entries[0]["preset"]), ("stored", "kimi"))
        entries, source = R.high_entries({}, stored=None, defaults=lambda: defaults)
        self.assertEqual((source, entries[0]["preset"]), ("defaults", "glm"))
        entries, source = R.high_entries({}, stored=lambda: [], defaults=lambda: defaults)
        self.assertEqual(source, "defaults", "an emptied stored list falls through")
        self.assertEqual(R.with_high_effort({"preset": "kimi", "model": "kimi-k3"})["effort"], "max")
        self.assertEqual(R.with_high_effort({"preset": "kimi", "model": "kimi-k3", "effort": "low"})["effort"], "low")

    def test_stored_high_list_is_read_from_the_gui_settings(self):
        conf = Path(os.environ["XDG_CONFIG_HOME"]) / "RelayTerminal" / "relay.conf"
        self.assertIsNone(R.stored_high_entries())
        conf.parent.mkdir(parents=True)
        conf.write_text('[models]\n'
                        'available=guest:claude|fable, guest:codex|gpt-6-astra, kimi|, glm|glm-5.3\n'
                        'tier\\high="guest:claude|fable|high|rank=1", "guest:codex|gpt-6-astra|high|rank=1", '
                        'kimi|kimi-k3|max|rank=3, glm|glm-5.3||rank=4, openrouter|z-ai/glm-5.3|high|rank=5\n'
                        'tier\\lite=openrouter|google/gemini-3.5-flash-lite|low\n')
        entries = R.stored_high_entries()
        self.assertEqual([(e["preset"], e["model"], e.get("effort"), e["rank"]) for e in entries],
                         [("guest:claude", "fable", "high", 1), ("guest:codex", "gpt-6-astra", "high", 1),
                          ("glm", "glm-5.3", None, 4), ("openrouter", "z-ai/glm-5.3", "high", 5)],
                         "kimi is un-ticked (its preset is named with no model); openrouter is untouched by the ticks")
        listed, source = R.high_entries({})
        self.assertEqual(source, "stored")
        self.assertEqual([e["preset"] for e in listed], ["guest:claude", "guest:codex", "glm", "openrouter"])
        self.assertEqual(R.with_high_effort(listed[2])["effort"], "max")


class AdapterTests(ReconcileTestCase):
    """The production adapters, driven end to end through the seams the worker's own tests use:
    `guest_harness_provider.make_harness` for a guest and a loopback server for an API entry."""

    def test_guest_entry_runs_one_read_only_harness_turn_in_the_candidate(self):
        fx = Fixture(self.root)

        class NamedHarness(FakeHarness):
            # The real CLI answers `start` with the model it runs ("claude-fable-5-1" for an
            # entry that said "fable"); the stock fake echoes what it was asked for.
            def start(self, **kw):
                started = super().start(**kw)
                self._model = "claude-fable-5-1"
                return HarnessStart(session_id=started.session_id, model=self._model)

        harness = NamedHarness([{"events": [ev("usage", input_tokens=2200, output_tokens=310)],
                                 "result": (reply({"app.py": MERGED_APP}), "end", {})}], guest="claude")
        rec = self.reconciler(tiers=[{"preset": "guest:claude", "model": "fable", "effort": "high", "rank": 1}],
                              guest_check=lambda g: g == "claude")
        with mock.patch("relay_core.guest_harness_provider.make_harness", lambda *a, **k: harness):
            result = rec.reconcile_sync(fx.context())
        self.assertEqual(result["status"], "resolved", result["reason"])
        self.assertEqual((result["preset"], result["model"], result["effort"]), ("guest:claude", "claude-fable-5-1", "high"))
        self.assertEqual(result["tokens"]["input"], 2200)
        self.assertEqual(result["tokens"]["output"], 310)
        start = harness.starts[0]
        self.assertEqual(start["permissions"], "deny", "a reconciliation turn cannot write")
        self.assertEqual(start["cwd"], str(fx.candidate))
        self.assertEqual((start["model"], start["effort"]), ("fable", "high"))
        self.assertIn("Relay's landing reconciler", harness.sent[0]["prompt"])
        self.assertIn("read-only reconciliation turn", harness.instructions)
        self.assertTrue(harness.closed)
        self.assertEqual((fx.candidate / "app.py").read_text(), MERGED_APP)
        rows = rec.history("job-1")
        self.assertEqual((rows[0]["preset"], rows[0]["model"], rows[0]["status"], rows[0]["input_tokens"]),
                         ("guest:claude", "claude-fable-5-1", "settled", 2200))

    def test_guest_account_of_a_family_row_is_drawn_and_recorded(self):
        fx = Fixture(self.root)
        from relay_core import guest_accounts
        account = guest_accounts.Account("work", "claude", "Work", str(self.root / "claude-work"))
        (self.root / "claude-work").mkdir()
        harness = FakeHarness([{"events": [], "result": (reply({"app.py": MERGED_APP}), "end",
                                                          {"input_tokens": 50, "output_tokens": 9})}])
        rec = self.reconciler(tiers=[{"preset": "guest:claude", "model": "fable", "effort": "high", "rank": 1}],
                              guest_check=lambda g: True)
        made = []

        def make_harness(guest_id, probe=False, *, memory=None, account=""):
            made.append((guest_id, account))
            return harness

        with mock.patch.object(guest_accounts, "accounts", lambda family=None: [account]), \
             mock.patch.object(guest_accounts, "find", lambda g, a: account), \
             mock.patch.object(guest_accounts, "overrides", lambda g, a: ({"CLAUDE_CONFIG_DIR": account.config_dir}, [])), \
             mock.patch("relay_core.guest_harness_provider.make_harness", make_harness), \
             mock.patch.object(model_roles.random, "random", lambda: 0.95):
            # Two tied candidates once the family row is expanded — the default login and the
            # account — at equal standing: a high uniform draw lands on the second, the account.
            result = rec.reconcile_sync(fx.context())
        self.assertEqual(result["status"], "resolved", result["reason"])
        self.assertEqual(result["preset"], "guest:claude:work", "the family row stood for its account")
        self.assertEqual(result["account"], "work")
        self.assertEqual(made, [("claude", "work")])
        self.assertEqual(result["tokens"]["input"], 50)

    def test_api_entry_is_a_no_tools_completion_under_the_cap(self):
        fx = Fixture(self.root)
        seen = []
        answer = reply({"app.py": MERGED_APP})

        def event(delta=None, finish=None):
            return ("data: " + json.dumps({"choices": [{"delta": delta or {}, "finish_reason": finish}]}) + "\n\n").encode()

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                seen.append((self.path, body))
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.end_headers()
                self.wfile.write(event({"role": "assistant", "content": answer[:40]}) + event({"content": answer[40:]})
                                 + event(finish="stop")
                                 + ("data: " + json.dumps({"choices": [], "usage": {"prompt_tokens": 777, "completion_tokens": 88}}) + "\n\n").encode()
                                 + b"data: [DONE]\n\n")

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        base_url = f"http://127.0.0.1:{server.server_port}/v1"
        rec = self.reconciler(tiers=[{"base_url": base_url, "model": "loop-model", "rank": 1}],
                              key_lookup=lambda p: "", guest_check=lambda g: False)
        ctx = fx.context(policy={"reconcile": {"completion_tokens": 4096}})
        result = rec.reconcile_sync(ctx)
        self.assertEqual(result["status"], "resolved", result["reason"])
        self.assertEqual(result["model"], "loop-model")
        self.assertEqual(result["tokens"], {"input": 777, "output": 88, "total": 865, "reserved": result["tokens"]["reserved"]})
        path, body = seen[0]
        self.assertEqual(path, "/v1/chat/completions")
        self.assertEqual(body["model"], "loop-model")
        self.assertEqual(body["max_tokens"], 4096)
        self.assertNotIn("tools", body)
        self.assertEqual([m["role"] for m in body["messages"]], ["system", "user"])
        self.assertIn("landing reconciler", body["messages"][0]["content"])


class BoardNoteTests(ReconcileTestCase):
    def test_card_notes_are_appended_once(self):
        from relay_core.board import Board
        board = self.root / "board"
        (board / "features").mkdir(parents=True)
        (board / "threads").mkdir()
        (board / "board.yaml").write_text("version: 1\n")
        (board / "features" / "2026-09-25-negative-radii.md").write_text(
            "---\nid: AB12\ntype: work\nstatus: executing\nlabels: [feature]\nrank: m\ncreated: '2026-09-25'\n"
            "links: {plans: [], commits: [], evidence: [], related: [], github: null}\n---\n# Negative radii\n\n## Issue\nx\n")
        context = {"job_id": "job-7", "cards": ["#ab12", {"id": "ZZ99"}], "target_sha": "t" * 40, "submitted_sha": "s" * 40}
        result = {"status": "resolved", "model": "kimi-k3", "preset": "kimi", "account": "", "effort": "max",
                  "resolved_paths": ["app.py"], "tokens": {"input": 10, "output": 2}, "trailer": "Reconciled-From: t s"}
        notes = R.card_notes(context, result)
        self.assertEqual([n["card"] for n in notes], ["AB12", "ZZ99"])
        self.assertEqual(R.append_card_notes(board, notes), ["AB12"], "the unknown card is skipped")
        self.assertEqual(R.append_card_notes(board, notes), [], "already noted: nothing appended")
        entries = Board(board).thread("AB12")
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0].kind, "note")
        self.assertIn("Reconciled landing job job-7", entries[0].text)
        self.assertIn("reconcile:job-7:resolved", entries[0].text)
        # A different outcome for the same job is a different note.
        handed = R.card_notes(context, {**result, "status": "author_required", "reason": "budget"})
        self.assertEqual(R.append_card_notes(board, handed), ["AB12"])
        self.assertEqual(len(Board(board).thread("AB12")), 2)


if __name__ == "__main__":
    unittest.main()
