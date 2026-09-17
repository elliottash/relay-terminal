#!/usr/bin/env bash
# Live check of the conversation list with full-text search (Ctrl+Shift+O) and find-in-view
# (Ctrl+F), under Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-17-conversation-search/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script plus relay-stderr.log. Needs Xvfb, xdotool and
# ImageMagick `import`. An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the real
# profile, so no provider key and no real conversation index is touched. The agent talks to
# stub-provider.py on 127.0.0.1 only: no network, no keys anywhere in this run.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:93
port=8731

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
# No desktop keyring, and no conversation index outside this run.
export RELAY_KEYRING=off
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=relay-qa-stub
extra={}
max_tokens=1024
CONF
trap 'kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

python3 "$out/stub-provider.py" "$port" &
stub_pid=$!
sleep 1

Xvfb "$display" -screen 0 1500x950x24 >/dev/null 2>&1 &
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
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 950
xdotool windowfocus "$win"
xdotool mousemove 700 400
sleep 2
shot 01-startup

# ---- point the pane at the loopback stub (palette -> Provider / BYOK…) --------------------
# The settings file already holds the base URL and model. The dialog still needs a non-empty API
# key (an empty one means "use the keyring", and this run has no keyring) and the consent box.
# The placeholder below is not a credential: the stub ignores the Authorization header.
# Tab forward to the key field, then back past the preset combo so the wrap lands on the consent
# box without walking through the "Extra request JSON" editor, which would swallow Tab.
k ctrl+shift+a; sleep 1
t 'Provider'; sleep 1
k Return; sleep 2
shot 02-provider-dialog
k Tab Tab Tab; sleep 0.3
t 'loopback-stub-not-a-key'; sleep 0.3
k shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab; sleep 0.5
k space; sleep 0.5
shot 03-provider-consent-ticked
k Return; sleep 4
shot 04-agent-ready

# ---- two short conversations and one terminal command ------------------------------------
xdotool windowfocus "$win"
t 'remember the word pelican for later'; sleep 0.5
k ctrl+Return; sleep 4
shot 05-first-turn

t 'and what about aardvarks in the basement'; sleep 0.5
k ctrl+Return; sleep 4
shot 06-second-turn

# A second conversation, so the list has more than one row.
t '/new'; k Return; sleep 2
t 'tell me about capybaras please'; sleep 0.5
k ctrl+Return; sleep 4
shot 07-second-conversation

# A terminal command run through the prompt box: Relay knows the line and its exit status.
t 'echo capybara-in-the-terminal'; sleep 0.5
k ctrl+shift+Return; sleep 4
shot 08-terminal-command

# ---- Ctrl+Shift+O: search a word from a past agent turn ----------------------------------
k ctrl+shift+o; sleep 2
shot 09-conversation-list
t 'pelican'; sleep 3
shot 10-search-pelican-agent-turn
k Return; sleep 4
shot 11-resumed-in-this-pane

# ---- Ctrl+Shift+O: search a word from a past terminal command ----------------------------
k ctrl+shift+o; sleep 2
t 'capybara'; sleep 3
# The newest row is the terminal history: the command line, its exit status and its output.
shot 12-search-capybara-terminal-history
k Down; sleep 2
shot 13-search-capybara-agent-conversation
k Escape; sleep 1

# ---- Ctrl+F: find in this pane ------------------------------------------------------------
xdotool windowfocus "$win"; sleep 1
k ctrl+f; sleep 1
t 'pelican'; sleep 2
shot 14-find-in-view
k Return; sleep 1
shot 15-find-next-match
k Escape; sleep 1
shot 16-find-closed

echo "screenshots in $out"
