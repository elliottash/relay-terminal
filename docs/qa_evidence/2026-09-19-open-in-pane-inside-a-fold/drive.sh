#!/usr/bin/env bash
# The fold's "open in pane" link (issue #7FD3): live under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a local
# model endpoint at stub-provider.py on 127.0.0.1 (the harness of the 2026-09-19 thinking-fold
# run, with the scenes this issue needs, stub courtesy of the 2026-09-17 wrong-mode-hints run).
#
#   docs/qa_evidence/2026-09-19-open-in-pane-inside-a-fold/drive.sh [build-dir] [scene...]
#
# Scenes (both by default), each shot as implementer-<scene>-<step>.png with OCR notes in
# implementer-notes.txt; the script fails (non-zero) the moment an assertion does:
#   run    ask "seq 1 40" -> one run_command call. Click the "ran seq 1 40" row, the fold
#         opens, click the "open in pane" link at its foot: a preview pane with the call's
#         RUN COMMAND detail must open (before 7241a40 this always said "detail is not
#         available", one second after the call). The click lands ~2 s after the answer.
#   merged ask "please show me those six files" -> six read_file calls merged into one
#         "read 6 files" row. Its fold must offer no "open in pane" link, and a member
#         row's file link still opens the file's preview.
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-run merged}
width=1440 height=900
port=${RELAY_QA_PORT:-8827}

display=
for n in $(seq 200 239); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-open-in-pane.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in ${relay_pid:-} ${stub_pid:-} ${xvfb_pid:-}; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >"$sandbox/stub.log" 2>&1 &
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
    # The pane's agent worker runs in a systemd user scope (src/Isolation.h) and the app removes
    # DBUS_SESSION_BUS_ADDRESS from that worker's environment, so systemd-run finds the user
    # manager through $XDG_RUNTIME_DIR/bus. A sandbox runtime dir has no bus of its own: without
    # this link the scope fails and the pane shows "The agent worker exited."
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work/src"
    printf 'PS1='"'"'\\w \\$ '"'"'\nunset PROMPT_COMMAND\n' >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    # The merged run's six reads. A marker word per file, so a preview that opened the right
    # file is an OCR assertion, not a guess.
    local i=0 name
    for name in a b c d e f; do
        i=$((i + 1))
        { printf 'MARKER-%s file %d\n' "$(echo "$name" | tr a-f A-F)" "$i"; seq 1 11 | sed "s/^/$name line /"; } >"$work/src/$name.py"
    done
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[provider]
preset=local:stub
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
}

start() {
    prepare
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
}

