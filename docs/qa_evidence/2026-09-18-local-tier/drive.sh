#!/usr/bin/env bash
# The Local tier and /local (card #JH22): a fourth tier beside Main/Flash/Lite, and one command in
# the composer that runs this pane on the model this machine serves.
#
#   docs/qa_evidence/2026-09-18-local-tier/drive.sh [build-dir]
#
# Copied from docs/qa_evidence/2026-09-18-local-models/drive.sh and its two harness traps:
#   * the profile is fully isolated — HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME,
#     XDG_RUNTIME_DIR and TMPDIR inside a throwaway sandbox, RELAY_KEYRING=off — so the owner's own
#     live Relay on :0 cannot be mistaken for this one;
#   * a fresh Relay per modal: closing one dialog and opening the next from the palette in the same
#     process lost the X connection under Xvfb and the shot after it was of nothing.
#
# One fake RELAY_KIMI_API_KEY, so the pane opens on a hosted Main (kimi-k3) and the switch to the
# local model is visible. Nothing is ever sent to Kimi: configure makes no network call and the only
# prompt is asked after the pane is on the local model.
#
# With one saved endpoint (llama-server's bonsai-2-27b on 127.0.0.1:8080):
#   a  the pane as it opens: the chip reads kimi-k3, the hosted Main
#   b  the model dropdown: Main agent, Flash agent and the new Local agent row
#   c  /local typed in the composer, before Enter
#   d  after Enter: the chip reads "Local agent · bonsai-2-27b"
#   e  a real agent turn on Bonsai that made a tool call
#   f  /main: the chip is back on kimi-k3
#   g  the roles modal, fourth row "Local · bonsai-2-27b"
# With an EMPTY registry:
#   h  /local says "No local model is set up." and the pane does not switch
#   i  the roles modal's Local row, disabled, with the inline note
#
# Needs Xvfb, xdotool and ImageMagick. Does NOT start or stop llama-bonsai.service.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=/tmp/relay-local-tier-$$
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
export RELAY_KIMI_API_KEY=not-a-real-key-kimi
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy

t() { xdotool type --delay 16 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {       # prepare [empty]
    rm -rf "$sandbox"
    export HOME=$sandbox
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    export XDG_RUNTIME_DIR=$HOME/run TMPDIR=$HOME/tmp
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work" \
             "$XDG_RUNTIME_DIR" "$TMPDIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    # Three files with names a model cannot guess: the answer proves the tool call really ran.
    : >"$work/ledger-ferrous.md"; : >"$work/quokka-notes.txt"; : >"$work/zamboni.cfg"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
CONF
    export RELAY_LOCAL_MODELS=$HOME/local-models.json
    if [[ ${1:-} == empty ]]; then
        printf '{"version": 1, "endpoints": []}\n' >"$RELAY_LOCAL_MODELS"
    else
        # The registry is written by the product's own CLI against the live llama-server, so the
        # window, the model id and the capabilities are detected, not typed here.
        (cd "$root" && python3 scripts/relay-local.py add --id bonsai --label "Bonsai 2 27B" \
             --base-url http://127.0.0.1:8080 --detect) >"$out/registry-add.txt" 2>&1 \
            || { echo "relay-local.py add --detect failed; see registry-add.txt"; exit 1; }
    fi
}

find_window() {   # find_window <name-fragment>
    win=
    local id
    for id in $(xdotool search --name "$1" 2>/dev/null); do win=$id; done
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

# ===== with one saved endpoint ================================================================
prepare
start with-endpoint
shot a-pane-on-hosted-main "$main_win"

# ---- the model dropdown: the Local agent row --------------------------------------------------
eval "$(xdotool getwindowgeometry --shell "$main_win")"
xdotool mousemove $((X + 1192)) $((Y + 864)) click 1; sleep 2
shot b-dropdown-has-the-local-row
k Escape; sleep 1

# ---- /local ----------------------------------------------------------------------------------
xdotool windowfocus "$main_win"; sleep 1
t '/local'; sleep 1.5
shot c-slash-local-typed "$main_win"
k Return; sleep 4
shot d-pane-on-the-local-agent "$main_win"

# ---- a real turn on Bonsai, with a tool call --------------------------------------------------
xdotool windowfocus "$main_win"; sleep 1
t '*list the files in the workspace directory and tell me their names'
sleep 1
k Return
sleep "${TURN_WAIT:-180}"
shot e-turn-with-a-tool-call "$main_win"

# ---- /main puts it back ------------------------------------------------------------------------
xdotool windowfocus "$main_win"; sleep 1
t '/main'; sleep 1
k Return; sleep 4
shot f-back-on-main "$main_win"
kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

# ---- the roles modal: the fourth row -----------------------------------------------------------
prepare
start roles-with-endpoint
xdotool windowfocus "$main_win"; sleep 0.5
k ctrl+shift+a; sleep 2
t 'model roles'; sleep 1.5
k Return; sleep 3
find_window "Model roles"
[[ -z $win ]] && { echo "no roles dialog"; exit 1; }
xdotool windowmove "$win" 40 40; sleep 1
shot g-roles-modal-local-row "$win"
kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

# ===== with an empty registry ==================================================================
prepare empty
start empty-registry
xdotool windowfocus "$main_win"; sleep 1
t '/local'; sleep 1.5
k Return; sleep 2
shot h-no-local-model-is-set-up "$main_win"
kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

prepare empty
start roles-empty-registry
xdotool windowfocus "$main_win"; sleep 0.5
k ctrl+shift+a; sleep 2
t 'model roles'; sleep 1.5
k Return; sleep 3
find_window "Model roles"
[[ -z $win ]] && { echo "no roles dialog"; exit 1; }
xdotool windowmove "$win" 40 40; sleep 1
shot i-roles-modal-local-row-disabled "$win"

printf 'done: %s\n' "$out"
