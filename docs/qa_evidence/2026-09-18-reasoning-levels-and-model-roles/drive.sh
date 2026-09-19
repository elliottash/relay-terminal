#!/usr/bin/env bash
# Reasoning levels and the Model roles modal, owner reports of 2026-09-18:
#   "in the models options page, there was low, medium, high, max reasoning. but in the model
#    roles, there were only 3 options"
#   "you also still cant pick the main model options"
#   "advanced options should be separate from the providers. i might want to pick kimi k3 for main
#    agents and glm 5.3 flash for subagents"
#   "also add a reset to defaults button on options pages"
#
#   docs/qa_evidence/2026-09-18-reasoning-levels-and-model-roles/drive.sh [build-dir] [scene...]
#
# One fresh Relay per scene under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and
# TMPDIR. The keys are fake strings in RELAY_*_API_KEY — enough to make has_stored_key true — and
# every proxy points at a closed port, so nothing leaves this machine: every level in these shots
# is decided inside Relay from the preset table.
#
# Scenes (all by default):
#   effort   a-options-effort-glm.png   Options › Models on Z.AI (GLM): the levels the provider has
#            b-options-effort-list.png  the same row's list, open
#            c-palette-effort.png       the palette's Reasoning effort submenu, same levels
#   roles    d-roles-modal.png          Model roles: the Main row's own controls
#            e-roles-effort-list.png    a tier's effort list — the same levels as scene b
#            f-roles-advanced.png       Advanced: a job pinned to another provider
#   reset    g-options-reset.png        an Options page's Reset to defaults row
#            h-options-reset-confirm.png the confirmation it asks first
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-effort roles reset}
width=1400 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=/tmp/relay-reasoning-levels-$$
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
# Fake keys, so three providers are choosable: Z.AI (GLM), whose medium and high are one request,
# Kimi, the same shape, and OpenRouter, which has all four levels.
export RELAY_GLM_CODING_API_KEY=not-a-real-key-glm-coding
export RELAY_KIMI_API_KEY=not-a-real-key-kimi
export RELAY_OPENROUTER_API_KEY=not-a-real-key-openrouter

t() { xdotool type --delay 16 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {       # prepare <preset>
    rm -rf "$sandbox"
    export HOME=$sandbox XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work" "$sandbox/run" "$sandbox/tmp"
    chmod 700 "$sandbox/run"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[provider]
preset=$1
[agent]
effort=high
CONF
}

find_window() {   # find_window <name-fragment>
    win=
    local id
    for id in $(xdotool search --name "$1" 2>/dev/null); do win=$id; done
}

shot() {          # shot <slug> [window]
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "${2:-root}" "$out/implementer-$1.png"
}

start() {         # start <preset> <stderr-slug>
    prepare "$1"
    "$build/relay" --workspace "$work" >"$out/relay-stderr-$2.log" 2>&1 &
    relay_pid=$!
    sleep 9
    find_window "Relay"
    [[ -z $win ]] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
    main_win=$win
}

stop() { [[ -n $relay_pid ]] && { kill -9 "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; }; relay_pid=; sleep 2; }

# The Options pane's search is the shortest keyboard path to one row, and it is also the check that
# the row is findable by name.
open_option() {   # open_option <search words>
    xdotool windowfocus "$main_win"; sleep 0.5
    k ctrl+comma; sleep 3
    t "$1"; sleep 2.5
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

effort_scene() {
    start glm-coding effort
    open_option 'reasoning effort'
    shot a-options-effort-glm "$main_win"
    # The row's own list. Clicking the combo, because Down in the search view walks on to the
    # palette rows underneath the option and Enter there would change the pane's effort.
    eval "$(xdotool getwindowgeometry --shell "$main_win")"
    xdotool mousemove $((X + 1315)) $((Y + 215)) click 1; sleep 1.5
    shot b-options-effort-list
    k Escape; sleep 1
    xdotool windowfocus "$main_win"; sleep 0.5
    k ctrl+shift+a; sleep 2
    t 'reasoning effort'; sleep 2
    # No Enter: the first row *is* a level, and running it would change the pane's effort under
    # the shot. What is on screen is the list, which is what this scene is about.
    shot c-palette-effort
    k Escape; sleep 1
    stop
}

# A control by its position inside the roles dialog. There is no window manager under Xvfb, so the
# dialog's own geometry is its position on the screen and the two add up exactly.
click_in_roles() {   # click_in_roles <x> <y>
    eval "$(xdotool getwindowgeometry --shell "$roles_win")"
    xdotool mousemove $((X + $1)) $((Y + $2)) click 1
}

roles_scene() {
    start glm-coding roles
    open_roles
    shot d-roles-modal "$roles_win"
    # The Main row's effort list: the same three levels the Options page offers (scene b).
    click_in_roles 780 144; sleep 1.5
    shot e-roles-effort-list
    k Escape; sleep 1
    # Advanced: one row per job, each able to name its own provider.
    click_in_roles 90 470; sleep 2
    shot f-roles-advanced "$roles_win"
    stop
}

reset_scene() {
    start glm-coding reset
    open_option 'reset defaults'
    shot g-options-reset "$main_win"
    stop
}

for scene in $scenes; do
    case $scene in
        effort) effort_scene ;;
        roles)  roles_scene ;;
        reset)  reset_scene ;;
        *) echo "unknown scene: $scene" ;;
    esac
done

printf 'done: %s\n' "$out"
