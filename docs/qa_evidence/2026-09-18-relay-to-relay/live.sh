#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Relay-to-Relay, live: Relay A shares a pane; Relay B, with a HOME and XDG dirs of its own, opens
# it through "Open a shared pane…", pairs (the two codes are screenshotted side by side), draws the
# terminal and the queue, takes the keyboard and types a command that runs on A, loses the keyboard
# to A's own keystroke, withdraws A's queued steer with its ×, and sends a command from its own
# prompt box. Then B is restarted and reconnects from its stored pairing without a link.
#
#   docs/qa_evidence/2026-09-18-relay-to-relay/live.sh [build dir]
#
# Two Xvfb displays, two jails (XDG_RUNTIME_DIR and TMPDIR included), the fake model of
# docs/qa_evidence/2026-09-18-thinking-copy-and-steer-withdraw on its own port. Like
# 2026-09-18-web-pane-view/live.sh it publishes through `tailscale serve` and refuses to start when
# something is already served, and resets only what it put up. Writes implementer-live-*.png beside itself.
set -uo pipefail
# Before anything else: without it `remote.identity` writes the owner's real desktop identity into
# the Secret Service keyring, and isolated XDG dirs do not protect the keyring.
export RELAY_KEYRING=off
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
fake=$root/docs/qa_evidence/2026-09-18-thinking-copy-and-steer-withdraw/fake-provider.py
port=${RELAY_QA_PORT:-18471}

if tailscale serve status --json 2>/dev/null | grep -q '"TCP"'; then
  echo "tailscale serve is already publishing something on this machine; this run would replace it."
  exit 1
