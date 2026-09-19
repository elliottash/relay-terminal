#!/usr/bin/env bash
# The Activity pane's button on the "Relaying – …" line (card #4X53): implementer screenshots
# under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider
# account: the profile points a local model endpoint at stub-provider.py on 127.0.0.1, whose
# turn takes ~15 s, so the busy line is up long enough to read it and to click its icon.
# Derived from docs/qa_evidence/2026-09-19-agent-internals-pane/drive.sh (#QT8C).
#
#   docs/qa_evidence/2026-09-19-activity-pane-button/drive.sh [build-dir] [scene...]
#
# Scenes (all by default):
#   turn     an agent turn: the line is violet and carries the circled relay mark at its left,
#            before the word, on the prompt text's own left edge (turn-button, and a 4x crop of
#            the line itself in turn-button-zoom).
#   program  `sleep 20` in the terminal: the same button in the terminal's blue (program-button,
#            program-button-zoom).
#   clicked  the icon is clicked during a turn: the Activity pane opens beside the terminal, its
#            band, its title and its tab all saying Activity (clicked), and clicking it again
#            focuses that same pane rather than opening a second one (clicked-twice).
#   palette  the Actions palette's row for it says Activity (palette).
#   hint     the shortcut hint on the button's path: a second turn, started more than the 20 s
#            global hint gap after the first turn's toast, and the click then teaches Alt+Shift+R
#            (hint).
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-turn program clicked palette hint}
width=1440 height=900
port=${RELAY_QA_PORT:-8837}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-activity.XXXXXX)
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
    printf '# project\n\nA readme with a few lines.\n' >"$work/README.md"
    printf '# notes\n\n- one\n- two\n' >"$work/notes.md"
    # approvals_chosen keeps the first-launch approvals pane (#K2FV) out of the window: it would
    # take half of it and turn the Activity pane's split from beside the terminal into under it.
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
thinking_display=collapse
[security]
approvals_chosen=true
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
}

start() {
    prepare
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win= ; local best=0 candidate
    for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
    done
    [[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" "$width" $height
    xdotool windowfocus "$win"; sleep 1.5
}

stop() {
    cp "$sandbox/relay.log" "$out/relay-$scene.log" 2>/dev/null
    mkdir -p "$out/logs-$scene" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs-$scene/" 2>/dev/null
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

shot() {   # shot <name> — the window, and OCR of the left (terminal) and right (pane) halves
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop "$((width / 2))x760+0+60" +repage -scale 150% "$sandbox/left.png"
    convert "$out/implementer-$1.png" -crop "$((width / 2))x760+$((width / 2))+60" +repage -scale 150% "$sandbox/right.png"
    { echo "--- $1 (terminal, left half)"; tesseract "$sandbox/left.png" - --psm 6 2>/dev/null
      echo "--- $1 (right half)"; tesseract "$sandbox/right.png" - --psm 6 2>/dev/null; } >>"$out/implementer-notes.txt"
}

# The word box of the first `needle` in a shot: "left top width height", or "-1 -1 -1 -1".
find_word() {   # find_word <shot-name> <word>
    tesseract "$out/implementer-$1.png" "$sandbox/box" tsv 2>/dev/null
    python3 - "$sandbox/box.tsv" "$2" <<'PY'
import sys
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1])]
head, needle = rows[0], sys.argv[2].lower()
for r in rows[1:]:
    if len(r) == len(head):
        w = dict(zip(head, r))
        if w["text"].strip().lower().startswith(needle):
            print(w["left"], w["top"], w["width"], w["height"])
            break
else:
    print(-1, -1, -1, -1)
PY
}

