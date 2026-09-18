#!/usr/bin/env bash
# Model roles: provider names, keyless providers hidden, and a tier that follows another provider.
# Owner, 2026-09-18: "i want kimi for my main model, and glm 5.3 flash for the flash model … it
# shouldn't show options where you don't have a key assigned … it should not say the model name".
#
#   docs/qa_evidence/2026-09-18-provider-names-and-tier-providers/drive.sh [build-dir]
#
# One fresh Relay under Xvfb with an isolated profile and a throwaway workspace. The API keys are
# fake strings in the environment (RELAY_*_API_KEY) and every outbound proxy points at a closed
# port, so nothing is ever sent to a provider: every line in these shots is decided inside Relay.
#
#   a  Model roles as it opens: Main on Kimi, provider rows named after the company
#   b  the Default provider list: only Kimi and Z.AI (GLM) — the other six have no key
#   c  the Flash row's provider list, same two entries
#   d  Flash set to Z.AI (GLM) by picking the provider alone: "Flash · glm-5.3-flash · on Z.AI (GLM)"
#   e  a second Kimi key (Kimi Code): the two Kimi entries are told apart by their plan
#   f  Settings › General: "Default input for new sessions" reads Auto, not Auto detect
#   g  the mode chip menu: auto / terminal / agent
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=/tmp/relay-provider-names-$$
xvfb_pid= relay_pid=
cleanup() {
    [[ -n $relay_pid ]] && kill "$relay_pid" 2>/dev/null
    [[ -n $xvfb_pid ]] && kill "$xvfb_pid" 2>/dev/null
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off          # the desktop keyring is never touched
export HTTP_PROXY=http://127.0.0.1:1 HTTPS_PROXY=http://127.0.0.1:1 ALL_PROXY=http://127.0.0.1:1
export http_proxy=$HTTP_PROXY https_proxy=$HTTPS_PROXY all_proxy=$ALL_PROXY
# Fake keys. Their only job is to make has_stored_key true for two providers.
export RELAY_KIMI_API_KEY=not-a-real-key-kimi
export RELAY_GLM_CODING_API_KEY=not-a-real-key-glm-coding

t() { xdotool type --delay 16 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {
    rm -rf "$sandbox"
    export HOME=$sandbox
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[provider]
preset=kimi
CONF
}

find_window() {   # find_window <name-fragment>
    win=
    local id
    for id in $(xdotool search --name "$1" 2>/dev/null); do win=$id; done
}

# Clicks a control by its position inside the roles dialog. There is no window manager under Xvfb,
# so the dialog's own geometry is its position on the screen and the two add up exactly.
click_in_roles() {   # click_in_roles <x> <y>
    eval "$(xdotool getwindowgeometry --shell "$roles_win")"
    xdotool mousemove $((X + $1)) $((Y + $2)) click 1
}

shot() {          # shot <letter-slug> [window]
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "${2:-root}" "$out/implementer-$1.png"
}

start() {         # start <stderr-slug>
    "$build/relay" --workspace "$work" >"$out/relay-stderr-$1.log" 2>&1 &
    relay_pid=$!
    sleep 9
    find_window "Relay"
    [[ -z $win ]] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
    main_win=$win
}

open_roles() {
    xdotool windowfocus "$main_win"; sleep 0.5
    k ctrl+shift+a; sleep 2
    t 'model roles'; sleep 1.5
    k Return; sleep 3
    find_window "Model roles"
    [[ -z $win ]] && { echo "no roles dialog"; exit 1; }
    roles_win=$win
    xdotool windowmove "$roles_win" 40 40; sleep 1
}

# ---- one Kimi key and one Z.AI key ------------------------------------------------------
prepare
start one-key-each
open_roles
shot a-roles-modal "$roles_win"

# The Default provider list. The popup is its own window, so the whole screen is captured.
xdotool windowfocus "$roles_win"; sleep 0.5
click_in_roles 400 27; sleep 2
shot b-default-provider-list
k Escape; sleep 1

# The Flash row's provider list, then Z.AI (GLM) chosen from it — a provider, with no model typed.
click_in_roles 530 185; sleep 2
shot c-flash-provider-list
k Down Down Return; sleep 3       # Default provider → Kimi → Z.AI (GLM)
shot d-flash-on-zai "$roles_win"

kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

# ---- both Kimi plans keyed: the plan tells them apart ------------------------------------
export RELAY_KIMI_CODE_API_KEY=not-a-real-key-kimi-code
prepare
start both-kimi-plans
open_roles
xdotool windowfocus "$roles_win"; sleep 0.5
click_in_roles 400 27; sleep 2
shot e-two-kimi-plans
k Escape; sleep 1
xdotool windowkill "$roles_win" 2>/dev/null; sleep 1.5
kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

# ---- "Auto", not "Auto detect" -----------------------------------------------------------
# A fresh window, so the shot is the settings pane and nothing else. Ctrl+, opens it on General.
prepare
start input-default
# The mode chip's menu first, while the window is one pane wide and the chip is where it starts.
eval "$(xdotool getwindowgeometry --shell "$main_win")"
xdotool mousemove $((X + 1345)) $((Y + 823)) click 1; sleep 2
shot g-mode-chip-menu
k Escape; sleep 1.5
# Then the settings row that names the same thing.
xdotool windowfocus "$main_win"; sleep 0.5
k ctrl+comma; sleep 3
t 'default input'; sleep 2.5
shot f-input-default-auto "$main_win"

printf 'done: %s\n' "$out"
