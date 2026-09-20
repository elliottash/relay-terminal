#!/usr/bin/env bash
# Inactive tab headers wear their own theme (owner, 2026-09-20: "can we color the other inactive
# tabs with their respective themes"), and the persistent Randomize behind it (card #R4ND: "i
# meant a persistent mode. it randomizes on each new tab"). Implementer screenshots under Xvfb
# with an isolated profile that has theme/randomize_new_tab=true before first launch:
#
#   docs/qa_evidence/2026-09-20-tab-theme-marks/drive.sh [build-dir]
#
#   implementer-a-first-tab.png    one tab, the default theme: the row carries no marks
#   implementer-b-four-tabs.png    four tabs, each new one a random theme: every inactive tab is
#                                  washed in its own accent with the strip along its bottom edge
#   implementer-c-tab-2-front.png  tab 2 brought to the front: the window rethemes, its mark is
#                                  gone, the others keep theirs
#   implementer-d-random-row.png   Options › Appearance searched for "random": the button, and
#                                  the persistent mode's row below the cycling one
#   implementer-e-mutex.png        the cycling row switched on: the randomize row is off again
#
# tab-row-pixels.txt samples the tab row of b and c, so the tints are checkable without opening
# the images; themes.txt is the relay.conf evidence for the mutual exclusion.
#
# No provider account and no network turn. Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880

display=
for n in $(seq 240 270); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${RELAY_SHOT_HOME:-/tmp/tabmarks-shots}
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off

k() { xdotool key --delay 60 "$@"; }

rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$HOME/run TMPDIR=$HOME/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
approvals_ask=@Invalid()
[theme]
randomize_new_tab=true
CONF

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

shot() {   # the whole screen, so popups and dialogs are in the picture
    sleep 0.8
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3
    import -window root "$out/implementer-$1.png"
}

# Every pixel across the tab row at two heights — the wash height and the strip height — so a
# tint per tab is checkable in text. The row is at the top of the window; the tabs are ~29px
# tall. Prints x:colour runs.
sample_row() {  # sample_row <png> <label>
    local png=$1 label=$2
    echo "== $label ==" >>"$out/tab-row-pixels.txt"
    for y in 14 27; do
        echo "-- y=$y --" >>"$out/tab-row-pixels.txt"
        prev=
        for ((x = 0; x < 900; x += 6)); do
            px=$(convert "$png" -format "%[pixel:p{$x,$y}]" info:)
            [[ $px != "$prev" ]] && { printf 'x=%d %s\n' "$x" "$px" >>"$out/tab-row-pixels.txt"; prev=$px; }
        done
    done
}

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 3

shot a-first-tab
: >"$out/tab-row-pixels.txt"

# Three new tabs: each draws a random theme (never the default, never the previous tab's).
k ctrl+t; sleep 2.5
k ctrl+t; sleep 2.5
k ctrl+t; sleep 2.5
shot b-four-tabs
sample_row "$out/implementer-b-four-tabs.png" "b-four-tabs"

# Bring tab 2 to the front: the window rethemes to it and its mark clears.
k ctrl+shift+Tab; sleep 2
k ctrl+shift+Tab; sleep 2
shot c-tab-2-front
sample_row "$out/implementer-c-tab-2-front.png" "c-tab-2-front"

# Options › Appearance: the persistent mode's row beside the button.
k ctrl+shift+o; sleep 3
k Right; sleep 1.5
xdotool type --delay 80 "random"; sleep 1.5
shot d-random-row

# The mutual exclusion: switch the cycling mode on, and the randomize row goes off. The search is
# "next theme" because it matches the cycling row alone ("new tab" matches its neighbour too).
k Escape; sleep 0.8
xdotool type --delay 80 "next theme"; sleep 1.5
k Down; sleep 0.5
k Return; sleep 2.5
{
    echo "after switching the cycling row on:"
    grep -aE 'new_tab|randomize' "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null
} | tee "$out/themes.txt"
# The page was rebuilt by the toggle; search again so both rows are in the picture.
k Escape; sleep 0.8
xdotool type --delay 80 "new tab"; sleep 1.5
shot e-mutex

echo "shots written to $out"
