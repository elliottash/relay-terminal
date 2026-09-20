#!/bin/bash
# storm.sh <gui pid> <board dir> <seconds>
# One card write per second, as several sessions committing to issues/ all day produce, and
# what it costs the Switchboard: GUI CPU, worker CPU and the longest gap where the GUI was busy.
set -u
P=$1; DIR=$2; N=${3:-60}
HZ=$(getconf CLK_TCK)
cpu() { awk '{print ($14+$15)}' "/proc/$1/stat" 2>/dev/null || echo 0; }
kidcpu() { local t=0; for k in $(pgrep -P "$P" 2>/dev/null); do t=$((t + $(cpu "$k"))); done; echo $t; }
CARD=$(find "$DIR" -name '*.md' -path '*features*' | head -1)
THREAD=$(find "$DIR/threads" -name '*.md' | head -1)
g0=$(cpu "$P"); k0=$(kidcpu)
start=$(date +%s%N)
for i in $(seq "$N"); do
    case $((i % 3)) in
        0) printf '\n<!-- storm %s -->\n' "$i" >> "$CARD" ;;                    # a card edit
        1) printf '\n<!-- relay:entry 2026092%02dT000000Z-01 author=owner kind=comment -->\n- storm %s\n' \
               $((i % 10)) "$i" >> "$THREAD" ;;                                 # a thread append
        2) touch "$DIR/BOARD.md" ;;                                             # the index rewritten
    esac
    sleep 1
done
sleep 2
end=$(date +%s%N)
g1=$(cpu "$P"); k1=$(kidcpu)
secs=$(( (end - start) / 1000000000 ))
echo "storm ${N}s: gui_cpu=$(( (g1-g0) * 1000 / HZ ))ms worker_cpu=$(( (k1-k0) * 1000 / HZ ))ms over ${secs}s"
echo "  => gui $(( (g1-g0) * 100 / HZ / secs ))% of one core, worker $(( (k1-k0) * 100 / HZ / secs ))%"
