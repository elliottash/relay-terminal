#!/usr/bin/env bash
# #GSJ7 diagnostic: with a verified window size and a drag that is confirmed to move the divider,
# does the toast appear at all, and does Alt+0 put the splitter back?
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=$root/build
out=$root/docs/qa_evidence/2026-09-20-equalize-key-and-drag-hint/diag
width=1400 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-diag.XXXX)
xvfb_pid= relay_pid=
cleanup() { local pid; for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.local/share" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"; ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true

[security]
approvals_chosen=true
approvals_ask=@Invalid()
CONF
work=$HOME/project; mkdir -p "$work"
(cd "$work" && exec "$build/relay" --workspace "$work" --fresh) >"$sandbox/relay.log" 2>&1 &
relay_pid=$!; sleep 9
win= ; best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
done
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height"
xdotool windowfocus "$win"; sleep 3
for attempt in 1 2 3; do
    eval "$(xdotool getwindowgeometry --shell "$win")"
    (( WIDTH >= width - 2 )) && break
    xdotool windowsize "$win" "$width" "$height"; sleep 2
done
echo "window ${WIDTH}x${HEIGHT}"
sleep 3

sizes() { python3 - "$XDG_DATA_HOME/relay/state/windows.json" <<'PY'
import json, sys
try: doc = json.load(open(sys.argv[1]))
except Exception as e: print("none: %s" % e); raise SystemExit
found = []
def walk(n):
    if "split" in n:
        if n["split"] == "h" and not found: found.append(n.get("sizes", []))
        for c in n.get("children", []): walk(c)
for w in doc.get("windows", []):
    for t in w.get("tabs", []): walk(t.get("node", t))
print(" ".join(str(s) for s in found[0]) if found else "none")
PY
}
shot() { import -window root "$out/$1.png"; }

xdotool key --window "$win" ctrl+e; sleep 3; sleep 2
s0=$(sizes); a=($s0); dx=$(( a[0] + 2 ))
echo "after ctrl+e: [$s0], divider x=$dx"

# Drag: button down, four moves, up. Screenshot every 0.6 s afterwards to catch a toast.
xdotool mousemove "$dx" 450; sleep 0.4; xdotool mousedown 1; sleep 0.3
xdotool mousemove $((dx + 40)) 450; sleep 0.25
xdotool mousemove $((dx + 80)) 450; sleep 0.25
xdotool mousemove $((dx + 160)) 450; sleep 0.5
xdotool mouseup 1
echo "after drag: [$(sizes)]"
for i in $(seq 1 6); do shot "drag-$i"; sleep 0.6; done

# Alt+0, with the sizes and a screenshot every 0.5 s.
echo "--- alt+0"
xdotool key --window "$win" alt+0
for i in $(seq 1 6); do echo "  t+$i: [$(sizes)]"; shot "alt0-$i"; sleep 0.5; done

# And the same with the old chord, in case only this one reaches the app.
echo "--- ctrl+alt+0"
xdotool key --window "$win" ctrl+alt+0
for i in $(seq 1 4); do echo "  t+$i: [$(sizes)]"; done

echo "--- OCR of the drag and alt0 shots"
for f in "$out"/drag-*.png "$out"/alt0-*.png; do
    echo "== $(basename "$f"): $(tesseract "$f" stdout --psm 11 2>/dev/null | tr '\n' ' ' | tr -s ' ' | rg -io 'next time.{0,60}|panes equalized|equal panes' | head -3 | tr '\n' ';')"
done