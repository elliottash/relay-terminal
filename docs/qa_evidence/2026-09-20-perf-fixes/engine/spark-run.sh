#!/usr/bin/env bash
# #6W0Z before/after: bulk-output scenarios in a real Relay pane on spark.
#   spark-run.sh <relay binary> <tag> [width height]
# Mirrors the #PF4K profiler's sphinx-run.sh: isolated profile, Xvfb, one pane,
# wall time measured by the shell inside the pane, CPU split per thread.
set -uo pipefail
BIN=$1; TAG=$2; W=${3:-1400}; H=${4:-900}
P=/tmp/claude-1000/pf4k/fix/engine/run
IN=/tmp/claude-1000/pf4k/engine/in
mkdir -p $P/out
disp=
for n in $(seq 81 120); do [[ -e /tmp/.X11-unix/X$n ]] || { disp=:$n; break; }; done
[[ -z $disp ]] && { echo "no free display"; exit 1; }
Xvfb $disp -screen 0 ${W}x${H}x24 >/dev/null 2>&1 &
xvfb=$!
sleep 2
export DISPLAY=$disp
S=$P/h.$TAG
rm -rf $S; mkdir -p $S/run $S/cfg $S/data $S/state $S/cache $S/tmp $S/work
chmod 700 $S/run
export HOME=$S XDG_RUNTIME_DIR=$S/run XDG_CONFIG_HOME=$S/cfg XDG_DATA_HOME=$S/data \
       XDG_STATE_HOME=$S/state XDG_CACHE_HOME=$S/cache TMPDIR=$S/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb
mkdir -p $XDG_CONFIG_HOME/RelayTerminal
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" > $S/.bashrc
hz=$(getconf CLK_TCK)

(setsid "$BIN" --clean-shell --fresh -w $S/work > $P/out/relay-$TAG.log 2>&1 &)
sleep 15
WIN=""; PID=""
for w in $(xdotool search --name "Relay" 2>/dev/null); do
  n=$(xdotool getwindowname $w 2>/dev/null); p=$(xdotool getwindowpid $w 2>/dev/null)
  case "$n" in Relay*) [ -n "$p" ] && WIN=$w && PID=$p;; esac
done
[ -z "$PID" ] && { echo "$TAG: no relay window"; kill $xvfb; exit 1; }
xdotool windowsize $WIN $W $H >/dev/null 2>&1
sleep 2
echo "TAG=$TAG BIN=$BIN WIN=$WIN PID=$PID DISPLAY=$disp geom=${W}x${H}"

snap() { for t in /proc/$PID/task/*; do [ -r "$t/stat" ] || continue; awk -v tid="${t##*/}" '{n=split($0,a," "); print tid, a[14]+a[15]}' "$t/stat"; done; }
run() {
  local name=$1 cmd=$2
  local stamp=$P/out/$TAG.$name.stamp; rm -f "$stamp"
  declare -A before; while read -r tid ticks; do before[$tid]=$ticks; done < <(snap)
  xdotool windowactivate --sync $WIN 2>/dev/null
  xdotool type --delay 4 -- "s=\$(date +%s%N); $cmd; e=\$(date +%s%N); echo \$s \$e > $stamp"
  xdotool key --clearmodifiers ctrl+shift+Return
  local deadline=$((SECONDS+300))
  while [ ! -s "$stamp" ] && [ $SECONDS -lt $deadline ]; do sleep 0.1; done
  sleep 1
  local rss; rss=$(awk '/VmHWM/{print $2}' /proc/$PID/status)
  local line="$TAG $name"
  if [ -s "$stamp" ]; then read -r s e <"$stamp"; line="$line wall_s=$(awk -v s=$s -v e=$e 'BEGIN{printf "%.3f",(e-s)/1e9}')"; else line="$line wall_s=TIMEOUT"; fi
  line="$line peak_rss_kb=$rss load=$(cut -d' ' -f1 /proc/loadavg)"
  declare -A after
  while read -r tid ticks; do after[$tid]=$ticks; done < <(snap)
  for tid in "${!after[@]}"; do
    d=$(( ${after[$tid]} - ${before[$tid]:-0} )); [ "$d" -gt 0 ] || continue
    c=$(cat /proc/$PID/task/$tid/comm 2>/dev/null)
    [ "$tid" = "$PID" ] && c="GUI"; [ "$c" = "relay" ] && c="pty"
    line="$line ${c}_cpu_s=$(awk -v d=$d -v h=$hz 'BEGIN{printf "%.2f",d/h}')"
  done
  echo "$line"
}

run geom "echo grid $(tput cols)x$(tput lines) > $P/out/$TAG.grid"
for i in 1 2 3; do run "cat50_$i" "cat $IN/b64_50.txt"; done
run seq "seq 1 2000000"
# memory per scrollback line: an empty pane, then 10 000 lines of ~80 characters
echo "$TAG smaps_empty $(grep -E '^Pss:' /proc/$PID/smaps_rollup | awk '{s+=$2} END{print s}') kB"
run tenk "yes '0123456789012345678901234567890123456789012345678901234567890123456789012345678' | head -n 10000"
sleep 2
echo "$TAG smaps_10k $(grep -E '^Pss:' /proc/$PID/smaps_rollup | awk '{s+=$2} END{print s}') kB"
echo "$TAG vmhwm $(awk '/VmHWM/{print $2}' /proc/$PID/status) kB"
kill -TERM $PID 2>/dev/null; sleep 3; kill -9 $PID 2>/dev/null
kill $xvfb 2>/dev/null
