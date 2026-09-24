---
id: Z82M
type: work
status: planned
labels: [feature, providers, gateway, media]
component: [gateway, worker, gui]
milestone: beta
rank: zzzzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: system, primary: script, also: [probe, person], human: required, criteria: 'on a keyless fresh install, ''Make me an image of a lighthouse'' shows an image inline and the quota chip counts it; past the daily image allowance the pane words quota_exhausted; a person with an OpenRouter key is untouched', sign_off: none, effort: medium, stakes: money, blast: capability}
source: Claude Fable session in Relay, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [9HS0, HG7K, 6VMF], github: null}
---
# Relay Free serves images: a relay-image role on the gateway backed by the cheap OpenRouter image model

## Issue
add the cheap openrouter image generator to relay free.

## Done means
- On a fresh install with no key, an agent turn that calls `media_generate {kind: image}` produces an image inline, served by the Relay Free gateway on the cheap OpenRouter image model (`black-forest-labs/flux.2-klein-4b`, already `IMAGE_DEFAULT` in `backend/relay_core/media.py:28`). No key dialog, no "image needs an openrouter key" refusal.
- The gateway counts images against the installation's allowance and the operator's spend ceilings, and a served image model with no price is refused at startup like a chat model is.
- Past the daily image allowance the gateway answers `quota_exhausted` and the pane words it, naming the person's own providers; a person with an OpenRouter key never passes through Relay's server for images.
- `media_catalog` and `media_quote` show Relay Free as the image provider with its remaining allowance and the price the gateway charges against it.
- `docs/RELAY-FREE.md`'s role table has the `relay-image` row, and the README's privacy section says that on Relay Free an image prompt goes to Relay's server.
- Failure looks like: a keyless install still refused for images; an image not counted on the quota chip; the operator ceiling not covering images; a BYOK user's image request logged on the gateway.

## Plan
### Goal
Relay Free gains an image role so a keyless install can make a picture, on the cheap OpenRouter image model, counted and capped like chat. Owner, 2026-09-24: "add the cheap openrouter image generator to relay free." With this, #6VMF's "Generate some art" recipe needs no "needs a provider" line on a fresh install.

### Findings
- Images today: `backend/relay_core/media.py` posts to OpenRouter `/images` with the person's own `openrouter` key (`_select` refuses without one: "image needs a openrouter key"); `IMAGE_DEFAULT = "black-forest-labs/flux.2-klein-4b"`, `IMAGE_MODELS` also lists `google/gemini-3.1-flash-lite-image` (`media.py:28-29`); `media_quote` prices from the endpoint's `output_image` per-megapixel rate (`media.py:399-416`); results are saved and shown inline (`docs/INLINE_MEDIA.md`).
- Gateway (`gateway/`): routes `/v1/challenge`, `/v1/register`, `/v1/chat/completions`, `/v1/pro`, `/v1/quota`, `/v1/health` (`gateway/server.py:6-11`); roles `relay-main`, `relay-flash`, `relay-lite` in `gateway/gateway.example.json`; `config.py` requires `price_per_mtok` for every served model and costs a call from tokens (`cost_micros`, `config.py:67-78`); quotas and ceilings are in tokens (`store.py`).
- Desktop: the `relay-free` preset rows (`backend/relay_core/presets.py:262, 405-411, 487-490`), the hosted token (`backend/relay_core/hosted.py`), `HostedChatProvider` in `provider.py`, the `hosted_quota` event (protocol 13.9), the quota chip (`src/Pane.h:5431-5560`).
- `docs/RELAY-FREE.md` role table names three roles; the README privacy section says a BYOK request never passes through Relay's server.

### Steps
1. **Gateway config.** A role may have `kind: "images"`; its upstream is priced with `price_per_image` (USD, one entry per model, refused at startup when missing like `price_per_mtok`), and `cost_micros` gains an image branch. `gateway.example.json` gets `relay-image` → OpenRouter `black-forest-labs/flux.2-klein-4b` with a default `resolution` and the resolutions it accepts.
2. **Gateway route.** `POST /v1/images` with the same bearer and admission checks as chat: body `{model: "relay-image", prompt, resolution?, aspect_ratio?}` validated (prompt 1–4000 chars, only the listed settings), forwarded to OpenRouter `/images` with the operator key, the reply's `b64_json` returned unchanged with the `X-Relay-Quota-*` headers. The image is counted against the installation's daily allowance as a separate **image count** (default 10 per day, in `gateway.json`) and against the operator ceilings in USD; `quota_exhausted`, `free_unavailable` and `rate_limited` are answered in the existing words. `/v1/health` lists the role. Logged as one line like chat, prompt never logged.
3. **Desktop.** `media.py`: provider `relay-free` for images when no `openrouter` key is stored (or when named), sending the request through `hosted.py`'s token to the gateway; `catalog()` shows Relay Free with its remaining image allowance; `media_quote` returns the gateway's price and the allowance; the `hosted_quota` event carries the image count so the chip can say "Free · 73% · 4 images left".
4. **Docs.** `docs/RELAY-FREE.md` role table row and the HTTP contract section for `/v1/images`; protocol 13.9 fields; README privacy paragraph: on Relay Free an image prompt goes to Relay's server, with the person's own key it does not; `gateway/README.md` operator note for the env and the allowance.
5. **#6VMF.** The "Generate some art" recipe's needs check passes when `/v1/health` lists `relay-image` or an `openrouter` key exists.

### Risks
- Money: an image costs far more than a chat turn, so the allowance is a count per install per day and the operator ceiling must include images before the route is enabled; the owner sets the number in `gateway.json` (recommendation: 10 per day) and deploys the box, which no session here can do.
- Content: OpenRouter's and the model's policies apply; the gateway forwards the upstream's refusal as it does for chat.
- Which cheap model: `flux.2-klein-4b` is the desktop's default already; the Gemini Flash Lite image model is the alternate in `IMAGE_MODELS`; the price check at startup settles which is cheaper on the day.

### Verify
- `PYTHONPATH=. python3 -m unittest tests.test_gateway -v` with the fake upstream serving `/images` (role kind, per-image pricing, count and ceiling, exhaustion).
- `PYTHONPATH=backend python3 -m unittest tests.test_media -v` (Relay Free provider selection, quote, catalog).
- Probe after deploy: a fresh `XDG_CONFIG_HOME` install asks for an image and the file appears inline; `curl /v1/health` lists `relay-image`; evidence in `docs/qa_evidence/<date>-relay-free-images/`.
