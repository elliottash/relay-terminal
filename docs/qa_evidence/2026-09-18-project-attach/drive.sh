#!/usr/bin/env bash
# A tab is attached to a project, and starts attached to none (card #JN7X, stage 1).
#
#   docs/qa_evidence/2026-09-18-project-attach/drive.sh [build-dir]
#
# Runs Relay under Xvfb with a fully isolated profile (HOME, XDG_CONFIG_HOME, XDG_DATA_HOME,
# XDG_RUNTIME_DIR and TMPDIR all under a scratch directory) and RELAY_KEYRING=off, and **launches
# it from this repository** — which has a board of its own, so it is exactly the trap the card is
# about: under the old rule every pane in every window found *this* repo's Switchboard.
#
# The model is stub-provider.py on 127.0.0.1, which records the tool list and the Switchboard
# block of the system prompt of every turn into implementer-tools.log. That is how "the agent has
# no board tools before the attach and has them after, in the same conversation" is checked
# without a provider account.
#
# Fixture in the sandbox:
#   scratch/downloads   a plain directory: no board, no .git, nothing above it
#   scratch/proj-new    .git + switchboard/board.yaml + one card   (the new folder name)
#   scratch/proj-old    .git + issues/board.yaml      + one card   (the older folder name)
#   scratch/proj-none   .git and nothing else                      (a project with no board yet)
#
# Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1500 height=940
port=${RELAY_QA_PORT:-8811}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# Short, so the sockets Relay and its workers open stay inside the 108-byte sun_path limit.
sandbox=$(mktemp -d /tmp/rpa.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

rm -f "$out/implementer-tools.log" "$out/implementer-notes.txt"
python3 "$out/stub-provider.py" "$port" "$out/implementer-tools.log" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }
# The composer of the left-hand terminal pane, and a tab by index: the keyboard is in the
# Switchboard after Ctrl+Shift+S, so the next prompt has to be aimed at the pane by hand.
composer() { k Escape; sleep 0.4; xdotool mousemove 250 $((height - 75)) click 1; sleep 1; }
tab() { xdotool mousemove $((110 + 310 * $1)) 22 click 1; sleep 1.5; }
note() { printf '%s\n' "$*" >>"$out/implementer-notes.txt"; }
shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8; import -window "$win" "$out/implementer-$1.png"; }
# The status bar is one strip at the very bottom; crop it at 200 % so the sentence is readable.
bar() { convert "$out/implementer-$1.png" -crop ${width}x34+0+$((height - 34)) +repage -scale 200% "$out/implementer-$1-bar.png"; }

# ----- the sandbox and its fixture ------------------------------------------------------------
export HOME=$sandbox/h XDG_RUNTIME_DIR=$sandbox/r TMPDIR=$sandbox/t
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" \
         "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

S=$HOME/scratch
board_yaml() {
    printf 'version: 1\ntabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\ncolumns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]\nagent: {autonomy: auto, max_creates_per_turn: 5}\n' >"$1"
}
card() {   # card <folder> <id> <title>
    mkdir -p "$1"
    cat >"$1/2026-09-18-$2.md" <<CARD
---
id: $2
type: work
status: inbox
labels: [feature]
rank: '10'
created: '2026-09-18'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# $3

## Issue

$3
CARD
}
mkdir -p "$S/downloads"
for p in proj-new proj-old proj-none; do mkdir -p "$S/$p/.git"; done
mkdir -p "$S/proj-new/switchboard/threads" && board_yaml "$S/proj-new/switchboard/board.yaml"
card "$S/proj-new/switchboard/features" NEW1 "A card in the new switchboard folder"
mkdir -p "$S/proj-old/issues/threads" && board_yaml "$S/proj-old/issues/board.yaml"
card "$S/proj-old/issues/features" OLD1 "A card in the older issues folder"

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[windows]
restore=true
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

# What the whole fixture and this repository's own board look like before anything is driven.
snapshot() { (cd "$S" && find . -type f | sort); (cd "$root" && find issues .relay -type f 2>/dev/null | sort); }
snapshot >"$sandbox/tree-before.txt"

start() {
    (cd "$root" && exec "$build/relay") >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 8
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
}

