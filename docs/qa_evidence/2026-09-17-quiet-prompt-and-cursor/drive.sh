#!/usr/bin/env bash
# Live check of the quieter terminal prompt, under Xvfb + xdotool:
#   - the shell prompt ends in a newline, so what you type sits below the folder line
#   - the engine draws no hollow cursor outline while the pane is unfocused
#
#   docs/qa_evidence/2026-09-17-quiet-prompt-and-cursor/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script plus relay-stderr.log. Needs Xvfb, xdotool and
# ImageMagick `import`. An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the real
# profile. No provider is configured and no agent turn runs: this is terminal-only, no network.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:94

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export RELAY_KEYRING=off
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

Xvfb "$display" -screen 0 1400x800x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() {
    import -window root "$out/implementer-$1.png"
    kill -0 "${relay_pid:-0}" 2>/dev/null || echo "relay is gone before $1"
}
t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 6
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1400 800
xdotool windowfocus "$win"
xdotool mousemove 700 400
sleep 3

# 01: the idle prompt. The folder line stands alone, with no hollow cursor square after the "$".
shot 01-idle-prompt

# 02: a terminal command typed in the prompt box lands on its own line under the folder.
t 'echo one; echo two'; sleep 1
k ctrl+shift+Return; sleep 4
shot 02-command-below-folder

echo "wrote $out/implementer-*.png"
