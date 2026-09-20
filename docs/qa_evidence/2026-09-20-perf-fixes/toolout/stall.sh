#!/usr/bin/env bash
# #PPR4 item 3: the ~70 ms event-loop stall at the start of a turn (FINDINGS.md finding 5).
#
# The published probe measures the gap between two ppoll()s, so the stack it could take at
# sys_enter_ppoll is always the event loop — the stall is already over. This one samples the GUI
# thread *inside* the gap: sys_exit_ppoll arms a timestamp, a 997 Hz profile probe collects a user
# stack whenever the thread has been busy for more than 20 ms, and sys_enter_ppoll disarms it.
# So every stack printed is a stack from inside a >20 ms event-loop iteration.
#
#   ./stall.sh <label> [prompt]
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
label=$1; prompt=${2:-'please stream zprose40000x20r200 now'}
out=${OUT:-$here/out}; mkdir -p "$out"
bin=${RELAY_BIN:-/tmp/claude-1000/pf4k/build/relay}

# One warm turn so the window, the shell and the worker are all settled; KEEP leaves it running.
info=$(OUT="$out" PORT=${PORT:-8961} RELAY_BIN="$bin" KEEP=1 "$here/h/drive.sh" "$label-boot" 3 'please stream zprose200x20r200 now' 2>&1 | tail -3)
echo "$info"
pid=$(sed -n 's/.*relay=\([0-9]*\).*/\1/p' <<<"$info" | head -1)
display=$(sed -n 's/.*display=\(:[0-9]*\).*/\1/p' <<<"$info" | head -1)
sb=$(sed -n 's/.*KEEP: sandbox \([^ ]*\).*/\1/p' <<<"$info" | head -1)
[[ -n $pid && -n $display ]] || { echo "no pid/display"; exit 1; }

sudo -n timeout 40s bpftrace -e "
  tracepoint:syscalls:sys_exit_ppoll  /tid == $pid/ { @back = nsecs; }
  tracepoint:syscalls:sys_enter_ppoll /tid == $pid/ {
      \$d = @back > 0 ? nsecs - @back : 0;
      if (\$d > 0) { @busy_us = hist(\$d / 1000); }
      if (\$d > 30000000) { printf(\"stall %d us at %d\n\", \$d / 1000, nsecs); }
      @back = 0;
  }
  profile:hz:997 /tid == $pid && @back > 0 && nsecs - @back > 10000000/ {
      @stall[ustack(perf, 28)] = count();
  }
  END { clear(@back); }" >"$out/$label.stall.txt" 2>&1 &
bpf=$!
sleep 3
for i in 1 2 3 4; do
    DISPLAY=$display xdotool type --delay 12 -- "$prompt"
    sleep 0.6
    DISPLAY=$display xdotool key --clearmodifiers Return
    sleep 5
done
wait $bpf
kill "$pid" 2>/dev/null
pkill -f "Xvfb $display" 2>/dev/null
[[ -n $sb ]] && rm -rf "$sb"
echo "--- $out/$label.stall.txt"
