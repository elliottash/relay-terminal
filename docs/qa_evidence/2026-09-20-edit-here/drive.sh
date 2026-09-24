#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #SEJ2 — in the file explorer, Enter opens a file read-only, Ctrl+Enter opens it ready to edit,
# Shift+Enter hands it to the desktop; the preview's ✎ button and the menu's "Open external"
# teach those chords; a local edit saves with Ctrl+S and a dirty preview asks before it closes.
#
#   docs/qa_evidence/2026-09-20-edit-here/drive.sh [relay-binary] [out-dir]
#
# Drives a real Relay under Xvfb:
#
#   01  explorer open on a folder with note.txt
#   02  Enter on note.txt: a read-only preview, with the ✎ button in its header
#   03  back in the explorer, Ctrl+Enter on note.txt: the preview is editable, ✎ is gone
#   04  typed into it: the title carries ●
#   05  Ctrl+S: ● gone, "Saved" in the notice, the bytes are on disk
#   06  typed again, then Ctrl+W on the preview: the Save / Discard / Cancel question
#   07  Enter on the file again, read-only, then ✎ clicked: "Next time: Ctrl+Enter"
#   08  right-click → Open external: "Next time: Shift+Enter" in the explorer, and the desktop
#       was asked to open note.txt
#   09  Shift+Enter on readme.md: the desktop was asked to open readme.md
#
# "The desktop" is a stub `xdg-open` first on PATH, which appends its argument to a log: Qt's
# QDesktopServices on a desktop-less X server goes through xdg-open, so the log is what Shift+Enter
# and the menu actually sent out, not a code read.
#
# Isolated HOME / XDG_* / TMPDIR under a short path, RELAY_KEYRING=off, no provider account.
# Needs Xvfb, xdotool, ImageMagick, tesseract. Each check writes one PASS/FAIL line to notes.txt.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
# Run with the binary land.py built from the exact tree it committed
# (/tmp/claude-1000/land/<me>/verify/build/relay): this checkout's build/relay carries other
# sessions' in-flight edits, so it is not evidence of what landed.
relay=${1:-$root/build/relay}
width=1400 height=900
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 600 699); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-sej2.XXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp" "$sandbox/bin"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
unset XDG_CURRENT_DESKTOP DESKTOP_SESSION KDE_FULL_SESSION GNOME_DESKTOP_SESSION_ID
opened=$sandbox/xdg-open.log
: >"$opened"
printf '#!/bin/sh\necho "$@" >>%s\n' "$opened" >"$sandbox/bin/xdg-open"
chmod +x "$sandbox/bin/xdg-open"
export PATH=$sandbox/bin:$PATH
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf 'first line of the note\n' >"$work/note.txt"
printf '# Readme\n\nhello\n' >"$work/readme.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
CONF

: >"$out/notes.txt"
pass=0 fail=0
note() { echo "$*" >>"$out/notes.txt"; }
ok()   { note "PASS $*"; pass=$((pass+1)); }
bad()  { note "FAIL $*"; fail=$((fail+1)); }
check() { local name=$1; shift; if "$@"; then ok "$name"; else bad "$name"; fi; }

win=
t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 80 "$@"; }
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/$1.png"
    tesseract "$out/$1.png" - --psm 11 2>/dev/null >"$out/$1.txt"
    xdotool mousemove "$X" "$Y"
    xdotool windowfocus "$win" 2>/dev/null
    sleep 0.3
}
words() {   # "word left top width height", read at 2x so the small type is legible
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'NF>=12 && $12 != "" {print $12, int($7/2), int($8/2), int($9/2), int($10/2)}'
}
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) print x, y}'; }
said() { grep -qi -- "$2" "$out/$1.txt"; }       # shot, pattern
click_at() { xdotool mousemove "$1" "$2" click "${3:-1}"; sleep 0.8; }

(cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 10
best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4
# The first launch asks how tools are approved, in a pane of its own. Take the recommendation.
shot _approvals
yes=$(word_xy _approvals "recommend")
[[ -n $yes ]] && click_at ${yes% *} ${yes#* }
sleep 2

phase=${PHASE:-all}
source "$PWD/phases.sh"

note "$pass passed, $fail failed"
cp "$opened" "$out/xdg-open.log"
echo "$pass passed, $fail failed (notes.txt)"
(( fail == 0 ))
