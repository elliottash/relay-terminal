#!/usr/bin/env bash
# Run one shell command inside a live Relay pane and measure it.
#   run-scenario.sh NAME PID WINDOW 'shell command'
# Wall time is measured by the shell inside the pane (so it includes pty
# back-pressure). CPU is taken per thread from /proc/PID/task/*/stat, so the
# GUI thread and the pty thread are separated. Peak RSS from VmHWM.
set -uo pipefail
name=$1 pid=$2 win=$3 cmd=$4
out=${OUTDIR:-/tmp/claude-1000/pf4k/engine/out}
mkdir -p "$out"
stamp="$out/$name.stamp"
rm -f "$stamp"

snap() { # per-thread utime+stime in ticks, "tid comm ticks"
  for t in /proc/$pid/task/*; do
    [ -r "$t/stat" ] || continue
    awk -v tid="${t##*/}" '{ for(i=NF;i>0;i--) if($i ~ /^[0-9]+$/) ; }
      { n=split($0,a," "); print tid, a[14]+a[15] }' "$t/stat" 2>/dev/null
  done
}

hz=$(getconf CLK_TCK)
declare -A before
while read -r tid ticks; do before[$tid]=$ticks; done < <(snap)
t0=$(date +%s.%N)

xdotool windowactivate --sync "$win" 2>/dev/null
xdotool type --delay 4 -- "s=\$(date +%s%N); $cmd; e=\$(date +%s%N); echo \$s \$e > $stamp"
# The agent worker is not running in this harness, so Auto cannot classify the
# line: ctrl+shift+Return always runs it in the terminal.
xdotool key --clearmodifiers ctrl+shift+Return

deadline=$((SECONDS + ${TIMEOUT:-300}))
while [ ! -s "$stamp" ] && [ $SECONDS -lt $deadline ]; do sleep 0.1; done
sleep 1   # settle: last repaint
t1=$(date +%s.%N)
rss=$(awk '/VmHWM/{print $2}' /proc/$pid/status)
declare -A after
declare -A comm
while read -r tid ticks; do after[$tid]=$ticks; comm[$tid]=$(cat /proc/$pid/task/$tid/comm 2>/dev/null); done < <(snap)

if [ -s "$stamp" ]; then read -r s e <"$stamp"; wall=$(awk -v s="$s" -v e="$e" 'BEGIN{printf "%.3f",(e-s)/1e9}')
else wall=TIMEOUT; fi

echo "== $name wall_s=$wall harness_s=$(awk -v a=$t0 -v b=$t1 'BEGIN{printf "%.2f",b-a}') peak_rss_kb=$rss load=$(cut -d' ' -f1 /proc/loadavg)"
for tid in "${!after[@]}"; do
  d=$(( ${after[$tid]} - ${before[$tid]:-0} ))
  [ "$d" -gt 0 ] || continue
  printf '   thread %-8s %-16s cpu_s=%.2f\n' "$tid" "${comm[$tid]}" "$(echo "scale=3; $d/$hz" | bc)"
done
