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
        self.assertEqual(result["tokens"], {"input": 1234, "output": 321, "cached": 0, "total": 1555,
                                            "reserved": result["tokens"]["reserved"]})
        self.assertGreater(result["tokens"]["reserved"], 0)
        self.assertEqual(result["reconciled_from"], f"Reconciled-From: {fx.target} {fx.submitted}")
        self.assertEqual(result["trailer"], "Reconciled-By: kimi-k3 (kimi) effort=max attempts=1 tokens=1234/321")
        self.assertEqual(result["kind"], "merge_conflict")
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
        self.assertIn(result["reconciled_from"], notes[0]["text"])

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
        self.assertEqual(result["case_key"], "repo-1:job-1:merge_conflict")
        self.assertEqual(rec.ledger.charged(case_key=result["case_key"]), 195_000)
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
        self.assertEqual(result["tokens"], {"input": 777, "output": 88, "cached": 0, "total": 865,
                                            "reserved": result["tokens"]["reserved"]})
        path, body = seen[0]
        self.assertEqual(path, "/v1/chat/completions")
        self.assertEqual(body["model"], "loop-model")
        self.assertEqual(body["max_tokens"], 4096)
        self.assertNotIn("tools", body)
        self.assertEqual([m["role"] for m in body["messages"]], ["system", "user"])
        self.assertIn("landing reconciler", body["messages"][0]["content"])


class ApiRetryTests(ReconcileTestCase):
    def test_a_refused_request_is_not_resent_behind_one_reservation(self):
        fx = Fixture(self.root)
        hits = []

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_POST(self):
                self.rfile.read(int(self.headers["Content-Length"]))
                hits.append(self.path)
                self.send_response(429)
                self.send_header("Retry-After", "1")
                self.send_header("Content-Length", "0")
                self.end_headers()

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        base_url = f"http://127.0.0.1:{server.server_port}/v1"
        rec = self.reconciler(tiers=[{"base_url": base_url, "model": "loop-model"}],
                              key_lookup=lambda p: "", guest_check=lambda g: False)
        result = rec.reconcile_sync(fx.context())
        self.assertEqual(result["status"], "author_required")
        self.assertEqual(len(hits), 1, "one reservation, one request: no retry behind it")
        self.assertEqual(result["attempts"][0]["outcome"], "error")
        self.assertEqual(rec.history("job-1")[0]["status"], "uncertain")


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


# ----- the second review pass (parent review, 2026-09-26) ----------------------------------------

PT_BASE = """from app import area


def test_area():
    assert area(1) > 3
"""
PT_THEIRS = PT_BASE + """

def test_area_two():
    value = round(area(2), 2)
    assert value == 12.57
"""


