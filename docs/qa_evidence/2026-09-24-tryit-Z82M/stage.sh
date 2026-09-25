#!/bin/sh
# Stage the #Z82M Try-it: a local gateway (the real gateway/server.py) whose upstream
# is the scripted fake provider, and a keyless fresh-install profile pointed at it.
# Only openrouter.ai is faked; everything between the pane and the picture is shipped code.
set -eu
REPO=/home/elliott/repos/relay-terminal
HERE="$(cd "$(dirname "$0")" && pwd)"
SANDBOX=/tmp/claude-1000/tryit/z82m
FAKE_PORT=4761
GATEWAY_PORT=4762

rm -rf "$SANDBOX"
mkdir -p "$SANDBOX/profile/home" "$SANDBOX/profile/.local/share" "$SANDBOX/project" "$SANDBOX/logs"

# A real PNG so the inline writer has a real picture to show.
python3 - "$SANDBOX/lighthouse.png" <<'EOF'
import sys
from PIL import Image, ImageDraw
img = Image.new("RGB", (512, 512))
d = ImageDraw.Draw(img)
for y in range(512):                       # dusk sky over dark water
    d.line([(0, y), (511, y)], fill=(28 + y // 12, 24 + y // 20, 40 + y // 14))
d.rectangle([0, 360, 511, 511], fill=(12, 16, 28))
d.polygon([(236, 300), (276, 300), (300, 360), (212, 360)], fill=(210, 205, 190))   # tower
d.rectangle([212, 360, 300, 420], fill=(150, 145, 132))
d.rectangle([232, 292, 280, 302], fill=(255, 236, 160))                             # lamp
d.polygon([(280, 300), (512, 230), (512, 340)], fill=(255, 240, 190))                # beam
for i in range(4):                                                                  # stripes
    d.rectangle([212 + 4, 364 + i * 14, 300 - 4, 364 + i * 14 + 7], fill=(168, 60, 48))
d.ellipse([404, 66, 428, 90], fill=(255, 250, 220))                                  # moon
img.save(sys.argv[1])
EOF

cat > "$SANDBOX/gateway.json" <<EOF
{
  "providers": {
    "fake": {
      "base_url": "http://127.0.0.1:$FAKE_PORT",
      "key_env": "GATEWAY_FAKE_KEY",
      "price_per_mtok": {"fake-model": [0.10, 0.40]},
      "price_per_image": {"fake-image-model": 0.003}
    }
  },
  "roles": {
    "relay-main":  {"upstreams": [{"provider": "fake", "model": "fake-model"}],
                    "effort": "low", "max_effort": "medium",
                    "max_output_tokens": 2048, "max_input_chars": 100000},
    "relay-flash": {"upstreams": [{"provider": "fake", "model": "fake-model"}],
                    "effort": "low", "max_effort": "medium",
                    "max_output_tokens": 2048, "max_input_chars": 100000},
    "relay-lite":  {"upstreams": [{"provider": "fake", "model": "fake-model"}],
                    "effort": "minimal", "max_effort": "medium",
                    "max_output_tokens": 512, "max_input_chars": 50000},
    "relay-image": {"kind": "images",
                    "upstreams": [{"provider": "fake", "model": "fake-image-model"}],
                    "resolutions": ["512x512", "1024x1024"],
                    "aspect_ratios": ["1:1"],
                    "max_input_chars": 4000}
  },
  "quota": {"tokens_per_day": 250000, "requests_per_minute": 30,
            "concurrency_per_install": 2, "images_per_day": 5},
  "limits": {"global_concurrency": 8, "spend_per_day_usd": 20, "spend_per_month_usd": 100,
             "per_provider_per_day_usd": {}, "registrations_per_ip_per_hour": 100,
             "challenges_per_ip_per_hour": 1000},
  "token_ttl_seconds": 3600,
  "allow_insecure_loopback": true,
  "upstream_connect_timeout": 2,
  "upstream_stall_timeout": 10
}
EOF

# The scripted provider, then the real gateway in front of it.
GATEWAY_FAKE_KEY=stage-key python3 "$HERE/fake_upstream.py" "$FAKE_PORT" \
  "$SANDBOX/lighthouse.png" > "$SANDBOX/logs/fake.log" 2>&1 &
echo $! > "$SANDBOX/fake.pid"
( cd "$REPO" && GATEWAY_FAKE_KEY=stage-key python3 -m gateway.server \
    --config "$SANDBOX/gateway.json" --db "$SANDBOX/gateway.db" --port "$GATEWAY_PORT" \
    > "$SANDBOX/logs/gateway.log" 2>&1 & echo $! > "$SANDBOX/gateway.pid" )

for i in $(seq 1 50); do
  if curl -fsS "http://127.0.0.1:$GATEWAY_PORT/v1/health" 2>/dev/null \
      | grep -q relay-image; then break; fi
  sleep 0.2
done
curl -fsS "http://127.0.0.1:$GATEWAY_PORT/v1/health"; echo

echo "staged. Open Relay with:"
echo "  RELAY_HOSTED_URL=http://127.0.0.1:$GATEWAY_PORT/v1 RELAY_KEYRING=off XDG_CONFIG_HOME=$SANDBOX/profile/.local/share XDG_DATA_HOME=$SANDBOX/profile/.local/share HOME=$SANDBOX/profile/home /tmp/z82m-verify/build/relay --workspace $SANDBOX/project --clean-shell --fresh"
