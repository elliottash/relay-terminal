---
id: VMZP
type: work
status: needs-qa-llm
labels: [feature, providers]
implemented_by: claude-opus-4-5
rank: zzzzzt
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-provider-http-retries/], related: [], github: null}
---
# Retry transient provider refusals (429, 5xx) in the transport

## Issue
add retries when there are provider errors, eg 429. look at how claude code does that for example

## Tasks
- [x] Read the retry policy out of the installed Claude Code binary (Anthropic SDK transport): 408/409/429/5xx, Retry-After first, else 0.5→8 s jittered backoff. <!-- t:sj -->
- [x] Implement it in `ChatProvider._open` (the one path every model call takes), emitting `provider_retry {reason: "http"}` + `status` per wait and a `provider_http_retry` log line. <!-- t:9q -->
- [x] `HostedChatProvider`: refusal body read once and shared between the retry decision and the failure text; `rate_limited` waited to its `resets_at`, `quota_exhausted` never waited out. <!-- t:yy -->
- [x] Local model servers excluded from the generic policy (loading 503 wait unchanged). <!-- t:7a -->
- [x] Tests: 9 new in `tests/test_provider.py`, hosted rate-limit/quota cases in `tests/test_hosted.py`, updated the superseded hosted-503 test. <!-- t:f4 -->
- [x] Docs: `docs/AGENT-SESSIONS-PROTOCOL.md` §15.2, `docs/ARCHITECTURE.md` Provider transport. <!-- t:nb -->

## QA checklist
Evidence: `docs/qa_evidence/2026-09-19-provider-http-retries/` (implementer notes + backend test log; 79 provider/hosted/local-transport tests pass).

- [ ] A turn against a provider that answers 429 (or 503) once recovers, and the pane shows the "Provider HTTP 429 · asking again in N s (retry n of 6)" line.
- [ ] Esc during the wait stops the turn; no request follows, no socket left open.
- [ ] A 401/403 still fails at once with the existing key-guidance sentence.
- [ ] Relay Free with a spent allowance still surfaces the quota sentence on the first refusal (no 6-retry pause), quota chip updating.
- [ ] A local model server's overflow 500 still names the context fix at once; a cold load still shows "The local server is loading its model…".
- [ ] `Retry-After` values are honoured (seconds and HTTP-date forms), capped at 60 s.
