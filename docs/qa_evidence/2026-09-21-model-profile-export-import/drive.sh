#!/usr/bin/env bash
# Exporting and importing a model profile (owner, 2026-09-21: "allow exporting and importing
# profiles"). Options › Models › profiles, under Xvfb with an isolated profile: make a profile,
# export it to a file, delete it, import the file back.
#
#   docs/qa_evidence/2026-09-21-model-profile-export-import/drive.sh [build-dir]
#
#   0-models-page.png     Options › Models, opened with /models
#   0b-fill-the-lists.png  the "fill the lists" row, whose defaults give the profile something to name
#   1-profiles-search.png  the profiles group with no profile yet: the choice row and "profiles file"
#                          with import… (export all… appears once there is more than one profile)
#   a-profiles-group.png   a profile current: rename… / export… / delete
#   b-export-dialog.png    the save dialog export… opens
#   c-deleted.png          the profile deleted, the way a second machine has never had it
#   d-imported.png         import… read the file back: "Imported “AI work”"
#   e-listed.png           the choice box: imported, listed, and not switched to
#   f-switched.png         switched onto it
#   "AI work.json"         the exported file itself; exporting again after the import gives a file
#                          identical to it but for the timestamp
#
# No provider account and no network turn: two literal non-key strings make two presets "stored",
# and nothing is ever submitted. Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880

display=
for n in $(seq 231 260); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# Short paths: the XDG runtime dir holds sockets, and a sun_path is 108 bytes (see the
# xvfb-test-isolation note). Isolating it keeps this run out of the owner's live session.
sandbox=${RELAY_SHOT_HOME:-/tmp/relay-prof-shots}
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-either

k() { xdotool key --delay 60 "$@"; }

rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[provider]
preset=glm-coding
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
approvals_ask=@Invalid()
CONF

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

shot() { sleep 0.8; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3; import -window root "$out/$1.png"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 3

# Options › Models, then the search box to jump straight at the profiles group.
xdotool mousemove 300 812 click 1; sleep 0.6
xdotool type --delay 60 "/models"; sleep 0.5; k Return; sleep 4
shot 0-models-page

# Fill the five lists, so the profile that follows names something.
xdotool mousemove 1040 110 click 1; sleep 0.5
xdotool type --delay 60 "fill the lists"; sleep 2
shot 0b-fill-the-lists
xdotool mousemove 769 248 click 1; sleep 2

# The search box jumps to the profiles group without counting Page_Downs.
xdotool mousemove 1040 110 click 1; sleep 0.4
k ctrl+a; xdotool type --delay 60 "profile"; sleep 2
shot 1-profiles-search

# Make a profile out of the lists as they are: the choice box's "new profile…".
xdotool mousemove 1287 215 click 1; sleep 1
k Down Return; sleep 1.5
xdotool type --delay 60 "AI work"; sleep 0.5
k Return; sleep 2
shot a-profiles-group

# export… → the save dialog, then write it into this evidence directory.
xdotool mousemove 1241 402 click 1; sleep 2
shot b-export-dialog
k ctrl+a; xdotool type --delay 20 "$out/AI work.json"; sleep 0.5
k Return; sleep 2

# Delete it, the way a second machine has never had it, then bring the file back.
xdotool mousemove 1327 402 click 1; sleep 1.5
k Return; sleep 2
shot c-deleted
xdotool mousemove 1319 280 click 1; sleep 2
k ctrl+a; xdotool type --delay 20 "$out/AI work.json"; sleep 0.5
k Return; sleep 2
shot d-imported
k Return; sleep 1.5
# It is listed but not switched to: importing does not change what this machine runs on.
xdotool mousemove 1287 215 click 1; sleep 1.2
shot e-listed
k Down Return; sleep 2
shot f-switched

# Export it again from the imported copy: same lists, same levels — the round trip is lossless.
xdotool mousemove 1241 402 click 1; sleep 2
k ctrl+a; xdotool type --delay 20 "$out/AI work (reimported).json"; sleep 0.5
k Return; sleep 2

echo "shots written to $out"
