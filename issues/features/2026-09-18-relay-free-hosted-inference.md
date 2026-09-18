---
id: HG7K
type: work
status: in-progress
labels: [feature, providers, privacy]
component: [worker, gui, gateway]
milestone: desktop-alpha
workstream: providers
rank: zzzzzn
created: '2026-09-18'
source: owner, in a Claude Code session, 2026-09-18, with a written spec ("Relay Free Hosted Inference")
links: {plans: ['docs/RELAY-FREE.md'], commits: [], evidence: [], related: ['24XJ', 'W5N2'], github: null}
---
# Relay Free: the agent works on a fresh install, with no API key

## Issue

> to boost usage i want to set up a free mode for new users. read the below carefully and
> objectively, develop a plan for being able to use relay AI instantly on install.

The owner's spec, lightly condensed where it listed the same thing twice (its code blocks and lists
are kept; the decisions in it are unchanged):

> # Relay Free Hosted Inference
>
> ## Purpose
>
> Add an optional hosted inference service to Relay so that a newly installed copy of Relay can use the AI agent immediately without requiring the user to configure an API key.
>
> Relay should remain BYOK-first for heavy users. Relay Free is intended to remove onboarding friction and provide a modest included allowance.
>
> The user's provider credentials must never be embedded in the Relay binary or distributed to clients. All Relay-funded inference should pass through a small Relay-hosted gateway.
>
> ## Proposed model roles
>
> Initial Relay Free configuration:
>
>     Relay Free
>         ├── Main    GLM-5.3-Flash
>         ├── Flash   DeepSeek V4.1 Flash
>         └── Lite    Gemini 3.5 Flash-Lite
>
> These should be exposed to the desktop as abstract Relay roles: relay-main, relay-flash, relay-lite.
>
> Do not make the client depend on the underlying provider/model names. The server should own the mapping so models can be changed without releasing a new Relay version.
>
> Example server configuration:
>
>     models:
>       relay-main:  {provider: openrouter, model: z-ai/glm-5.3-flash}
>       relay-flash: {provider: deepseek,   model: deepseek-v4.1-flash}
>       relay-lite:  {provider: google,     model: gemini-3.5-flash-lite}
>
> ## Intended role of each model
>
> ### Main: GLM-5.3-Flash
>
> Use for: normal agent turns, complex user requests, planning, request audits, long-context work, tasks that need stronger judgment. The reason for using GLM-5.3-Flash is the combination of good coding/agent capability, very large context, and unusually low PAYG cost.
>
> ### Flash: DeepSeek V4.1 Flash
>
> Use for: terminal-driving loops, subagents, code-search tasks, short coding operations, repeated tool-use loops, jobs where fast coding-agent behavior matters more than broad reasoning. Its cache economics may also be particularly useful for repeated agent context.
>
> ### Lite: Gemini 3.5 Flash-Lite
>
> Use only for very short, latency-sensitive chores: command routing, classification, title generation, duplicate detection, tiny structured decisions, simple suggestions where appropriate. The main reason for Gemini here is latency, not token price.
>
> For Lite calls, configure requests to minimize latency and output: thinking minimal, temperature 0, max_output_tokens 8–32, structured output where possible. For command routing, prefer a fixed enum rather than prose, e.g. {"route": "SHELL"} with allowed values SHELL, MAIN, FLASH, ASK. Do not ask the Lite model to explain its decision.
>
> ## High-level architecture
>
>     Relay desktop  --Relay access token-->  api.relayterminal.org  (auth, quotas, model mapping, abuse protection, request validation)  -->  provider APIs (GLM, DeepSeek, Gemini)
>
> Provider API keys exist only on the Relay server. The client must never receive, derive, log, cache, or otherwise have access to the upstream provider credentials.
>
> ## Relationship to existing BYOK
>
> Relay Free should be another provider option, not a replacement for the existing BYOK design.
>
>     User has configured provider key  →  use BYOK provider normally
>     User has no provider key          →  use Relay Free if enabled and quota remains
>     Relay Free quota exhausted        →  show a clear message and offer configured BYOK providers
>
> Relay should continue supporting GLM Coding Plan, Kimi Coding Plan, MiniMax plans, OpenRouter, OpenAI, Anthropic, Gemini, and arbitrary OpenAI-compatible endpoints. Do not route a user's BYOK request through the Relay gateway unless there is a specific later reason to do so.
>
> ## First-run experience
>
> Current: install Relay → configure API provider → obtain API key → enter key → test key → finally use agent.
> Target: install Relay → open Relay → ask agent something → it works.
>
> The first-run model selector should show something like: "Relay Free — Included · No API key required — Main GLM / Flash DeepSeek / Lite Gemini". Underlying model names may optionally appear in a detail view, but the normal user-facing abstraction should remain Main / Flash / Lite.
>
> ## Client identity
>
> Do not require user signup for the initial free allowance. On first run: generate a random installation ID; generate an asymmetric keypair; store the private key in KWallet / GNOME Keyring; send the public key to the Relay gateway; receive an installation record and short-lived access token. The private key must never leave the machine. The installation ID is not a security boundary by itself. Because Relay is open source, assume that hostile clients can fabricate installation IDs or remove all client-side restrictions.
>
> ## Authentication
>
> Use short-lived Relay access tokens (Authorization: Bearer <relay-token>). The token should encode or refer to installation_id, issued_at, expires_at, plan. Suggested lifetime 30–60 minutes. Clients should transparently refresh tokens. Do not put upstream provider credentials into these tokens.
>
> ## Request API
>
> Prefer an OpenAI-compatible API surface (POST /v1/chat/completions) with {"model": "relay-main", "messages": [...], "tools": [...], "stream": true}. The gateway translates relay-main → current configured provider → current configured upstream model, and must reject arbitrary model names. Do not allow the client to specify arbitrary provider URLs, arbitrary upstream models, upstream API headers, provider credentials, or unbounded output sizes.
>
> ## Streaming
>
> The gateway must preserve streaming and should not buffer complete responses. client request → gateway validation → upstream request → stream upstream SSE immediately → Relay displays tokens as they arrive. Add as little latency as possible.
>
> ## Quotas
>
> All quotas must be enforced server-side. Do not trust request counters in the client, token counts submitted by the client, client model-role declarations, or client-side daily limits. Track at minimum installation_id, requests_today, input_tokens_today, output_tokens_today, current_concurrency, last_request_at. Prefer token-based quotas over request-based quotas. Initial quota should be configurable remotely (example only: 250,000 tokens/day or 1,000,000 tokens/month). Do not hard-code this into the desktop application.
>
> ## Cost controls
>
> The server should enforce maximum input tokens per request, maximum output tokens per request, maximum concurrent requests per installation, maximum daily/monthly tokens, maximum tool-call loops where relevant. For example relay-main max_output_tokens 16000, relay-flash 12000, relay-lite 32. Exact values should remain server-configurable.
>
> ## Lite routing
>
> For chores where latency matters much more than intelligence, call relay-lite: command routing (input: the typed text and terminal state; output: SHELL), agent routing (MAIN or FLASH), title generation (at most six words). Keep prompts small; avoid sending large conversation histories to Lite when a compact derived input will suffice.
>
> ## Privacy
>
> Relay's current privacy posture is a major product feature and should remain explicit. Users must be told that Relay Free sends prompts and necessary tool context to Relay's hosted service. Relay's server should avoid storing prompt content unless strictly required; do not create analytics logs containing prompt or response text; server logs should contain metadata only where possible (timestamp, installation_id hash, model role, provider, latency, HTTP status, input/output token counts, error type). Avoid storing prompt text, model output, file contents, terminal output, tool results, API keys. If request logging is ever needed for debugging, make it an explicitly enabled temporary diagnostic mode.
>
> ## Gemini privacy issue
>
> Before routing user content through Gemini's free API tier, confirm the applicable data-use terms. If Google's free tier permits model-improvement use of submitted data, do not silently send sensitive Relay content through that tier. Possible policies: use paid Gemini API traffic for Relay Lite; use Gemini only for small derived inputs such as classifications; replace Gemini with another Lite provider if necessary.
>
> ## Provider abstraction
>
> Implement providers independently from Relay roles: Role → Model mapping → Provider adapter → Upstream API. Changing provider/model mappings must not require a Relay desktop release.
>
> ## Failover
>
> Each role should support ordered fallbacks, for provider outage, rate limit, provider quota exhaustion, temporary timeout. Do not silently fall back from a cheap model to a much more expensive model without a cost ceiling.
>
> ## Cost-aware routing
>
> Eventually the gateway should know estimated request cost, provider health, latency, quota state, cache state, and route on capability, latency, cost and availability. For v1, static mappings are enough.
>
> ## Server technology
>
> The gateway does not need GPUs. It needs HTTPS, streaming HTTP, secrets storage, authentication, rate limiting, a small persistent quota store, low latency. Cloudflare Workers are one suitable option. The design should not depend tightly on Cloudflare. A small conventional server should also work.
>
> ## Abuse assumptions
>
> Assume hostile users can modify Relay source, compile custom clients, issue gateway requests outside Relay, generate unlimited installation IDs, fake client version information, bypass every client-side limit. Server-side controls are mandatory: installation token, per-install quota, per-IP rate limits, global concurrency limits, global spend ceiling, provider-specific spend ceilings. Do not attempt invasive device fingerprinting for v1. If abuse becomes material: GitHub login, email login, proof-of-work, CAPTCHA during registration, verified accounts for larger quotas. The initial product should favor low-friction onboarding.
>
> ## Global spend protection
>
> Required before public release: daily global spend limit, monthly global spend limit, per-provider limit, per-role limit. When a limit is reached, "Relay Free temporarily unavailable" is preferable to an unexpected provider bill. The server should fail closed if cost-accounting state is unavailable.
>
> ## Observability
>
> Provide an internal dashboard or structured metrics for requests by role, tokens by role and provider, estimated cost, latency p50/p95, TTFT p50/p95, errors, rate limits, fallback frequency, active installations, quota exhaustion. For Lite track TTFT / total classification latency.
>
> ## Client UI
>
> Model selector: Main / Flash / Lite each "Relay Free"; expanded detail "Relay Free — No API key required — Usage resets daily". Quota indicator such as "Relay Free · 73% remaining" (avoid fake precision if accounting is delayed). Quota exhausted: "Relay Free allowance used for today. Use one of your configured providers: GLM, Kimi, MiniMax, OpenRouter, or try Relay Free again after reset." Do not make quota exhaustion break normal terminal use.
>
> ## Existing Relay model-role integration
>
> The hosted service should slot into the existing Main/Flash/Lite roles: Provider "Relay Free": Main → relay-main, Flash → relay-flash, Lite → relay-lite. The rest of Relay should continue asking for logical roles.
>
> ## Suggested role assignments
>
> Agent turns, planning, request audit, complex summarization → Main. Subagents, terminal driving, short coding loops → Flash. Command routing, titles, classification, duplicate detection, very short suggestions → Lite. Do not automatically put all summaries on Lite.
>
> ## BYOK upgrade path
>
> "Relay Free is included for light use. For higher limits or direct provider access: add GLM / Kimi / MiniMax / OpenRouter / OpenAI / Anthropic / Gemini." Do not artificially degrade Relay Free to force upgrades.
>
> ## Provider subscriptions
>
> Do not build production Relay Free around multiple personal Coding Plan accounts unless the provider explicitly authorizes this use. Use ordinary PAYG APIs unless there is written permission or a provider partnership.
>
> ## Future provider sponsorship
>
> The architecture should allow a provider to sponsor Relay usage ("Relay Free powered by Z.ai") with no desktop changes.
>
> ## Implementation phases
>
> Phase 1: minimal hosted provider (relay-main, one provider, server proxy, server-held key, installation identity, short-lived token, streaming, per-install quota, global spend ceiling).
> Phase 2: Main / Flash / Lite (role mappings, GLM Main, DeepSeek Flash, Gemini Lite, adapters, fallback).
> Phase 3: UI and onboarding (Relay Free as default first-run provider, quota display, provider explanation, BYOK fallback, quota-exhaustion UX).
> Phase 4: cost-aware routing (provider health, pricing configuration, automatic failover, cost ceilings, latency monitoring).
> Phase 5: abuse hardening, only after real usage demonstrates a need.
>
> ## Acceptance criteria
>
> 1. A fresh Relay install can make an agent request without configuring an API key.
> 2. No upstream API credential exists anywhere in the client.
> 3. All Relay Free quotas are enforced server-side.
> 4. A modified client cannot request arbitrary upstream models.
> 5. Streaming works with negligible additional latency.
> 6. Relay Main / Flash / Lite roles work through the hosted provider.
> 7. Provider mappings can be changed server-side without a desktop release.
> 8. A hard global spend cap prevents unexpected charges.
> 9. Hosted request logs do not contain prompts, responses, file contents, or provider keys.
> 10. BYOK continues to operate independently.
> 11. When Relay Free is unavailable or exhausted, terminal functionality remains unaffected.
> 12. Lite routing can return a minimal structured response with sub-second latency under normal conditions.
>
> ## Core design principle
>
> The desktop should know that it has access to Main, Flash, Lite. The server should know which provider, which model, what it costs, whether it is healthy, whether quota remains. Keep those responsibilities separate.

