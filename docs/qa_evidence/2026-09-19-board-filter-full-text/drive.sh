#!/usr/bin/env bash
# Full-text filter (owner, 2026-09-19: "switchboard filter bar should be full text search"):
# implementer evidence under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and
# TMPDIR. No provider account and no model: the scenes are the pane's own filter box and the
# worker's rows.
#
#   docs/qa_evidence/2026-09-19-board-filter-full-text/drive.sh [build-dir]
#
# Two cards whose titles say nothing about the words searched for:
#   A1AA has "the composer eats the third bullet point" in its body;
#   B2BB has "does the hotline work offline?" only in a thread entry (author dana, a question).
# Scenes:
#   before        the unfiltered list, both sections, and the filter box's new placeholder
#   word-in-body  filter "bullet"   -> only A1AA's section survives
#   word-in-thread filter "hotline" -> only B2BB's section survives
#
# Needs Xvfb, xdotool, ImageMagick; tesseract for the OCR notes.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1440 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-filter.XXXXXX)
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

rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$work/.switchboard/features" "$work/.switchboard/threads"
printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[appearance]
pane_colours=type
CONF
cat >"$work/.switchboard/board.yaml" <<'YAML'
# Switchboard configuration. Format: docs/SWITCHBOARD-FORMAT.md
version: 1
tabs: [{id: features, folder: features}, {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
YAML
card() {   # card <path> <id> <status> <title> <issue>
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
$5
CARD
}
card features/a.md A1AA inbox      "Voice transcription mode" "the composer eats the third bullet point"
card features/b.md B2BB discussing "Where the queue badge goes" "the badge should sit beside the count"
# Thread entries: the only places "hotline" and "zeppelin" appear on the whole board.
cat >"$work/.switchboard/threads/B2BB.md" <<'THREAD'
<!-- relay:entry 20260919T120000Z-a1 author=dana kind=question -->
- does the hotline work offline?
THREAD
cat >"$work/.switchboard/threads/A1AA.md" <<'THREAD'
<!-- relay:entry 20260919T120100Z-a1 author=owner kind=note -->
- zeppelin: remembered only from this thread note
THREAD

(cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 7
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 1.5

shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x260+0+30 +repage -scale 200% \
            "$out/implementer-$1-tools.png"
    { echo "--- $1"; tesseract "$out/implementer-$1.png" - --psm 6 2>/dev/null; } \
        >>"$out/implementer-notes.txt"
}

# Ctrl+Shift+S opens the Switchboard pane; `/` focuses its filter box.
k ctrl+shift+s; sleep 4
shot before
filter() {   # filter <scene> <word>: clear the box, type the word, shoot what survives
    k slash; sleep 0.4
    k ctrl+a; k Delete; sleep 0.4
    xdotool type --delay 60 "$2"; sleep 1.5
    shot "$1"
}
filter word-in-body    "bullet"    # A1AA's body: "the composer eats the third bullet point"
filter word-in-body2   "beside"    # B2BB's body: "the badge should sit beside the count"
filter word-in-thread  "hotline"   # B2BB's thread: "does the hotline work offline?"
filter word-in-thread2 "zeppelin"  # A1AA's thread: "zeppelin: remembered only from ..."
k ctrl+a; k Delete
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
mkdir -p "$out/logs" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null