class AssertionWeakeningTests(unittest.TestCase):
    """`review_file` alone: the count-preserving weakenings the first pass let through."""

    def versions(self, ours=PT_BASE, theirs=PT_THEIRS, base=PT_BASE, path="tests/test_area.py"):
        return R.FileVersions(path, base, ours, theirs, "", True)

    def test_assert_true_with_the_same_count_is_refused(self):
        weakened = PT_THEIRS.replace("assert value == 12.57", "assert True")
        problems = R.review_file(self.versions(), weakened)
        text = " ".join(problems)
        self.assertIn("assertion line(s) changed or removed", text)
        self.assertIn("test_area_two lost or changed 1 check(s)", text)
        self.assertEqual(R.review_file(self.versions(), PT_THEIRS), [], "the honest merge passes")

    def test_changed_expected_operand_is_refused(self):
        changed = PT_THEIRS.replace("assert value == 12.57", "assert value == 12.5")
        self.assertIn("assertion line(s) changed or removed", " ".join(R.review_file(self.versions(), changed)))
        # Non-Python too: the line rule needs no parser.
        base = 'TEST(Area, One) {\n  EXPECT_EQ(area(1), 3);\n}\n'
        theirs = base + 'TEST(Area, Two) {\n  EXPECT_EQ(area(2), 12);\n}\n'
        v = R.FileVersions("tests/area_test.cpp", base, base, theirs, "", True)
        self.assertIn("assertion line(s) changed or removed",
                      " ".join(R.review_file(v, theirs.replace("EXPECT_EQ(area(2), 12)", "EXPECT_TRUE(true)"))))
        self.assertEqual(R.review_file(v, theirs), [])

    def test_early_return_and_pass_bodies_are_refused(self):
        bypass = PT_THEIRS.replace("def test_area_two():\n", "def test_area_two():\n    return\n")
        text = " ".join(R.review_file(self.versions(), bypass))
        self.assertIn("test_area_two gained an early return/raise", text)
        self.assertIn("test_area_two now opens with return/raise/pass", text)
        guarded = PT_THEIRS.replace("    assert value == 12.57", "    if value != 12.57:\n        return\n    assert value == 12.57")
        self.assertIn("gained an early return", " ".join(R.review_file(self.versions(), guarded)))

    def test_a_check_moved_under_a_guard_that_never_runs_is_lost(self):
        guarded = PT_THEIRS.replace("    assert value == 12.57", "    if False:\n        assert value == 12.57")
        text = " ".join(R.review_file(self.versions(), guarded))
        self.assertIn("test_area_two lost or changed 1 check(s)", text)
        self.assertIn("a guard was added to a test file", text)
        nested = PT_THEIRS.replace("    assert value == 12.57", "    def later():\n        assert value == 12.57")
        self.assertIn("lost or changed 1 check(s)", " ".join(R.review_file(self.versions(), nested)))
        commented = PT_THEIRS.replace("    assert value == 12.57", "    \"\"\"\n    assert value == 12.57\n    \"\"\"")
        text = " ".join(R.review_file(self.versions(), commented))
        self.assertIn("lost or changed 1 check(s)", text)
        # A check that was under a guard on both sides keeps its fingerprint.
        looped = PT_BASE + "\n\ndef test_many():\n    for r in (1, 2):\n        assert area(r) > 0\n"
        self.assertEqual(R.review_file(self.versions(theirs=looped), looped), [])
        # Non-Python: the guard, return and block-comment counters.
        base = 'TEST(Area, One) {\n  EXPECT_EQ(area(1), 3);\n}\n'
        theirs = base + 'TEST(Area, Two) {\n  EXPECT_EQ(area(2), 12);\n}\n'
        v = R.FileVersions("tests/area_test.cpp", base, base, theirs, "", True)
        for label, bad in (("guard", theirs.replace("  EXPECT_EQ(area(2), 12);", "  if (false) {\n    EXPECT_EQ(area(2), 12);\n  }")),
                           ("return", theirs.replace("  EXPECT_EQ(area(2), 12);", "  return;\n  EXPECT_EQ(area(2), 12);")),
                           ("block comment", theirs.replace("  EXPECT_EQ(area(2), 12);", "  /*\n  EXPECT_EQ(area(2), 12);\n  */"))):
            with self.subTest(label):
                self.assertIn(f"a {label} was added to a test file", " ".join(R.review_file(v, bad)))

    def test_a_superseded_base_assertion_may_go_and_a_kept_one_may_not(self):
        ours = PT_BASE.replace("assert area(1) > 3", "assert round(area(1), 2) == 3.14")
        merged = PT_THEIRS.replace("assert area(1) > 3", "assert round(area(1), 2) == 3.14")
        self.assertEqual(R.review_file(self.versions(ours=ours), merged), [], "ours changed it; the merge takes ours")
        dropped = PT_THEIRS.replace("    assert area(1) > 3\n", "    area(1)\n")
        text = " ".join(R.review_file(self.versions(), dropped))
        self.assertIn("assertion line(s) changed or removed", text)
        self.assertIn("test_area lost or changed 1 check(s)", text)

    def test_files_that_no_longer_parse_are_refused(self):
        broken = PT_THEIRS.replace("def test_area_two():", "def test_area_two(:")
        self.assertIn("does not parse", " ".join(R.review_file(self.versions(), broken)))
        v = R.FileVersions("conf.json", '{"a": 1}\n', '{"a": 1}\n', '{"a": 1, "b": 2}\n', "", True)
        self.assertIn("does not parse", " ".join(R.review_file(v, '{"a": 1, "b": 2,}\n')))
        v = R.FileVersions("x.toml", "a = 1\n", "a = 1\n", "a = 1\nb = 2\n", "", True)
        self.assertIn("does not parse", " ".join(R.review_file(v, "a = 1\nb = \n")))


