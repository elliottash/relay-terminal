#!/usr/bin/env bash
# Card #MDL1 — the list Alt+M and Alt+E drop open is measured one row shorter than it is drawn.
#
# Reported twice on 2026-09-21 (docs/qa_evidence/2026-09-21-effort-by-model/NOTES.md): the last row
# of a four-level Alt+E list clipped against the frame, and a list that got *shorter* between two
# opens — four levels then three — drawn scrolled past its first row, persistently.
#
# This drives both, before and after the fix, from the same script. Two binaries, both built from a
# clean `git archive main` export (the checkout's own build/ was held by another session's
# in-flight edit): BEFORE is main as it stands, AFTER is main plus src/FilterPopup.{h,cpp}.
#
#     RELAY_BIN=/path/to/relay TAG=before ./drive.sh
#     RELAY_BIN=/path/to/relay TAG=after  ./drive.sh
#
# Xvfb on a free display in 760..790, an isolated HOME/XDG_*/TMPDIR under a short path (the
# 108-byte unix-socket limit), RELAY_KEYRING=off so the owner's real identity key is never touched,
# `isolation/enabled=false` because the worker exits under a fake XDG_RUNTIME_DIR, and fake
# provider keys. No turn is ever sent: every step is a model switch or a list opening, which the
# worker answers by itself.
#
# Every list is shot closed and then open, and its window geometry is written beside the open shot:
# measure.py counts the bands of ink inside that rectangle and compares them with the rows the list
# holds. The geometry is asked of X rather than found in the pixels — see popupgeom below.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-popup-sizing
tag=${TAG:-after}
bin=${RELAY_BIN:?set RELAY_BIN to the relay binary to drive}
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 760 790); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 760..790"; exit 1; }
echo "display $display, binary $bin, tag $tag"

xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off

k() { xdotool key --delay 70 "$@"; }
t() { xdotool type --delay 35 "$1"; }
# The pointer is parked off the window before every shot: the row under it takes the highlight.
shot() { sleep "${2:-0.9}"; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.35; import -window root "$out/$tag-$1.png"; }
crop() { convert "$out/$tag-$1.png" -crop "${width}x470+0+$((height - 450))" +repage "$out/$tag-$1-box.png"; }
say() { xdotool mousemove 400 $((height - 70)) click 1; sleep 0.4; t "$1"; sleep 0.5; k Return; sleep "${2:-3}"; }
# The popup's geometry, exactly, rather than guessed out of the pixels: over the composer strip the
# popup's ground *is* the strip's ground, so neither a diff of the closed and open shots nor a hunt
# for its border can say where one ends and the other begins. It is an override-redirect window
# with no title, but it is mapped and it carries the application's WM_CLASS, so it is the one
# visible `relay` window that is not the main one. (Its id is *reused* between opens — the popup is
# one widget, shown and hidden — which is why "the window that appeared" does not identify it.)
popupgeom() {   # $1 name
    local w
    : > "$out/$tag-$1-popup.txt"
    for w in $(xdotool search --onlyvisible --class relay 2>/dev/null); do
        [[ $w == "$win" ]] && continue
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)" || continue
        (( WIDTH >= 60 && HEIGHT >= 40 )) || continue
        echo "$w ${WIDTH}x${HEIGHT}+${X}+${Y}" >> "$out/$tag-$1-popup.txt"
        # The popup alone, three times life size: at this scale a shaved descender is plain
        # ("xhigh" reads "xhiah" in before-a-alt-e-four-zoom.png).
        convert "$out/$tag-$1-open.png" -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" +repage \
                -scale 300% "$out/$tag-$1-zoom.png"
    done
    xwininfo -root -children > "$out/$tag-$1-windows.txt"
}
# closed shot (the screen before the list), key, open shot, the popup's geometry, crop.
pair() {   # $1 name  $2 key
    shot "$1-closed" 0.8
    xdotool mousemove 400 $((height - 70)) click 1; sleep 0.5
    k "$2"; sleep 1.8
    shot "$1-open" 0.9
    popupgeom "$1"
    crop "$1-open"
}

sandbox=/tmp/claude-1000/pz$tag
rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
         "$XDG_RUNTIME_DIR" "$TMPDIR" "$HOME/project" "$sandbox/bin"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$HOME/project/README.md"
export PATH=$sandbox/bin:$PATH
unset PYTHONPATH
export RELAY_OPENAI_API_KEY=xvfb-not-a-real-key
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-two
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key-either

conf=$XDG_CONFIG_HOME/RelayTerminal/relay.conf
cat >"$conf" <<CONF
[instructions]
onboarded=true

[isolation]
enabled=false

[agent]
effort=high

[models]
tier\\main=openai|gpt-6-astra|, kimi-code|k3|, glm-coding|glm-5.3|
tier\\high=openai|gpt-6-astra|xhigh
tier\\flash=glm-coding|glm-5.3-flash|low
tier\\lite=glm-coding|glm-5.3-flash|low
tier\\local=@Invalid()

[suggestions]
next_command=false
next_prompt=false

[security]
approvals_chosen=true
approvals_ask=@Invalid()

[url_handler]
announced=true
CONF

"$bin" --workspace "$HOME/project" >>"$out/$tag-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 16
win= best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 5

# --- a. Alt+E on the OpenAI row: four levels, low medium high xhigh ------------------------------
# The pane starts on rank 1 of main, gpt-6-astra, whose four levels include xhigh. The last of them
# is what used to be clipped against the bottom of the frame.
pair a-alt-e-four alt+e
k Escape; sleep 1

# --- b. the same box on kimi: three levels, the list that got SHORTER between two opens ----------
# The reported sequence exactly: the level is put on the *last* of openai's four, so that the
# switch to kimi — which stops at max — snaps it onto the last of kimi's three. A list whose
# current row is its last one has to scroll to reach it, and with the height a row short it drew
# "high" and "max" with "low" off the top. A second Alt+E (step c) drew the same two.
say "/effort xhigh" 2
say "/model kimi-k3" 4
pair b-alt-e-three alt+e
k Escape; sleep 1

# --- c. and again, to show it is not a first-draw transient --------------------------------------
pair c-alt-e-three-again alt+e
k Escape; sleep 1

# --- d. Alt+M: the model list, with its class headers, a separator and "more models…" ------------
# Eight rows in this profile. Its last row is the one that used to sit hard against the frame.
pair d-alt-m-long alt+m

# --- e. a short filter typed at it: the list shrinks to the four rows "glm" leaves ---------------
t "glm"; sleep 1.5
shot e-alt-m-filtered-open 0.9
popupgeom e-alt-m-filtered
crop e-alt-m-filtered-open
k Escape; sleep 1

cp "$conf" "$out/$tag-conf.txt"
kill "$relay_pid" 2>/dev/null; sleep 3; relay_pid=
echo "shots in $out with the prefix $tag-"
