# relay-gateway — operator notes

The service behind Relay Free: `api.relay-terminal.ai`. A fresh Relay install registers an
installation here, takes a short-lived token, and sends chat completions with `model` set to a
role (`relay-main`, `relay-flash`, `relay-lite`); the gateway rebuilds each request, adds the
operator's provider key, streams the reply and counts tokens against the installation's daily
allowance and the operator's spend ceilings. Design and HTTP contract: `docs/RELAY-FREE.md`.

Stdlib Python 3.10+ plus `python3-cryptography` (for the X25519 registration proof, shared with
`remote/noise.py`). Nothing is pip-installed.

## Install on the box

```sh
# 1. Code: the repo checkout the box already has, or a copy of gateway/ and remote/.
sudo mkdir -p /opt/relay && sudo rsync -a gateway remote /opt/relay/
sudo apt install python3-cryptography

# 2. A user with no shell and no home.
sudo useradd --system --no-create-home --shell /usr/sbin/nologin relay-gateway

# 3. Config and keys. The keys go in the env file, never in gateway.json.
sudo mkdir -p /etc/relay-gateway
sudo cp gateway/gateway.example.json /etc/relay-gateway/gateway.json
sudo install -m 0600 -o root -g root /dev/null /etc/relay-gateway/env
printf 'GATEWAY_OPENROUTER_KEY=sk-or-...\n' | sudo tee /etc/relay-gateway/env >/dev/null

# 4. The unit. StateDirectory= creates /var/lib/relay-gateway for the SQLite file.
sudo cp gateway/relay-gateway.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now relay-gateway
curl -s http://127.0.0.1:8790/v1/health
```

Check the config without starting anything: `python3 -c 'import gateway.config as c; c.load(
"/etc/relay-gateway/gateway.json")'` from `/opt/relay` with the env file sourced. A served model
with no price, or a provider whose key variable is unset, is refused at startup on purpose: the
spend ceilings must be able to cost every call.

## cloudflared ingress

The gateway listens on loopback only; cloudflared brings it to the public name. Add one rule to
the tunnel's ingress list, **before** the catch-all and before any nginx rule, so it bypasses
nginx:

```yaml
ingress:
  - hostname: api.relay-terminal.ai
    service: http://localhost:8790
    originRequest:
      noHappyEyeballs: true
      # Streams are long; do not let the tunnel cut a slow reply.
      connectTimeout: 30s
  # ... the existing site rules and the http_status:404 catch-all
```

