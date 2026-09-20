#!/usr/bin/env bash
# Key/byte -> first paint latency.
#   lat-run.sh <relay binary> <tag>
set -uo pipefail
BIN=$1; TAG=$2
P=/tmp/claude-1000/pf4k/fix/engine/run
mkdir -p $P/out
disp=
for n in $(seq 81 120); do [[ -e /tmp/.X11-unix/X$n ]] || { disp=:$n; break; }; done
Xvfb $disp -screen 0 1400x900x24 >/dev/null 2>&1 & xvfb=$!
sleep 2
export DISPLAY=$disp
S=$P/l.$TAG; rm -rf $S; mkdir -p $S/run $S/cfg $S/data $S/state $S/cache $S/tmp $S/work
chmod 700 $S/run
export HOME=$S XDG_RUNTIME_DIR=$S/run XDG_CONFIG_HOME=$S/cfg XDG_DATA_HOME=$S/data \
       XDG_STATE_HOME=$S/state XDG_CACHE_HOME=$S/cache TMPDIR=$S/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb
mkdir -p $XDG_CONFIG_HOME/RelayTerminal
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" > $S/.bashrc
(setsid "$BIN" --clean-shell --fresh -w $S/work > $P/out/lat-$TAG.log 2>&1 &)
sleep 15
WIN=""; PID=""
for w in $(xdotool search --name "Relay" 2>/dev/null); do
  n=$(xdotool getwindowname $w 2>/dev/null); p=$(xdotool getwindowpid $w 2>/dev/null)
  case "$n" in Relay*) [ -n "$p" ] && WIN=$w && PID=$p;; esac
done
[ -z "$PID" ] && { echo "$TAG: no window"; kill $xvfb; exit 1; }
xdotool windowsize $WIN 1400 900 >/dev/null 2>&1; sleep 3
sh=$(pgrep -P $PID bash | head -1)
tty=$(readlink /proc/$sh/fd/0 2>/dev/null)
echo "$TAG pid=$PID shell=$sh tty=$tty"
off=0x$(nm "$BIN" | awk '/T _ZN5relay12TerminalView10paintEventEP11QPaintEvent$/{print $1}')
sudo -n perf probe -x "$BIN" -d 'probe_relay:paint*' >/dev/null 2>&1
sudo -n perf probe -x "$BIN" --add "paint=$off" >/dev/null 2>&1
rm -f $P/out/lat-$TAG.data
sudo -n perf record -k CLOCK_MONOTONIC -e probe_relay:paint -p $PID -o $P/out/lat-$TAG.data >/dev/null 2>&1 &
perfpid=$!
sleep 3
python3 - "$tty" "$P/out/lat-$TAG.sends" <<'PY'
import sys, time, os
tty, outp = sys.argv[1], sys.argv[2]
fd = os.open(tty, os.O_WRONLY)
sends = []
time.sleep(1.0)
for i in range(25):
    t = time.monotonic()
    os.write(fd, b"x")
    sends.append(t)
    time.sleep(0.4)
open(outp, "w").write("\n".join("%.9f" % t for t in sends) + "\n")
PY
sleep 1
sudo -n kill -INT $perfpid 2>/dev/null; wait $perfpid 2>/dev/null
sudo -n chmod a+r $P/out/lat-$TAG.data 2>/dev/null
sudo -n perf script -i $P/out/lat-$TAG.data -F time 2>/dev/null | tr -d ':' > $P/out/lat-$TAG.paints
python3 - "$P/out/lat-$TAG.sends" "$P/out/lat-$TAG.paints" "$TAG" <<'PY'
import sys
sends = [float(x) for x in open(sys.argv[1]) if x.strip()]
paints = sorted(float(x) for x in open(sys.argv[2]) if x.strip())
out = []
for t in sends:
    nxt = [p for p in paints if p >= t and p - t < 0.35]
    if nxt:
        out.append((nxt[0] - t) * 1000.0)
out.sort()
if not out:
    print(sys.argv[3], "no paints matched", len(paints), "paints")
else:
    n = len(out)
    print("%s n=%d min=%.1f p50=%.1f p90=%.1f max=%.1f ms" %
          (sys.argv[3], n, out[0], out[n // 2], out[int(n * 0.9)], out[-1]))
    print("   " + " ".join("%.1f" % v for v in out))
PY
sudo -n perf probe -x "$BIN" -d 'probe_relay:paint*' >/dev/null 2>&1
kill -TERM $PID 2>/dev/null; sleep 3; kill -9 $PID 2>/dev/null; kill $xvfb 2>/dev/null
