#!/usr/bin/env bash
# Local model endpoints in the GUI (card #24XJ): a model server on this machine is a row in the
# model dropdown, is selectable, and runs a real agent turn with a tool call — with no API key
# stored anywhere.
#
#   docs/qa_evidence/2026-09-18-local-models/drive.sh [build-dir]
#
# One fresh Relay under Xvfb. The profile is fully isolated — HOME, XDG_CONFIG_HOME,
# XDG_DATA_HOME, XDG_CACHE_HOME, XDG_RUNTIME_DIR and TMPDIR all point inside a throwaway sandbox,
# RELAY_KEYRING=off — so a live Relay of the owner's cannot be mistaken for this one, and no
# RELAY_*_API_KEY is exported: has_stored_key is false for every built-in preset. The only usable
# model is RELAY_LOCAL_MODELS's one endpoint, llama-server's bonsai-2-27b on 127.0.0.1:8080.
#
# With no key at all:
#   a  the pane as it opens: the model chip reads "bonsai-2-27b · local"
#   b  the model dropdown open: the Bonsai row, and no other preset (none has a key)
#   c  the keys modal: the local endpoint is not in it — it is not a key to hold
#   d  the roles modal: "Bonsai 2 27B" is the default provider, offered with no key
# Then again with one fake RELAY_KIMI_API_KEY, so one built-in preset has a key:
#   e  the pane opened on Kimi: a local row never wins the automatic choice over a key
#   f  the dropdown: the keyed preset and, after it, "bonsai-2-27b · local"
#   g  the local row picked by hand: the chip is on it
#   h  a real agent turn on the local model that made a list_directory tool call
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

sandbox=/tmp/relay-local-models-$$
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
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy
# No RELAY_*_API_KEY of any kind: nothing has a key, so nothing but the local endpoint is usable.

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
    # Three files with names a model cannot guess: the answer proves the tool call really ran.
    : >"$work/ledger-ferrous.md"; : >"$work/quokka-notes.txt"; : >"$work/zamboni.cfg"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
CONF
    # The registry the GUI reads. Written by scripts/relay-local.py add --detect against the live
    # llama-server, then copied here so the run does not depend on the owner's real config.
    export RELAY_LOCAL_MODELS=$HOME/local-models.json
    cat >"$RELAY_LOCAL_MODELS" <<'JSON'
{
  "version": 1,
  "endpoints": [
    {
      "id": "local:bonsai",
      "label": "Bonsai 2 27B",
      "base_url": "http://127.0.0.1:8080/v1",
      "model": "bonsai-2-27b",
      "server": "llamacpp",
      "context_window": 131072,
      "tools": true,
      "thinking": true,
      "extra": {},
      "note": "",
      "first_token_timeout": 300.0,
      "parallel_tool_calls": false,
      "tool_text_recovery": false
    }
  ]
}
JSON
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

prepare
start local-only
shot a-pane-on-the-local-model "$main_win"

# ---- the model dropdown -------------------------------------------------------------------
# The chip sits at the right of the composer's status row. The popup is its own window, so the
# whole screen is captured.
eval "$(xdotool getwindowgeometry --shell "$main_win")"
xdotool mousemove $((X + 1192)) $((Y + 864)) click 1; sleep 2
shot b-model-dropdown
k Escape; sleep 1

# ---- the keys modal: no local row ----------------------------------------------------------
xdotool windowfocus "$main_win"; sleep 0.5
k ctrl+shift+a; sleep 2
t 'api keys'; sleep 1.5
k Return; sleep 3
find_window "API keys"
[[ -z $win ]] && { echo "no keys dialog"; exit 1; }
xdotool windowmove "$win" 40 40; sleep 1
shot c-keys-modal-has-no-local-row "$win"
kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

# ---- the roles modal: the local endpoint is the default provider ---------------------------
# A fresh Relay per modal. Closing one dialog and opening the next from the palette in the same
# process lost the X connection under Xvfb, and the shot after it was of nothing.
prepare
start local-only-roles
xdotool windowfocus "$main_win"; sleep 0.5
k ctrl+shift+a; sleep 2
t 'model roles'; sleep 1.5
k Return; sleep 3
find_window "Model roles"
[[ -z $win ]] && { echo "no roles dialog"; exit 1; }
xdotool windowmove "$win" 40 40; sleep 1
shot d-roles-modal-local-provider "$win"
kill -9 "$relay_pid" 2>/dev/null; relay_pid=; sleep 3

# ---- one keyed provider beside the local endpoint -------------------------------------------
# A fake Kimi key, so exactly one built-in preset has one. Nothing is ever sent to Kimi: the pane
# only ever configures on it (no network call), and the one prompt is asked after the switch.
export RELAY_KIMI_API_KEY=not-a-real-key-kimi
prepare
start one-key-and-local
# The pane auto-configured on Kimi, not on the local endpoint: a local row is never the automatic
# "first stored" choice ahead of a provider a key can reach.
shot e-a-key-wins-the-automatic-choice "$main_win"

# Now select the local row by hand: the chip goes to it and the conversation is kept (set_model).
eval "$(xdotool getwindowgeometry --shell "$main_win")"
xdotool mousemove $((X + 1192)) $((Y + 864)) click 1; sleep 2
shot f-dropdown-with-a-key-and-the-local-row
k Down Return; sleep 4
shot g-switched-to-the-local-model "$main_win"

# ---- a real turn on the local model, with a tool call ---------------------------------------
xdotool windowfocus "$main_win"; sleep 1
t '*list the files in the workspace directory and tell me their names'
sleep 1
k Return
# A 27B model on this machine, woken from idle: give it room.
sleep "${TURN_WAIT:-180}"
shot h-agent-turn-with-tool-call "$main_win"

printf 'done: %s\n' "$out"
