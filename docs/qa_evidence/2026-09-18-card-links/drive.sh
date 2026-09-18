#!/usr/bin/env bash
# `#K7Q2` in terminal output is a link to its Switchboard card (owner 2026-09-18).
#
#   docs/qa_evidence/2026-09-18-card-links/drive.sh [build-dir]
#
# One fresh Relay under Xvfb with an isolated profile, on a *throwaway copy* of this
# repository's issues/ tree (the real one is never opened, let alone written):
#
#   1. the shell prints a line of real and unfiled references, a `#` comment and a path.
#      The line comes from a file this script writes, so no `#` is ever typed into the
#      composer, where it would open the card picker instead (design section 5);
#   2. the pointer walks down the rows until the hover underline appears — that is how the
#      script finds the reference without knowing the engine's cell geometry. The comparison
#      is cropped to the row under the pointer, so the blinking cursors elsewhere on screen
#      cannot be mistaken for it;
#   3. shots of the hover (the window, then the root so the tooltip is in frame), the
#      right-click menu, `#ID -> prompt` from it, the keyboard walk (Ctrl+Shift+L), and the
#      Switchboard a plain click opens on that card.
#
# No model call is made: nothing is ever submitted to the agent. The provider is a preset with
# a dummy key from the environment (RELAY_KEYRING=off keeps the run away from the real
# keyring), and every proxy variable points at a dead loopback port, so even an accidental
# request could not leave the machine. The pane needs a *configured* session only because that
# is what lets it ask the worker for the card index.
#
# Needs Xvfb, xdotool, ImageMagick and python3.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880
display=${RELAY_QA_DISPLAY:-:184}

# The cards the run talks about: one real id from the copied tree, and one nobody ever filed.
known=${RELAY_QA_CARD:-YZTK}
unknown=ABCD

sandbox=/tmp/relay-card-links-$$
xvfb_pid= relay_pid=
cleanup() {
    [[ -n $relay_pid ]] && kill "$relay_pid" 2>/dev/null
    [[ -n $xvfb_pid ]] && kill "$xvfb_pid" 2>/dev/null
    rm -rf "$sandbox"
}
trap cleanup EXIT

