#!/usr/bin/env bash
# Paints per second and GUI-thread statx per second during a flood.
#   probe-run.sh <relay binary> <tag>
set -uo pipefail
BIN=$1; TAG=$2
P=/tmp/claude-1000/pf4k/fix/engine/run
IN=/tmp/claude-1000/pf4k/engine/in
mkdir -p $P/out
disp=
for n in $(seq 81 120); do [[ -e /tmp/.X11-unix/X$n ]] || { disp=:$n; break; }; done
Xvfb $disp -screen 0 1400x900x24 >/dev/null 2>&1 & xvfb=$!
sleep 2
export DISPLAY=$disp
S=$P/p.$TAG; rm -rf $S; mkdir -p $S/run $S/cfg $S/data $S/state $S/cache $S/tmp $S/work
chmod 700 $S/run
export HOME=$S XDG_RUNTIME_DIR=$S/run XDG_CONFIG_HOME=$S/cfg XDG_DATA_HOME=$S/data \
       XDG_STATE_HOME=$S/state XDG_CACHE_HOME=$S/cache TMPDIR=$S/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb
mkdir -p $XDG_CONFIG_HOME/RelayTerminal
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" > $S/.bashrc
(setsid "$BIN" --clean-shell --fresh -w $S/work > $P/out/probe-$TAG.log 2>&1 &)
sleep 15
WIN=""; PID=""
for w in $(xdotool search --name "Relay" 2>/dev/null); do
  n=$(xdotool getwindowname $w 2>/dev/null); p=$(xdotool getwindowpid $w 2>/dev/null)
  case "$n" in Relay*) [ -n "$p" ] && WIN=$w && PID=$p;; esac
done
[ -z "$PID" ] && { echo "$TAG: no window"; kill $xvfb; exit 1; }
xdotool windowsize $WIN 1400 900 >/dev/null 2>&1; sleep 3

off=0x$(nm "$BIN" | awk '/T _ZN5relay12TerminalView10paintEventEP11QPaintEvent$/{print $1}')
sudo -n perf probe -x "$BIN" -d 'probe_relay:paint*' >/dev/null 2>&1
sudo -n perf probe -x "$BIN" --add "paint=$off" >/dev/null 2>&1 || { echo "$TAG: probe failed at $off"; }

flood() {   # keep ~25 s of output coming
  xdotool windowactivate --sync $WIN 2>/dev/null
  xdotool type --delay 4 -- "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do cat $IN/b64_50.txt; done"
  xdotool key --clearmodifiers ctrl+shift+Return
}
flood
sleep 3
echo "== $TAG paints, 4 s of flood"
sudo -n perf stat -e probe_relay:paint -p $PID -- sleep 4 2>&1 | grep -E "probe_relay:paint|seconds"
echo "== $TAG GUI-thread statx + readlinkat, 4 s of flood"
sudo -n perf stat -e syscalls:sys_enter_statx,syscalls:sys_enter_readlinkat -t $PID -- sleep 4 2>&1 | grep -E "sys_enter|seconds"
echo "== $TAG load=$(cut -d' ' -f1 /proc/loadavg)"
sudo -n perf probe -x "$BIN" -d 'probe_relay:paint*' >/dev/null 2>&1
kill -TERM $PID 2>/dev/null; sleep 3; kill -9 $PID 2>/dev/null; kill $xvfb 2>/dev/null
