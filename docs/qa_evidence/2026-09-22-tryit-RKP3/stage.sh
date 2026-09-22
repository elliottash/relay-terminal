#!/usr/bin/env bash
# Try-it staging for #RKP3: an isolated Relay with seeded model priorities.
# Safe to run twice: the fixture is recreated each time. Your real config is untouched.
set -euo pipefail
ROOT=/home/elliott/repos/relay-terminal
QA=/tmp/claude-1000/tryit/rkp3
rm -rf "$QA"
mkdir -p "$QA"/{home/Downloads,config/RelayTerminal,data,cache,runtime,tmp}
chmod 700 "$QA/runtime"
cat > "$QA/config/RelayTerminal/relay.conf" <<'EOF'
[instructions]
onboarded=true

[models]
tier\high=anthropic|claude-h1|, anthropic|claude-h2|
tier\main=anthropic|claude-m1|, anthropic|claude-m2|, anthropic|claude-m3|
tier\flash=anthropic|claude-f1|
EOF
env HOME="$QA/home" XDG_CONFIG_HOME="$QA/config" XDG_DATA_HOME="$QA/data" \
    XDG_CACHE_HOME="$QA/cache" XDG_RUNTIME_DIR="$QA/runtime" TMPDIR="$QA/tmp" \
    RELAY_KEYRING=off \
    "$ROOT/build/relay" --workspace "$QA/home/Downloads" --clean-shell --fresh &
echo "Relay is open with seeded priorities (isolated profile). Press Ctrl+Shift+M for the models pane — it opens on Priorities."
