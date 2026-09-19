#!/usr/bin/env bash
# The agent internals pane (card #QT8C): implementer screenshots under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a local
# model endpoint at stub-provider.py on 127.0.0.1. Derived from the K48R harness
# (docs/qa_evidence/2026-09-19-thinking-fold-cap/drive.sh).
#
#   docs/qa_evidence/2026-09-19-agent-internals-pane/drive.sh [build-dir] [scene...]
#
# Scenes (all by default):
#   open       Alt+Shift+R opens the pane; a turn with reasoning and three tool calls streams
#              into it and the terminal prints none of it (open-stream, open-tools, open-done);
#              a second turn (open-two-turns); closing the pane (Ctrl+W on it) reprints both
#              turns' rows under their rules (closed); a reprinted row clicked unfolds
#              (closed-unfolded); opened and closed again with nothing new prints nothing
#              (closed-twice).
#   midturn    the pane is closed while the run_command still runs: the thought row so far is
#              reprinted, then the live rows continue under it (midturn-closed, midturn-done).
#   midblock   the pane is opened while the reasoning streams: the inline fold settles to
#              "✦ thinking moved to the internals pane" and the pane has the block from its
#              start (midblock).
#   less       the pane is closed while `less` owns the screen: nothing is printed until q
#              brings the prompt back (less-open, less-quit).
#   twopanes   two terminal panes in one tab, each with its own internals pane: an ask in the
#              second fills the second pane only (twopanes-open, twopanes-asked).
#   ownerclose the internals pane is dragged to another split position and its owner is then
#              closed: the pane goes with it (ownerclose-moved, ownerclose-after).
#   hint       opened the slow way (the Actions palette): the toast teaches Alt+Shift+R (hint).
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-open midturn midblock less twopanes ownerclose hint}
width=1440 height=900
port=${RELAY_QA_PORT:-8831}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-internals.XXXXXX)
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

ask() { t "$1"; k Return; }
open_pane() { k alt+shift+r; sleep 1.2; }             # opens (or brings forward) the internals pane
back_to_terminal() { k alt+Left; sleep 0.6; }         # pane.focusLeft: the terminal, to type the ask
close_pane() { k alt+Right; sleep 0.6; k ctrl+w; sleep 1.5; }   # focus the pane, pane.close

# The word box of the first `needle` in the last shot, for a click on a reprinted row.
find_word() {   # find_word <shot-name> <word>  → "x y" or "-1 -1"
    tesseract "$out/implementer-$1.png" "$sandbox/box" tsv 2>/dev/null
    python3 - "$sandbox/box.tsv" "$2" <<'PY'
import sys
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1])]
head, needle = rows[0], sys.argv[2].lower()
for r in rows[1:]:
    if len(r) == len(head):
        w = dict(zip(head, r))
        if w["text"].strip().lower().startswith(needle):
            print(int(w["left"]) + int(w["width"]) // 2, int(w["top"]) + int(w["height"]) // 2)
            break
else:
    print(-1, -1)
PY
}

open() {
    scene=open; start
    open_pane; shot open-empty
    back_to_terminal
    ask 'please check the stable'
    sleep 3;  shot open-stream        # reasoning streams in the pane; the terminal shows only "▸ agent"
    sleep 7;  shot open-tools         # the run row, then the merged reads, in the pane
    sleep 6;  shot open-done          # the answer is in the terminal; the pane holds the rest
    ask 'and once more please'
    sleep 16; shot open-two-turns     # two rules, two blocks in the pane
    close_pane; sleep 1; shot closed  # both turns reprinted under their rules, collapsed
    read -r cx cy < <(find_word closed 'sleep')
    if [[ $cx -gt 0 ]]; then
        echo "open: clicking the reprinted run row at $cx,$cy" >>"$out/implementer-notes.txt"
        xdotool mousemove "$cx" "$cy"; sleep 0.4; xdotool click 1; sleep 1.5
        shot closed-unfolded          # the row's fold: the command and its output
    else
        echo "open: no 'sleep' word found in the closed shot" >>"$out/implementer-notes.txt"
    fi
    open_pane; sleep 0.5; close_pane; sleep 1; shot closed-twice   # nothing new: nothing printed twice
    stop
}

midturn() {
    scene=midturn; start
    open_pane; back_to_terminal
    ask 'please check the stable'
    sleep 8                           # the reasoning is done, the run_command is sleeping
    close_pane; sleep 1; shot midturn-closed   # the thought row reprinted; the run row is still to come
    sleep 9; shot midturn-done                 # …and it came inline, under the reprinted row
    stop
}

midblock() {
    scene=midblock; start
    ask 'please check the stable'
    sleep 2.5                         # mid-reasoning: the inline fold is streaming
    open_pane; sleep 1.5; shot midblock         # the fold settled to "moved to the internals pane"; the pane has the block
    sleep 14; shot midblock-done
    stop
}

less() {
    scene=less; start
    open_pane; back_to_terminal
    ask 'please check the stable'
    sleep 17
    t 'less README.md'; k Return; sleep 2      # a program owns the screen
    close_pane; sleep 1; shot less-open        # closed under less: nothing is printed into it
    back_to_terminal; k ctrl+h; sleep 0.5; k q; sleep 2.5   # take control of less, quit it
    shot less-quit                             # the prompt is back: the reprint arrives
    stop
}

click_at() { xdotool mousemove "$1" "$2"; sleep 0.4; xdotool click 1; sleep 0.8; }

# Two terminal panes in one tab, each with an internals pane of its own: the ask goes to the
# second terminal, and only the second pane fills.
twopanes() {
    scene=twopanes; start
    k ctrl+e; sleep 5                 # a second terminal pane, to the right; it takes the focus
    open_pane                         # …and its own internals pane, beside it
    click_at $((width / 6)) 300       # back to the first terminal
    open_pane                         # the first terminal's own internals pane
    shot twopanes-open                # two terminals, two internals panes, both empty
    click_at $((width * 5 / 8)) 300   # the second terminal
    ask 'please check the stable'
    sleep 12; shot twopanes-asked     # only the second terminal's pane has the turn
    stop
}

# The pane drags to another split position and still belongs to its owner: closing the owner
# takes it along, and nothing is reprinted into a pane that is going.
ownerclose() {
    scene=ownerclose; start
    k ctrl+e; sleep 5                 # two terminals: closing one must not close the window
    open_pane                         # the second terminal's internals pane, under it (720 px wide)
    click_at $((width * 3 / 4)) 150   # its owner, the second terminal
    ask 'please check the stable'
    sleep 14                          # a turn's worth of rows is in the pane, hidden from the terminal
    click_at $((width * 3 / 4)) 600   # the internals pane
    k ctrl+alt+Left; sleep 2          # pane.moveLeft: away from its owner, beside the first terminal
    shot ownerclose-moved             # the pane in its new split position, still holding the turn
    click_at $((width * 3 / 4)) 150   # its owner again
    k ctrl+w; sleep 2.5
    shot ownerclose-after             # owner and pane both gone; the first terminal is alone
    stop
}

# The slow path teaches the key: the Actions palette's own hint (runFromSettings).
hint() {
    scene=hint; start
    k ctrl+shift+a; sleep 2           # the Actions pane
    t 'agent internals'; sleep 1.2
    k Return; sleep 2
    shot hint                         # the pane opened, and the toast says "Next time: Alt+Shift+R"
    stop
}

# Only a full run starts the notes again: re-shooting one scene must not drop the others'.
[[ -z ${*:-} ]] && rm -f "$out/implementer-notes.txt"
for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
