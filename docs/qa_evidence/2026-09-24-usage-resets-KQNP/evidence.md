# #KQNP — banked usage resets: evidence (2026-09-24)

## Where each provider exposes them

- **Claude** (Claude Code 2.1.281 calls them `cedar_ember`; `/limit-reset` in the CLI):
  `GET https://api.anthropic.com/api/oauth/usage?cedar_ember=1&skip_spend=1` with the login's
  OAuth token. The plain `/api/oauth/usage` answer has `cedar_ember: null`. With the
  `claude-cli/2.1.272` User-Agent the block reads `{"eligible": false, "ineligible_reason":
  "cli_version", "grants": []}`; with `2.1.281`:
  `grants: [{"id": "opus55-launch-promax-20260921", "resets_total": 1, "resets_left": 1,
  "ends_at": "2026-10-22T16:00:00+00:00", "clears": ["five_hour", "seven_day",
  "seven_day_overage_included"], "usable_now": true, ...}]`. Spent on claude.ai (owner, 2026-09-23).
- **Codex** (codex-cli 0.156.1, `codex app-server generate-json-schema`):
  `account/rateLimitResetCredit/consume {idempotencyKey, creditId?}` →
  `{outcome: reset | nothingToReset | noCredit | alreadyRedeemed}`; the read's
  `rateLimitResetCredits.credits[]` carry `expiresAt` (unix seconds).

## Live reads through Relay's own code

```
$ python3 -c 'guest_harness_claude.read_usage_resets("~/.claude", "")'
(1, 1792684800)                      # 1 left, use by 2026-10-22 16:00 UTC

$ CodexHarness().start(cwd="/tmp"); _limits_event(); use_usage_reset()   # unconfirmed
{"windows": [{"kind": "weekly", "used_percent": 98.0, "resets_at": 1790732488}],
 "resets_available": 1, "resets_expire_at": 1792703299}
{'outcome': 'confirm', 'message': "Use a Codex usage reset now? It refills this account's
 usage limits at once (1 left, use by Oct 22)."}
```

No reset was spent: the confirmed path is covered by the unit test with a stubbed app-server.

## Crontab scanner (`/home/elliott/data/usage_tracker/check_usage.py`, run by hand)

Column I "resets use by" added; Claude reads now use `?cedar_ember=1` and the installed CLI's
version (a pinned 2.1.272 got `cli_version` and showed 0).

```
[OK]   claude / ashe@ethz.ch / cli              ... usage_resets=1 resets_by=2026-10-22 12:00
[OK]   claude / elliott.t.ash@gmail.com / cli   ... usage_resets=1 resets_by=2026-10-22 12:00
[OK]   claude / e@elliottash.com / cli          ... usage_resets=1 resets_by=2026-10-22 12:00
[OK]   claude / elliott.ash@gess.ethz.ch / cli  ... usage_resets=1 resets_by=2026-10-22 12:00
Sheet updated.
```

## Tests

- `PYTHONPATH=backend python3 -m unittest tests.test_claude_usage_resets tests.test_guest_harness_codex tests.test_guest_harness_provider`
  — 160 run; the new ones pass. 3 failures in `StartTests.test_session_replacement_preserves_bridge_and_instructions`
  come from another session's uncommitted `backend/relay_core/board_tools.py` (`'BoardTools' object has no attribute '_case'`).
- `build/relay-modelcatalog-tests` — 70 passed, 0 failed (`limitsText(..., 1, oct22)` → `1 usage reset, use by 22 oct`).
- `land.py` built the exact landed tree (`relay` target) before commit `22464f33`.
