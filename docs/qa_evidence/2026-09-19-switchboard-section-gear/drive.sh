#!/usr/bin/env bash
# The section gear (owner, 2026-09-19: "put a gear after the list of switchboard sections, which
# allows you to add, remove, merge, or rename sections"): implementer screenshots under Xvfb with
# an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account and no model:
# every scene here is the pane's own UI and the worker's board.yaml write.
#
#   docs/qa_evidence/2026-09-19-switchboard-section-gear/drive.sh [build-dir] [scene...]
#
# Scenes:
#   list   the Switchboard with its section checkboxes and the gear at the end of that row
#   page   the gear's page: a row per section, Merge…/✕ per row, the add row, Save/Cancel
#   click  <x> <y> — click a point, then shoot (the gear's position is read off `list`)
#
# Needs Xvfb, xdotool, ImageMagick; tesseract for the OCR notes.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
width=1440 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-gear.XXXXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

k() { xdotool key --delay 60 "$@"; }

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$work/.switchboard/features" \
             "$work/.switchboard/changes/needs_qa_llm" "$work/.switchboard/changes/done"
    printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=${THEME:-relay-dark}
[appearance]
pane_colours=type
CONF
    # A board with a card in several lanes, so the checkbox row has every section in it.
    cat >"$work/.switchboard/board.yaml" <<'YAML'
# Switchboard configuration. Format: docs/SWITCHBOARD-FORMAT.md
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
YAML
    card() {   # card <path> <id> <status> <title>
        mkdir -p "$(dirname "$work/.switchboard/$1")"
        cat >"$work/.switchboard/$1" <<CARD
---
id: $2
type: work
status: $3
rank: m$2
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# $4

## Issue
$4
CARD
    }
    card features/a.md A1AA inbox        "Voice transcription mode"
    card features/b.md B2BB discussing   "Where the queue badge goes"
    card features/c.md C3CC ready        "Clickable paths in the output"
    card features/d.md D4DD in-progress  "The session index is incremental"
    card changes/needs_qa_llm/e.md E5EE needs-qa-llm "Ctrl+Enter sends now"
    card changes/done/f.md F6FF done     "The theme row says what it sets"
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
    xdotool windowfocus "$win"; sleep 1.5
}

shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    # The tools row and the section checkboxes: where the gear is, at 300 %.
    convert "$out/implementer-$1.png" -crop ${width}x150+0+30 +repage -scale 200% \
            "$out/implementer-$1-tools.png"
    { echo "--- $1"; tesseract "$out/implementer-$1.png" - --psm 6 2>/dev/null; } \
        >>"$out/implementer-notes.txt"
}

# Ctrl+Shift+S opens the Switchboard pane on this project's board.
scene_list() {
    start
    k ctrl+shift+s; sleep 4
    shot list
}

main() {
    : >"$out/implementer-notes.txt"
    scene_list
    if [[ -n ${GEAR_AT:-} ]]; then      # "x,y", read off implementer-list.png
        xdotool mousemove "${GEAR_AT%,*}" "${GEAR_AT#*,}" click 1; sleep 2
        shot page
        # Rename the third section, then Save, and shoot the list it comes back to.
        if [[ -n ${NAME_AT:-} ]]; then
            xdotool mousemove "${NAME_AT%,*}" "${NAME_AT#*,}" click 1; sleep 0.5
            xdotool key --delay 60 ctrl+a
            xdotool type --delay 40 "Up next"
            xdotool key --delay 60 Tab; sleep 0.5
            shot renamed
            [[ -n ${SAVE_AT:-} ]] && {
                xdotool mousemove "${SAVE_AT%,*}" "${SAVE_AT#*,}" click 1; sleep 3
                shot saved
                cp "$work/.switchboard/board.yaml" "$out/board-after-save.yaml"
            }
        fi
    fi
    cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
    mkdir -p "$out/logs" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null
}

main
