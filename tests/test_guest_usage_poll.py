# SPDX-License-Identifier: AGPL-3.0-or-later
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import guest_usage_poll, guest_harness_provider


class _Response:
    def __init__(self, value):
        self.data = io.BytesIO(json.dumps(value).encode())

    def __enter__(self):
        return self

    def __exit__(self, *_):
        pass

    def read(self, size):
        return self.data.read(size)


class UsagePollTests(unittest.TestCase):
    def test_claude_reads_both_windows_and_credits_without_spend(self):
        payload = {"five_hour": {"utilization": 31.2, "resets_at": "2026-09-25T10:00:00Z"},
                   "seven_day": {"utilization": 52.1, "resets_at": "2026-09-28T10:00:00Z"},
                   "cedar_ember": {"grants": [{"resets_left": 1,
                                                  "ends_at": "2026-10-22T12:00:00Z"}]}}
        seen = []
        def opener(request, timeout):
            seen.append((request.full_url, request.get_method()))
            return _Response(payload)
        with mock.patch("relay_core.guest_harness_claude._oauth_token", return_value="secret"), \
             mock.patch("relay_core.guest_harness_claude._installed_version", return_value="2.1.281"):
            data = guest_usage_poll.fetch_claude("/fake/login", opener=opener)
        self.assertEqual([w["kind"] for w in data["windows"]], ["5h", "weekly"])
        self.assertEqual(data["windows"][0]["used_percent"], 31.2)
        self.assertEqual(data["resets_available"], 1)
        self.assertEqual(seen, [(guest_usage_poll.CLAUDE_URL, "GET")])

    def test_codex_reads_both_windows_and_never_consumes_credit(self):
        payload = {"rate_limit": {"primary_window": {"used_percent": 12,
                     "limit_window_seconds": 18000, "reset_at": 1790320000},
                     "secondary_window": {"used_percent": 45,
                     "limit_window_seconds": 604800, "reset_at": 1790800000}},
                   "rate_limit_reset_credits": {"available_count": 1}}
        with tempfile.TemporaryDirectory() as home:
            (Path(home) / "auth.json").write_text(json.dumps({"tokens": {
                "access_token": "secret", "account_id": "acct"}}))
            seen = []
            def opener(request, timeout):
                seen.append((request.full_url, request.get_method()))
                return _Response(payload)
            data = guest_usage_poll.fetch_codex(home, opener=opener)
        self.assertEqual([w["kind"] for w in data["windows"]], ["5h", "weekly"])
        self.assertEqual(data["resets_available"], 1)
        self.assertEqual(seen, [(guest_usage_poll.CODEX_URL, "GET")])

    def test_poll_assigns_each_reading_to_its_account(self):
        events = []
        data = {"windows": [{"kind": "weekly", "used_percent": 40,
                             "resets_at": 1790800000}]}
        with mock.patch.object(guest_usage_poll.guest_accounts, "accounts", side_effect=lambda guest: []), \
             mock.patch.object(guest_harness_provider, "usage_limits_event",
                               side_effect=lambda family, value, account: {
                                   "event": "usage_limits", "preset": "guest:" + family,
                                   "windows": value["windows"]}):
            guest_usage_poll.poll_once(emit=events.append,
                fetchers={"claude": lambda _: data, "codex": lambda _: data})
        self.assertEqual([e["preset"] for e in events], ["guest:claude", "guest:codex"])
        self.assertTrue(all(e["source"] == "subscription_poll" for e in events))


if __name__ == "__main__":
    unittest.main()
