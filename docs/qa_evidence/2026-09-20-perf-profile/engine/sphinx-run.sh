#!/usr/bin/env bash
# #PF4K engine area: run the bulk-output scenarios in a real Relay pane on sphinxpad.
#   sphinx-run.sh <relay binary> <tag>
set -uo pipefail
BIN=$1; TAG=$2
P=$HOME/relay-perf/engine
export DISPLAY=${RDISP:-:71}
export HOME_ORIG=$HOME
export XDG_RUNTIME_DIR=$P/h/run XDG_CONFIG_HOME=$P/h/cfg XDG_DATA_HOME=$P/h/data \
       XDG_STATE_HOME=$P/h/state XDG_CACHE_HOME=$P/h/cache TMPDIR=$P/h/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb
rm -rf $P/h; mkdir -p $P/h $XDG_RUNTIME_DIR $XDG_CONFIG_HOME $XDG_DATA_HOME $XDG_STATE_HOME $XDG_CACHE_HOME $TMPDIR $P/work $P/out
chmod 700 $XDG_RUNTIME_DIR
export HOME=$P/h

hz=$(getconf CLK_TCK)

(setsid "$BIN" --clean-shell --fresh -w $P/work > $P/relay-$TAG.log 2>&1 &)
sleep 18
WIN=""; for w in $(xdotool search --name "Relay"); do n=$(xdotool getwindowname $w); p=$(xdotool getwindowpid $w 2>/dev/null); case "$n" in Relay*) [ -n "$p" ] && WIN=$w && PID=$p;; esac; done
echo "TAG=$TAG BIN=$BIN WIN=$WIN PID=$PID"
[ -z "$PID" ] && { echo "no relay window"; exit 1; }

snap() { for t in /proc/$PID/task/*; do [ -r "$t/stat" ] || continue; awk -v tid="${t##*/}" '{n=split($0,a," "); print tid, a[14]+a[15]}' "$t/stat"; done; }

run() {
  local name=$1 cmd=$2
  local stamp=$P/out/$name.stamp; rm -f "$stamp"
  declare -A before; while read -r tid ticks; do before[$tid]=$ticks; done < <(snap)
  xdotool type --delay 4 -- "s=\$(date +%s%N); $cmd; e=\$(date +%s%N); echo \$s \$e > $stamp"
  xdotool key --clearmodifiers ctrl+shift+Return
  local deadline=$((SECONDS+300))
  while [ ! -s "$stamp" ] && [ $SECONDS -lt $deadline ]; do sleep 0.1; done
  sleep 1
  local rss; rss=$(awk '/VmHWM/{print $2}' /proc/$PID/status)
  local line="$TAG $name"
  if [ -s "$stamp" ]; then read -r s e <"$stamp"; line="$line wall_s=$(awk -v s=$s -v e=$e 'BEGIN{printf "%.3f",(e-s)/1e9}')"; else line="$line wall_s=TIMEOUT"; fi
  line="$line peak_rss_kb=$rss load=$(cut -d' ' -f1 /proc/loadavg)"
  declare -A after; while read -r tid ticks; do after[$tid]=$ticks; done < <(snap)
  for tid in "${!after[@]}"; do
    d=$(( ${after[$tid]} - ${before[$tid]:-0} )); [ "$d" -gt 0 ] || continue
    c=$(cat /proc/$PID/task/$tid/comm 2>/dev/null)
    [ "$tid" = "$PID" ] && c="GUI"; [ "$c" = "relay" ] && c="pty"
    line="$line ${c}_cpu_s=$(awk -v d=$d -v h=$hz 'BEGIN{printf "%.2f",d/h}')"
  done
  echo "$line"
}

run geom 'echo grid $(tput cols)x$(tput lines)'
for i in 1 2 3; do run "cat50_$i" "cat $P/in/b64_50.txt"; done
for i in 1 2 3; do run "sgr_$i" "cat $P/in/sgr.txt"; done
for i in 1 2 3; do run "uni_$i" "cat $P/in/uni.txt"; done
for i in 1 2 3; do run "seq_$i" "seq 1 2000000"; done
for i in 1 2; do run "lscolor_$i" "ls --color=always -R /usr 2>/dev/null"; done
echo "RSS_EMPTY_AFTER_ALL=$(awk '/VmRSS/{print $2}' /proc/$PID/status)"
grep -E "^(Pss|Private_Dirty)" /proc/$PID/smaps_rollup 2>/dev/null | sed "s/^/$TAG smaps /"
kill -TERM $PID 2>/dev/null
sleep 3