class PersistedAttemptTests(ReconcileTestCase):
    def test_attempts_survive_a_restart_of_the_same_case(self):
        fx = Fixture(self.root)
        calls = []
        first = self.reconciler()
        result = first.reconcile_sync(fx.context(), model_call=lambda req: calls.append(req) or ('{"give_up": "no"}', usage()))
        self.assertEqual(len(calls), 2)
        again = self.reconciler()                      # a restarted publisher re-runs the job
        result = again.reconcile_sync(fx.context(), model_call=lambda req: calls.append(req))
        self.assertEqual(len(calls), 2, "no third call, in this process or the next")
        self.assertEqual(result["status"], "author_required")
        self.assertIn("attempt budget: 2 attempt(s) already on record", result["reason"])
        self.assertEqual(result["attempts"], [])
        # One attempt used before the restart leaves exactly one.
        (self.root / "two").mkdir()
        fx2 = Fixture(self.root / "two")
        calls.clear()

        def one_then_crash(request):
            calls.append(request)
            raise ProviderError("connection reset")

        rec = self.reconciler(tiers=[{"preset": "kimi", "model": "kimi-k3"}])
        rec.reconcile_sync(fx2.context(job_id="job-2"), model_call=one_then_crash)
        self.assertEqual(len(calls), 1, "kimi failed and nothing else is listed")
        result = self.reconciler().reconcile_sync(fx2.context(job_id="job-2"),
                                                  model_call=lambda req: calls.append(req) or (reply({"app.py": MERGED_APP}), usage()))
        self.assertEqual(result["status"], "resolved")
        self.assertEqual(len(calls), 2)
        self.assertEqual(result["attempts"][0]["attempt"], 2, "numbered after the one on record")


class RepairFixture:
    """A clean merge whose result the gate refused: the submission added a function with a bug,
    the target changed another part of the same file, and `candidate` is the merged commit."""

    def __init__(self, root: Path):
        self.root = root
        self.repo = root / "repo"
        self.repo.mkdir()
        git(self.repo, "init", "-q", "-b", "main")
        (self.repo / "app.py").write_text(BASE_APP)
        (self.repo / "README.md").write_text("readme\n")
        (self.repo / "tests").mkdir()
        (self.repo / "tests" / "test_app.py").write_text(BASE_TEST)
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "base")
        self.base = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "checkout", "-q", "-b", "submitted")
        self.theirs_app = BASE_APP.replace("import math\n", "import math\n\n\ndef diameter(r):\n    return 2 * radius\n")
        (self.repo / "app.py").write_text(self.theirs_app)
        (self.repo / "tests" / "test_app.py").write_text(
            BASE_TEST.replace("from app import area, describe", "from app import area, describe, diameter")
            + "\n    def test_diameter(self):\n        self.assertEqual(diameter(2), 4)\n")
        git(self.repo, "commit", "-q", "-am", "theirs: diameter (buggy)")
        self.submitted = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "checkout", "-q", "main")
        (self.repo / "app.py").write_text(OURS_APP)
        git(self.repo, "commit", "-q", "-am", "ours: describe shows area")
        self.target = git(self.repo, "rev-parse", "HEAD")
        self.candidate = root / "candidate"
        git(self.repo, "worktree", "add", "-q", "--detach", str(self.candidate), self.target)
        git(self.candidate, "merge", "--no-edit", self.submitted)
        assert not git(self.candidate, "ls-files", "-u"), "the merge is clean"
        self.merged_app = (self.candidate / "app.py").read_text()
        assert "area={area(r):.2f}" in self.merged_app and "radius" in self.merged_app

    def context(self, **extra) -> dict:
        # The shape `landq._context` sends for a repair round: no conflicts, dict diagnostics,
        # a policy *hash*, intents as an (empty) dict, cards as strings.
        ctx = {"repo": str(self.repo), "repo_id": "repo-1", "job_id": "job-1", "base_sha": self.base,
               "target_sha": self.target, "submitted_sha": self.submitted, "candidate_path": str(self.candidate),
               "cards": ["#AB12"], "intents": {}, "conflicts": [],
               "diagnostics": {"kind": "gate_failure", "round": 1, "candidate_sha": "c" * 40,
                               "reason": "gate said no", "policy_hash": "policy-1", "log_path": "/x/gate.log",
                               "log": "collecting ...\nFAILED tests/test_app.py::AreaTests::test_diameter\n"
                                      "NameError: name 'radius' is not defined\n"},
               "policy": "policy-1"}
        ctx.update(extra)
        return ctx


