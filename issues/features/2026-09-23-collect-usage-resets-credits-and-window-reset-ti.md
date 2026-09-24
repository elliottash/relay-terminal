---
id: V1VM
type: work
status: needs-verification
labels: [feature, providers]
assignee: agent
implemented_by: glm/glm-5.3
session: 9af6a7f5-a6df-450e-9953-621582632fbf
rank: zzzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [probe], human: none, criteria: unit tests show credits riding the usage_limits event and the limits text; a dry-run of the scanner shows the new fields without writing the sheet, sign_off: none, effort: low}
source: pane 2, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-usage-resets/], related: [XH4K, M8S2], github: null}
---
# Collect usage resets (credits) and window reset times with subscription usage stats

## Issue
for the usage stats on subscriptions, the system should also collect usage resests and when they expire. look into that, as my crontab scanner was able to retrieve that info i think

## Done means
- A Codex guest's `usage_limits` figures carry the account's banked usage-reset credits (`rateLimitResetCredits.availableCount` from the app-server `account/rateLimits/read` response), not just window percentages.
- The picker row and the Models/Sources limits text show them (e.g. `weekly 9% left, resets 23:10 · 1 usage reset`), next to the per-window reset times that already flow.
- Claude figures are unchanged (its API has no reset-credit field today; reset times per window already flow — verified against the live endpoint).
- The user's crontab scanner (`/home/elliott/data/usage_tracker/check_usage.py`) additionally tracks Claude's five-hour reset and any future reset-credit field, writing them to the sheet.
- Failure looks like: credits visible in the app-server response but dropped before the picker; or the scanner sheet missing the new columns after a run.