stop() {
    cp "$sandbox/relay.log" "$out/relay-$scene.log" 2>/dev/null
    mkdir -p "$out/logs-$scene" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs-$scene/" 2>/dev/null
    [[ -n ${relay_pid:-} ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

shot() {   # shot <name> — the window, a header crop and a body crop, all OCR'd into the notes
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/implementer-$scene-$1.png"
    convert "$out/implementer-$scene-$1.png" -crop 1000x40+0+36 +repage -scale 300% "$out/implementer-$scene-$1-head.png"
    convert "$out/implementer-$scene-$1.png" -crop ${width}x640+0+90 +repage -scale 150% "$out/implementer-$scene-$1-body.png"
    { echo "--- $scene-$1 (header)"; tesseract "$out/implementer-$scene-$1-head.png" - --psm 7 2>/dev/null
      echo "--- $scene-$1 (body)";   tesseract "$out/implementer-$scene-$1-body.png"  - --psm 6 2>/dev/null
      echo "--- $scene-$1 (window)"; tesseract "$out/implementer-$scene-$1.png"       - --psm 6 2>/dev/null
    } >>"$out/implementer-notes.txt"
}

text_of() { tesseract "$out/implementer-$scene-$1.png" - --psm 6 2>/dev/null; }

# The centre ("x y") of a word's OCR bounding box in implementer-<scene>-<step>.png, tries the
# raw frame first, then an upscaled inverted one (the theme is light-on-dark). $3/$4 add a crop
# origin (search only a region, e.g. the fold's foot away from the header's words).
word_xy() {  # word_xy <step> <word> [xoff] [yoff]
    local step=$1 word=$2 xoff=${3:-0} yoff=${4:-0} found=""
    local png=$out/implementer-$scene-$step.png
    local region=$png
    (( xoff || yoff )) && { convert "$png" -crop $((width - xoff))x$((height - yoff))+$xoff+$yoff +repage "$png.region.png"; region=$png.region.png; }
    local variant img
    for variant in plain inverted; do
        img=$region
        [[ $variant == inverted ]] && { convert "$region" -resize 150% -colorspace Gray -negate "$region.inv.png"; img=$region.inv.png; }
        found=$(tesseract "$img" - tsv 2>/dev/null | awk -F'\t' -v w="$word" -v inv="$variant" '
            $1==5 && $12==w && $11+0>=30 && !seen {seen=1; x=$7+$9/2; y=$8+$10/2}
            END { if (seen) printf "%d %d", (inv=="inverted"? x/1.5 : x), (inv=="inverted"? y/1.5 : y) }')
        [[ $variant == inverted ]] && rm -f "$region.inv.png"
        [[ -n $found ]] && break
    done
    rm -f "$png.region.png"
    [[ -n $found ]] || return 1
    echo $((${found% *} + xoff)) $((${found#* } + yoff))
}

click_word() {  # click_word <step> <word> [xoff] [yoff]
    local xy
    xy=$(word_xy "$@") || { echo "OCR lost the word '$2' in $scene-$1"; exit 1; }
    xdotool mousemove --sync ${xy% *} ${xy#* } click 1
}

ask() { t "$1"; k Return; }

fail() {
    echo "FAIL [$scene] $*"
    cp "$sandbox/relay.log" "$out/relay-$scene-fail.log" 2>/dev/null
    cp "$sandbox/stub.log" "$out/stub-$scene-fail.log" 2>/dev/null
    exit 1
}

# ---- scene: the fold's own "open in pane" link, on a seconds-old call ------------------------
run() {
    scene=run; start
    # AUTO input routes "seq 1 40" alone to the terminal (it reads like a command); phrased as a
    # request it goes to the agent, whose run_command call is what this scene needs.
    ask 'could you run seq 1 40 please'
    sleep 5                                  # the turn: the call runs, the answer lands
    shot row
    text_of row | grep -qi "ran seq" || fail "no 'ran seq 1 40' row after the turn"
    click_word row ran                       # the fold row: the fold opens under it
    sleep 1.2; shot fold
    text_of fold | grep -qi "open in pane" || fail "the open fold shows no 'open in pane' link"
    # The call is ~6 s old and the pane never restarted: the exact case that failed every time.
    click_word fold pane                       # the link row at the fold's foot (its only "pane")
    sleep 2; shot preview
    text_of preview | grep -Eqi "RUN +COMMAND|Working directory" || fail "no RUN COMMAND preview pane opened"
    text_of preview | grep -qi "not available" && fail "status line still says the detail is not available"
    text_of preview | grep -q "40" || fail "the preview does not show the call's 40 lines"
    stop
}

# ---- scene: a merged run's fold offers no "open in pane", but files still open ---------------
merged() {
    scene=merged; start
    ask 'please show me those six files'
    sleep 6                                  # six reads, merged into one row
    shot row
    text_of row | grep -qi "read 6 files\|read 6" || fail "no merged 'read 6 files' row after the turn"
    click_word row read                      # the merged row: the fold opens under it
    sleep 1.2; shot fold
    text_of fold | grep -q "a.py" || fail "the merged fold does not list member a.py"
    text_of fold | grep -qi "in pane" && fail "a merged run's fold must not offer 'open in pane'"
    click_word fold src/a.py                  # a member row's file link (one OCR token)
    sleep 1.5; shot file
    text_of file | grep -q "MARKER-A" || fail "clicking member a.py did not open its file preview"
    stop
}

for scene in $scenes; do
    "$scene"
done
echo "OK: $scenes"
