# Relay Free: hosted inference for the first ask

Status 2026-09-18: designed, and being built in the order of the phases below. The owner's spec is
quoted in full on card `#HG7K` (`issues/features/2026-09-18-relay-free-hosted-inference.md`); this
document is the design as it maps onto Relay, the HTTP contract, and what an operator needs.

## Why

A fresh install could not ask the agent anything until a provider key was found, pasted and tested.
Relay Free removes that step: a small, included allowance served by a Relay-operated gateway that
holds the upstream provider keys. Relay stays BYOK-first. A user with any provider key configured is
untouched, a BYOK request never passes through Relay's server, and the allowance is a resource
quota, not a degraded product.

## Shape

```text
Relay desktop ── Bearer <short-lived token> ──▶ api.relay-terminal.ai (gateway/)
                                                    │ installation identity, quotas,
                                                    │ role → provider mapping, spend ceilings,
                                                    │ request validation, streaming proxy
                                                    ▼
                                             provider APIs (server-held keys)
```

**Reasoning is medium and below** (owner, 2026-09-18). The desktop's effort picker offers Low
and Medium on Relay Free (`EFFORT_LEVELS["relay"]` in `presets.py`; Main defaults to Medium, Flash
to Low, Lite to the gateway's minimal), and the gateway clamps whatever it is sent to each role's
`max_effort`, so a modified client asking for more gets medium, not an error. Since 2026-09-21 the
picker is greyed on every Relay Free row (`effort_fixed`, card #MDP1): the clamp that decides is
the gateway's, per role, so a control the user could move would only pretend.

The desktop knows three abstract models, `relay-main`, `relay-flash` and `relay-lite`. They are the
Main, Flash and Lite tiers of one more provider row, `relay-free`, in `backend/relay_core/presets.py`;
every chore in Relay already asks for a tier through `RoleResolver`, so nothing else learned a new
concept. The gateway owns which upstream model serves each role, its reasoning settings and its
output cap, so the mapping changes without a desktop release.

| Role | Serves | First upstream (Phase 1) | Later |
|---|---|---|---|
| `relay-main` | agent turns, planning, audits, judgment | OpenRouter `z-ai/glm-5.3-flash` | a GLM host |
| `relay-flash` | terminal driving, subagents, short coding loops | OpenRouter `deepseek/deepseek-v4.1-flash` | DeepSeek direct |
| `relay-lite` | routing, titles, labels, tiny decisions | OpenRouter `google/gemini-3.5-flash-lite` | Gemini direct, once the data-use terms are confirmed for a billed project |

Every Phase 1 upstream is a pay-as-you-go API on one OpenRouter key. Subscription plans (the GLM
Coding Plan, Kimi Code, MiniMax Token Plan) are never used upstream: their terms forbid backing a
service with an individual plan.

## What the desktop does

- **Identity.** On first use the backend makes an X25519 keypair (`backend/relay_core/hosted.py`,
  reusing `remote/identity.py`'s storage: the Secret Service keyring, else a 0600 file under
  `$XDG_DATA_HOME/relay/hosted/`). It is a separate key from the remote-access identity, so
  regenerating one never breaks the other. The private half never leaves the machine; the
  installation id is derived from the public half by the server.
- **Token.** `POST /v1/challenge`, prove possession with `HMAC-SHA256(DH(static, ephemeral), challenge)`,
  `POST /v1/register`, keep the token in memory and refresh it before it expires or after a 401.
  Tokens live an hour and carry nothing but a random string.
- **Requests.** `HostedChatProvider` in `backend/relay_core/provider.py` is the ordinary
  OpenAI-compatible transport with the token as the bearer. It reads the `X-Relay-Quota-*` headers
  and emits a `hosted_quota` event (protocol section 13.9) so the pane can show "Free · 73% left".
- **Default provider.** A pane with no stored key and no saved provider lands on Relay Free
  (`src/Pane.h`, the `presets` handler). The first time a pane configures it, one line in the
  transcript says what is sent where. Any BYOK key wins over Relay Free in the default order.
- **When it is exhausted or down.** The gateway answers with a stable error code
  (`quota_exhausted`, `free_unavailable`, `rate_limited`); the pane words it, names the user's
  configured providers, and the terminal is untouched.

## Gateway HTTP contract

| Route | Body | Reply |
|---|---|---|
| `POST /v1/challenge` | — | `{challenge, ephemeral_public, expires_in}` |
| `POST /v1/register` | `{static_pubkey, challenge, proof, client: {version}}` | `{installation_id, token, expires_in, plan, quota: {limit, used, resets_at}}` |
| `POST /v1/chat/completions` | OpenAI chat shape; `model` is a role id | SSE stream; headers `X-Relay-Quota-Limit`, `X-Relay-Quota-Used`, `X-Relay-Quota-Resets-At` |
| `GET /v1/quota` | bearer | `{limit, used, resets_at, plan}` |
| `GET /v1/health` | — | `{ok, roles, open}` |

Errors are `{"error": {"code", "message", "resets_at", "retried"}}` with codes `bad_request` (400),
`token_expired` (401), `quota_exhausted` and `rate_limited` (429), `free_unavailable` (503).

`retried` is present only when the gateway had already sent this request to more than one upstream
before giving up, and it is the number of those extra attempts. **The gateway owns the upstream
retries** (owner, 2026-09-19): the desktop transport retries a hosted 429 or 5xx six times of its
own, which re-ran the gateway's whole upstream chain each time, so a single refusal was paid for
twice over. `HostedChatProvider` now treats a refusal carrying `retried` as final and does not wait
at all; a `rate_limited` window the gateway reports is still waited out, because that is the
gateway's own door rather than an upstream's. The status set the gateway fails over on is the same
one the client retries (`proxy.RETRYABLE_STATUSES` == `ChatProvider.HTTP_RETRY_STATUSES`, asserted
in `tests/test_gateway.py`; the box runs `gateway/` and `remote/` only, so they cannot share a
module). **This half of the change needs the gateway redeployed** — see "Operating it" below — and
until then hosted refusals simply retry as they did before.

The gateway accepts exactly `model, messages, tools, tool_choice, temperature, top_p, max_tokens,
stream, response_format, stream_options` and rebuilds the upstream request from those fields plus
the role's own settings. A client cannot name an upstream model, a provider URL, a header, a
credential or an unbounded output size. The client's `reasoning_effort` (or `reasoning.effort`) is
read, clamped to the role's `max_effort` and sent in the upstream's dialect; `thinking` is refused.

## Quotas and spend

All limits are server-side and remotely configurable (`gateway/gateway.example.json`): tokens per
installation per day, requests per minute, concurrency per installation, global concurrency,
spend per day and per month, spend per provider per day, registrations and challenges per IP per
hour. Token quotas are settled from the provider's `usage` chunk, estimated at four characters a
token when a provider sends none. When a ceiling is hit the gateway says `free_unavailable` and
`/v1/health` reports `open: false`; if the quota store itself fails, requests fail closed.

`relay-lite` is capped at 512 output tokens to start, not the 32 in the spec: Relay's Lite chores
today produce titles, tab labels and 320-character session summaries, and route assist measured
20-token replies truncating when thinking was on. The cap is server config; tune it down after
measuring real replies.

## Privacy

- Relay Free sends a pane's prompts, conversation and tool results to the gateway and on to the
  provider serving the role. The desktop says so the first time a pane uses it, in Options ›
  Privacy, and in the keys modal.
- The gateway logs one line per request: time, a hash of the installation id, role, provider,
  status, time to first token, total time, token counts, error code. Provider keys are excluded.
  The gateway also has a diagnostic body-logging switch, `GATEWAY_DIAGNOSTIC_BODIES=1`;
  its use is announced at startup.
- BYOK requests never touch the gateway. Picking any other provider, or removing the row, turns
  Relay Free off for that pane.
- Gemini: do not move `relay-lite` to Google's API until the data-use terms for the project are
  confirmed to exclude model training; a billed project is the expected answer. OpenRouter is the
  interim.

## Abuse

Relay is open source, so a hostile client can fabricate installation ids, strip every client-side
limit and call the gateway directly. Nothing on the client is trusted: not counters, not token
counts, not role declarations. The v1 controls are the installation token, per-installation
quota, per-IP limits on registration, global concurrency, and the spend ceilings. Stronger
identity (GitHub or email sign-in, proof of work at registration) is Phase 5, only if real use
shows the need.

## Operating it

`gateway/README.md` has the commands. In short: a systemd unit (`gateway/relay-gateway.service`)
runs `python3 -m gateway.server` as its own user with `EnvironmentFile=/etc/relay-gateway/env`
holding the provider keys, `/etc/relay-gateway/gateway.json` holding the role table and limits,
and `/var/lib/relay-gateway/gateway.db` holding installations, usage and spend. A cloudflared
ingress rule sends `api.relay-terminal.ai` to the gateway's loopback port, bypassing nginx. The
box needs `python3-cryptography`. DNS and the tunnel ingress are applied by hand: `deploy.sh`
touches neither.

## What exists today

| Piece | State |
|---|---|
| `gateway/` package, tests in `tests/test_gateway.py` | Phase 1 |
| `relay-free` preset, `hosted.py`, `HostedChatProvider`, roles, protocol 13.9 | Phase 2 |
| Desktop default, disclosure line, keys-modal row, quota chip, exhausted message | Phase 3 |
| Site and docs | this document, `site/free.html`, README and ROADMAP updates |
| Live gateway at `api.relay-terminal.ai` | **live since 2026-09-18 20:14 UTC**: `relay-gateway.service` on the Hetzner box, the shared tunnel's first ingress rule, a proxied CNAME; the desktop's client streamed a Lite reply through it in 1.5 s |
| Cost-aware routing, provider health, stronger identity, dashboard | Phase 4–5, not started |

Card `#HG7K` records the phases' commits and QA evidence.
