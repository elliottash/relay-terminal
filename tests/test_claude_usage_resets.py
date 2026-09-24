# SPDX-License-Identifier: AGPL-3.0-or-later
"""Claude's banked usage-limit resets (#KQNP): read from the login's usage endpoint with
`?cedar_ember=1`, carried on the `limits` event, and spent on claude.ai rather than here."""
import io
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "backend"))

from relay_core import guest_harness_claude as ghc   # noqa: E402

# Recorded 2026-09-23 from a Max login with claude-cli/2.1.281 (ids and figures as they came).
ANSWER = {"five_hour": {"utilization": 44.0}, "cedar_ember": {
    "eligible": True, "ineligible_reason": None, "at_limit": False, "exhausted": [],
    "grants": [{"id": "opus55-launch-promax-20260921",
                "label": "Claude Opus 5.5 launch: one usage-limit reset for Pro and Max",
                "resets_total": 1, "resets_left": 1, "starts_at": "2026-09-22T16:00:00+00:00",
                "ends_at": "2026-10-22T16:00:00+00:00",
                "clears": ["five_hour", "seven_day", "seven_day_overage_included"],
                "paused": False, "usable_now": True, "use_requires_limit": False}],
    "next_grant_id": "opus55-launch-promax-20260921",
    "weekly_resets_at": "2026-09-28T22:00:00+00:00", "cooldown_until": None}}
# What 2.1.272 got for the same login: withheld, not "none".
OLD_CLI = {"cedar_ember": {"eligible": False, "ineligible_reason": "cli_version", "grants": []}}
OCT_22 = 1792684800


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class ParseTest(unittest.TestCase):
    def test_the_recorded_answer(self):
        self.assertEqual(ghc.parse_resets(ANSWER), (1, OCT_22))

    def test_an_old_cli_is_told_nothing(self):
        self.assertIsNone(ghc.parse_resets(OLD_CLI))
        self.assertIsNone(ghc.parse_resets({"five_hour": {}}))
        self.assertIsNone(ghc.parse_resets([]))

    def test_spent_grants_count_nothing_and_set_no_date(self):
        spent = json.loads(json.dumps(ANSWER))
        spent["cedar_ember"]["grants"][0]["resets_left"] = 0
        self.assertEqual(ghc.parse_resets(spent), (0, 0))
        two = json.loads(json.dumps(ANSWER))
        two["cedar_ember"]["grants"].append(dict(two["cedar_ember"]["grants"][0],
                                                 ends_at="2026-11-01T00:00:00Z", resets_left=2))
        self.assertEqual(ghc.parse_resets(two), (3, OCT_22))


class ReadTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.addCleanup(lambda: ghc._resets.pop(self.dir, None))

    def login(self, expires_ms):
        with open(os.path.join(self.dir, ".credentials.json"), "w") as fh:
            json.dump({"claudeAiOauth": {"accessToken": "sk-ant-oat-test",
                                         "expiresAt": expires_ms}}, fh)

    def test_a_read_uses_the_login_and_the_cli_version_and_is_remembered(self):
        self.login((time.time() + 3600) * 1000)
        seen = []

        def opener(request, timeout):
            seen.append(request)
            return Response(json.dumps(ANSWER).encode())
        self.assertEqual(ghc.read_usage_resets(self.dir, "2.1.281", opener=opener), (1, OCT_22))
        self.assertEqual(seen[0].full_url, ghc.CLAUDE_USAGE_URL)
        self.assertEqual(seen[0].get_header("Authorization"), "Bearer sk-ant-oat-test")
        self.assertEqual(seen[0].get_header("User-agent"), "claude-cli/2.1.281 (external, cli)")
        self.assertEqual(ghc.usage_resets(self.dir), (1, OCT_22))

    def test_an_expired_token_is_not_used(self):
        self.login((time.time() - 60) * 1000)
        self.assertIsNone(ghc.read_usage_resets(
            self.dir, "2.1.281", opener=lambda *a, **k: self.fail("no request")))

    def test_a_failure_keeps_the_last_figure(self):
        self.login((time.time() + 3600) * 1000)
        ghc._resets[self.dir] = (1, OCT_22)

        def opener(request, timeout):
            raise OSError("offline")
        self.assertIsNone(ghc.read_usage_resets(self.dir, "2.1.281", opener=opener))
        self.assertEqual(ghc.usage_resets(self.dir), (1, OCT_22))

    def test_refresh_is_throttled_per_login(self):
        clock = [1000.0]
        ghc._resets_asked.pop(self.dir, None)
        started = []
        real = ghc.threading.Thread
        ghc.threading.Thread = lambda **kw: type("T", (), {"start": lambda s: started.append(kw)})()
        try:
            ghc.refresh_usage_resets(self.dir, "2.1.281", clock=lambda: clock[0])
            ghc.refresh_usage_resets(self.dir, "2.1.281", clock=lambda: clock[0])
            clock[0] += ghc.RESETS_TTL
            ghc.refresh_usage_resets(self.dir, "2.1.281", clock=lambda: clock[0])
        finally:
            ghc.threading.Thread = real
            ghc._resets_asked.pop(self.dir, None)
        self.assertEqual(len(started), 2)


class HarnessTest(unittest.TestCase):
    def test_the_limits_event_carries_the_held_resets(self):
        harness = ghc.ClaudeHarness(spawn=lambda *a: None,
                                    env_overrides={"CLAUDE_CONFIG_DIR": "/nowhere/login"})
        ghc._resets["/nowhere/login"] = (1, OCT_22)
        self.addCleanup(lambda: ghc._resets.pop("/nowhere/login", None))
        events = []
        message = {"type": "rate_limit_event", "rate_limit_info": {
            "status": "allowed", "unifiedWindows": {"five_hour": {"utilization": 0.44,
                                                                  "resetsAt": 1789926600}}}}
        harness._dispatch(message, {}, events.append)
        data = events[0].data
        self.assertEqual((data["resets_available"], data["resets_expire_at"]), (1, OCT_22))

    def test_a_reset_is_spent_on_the_website(self):
        harness = ghc.ClaudeHarness(spawn=lambda *a: None,
                                    env_overrides={"CLAUDE_CONFIG_DIR": "/nowhere/login"})
        ghc._resets["/nowhere/login"] = (1, OCT_22)
        self.addCleanup(lambda: ghc._resets.pop("/nowhere/login", None))
        answer = harness.use_usage_reset(confirmed=True)
        self.assertEqual(answer["outcome"], "web")
        self.assertEqual(answer["url"], "https://claude.ai/settings/usage")
        self.assertIn("1 left", answer["message"])
        self.assertIn("use by Oct", answer["message"])


if __name__ == "__main__":
    unittest.main()
