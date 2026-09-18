#!/usr/bin/env bash
# #T9ZS — does the shortcuts overlay open from the keyboard?
#
#   docs/qa_evidence/2026-09-17-bugfix-batch1/shortcuts.sh [preset] [build-dir]
#   RELAY_QA_DISPLAY=:91 docs/qa_evidence/2026-09-17-bugfix-batch1/shortcuts.sh warp
#
# Presses, in order, the combinations a "Ctrl+?" binding can arrive as on a real keyboard —
# Ctrl+Shift+/ (what Ctrl+? is on the owner's layout), Ctrl+/, Ctrl+Shift+? and the two keypad
# variants — plus F1, shooting the window after each one. Escape closes the overlay in between,
# so a screenshot showing the overlay means that key, and only that key, opened it.
#
# There is no window manager on the Xvfb display, so X focus is PointerRoot: the pointer is
# parked over the Relay window and keys go there. Windows are captured by id (a root-window
# capture is black now that Relay draws its own frame).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
preset=${1:-relay}
build=${2:-$root/build}
display=${RELAY_QA_DISPLAY:-:91}
width=1400 height=900

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export XDG_RUNTIME_DIR=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal/relay"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
printf '{"version":1,"preset":"%s","bindings":{}}\n' "$preset" >"$XDG_CONFIG_HOME/RelayTerminal/relay/keybindings.json"
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$work"' EXIT

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display

"$build/relay" --workspace "$work" >"$out/$preset-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool mousemove 700 450   # PointerRoot focus
sleep 2

# The overlay is a modal dialog, so it is a top-level window of its own: it shows up as a second
# "Keyboard shortcuts" window, and it is that window that has to be captured.
overlay() { xdotool search --pid "$relay_pid" --name '^Keyboard shortcuts$' 2>/dev/null | tail -1; }

try() {   # try <name> <xdotool key>
    xdotool key --delay 60 "$2"
    sleep 2
    local dialog
    dialog=$(overlay)
    if [[ -n $dialog ]]; then
        import -window "$dialog" "$out/$preset-$1.png"
        printf '%-22s %-24s OPENED the overlay\n' "$2" "$1"
    else
        import -window "$win" "$out/$preset-$1-no-overlay.png"
        printf '%-22s %-24s no overlay\n' "$2" "$1"
    fi
    xdotool key --delay 60 Escape
    sleep 1.5
}

echo "=== preset: $preset ==="
try 01-ctrl-shift-slash    ctrl+shift+slash
try 02-ctrl-slash          ctrl+slash
try 03-ctrl-shift-question ctrl+shift+question
try 04-keypad-ctrl-shift   ctrl+shift+KP_Divide
try 05-keypad-ctrl         ctrl+KP_Divide
try 06-f1                  F1
# The palette entry must keep working: open the palette, type "shortcut", take the first row.
xdotool key --delay 60 ctrl+shift+a; sleep 2
xdotool type --delay 20 'Keyboard shortcuts'; sleep 1.5
xdotool key --delay 60 Return; sleep 2.5
dialog=$(overlay)
if [[ -n $dialog ]]; then
    import -window "$dialog" "$out/$preset-07-from-the-palette.png"
    echo "palette                07-from-the-palette      OPENED the overlay"
else
    import -window "$win" "$out/$preset-07-from-the-palette-no-overlay.png"
    echo "palette                07-from-the-palette      no overlay"
fi
xdotool key --delay 60 Escape; sleep 1
# The ? help card names the keys that work.
xdotool key --delay 60 Escape; sleep 1
xdotool key --delay 60 question; sleep 2
import -window "$win" "$out/$preset-08-help-card.png"
echo "screenshots in $out"
