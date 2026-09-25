# #QK2Q — provider failures say which limit or credential failed

## What happened in pane f35e3cfe (2026-09-25, 20:28 and 20:41 UTC)

Every turn went k3 (Kimi Code) → HTTP 401 → glm-5.3 (Z.AI Coding Plan) → HTTP 429 ×6 → OpenRouter.

- **Kimi 401.** The `kimi-code` keyring entry is a Kimi Code OAuth access token (`iss: kimi-auth`,
  `exp = iat + 900`). The pane read it once when its model was set; the owner's
  `kimi-code-relay-key.timer` kept the keyring fresh, but the pane kept sending the expired copy.
  At 20:41:51 the keyring held a token issued 20:38:57 and the pane still 401'd.
- **Z.AI 429.** Live body: `{"error":{"code":"1308","message":"Usage limit reached for 5 hour. Your
  limit will reset at 2026-09-26 05:53:25"}}`. Relay only recognised 1310, so 1308 was retried six
  times (~20 s) every turn. Z.AI's reset time is UTC+8 (`X-LOG-ID` clock vs `Date`), i.e. 21:53:25 UTC.

## Why effective-availability routing did not avoid GLM

`routing-draws.jsonl` shows the #495G draws *did* adjust: glm-coding was in every Qt draw until
20:19 (score 0.13, bound by the weekly window at 87% used) and absent from every draw from
20:21:20 on. The worker's quota poll (`provider_limits`) reported `5h used 100%` (verified live:
`percentage: 100`, `nextResetTime` 21:53:25 UTC).

What did not adjust was the **worker's failover**: the Kimi 401 is an ordinary failure, and
`Agent._begin_failover` walked the priority list through `RoleResolver.fallback_candidate`, which
never consulted quota — only the quota-failover path used `ordered_candidates(choose=True)`, which
excludes a score of 0. And because 1308 was not recognised as quota, Z.AI's own refusal never set a
quota hold either, so the same walk repeated each turn.

Fixed: failover and side-call chains skip a preset with a held refusal or a spent window in its
fresh quota report (`Agent._spent_reason`), and 1308 is quota.

## Evidence

- `tests.txt` — `tests/test_provider_errors.py` (16 new) + `tests/test_provider.py`: 90 passed.
- `live-probe.txt` — Relay's own `ChatProvider` against the live spent Z.AI plan: one request,
  0.6 s, `ProviderQuotaExhausted`, "5-hour usage limit reached; resets 17:53 (in 0.8 h)", issue
  `quota_5h`, vendor code 1308, `resets_at` 1790373205. Kimi Code answered normally.
- Tests failing identically on a clean `HEAD` export (not this change):
  `test_failover.py::…twin_is_tried_once_even_when_the_list_names_openrouter`,
  `test_provider_limits.py::…coding_plan_limits_feed_tied_rank_weight`,
  `test_session_protocol.py::GuestSessionRows` (16).

## Outside the repository

`~/.local/bin/kimi-code-relay-key.py` refreshed only tokens with < 180 s left while its timer runs
every 300 s, so the keyring token lapsed for up to ~2 min each cycle (the Kimi quota poll failed at
20:38:47; the next token was issued 20:38:57). `REFRESH_WITHIN_S` is now 420.
