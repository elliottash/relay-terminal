#!/usr/bin/env bash
# The three shots drive.sh did not reach on its first run (a sibling session relinked build/relay
# mid-run, so the second Relay had no binary to start). Same isolation and the same two harness
# traps as drive.sh; the caller holds build/.relay-build.lock so the binary cannot move again.
#
#   g  the roles modal with one saved endpoint: the fourth row, "Local · bonsai-2-27b"
#   h  an EMPTY registry: /local says "No local model is set up." and the pane does not switch
#   i  the roles modal with an empty registry: the Local row disabled, with its inline note
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=/tmp/relay-local-tier-rest-$$
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
export DISPLAY=$display
export RELAY_KEYRING=off
export RELAY_KIMI_API_KEY=not-a-real-key-kimi
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy

t() { xdotool type --delay 16 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {
    rm -rf "$sandbox"
    export HOME=$sandbox
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    export XDG_RUNTIME_DIR=$HOME/run TMPDIR=$HOME/tmp
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work" \
             "$XDG_RUNTIME_DIR" "$TMPDIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
CONF
    export RELAY_LOCAL_MODELS=$HOME/local-models.json
    if [[ ${1:-} == empty ]]; then
        printf '{"version": 1, "endpoints": []}\n' >"$RELAY_LOCAL_MODELS"
    else
        (cd "$root" && python3 scripts/relay-local.py add --id bonsai --label "Bonsai 2 27B" \
             --base-url http://127.0.0.1:8080 --detect) >"$out/registry-add.txt" 2>&1 \
            || { echo "relay-local.py add --detect failed"; exit 1; }
    fi
}

find_window() { win=; local id; for id in $(xdotool search --name "$1" 2>/dev/null); do win=$id; done; }
shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8; import -window "${2:-root}" "$out/implementer-$1.png"; }
start() {
    "$build/relay" --workspace "$work" >"$out/relay-stderr-$1.log" 2>&1 &
    relay_pid=$!
    sleep 9
    find_window "Relay"
    [[ -z $win ]] && { echo "no Relay window ($1)"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
    main_win=$win
}
roles_shot() {    # roles_shot <letter-slug>
    xdotool windowfocus "$main_win"; sleep 0.5
    k ctrl+shift+a; sleep 2
    t 'model roles'; sleep 1.5
    k Return; sleep 3
    find_window "Model roles"
    [[ -z $win ]] && { echo "no roles dialog"; exit 1; }
    xdotool windowmove "$win" 40 40; sleep 1
    shot "$1" "$win"
}

prepare
start roles-with-endpoint
roles_shot g-roles-modal-local-row
kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

prepare empty
start empty-registry
xdotool windowfocus "$main_win"; sleep 1
t '/local'; sleep 1.5
k Return; sleep 2
shot h-no-local-model-is-set-up "$main_win"
kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

prepare empty
start roles-empty-registry
roles_shot i-roles-modal-local-row-disabled

printf 'done: %s\n' "$out"
