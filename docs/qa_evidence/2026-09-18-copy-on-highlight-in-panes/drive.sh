#!/usr/bin/env bash
# Copy on highlight in every pane: implementer screenshots under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_RUNTIME_DIR and TMPDIR, so a live Relay on this machine
# can own neither the clipboard nor the profile being read.
#
#   docs/qa_evidence/2026-09-18-copy-on-highlight-in-panes/drive.sh [build-dir] [scene...]
#
# Scenes (both by default):
#   on    `terminal/copy_on_select` on: highlight text in the terminal, the conversation-info
#         pane, the Markdown file preview and the Switchboard card, and print what landed on the
#         clipboard and on PRIMARY each time.
#   off   the setting off, which is how it ships: the same four drags copy nothing.
#
# Before every drag both selections are set to a sentinel, so "nothing was copied" and "something
# was copied" are told apart by reading them back rather than by an empty result. Each surface
# gets its own Relay run so the pane layout, and with it every coordinate, stays put.
#
# No provider account: the profile points a local endpoint at stub-provider.py on 127.0.0.1.
# Needs Xvfb, xdotool, ImageMagick and xclip.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-build}; [[ $build == /* ]] || build=$root/$build; shift || true
scenes=${*:-on off}
width=1440 height=900
port=${RELAY_QA_PORT:-8791}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-copy-on-select.XXXXXX)
xvfb_pid= relay_pid= stub_pid=
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

log=$out/implementer-clipboard.txt

k() { xdotool key --delay 60 "$@"; }
t() { xdotool type --delay 18 "$1"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep 0.6; }
# A real highlight: press at x1,y1, drag across to x2,y2 in two steps, release.
drag() {
    xdotool mousemove "$1" "$2" mousedown 1; sleep 0.3
    xdotool mousemove $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )); sleep 0.2
    xdotool mousemove "$3" "$4"; sleep 0.3
    xdotool mouseup 1; sleep 1.2
}

sentinel="SENTINEL-nothing-was-copied"
arm() {   # the sentinel on both selections, so a copy shows up as a change
    printf '%s' "$sentinel" | xclip -i -selection clipboard
    printf '%s' "$sentinel" | xclip -i -selection primary
    sleep 0.5
}
report() {   # report <label>
    { printf '\n--- %s\n' "$1"
      printf '  CLIPBOARD: %s\n' "$(xclip -o -selection clipboard 2>/dev/null | head -c 300 | tr '\n' '|')"
      printf '  PRIMARY  : %s\n' "$(xclip -o -selection primary   2>/dev/null | head -c 300 | tr '\n' '|')"
    } | tee -a "$log"
}

prepare() {   # prepare <copy_on_select>
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" \
             "$XDG_CACHE_HOME" "$work/issues/features"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    cat >"$work/NOTES.md" <<'DOC'
# Copy on highlight

The quick brown fox jumps over the lazy dog. Highlighting any of these words should copy them.
DOC
    cp "$root/issues/board.yaml" "$work/issues/"
    cp "$root"/issues/features/2026-09-17-*.md "$work/issues/features/" 2>/dev/null
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[terminal]
copy_on_select=$1
[provider]
preset=local:stub
CONF
}

start() {   # start <copy_on_select>
    prepare "$1"
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

stop() { [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null; relay_pid=; }

shot() {   # shot <name>
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
}

# 1. The terminal, which has had copy on select all along: the regression check.
terminal_scene() {   # terminal_scene <mode> <setting>
    start "$2"
    arm
    drag 26 90 655 118
    shot "$1-1-terminal"
    report "$1 / terminal pane (the behaviour this matches)"
    stop
}

# 2. The conversation info pane: SessionInfo's QTextBrowser body.
info_scene() {   # info_scene <mode> <setting>
    start "$2"
    t '*hello'; k Return; sleep 8
    click 1307 60; sleep 3          # the ⓘ button in the pane header (Alt+I is the fast path)
    arm
    drag 748 165 1120 225
    shot "$1-2-info-pane"
    report "$1 / conversation info pane (the ⓘ button / Alt+I)"
    stop
}

# 3. The file preview pane: FilePreview's rendered-Markdown QTextBrowser.
preview_scene() {   # preview_scene <mode> <setting>
    start "$2"
    k ctrl+shift+b; sleep 3         # the folder explorer
    click 790 200; sleep 3          # NOTES.md: its preview opens beside it
    arm
    drag 978 143 1310 178
    shot "$1-3-file-preview"
    report "$1 / file preview pane, rendered Markdown (Ctrl+Shift+B, NOTES.md)"
    stop
}

# 4. The Switchboard card detail: BoardPane's card-document QTextBrowser.
board_scene() {   # board_scene <mode> <setting>
    start "$2"
    k ctrl+shift+s; sleep 4         # the Switchboard
    click 1200 380; sleep 1         # a card in the READY column
    k Return; sleep 3               # its detail
    arm
    drag 748 345 1400 378
    shot "$1-4-board-card"
    report "$1 / Switchboard card detail (Ctrl+Shift+S, Enter on a card)"
    stop
}

: >"$log"
for scene in $scenes; do
    setting=false; [[ $scene == on ]] && setting=true
    printf '\n===== terminal/copy_on_select = %s =====\n' "$setting" | tee -a "$log"
    terminal_scene "$scene" "$setting"
    info_scene     "$scene" "$setting"
    preview_scene  "$scene" "$setting"
    board_scene    "$scene" "$setting"
done
printf '\ndone: %s\n' "$out"
