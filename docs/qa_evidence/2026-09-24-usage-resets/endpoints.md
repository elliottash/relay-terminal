# Usage resets: what each surface reports (#V1VM)

Probes run 2026-09-23/24, read-only, with the accounts' own credentials (the same
surfaces Relay's guest harnesses and the crontab scanner already use).

## Codex — `GET https://chatgpt.com/backend-api/wham/usage`

```json
{"rate_limit": {"primary_window": {"used_percent": 36.5, "limit_window_seconds": 604800,
                                   "reset_after_seconds": 371087, "reset_at": 1790505600},
                "secondary_window": {"used_percent": 3.0, "limit_window_seconds": 10800, ...}},
 "rate_limit_reset_credits": {"available_count": 1, "applicable_available_count": 0},
 "plan_type": "prolite"}
```

- `rate_limit_reset_credits.available_count` is the banked "usage resets" (scanner column F
  showed 1 for both logins).
- Window reset times: `reset_at` per window. No expiry date for the credits on this surface.

## Codex — app-server `account/rateLimits/read` (what Relay already calls)

The recorded capture (`tests/test_guest_harness_codex.py:44`) shows the top level of the
result carrying the same figure beside the snapshot:

```json
{"rateLimits": {"primary": {...}, "secondary": null, "planType": "pro", ...},
 "rateLimitResetCredits": {"availableCount": 0, "credits": []}}
```

`_read_limits()` kept only `result.rateLimits` before this change. The `updated`
notification carries only `rateLimits`, so the count is folded in under the same sparse
rule as the windows.

## Claude — `GET https://api.anthropic.com/api/oauth/usage`

Checked all four logins (ashe@ethz.ch, gmail, e, gess) with the CLI 2.1.281 UA and the
`oauth-2025-04-20` beta:

- No `usage_resets` / `rate_limit_reset_credits` / reset-credit field of any shape on any
  account (top-level keys: `five_hour`, `seven_day`, `limits`, `spend`, `extra_usage`, …).
  `extra_usage` (pay-per-use credits) is a different feature.
- Per-window reset times are there and already flow: `five_hour.resets_at`,
  `seven_day.resets_at`, `limits[].resets_at`. The CLI's `/usage` panel renders exactly
  these ("resets Sep 24, 1:59am") — what the user saw as "claude has usage resets now".

## Crontab scanner — `/home/elliott/data/usage_tracker/check_usage.py`

Before: one reset per row (column G, weekly), `usage_resets` (F) Codex-only, the five-hour
reset tracked nowhere. After: column H "5h reset" added — Claude's `five_hour.resets_at`,
Codex's `limit_window_seconds < 86400` window — and the Claude checker parses
`rate_limit_reset_credits` / `usage_resets` in either shape if Anthropic ships it.
Live run 2026-09-24 (sheet updated): `claude (ashe@ethz.ch) reset5h=2026-09-24 01:59`,
`codex (personal) usage_resets=1`, all rows green.
