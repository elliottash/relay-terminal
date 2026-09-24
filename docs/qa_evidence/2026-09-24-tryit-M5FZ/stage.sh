#!/usr/bin/env bash
# Stage the M5FZ Try it: a private X display (:96), an isolated profile, a seeded git repo, a
# loopback-only scripted model, and one finished background subagent ("notes QA sweep") sitting in
# the subagents strip of a running Relay. No arguments, safe to run twice (it resets everything
# under /tmp/claude-1000/tryit/m5fz), needs no key and no network beyond loopback.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT=/tmp/claude-1000/tryit/m5fz
DISPLAY_NUM=96
PORT=8731

# Reset any previous staging: the staged app, its Xvfb and the mock model are named in
# $ROOT/pids; anything else squatting on the fixture's paths (an app or Xvfb left by an older
# stage.sh run) goes too. The user's own Relay never touches these paths.
for pid in $(pgrep -f "Xvfb :96" 2>/dev/null || true) \
           $(pgrep -f "tryit/m5fz/seed" 2>/dev/null || true) \
           $(pgrep -f "m5fz/mock_model" 2>/dev/null || true); do kill "$pid" 2>/dev/null || true; done
if [ -f "$ROOT/pids" ]; then
  while read -r pid; do kill "$pid" 2>/dev/null || true; done < "$ROOT/pids"
fi
sleep 1
rm -rf "$ROOT"
mkdir -p "$ROOT"/{home/.config/RelayTerminal,runtime,seed}

# --- the seeded workspace: a real repo with a real file for read_file to land on -----------------
cd "$ROOT/seed"
git init -q .
git config user.email qa@example.invalid
git config user.name "M5FZ QA"
printf 'Relay staging notes\n\nThe first paint of a subagent tab must show folded tool rows.\n' > notes.md
printf '# Staged project\n\nA fixture for the M5FZ Try it.\n' > README.md
git add . && git commit -qm "seed"

# --- the scripted gateway (loopback only; see mock_model.py) --------------------------------------
python3 "$HERE/mock_model.py" "$PORT" "$ROOT/mock.log" > "$ROOT/mock.stdout" 2>&1 &
MOCK_PID=$!
for _ in $(seq 1 40); do grep -q "mock model on" "$ROOT/mock.stdout" 2>/dev/null && break; sleep 0.25; done

# --- a private X display the size the app opens at, an isolated profile --------------------------------
Xvfb ":$DISPLAY_NUM" -screen 0 1600x1000x24 > "$ROOT/xvfb.stdout" 2>&1 &
XVFB_PID=$!
for _ in $(seq 1 40); do xdpyinfo -display ":$DISPLAY_NUM" >/dev/null 2>&1 && break; sleep 0.25; done
export XDG_RUNTIME_DIR="$ROOT/runtime"
export XDG_CONFIG_HOME="$ROOT/home/.config"
export HOME="$ROOT/home"
export DISPLAY=":$DISPLAY_NUM"
export RELAY_KEYRING=off
export RELAY_NO_URL_HANDLER=1
export QT_QPA_PLATFORM=xcb
export RELAY_SESSION=m5fz-stage
export RELAY_LOG_LEVEL=debug
# A minimal PATH: the claude/codex CLI guest rows must not exist in the sandbox (they would rank
# above the scripted free-tier model in the start choice).
export PATH=/usr/bin:/bin
# The hosted gateway is the scripted one on loopback: a config pointed at the canonical gateway
# follows this override (hosted.endpoint_for), so every chat, key test and quota probe lands on
# the mock. 127.0.0.1 on purpose — plain HTTP is allowed only to a loopback model server.
export RELAY_HOSTED_URL=http://127.0.0.1:$PORT/v1
mkdir -p "$XDG_RUNTIME_DIR/relay"
chmod 700 "$XDG_RUNTIME_DIR" "$ROOT/home" "$XDG_CONFIG_HOME"

cat > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<EOF
[provider]
preset=relay-free
EOF

REPO="$(git -C "$HERE" rev-parse --show-toplevel)"
(cd "$REPO" && exec ./build/relay --fresh --workspace "$ROOT/seed" > "$ROOT/relay.stdout" 2>&1) &
RELAY_PID=$!
printf '%s\n%s\n%s\n%s\n' "$MOCK_PID" "$XVFB_PID" "$RELAY_PID" $$ > "$ROOT/pids"

# --- wait for the drive socket (a file naming the socket's address), then submit the one prompt -
for _ in $(seq 1 480); do [ -s "$XDG_RUNTIME_DIR/relay/open-socket" ] && break; sleep 0.5; done
[ -s "$XDG_RUNTIME_DIR/relay/open-socket" ] || { echo "stage: open socket never appeared"; tail "$ROOT/relay.stdout"; exit 1; }
sleep 2

WID=""
for _ in $(seq 1 40); do
  WID="$(xdotool search --onlyvisible --class relay 2>/dev/null | head -1 || true)"
  [ -n "$WID" ] && break
  sleep 0.5
done
[ -n "$WID" ] || { echo "stage: relay window never appeared"; tail "$ROOT/relay.stdout"; exit 1; }
# --- first-run: dismiss the Board-init dialog, then focus the agent input line --------------------
click_text() {  # click_text <screenshot> <phrase>: OCR-locate a control by its text and click it
  local point
  point="$(python3 "$HERE/find_row.py" "$1" "$2" 2>/dev/null)" || return 1
  xdotool mousemove ${point% *} ${point#* } click 1
  return 0
}

sleep 3
import -window root "$ROOT/firstrun.png"
click_text "$ROOT/firstrun.png" "Not now" || true
sleep 1.5
import -window root "$ROOT/firstrun2.png"
click_text "$ROOT/firstrun2.png" "agent prompts" || true   # the input line's placeholder
sleep 0.5

xdotool type --delay 25 "Run the notes QA sweep as a background subagent."
xdotool key --clearmodifiers Return

# --- wait until the scripted sequence has fully played (subagent landed + both finals) -------------
for _ in $(seq 1 120); do
  grep -q "sub_tools" "$ROOT/mock.log" 2>/dev/null \
    && grep -q "sub_final" "$ROOT/mock.log" 2>/dev/null \
    && grep -q "main_final" "$ROOT/mock.log" 2>/dev/null && break
  sleep 0.5
done
grep -q "sub_final" "$ROOT/mock.log" || { echo "stage: the scripted model never finished"; cat "$ROOT/mock.log" 2>/dev/null; tail "$ROOT/relay.stdout"; exit 1; }
sleep 3   # let the strip settle on the finished row

import -window root "$HERE/staged-strip.png"
echo "staged: Relay is running on :$DISPLAY_NUM (window $WID) over $ROOT/seed"
echo "staged: the finished subagent 'notes QA sweep' is in the strip; the drive socket is $XDG_RUNTIME_DIR/relay/open-socket"