## Plan
**Goal.** Subscription usage stats collect usage resets (Codex's banked reset credits) and window reset times, and show both; the crontab scanner tracks Claude's new per-window resets too.

**Findings.**
- The crontab scanner (`/home/elliott/data/usage_tracker/check_usage.py`, every 4 h) reads `api.anthropic.com/api/oauth/usage`, `chatgpt.com/backend-api/wham/usage`, Kimi `/coding/v1/usages`, Z.AI `/api/monitor/usage/quota/limit`. Codex's payload has `rate_limit_reset_credits.available_count`; every payload has per-window reset times. Claude's payload has **no** credit field today (checked all four logins, CLI 2.1.281); what's new is the `/usage` panel's per-window reset lines.
- Relay's wire already carries reset times: `usage_limits` event `{windows: [{kind, used_percent, resets_at}], status?, updated_at}` (protocol 29.3), normalised by `limit_windows()` (`backend/relay_core/guest_harness.py:321`), shown by `limitsText()` (`src/ModelCatalog.cpp:1415`).
- The Codex app-server's `account/rateLimits/read` response already carries `rateLimitResetCredits: {availableCount, credits: []}` at the **top level** — `_read_limits()` (`backend/relay_core/guest_harness_codex.py:319`) reads only `result.rateLimits` and drops it. No new endpoint or token scraping needed.
- No expiry date exists for the credits on any surface (app-server `credits: []`, wham has counts only); "when they expire" = the window reset times, which already flow.

**Steps.**
1. `guest_harness_codex.py`: keep `rateLimitResetCredits.availableCount` from the read response; include it in every `limits` event as `resets_available` (int).
2. `guest_harness_provider.py` `usage_limits_event()`: pass `resets_available` into the event and the held `_LAST_LIMITS` row (so `preset_rows()` and idle pickers keep it).
3. Protocol 29.3 doc (`docs/AGENT-SESSIONS-PROTOCOL.md`): document the optional field.
4. C++: parse `resets_available` from presets rows (`windowsOf`, `src/ModelCatalog.cpp:81`) and the `usage_limits` event (`src/PaneSession.cpp:287`); hold per preset; `limitsText()` appends `· N usage resets`.
5. Tests: `tests/test_guest_harness_codex.py`, `tests/test_guest_harness_provider.py`, `tests/modelcatalog_test.cpp`.
6. Scanner: `_claude_metrics` returns the five-hour reset (and reads `usage_resets`/`rate_limit_reset_credits` defensively), `_codex_metrics` returns the secondary window's reset, `main()` writes an H “5h reset” column and Claude credits when present.

**Risks.** The app-server response shape is Codex's, not Anthropic's — parsed defensively, absent field leaves everything as today. The scanner edit is outside this repo (user-owned); it is changed in place and dry-run before any write. Credits have no expiry date on any surface — nothing to collect there yet; noted rather than invented.

**Verify.** `pytest tests/test_guest_harness_codex.py tests/test_guest_harness_provider.py` and `ctest -R modelcatalog` after `scripts/relay-build`; scanner `--dry-run` output showing the new fields; sheet untouched until the next cron run.

## Execution Summary
Landed in `f607e5e9` (plus the scanner, outside this repo, patched in place).

- `guest_harness_codex.py`: `_read_limits` now hands the read answer's top-level `rateLimitResetCredits` to `_merge_limits`, which holds `availableCount` under the snapshot's own sparse rule (a figure-less update notification keeps the last count); `_limits_event` emits it as `resets_available`.
- `guest_harness_provider.py`: `usage_limits_event` passes `resets_available` into the wire event and the held `_LAST_LIMITS` row, so `preset_rows()` keeps it for idle pickers; absent means "not reported", and only a real number ≥ 0 rides (a bool or string does not).
- `docs/AGENT-SESSIONS-PROTOCOL.md` 29.3: the optional `resets_available` documented on the event and the presets row.
- C++: `Catalog::resetsAvailable` (`ModelCatalog.h`) filled from presets rows (`windowsOf` out-param, `catalogFrom`) and from live `usage_limits` events (`PaneSession.cpp` → `Pane::noteLimits`, which keeps a figure-less event from erasing the count); `limitsText` appends `· N usage reset(s)` when at least one is banked; picker row and Models/Sources line pass the value.
- Crontab scanner `check_usage.py`: 5-tuple checkers, new sheet column H "5h reset" (Claude `five_hour.resets_at`, Codex sub-day window), Claude checker parses `rate_limit_reset_credits`/`usage_resets` in either shape when Anthropic ships it. Live run green, sheet updated.
- What "when they expire" turned out to be: window reset times — already collected per window (`resets_at`); the credits carry no expiry date on any surface, so nothing was invented for that.
- Contested `src/Pane.h` was landed as three selected hunks only (`--only-hunk src/Pane.h:6,7,14`); the peer session's #R5TC work stayed uncommitted in the tree. The land gate's verify build caught the first attempt, which was missing Pane.h entirely.

Claude needs no new collection today: its endpoint has no credit field (all four logins probed, CLI 2.1.281), and its per-window reset times already flow through `rate_limit_event`.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_codex tests.test_guest_harness_provider` — 144 tests, 1 failure: `test_the_first_presets_answer_does_not_wait_for_codex`, order-dependent and failing identically on committed HEAD in a clean tree (`git archive HEAD | tar -x` rerun), so pre-existing, not this change (bug card filed). New: `test_banked_usage_resets_ride_the_limits_event`, `test_a_report_without_usage_resets_leaves_them_unsaid`, and the read-answer replay now asserts `resets_available: 0`.
- `scripts/relay-build --target relay-modelcatalog-tests && ctest --test-dir build -R modelcatalog --output-on-failure` — 1/1 passed. `limitsTextNamesEachWindow` covers `· 1 usage reset` / `· 3 usage resets` / none at 0 / a lone `2 usage resets`; `theWorkersLimitsObjectIsReadWindowsAndStatus` covers the presets-row field.
- `scripts/relay-build` — `relay` target built (the repo-wide `all` fails to link `relay-console-harness` on another session's in-flight `PaneRuntime.cpp`/`panedir` work, untouched by this change); `scripts/land.py`'s own verify build of the exact landed tree passed.
- Scanner offline checks against real payload shapes plus one live run (sheet updated, `notifications=0 errors=0`): `docs/qa_evidence/2026-09-24-usage-resets/`.