class RepairTests(ReconcileTestCase):
    def test_gate_failure_round_repairs_only_what_the_submission_touched(self):
        fx = RepairFixture(self.root)
        fixed = fx.merged_app.replace("return 2 * radius", "return 2 * r")
        calls = []

        def model(request):
            calls.append(request)
            return reply({"app.py": fixed}, "renamed the undefined name"), usage(3000, 400)

        result = self.reconciler().reconcile_sync(fx.context(), model_call=model)
        self.assertEqual(result["status"], "resolved", result["reason"])
        self.assertEqual(result["kind"], "gate_failure")
        self.assertEqual(result["case_key"], "repo-1:job-1:gate_failure:1")
        self.assertEqual(result["policy_hash"], "policy-1")
        self.assertEqual(result["conflicts"], ["app.py", "tests/test_app.py"], "what the submission changed")
        self.assertEqual(result["resolved_paths"], ["app.py"], "a repair may return a subset")
        self.assertEqual((fx.candidate / "app.py").read_text(), fixed)
        self.assertIn("-    return 2 * radius\n+    return 2 * r\n", result["patch"])
        prompt = calls[0]["user"]
        self.assertTrue(prompt.startswith("GATE FAILURE to repair"))
        self.assertIn("NameError: name 'radius' is not defined", prompt)
        self.assertIn("reason: gate said no", prompt)
        self.assertIn("the candidate as the gate saw it", prompt)
        self.assertEqual(calls[0]["kind"], "gate_failure")
        self.assertIn("repaired by kimi-k3", result["card_notes"][0]["text"])

    def test_a_repair_may_not_touch_other_files_undo_the_target_or_weaken_tests(self):
        fx = RepairFixture(self.root)
        fixed = fx.merged_app.replace("return 2 * radius", "return 2 * r")
        cases = {
            "readme": ({"README.md": "fixed\n"}, "outside the files this repair may change"),
            "unchanged": ({"app.py": fx.merged_app}, "returned unchanged"),
            "undoes_target": ({"app.py": fixed.replace(' area={area(r):.2f}', "")}, "target's 1 added lines are gone"),
            "weakens_test": ({"app.py": fixed, "tests/test_app.py": (fx.candidate / "tests" / "test_app.py").read_text()
                              .replace("self.assertEqual(diameter(2), 4)", "self.assertTrue(True)")},
                             "assertion line(s) changed or removed"),
        }
        for label, (files, expected) in cases.items():
            with self.subTest(label):
                ctx = fx.context(job_id=f"job-{label}")
                result = self.reconciler().reconcile_sync(ctx, model_call=lambda req, files=files: (reply(files), usage()))
                self.assertEqual(result["status"], "author_required", label)
                self.assertIn(expected, " ".join(result["diagnostics"]), label)
        self.assertEqual((fx.candidate / "app.py").read_text(), fx.merged_app, "the candidate is untouched")

    def test_explicit_repair_paths_win_and_too_wide_a_submission_is_the_authors(self):
        fx = RepairFixture(self.root)
        self.assertEqual(R.repair_paths(fx.candidate, fx.repo, fx.context(repair_paths=["app.py"])), ["app.py"])
        with mock.patch.object(R, "MAX_REPAIR_PATHS", 1):
            with self.assertRaises(R.ReconcileError):
                R.repair_paths(fx.candidate, fx.repo, fx.context())
            result = self.reconciler().reconcile_sync(fx.context(), model_call=lambda req: self.fail("no call"))
            self.assertIn("bounded to 1", result["reason"])


