# Z.AI and Kimi Code quota integration (#XH4K)

On 2026-09-23, focused offline checks passed:

- `PYTHONPATH=backend python3 -m unittest tests.test_provider_limits tests.test_roles tests.test_guest_harness_provider tests.test_provider`: 228 tests.
- `PYTHONPATH=backend python3 -m unittest tests.test_provider_limits tests.test_presets tests.test_worker_encoding`: 58 tests.
- The exact committed tree built the `relay` target through `scripts/land.py`.

Read-only live probes used Relay's configured keys without printing them. The Z.AI Coding Plan endpoint returned valid 5-hour and weekly windows (3% and 1% used), both with reset times. The Kimi Code endpoint returned valid 5-hour and weekly windows (both 0% used), both with reset times. An initial Kimi request failed with HTTPError; a retry returned the expected `limits`, `usage`, and `usages` fields. The poller keeps the last good snapshot on such a failure.

The probes verify vendor response parsing. A restarted Relay instance still needs separate visual verification that these windows appear in its picker and that a 100% window is skipped until reset.
