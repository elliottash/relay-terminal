#!/usr/bin/env bash
# Scrollback memory per line and the grid, on a fresh pane.
#   mem-run.sh <relay binary> <tag> [w h]
set -uo pipefail
BIN=$1; TAG=$2; W=${3:-1400}; H=${4:-900}
P=/tmp/claude-1000/pf4k/fix/engine/run
mkdir -p $P/out
disp=
for n in $(seq 81 120); do [[ -e /tmp/.X11-unix/X$n ]] || { disp=:$n; break; }; done
Xvfb $disp -screen 0 ${W}x${H}x24 >/dev/null 2>&1 & xvfb=$!
sleep 2
export DISPLAY=$disp
S=$P/m.$TAG; rm -rf $S; mkdir -p $S/run $S/cfg $S/data $S/state $S/cache $S/tmp $S/work
chmod 700 $S/run
export HOME=$S XDG_RUNTIME_DIR=$S/run XDG_CONFIG_HOME=$S/cfg XDG_DATA_HOME=$S/data \
       XDG_STATE_HOME=$S/state XDG_CACHE_HOME=$S/cache TMPDIR=$S/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb
mkdir -p $XDG_CONFIG_HOME/RelayTerminal
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" > $S/.bashrc
(setsid "$BIN" --clean-shell --fresh -w $S/work > $P/out/mem-$TAG.log 2>&1 &)
sleep 15
WIN=""; PID=""
for w in $(xdotool search --name "Relay" 2>/dev/null); do
  n=$(xdotool getwindowname $w 2>/dev/null); p=$(xdotool getwindowpid $w 2>/dev/null)
  case "$n" in Relay*) [ -n "$p" ] && WIN=$w && PID=$p;; esac
done
[ -z "$PID" ] && { echo "$TAG: no window"; kill $xvfb; exit 1; }
xdotool windowsize $WIN $W $H >/dev/null 2>&1; sleep 3
pss() { awk '/^Pss:/{s+=$2} END{print s}' /proc/$PID/smaps_rollup; }
type_run() {
  xdotool windowactivate --sync $WIN 2>/dev/null
  xdotool type --delay 4 -- "$1"
  xdotool key --clearmodifiers ctrl+shift+Return
}
rm -f $S/work/grid.txt
type_run 'echo grid $(tput cols)x$(tput lines) > '"$S/work/grid.txt"
sleep 3
echo "$TAG grid=$(cat $S/work/grid.txt 2>/dev/null | tr -d '\n') pss_empty_kb=$(pss) rss_kb=$(awk '/VmRSS/{print $2}' /proc/$PID/status)"
# 10 000 lines of 79 characters
rm -f $S/work/done.txt
type_run "yes '0123456789012345678901234567890123456789012345678901234567890123456789012345678' | head -n 10000; touch $S/work/done.txt"
for i in $(seq 1 200); do [ -f $S/work/done.txt ] && break; sleep 0.2; done
sleep 4
echo "$TAG pss_10k_kb=$(pss) rss_kb=$(awk '/VmRSS/{print $2}' /proc/$PID/status) vmhwm_kb=$(awk '/VmHWM/{print $2}' /proc/$PID/status)"
# another 200 000 lines: the cap holds
rm -f $S/work/done2.txt
type_run "yes '0123456789012345678901234567890123456789012345678901234567890123456789012345678' | head -n 200000; touch $S/work/done2.txt"
for i in $(seq 1 300); do [ -f $S/work/done2.txt ] && break; sleep 0.2; done
sleep 4
echo "$TAG pss_210k_kb=$(pss) vmhwm_kb=$(awk '/VmHWM/{print $2}' /proc/$PID/status) load=$(cut -d' ' -f1 /proc/loadavg)"
kill -TERM $PID 2>/dev/null; sleep 3; kill -9 $PID 2>/dev/null; kill $xvfb 2>/dev/null
