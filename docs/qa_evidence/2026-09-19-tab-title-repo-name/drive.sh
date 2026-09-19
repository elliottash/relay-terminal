#!/usr/bin/env bash
# Live check of the place-based tab title (owner, 2026-09-19: "change the tab title to be the
# repo name of the associated project, otherwise the folder of the active pane"), under
# Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-19-tab-title-repo-name/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script, plus relay-stderr.log. Needs Xvfb, xdotool
# and ImageMagick `import`. An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the
# real profile. No provider is configured anywhere in this run and none is started: a tab label
# is the GUI's own since 2026-09-19 (protocol 18.3), so the six scenes below are their own proof
# that no model is asked — the label is right immediately, and moves with every cd.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
repo=$root                      # this checkout: a project with a board, so candidateFor finds it
display=:95

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export RELAY_KEYRING=off
work=$(mktemp -d)
mkdir -p "$work/qa-playground"
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" 2>/dev/null; sleep 1; kill "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

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

# The workspace is an ordinary folder outside any project: the tab starts on its folder name.
# (Typing goes to the composer; Return hands a shell command to the pane's shell, and OSC 7
# carries the new directory back, which is what the tab label follows.)
"$build/relay" --workspace "$work/qa-playground" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 6
win=$(xdotool search --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 950
xdotool windowfocus "$win"
xdotool mousemove 700 700
sleep 2

# Warm the shell up: shell integration loads with the first prompt, and the OSC 7 that carries a
# new directory back — which is what the tab label follows — only comes from prompts after it.
t 'echo ready'; sleep 0.5
k Return; sleep 3

# ---- inside the repository: the tab names the repo, from a subdirectory ---------------------
t "cd $repo/src"; sleep 0.5
k Return; sleep 3
shot 01-repo-name-from-a-subdirectory

# ---- back outside any project: the folder of the pane's own directory -----------------------
t 'cd /tmp'; sleep 0.5
k Return; sleep 3
shot 02-folder-outside-any-repo

# ---- home is "~" ----------------------------------------------------------------------------
t 'cd ~'; sleep 0.5
k Return; sleep 3
shot 03-home-is-tilde

# ---- and the repo again, from another directory: the label follows the cwd ------------------
t "cd $repo/backend"; sleep 0.5
k Return; sleep 3
shot 04-back-in-the-repo

# ---- /rename-tab still wins -----------------------------------------------------------------
t '/rename-tab My own name'; sleep 0.5
k Return; sleep 2
shot 05-rename-tab-wins

# ---- and clearing it puts the tab back under its place title --------------------------------
# A bare /rename-tab opens the same editor, prefilled with the hand-set name; committing an
# empty field hands the tab back to its place.
t '/rename-tab'; sleep 0.5
k Return; sleep 1.5
k ctrl+a; sleep 0.3
k BackSpace; sleep 0.3
k Return; sleep 2
shot 06-clearing-returns-to-the-place

echo "screenshots in $out"