# A 4x crop around the "Relaying" word, so the mark inside the circle can actually be read.
zoom_line() {   # zoom_line <shot-name> <zoom-name>
    read -r lx ly lw lh < <(find_word "$1" 'Relaying')
    if [[ $lx -lt 0 ]]; then echo "$1: no 'Relaying' word found" >>"$out/implementer-notes.txt"; return 1; fi
    convert "$out/implementer-$1.png" -crop "300x$((lh + 16))+$((lx - 40))+$((ly - 8))" +repage \
        -scale 400% "$out/implementer-$2.png"
    echo "$1: Relaying at ${lx},${ly} ${lw}x${lh}; zoom in implementer-$2.png" >>"$out/implementer-notes.txt"
}

# The icon sits immediately left of the word, one gap away: its centre is about 12 px left of the
# word's left edge, on the word's own middle line.
click_icon() {   # click_icon <shot-name>
    read -r lx ly lw lh < <(find_word "$1" 'Relaying')
    if [[ $lx -lt 0 ]]; then echo "$1: no 'Relaying' word to aim at" >>"$out/implementer-notes.txt"; return 1; fi
    local cx=$((lx - 12)) cy=$((ly + lh / 2))
    echo "$1: clicking the icon at $cx,$cy (word left edge $lx)" >>"$out/implementer-notes.txt"
    xdotool mousemove "$cx" "$cy"; sleep 0.5; xdotool click 1; sleep 1.6
}

# xdotool's first few keystrokes after the window comes up are dropped (the composer takes the
# focus a moment later), so every typed line starts by clicking into the prompt box.
focus_prompt() { xdotool mousemove 300 $((height - 74)); sleep 0.4; xdotool click 1; sleep 0.6; }

ask() { focus_prompt; t "$1"; k Return; }

turn() {
    scene=turn; start
    ask 'please check the stable'
    sleep 3; shot turn-button          # violet: "Relaying – thinking…", the mark at its left
    zoom_line turn-button turn-button-zoom
    sleep 14; stop
}

program() {
    scene=program; start
    focus_prompt
    t 'sleep 20'; k Return; sleep 3    # a runnable command goes to the terminal (InputPolicy)
    shot program-button                # blue: "Relaying – sleep…", the same mark
    zoom_line program-button program-button-zoom
    stop
}

clicked() {
    scene=clicked; start
    ask 'please check the stable'
    sleep 3
    shot before-click                  # the line, for the icon's position
    click_icon before-click
    shot clicked                       # the Activity pane beside the terminal: band, title, tab
    sleep 9
    shot clicked-streaming             # …and the turn's reasoning and tool rows are in it
    # Clicking it again brings that same pane forward rather than opening a second one. The click
    # left the focus in the Activity pane, so the terminal has to be focused to type the next ask.
    k alt+Left; sleep 0.6
    ask 'and once more please'
    sleep 3
    shot before-second-click
    click_icon before-second-click
    shot clicked-twice
    stop
}

palette() {
    scene=palette; start
    k ctrl+shift+a; sleep 2            # the Actions pane
    t 'activity'; sleep 1.2
    shot palette                       # the row: "Activity · Watch the reasoning and the tool calls…"
    k Return; sleep 2
    shot palette-opened                # …and the toast teaches Alt+Shift+R
    stop
}

# The `internals.open` hint fires from the button's path like any other slow path, but hints keep
# a 20 s global gap (relay::ShortcutHints::kGlobalGapSeconds) and the first turn's "Reasoning
# streams under the ✦ line" toast takes it. So: one whole turn, a wait past the gap, then a second
# turn whose button click has the toast lane to itself.
hint() {
    scene=hint; start
    ask 'please check the stable'
    sleep 18                           # the whole turn, and its toast, are done
    sleep 24                           # …and past the 20 s global gap between hints
    ask 'and once more please'
    sleep 3
    shot before-hint
    click_icon before-hint
    shot hint                          # "Next time: Alt+Shift+R · opens the Activity pane"
    stop
}

# Only a full run starts the notes again: re-shooting one scene must not drop the others'.
[[ -z ${*:-} ]] && rm -f "$out/implementer-notes.txt"
for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
