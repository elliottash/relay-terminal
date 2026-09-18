#!/usr/bin/env bash
# The Dark Copper / IBM Beige audition (card #0JA7, owner 2026-09-18): the same window content in
# all six themes, at the same size, then contact sheets that put them side by side.
#
#   docs/qa_evidence/2026-09-18-copper-and-beige-themes/drive.sh [build-dir]
#
# Per theme, one fresh Relay with an isolated profile pinned to that theme (`theme/name`):
#   * a coloured `ls`, a failing `make`, and an agent turn that runs `make`, writes the fix (a real
#     write_file diff) and answers — the provider is stub-provider.py on 127.0.0.1, no network, no
#     real key (the recipe from docs/qa_evidence/2026-09-17-website-update/);
#   * a shell line typed, not sent, into the focused composer, so every syntax colour and the
#     status strip are on screen;                                       -> implementer-<id>-a-session.png
#   * the Switchboard pane on this repository's own cards.             -> implementer-<id>-b-switchboard.png
#
# Needs Xvfb, xdotool, ImageMagick. Captures name the largest window of the relay pid: a root
# capture, or the wrong one of relay's four X windows, comes out black.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880
port=${RELAY_QA_PORT:-8793}
themes=(${RELAY_QA_THEMES:-relay-dark dark-copper gruvbox-dark solarized-dark relay-light ibm-beige})

# A free display: the first of :160-:199 with no socket.
display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# A fixed sandbox so the prompt reads `~/project $`, never a random /tmp path.
sandbox=${RELAY_SHOT_HOME:-/tmp/relay-audition}
stub_pid= xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off

t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }

prepare() {   # prepare <theme-id>: a clean home, a failing project, a profile pinned to the theme
    rm -rf "$sandbox"
    export HOME=$sandbox
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work/src" "$work/build"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf 'greet: greet.c\n\tcc -o greet greet.c\n' >"$work/Makefile"
    printf '#include <stdio.h>\n\nint main(void)\n{\n    printf("hello from relay\\n")\n    return 0;\n}\n' >"$work/greet.c"
    printf '# greet\n' >"$work/README.md"
    printf '#!/bin/sh\necho ok\n' >"$work/run.sh"; chmod +x "$work/run.sh"
    ln -s README.md "$work/NOTES.md"
    : >"$work/build/greet.o"
    # This repository's own cards, so the Switchboard shows real content.
    cp -r "$root/issues" "$work/issues"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[theme]
name=$1
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=glm-5.3
extra={}
max_tokens=1024
CONF
}

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

shot() {   # park the pointer off the window so hover chrome does not appear, then capture
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/implementer-$1.png"
}

for theme in "${themes[@]}"; do
    printf '== %s\n' "$theme"
    prepare "$theme"
    "$build/relay" --workspace "$work" >"$out/relay-stderr-$theme.log" 2>&1 &
    relay_pid=$!
    sleep 7
    largest_window
    [[ -z $win ]] && { echo "no Relay window for $theme"; kill "$relay_pid"; continue; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2

    # The provider dialog needs a non-empty key and the consent box (the stub ignores the key).
    k ctrl+shift+a; sleep 1.5
    t 'provider'; sleep 1.5
    k Down; sleep 0.5
    k Return; sleep 2
    t 'Advanced'; sleep 1.5
    k Return; sleep 3
    k Tab Tab Tab; sleep 0.3
    t 'loopback-stub-not-a-key'; sleep 0.3
    k shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab; sleep 0.5
    k space; sleep 0.5
    k Return; sleep 4

    # The session: coloured output, a failing build, an agent turn with a diff.
    xdotool windowfocus "$win"
    t 'clear'; k Return; sleep 1.5
    t 'ls --color=always -F'; k Return; sleep 2
    t 'make'; k Return; sleep 3
    t 'why does the build fail?'; sleep 0.8
    k Return; sleep 18
    # A shell line in the focused composer, not sent: command, flag, string, path, operator,
    # variable, all at once, beside the status strip.
    t 'grep -n "printf" src/*.c | wc -l && echo $HOME'; sleep 1.5
    shot "$theme-a-session"

    # The Switchboard on this repository's cards. Clear the line first.
    k ctrl+a BackSpace; sleep 0.5
    # 87 cards through the board backend: 5 s still shows "Loading the Switchboard…".
    t '/switchboard'; k Return; sleep 15
    largest_window
    shot "$theme-b-switchboard"

    kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; relay_pid=
done

# ---- contact sheets ------------------------------------------------------------------------------
label() { case $1 in
    relay-dark) echo "Relay Dark (default)";; dark-copper) echo "Dark Copper (new)";;
    gruvbox-dark) echo "Gruvbox Dark";; solarized-dark) echo "Solarized Dark";;
    relay-light) echo "Relay Light";; ibm-beige) echo "IBM Beige (new)";; *) echo "$1";; esac; }
sheet() {   # sheet <out> <tile> <suffix> <theme...>
    local name=$1 tile=$2 suffix=$3; shift 3
    local args=() th
    for th in "$@"; do
        [[ -f $out/implementer-$th-$suffix.png ]] && args+=(-label "$(label "$th")" "$out/implementer-$th-$suffix.png")
    done
    ((${#args[@]})) && montage "${args[@]}" -tile "$tile" -geometry 700x440+12+12 -pointsize 18 \
        -background '#808080' "$out/$name.png"
}
sheet contact-all-six-session 3x2 a-session relay-dark dark-copper gruvbox-dark solarized-dark relay-light ibm-beige
sheet contact-all-six-switchboard 3x2 b-switchboard relay-dark dark-copper gruvbox-dark solarized-dark relay-light ibm-beige
# The two head-to-heads the owner is judging, larger.
for pair in "dark relay-dark dark-copper" "light relay-light ibm-beige"; do
    set -- $pair
    montage -label "$(label "$2")" "$out/implementer-$2-a-session.png" -label "$(label "$3")" "$out/implementer-$3-a-session.png" \
        -tile 2x1 -geometry 1000x629+14+14 -pointsize 22 -background '#808080' "$out/head-to-head-$1.png" 2>/dev/null
done
# The composer and status strip, where the destination pair and syntax live: full size, cropped.
crops=()
for th in "${themes[@]}"; do
    f=$out/implementer-$th-a-session.png
    [[ -f $f ]] || continue
    convert "$f" -gravity south -crop 1400x150+0+0 +repage "$out/crop-$th-composer.png"
    crops+=(-label "$(label "$th")" "$out/crop-$th-composer.png")
done
((${#crops[@]})) && montage "${crops[@]}" -tile 1x -geometry 1400x150+0+10 -pointsize 18 -background '#808080' "$out/contact-composer-and-status-strip.png"
rm -f "$out"/crop-*-composer.png
printf 'done: %s\n' "$out"
