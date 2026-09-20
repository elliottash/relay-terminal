#!/usr/bin/env bash
# #PF4K sphinxpad re-measure (#6W0Z): what a scrollback line costs, from a FRESH pane.
#   sb.sh <tag> <relay binary> <data root>
# Pss right after the shell's first prompt, then after 2 000 000 lines have gone past (the ring
# holds 10 000), then after a second run to show the ring does not keep growing.
set -u
TAG=$1; BIN=$2; ROOT=$3
P=/tmp/rx/sb
export DISPLAY=${RDISP:-:271}
rm -rf $P/h; mkdir -p $P/h/{run,cfg,data,state,cache,tmp} $P/work $P/out
chmod 700 $P/h/run; ln -sfn "/run/user/$(id -u)/bus" "$P/h/run/bus"
export XDG_RUNTIME_DIR=$P/h/run XDG_CONFIG_HOME=$P/h/cfg XDG_DATA_HOME=$P/h/data \
       XDG_STATE_HOME=$P/h/state XDG_CACHE_HOME=$P/h/cache TMPDIR=$P/h/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb RELAY_DATA_DIR=$ROOT RELAY_NO_URL_HANDLER=1
export https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 no_proxy=127.0.0.1,localhost
mkdir -p "$P/h/cfg/RelayTerminal"
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n[input]\ndefault=shell\n' \
  > "$P/h/cfg/RelayTerminal/relay.conf"
(setsid "$BIN" --clean-shell --fresh -w $P/work > $P/relay-$TAG.log 2>&1 &)
sleep 14
PID=""; WIN=""
for w in $(xdotool search --name "Relay"); do
  n=$(xdotool getwindowname "$w" 2>/dev/null); p=$(xdotool getwindowpid "$w" 2>/dev/null)
  case "$n" in Relay*) [ -n "$p" ] && WIN=$w && PID=$p;; esac
done
[ -z "$PID" ] && { echo "$TAG NO_WINDOW"; exit 1; }
xdotool windowfocus --sync "$WIN" 2>/dev/null
pss() { awk '/^Pss:/{s+=$2} END{print s}' /proc/$PID/smaps_rollup; }
say() { local stamp=$P/out/$1.stamp; rm -f "$stamp"
  xdotool type --delay 6 -- "$2; echo done > $stamp"
  xdotool key --clearmodifiers ctrl+shift+Return
  local d=$((SECONDS+240)); while [ ! -s "$stamp" ] && [ $SECONDS -lt $d ]; do sleep 0.1; done
  [ -s "$stamp" ] || { echo "$TAG $1 TIMEOUT"; return 1; }; sleep 1.5; }
say warm "tput cols > $P/out/cols; tput lines >> $P/out/cols" || { kill -TERM $PID; exit 1; }
echo "$TAG grid=$(tr '\n' 'x' < $P/out/cols) load=$(cut -d' ' -f1 /proc/loadavg)"
p0=$(pss); r0=$(awk '/VmRSS/{print $2}' /proc/$PID/status)
say s1 "seq 1 2000000"
p1=$(pss); r1=$(awk '/VmRSS/{print $2}' /proc/$PID/status)
say s2 "seq 1 2000000"
p2=$(pss); r2=$(awk '/VmRSS/{print $2}' /proc/$PID/status)
echo "$TAG pss_kb empty=$p0 after1=$p1 after2=$p2   rss_kb empty=$r0 after1=$r1 after2=$r2"
echo "$TAG per_scrollback_line_bytes=$(awk -v a=$p0 -v b=$p1 'BEGIN{printf "%.0f",(b-a)*1024/10000}')"
kill -TERM $PID 2>/dev/null; sleep 3
