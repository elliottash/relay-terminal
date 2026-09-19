#!/usr/bin/env bash
# Live check of the per-page "Reset to defaults" row under Xvfb, with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider: the Options pane needs none.
set -uo pipefail
out=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$out"
root=$(cd "$out/../../.." && pwd)
build=${1:-$root/build}
width=1280 height=860

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-reset.XXXXXX)
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
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }

export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
mkdir -p "$HOME" "$sandbox/run" "$sandbox/tmp" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" \
         "$XDG_CACHE_HOME" "$HOME/project"
chmod 700 "$sandbox/run"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

# Everything below is away from what Relay ships with: two General toggles, the log level, the
# window-restore toggle, one Terminal toggle and one Agent number (another page, which must survive).
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-light
[agent]
show_thinking=false
show_tool_output=true
max_steps=17
[hints]
enabled=false
[notifications]
desktop=false
[logging]
level=debug
[windows]
restore=false
[terminal]
copy_on_select=true
CONF
cp "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" "$out/relay.conf.before"

"$build/relay" --workspace "$HOME/project" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$out/relay-stderr.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2

shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6; import -window "$win" "$out/implementer-$1.png"; }

k ctrl+comma; sleep 2.5                 # Options
shot a-general-top
k Up; sleep 1.2                         # nothing highlighted yet: the last row on the page
shot b-reset-row-highlighted
k Return; sleep 2                       # the confirmation
dialog=$(xdotool search --name '^Reset General$' | head -1)
echo "dialog window: ${dialog:-none}"
import -window root "$out/implementer-c-confirmation.png"
xdotool windowactivate "$dialog"; sleep 0.5
k Escape; sleep 1.5                     # Cancel is the escape button
shot d-cancelled
cp "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" "$out/relay.conf.after-cancel"

# The mouse path this time: click the row's own button, then the dialog's Reset.
xdotool mousemove 1203 733 click 1; sleep 2
import -window root "$out/implementer-c2-confirmation-again.png"
xdotool mousemove 682 442; sleep 0.4; xdotool click 1; sleep 2.5
import -window root "$out/implementer-e-after-reset-root.png"
shot e-after-reset
cp "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" "$out/relay.conf.after-reset"

# With no window manager under Xvfb nothing hands the keyboard back when a modal closes, so the
# search box is clicked once before the keyboard is used again.
xdotool mousemove 958 110; sleep 0.3; xdotool click 1; sleep 1
k Right; sleep 1.5                      # Appearance, whose own button is still there
shot f-appearance
k Right Right Right; sleep 1.5          # Models → Local models → Terminal
shot g-terminal
k Right Right Right Right; sleep 1.5    # Agent → Voice → Privacy → Keyboard
shot h-keyboard
k Left Left Left Left Left; sleep 1.5   # back to Local models, which has nothing to put back
shot i-local-models
kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; relay_pid=
echo "done: $out"
