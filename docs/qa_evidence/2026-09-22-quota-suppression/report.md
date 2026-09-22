# HG26 package 2: #MSW7 and #YJG7

The model/role transport, refusal rollback, and separate fallback-error changes already landed in
552982fa58c6f82a0999452888e88f980b37c22e, with exact-tree build evidence in df9b35bf.
This follow-up retains that implementation and closes the repeated-quota-call gap.

Changes: provider.py now exposes ProviderQuotaExhausted separately from transient rate-limit
errors (`provider_rate_limited`). A provider instance remembers exhausted quota for at most 60
seconds, scoped to endpoint/model/key identity. Changed credentials bypass the remembered refusal;
expiry rechecks the service. Stop and a pending model switch preempt the remembered failure.
Reset strings undergo calendar validation before display. Because the provider timestamp has no
timezone, suppression is capped at the earliest possible reset (UTC+14), never an invented local
or UTC reset. This is bounded per-instance suppression, not a persistent account-wide cache;
new panes/providers can recheck the service. Unknown reset times get the 60-second cap.

Verification on the shared main checkout:

- `PYTHONPATH=backend:tests python3 -m unittest test_provider test_model_switch test_guest_harness_provider test_failover -q`: 220 tests passed (tests.txt). Includes failed native→guest and guest→native installation, active role preservation, deferred switching, retry interruption, quota expiry, invalid calendar values and key replacement. No paid probes.
- `scripts/relay-build --target relay`: passed after waiting for hg26-logging's build lock. No C++ changes in this follow-up; land.py also byte-compiles the exact Python blobs it lands.
- Re-ran existing `2026-09-22-model-switch-fixes/drive.py` in a temporary directory, changing only ROOT to this checkout so original evidence was untouched. Real compiled GUI and real worker, deterministic HTTP/guest boundaries, isolated XDG config/data, bounded by `timeout 150`. All 14 stages passed; see drive-result.txt and selected screenshots. Raw temporary evidence: /tmp/hg26-p2-gui-plcnra3t/. Processes exited normally. No production model calls.
- Viewed screenshots: rejected guest retains kimi-k3 and shows “Still on kimi-k3”; quota failure shows validated reset and separate Muse HTTP 401.
- `TestsCommands(Path.cwd()).check_card(...)` for MSW7/YJG7: no findings (tests-check.json). Board format check has no diagnostics for either card/thread; 12 existing errors elsewhere remain.

No relay_board tools were exposed by discovery; used POLICY.md's file fallback. No further
delegation. HG26, Agent._record_tool and Pane.h worker-exit logging were not edited.

Implementation/evidence commit: `24a6b202d8121b35173b9aabe456c9c7eb32a72c` (land.py exact Python byte-compilation gate passed).
