#!/usr/bin/env bash
# #PF4K sphinxpad re-measure, Switchboard GUI (#7M6E): open cost and per-keystroke filter cost.
#   board-gui.sh <tag> <relay binary> <data root> <board workspace> [filter_x filter_y]
# The pane's input mode is forced to shell and Return is never pressed, so no key typed here can
# start an agent turn even if a click misses the filter box.
set -u
TAG=$1; BIN=$2; ROOT=$3; WSSRC=$4; FX=${5:-960}; FY=${6:-102}
H=/tmp/rx/bg/$TAG
export DISPLAY=${RDISP:-:271}
rm -rf "$H"; mkdir -p "$H"/{cfg,data,state,cache,tmp,xdg}
chmod 700 "$H/xdg"; ln -sfn "/run/user/$(id -u)/bus" "$H/xdg/bus"
export XDG_CONFIG_HOME=$H/cfg XDG_DATA_HOME=$H/data XDG_STATE_HOME=$H/state \
       XDG_CACHE_HOME=$H/cache XDG_RUNTIME_DIR=$H/xdg TMPDIR=$H/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb RELAY_DATA_DIR=$ROOT RELAY_NO_URL_HANDLER=1
export https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 no_proxy=127.0.0.1,localhost
mkdir -p "$H/cfg/RelayTerminal"
cat > "$H/cfg/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[theme]
name=relay-dark
[security]
approvals_chosen=true
approvals_ask=
[input]
default=shell
CONF
WS=/tmp/rx/bg/ws-$TAG; rm -rf "$WS"; mkdir -p "$WS"; cp -r "$WSSRC" "$WS/issues"
O=/tmp/rx/out; mkdir -p "$O"

cd "$WS" || exit 1
setsid nohup "$BIN" --workspace "$WS" --fresh --clean-shell >"$H/relay.out" 2>&1 </dev/null &
PID=""; WIN=""
for _ in $(seq 60); do
  sleep 0.5
  PID=$(pgrep -f "relay --workspace $WS " | head -1)
  [ -n "$PID" ] || continue
  WIN=$(xdotool search --name "^Relay — " 2>/dev/null | head -1)
  [ -n "$WIN" ] && break
done
[ -z "${WIN:-}" ] && { echo "$TAG LAUNCH_FAILED"; exit 1; }
sleep 8
xdotool windowfocus --sync "$WIN"
echo "$TAG pid=$PID win=$WIN load=$(cut -d' ' -f1 /proc/loadavg) geom=$(xdotool getwindowgeometry "$WIN" | tr '\n' ' ')"

python3 /tmp/rx/h/act.py --pid "$PID" --win "$WIN" --label "$TAG board.open" --timeout 120 \
    -- xdotool key ctrl+shift+s
sleep 3
import -silent -window "$WIN" "$O/$TAG-board.png" 2>/dev/null
xdotool mousemove "$FX" "$FY" click 1; sleep 1.5
for round in 1 2 3; do
  for c in s w i t c h; do
    python3 /tmp/rx/h/measure.py --pid "$PID" --settle 0.25 --timeout 30 \
        --label "$TAG filter r$round '$c'" -- xdotool key "$c"
  done
  xdotool key --clearmodifiers ctrl+a; sleep 0.3; xdotool key BackSpace; sleep 1.5
done
sleep 1
import -silent -window "$WIN" "$O/$TAG-filtered.png" 2>/dev/null
echo "$TAG worker_rss_kb=$(for k in $(pgrep -P $PID -f worker.py); do awk '/VmRSS/{print $2}' /proc/$k/status; done | paste -sd,)"
echo "$TAG gui_rss_kb=$(awk '/VmRSS/{print $2}' /proc/$PID/status)"
kill -TERM "$PID" 2>/dev/null; sleep 3
kill -9 "$PID" 2>/dev/null
