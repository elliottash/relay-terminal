"""Subscription quota parsing and polling; every response is local test data."""
import unittest

from relay_core import provider_limits as limits
from relay_core import roles


class ProviderLimitsTests(unittest.TestCase):
    def setUp(self):
        limits._last.clear()
        limits.set_listener(None)

    def test_zai_coding_windows_and_epoch_milliseconds(self):
        rows = limits.parse_zai({"code": 200, "data": {"limits": [
            {"type": "CREDIT_LIMIT", "unit": 3, "percentage": 20,
             "nextResetTime": 1789990000000},
            {"type": "TOKENS_LIMIT", "unit": 6, "percentage": 40,
             "nextResetTime": 1790000000000},
            {"type": "TIME_LIMIT", "unit": 5, "percentage": 100,
             "nextResetTime": 1790000000000},
            {"type": "CREDIT_LIMIT", "unit": 9, "percentage": 10},
        ]}})
        self.assertEqual(rows, [
            {"kind": "5h", "used_percent": 20.0, "resets_at": 1789990000},
            {"kind": "weekly", "used_percent": 40.0, "resets_at": 1790000000},
        ])

    def test_kimi_legacy_count_windows(self):
        rows = limits.parse_kimi({"usage": {"limit": "100", "remaining": "25",
                                                  "resetTime": "2026-09-24T00:00:00Z"},
                                  "limits": [{"window": {"duration": 300,
                                                         "timeUnit": "TIME_UNIT_MINUTE"},
                                              "detail": {"limit": "50", "remaining": "40",
                                                         "resetTime": "2026-09-23T20:00:00Z"}}]})
        self.assertEqual([(r["kind"], r["used_percent"]) for r in rows],
                         [("5h", 20.0), ("weekly", 75.0)])
        self.assertTrue(all(r["resets_at"] for r in rows))

    def test_kimi_ratio_pools_with_optional_monthly_window(self):
        rows = limits.parse_kimi({"usages": {
            "limit_5h": {"used_ratio": 0.4, "reset_time": "2026-09-23T21:00:00Z"},
            "limit_month_total": {"used_ratio": 0.75,
                                  "reset_time": "2026-10-01T00:00:00Z"}}})
        self.assertEqual([(r["kind"], r["used_percent"]) for r in rows],
                         [("5h", 40.0), ("monthly", 75.0)])

    def test_poll_skips_missing_keys_and_keeps_last_snapshot_on_failure(self):
        events, refreshes, calls = [], [], []
        limits.set_listener(lambda: refreshes.append(1))

        def fetch(preset, key):
            calls.append(preset)
            self.assertEqual(key, "secret")
            return [{"kind": "weekly", "used_percent": 30, "resets_at": 1790000000}]

        limits.poll_once(key_lookup=lambda preset: "secret" if preset == "glm-coding" else "",
                         emit=events.append, fetcher=fetch, clock=lambda: 1789990000)
        self.assertEqual(calls, ["glm-coding"])
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["preset"], "glm-coding")
        self.assertNotIn("secret", str(events))
        self.assertEqual(limits.last("glm-coding")["updated_at"], 1789990000)
        self.assertEqual(len(refreshes), 1)

        def fail(*_):
            raise RuntimeError("secret")

        limits.poll_once(key_lookup=lambda preset: "secret" if preset == "glm-coding" else "",
                         emit=events.append, fetcher=fail, clock=lambda: 1789991000)
        self.assertEqual(len(events), 1)
        self.assertEqual(limits.last("glm-coding")["updated_at"], 1789990000)

    def test_successful_repeat_refreshes_freshness(self):
        events = []
        limits.set_listener(lambda: None)
        fetch = lambda preset, key: [{"kind": "5h", "used_percent": 10, "resets_at": 1790000000}]
        for now in (1789990000, 1789990900):
            limits.poll_once(key_lookup=lambda _: "secret", emit=events.append,
                             fetcher=fetch, clock=lambda: now)
        self.assertEqual(len(events), 4)  # each successful preset poll renews its freshness
        self.assertEqual(limits.last("kimi-code")["updated_at"], 1789990900)

    def test_coding_plan_limits_feed_tied_rank_weight(self):
        now = 1789990000
        limits.poll_once(key_lookup=lambda p: "key" if p == "glm-coding" else "",
                         emit=lambda _: None,
                         fetcher=lambda p, k: [{"kind": "weekly", "used_percent": 20,
                                                "resets_at": now + 3600}],
                         clock=lambda: now)
        self.assertGreater(roles._usage_weight("glm-coding", now), 3.0)
        self.assertEqual(roles._usage_weight("glm-coding", now + 1801), 1.0)


if __name__ == "__main__":
    unittest.main()