## Decisions

- **API host `api.relay-terminal.ai`**, not `api.relayterminal.org` (owner, 2026-09-18): the site and
  the rendezvous plan are on `relay-terminal.ai`.
- **The gateway is Python on the existing Hetzner box behind cloudflared** (owner, 2026-09-18), a
  `gateway/` package beside `rendezvous/` on `remote/httpd.py`, not a Cloudflare Worker. Same
  language, same in-process test harness, one deploy story.
- **Phase 1 upstreams are all on the owner's OpenRouter key** (owner, 2026-09-18): every target
  model is on OpenRouter as pay-as-you-go, and the GLM Coding Plan key is never used upstream.
  DeepSeek direct and Gemini direct are a server config change later.
- Opaque random token plus a database row, not a self-describing token: the DB is read on every
  request for quotas anyway, and an opaque token is revocable.
- A separate installation keypair for Relay Free, not the remote-access identity: regenerating one
  must not break the other. Same storage code (`remote/identity.py`), different keyring attribute.
- JSON config, not YAML: the Python policy here is the standard library only.
- `relay-lite` starts at 512 output tokens, not 32. Today's Lite chores produce titles, tab labels
  and 320-character session summaries, and route assist measured 20-token replies truncating with
  thinking on. Server config; tune down after measuring.
