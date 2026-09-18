#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The whole path, live and offline: a real Relay pane with a real (fake-provider) agent turn, its
# queue holding a message for the next tool call, shared to a real headless browser, which draws
# the pane from `pane_state` and withdraws that message with its ×.
#
#   docs/qa_evidence/2026-09-18-web-pane-view/live.sh [build dir]
#
# Isolated HOME and XDG dirs (including XDG_RUNTIME_DIR and TMPDIR) so it cannot touch the owner's
# profile, his remote identity or his paired devices. Writes implementer-live-*.png beside this
# script, plus live-relay.log, live-pair.log and live-requests.jsonl (what the model was sent).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
fake=$root/docs/qa_evidence/2026-09-18-thinking-copy-and-steer-withdraw/fake-provider.py
port=${RELAY_QA_PORT:-18452}

display=
for n in $(seq 140 180); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free X display"; exit 1; }

jail=$(mktemp -d)
export HOME="$jail/home" XDG_CONFIG_HOME="$jail/config" XDG_DATA_HOME="$jail/data"
export XDG_CACHE_HOME="$jail/cache" XDG_RUNTIME_DIR="$jail/run" TMPDIR="$jail/tmp"
export RELAY_KEYRING=off
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" \
         "$XDG_CACHE_HOME" "$jail/work" "$TMPDIR"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[provider]
preset=local:fake

[instructions]
onboarded=true
CONF
printf '{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake", "base_url": "http://127.0.0.1:%s/v1", "model": "fake", "server": "openai-compatible", "context_window": 131072}]}\n' \
  "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$out/live-requests.jsonl"
python3 "$fake" "$port" "$out/live-requests.jsonl" &
fake_pid=$!
Xvfb "$display" -screen 0 1500x950x24 >/dev/null 2>&1 &
xvfb_pid=$!
trap 'kill "${relay_pid:-0}" "$xvfb_pid" "$fake_pid" 2>/dev/null; rm -rf "$jail"' EXIT
sleep 1
export DISPLAY=$display

export RELAY_REMOTE_PAIR_FILE="$jail/pair-url.txt"
"$build/relay" --workspace "$jail/work" --clean-shell --fresh >"$out/live-relay.log" 2>&1 &
relay_pid=$!
sleep 8
win=$(for w in $(xdotool search --onlyvisible --pid "$relay_pid" --name "^Relay"); do
        g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)
        wdt=$(echo "$g" | sed -n 's/^WIDTH=//p'); hgt=$(echo "$g" | sed -n 's/^HEIGHT=//p')
        echo "$(( ${wdt:-0} * ${hgt:-0} )) $w"
      done | sort -rn | head -1 | cut -d' ' -f2)
[[ -z $win ]] && { echo "no Relay window"; tail -5 "$out/live-relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 950 windowfocus "$win"
sleep 1

import -window "$win" "$out/implementer-live-00-desktop-before-sharing.png"

# Share the pane: the last chip in the composer's strip (the palette action does the same).
xdotool mousemove --window "$win" 1452 914 click 1
sleep 10
url=$(cat "$RELAY_REMOTE_PAIR_FILE" 2>/dev/null)
[[ -z $url ]] && { echo "no pairing url; the sidecar did not start"; tail -20 "$out/live-relay.log"; exit 1; }
echo "pairing url: ${url:0:48}..."

( cd "$root" && python3 "$out/live_browser.py" "$url" "$out" >"$out/live-pair.log" 2>&1 ) &
pair_pid=$!
for i in $(seq 1 60); do grep -q "phone shows code" "$out/live-pair.log" 2>/dev/null && break; sleep 1; done
sleep 3
# Allow the device, with typing: Refuse holds the focus, so it is Tab Tab space, not Return.
dlg=$(xdotool search --onlyvisible --name "Share this pane" | head -1)
if [[ -n $dlg ]]; then xdotool windowfocus "$dlg"; sleep 1; xdotool key --delay 40 Tab Tab space; fi
sleep 2

# Only now the turn, so the message the phone withdraws cannot have been delivered at a tool call
# while pairing was still going on: the fake model reasons for 20 s before its first tool call.
xdotool windowfocus "$win"; sleep 1
xdotool type --delay 15 'please plan this out'; xdotool key ctrl+Return
sleep 4
# A message waiting for the agent's next tool call: Enter queues it, Enter again steers it.
xdotool type --delay 12 'LIVESTEER also check the readme'
xdotool key Return; sleep 1; xdotool key Return
sleep 2
import -window "$win" "$out/implementer-live-01-desktop-with-the-waiting-message.png"

wait $pair_pid
[[ -n $dlg ]] && xdotool windowmove "$dlg" 2000 2000
sleep 2
import -window "$win" "$out/implementer-live-03-desktop-after-the-phone-withdrew-it.png"

echo "--- what the phone saw ---"
cat "$out/live-pair.log"
# The proof needs the turn to reach the step where a steer would have been delivered: the fake
# model calls its tool at about 20 s and the command runs for 30 s, so the next request to the
# model comes at about 50 s. Without waiting for it, "nothing carried it" would only mean "the
# turn had not asked the model anything since".
echo "waiting for the turn's next model call…"
for i in $(seq 1 80); do [ "$(wc -l <"$out/live-requests.jsonl")" -ge 2 ] && break; sleep 1; done
import -window "$win" "$out/implementer-live-04-desktop-after-the-tool-call.png"

echo "--- did the withdrawn message reach the model? ---"
if grep -q LIVESTEER "$out/live-requests.jsonl"; then echo "FAIL: LIVESTEER reached the model"; exit 1; fi
requests=$(wc -l <"$out/live-requests.jsonl")
[ "$requests" -ge 2 ] || { echo "FAIL: the turn never reached its next model call ($requests request)"; exit 1; }
echo "no: $requests requests including the one after the tool call, none carried LIVESTEER"
