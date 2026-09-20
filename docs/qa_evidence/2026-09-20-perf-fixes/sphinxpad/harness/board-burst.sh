#!/usr/bin/env bash
# #PF4K sphinxpad re-measure (#7M6E), confirmation run: what typing in the Switchboard's filter
# really costs, at the profile's window size (1600x1000) and as a BURST rather than one key at a
# time — the after build answers the box from a debounced worker search, so a per-key figure
# charges every key its own 120 ms debounce, which no one types slowly enough to pay.
#   board-burst.sh <tag> <relay binary> <data root> <board dir>
set -u
TAG=$1; BIN=$2; ROOT=$3; WSSRC=$4
H=/tmp/rx/bk/$TAG
export DISPLAY=${RDISP:-:271}
rm -rf "$H"; mkdir -p "$H"/{cfg,data,state,cache,tmp,xdg}
chmod 700 "$H/xdg"; ln -sfn "/run/user/$(id -u)/bus" "$H/xdg/bus"
export XDG_CONFIG_HOME=$H/cfg XDG_DATA_HOME=$H/data XDG_STATE_HOME=$H/state \
       XDG_CACHE_HOME=$H/cache XDG_RUNTIME_DIR=$H/xdg TMPDIR=$H/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb RELAY_DATA_DIR=$ROOT RELAY_NO_URL_HANDLER=1
export https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 no_proxy=127.0.0.1,localhost
mkdir -p "$H/cfg/RelayTerminal"
printf '[instructions]\nonboarded=true\n[theme]\nname=relay-dark\n[security]\napprovals_chosen=true\napprovals_ask=\n[input]\ndefault=shell\n' > "$H/cfg/RelayTerminal/relay.conf"
WS=/tmp/rx/bk/ws-$TAG; rm -rf "$WS"; mkdir -p "$WS"; cp -r "$WSSRC" "$WS/issues"
O=/tmp/rx/out; mkdir -p "$O"
cd "$WS" || exit 1
setsid nohup "$BIN" --workspace "$WS" --fresh --clean-shell >"$H/relay.out" 2>&1 </dev/null &
PID=""; WIN=""
for _ in $(seq 60); do
  sleep 0.5
  PID=$(pgrep -f "relay --workspace $WS " | head -1); [ -n "$PID" ] || continue
  WIN=$(xdotool search --name "^Relay — " 2>/dev/null | head -1); [ -n "$WIN" ] && break
done
[ -z "${WIN:-}" ] && { echo "$TAG LAUNCH_FAILED"; exit 1; }
xdotool windowsize "$WIN" 1600 1000; sleep 2
sleep 8; xdotool windowfocus --sync "$WIN"
echo "$TAG pid=$PID load=$(cut -d' ' -f1 /proc/loadavg) geom=$(xdotool getwindowgeometry "$WIN" | tail -1)"
python3 /tmp/rx/h/act.py --pid "$PID" --win "$WIN" --label "$TAG board.open" --timeout 120 \
    -- xdotool key ctrl+shift+s
sleep 3
# every section open, so the list the filter rebuilds is the whole board (the profile expanded one
# section by hand at 1600x1000; ctrl+shift+plus is the pane's "expand all" and needs no coordinates)
xdotool key --clearmodifiers ctrl+shift+plus; sleep 3
import -silent -window "$WIN" "$O/$TAG-burst-open.png" 2>/dev/null
xdotool mousemove 960 102 click 1; sleep 1.5
for r in 1 2 3 4 5; do
  python3 /tmp/rx/h/measure.py --pid "$PID" --settle 0.35 --timeout 40 \
      --label "$TAG burst 'switch'" -- xdotool type --delay 40 switch
  sleep 0.5
  python3 /tmp/rx/h/measure.py --pid "$PID" --settle 0.35 --timeout 40 \
      --label "$TAG clear" -- xdotool key ctrl+a BackSpace
  sleep 0.5
done
import -silent -window "$WIN" "$O/$TAG-burst-end.png" 2>/dev/null
kill -TERM "$PID" 2>/dev/null; sleep 3; kill -9 "$PID" 2>/dev/null
