---
id: P004
type: work
status: done
labels: [bug]
assignee: agent
implemented_by: kimi/k3
verified_by: kimi/k3
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [probe], human: none, sign_off: none, effort: low, stakes: rework, blast: capability}
source: pane 16 (this terminal pane), 2026-09-25 23:43Z
links: {plans: [], commits: [33a0ddddbeda], evidence: [], related: [9R2V, QK2Q], github: null}
---
# Kimi Code's 403 quota wall is read as an auth failure and never cooled down

## Issue
Kimi Code sends its spent 5-hour window as HTTP 403 typed `access_terminated_error`, and Relay mishandles it three ways: the classifier demotes the recognised `quota_5h` kind back to `auth` ("API key rejected", triggering pointless keyring reloads), the quota-cooldown path only engages on 402/429 so every turn re-hits kimi and fails over to glm-5.3 "this turn", and the quota poll's parser drops a spent count row because it has no `remaining` field — so Relay never sees the window it should have cooled down.

> kimi still isnt working
> — elliott · [session:f1140809e6f14769866887282d5dc791](relay://session/f1140809e6f14769866887282d5dc791) · 2026-09-25

## Done means
- A 403 whose body names a usage window ("You've reached your 5-hour usage limit", typed `access_terminated_error`) classifies as that quota kind — `quota_5h` — not `auth`, and carries the reset instant from the quota poll when the body has no date.
- A refusal of a final kind over 403 gets the same cooldown Z.AI's 429 already gets: `ProviderQuotaExhausted`, suppressed until the reset (re-asked at most every 15 min), not a per-turn failover with a keyring reload.
- `parse_kimi` reports a spent count row (`limit`+`used`, no `remaining`) as a 100 % window with its reset time; rows carrying only `remaining` keep working.
- A genuinely key-rejected 403 still classifies as `auth` and still reloads the keyring once — that path is unchanged.

## Tasks

- [x] classify: a quota kind read from the body survives a 401/403 (an unexplained 403 stays auth) <!-- t:yj -->
- [x] provider: a final refusal over 403 gets the ProviderQuotaExhausted cooldown (402/403/429) <!-- t:f7 -->
- [x] provider_limits: a spent count row with no `remaining` is read from `used`, clamped at the limit <!-- t:qg -->
- [x] tests for all three, plus the live refusal/payload shapes captured verbatim <!-- t:2t -->

## Execution Summary
Landed `33a0ddddbe`. Diagnosis from `worker.log` (first `provider_http_refusal` 22:59:15Z, `kind=auth`, `vendor_type=access_terminated_error`, key reload `changed=False`, every turn failing over to glm-5.3) and a live probe with the stored key: both `api.kimi.ai` and `api.kimi.com` answer 403 with "You've reached your 5-hour usage limit … purchase extra usage or upgrade your plan" typed `access_terminated_error`, while the same key still authenticates the `usages` poll (5h 100/100, weekly 30/100, reset 2026-09-26T01:44:41Z) — so the key was never the problem. Three fixes: `classify` no longer lets a 401/403 outvote a quota kind read from the body; `_coding_quota_error` engages on 403 when the refusal is final (a key-rejected 403 is classified auth and unchanged); `_count_window` reads `used` (clamped at the limit) so a spent row with no `remaining` is reported with its reset instead of dropped. After this, a spent kimi window is suppressed until its reset and the pane is told which limit and when, instead of "API key rejected" per turn.

## Tests
`PYTHONPATH=backend python3 -m pytest tests/test_provider_limits.py tests/test_provider_errors.py tests/test_provider.py` — 103 passed. New: `test_kimi_spent_count_row_has_no_remaining` (live payload shape), `test_kimi_sends_its_spent_window_as_a_403_and_it_is_not_auth` + `test_the_same_403_with_a_spent_poll_carries_the_reset` + `test_an_unexplained_403_is_still_an_auth_failure`, `test_kimi_spent_window_403_is_a_quota_refusal_not_an_auth_one` (through `_open`: one ask, `provider_quota_exhausted`) + `test_a_key_rejected_403_keeps_the_retry_and_failover_path`. Router neighbours `tests/test_router.py tests/test_lang_router.py` — 70 passed. Pre-existing failure `test_coding_plan_limits_feed_tied_rank_weight` reproduces on clean main before this commit; reported on #YC0T's thread.
