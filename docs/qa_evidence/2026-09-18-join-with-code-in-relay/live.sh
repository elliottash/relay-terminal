#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# /join, live: a real rendezvous and hub (host.py) share two panes as one tab behind a meeting
# code; Relay, in a jail of its own under Xvfb, types `/join CODE` in its prompt box, then the PIN
# in the dialog, and is admitted as a guest. Then the host adds a third pane to the tab, which must
# open in the joined tab without anybody asking. Writes implementer-live-*.png beside itself.
#
#   docs/qa_evidence/2026-09-18-join-with-code-in-relay/live.sh [build dir]
set -uo pipefail
export RELAY_KEYRING=off
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
port=${RELAY_QA_PORT:-18493}
d=; for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { d=:$n; break; }; done
[[ -z $d ]] && { echo "no free X display"; exit 1; }
state=$(mktemp -d)
pids=()
cleanup() { kill "${pids[@]}" 2>/dev/null; rm -rf "$state"; }
trap cleanup EXIT

j=$state/B
mkdir -p "$j/home" "$j/config/RelayTerminal" "$j/data" "$j/cache" "$j/work" "$j/tmp"
mkdir -m 700 -p "$j/run"
printf '[instructions]\nonboarded=true\n\n[remote]\njoinServer=http://127.0.0.1:%s\njoinName=Elliott\n' "$port" \
  >"$j/config/RelayTerminal/relay.conf"

: >"$state/events.log"
python3 "$out/host.py" "$port" "$state" >"$out/live-host.log" 2>&1 & pids+=($!)
for _ in $(seq 1 30); do [[ -s $state/code.txt ]] && break; sleep 0.5; done
[[ -s $state/code.txt ]] || { echo "the host never made a code"; cat "$out/live-host.log"; exit 1; }
read -r code pin <"$state/code.txt"

Xvfb "$d" -screen 0 1500x950x24 >/dev/null 2>&1 & pids+=($!)
sleep 1
( export HOME=$j/home XDG_CONFIG_HOME=$j/config XDG_DATA_HOME=$j/data XDG_CACHE_HOME=$j/cache \
    XDG_RUNTIME_DIR=$j/run TMPDIR=$j/tmp DISPLAY=$d
  cd "$j/work" && exec "$build/relay" --workspace "$j/work" --clean-shell --fresh >"$out/live-relay.log" 2>&1 ) &
pids+=($!)
sleep 8
X() { DISPLAY=$d xdotool "$@"; }
w=$(X search --onlyvisible --name "^Relay" | tail -1)
[[ -z $w ]] && { echo "Relay did not open a window"; tail -5 "$out/live-relay.log"; exit 1; }
X windowmove "$w" 0 0 windowsize "$w" 1500 950 windowfocus "$w"; sleep 1
shot() { DISPLAY=$d import -window root "$out/implementer-$1"; }

shot live-01-plug-at-top-right.png
X type --delay 30 "/join $code"; sleep 0.5; X key Return; sleep 2
shot live-02-join-dialog-code-prefilled.png
X type --delay 60 "$pin"; X key Return; sleep 1
shot live-02b-after-enter.png; sleep 3
shot live-02c-knocking.png; sleep 6
shot live-03-joined-tab.png
for _ in $(seq 1 30); do grep -q "pane-3 added" "$state/events.log" && break; sleep 1; done
sleep 4
shot live-04-pane-added-to-the-tab-opens-here.png

echo "--- host events ---"; cat "$state/events.log"
echo "--- results ---"
fail=0
grep -q "admitted: \[('Elliott'" "$state/events.log" && echo "joined as Elliott" || { echo "FAIL: never admitted"; fail=1; }
grep -q "pane-3 added to the tab: \[('Elliott', \['pane-1', 'pane-2', 'pane-3'\])" "$state/events.log" \
  && echo "the guest's scope grew with the tab" || { echo "FAIL: scope did not grow"; fail=1; }
grep -iq "error\|traceback" "$out/live-host.log" && { echo "host log has errors:"; tail -5 "$out/live-host.log"; }
exit $fail
