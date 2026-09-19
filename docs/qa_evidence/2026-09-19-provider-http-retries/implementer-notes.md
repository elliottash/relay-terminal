# Provider HTTP retries (429, 5xx) — implementer notes

Card: `issues/features/2026-09-19-retry-transient-provider-refusals-429-5xx-in-the.md` (#VMZP)
Change: `backend/relay_core/provider.py` (+ tests, + docs). No C++ change.

## What was asked

> add retries when there are provider errors, eg 429. look at how claude code does that for example

## How Claude Code does it (read out of the installed binary, 2.1.278)

The Anthropic SDK compiled into `claude` decides retries in its transport:

- **Retryable:** HTTP 408, 409, 429 and every status >= 500 (plus an explicit
  `x-should-retry: true` header). Connection errors and timeouts are retried too.
- **Delay:** the `retry-after-ms` header, else `Retry-After` (seconds, or an HTTP date
  parsed as a delta from now), else exponential backoff
  `min(0.5 * 2^n, 8 s)` cut by 0–25 % jitter.
- The whole request is sent again; retries happen below the application, so no
  transcript-level state is involved.

Relay implements the same policy at the same layer: `ChatProvider._open`, the one
method every model call goes through (pane turns, side calls, subagents, the key test).

## What Relay now does

- Retries HTTP 408/409/429/5xx up to `HTTP_RETRY_ATTEMPTS = 6` times, waiting
  `Retry-After`/`retry-after-ms` (capped at 60 s) or the 0.5→8 s jittered backoff.
- The status arrives before anything streams, so a retry cannot repeat text the user
  has seen; 4xx that describe the request (401, 403, 404, …) still fail at once.
- Each wait emits `provider_retry {reason: "http", attempt, max_attempts, text}` (no
  `turn_id`; the GUI prints `text`, and `remote/wire.py` already allow-lists the event)
  and a `status`, and logs `provider_http_retry` with host/model/status/wait.
- `HostedChatProvider` reads the gateway's own error body once (kept, since `fp` reads
  once) and lets it decide: `rate_limited` is waited out until its `resets_at`, a spent
  `quota_exhausted` allowance is never waited out (it lifts at midnight), `token_expired`
  keeps its existing one-shot refresh path above the transport.
- A local model server is excluded from the generic policy: its 5xx are deterministic
  (the overflow sentence must not be delayed), and its loading-503 wait is unchanged.
- Stop still works during a wait (`cancel.wait`), and no socket is left open between
  attempts (`hard_close` on each refused response; asserted by `response_open()` checks).

## Deliberate behaviour change

`tests/test_provider_local.py::test_a_hosted_503_is_not_retried` pinned the old
no-retry behaviour (born with the local-models transport, #24XJ, as a contrast to the
loading wait). It now asserts the new contract: a hosted 503 is asked again and the
call succeeds (`test_a_hosted_503_is_asked_again`).

## Evidence

- `implementer-backend-tests.log` — `tests.test_provider tests.test_hosted
  tests.test_provider_local`: 79 tests, all pass, including the 10 new ones:
  429 retried, Retry-After honoured (seconds, ms, HTTP date, clamp, garbage),
  exhaustion after 6 retries, 5xx retried, 401 not retried, local excluded, loading
  waited out, stop during the wait, hosted rate_limited retried / quota_exhausted not.
- Full backend discover run: 2318 tests, 5 failures — 4 pre-exist on a pristine
  `git archive HEAD` tree (test_remote_wire, test_roles ×2, test_sessions; another
  session's area), the 5th was the superseded 503 test, since updated.
- Scratch-tree configure + build + `ctest` per the repo commit procedure: see the
  commit itself.

## QA checklist

- [ ] A turn against a provider that answers 429 (or 503) once recovers and the pane
      shows the "Provider HTTP 429 · asking again in N s (retry n of 6)" line.
- [ ] Esc during the wait stops the turn (no further request, socket closed).
- [ ] A 401/403 still fails at once with the existing key-guidance sentence.
- [ ] Relay Free with a spent allowance still surfaces the quota sentence on the first
      refusal (no 6-retry pause), and the quota chip updates.
- [ ] A local model server's overflow 500 still names the context fix at once, and a
      cold load still shows "The local server is loading its model…".
