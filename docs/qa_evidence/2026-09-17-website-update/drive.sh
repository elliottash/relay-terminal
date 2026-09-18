#!/usr/bin/env bash
# Screenshots for the relay-terminal.ai landing page, taken from the current build under Xvfb.
#
#   docs/qa_evidence/2026-09-17-website-update/drive.sh [build-dir]
#
# Everything the shots could leak is isolated: a temporary HOME (so every path renders as
# `~/project`), temporary XDG dirs, `RELAY_KEYRING=off`, a neutral `PS1`, and an agent that talks
# only to stub-provider.py on 127.0.0.1 — no network, no account, no real key. The window is
# captured by id, not by root: Relay draws its own frame, so a root capture comes back black
# without a window manager.
#
# Writes shot-NN-*.png next to this script plus relay-stderr.log. Needs Xvfb, xdotool and
# ImageMagick (`import`, `convert`).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
port=8791
width=1320 height=860

[[ -x $build/relay ]] || { echo "no $build/relay — build first"; exit 1; }

# Pick a display nobody else holds. Several QA runs share this machine and a taken display is the
# dangerous case: Xvfb exits, the app lands on someone else's X server and xdotool types into
# their window. Refuse to drive a display we did not create.
display=
for n in $(seq 160 199); do
    [[ -e /tmp/.X11-unix/X$n ]] && continue
    display=:$n
    break
done
[[ -z $display ]] && { echo "no free X display in 160..199"; exit 1; }

# A fixed, neutral sandbox: the conversation window prints the workspace path in full, and
# `mktemp -d` would put a random /tmp/tmp.XXXXXXXX in the screenshots. Nothing here is personal.
sandbox=${RELAY_SHOT_HOME:-/tmp/relay-demo}
[[ -e $sandbox ]] && { echo "$sandbox already exists; remove it or set RELAY_SHOT_HOME"; exit 1; }
mkdir -p "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export RELAY_KEYRING=off
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"

# A neutral prompt: no user, no host, and `~/project` for the directory.
cat >"$HOME/.bashrc" <<'RC'
PS1='\w $ '
unset PROMPT_COMMAND
RC

# A tiny project whose build really fails, so the agent turn in the hero shot is a real one.
cat >"$work/Makefile" <<'MK'
greet: greet.c
	cc -o greet greet.c
MK
cat >"$work/greet.c" <<'SRC'
#include <stdio.h>

int main(void)
{
    printf("hello from relay\n")
    return 0;
}
SRC
cat >"$work/README.md" <<'MD'
# greet

A one-file demo project used for Relay's website screenshots.

- `make` builds `greet`
- `./greet` prints one line
MD

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=glm-5.3
extra={}
max_tokens=1024
CONF

trap 'kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$sandbox"' EXIT

python3 "$out/stub-provider.py" "$port" &
stub_pid=$!
sleep 1

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display (taken?)"; exit 1; }
export DISPLAY=$display
xdotool search --name . 2>/dev/null | grep -q . && { echo "$display already has windows"; exit 1; }

win=
shot() {   # shot <name> [scale-width]
    windows "$1"
    # Hover chrome (the pane's ⠿ / split / close row) only shows under the pointer; park it off
    # the window so every shot is the resting UI.
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/shot-$1.png"
    [[ -n ${2:-} ]] && convert "$out/shot-$1.png" -resize "$2" "$out/shot-$1.png"
    kill -0 "${relay_pid:-0}" 2>/dev/null || echo "relay is gone before $1"
}
t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
# Relay owns four X windows (the main one, a 1x1 helper, an 8x19 stub and Qt's selection owner)
# and `--name Relay` matches the stub too, so pick the largest. Capturing the wrong one is what
# produces an all-black PNG, not the missing window manager.
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; exit 1; }

windows() {   # log every mapped window, so a stray dialog over the shot is obvious
    { echo "--- windows before $1"
      for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
          echo "  $w $(xdotool getwindowgeometry --shell "$w" 2>/dev/null | tr '\n' ' ') '$(xdotool getwindowname "$w" 2>/dev/null)'"
      done; } >>"$out/windows.log"
}
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"
xdotool mousemove $((width / 2)) $((height / 2))
sleep 2

# ---- point the pane at the loopback stub (palette -> Provider / BYOK…) ------------------------
# The settings file holds the base URL and model; the dialog still needs a non-empty key (an empty
# one means "use the keyring", and this run has none) and the consent box. The placeholder is not a
# credential: the stub ignores the Authorization header.
# Palette: "provider" -> the Settings › Models submenu (second row) -> "Advanced provider settings".
k ctrl+shift+a; sleep 1.5
t 'provider'; sleep 1.5
k Down; sleep 0.5
k Return; sleep 2
t 'Advanced'; sleep 1.5
k Return; sleep 3
# In the dialog the focus starts on Preset. Tab x3 reaches the API key; from there Tab would land
# in the "Extra request JSON" editor, which swallows it, so walk backwards: the chain wraps to
# Cancel, Save, then the consent box.
k Tab Tab Tab; sleep 0.3
t 'loopback-stub-not-a-key'; sleep 0.3
k shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab; sleep 0.5
k space; sleep 0.5
k Return; sleep 5

# ---- hero: a command, then a request, in one pane --------------------------------------------
# Debian's /etc/bash.bashrc is read even with --rcfile, so the pane opens on its sudo notice.
xdotool windowfocus "$win"
t 'clear'; k Return; sleep 2
t 'make'; k Return; sleep 3
t 'why does the build fail?'; sleep 0.8
k Return; sleep 18
shot 01-agent-inline

# ---- split panes ------------------------------------------------------------------------------
# Ctrl+E is "new pane to the right", Ctrl+Alt+E "new pane below" (Keymap defaults, src/main.cpp).
# A new pane has no provider of its own, so keep to commands the router resolves locally: anything
# it cannot resolve goes to the agent and opens the BYOK dialog over the shot.
k ctrl+e; sleep 8
xdotool windowfocus "$win"
t 'clear'; k Return; sleep 2
t 'make'; k Return; sleep 4
t './greet'; k Return; sleep 3
k ctrl+alt+e; sleep 8
xdotool windowfocus "$win"
t 'clear'; k Return; sleep 2
t 'cat README.md'; k Return; sleep 3
shot 02-splits

# ---- folder explorer and file preview ---------------------------------------------------------
# `relay` is a shell function, so this stays in the terminal; it opens a preview pane beside it.
t 'relay open README.md'; k Return; sleep 5
shot 03-file-panes

# ---- actions palette (a sidebar inside the window) ---------------------------------------------
k ctrl+shift+a; sleep 2.5
shot 04-palette
k Escape; sleep 1.5

# ---- conversation search (its own top-level window, so capture that window) --------------------
xdotool windowfocus "$win"
k ctrl+shift+o; sleep 4
t 'greet'; sleep 2.5
dialog=
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    [[ $(xdotool getwindowname "$w" 2>/dev/null) == *onversation* ]] && dialog=$w
done
if [[ -n $dialog ]]; then
    windows 05-conversations
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$dialog" "$out/shot-05-conversations.png"
else
    echo "conversation window not found"
fi
k Escape; sleep 1

echo "wrote $out/shot-*.png on $display"
