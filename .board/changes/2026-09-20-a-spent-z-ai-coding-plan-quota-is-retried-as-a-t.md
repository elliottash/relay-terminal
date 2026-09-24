---
id: YJG7
type: work
status: needs-verification
labels: [bug, providers, routing]
assignee: codex-hg26-p2
implemented_by: openai/gpt-6-astra via codex
rank: zzzzzzzzzzzzzzzi
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [24a6b202d8121b35173b9aabe456c9c7eb32a72c, 552982fa58c6f82a0999452888e88f980b37c22e], evidence: [docs/qa_evidence/2026-09-22-quota-suppression/report.md, docs/qa_evidence/2026-09-22-model-switch-fixes/], related: [VMZP, G9VE, DC4J], github: null}
---
# A spent Z.AI Coding Plan quota is retried as a transient 429, and the pane never says when it resets

## Issue
The `glm-coding` preset (Z.AI Coding Plan) is out of quota until the provider's reset, but Relay treats the refusal as a transient rate limit and never tells the user why or for how long.

Measured on this machine, 2026-09-20:

A minimal one-shot call on the stored `glm-coding` key:

```
$ curl -sS -X POST https://api.z.ai/api/coding/paas/v4/chat/completions \
    -H "Authorization: Bearer <stored glm-coding key>" -H "Content-Type: application/json" \
    -d '{"model":"glm-5.3-flash","messages":[{"role":"user","content":"hi"}],"max_tokens":8}'
HTTP 429
{"error":{"code":"1310","message":"Weekly/Monthly Limit Exhausted. Your limit will reset at 2026-09-23 05:53:09"}}
```

So the provider does say exactly what is wrong and when it clears. What Relay shows instead (from `worker.log`, `provider_failover`):

```
error="Provider HTTP 429 for glm-5.3 at api.z.ai (Z.AI · GLM-5.3 · Coding Plan). Check endpoint, model access, key, quota, and parameters."
```

— a guess list that names neither the exhaustion nor the reset time.

Cost of the guess: 416 `provider_http_retry status=429` lines in `~/.local/share/relay/logs/worker.log`, first at 2026-09-19T21:25:45Z and still going at 2026-09-20T12:45:19Z — ~15 hours in which every turn on a GLM pane sent 6 retries (`attempt=1..6`, waits 0.4-7.8 s each, ~20 s per turn) before failing over to Kimi. Because the quota is spent for days, not seconds, that spend repeats on every turn on every GLM pane.

Related: `#VMZP` (retry transient refusals) and `#G9VE` (fail over when a provider keeps failing a turn) built the current behaviour; `#DC4J` covers not being able to switch models mid-retry.

## Done means
- Z.AI Coding Plan HTTP 429/code 1310 reports exhausted quota and the recognised provider reset timestamp without six transient retries.
- Unrecognised/transient 429s retain their retry behavior; arbitrary error body content is never displayed.
- Targeted regression tests and a live GUI failure/fallback drive provide evidence.

## Tests
- manual: docs/qa_evidence/2026-09-22-quota-suppression/report.md
- manual: docs/qa_evidence/2026-09-22-model-switch-fixes/tests.txt
- manual: docs/qa_evidence/2026-09-22-model-switch-fixes/build.txt
- manual: docs/qa_evidence/2026-09-22-model-switch-fixes/drive-result.txt
- manual: docs/qa_evidence/2026-09-22-model-session-audit/report.md

## Execution Summary

Follow-up package 2 under HG26: typed exhausted-quota vs transient HTTP429 errors, bounded per-provider suppression capped by the earliest possible reset, calendar validation, cancellation and model-switch preemption. Added guest→native construction-failure regression; retained original atomic switching implementation. 220 focused tests and the isolated 14-stage GUI drive pass. Evidence: docs/qa_evidence/2026-09-22-quota-suppression/report.md

Recognise Z.AI HTTP 429/code 1310 as exhausted quota immediately, retain a validated reset timestamp in provider time, and avoid echoing arbitrary response text. Unrecognised 429s still retry. Fallback failures now preserve each provider's error.

Verification: quota/transient regression tests passed in the 299-test suite. Live rebuilt GUI/real-worker drive with deterministic HTTP fixtures made exactly one exhausted GLM request and one Muse fallback request, showing both the quota reset and Muse HTTP 401. Evidence: docs/qa_evidence/2026-09-22-model-switch-fixes/. No production provider calls used.

## Plan
Follow-up under HG26: retain landed model-switch fixes; add bounded reset-aware suppression of repeated exhausted-quota calls and validated reset timestamps in provider.py. Verify with deterministic provider/model-switch tests and reuse the isolated GUI flow. Unknown provider timezone must not become an invented UTC deadline.