fi
free=(); for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || free+=("$n"); [[ ${#free[@]} -ge 2 ]] && break; done
[[ ${#free[@]} -lt 2 ]] && { echo "no two free X displays"; exit 1; }
dA=:${free[0]}; dB=:${free[1]}
state=$(mktemp -d)
pids=()
cleanup() {
  kill "${pids[@]}" 2>/dev/null
  tailscale serve status --json 2>/dev/null | grep -q '"TCP"' && tailscale serve reset >/dev/null 2>&1
  rm -rf "$state"
}
trap cleanup EXIT

jail() {
  local j=$state/$1
  mkdir -p "$j/home" "$j/config/RelayTerminal" "$j/config/relay" "$j/data" "$j/cache" "$j/work" "$j/tmp"
  mkdir -m 700 -p "$j/run"
  printf '[provider]\npreset=local:fake\n\n[instructions]\nonboarded=true\n' >"$j/config/RelayTerminal/relay.conf"
  printf '{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake", "base_url": "http://127.0.0.1:%s/v1", "model": "fake", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$j/config/relay/local-models.json"
}
start_relay() {   # jail display log
  local j=$state/$1
  ( export HOME=$j/home XDG_CONFIG_HOME=$j/config XDG_DATA_HOME=$j/data XDG_CACHE_HOME=$j/cache \
      XDG_RUNTIME_DIR=$j/run TMPDIR=$j/tmp RELAY_KEYRING=off DISPLAY=$2 RELAY_REMOTE_PAIR_FILE=$j/pair-url.txt
    cd "$j/work" && exec "$build/relay" --workspace "$j/work" --clean-shell --fresh >"$out/$3" 2>&1 ) &
  pids+=($!)
  sleep 8
  local w
  w=$(DISPLAY=$2 xdotool search --onlyvisible --pid "$!" --name "^Relay" | tail -1)
  [[ -z $w ]] && { echo "Relay $1 did not open a window"; tail -5 "$out/$3"; exit 1; }
  DISPLAY=$2 xdotool windowmove "$w" 0 0 windowsize "$w" 1500 950 windowfocus "$w"
  echo "$w" >"$state/win$1"
}
shot() { DISPLAY=$1 import -window root "$out/implementer-$2"; }
A() { DISPLAY=$dA xdotool "$@"; }
B() { DISPLAY=$dB xdotool "$@"; }

jail A; jail B
: >"$out/live-requests.jsonl"
python3 "$fake" "$port" "$out/live-requests.jsonl" & pids+=($!)
Xvfb "$dA" -screen 0 1500x950x24 >/dev/null 2>&1 & pids+=($!)
Xvfb "$dB" -screen 0 1500x950x24 >/dev/null 2>&1 & pids+=($!)
sleep 1
start_relay A "$dA" live-relay-a.log
start_relay B "$dB" live-relay-b.log

# A shares its pane: the last chip of the prompt box's strip.
A mousemove 1452 914 click 1
for _ in $(seq 1 25); do [[ -s $state/A/pair-url.txt ]] && break; sleep 1; done
[[ -s $state/A/pair-url.txt ]] || { echo "A never produced a pairing link"; exit 1; }

# B: the palette, "Open a shared pane…", the link.
B key ctrl+shift+a; sleep 1.5
B type --delay 20 'open a shared pane'; sleep 1; B key Return; sleep 4
shot "$dB" live-01-laptop-dialog.png
B mousemove 740 388 click 1; sleep 0.3
B type --delay 2 "$(cat "$state/A/pair-url.txt")"; B key Return
sleep 12
shot "$dB" live-02-laptop-code.png; shot "$dA" live-02-desktop-code.png
# A: Allow typing (the third button of the ask row).
A mousemove 903 676 click 1; sleep 8
shot "$dB" live-03-laptop-panes.png
B mousemove 945 464 click 1; sleep 5
shot "$dB" live-04-laptop-pane-open.png

# B takes the keyboard and types a command; it must run on A.
B mousemove 1410 100 click 1; sleep 2
B type --delay 30 'echo R2R-$((6*7)) | tee r2r-proof.txt'; B key Return; sleep 3
shot "$dB" live-05-laptop-typed-command.png
dlg=$(A search --onlyvisible --name "Share this pane" | head -1)
[[ -n $dlg ]] && A windowmove "$dlg" 2000 2000
# A's owner types: the keyboard goes back to the desktop.
A windowfocus "$(cat "$state/winA")"; sleep 0.5; A mousemove 700 500 click 1; sleep 0.5
A type --delay 50 x; A key BackSpace; sleep 3
shot "$dB" live-06-laptop-desktop-took-it-back.png

# An agent turn on A with a message waiting for the next tool call; B withdraws it with its ×.
A type --delay 15 'please plan this out'; A key ctrl+Return; sleep 4
A type --delay 12 'LIVESTEER also check the readme'; A key Return; sleep 1; A key Return; sleep 3
shot "$dB" live-07-laptop-thinking-and-queue.png
B mousemove 1466 843 click 1; sleep 3
shot "$dB" live-08-laptop-after-withdraw.png
for _ in $(seq 1 80); do [ "$(wc -l <"$out/live-requests.jsonl")" -ge 2 ] && break; sleep 1; done
B mousemove 1100 889 click 1; sleep 0.5
B type --delay 20 'echo FROM-PROMPT-BOX | tee pb.txt'; B key Return; sleep 4
shot "$dB" live-09-laptop-prompt-box-command.png

# B again, from its stored pairing: no link.
kill "${pids[4]}" 2>/dev/null; sleep 2   # fake, Xvfb A, Xvfb B, Relay A, Relay B
start_relay B "$dB" live-relay-b2.log
B key ctrl+shift+a; sleep 1.5; B type --delay 20 'open a shared pane'; sleep 0.8; B key Return; sleep 8
shot "$dB" live-10-laptop-reconnects-with-stored-pairing.png

echo "--- results ---"
fail=0
[[ $(cat "$state/A/work/r2r-proof.txt" 2>/dev/null) == R2R-42 ]] && echo "typed on B, ran on A: R2R-42" || { echo "FAIL: no R2R-42 on A"; fail=1; }
[[ $(cat "$state/A/work/pb.txt" 2>/dev/null) == FROM-PROMPT-BOX ]] && echo "B's prompt box ran on A" || { echo "FAIL: no pb.txt on A"; fail=1; }
[[ ! -e $state/B/work/r2r-proof.txt ]] && echo "nothing ran on B" || { echo "FAIL: it ran on B"; fail=1; }
grep -q LIVESTEER "$out/live-requests.jsonl" && { echo "FAIL: the withdrawn steer reached the model"; fail=1; } \
  || echo "the steer B withdrew never reached the model ($(wc -l <"$out/live-requests.jsonl") requests)"
exit $fail
