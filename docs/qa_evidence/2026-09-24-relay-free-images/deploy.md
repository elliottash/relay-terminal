# #Z82M — gateway deploy record, 2026-09-24

The `relay-image` role is live on api.relay-terminal.ai.

## What was deployed

- Box: elliott-main-1 (138.201.189.28), service `relay-gateway`, code `/opt/relay`, config
  `/etc/relay-gateway/gateway.json`.
- Code: a clean `git archive main` export (never the working tree), pushed with
  `rsync -a --delete` to `/opt/relay/gateway/` and `/opt/relay/remote/`. `gateway/` and `remote/`
  are byte-identical to `fc0cc0eb` ("Gateway: the example image price is the deploy value, 0.006
  per picture"), the last commit to touch them before the deploy; verified with
  `git log fc0cc0eb..<tip> -- gateway remote` (empty).
- Config merge (idempotent, from the deployed example config):
  - `roles["relay-image"]` — kind `images`, upstream OpenRouter
    `black-forest-labs/flux.2-klein-4b`, resolutions 1024x1024 (default), 1536x1024, 1024x1536,
    aspect ratios 1:1, 3:2, 2:3, `max_input_chars` 4000.
  - `providers.openrouter.price_per_image["black-forest-labs/flux.2-klein-4b"] = 0.006` — the
    largest served resolution rounded up (live list price ≈ $0.0035 at 1024x1024, ≈ $0.0055 at
    1536x1024, fetched 2026-09-24), so the flat per-picture charge never under-counts.
  - `quota.images_per_day = 10` (pictures per install per UTC day, counted separately from tokens).
- Startup config check passed (`gateway.config.load` with the env file sourced), then
  `systemctl restart relay-gateway`; `systemctl is-active` → `active`, exit status 0 end to end.
- Backups on the box before any change: `/opt/relay.pre-images` (code) and
  `/etc/relay-gateway/gateway.json.pre-images` (config).
  Rollback: swap the code back, restore the config, restart.

## Verified after the restart

- `GET https://api.relay-terminal.ai/v1/health` → `ok: true`, roles
  `relay-flash, relay-image, relay-lite, relay-main`, and the new `images` block:
  `relay-image` / `black-forest-labs/flux.2-klein-4b`, `price_usd: 0.006`, the three resolutions
  and three aspect ratios above.
- `POST https://api.relay-terminal.ai/v1/images` without a token → `401` (the route exists and its
  admission checks answer before anything spends).

## Not verified here

The person probe on a fresh keyless install ("Make me an image of a lighthouse" shows the image
inline, the quota chip counts it, past the allowance the pane words `quota_exhausted`, a BYOK
person untouched) — that is the card's remaining verification, for a desktop build from main
against the now-live gateway.
