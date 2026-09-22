<!-- relay:entry 20260920T124840Z-gk author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=e9e7159c turn=32d94133997843a4a39505146a114344/0cf1167ec6b74c389779515dea286328 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-20-a-spent-z-ai-coding-plan-quota-is-retried-as-a-t.md

<!-- relay:entry 20260920T124854Z-ht author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=e9e7159c turn=32d94133997843a4a39505146a114344/0cf1167ec6b74c389779515dea286328 -->
- ✦ agent updated this card · replaced `## Issue`

<!-- relay:entry 20260920T124854Z-vv author=agent kind=rewrite model=deepseek/deepseek-v4.1-flash pane=e9e7159c turn=32d94133997843a4a39505146a114344/0cf1167ec6b74c389779515dea286328 -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
amy out of glm credits
```

</details>

<details><summary>after</summary>

```
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
```

</details>

<!-- relay:entry 20260922T132117Z-vs author=agent kind=evidence model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/69757fa04bba4791b0b2c06e1605b039 -->
Session ef7ab5c3ef21445ab80dea50fedfbb29 repeats six GLM 429 retry waits on each of two turns at 2026-09-22 13:08:54 and 13:09:43 UTC; turns failed after 24.461 and 27.684 seconds following Muse fallback. This session's logs do not retain the 429 body, so quota exhaustion is not independently established here. Extracted evidence: docs/qa_evidence/2026-09-22-model-session-audit/events.log. Related selection/error-reporting findings filed as #MSW7.

<!-- relay:entry 20260922T132117Z-vt author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/69757fa04bba4791b0b2c06e1605b039 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started
