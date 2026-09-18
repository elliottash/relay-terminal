#!/usr/bin/env bash
# Live check of model-written pane titles and tab labels (issue JRWQ), under Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-17-pane-title-summary/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script, plus relay-stderr.log, calls.log (which side
# calls the worker actually made) and cadence.txt. Needs Xvfb, xdotool and ImageMagick `import`.
# An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the real profile, so no provider
# key and no real conversation index is touched. The agent talks to stub-provider.py on 127.0.0.1
# only: no network, no keys anywhere in this run. For the same screens against a real model, see
# drive-live.sh.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:94
port=8747

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
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
titles() { grep -c ' title$' "$out/calls.log"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 6
win=$(xdotool search --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 950
xdotool windowfocus "$win"
xdotool mousemove 700 700
sleep 2
shot 01-startup-header-is-the-directory

# ---- first prompt: with no provider configured the BYOK dialog opens over it ---------------
# The settings file already holds the base URL and model; the dialog still needs a non-empty key
# (an empty one means "use the keyring", and this run has no keyring) and the consent box. The
# placeholder below is not a credential: the stub ignores the Authorization header. Tab forward to
# the key field, then back past the preset combo so the wrap lands on the consent box without
# walking through the "Extra request JSON" editor, which would swallow Tab.
t 'have a look at how pane drag picks its drop target'; sleep 0.5
k ctrl+Return; sleep 3
shot 02-provider-dialog
k Tab Tab Tab; sleep 0.4
t 'loopback-stub-not-a-key'; sleep 0.4
k shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab; sleep 0.5
k space; sleep 0.5
k Return; sleep 5
shot 03-agent-ready

# ---- first turn: the header stops being the directory and says what the pane is doing -----
xdotool windowfocus "$win"
k ctrl+Return; sleep 8
shot 04-first-turn-model-title-in-header
echo "title calls after the first turn: $(titles)" | tee "$out/cadence.txt"

# ---- the cadence: four more turns must not ask for another title --------------------------
for turn in 2 3 4 5; do
    t "and what else did you notice about the drag, pass $turn"; sleep 0.5
    k ctrl+Return; sleep 6
done
shot 05-four-more-turns-same-title
echo "title calls after five turns (must still be 1): $(titles)" | tee -a "$out/cadence.txt"

# ---- a second pane on unrelated work: the tab label joins them with a semicolon ------------
# A new pane is a new worker with no key of its own, so it asks for the provider once too.
k ctrl+e; sleep 4
t 'draft the release notes for the 0.1 preview'; sleep 0.5
k ctrl+Return; sleep 3
k Tab Tab Tab; sleep 0.4
t 'loopback-stub-not-a-key'; sleep 0.4
k shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab; sleep 0.5
k space; sleep 0.5
k Return; sleep 5
k ctrl+Return; sleep 9
shot 06-two-panes-tab-label-semicolon
echo "title calls with two panes: $(titles)" | tee -a "$out/cadence.txt"

# ---- naming a pane by hand: double click the header, type, Enter --------------------------
# The focused (right-hand) pane's header sits just below the tab row.
xdotool mousemove 830 56 click --repeat 2 1; sleep 2
shot 07-header-editor-open
t 'Release notes'; sleep 0.5
k Return; sleep 3
shot 08-renamed-by-hand-no-auto-badge

# ---- /rename-tab names the tab -------------------------------------------------------------
xdotool windowfocus "$win"; sleep 1
t '/rename-tab Two jobs at once'; sleep 0.5
k Return; sleep 3
shot 09-rename-tab

# ---- /rename with no argument opens the same editor ---------------------------------------
t '/rename'; sleep 0.5
k Return; sleep 2
shot 10-rename-with-no-argument-opens-the-editor
k Escape; sleep 2

# ---- a hand-set name survives more turns ---------------------------------------------------
xdotool windowfocus "$win"
t 'keep going with the release notes please'; sleep 0.5
k ctrl+Return; sleep 8
shot 11-hand-set-name-not-overwritten
{ echo "title calls at the end: $(titles)"; echo "--- every side call ---"; awk '{print $2}' "$out/calls.log" | sort | uniq -c; } | tee -a "$out/cadence.txt"

echo "screenshots in $out"
