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

<!-- relay:entry 20260922T144034Z-rn author=agent kind=progress model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
Claimed via file fallback as codex, under the user's request to deliver the session's diagnosed fixes (#MSW7). Implemented narrow Z.AI 429/code 1310 recognition: no retry, provider reset time retained, arbitrary body excluded. New quota and transient-rate-limit regressions pass; live-drive verification underway.

<!-- relay:entry 20260922T144048Z-7n author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260922T145029Z-ax author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T145511Z-4y author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T145831Z-1j author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["VMZP", "G9VE", "DC4J"]… → {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-model-swi…; replaced `## Execution Summary`

<!-- relay:entry 20260922T145831Z-7c author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented and tested; live GUI drive and picker logs saved for independent verification. · evidence docs/qa_evidence/2026-09-22-model-switch-fixes/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T150313Z-fm author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-model-swi… → {"plans": [], "commits": ["552982fa58c6f82a0999452888e88f980b37c22e"], "evidence…

<!-- relay:entry 20260922T150313Z-fn author=agent kind=evidence model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
Landed implementation and live-drive evidence in 552982fa58c6f82a0999452888e88f980b37c22e. land.py built the exact committed tree successfully using scripts/relay-build --target relay, excluding concurrent sessions' uncommitted changes. Build-gate record: docs/qa_evidence/2026-09-22-model-switch-fixes/land-build.txt. Awaiting independent verification.

<!-- relay:entry 20260922T165157Z-p2 author=codex-hg26-p2 kind=progress -->
Claimed follow-up as codex-hg26-p2: existing 552982fa implementation is landed; adding bounded reset-aware exhausted-quota suppression and calendar validation. Existing Done means retained.

<!-- relay:entry 20260922T165621Z-p2 author=codex-hg26-p2 kind=evidence -->
Follow-up complete; needs-verification. 220 targeted tests and all 14 isolated GUI stages pass; scripts/relay-build --target relay passed. Existing switching/fallback implementation preserved; new quota suppression and failed guest→native transport test. Evidence: docs/qa_evidence/2026-09-22-quota-suppression/report.md. No HG26 or logging edits.