class GuestBoundTests(ReconcileTestCase):
    GUEST = [{"preset": "guest:claude", "model": "fable", "effort": "high", "rank": 1}]

    def run_guest(self, fx, turn, **policy):
        harness = FakeHarness([turn], guest="claude")
        rec = self.reconciler(tiers=self.GUEST, guest_check=lambda g: True)
        ctx = fx.context(policy={"reconcile": policy})
        with mock.patch("relay_core.guest_harness_provider.make_harness", lambda *a, **k: harness):
            return rec, rec.reconcile_sync(ctx), harness

    def test_usage_past_the_reservation_cancels_the_turn_and_charges_at_least_what_was_seen(self):
        fx = Fixture(self.root)
        from relay_core.guest_harness import TurnResult

        def runaway(prompt, attachments, emit, cancel, harness):
            emit(ev("usage", input_tokens=150_000, output_tokens=60_000))   # cumulative, past the reservation
            self.assertTrue(cancel.wait(5), "the reconciler's watch must cancel the turn")
            return TurnResult(text="", stop_reason="interrupted")

        rec, result, harness = self.run_guest(fx, runaway)
        self.assertEqual(result["status"], "author_required")
        self.assertEqual(result["attempts"][0]["outcome"], "over_budget")
        self.assertIn("past the attempt's reservation", result["attempts"][0]["diagnostics"][0])
        self.assertIn("no further usable model", result["reason"])
        rows = rec.history("job-1")
        self.assertEqual(rows[0]["status"], "uncertain")
        self.assertGreaterEqual(rows[0]["reserved"], 210_000, "charged at what the guest reported")
        self.assertTrue(harness.closed)
        self.assertIn("<<<<<<<", (fx.candidate / "app.py").read_text())

    def test_a_final_usage_report_past_the_reservation_refuses_a_complete_answer(self):
        fx = Fixture(self.root)
        # Claude Code reports usage once, with its result: the turn is over before any cancel
        # could be seen, and the (complete, well-formed) answer must still not be accepted.
        turn = {"events": [ev("usage", input_tokens=150_000, output_tokens=60_000)],
                "result": (reply({"app.py": MERGED_APP}), "end", {})}
        rec, result, harness = self.run_guest(fx, turn)
        self.assertEqual(result["status"], "author_required")
        self.assertEqual(result["attempts"][0]["outcome"], "over_budget")
        self.assertIn("<<<<<<<", (fx.candidate / "app.py").read_text(), "nothing applied")
        self.assertGreaterEqual(rec.history("job-1")[0]["reserved"], 210_000)

    def test_a_streamed_answer_past_the_completion_cap_is_cut_off(self):
        fx = Fixture(self.root)
        from relay_core.guest_harness import TurnResult

        def verbose(prompt, attachments, emit, cancel, harness):
            for _ in range(40):
                emit(ev("delta", text="x" * 100))
            self.assertTrue(cancel.wait(5))
            return TurnResult(text="", stop_reason="interrupted")

        rec, result, harness = self.run_guest(fx, verbose, completion_tokens=256, tokens_per_case=2000)
        self.assertEqual(result["attempts"][0]["outcome"], "over_budget")
        self.assertIn("answer passed 256 tokens", result["attempts"][0]["diagnostics"][0])

    def test_a_guest_turn_has_a_wall_clock_deadline(self):
        fx = Fixture(self.root)
        from relay_core.guest_harness import TurnResult
        import time as _time

        def silent(prompt, attachments, emit, cancel, harness):
            self.assertTrue(cancel.wait(20), "the deadline must fire")
            return TurnResult(text="", stop_reason="interrupted")

        started = _time.monotonic()
        rec, result, harness = self.run_guest(fx, silent, guest_timeout_seconds=1)
        self.assertLess(_time.monotonic() - started, 10)
        self.assertEqual(result["attempts"][0]["outcome"], "timeout")
        self.assertEqual(rec.history("job-1")[0]["outcome"], "timeout after 1s")

    def test_the_callers_cancel_still_propagates(self):
        fx = Fixture(self.root)
        from relay_core.provider import Cancelled as _Cancelled
        outer = threading.Event()

        def model(request):
            outer.set()
            self.assertTrue(request["cancel"].wait(5))
            raise _Cancelled("Stopped.")

        with self.assertRaises(_Cancelled):
            self.reconciler().reconcile_sync(fx.context(), model_call=model, cancel=outer)
        self.assertEqual(self.reconciler().history("job-1")[0]["outcome"], "cancelled")


class CachedTokenTests(ReconcileTestCase):
    def test_cached_input_is_recorded_beside_input_and_output(self):
        fx = Fixture(self.root)
        cached = {"prompt_tokens": 5000, "completion_tokens": 300, "prompt_tokens_details": {"cached_tokens": 4200}}
        rec = self.reconciler()
        result = rec.reconcile_sync(fx.context(), model_call=lambda req: (reply({"app.py": MERGED_APP}), cached))
        self.assertEqual(result["tokens"], {"input": 5000, "output": 300, "cached": 4200, "total": 5300,
                                            "reserved": result["tokens"]["reserved"]})
        row = rec.history("job-1")[0]
        self.assertEqual((row["input_tokens"], row["output_tokens"], row["cached_tokens"]), (5000, 300, 4200))
        # Anthropic's names, as the claude harness reports them (`relay_usage` completes prompt_tokens).
        self.assertEqual(R.usage_counts({"input_tokens": 7, "output_tokens": 2}), (7, 2))


