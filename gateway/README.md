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
| `providers.<name>.price_per_mtok` | `model -> [input USD per million, output USD per million]`. **The numbers in the example are placeholders**: fill them from the providers' pricing pages before going live; they are what the ceilings count. |
| `quota` | Per installation: `tokens_per_day` (input plus output, UTC day), `requests_per_minute`, `concurrency_per_install`. |
| `limits` | `global_concurrency`, `spend_per_day_usd` and `spend_per_month_usd` (all providers together), `per_provider_per_day_usd`, `registrations_per_ip_per_hour`, `challenges_per_ip_per_hour`. |
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
| 429 | `quota_exhausted` | The installation's `tokens_per_day` is used; `resets_at` is the next UTC midnight. |
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

## Running it locally

```sh
export GATEWAY_OPENROUTER_KEY=sk-or-...
python3 -m gateway.server --config gateway/gateway.example.json --db :memory: --port 8790
curl -s http://127.0.0.1:8790/v1/health
```

The tests (`tests/test_gateway.py`) run a fake provider on loopback and need no key:
`PYTHONPATH=backend RELAY_KEYRING=off python3 -m unittest tests.test_gateway -v`.
