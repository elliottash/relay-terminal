---
id: QK2Q
type: work
status: needs-verification
labels: [bug, providers, routing]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 7f908958-18c6-4f6e-8f75-cfbbc71d3e50
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'tests/test_provider_errors.py passes; a live read-only probe of a spent Z.AI plan fails once with the named window and local reset; the pane shows the failover cause and the helper link, and clicking it drafts/sends the failure in the Models helper.', sign_off: none, effort: medium}
source: Claude guest pane, 2026-09-25
links: {plans: [], commits: [4947ff813865], evidence: [docs/qa_evidence/2026-09-25-qk2q/], related: [YJG7, 9R2V, XH4K, 495G], github: null}
---
# Provider failures say which limit or credential failed, and the helper offers the fix

## Issue
Pane f35e3cfe (2026-09-25) failed over k3→glm-5.3→openrouter on every turn: Kimi Code answered 401 because the pane held an expired 15-minute OAuth access token (the keyring already had a fresh one), and Z.AI answered 429 code 1308 "Usage limit reached for 5 hour … reset at …", which Relay retried six times as transient because only code 1310 is recognised. Name the actual failure (5-hour / weekly limit with local reset time, rate limit, balance, expired token, rejected key), log the provider's error code, re-read a rotated key on 401, and let the helper agent offer to fix it. Then find out why effective-availability routing did not skip GLM during the 5-hour exhaustion.

> check out the kimi / glm failures here: f35e3cfe
>
> can you tell what those were? we should try to inform the user about failures. check if the APIs say what it is, if its a rate limit, 5 hour limit, weekly limit, etc, or invalid account. how to address it
>
> ---
>
> i agree with all these points. 
>
> add more informative messages, logging as feasible, and the (helper) agent can offer to help the user adjust / fix issues.
>
> after thats done, can you check -- for glm, why wasnt the 5 hour limit being detected and adjusted by the effective-ability assignment
> — elliott · [session:eb540dbc335b48608bf140f752a963ce](relay://session/eb540dbc335b48608bf140f752a963ce) · 2026-09-25

## Done means
- A Z.AI Coding Plan 429 with code 1308 (5-hour) or 1310 (weekly/monthly) is asked once, not retried, and the user reads which window is spent and when it resets in local time; the body is never shown.
- Other recognised refusals are named: rate limit (still retried), balance/402, plan expired, account, rejected key, and an expired login token (a JWT key past `exp`).
- A key that came from the keyring is read again on 401/403 (and before a request when it is an expired JWT) and the request is resent once with the rotated key; a typed-in key is never swapped.
- Failover and side-call chains skip a provider this pane holds a quota refusal for, or whose fresh quota report has a spent window; the failover note names the cause of the move and what it skipped.
- `provider_retry` (failover, quota_exhausted) and a turn's `error` carry `issue {kind, label, preset, model, host, status, hint, resets_at?, vendor_code?}`; the pane prints one "▸ Ask the helper to fix this" link a turn for an actionable issue, which opens Options › Models › Providers and sends the failure to its helper.
- Logs carry `provider_http_refusal` / `provider_quota_refusal` / `provider_key_reload` / `provider_failover_skip` with kind and vendor code, never the body or key.

## Execution Summary
Landed `4947ff81`. `provider_errors.py` classifies hosted 401/402/403/429 refusals (Z.AI codes incl. 1308 5-hour and 1310, OpenAI-shaped types, 402, expired JWT login tokens) and pins Z.AI's provider-local reset to an instant (X-LOG-ID vs Date, or the quota poll). Spent windows, balance and expired plans are final with a local reset time; a keyring key is re-read on 401/403 (and before a request when an expired JWT) and resent once. Failover and side-call chains skip held/spent presets and the note names the cause and the skips; `provider_retry` and turn `error` carry `issue`; the pane prints one "▸ Ask the helper to fix this" link a turn that opens Options › Models › Providers and sends the failure to its helper.

Why routing missed GLM: the #495G draws did drop glm-coding from 20:21:20 on; the worker's *ordinary* failover (triggered by the Kimi 401) never consulted quota, and 1308 was not recognised as quota so no hold was set. Both fixed. Evidence: `docs/qa_evidence/2026-09-25-qk2q/report.md`.

Not driven live: the helper link click under Xvfb (built in the exact landed tree only). Also changed outside the repo: `~/.local/bin/kimi-code-relay-key.py` REFRESH_WITHIN_S 180→420 so the keyring token no longer lapses between 5-minute timer runs.

## Tests
- `PYTHONPATH=backend python3 -m pytest tests/test_provider_errors.py tests/test_provider.py`: 90 passed (16 new).
- `tests/test_failover.py`, `tests/test_roles.py`, `tests/test_guest_harness_provider.py`: pass except failures that reproduce identically on a clean HEAD export (twin-once test, tied-rank-weight test, 16 GuestSessionRows).
- Live read-only probe (`live-probe.txt`): spent Z.AI plan → one request, 0.6 s, "5-hour usage limit reached; resets 17:53 (in 0.8 h)".
- Exact landed tree built by `land.py commit` (target relay).
