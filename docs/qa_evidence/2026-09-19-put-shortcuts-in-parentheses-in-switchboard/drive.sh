#!/usr/bin/env bash
# Card #QG60, shortcuts in parentheses in Switchboard labels: implementer screenshots under
# Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. Harness copied from
# the 2026-09-19 relaying-line run (#HQ2B) — its sandbox skeleton, relay.conf isolation note and
# stub provider carry over unchanged. The workspace is a sandbox copy of this repo's issues/
# board (a snapshot), so the pane shows a real Switchboard; no model is called, the stub is only
# there so the preset resolves and no provider-setup dialog appears.
#
#   docs/qa_evidence/2026-09-19-put-shortcuts-in-parentheses-in-switchboard/drive.sh [build-dir] [scene...]
#
# Scenes (all by default), each screenshot into <scene>.png (whole window) plus <scene>-2x.png
# (the same frame scaled 200 % for reading the small UI text / OCR):
#   list        the board list page — "+  New card (n)" and "Clean up" in the tools row.
#   card        a card opened from the list — "Edit (e)", "#ID → prompt (t)", "Open file (o)",
#               "←  Back to board (Esc)", and the reply row "Comment (Ctrl+Shift+Enter)" /
#               "Discuss (Enter)" / "Plan (p)" / "Execute (x)".
#   narrow      the same card page in a 420 px window — the suffixed rows fit or wrap cleanly.
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-list card narrow}
port=${RELAY_QA_PORT:-8817}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-qg60.XXXXXX)
stub_pid= xvfb_pid= relay_pid= scene=current
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

# The #HQ2B stub provider (committed a directory up): only so the preset resolves.
python3 "$root/docs/qa_evidence/2026-09-19-relaying-line-left-normal/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

k() { xdotool key --delay 60 "$@"; }

prepare() {   # prepare [width] [height]
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    # A snapshot of this repo's Switchboard: board.yaml plus its cards, frozen for the run.
    cp -r "$root/issues" "$work/issues"
    rm -rf "$work/issues/needs_qa_evidence" 2>/dev/null
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[isolation]
enabled=false
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
    width=${1:-1440} height=${2:-900}
}

start() {   # start [width] [height] — leaves the window up, the board list on screen
    prepare "${1:-1440}" "${2:-900}"
    relay_pid=
    for attempt in 1 2; do
        (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay-stderr.log" 2>&1 &
        relay_pid=$!
        sleep 7
        win= ; local best=0 w
        for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
            eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
            (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
        done
        [[ -n $win ]] && break
        kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
        ((attempt == 2)) && { echo "no Relay window"; cat "$sandbox/relay-stderr.log"; exit 1; }
    done
    xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height"
    xdotool windowfocus "$win"; sleep 1.5
    k ctrl+shift+s          # Switchboard (#QG60's pane); it selects the first card on focus
    sleep 3                 # cards read from disk, no worker round-trip
}

stop() {
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null; relay_pid=
    for name in relay worker; do
        [[ -f $XDG_DATA_HOME/relay/logs/$name.log ]] && cp "$XDG_DATA_HOME/relay/logs/$name.log" "$out/logs/$scene-$name.log"
    done
}

shoot() {   # shoot <name>
    mkdir -p "$out/logs"
    xdotool mousemove 1580 980; sleep 0.6
    import -window "$win" "$out/$1.png"
    convert "$out/$1.png" -scale 200% "$out/$1-2x.png"
}

ocr() {   # ocr <name> — sparse-text OCR of the 2× frame, for the label greps in the README
    tesseract "$out/$1-2x.png" - --psm 11 2>/dev/null
}

# Click a card row and wait for the card page. The keyboard never reached the list (Return
# landed in the terminal pane behind it), but a row click opens a card outright (BoardPane.cpp:
# itemClicked → openSelected). The row is found by OCR on a probe frame: tesseract gives
# bounding boxes per word at the 2× scale of the probe, so the click halves them back. The
# snapshot board's INBOX section is empty and shows explainer text that is NOT a row, so the
# anchors are words from real card titles — this card's own ("put shortcuts in parentheses in
# switchboard", sitting in Needs QA (LLM)) — and each click is verified by OCR ("Back to
# board") before the scene is shot.
open_first_card() {
    local probe x2 y2 w2 h2 box filterY2
    # Filter the list down to this card first: at 289 open cards the rows sit below a long
    # label cloud and no row is on screen to click. The filter field is found by its own
    # placeholder word ("Filter — any word in the card, label:bug, …").
    probe=$(mktemp /tmp/qg60-probe.XXXXXX.png)
    import -window "$win" "$probe"
    convert "$probe" -scale 200% "$probe"
    box=$(tesseract "$probe" - --psm 11 tsv 2>/dev/null \
          | awk -F'\t' 'NF == 12 && $11 > 30 && $12 == "Filter" {print $7, $8, $9, $10; exit}')
    if [[ -n $box ]]; then
        read -r x2 y2 w2 h2 <<<"$box"
        xdotool mousemove --window "$win" $(( (x2 + 20) / 2 )) $(( (y2 + h2 / 2) / 2 ))
        sleep 0.3
        xdotool click 1; sleep 0.3
        xdotool type --delay 25 'parentheses'
        sleep 1.5                     # the list re-renders to the matching card(s)
        filterY2=$y2
    else
        filterY2=0
    fi
    rm -f "$probe"
    # TSV is one line per *word*, and Ctrl+Shift+S opens the board as a *split* beside the
    # terminal, so anchors must be words only the board shows. The label cloud between the
    # tabs and the rows also spells "switchboard" and the field holds the typed "parentheses",
    # so the anchors are this card's title words that exist nowhere else: "put", "shortcuts".
    probe=$(mktemp /tmp/qg60-probe.XXXXXX.png)
    import -window "$win" "$probe"
    convert "$probe" -scale 200% "$probe"
    box=$(tesseract "$probe" - --psm 11 tsv 2>/dev/null \
          | awk -F'\t' -v below=$(( filterY2 + 60 )) 'NF == 12 && $11 > 30 && $8 > below \
                       && ($12 ~ /^#[A-Za-z0-9]{4}$/ || $12 ~ /^(put|shortcuts)$/) \
                       {print $7, $8, $9, $10; exit}')
    rm -f "$probe"
    if [[ -z $box ]]; then
        echo "open_first_card: no card row word on screen to click"
        return 1
    fi
    read -r x2 y2 w2 h2 <<<"$box"
    xdotool mousemove --window "$win" $(( (x2 + w2 / 2) / 2 )) $(( (y2 + h2 / 2) / 2 ))
    sleep 0.4
    xdotool click 1
    sleep 1.8
    return 0
}

list() {
    scene=list
    start
    shoot list
    stop
}

card() {
    scene=card
    start 1900 900        # the board opens as a split; wide window gives the pane room
    open_first_card
    shoot card
    stop
}

narrow() {
    scene=narrow
    start 1440 900        # the split leaves the board pane ~290 px wide: the tight case
    open_first_card
    shoot narrow
    stop
}

for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
