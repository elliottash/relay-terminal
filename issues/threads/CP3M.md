<!-- relay:entry 20260923T004506Z-ca author=codex kind=evidence -->
### Codex · 2026-09-23 00:45
Filed the confirmed compaction accounting bug after tracing pane e0711a2a to its session and Codex usage metadata. A 3,105,465-token turn aggregate was treated as context; the final individual request was 181,959 tokens. Reproduced a reported reduction with unchanged messages. Investigation complete; code fix remains open. relay_board tools were not exposed, so this record uses POLICY.md's file fallback.

<!-- relay:entry 20260923T010810Z-jk author=agent kind=event model=kimi-k3 pane=43a3f0e7 turn=1741f896515c4dd78e282ae5cd1a9be6/0f32f10cf16c40079ecaefbd5c41c2f9 -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by kimi/kimi-k3, session 43a3f0e7

<!-- relay:entry 20260923T010810Z-qc author=agent kind=progress model=kimi-k3 pane=43a3f0e7 turn=1741f896515c4dd78e282ae5cd1a9be6/0f32f10cf16c40079ecaefbd5c41c2f9 pane_token=43a3f0e7-f04a-4981-b2dd-ee0432e64f24 -->
Claimed (43a3f0e7) · working on it from a terminal pane

Implementing the fix: guest turn-aggregate usage must not feed ContextTracker.record_usage, and an unchanged transcript must not claim a reduction.

<!-- relay:entry 20260923T011151Z-0e author=agent kind=event model=kimi-k3 pane=43a3f0e7 turn=1741f896515c4dd78e282ae5cd1a9be6/0f32f10cf16c40079ecaefbd5c41c2f9 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T012818Z-cn author=agent kind=event model=kimi-k3 pane=43a3f0e7 turn=1741f896515c4dd78e282ae5cd1a9be6/0f32f10cf16c40079ecaefbd5c41c2f9 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["C8WX"], "github": null… → {"plans": [], "commits": ["b469da5728573988afe4d7f68063ccd768ab0f71"], "evidence…; appended to `## Execution Summary`; replaced `## Tests`

<!-- relay:entry 20260923T012844Z-w6 author=agent kind=event model=kimi-k3 pane=43a3f0e7 turn=1741f896515c4dd78e282ae5cd1a9be6/0f32f10cf16c40079ecaefbd5c41c2f9 -->
- ✦ agent moved this card · Executing → Needs verification · Fix landed in b469da57: guest turn-aggregate usage no longer feeds ContextTracker (turn loop skips record_usage on a guest harness; session totals unchanged), and Agent.compact reports after == before when the transcript was not rewritten. Proved by tests.test_agent.GuestAggregateUsageTests (3 tests) plus full runs of test_agent / test_guest_context_meter / test_guest_harness_provider. · evidence docs/qa_evidence/2026-09-23-cp3m/ · implemented_by kimi/kimi-k3

<!-- relay:entry 20260923T014956Z-ng author=agent kind=evidence model=kimi-k3 pane=43a3f0e7 turn=1741f896515c4dd78e282ae5cd1a9be6/7320f3da89d34483a0f5ea3f8626791c -->
Claude coverage verified (same guard, no extra code): Claude Code's `result.usage` is cumulative across the turn's requests (guest_harness_claude.py:736 keeps the final request separately only for `context_tokens`, #C8WX). A synthetic claude result (1.8M input + 1.2M cache counters cumulative, final request 150k) maps through `_usage_event` → `relay_usage` to `prompt_tokens=3,000,000` — the same poisoned shape as codex. The b469da57 guard keys on `HarnessProvider.serves_side_calls = False`, which is guest-kind-independent, so guest:claude panes skip `record_usage` the same way. `tests.test_guest_harness_claude` + `tests.test_guest_context_meter`: 84 tests OK.