[[ -e /tmp/.X11-unix/X${display#:} ]] && { echo "display $display is taken"; exit 1; }
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export RELAY_OPENAI_API_KEY=qa-dummy-key-no-call-is-made
export HTTP_PROXY=http://127.0.0.1:1 HTTPS_PROXY=http://127.0.0.1:1 ALL_PROXY=http://127.0.0.1:1
export http_proxy=$HTTP_PROXY https_proxy=$HTTPS_PROXY all_proxy=$ALL_PROXY

export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
# A throwaway copy: the Switchboard in this run reads and could only ever write this one.
cp -r "$root/issues" "$work/issues"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[provider]
preset=openai
max_tokens=256
CONF

# The line under test, printed by the shell. Every `#` comes from this file, never from
# xdotool. The second line repeats the reference so that five characters in six anywhere
# along it belong to one, which is what makes a blind hover land on it.
cat >"$work/recap.sh" <<EOF
#!/bin/sh
echo "recap: #$known shipped; #$unknown was never filed   # $known is the real one"
echo "#$known,#$known,#$known,#$known,#$known,#$known,#$known,#$known,#$known,#$known,#$known,#$known"
EOF
chmod +x "$work/recap.sh"

t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }
park() { xdotool mousemove $((width + 20)) $((height + 20)); }

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 9
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2

# The pane's card index arrives with `board_open`, which it sends the first time anything asks
# about a reference. Typing `#` in the composer is the other way in and is what a user does
# first, so the picker asks for the board here; the text is then deleted again.
t ' #'; sleep 4
k Escape; sleep 0.5
k ctrl+a; k BackSpace; sleep 0.5

t 'clear'; k Return; sleep 1.5
t './recap.sh'; k Return; sleep 2.5
park; sleep 1.2
import -window "$win" "$out/00-output-with-references.png"

# ---- find the reference row, by the underline hovering it draws ---------------------------
python3 - "$out" "$win" <<'PY' | tee "$out/hover-scan.txt"
import os, subprocess, sys, time

out, win = sys.argv[1], sys.argv[2]
base = os.path.join(out, "00-output-with-references.png")
probe, band_a, band_b = ("/tmp/card-links-probe.png", "/tmp/card-links-a.png", "/tmp/card-links-b.png")
x = 300


def band(src, dst, y):
    # Only the row under the pointer: cursors blink elsewhere on screen the whole time.
    subprocess.run(["convert", src, "-crop", f"1400x22+0+{max(0, y - 11)}", "+repage", dst], check=True)


def differing_pixels(y):
    subprocess.run(["xdotool", "mousemove", str(x), str(y)], check=True)
    time.sleep(0.4)
    subprocess.run(["import", "-window", win, probe], check=True)
    band(base, band_a, y)
    band(probe, band_b, y)
    r = subprocess.run(["compare", "-metric", "AE", band_a, band_b, "null:"], capture_output=True, text=True)
    return int(float((r.stderr or "0").split()[0].replace(",", "")))


best, best_y = 0, 0
for y in range(96, 200, 3):
    n = differing_pixels(y)
    print(f"y={y} underlined_pixels={n}")
    if n > best:
        best, best_y = n, y
print(f"BEST y={best_y} underlined_pixels={best}")
open(os.path.join(out, "hover-y.txt"), "w").write(str(best_y))
PY
hover_y=$(cat "$out/hover-y.txt")
echo "hover row at y=$hover_y"
[[ $hover_y -eq 0 ]] && { echo "no row underlined: the references did not become links"; }

# ---- the hover -----------------------------------------------------------------------------
# The window shot is taken before the tooltip pops: a tooltip is a window of its own, and it
# would otherwise leave a black hole in `import -window`. The root shot below has it in frame.
park; sleep 0.5
xdotool mousemove 300 "$hover_y"; sleep 0.45
import -window "$win" "$out/01-hover-underline.png"
convert "$out/01-hover-underline.png" -crop 700x60+0+120 +repage -resize 200% \
    "$out/01b-hover-underline-detail.png"
sleep 2.5
import -window root "$out/02-hover-tooltip.png"

# ---- the right-click menu ------------------------------------------------------------------
xdotool click 3; sleep 2
import -window root "$out/03-right-click-menu.png"

# ---- `#ID -> prompt` from that menu --------------------------------------------------------
# The entries the arrows stop on, in order: Take control, Tasks, Paste, Select all, Open #ID,
# Copy #ID, #ID -> prompt. (Copy is greyed with nothing selected, so it is skipped.)
for _ in 1 2 3 4 5 6 7; do k Down; done
k Return; sleep 2
import -window "$win" "$out/04-reference-in-the-prompt.png"
k ctrl+a; k BackSpace; sleep 0.5

# ---- the keyboard walk ---------------------------------------------------------------------
# The first press lands on the newest link (the prompt's own directory), so it takes a few
# steps back through the output to reach a reference.
park; sleep 0.5
xdotool windowfocus "$win"; sleep 0.8
for _ in 1 2 3; do k ctrl+shift+l; sleep 1.2; done
import -window "$win" "$out/05-keyboard-walk.png"
k Escape; sleep 1

# ---- the plain click: the Switchboard opens in this tab, on that card ----------------------
xdotool mousemove 300 "$hover_y"; sleep 1.5
xdotool click 1; sleep 20
largest_window
park; sleep 1.5
import -window "$win" "$out/06-clicked-card-open.png"

# ---- Ctrl+click from the pane that no longer has the focus ---------------------------------
# The Switchboard has the focus now, so the terminal pane's plain click is disarmed (there, the
# click is what moves the focus); Ctrl+click always follows a link.
xdotool mousemove 300 "$hover_y"; sleep 1
xdotool keydown ctrl click 1; xdotool keyup ctrl; sleep 6
park; sleep 1.5
import -window "$win" "$out/07-ctrl-clicked-from-inactive-pane.png"

printf 'done: %s\n' "$out"
