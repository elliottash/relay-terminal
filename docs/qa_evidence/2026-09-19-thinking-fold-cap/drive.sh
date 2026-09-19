#!/usr/bin/env bash
# The thinking fold's height cap (issue K48R): implementer screenshots under Xvfb with an isolated
# HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a
# local model endpoint at stub-provider.py on 127.0.0.1. Derived from the T8CN harness
# (docs/qa_evidence/2026-09-19-thinking-fold/drive.sh).
#
#   docs/qa_evidence/2026-09-19-thinking-fold-cap/drive.sh [build-dir] [scene...]
#
# Scenes (all by default):
#   long     240 lines of reasoning: the fold stays six rows under "… N earlier lines" the whole
#            way (stream-1/2/3), and the settled fold reopened by hand is eighteen rows over
#            "… N more lines · open in pane" (done, reopened).
#   narrow   a 40-column pane and one 5,000-character paragraph — no newline in it at all — which
#            is the shape a cap counted in source lines cannot see (narrow-stream, narrow-done).
#   link     "open in pane" on a settled fold: clicked (its word box found by OCR), the turn pane
#            opens beside the terminal with the whole reasoning in it.
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-long narrow link}
width=1440 height=900
port=${RELAY_QA_PORT:-8824}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-fold-cap.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    # The pane's worker runs in a systemd user scope and finds the user manager through
    # $XDG_RUNTIME_DIR/bus; a sandbox runtime dir has none, so link the real one (T8CN's lesson).
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[appearance]
pane_colours=type
[provider]
preset=local:stub
[agent]
thinking_display=${MODE:-collapse}
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
}

start() {   # start [window-width]
    prepare
    local w=${1:-$width}
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win= ; local best=0 candidate
    for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
    done
    [[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" "$w" $height
    xdotool windowfocus "$win"; sleep 1.5
}

stop() {
    cp "$sandbox/relay.log" "$out/relay-$scene.log" 2>/dev/null
    mkdir -p "$out/logs-$scene" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs-$scene/" 2>/dev/null
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

shot() {   # shot <name> — the window, a 300 % header crop, and OCR of the pane body
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop 1000x40+0+36 +repage -scale 300% "$out/implementer-$1-head.png"
    convert "$out/implementer-$1.png" -crop "$(xdotool getwindowgeometry --shell "$win" | sed -n 's/^WIDTH=//p')x700+0+90" \
        +repage -scale 150% "$out/implementer-$1-body.png" 2>/dev/null
    { echo "--- $1 (body)"; tesseract "$out/implementer-$1-body.png" - --psm 6 2>/dev/null; } >>"$out/implementer-notes.txt"
}

ask() { t "$1"; k Return; }

# 240 lines of reasoning: six rows while it streams, eighteen when it is opened after done.
long() {
    scene=long; MODE=collapse; start
    ask 'please think long about the route'
    sleep 4;  shot stream-1
    sleep 6;  shot stream-2
    sleep 6;  shot stream-3
    sleep 12; shot done            # collapsed: the anchor row alone
    k alt+r; sleep 1.5; shot reopened   # eighteen rows over "… N more lines · open in pane"
    stop
}

# A 40-column pane and one 5,000-character paragraph.
narrow() {
    scene=narrow; MODE=always; start 460
    ask 'please think wall about ponies'
    sleep 5;  shot narrow-stream
    sleep 12; shot narrow-done
    stop
}

# The "open in pane" row of a settled fold: click it, and the turn pane has the whole text.
link() {
    scene=link; MODE=always; start
    ask 'please think quick about ponies'
    sleep 10; shot link-before
    tesseract "$out/implementer-link-before.png" "$sandbox/box" tsv 2>/dev/null
    read -r cx cy < <(python3 - "$sandbox/box.tsv" <<'PY'
import sys
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1])]
head, words = rows[0], []
for r in rows[1:]:
    if len(r) == len(head):
        words.append(dict(zip(head, r)))
for i, w in enumerate(words[:-2]):
    if (w["text"].strip().lower() == "open" and words[i+1]["text"].strip().lower() == "in"
            and words[i+2]["text"].strip().lower().startswith("pane")):
        left, top, high = int(w["left"]), int(w["top"]), int(w["height"])
        right = int(words[i+2]["left"]) + int(words[i+2]["width"])
        print((left + right) // 2, top + high // 2)
        break
else:
    print(-1, -1)
PY
)
    if [[ $cx -lt 0 ]]; then echo "link: no 'open in pane' row found in the shot"; else
        echo "link: clicking the fold's link at $cx,$cy" >>"$out/implementer-notes.txt"
        xdotool mousemove "$cx" "$cy"; sleep 0.5; xdotool click 1; sleep 2.5
        shot link-opened
    fi
    stop
}

rm -f "$out/implementer-notes.txt"
for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
