#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Start the exported `relay` on the staged fixture, under its own Xvfb display and an isolated
# profile, and leave it running. Writes DISPLAY / window id / pids to <out>/session.env.
#   RELAY_BIN=<relay> bash launch.sh <staged project> <out dir> [nokey|key]
set -uo pipefail
bin=${RELAY_BIN:?RELAY_BIN}
work=${1:?staged project}; out=${2:?out}; mode=${3:-nokey}
mkdir -p "$out"; width=1600 height=1000
display=; for n in $(seq 230 279); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }
sandbox=$(mktemp -d /tmp/rl-jv.XXXX)
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp \
       XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share \
       XDG_CACHE_HOME=$sandbox/home/.cache
# No model key at all: the Switchboard agent has no provider, which is the point of phase A.
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' \
       > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
# mode `stub`: a loopback OpenAI-compatible endpoint that accepts the request and then fails
# (STUB_PORT). It stands in for "no model key" while letting the turn actually start, so the
# button's Trying… state and the run's failure path can both be seen.
if [[ $mode == stub ]]; then
  mkdir -p "$XDG_CONFIG_HOME/relay"
  cat > "$XDG_CONFIG_HOME/relay/custom-providers.json" <<JSON
{"providers": [{"id": "custom:stub", "name": "stub", "base_url": "http://127.0.0.1:${STUB_PORT:-8791}/v1",
  "models": ["stub-1"], "effort_style": "none", "served_models": [], "note": ""}]}
JSON
  # The helper agent refuses a guest Main and walks the Options > Models priority list
  # instead, so the stub has to be *in* that list (tier\\main) as well as on the role.
  printf '[roles]\nswitchboard\\preset=custom:stub\nswitchboard\\model=stub-1\n[models]\ntier\\main=custom:stub|stub-1\nprovider_order=custom:stub\n' \
         >> "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
fi
(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) > "$out/relay.log" 2>&1 &
relay_pid=$!
sleep 14
win=; best=0
for c in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
  eval "$(xdotool getwindowgeometry --shell "$c" 2>/dev/null)"
  (( WIDTH*HEIGHT > best )) && { best=$((WIDTH*HEIGHT)); win=$c; }
done
[[ -z $win ]] && { echo "no window"; tail -20 "$out/relay.log"; kill -TERM $relay_pid $xvfb_pid; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height windowfocus "$win"
sleep 4
cat > "$out/session.env" <<ENV
DISPLAY=$display
WIN=$win
RELAY_PID=$relay_pid
XVFB_PID=$xvfb_pid
SANDBOX=$sandbox
ENV
echo "display=$display win=$win relay=$relay_pid xvfb=$xvfb_pid sandbox=$sandbox"
