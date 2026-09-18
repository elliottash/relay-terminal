#!/usr/bin/env bash
# Colour themes (issue 0JA7), live under Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-17-color-themes/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script plus implementer-relay-stderr.log.
# Needs Xvfb, xdotool and ImageMagick `import`. XDG_CONFIG_HOME, XDG_DATA_HOME and
# XDG_CACHE_HOME are isolated, so the run never touches the real profile and the generated
# Konsole profiles land in a throwaway cache.
#
# ONE Relay process runs for the whole script. Every theme below is switched inside it, so the
# screenshots are the evidence that no restart is involved: 01 is Relay Dark, 05-08 are the other
# three, and 19 is Relay Dark again, all from the same pid.
#
# Layout the coordinates assume: the Relay window at 0,0 sized 1600x1000, and the Settings window
# moved to 700,60 (820x560). Section rows are 30px apart starting at y=76; the Theme combo is the
# first row of the Appearance page at (1388, 183).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:95

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$work/src"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

echo "hello from the colour-theme QA run" >"$work/README.md"
printf 'int main() { return 0; }  /* theme */\n' >"$work/src/main.c"

Xvfb "$display" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window root "$out/implementer-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
# No window manager, so pick Relay's real window by size (the 8x19 helper window shares the name).
mainwin() {
  local m=""
  for id in $(xdotool search --pid "$relay_pid"); do
    local w
    w=$(xdotool getwindowgeometry --shell "$id" | sed -n 's/^WIDTH=//p')
    [[ ${w:-0} -gt 400 ]] && m=$id
  done
  echo "$m"
}

"$build/relay" --workspace "$work" >"$out/implementer-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 14
win=$(mainwin)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1600 1000 windowfocus "$win"
sleep 3

# --- terminal output and a half-typed shell line, so every screenshot carries the ANSI palette
# and the composer's syntax colours as well as the chrome. -------------------------------------
xdotool mousemove 700 930 click 1; sleep 1
t '!ls --color=always -la /usr/share/zoneinfo | head -10'
sleep 0.5; k Return; sleep 3
xdotool mousemove 700 930 click 1; sleep 0.5
t '!grep -rn "theme" ./src/*.c | sort > /tmp/o.txt && echo $HOME --force'
sleep 1.5
shot 01-relay-dark-start

# --- the picker ---------------------------------------------------------------------------------
k ctrl+comma; sleep 4
settings=$(xdotool search --name "Settings" | tail -1)
[[ -z $settings ]] && { echo "no Settings window"; exit 1; }
xdotool windowmove "$settings" 700 60; sleep 1.5
shot 02-settings-general
xdotool mousemove 740 106 click 1; sleep 1.5      # the Appearance section
shot 03-appearance-section
xdotool mousemove 1388 183 click 1; sleep 1.5     # open the Theme combo
shot 04-theme-list-open
k Escape; sleep 1

# --- switch to each built-in theme, in this same process ----------------------------------------
# The list is sorted by theme id: gruvbox-dark, relay-dark, relay-light, solarized-dark.
choose() {   # choose <index from the top> <shot name>
  xdotool mousemove 1388 183 click 1
  sleep 1.2
  k Home; sleep 0.4
  local i
  for ((i = 0; i < $1; i++)); do k Down; sleep 0.25; done
  k Return
  sleep 3
  shot "$2"
}

choose 0 05-gruvbox-dark
choose 2 06-relay-light
choose 3 07-solarized-dark
choose 1 08-relay-dark-again

# --- the light theme, on every surface ------------------------------------------------------------
choose 2 09-relay-light-selected
k Escape; sleep 1.5
xdotool windowfocus "$win"; sleep 1
shot 10-light-main-window

k ctrl+shift+a; sleep 2
shot 11-light-palette
t 'theme'; sleep 1.5
shot 12-light-palette-filtered
k Escape; sleep 0.6; k Escape; sleep 0.6; k Escape; sleep 1

# A second tab and a split pane: the tab bar, two pane frames and the focused-pane outline.
k ctrl+t; sleep 5
k ctrl+e; sleep 6
shot 13-light-tabs-and-split

# The notification centre behind the bell in the window header.
xdotool mousemove 1455 27 click 1; sleep 1.5
shot 13b-light-notifications
k Escape; sleep 1

# A toast: Settings > General > Reset shortcut hints.
k ctrl+comma; sleep 3
settings=$(xdotool search --name "Settings" | tail -1)
xdotool windowmove "$settings" 700 60; sleep 1.2
xdotool mousemove 740 76 click 1; sleep 1.2       # the General section
xdotool mousemove 1470 256 click 1; sleep 1.5     # Reset
shot 14-light-toast-and-settings
k Escape; sleep 1.5

# A menu over the terminal.
xdotool windowfocus "$win"; sleep 1
xdotool mousemove 400 400 click 3; sleep 1.5
shot 15-light-context-menu
k Escape; sleep 1

# A dialog and a file pane, both reached from the actions palette.
k ctrl+shift+a; sleep 1.5
t 'shortcuts'; sleep 1.5
k Return; sleep 3
shot 16-light-shortcuts-dialog
k Escape; sleep 1.5

xdotool windowfocus "$win"; sleep 1
k ctrl+shift+a; sleep 1.5
t 'explorer'; sleep 1.5
k Return; sleep 4
shot 17-light-file-pane

# --- back to dark, in the same process ------------------------------------------------------------
k ctrl+comma; sleep 3
settings=$(xdotool search --name "Settings" | tail -1)
xdotool windowmove "$settings" 700 60; sleep 1.2
xdotool mousemove 740 106 click 1; sleep 1.2
choose 1 18-back-to-relay-dark
k Escape; sleep 1.5
xdotool windowfocus "$win"; sleep 1
shot 19-dark-again-no-restart

kill -0 "$relay_pid" 2>/dev/null && echo "relay pid $relay_pid never restarted" || echo "WARNING: relay exited"
xdotool getdisplaygeometry >/dev/null 2>&1 || echo "WARNING: Xvfb died during the run; re-run."
echo "screenshots in $out"
grep -ci 'traceback\|Exception' "$out/implementer-relay-stderr.log" && echo "NOTE: check the stderr log"
