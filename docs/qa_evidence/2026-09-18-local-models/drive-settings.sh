#!/usr/bin/env bash
# Settings › Local models (card #24XJ): the section of the Options pane that finds, adds, checks
# and removes a model server on this machine.
#
#   docs/qa_evidence/2026-09-18-local-models/drive-settings.sh [build-dir]
#
# One fresh Relay under Xvfb with the profile fully isolated — HOME, XDG_CONFIG_HOME,
# XDG_DATA_HOME, XDG_CACHE_HOME, XDG_RUNTIME_DIR and TMPDIR inside a throwaway sandbox,
# RELAY_KEYRING=off, and RELAY_LOCAL_MODELS pointing at a file that does not exist yet — so the
# section starts empty and the owner's live Relay cannot be mistaken for this one. No
# RELAY_*_API_KEY of any kind: the only model this Relay can reach is the one saved here.
#
# Nothing on the machine is started, stopped or reconfigured. The two servers it finds are the
# owner's: llama-server with bonsai-2-27b on 127.0.0.1:8080 (it sleeps when idle) and Ollama with
# muse-glimmer:latest on 127.0.0.1:11434.
#
#   implementer-settings-a-empty-section                 no endpoints saved
#   implementer-settings-b-find-servers                  both live servers, each with Save
#   implementer-settings-c-saved-row                     bonsai-2-27b saved with Detect
#   implementer-settings-d-test                          Test answered
#   implementer-settings-e-model-dropdown                the pane offers "bonsai-2-27b · local"
#   implementer-settings-f-removed                       Remove: the section is empty again
#   implementer-settings-g-add-by-address-and-toggles    Add by address, and the two per-endpoint flags
#   implementer-settings-h-setup-with-agent              "Ask the agent…" submits the one prompt
#
# The rows are reached by keyboard where the order is fixed (↓ moves, Enter presses the row's first
# button) and by click where it is not — the two "Save" rows arrive in whatever order the probes
# answered in, and Remove is the row's third button. The click coordinates below are for a
# 1500x950 window; a different size needs new ones.
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1500 height=950

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=/tmp/relay-local-settings-$$
xvfb_pid= relay_pid=
cleanup() {
    [[ -n $relay_pid ]] && kill "$relay_pid" 2>/dev/null
    sleep 2
    [[ -n $xvfb_pid ]] && kill "$xvfb_pid" 2>/dev/null
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 3
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy

export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$HOME/run TMPDIR=$HOME/tmp
export RELAY_LOCAL_MODELS=$HOME/local-models.json     # written only when the GUI saves an endpoint
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '[instructions]\nonboarded=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

k() { xdotool key --delay 100 "$@"; }
shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 1; import -window "$win" "$out/implementer-settings-$1.png"; }
click() { eval "$(xdotool getwindowgeometry --shell "$win")"; xdotool mousemove $((X + $1)) $((Y + $2)) click 1; }

"$build/relay" --workspace "$work" >"$out/relay-stderr-settings.log" 2>&1 &
relay_pid=$!
sleep 10
win=
for id in $(xdotool search --name "Relay" 2>/dev/null); do win=$id; done
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2

# ---- the Options pane, on Local models -----------------------------------------------------
# Ctrl+Shift+O opens Options beside the pane with its search box focused; with the search empty the
# arrows walk the tabs. General → Appearance → Models → Local models, which is where it belongs.
k ctrl+shift+o; sleep 3
k Right Right Right; sleep 4
shot a-empty-section

# ---- Find servers ---------------------------------------------------------------------------
# One probe per loopback port. Two answer; 1234 and 8000 are named as silent rather than hidden.
k Down; sleep 1
k Return; sleep 7
shot b-find-servers

# ---- Save the llama.cpp row (with Detect) ---------------------------------------------------
# Row 0 is "Find servers", then one row per server that answered, in the order the probes came
# back. On this machine Ollama is first and llama.cpp second; check shot b before trusting it.
k Down Down; sleep 1
k Return; sleep 10
shot c-saved-row

# ---- Test -----------------------------------------------------------------------------------
# The saved endpoint is now row 0. Enter presses its first button, Test. llama-server may have to
# wake and reload the weights, which is what the 48 s in the shot is.
k Down; sleep 1
k Return; sleep 80
shot d-test

# ---- the pane's model dropdown --------------------------------------------------------------
# The chip at the right of the composer's status row. Its popup is its own window.
click 535 915; sleep 2
xdotool mousemove $((width + 20)) $((height + 20)); sleep 1
import -window root "$out/implementer-settings-e-model-dropdown.png"
k Escape; sleep 2

# ---- Remove ----------------------------------------------------------------------------------
click 1421 253; sleep 5
shot f-removed

# ---- Add by address, and the two per-endpoint flags ------------------------------------------
click 1391 448; sleep 1
xdotool type --delay 20 "http://127.0.0.1:11434"; sleep 1
click 1425 503; sleep 6                 # Detect
click 1432 559; sleep 8                 # Save
click 1447 338; sleep 3                 # Recover tool calls written as text
click 1447 393; sleep 3                 # Send tool-call arguments as objects
shot g-add-by-address-and-toggles
python3 - <<'PY'
import json, os
endpoint = json.load(open(os.environ["RELAY_LOCAL_MODELS"]))["endpoints"][0]
print("registry:", {k: endpoint[k] for k in ("id", "model", "server", "context_window",
                                             "tool_text_recovery", "tool_arguments_as_object")})
PY

# ---- "Set up a model with the agent…" ---------------------------------------------------------
# One ordinary agent request, queued like anything typed in the prompt box.
click 1397 753; sleep 15
shot h-setup-with-agent

printf 'done: %s\n' "$out"
