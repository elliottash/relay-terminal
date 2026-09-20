#!/usr/bin/env bash
# Scroll the pane back through scrollback and measure the GUI thread's CPU.
#   scroll.sh NAME PID WIN KEY N     KEY = wheel | Prior (Shift+PgUp)
set -uo pipefail
name=$1 pid=$2 win=$3 mode=$4 n=$5
hz=$(getconf CLK_TCK)
gui=$pid
cpu() { awk '{print $14+$15}' /proc/$gui/task/$gui/stat; }
allcpu() { awk '{print $14+$15}' /proc/$gui/stat; }
xdotool windowactivate --sync "$win" 2>/dev/null
# park the pointer in the middle of the terminal area
eval "$(xdotool getwindowgeometry --shell $win)"
xdotool mousemove --sync $((X+WIDTH/2)) $((Y+HEIGHT/2))
sleep 0.5
c0=$(cpu); a0=$(allcpu); t0=$(date +%s.%N)
for i in $(seq 1 "$n"); do
  case $mode in
    wheel)   xdotool click 4 ;;
    wheeldn) xdotool click 5 ;;
    Prior)   xdotool key --clearmodifiers shift+Prior ;;
    Next)    xdotool key --clearmodifiers shift+Next ;;
  esac
done
sleep 1.2
t1=$(date +%s.%N); c1=$(cpu); a1=$(allcpu)
printf '%s mode=%s steps=%s wall_s=%.2f gui_cpu_s=%.3f proc_cpu_s=%.3f gui_ms_per_step=%.2f load=%s\n' \
  "$name" "$mode" "$n" "$(echo "$t1-$t0"|bc)" "$(echo "scale=3;($c1-$c0)/$hz"|bc)" \
  "$(echo "scale=3;($a1-$a0)/$hz"|bc)" "$(echo "scale=2;($c1-$c0)*1000/$hz/$n"|bc)" "$(cut -d' ' -f1 /proc/loadavg)"
