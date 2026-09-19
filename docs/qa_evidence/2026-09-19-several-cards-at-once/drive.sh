#!/usr/bin/env bash
# Two Switchboard cards planned at the same time (protocol 19.16; owner, 2026-09-19: "multiple
# agents working on planning switchboard cards doesnt seem to work ... if i was planning in one
# card, i couldnt plan in another card", and "it seems like the planning agent was getting stuck").
#
# Implementer screenshots under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and
# TMPDIR. This one DOES call a model: two live Plan turns on a throwaway board, on the desktop
# keyring's `glm-coding` key (never printed, never written here). The repository's own issues/ is
# not touched — the workspace is a scratch git repo with three cards and a toy source file.
#
#   docs/qa_evidence/2026-09-19-several-cards-at-once/drive.sh [build-dir]
#
# Needs Xvfb, xdotool, ImageMagick; tesseract for the OCR notes.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1500 height=950
INBOX_AT=${INBOX_AT:-1120,228}    # the "INBOX 3" header, read off implementer-01-board.png

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-cards.XXXXXX)
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
export DISPLAY=$display
unset RELAY_KEYRING            # the glm-coding key is read from the desktop keyring

k() { xdotool key --delay 60 "$@"; }

prepare() {
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"     # the keyring's D-Bus, nothing else
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$work/issues/features" "$work/src"
    printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[provider]
preset=glm-coding
[theme]
name=relay-dark
CONF
    cat >"$work/issues/board.yaml" <<'YAML'
# Switchboard configuration. Format: docs/SWITCHBOARD-FORMAT.md
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
YAML
    cat >"$work/src/complete.py" <<'PY'
"""Toy folder completion."""
import os


def candidates(prefix: str, root: str = ".") -> list[str]:
    out = []
    for name in sorted(os.listdir(root)):
        if not name.startswith(prefix):
            continue
        out.append(name + "/-" if os.path.isdir(os.path.join(root, name)) else name)
    return out
PY
    card() {   # card <file> <id> <title> <issue>
        cat >"$work/issues/features/$1" <<CARD
---
id: $2
type: work
status: inbox
rank: $5
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# $3

## Issue
$4
CARD
    }
    card a.md ZW95 'Tab completion adds a stray "-" after a folder' \
        'Completing a directory name gives `src/-` instead of `src/`.' m1
    card b.md SPBN 'Each pane type gets its own header colour' \
        'Different headers or colours for subagents, switchboard and options panes.' m2
    card c.md K7Q2 'Clickable paths in the output' \
        'A path printed by a command opens the file in a pane when clicked.' m3
    (cd "$work" && git init -q && git add -A \
        && git -c user.email=qa@example.invalid -c user.name=QA commit -qm init) >/dev/null
}

start() {
    prepare
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 8
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
    { echo "--- $1"; tesseract "$out/implementer-$1.png" - --psm 6 2>/dev/null; } \
        >>"$out/implementer-notes.txt"
}

main() {
    : >"$out/implementer-notes.txt"
    start
    k ctrl+shift+s; sleep 5                 # the Switchboard, on this project's board
    shot 01-board
    # A new pane starts with every section folded: open Inbox (the header is at a fixed place).
    xdotool mousemove "${INBOX_AT%,*}" "${INBOX_AT#*,}" click 1; sleep 2
    shot 01b-inbox-open

    # The first card: select it in the list and plan it (`p` works from the list).
    k Down; sleep 0.5
    k p; sleep 12
    shot 02-first-card-planning             # the strip: "Agent is planning…" and what it is doing

    # Back to the list and plan a second card while the first is still going.
    k Escape; sleep 1.5
    shot 03-list-while-one-plans            # the row wears the agent's mark
    k Down; sleep 0.5
    k p; sleep 12
    shot 04-second-card-planning            # this card's own turn, on its own strip
    k Escape; sleep 1.5
    shot 05-list-with-two-running           # two rows marked at once

    # Let both finish, then look at each card's plan.
    sleep 90
    shot 06-list-after                      # no marks left
    k Down; sleep 0.5; k Return; sleep 3
    shot 07-a-plan-on-its-card              # what one of the two turns wrote

    : >"$out/implementer-cards-after.txt"
    for f in "$work"/issues/features/*.md; do
        { echo "=== $(basename "$f")"; cat "$f"; } >>"$out/implementer-cards-after.txt"
    done
    echo "shots in $out"
}

main "$@"