- The first-use disclosure is one inline transcript line, not a modal, so "open, ask, it works"
  holds. Options › Privacy and the keys modal carry the full text.
- **Medium reasoning and below** (owner, 2026-09-18): the picker offers Low and Medium on Relay
  Free, Main defaults to medium, Flash to low, Lite to minimal, and the gateway clamps anything
  higher to each role's `max_effort`.

## Tasks

- [ ] `gateway/`: config, store, validation, streaming proxy, routes, systemd unit, README, tests (Phase 1) <!-- t:g1 -->
- [ ] `relay-free` preset, `hosted.py`, `HostedChatProvider`, roles, `provider_config`, keytest, `presets` row, `hosted_quota`, protocol 13.9, tests (Phase 2) <!-- t:b2 -->
- [ ] Desktop: usable gate, first-run default, disclosure line, mirror row, keys-modal group and row, quota chip, exhausted message, privacy copy (Phase 3) <!-- t:d3 -->
- [ ] Docs and site: `docs/RELAY-FREE.md`, ROADMAP and WARP decision rows, README privacy, `site/index.html`, `site/free.html` (Phase 4) <!-- t:s4 -->
- [ ] Deploy: `python3-cryptography`, unit, env file, cloudflared ingress, DNS (owner), live health check <!-- t:p5 -->
- [ ] QA evidence under `docs/qa_evidence/2026-09-18-relay-free/` <!-- t:q6 -->

## What is left

- The phone view's copy of the quota chip (`src/PaneState.h`, `app/pane.js`): those files belong
  to the pane-view session today.
- Moving session summaries from the `chores` role (Lite) to `summaries` (Flash), as the spec
  suggests: one line in `roles.ROLE_TIERS`, but it changes every provider's behaviour, so it is a
  separate decision.
- Tuning the `relay-lite` cap toward the spec's 32 after measuring live replies.
- Gemini direct for Lite: confirm the data-use terms on a billed project first.
- Phase 4–5: cost-aware routing, provider health, a metrics dashboard, stronger identity.