Then `cloudflared tunnel route dns <tunnel> api.relay-terminal.ai` (DNS is the owner's step).
Because every request now arrives from cloudflared, `client_ip_header` in the config must be
`CF-Connecting-IP` (as in the example) or the per-address registration limits would throttle
everyone as one address. Leave it unset only when the port is reached directly.

## The config (`gateway.example.json`)

| Key | Meaning |
|---|---|
| `roles.<role>.upstreams` | Ordered list of `{provider, model, extra}`. On a 429, a 5xx or a connect timeout **before the first byte**, the next one is tried. `extra` is merged over the client's fields (the operator's `temperature` wins); it may not set `reasoning` or `reasoning_effort`, which the role's `effort` owns. |
| `roles.<role>.effort` | Reasoning effort when the client names none: `none`, `minimal`, `low`, `medium`, `high`, `xhigh` or `max`, in that order. The example: `medium` for main, `low` for flash, `minimal` for lite. |
| `roles.<role>.max_effort` | The ceiling. A client's own `reasoning_effort` (or `reasoning: {effort}`) above it is clamped to it silently, never refused; an unknown value is a 400. Relay Free runs at `medium` and below (owner, 2026-09-18), so the example caps all three roles there. |
| `roles.<role>.max_output_tokens` | Clamp on the client's `max_tokens`. `relay-lite` starts at 512, not the spec's 32: titles, tab labels and 320-char summaries need it (decision on the feature card). |
| `roles.<role>.max_input_chars` | Largest request body for that role, in bytes. |
| `providers.<name>.base_url` | HTTPS only. Plain HTTP to loopback is allowed with `allow_insecure_loopback: true` (the tests) and refused otherwise. |
| `providers.<name>.key_env` | Environment variable holding the key. Read on every request, so restarting after editing the env file rotates it. |
| `providers.<name>.effort_style` | How the provider takes the effort: `reasoning` sends `{"reasoning": {"effort": "medium"}}` (OpenRouter); `reasoning_effort` sends `"reasoning_effort": "medium"` (OpenAI, DeepSeek, Gemini's OpenAI endpoint; the default); `none` sends nothing, for a model with no knob. |
| `providers.<name>.price_per_mtok` | `model -> [input USD per million, output USD per million]`. The example's numbers are OpenRouter's list prices as fetched from `https://openrouter.ai/api/v1/models` on 2026-09-18 (GLM-5.3-Flash 0.09/0.30, DeepSeek V4.1 Flash 0.15/0.60, Gemini 3.5 Flash-Lite 0.30/2.50); re-check them when a role's upstream changes, because they are what the ceilings count. |
| `providers.<name>.price_per_image` | `model -> USD per picture`, for a role of `kind: "images"`. Every model an images role serves must be priced here, exactly as every chat model must be in `price_per_mtok`: a served image model with no price is refused at startup. OpenRouter bills FLUX.2 Klein per image token, but the gateway charges one flat price at any resolution, so the example's 0.006 is the 2026-09-24 list price rounded up over the largest resolution it serves (about 0.0035 at 1024x1024, 0.0055 at 1536x1024) — the ledger never under-counts. |
| `roles.<name>.kind` | `"chat"` (the default) or `"images"`. An images role serves `POST /v1/images` on the upstream's `/images` endpoint, one picture per call, with `resolutions` (first is the default) and `aspect_ratios` it accepts and `max_input_chars` for the prompt. An images role also requires `quota.images_per_day` > 0 — never a route that spends with nothing counting it. |
| `quota` | Per installation: `tokens_per_day` (input plus output, UTC day), `requests_per_minute`, `concurrency_per_install`, `images_per_day` (pictures per day, counted separately from tokens; the example sets 10). |
| `limits` | `global_concurrency`, `spend_per_day_usd` and `spend_per_month_usd` (all providers together — an image's whole price is checked against these *before* the call, because it is known upfront), `per_provider_per_day_usd`, `registrations_per_ip_per_hour`, `challenges_per_ip_per_hour`. |
| `token_ttl_seconds` | How long a registration token lives (at least 60). Clients re-register silently. |
| `client_ip_header` | See cloudflared above. |
| `upstream_connect_timeout`, `upstream_stall_timeout` | Seconds; defaults 20 and 120. |

Edits take effect on restart: `sudo systemctl restart relay-gateway`.

## What the refusals mean

Every refusal is `{"error": {"code", "message", "resets_at"?}}`:

| Status | `code` | Why |
|---|---|---|
| 401 | `token_expired` | No token, an unknown one, or one past its TTL. The client registers again. |
| 400 | `bad_request` | A field outside the allow-list, `model` not a role, a malformed message, a body over the role's size. |
| 429 | `quota_exhausted` | The installation's `tokens_per_day` — or, on `POST /v1/images`, its `images_per_day` — is used; `resets_at` is the next UTC midnight. |
| 429 | `rate_limited` | Requests per minute, concurrent requests per installation, or per-address registration. |
| 503 | `free_unavailable` | A spend ceiling is hit, the gateway is at `global_concurrency`, no upstream answered, or the database failed (the gateway fails closed). |
| 502 | `free_unavailable` | An upstream refused the rebuilt request outright (a 4xx other than 429, or a redirect). Check the config and the key. |

`GET /v1/health` reports `open: false` while a global ceiling is hit; monitor that.

## The log

One line per request in the journal (`journalctl -u relay-gateway -f`):

```
chat install=3f1c9a0d52e1 role=relay-main provider=openrouter model=z-ai/glm-5.3-flash status=200 ttft_ms=412 total_ms=3180 in=1834 out=212 usage=reported error=- fallback=0 truncated=0
```

`install` is `sha256(installation_id)[:12]`, never the id. `usage=estimated` means the provider
sent no usage chunk and the tokens were counted at four characters each. No message content and
no key is ever logged. `GATEWAY_DIAGNOSTIC_BODIES=1` in the env file turns on body logging on the
`relay.gateway.bodies` logger for a diagnosis; it is off by default, the process warns loudly at
startup while it is on, and it must be unset again as soon as the problem is found.

## Looking at the database

`sqlite3 /var/lib/relay-gateway/gateway.db` (WAL mode: safe to read while the service runs).

```sql
-- Today's spend, by provider and role, in dollars.
SELECT provider, role, requests, input_tokens, output_tokens, cost_micros / 1e6 AS usd
  FROM spend WHERE day = date('now') ORDER BY usd DESC;

-- This month so far.
SELECT SUM(cost_micros) / 1e6 AS usd FROM spend WHERE day LIKE strftime('%Y-%m', 'now') || '-%';

-- Installations seen today, and ever.
SELECT COUNT(*) FROM installations WHERE last_seen > strftime('%s', 'now') - 86400;
SELECT COUNT(*) FROM installations;

-- The heaviest installations today.
SELECT installation_id, requests, input_tokens + output_tokens AS tokens
  FROM usage WHERE day = date('now') ORDER BY tokens DESC LIMIT 20;

-- Registrations and refusals over the last hour (events keep seven days of metadata).
SELECT kind, COUNT(*) FROM events WHERE at > strftime('%s', 'now') - 3600 GROUP BY kind;
```

## Raising a quota

The allowance is per plan in the config (`quota.tokens_per_day` applies to every installation
today; v1 has the one `free` plan). To give one installation more room for the rest of the day,
lower what it has used:

```sql
UPDATE usage SET input_tokens = 0, output_tokens = 0
  WHERE installation_id = '<id>' AND day = date('now');
```

To raise the allowance for everyone, edit `quota.tokens_per_day` and restart. A per-plan
allowance (`installations.plan` is stored and returned to the client) is the hook for a later
paid or sponsored tier; nothing reads it yet beyond echoing it.

Images are their own allowance: `quota.images_per_day` pictures per installation per day, in the
`image_usage` table, with the token quota untouched. The whole price of a picture is known before
the call, so unlike chat it is checked against `spend_per_day_usd` and `spend_per_month_usd`
*before* the upstream is called, and settled after. To give one installation more pictures today:

```sql
UPDATE image_usage SET images = 0
  WHERE installation_id = '<id>' AND day = date('now');
```

## Running it locally

```sh
export GATEWAY_OPENROUTER_KEY=sk-or-...
python3 -m gateway.server --config gateway/gateway.example.json --db :memory: --port 8790
curl -s http://127.0.0.1:8790/v1/health
```

The tests (`tests/test_gateway.py`) run a fake provider on loopback and need no key:
`PYTHONPATH=backend RELAY_KEYRING=off python3 -m unittest tests.test_gateway -v`.

## Relay Pro (optional)

Pro uses the same gateway, installation identity, quotas and spend ceilings as Free. The
installation `Authorization: Bearer <token>` exchange is unchanged. On **every** Pro completion,
clients also send `X-Relay-Pro-Code: <person-code>`. The server checks that code in SQLite on
that request; activating a client or having `plan: pro` in client settings grants no access.
The header is consumed by the gateway, never forwarded to the model provider or logged.

`GET /v1/pro` requires both headers. A valid code with configured Pro roles returns:

```json
{"active": true, "models": ["relay-pro-high", "relay-pro-main", "relay-pro-flash"]}
```

`models` contains only the supported Pro roles configured on this gateway. Missing, invalid or
revoked codes, no Pro configuration, or a request for an unavailable Pro role receive HTTP 403
with `error.code: pro_access_denied`. Missing/expired installation credentials still receive
401 `token_expired`; a database failure fails closed with 503 `free_unavailable`. Success here
confirms entitlement, not remaining quota or upstream availability; completions still enforce
all existing identity, rate, concurrency, quota and spend checks. Free roles ignore this header.
The whole `relay-pro-*` namespace is reserved: unsupported roles are rejected in configuration
and cannot be called even with a valid code. There is no Pro lite or Ultra role.

### Issue, list and revoke codes

Run locally on the gateway host as the service user, using the service's **existing** database
(the CLI refuses a missing file to avoid silently operating on the wrong new database):

```sh
cd /opt/relay
sudo -u relay-gateway python3 -m gateway.pro --db /var/lib/relay-gateway/gateway.db issue person-label
sudo -u relay-gateway python3 -m gateway.pro --db /var/lib/relay-gateway/gateway.db list
sudo -u relay-gateway python3 -m gateway.pro --db /var/lib/relay-gateway/gateway.db revoke person-label
```

Choose a unique stable person label. `issue` prints the cryptographically random code **once**;
share it privately with that person. Only a SHA-256 digest and operator metadata are persisted.
`list` prints labels and creation/revocation timestamps, never codes or digests. Each label may
have one active code; revoke before issuing a replacement. Revocation takes effect for subsequent
requests immediately, including requests using an already-issued installation bearer token,
without restarting the server. A stream already admitted can finish. This is a per-person
bearer secret, usable on that person's installations; it is not an account/login system or a
new allowance. Protect terminal output and backups; do not place the code in command arguments,
access logs, prompts or diagnostics.

### Configure the three Pro roles

Pro is disabled in the existing Free example. Add roles explicitly on the operator's box;
no production configuration changes are made by installing this code. High and main map to
`glm5.3`, flash to `glm5.3flash` in the example below. These are operator-supplied upstream model
identifiers: use the exact IDs offered by your provider (including any provider namespace).
The provider must support the configured reasoning dialect. Lite continues using `relay-lite`.

This example writes a **new** config from the Free example and prompts for the endpoint, model
IDs and actual current prices. It has no default prices; do not substitute guessed rates.
Review the result before installing it and provide `GATEWAY_PRO_KEY` through the service env
file. Caps and efforts below are explicit example operator settings, not new quota allowances.

```python
import json
from pathlib import Path

cfg = json.loads(Path("gateway/gateway.example.json").read_text())
main = input("GLM 5.3 upstream ID [glm5.3]: ").strip() or "glm5.3"
flash = input("GLM 5.3 Flash upstream ID [glm5.3flash]: ").strip() or "glm5.3flash"
prices = {}
for model in dict.fromkeys((main, flash)):
    prices[model] = [float(input(f"{model}: input USD/million tokens: ")),
                     float(input(f"{model}: output USD/million tokens: "))]
cfg["providers"]["pro"] = {
    "base_url": input("Provider HTTPS OpenAI-compatible base URL: ").strip(),
    "key_env": "GATEWAY_PRO_KEY",
    "effort_style": input("Effort dialect (reasoning/reasoning_effort/none): ").strip(),
    "price_per_mtok": prices,
}
for role, model, effort in (("relay-pro-high", main, "high"),
                            ("relay-pro-main", main, "medium"),
                            ("relay-pro-flash", flash, "low")):
    cfg["roles"][role] = {
        "upstreams": [{"provider": "pro", "model": model}],
        "effort": effort, "max_effort": "high",
        "max_output_tokens": 8192, "max_input_chars": 2000000,
    }
Path("gateway.pro.json").write_text(json.dumps(cfg, indent=2) + "\n")
```

Validate with `gateway.config.load` and the key environment set before restarting. Every served
upstream requires an explicit price, including fallback models. Existing `quota` and `limits`
remain shared across Free and Pro; there is no billing or per-code allowance in this version.

Pro regression tests use local synthetic prices and no production secrets:
`PYTHONPATH=backend RELAY_KEYRING=off python3 -m unittest tests.test_gateway_pro tests.test_gateway -v`.