# ----- 1. an unattached tab is quiet ------------------------------------------------------------
start
t "cd $S/downloads"; k Return; sleep 1.5
t '*hello from downloads'; k Return; sleep 6
shot 01-unattached-downloads
t '/card a card that must never be written'; k Return; sleep 3
shot 02-unattached-card-command
bar 02-unattached-card-command
k ctrl+shift+s; sleep 2                       # and the Switchboard key says the same thing
shot 03-unattached-switchboard-key
bar 03-unattached-switchboard-key
snapshot >"$sandbox/tree-after-downloads.txt"
if diff -q "$sandbox/tree-before.txt" "$sandbox/tree-after-downloads.txt" >/dev/null; then
    note "unattached: /card and Ctrl+Shift+S in ~/scratch/downloads wrote no file anywhere (fixture and this repo's issues/ and .relay/ identical before and after)"
else
    note "unattached: FILES CHANGED —"; diff "$sandbox/tree-before.txt" "$sandbox/tree-after-downloads.txt" >>"$out/implementer-notes.txt"
fi

# ----- 2. a project whose board is switchboard/ -------------------------------------------------
t "cd $S/proj-new"; k Return; sleep 1.5
k ctrl+shift+s; sleep 5
shot 04-attached-switchboard-folder
# Same pane, same conversation: the second turn is the one that shows the card tools arriving.
composer
t '*second turn, same chat'; k Return; sleep 7
shot 05-attached-same-conversation

# ----- 3. a project whose board is the older issues/ --------------------------------------------
k ctrl+t; sleep 3
t "cd $S/proj-old"; k Return; sleep 1.5
k ctrl+shift+s; sleep 5
shot 06-attached-issues-folder

# ----- 4. a project with no board at all --------------------------------------------------------
k ctrl+t; sleep 3
t "cd $S/proj-none"; k Return; sleep 1.5
k ctrl+shift+s; sleep 3
shot 07-project-with-no-board
bar 07-project-with-no-board
t '/card nothing may be created here either'; k Return; sleep 3
shot 08-project-with-no-board-card
bar 08-project-with-no-board-card
snapshot >"$sandbox/tree-after-noboard.txt"
if diff -q "$sandbox/tree-before.txt" "$sandbox/tree-after-noboard.txt" >/dev/null; then
    note "no board yet: Ctrl+Shift+S and /card in a .git project with no board created nothing (no switchboard/ folder, no card, nothing in this repo)"
else
    note "no board yet: FILES CHANGED —"; diff "$sandbox/tree-before.txt" "$sandbox/tree-after-noboard.txt" >>"$out/implementer-notes.txt"
fi

# ----- 5. quit and relaunch: the tab comes back attached ----------------------------------------
kill -TERM "$relay_pid"; wait "$relay_pid" 2>/dev/null; relay_pid=
sleep 2
python3 - "$XDG_DATA_HOME/relay/state/windows.json" >>"$out/implementer-notes.txt" <<'PY'
import json, sys
state = json.load(open(sys.argv[1]))
for w, window in enumerate(state.get("windows") or []):
    for i, tab in enumerate(window.get("tabs") or []):
        project = tab.get("project") if isinstance(tab.get("node"), dict) else None
        print("saved layout: window %d tab %d project=%s shape=%s"
              % (w, i, project or "(unattached)", "wrapper" if project else "bare node"))
PY
start
sleep 3
shot 09-restored-attached          # the layout is back: two attached tabs, one unattached
# The unattached tab offers no detach: the quiet state does not advertise itself.
k ctrl+shift+a; sleep 1.5
t 'Detach this tab'; sleep 1.5
shot 10-palette-no-detach-when-unattached
k Escape; sleep 1

# ----- 6. detach from the palette ----------------------------------------------------------------
tab 0                              # the tab that was attached to proj-new before the restart
shot 11-restored-tab-is-attached
composer
t '*after the restart'; k Return; sleep 7
shot 12-restored-tools-are-back
k ctrl+shift+a; sleep 1.5
t 'Detach this tab'; sleep 1.5
shot 13-palette-detach
k Return; sleep 2
shot 14-after-detach               # the Switchboard pane is still open on its board
bar 14-after-detach
composer
t '*after detaching'; k Return; sleep 7
shot 15-detached-same-conversation
kill -TERM "$relay_pid"; wait "$relay_pid" 2>/dev/null; relay_pid=

for f in "$out"/implementer-*.png; do convert "$f" -resize 1200x "$f"; done
note ""
note "tool lists the worker sent the model, in order (implementer-tools.log):"
cat "$out/implementer-tools.log" >>"$out/implementer-notes.txt"
note ""
note "board_state events in the pane log (relay.log):"
grep -h "board_state" "$XDG_DATA_HOME/relay/logs/relay.log" >>"$out/implementer-notes.txt" 2>/dev/null || note "(none)"
printf 'done: %s\n' "$out"
