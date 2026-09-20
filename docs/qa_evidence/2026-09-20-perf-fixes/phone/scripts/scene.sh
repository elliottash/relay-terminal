#!/usr/bin/env bash
# One profiled scenario against the shared pane (#PF4K phone area).
#
#   ./scene.sh <label> <seconds> <prompt>
#
# Types <prompt> into the desktop pane (DISPLAY/WIN from the environment), and around it:
#   * `dumpsys gfxinfo com.android.chrome reset` before, a full read after (frames + jank),
#   * the phone tab's WebSocket frames and a JS sampling profile (wsbytes.py),
#   * the worker->GUI byte table for exactly that turn (seg.py over the IPC shim's log),
#   * per-thread CPU for Chrome (dumpsys cpuinfo) at the end.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
label=$1; secs=$2; prompt=$3
out=${OUT:-/tmp/claude-1000/pf4k/phone/out}
A="adb -s ${SERIAL:-192.168.1.184:46849}"
py=/tmp/claude-1000/pf4k/phone/v/bin/python
ipc=$out/qa5.ipc/worker2gui.jsonl

$A shell dumpsys gfxinfo com.android.chrome reset >/dev/null 2>&1
$py "$here/wsbytes.py" "$secs" "$out/$label" 42061 --profile >"$out/$label.ws.txt" 2>&1 &
ws=$!
sleep 3
w0=$(wc -c <"$ipc")
export DISPLAY=${DISPLAY:-:210}
xdotool mousemove "${PROMPT_X:-180}" "${PROMPT_Y:-825}" click 1
sleep 0.4
xdotool type --delay 12 -- "$prompt"
sleep 0.6
xdotool key --clearmodifiers Return
wait $ws
w1=$(wc -c <"$ipc")
{
  echo "== $label  prompt: $prompt   window ${secs}s   load $(cut -d' ' -f1-3 /proc/loadavg)"
  echo "-- worker -> GUI for this turn ($w0 -> $w1)"
  python3 "$here/seg.py" "$ipc" "$w0" "$w1"
  echo
  echo "-- phone websocket + JS"
  cat "$out/$label.ws.txt"
  echo
  echo "-- gfxinfo (com.android.chrome) since reset"
  $A shell dumpsys gfxinfo com.android.chrome 2>/dev/null |
      grep -A 14 -E "Total frames rendered|HISTOGRAM" | head -24
  echo
  echo "-- cpuinfo (chrome rows)"
  $A shell dumpsys cpuinfo 2>/dev/null | grep -i -E "chrome|Load:|TOTAL" | head -12
} >"$out/$label.report.txt" 2>&1
cat "$out/$label.report.txt"