class QueueIntegrationTests(ReconcileTestCase):
    """The A2 queue driving this reconciler: conflict → resolved candidate → required gate →
    receipt, and a failed gate → one repair round → landed."""

    def setUp(self):
        super().setUp()
        from test_landq import make_repo  # noqa: F401
        self.make_repo = make_repo

    def test_conflict_is_reconciled_verified_and_landed_with_both_trailers(self):
        from test_landq import Recorder, plumb_commit, run_git, git_out, tip, show
        from relay_core.landq import Queue
        repo = self.make_repo(self.root)
        base = tip(repo)
        theirs = plumb_commit(repo, base, {"a.txt": "theirs\n"}, "theirs")
        run_git(repo, "update-ref", "refs/heads/main", theirs, base)
        mine = plumb_commit(repo, base, {"a.txt": "mine\n"}, "mine")
        q = Queue(repo, state_root=self.state / "q", repo_id="test-repo")
        q.submit(mine, request_id="r1", card="#FW1C")
        seen = []

        def model(request):
            seen.append(request)
            return reply({"a.txt": "theirs\nmine\n"}), usage(500, 40)

        rec = self.reconciler(model_call=model)
        verifier = Recorder()
        done = q.process_one(verifier, reconcile=rec.reconcile_sync)
        self.assertEqual(done["status"], "landed", done)
        new = tip(repo)
        self.assertEqual(verifier.calls[0][1], new, "the required gate saw the resolved candidate")
        self.assertEqual(show(repo, new, "a.txt"), "theirs\nmine")
        message = git_out(repo, "log", "-1", "--format=%B", new)
        self.assertEqual(message.count("Reconciled-From:"), 1, message)
        self.assertIn("Reconciled-From: %s %s" % (theirs, mine), message)
        self.assertIn("Reconciled-By: kimi-k3 (kimi) effort=max attempts=1 tokens=500/40", message)
        self.assertEqual(seen[0]["conflicts"], ["a.txt"])
        self.assertEqual(q.receipt(done["id"])["published_sha"], new)
        self.assertEqual(rec.history(done["id"])[0]["outcome"], "resolved")

    def test_failed_gate_gets_a_repair_round_through_the_reconciler(self):
        from test_landq import Recorder, plumb_commit, run_git, git_out, tip, show
        from relay_core.landq import Queue
        repo = self.make_repo(self.root)
        base = tip(repo)
        mine = plumb_commit(repo, base, {"b.txt": "one\ntwo\nthree\nbug\n"}, "mine")
        q = Queue(repo, state_root=self.state / "q", repo_id="test-repo")
        q.submit(mine, request_id="r1", card="#FW1C")
        contexts = []

        def model(request):
            contexts.append(request)
            return reply({"b.txt": "one\ntwo\nthree\nfour\n"}), usage()

        class Gate(Recorder):
            def __call__(self, job, candidate_sha, candidate_path):
                self.ok = "bug" not in (Path(candidate_path) / "b.txt").read_text()
                out = super().__call__(job, candidate_sha, candidate_path)
                if not self.ok:
                    out["log"] = "FAIL: b.txt contains 'bug'"
                return out

        rec = self.reconciler(model_call=model)
        gate = Gate()
        done = q.process_one(gate, reconcile=rec.reconcile_sync)
        self.assertEqual(done["status"], "landed", done)
        self.assertEqual(len(gate.calls), 2, "the repaired candidate went through the gate again")
        self.assertEqual(show(repo, tip(repo), "b.txt"), "one\ntwo\nthree\nfour")
        self.assertEqual(contexts[0]["kind"], "gate_failure")
        self.assertIn("FAIL: b.txt contains 'bug'", contexts[0]["user"])
        self.assertIn("Reconciled-By: kimi-k3", git_out(repo, "log", "-1", "--format=%B", tip(repo)))
        rows = rec.history(done["id"])
        self.assertTrue(rows[0]["case_key"].endswith(":gate_failure:1"), rows[0]["case_key"])
