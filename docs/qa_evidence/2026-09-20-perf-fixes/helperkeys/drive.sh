#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #GMCF — the Actions-pane helper may rebind keys (owner, 2026-09-20: "3 yes").
#
#   docs/qa_evidence/2026-09-20-perf-fixes/helperkeys/drive.sh [relay-binary] [out-dir]
#
# Drives a real Relay under Xvfb, opens the **Actions** pane, opens the helper panel on it with
# Alt+Q and asks it to move a shortcut. The stub endpoint plays the turn the owner's decision is
# about — `app_action_list` then `set_keybinding` — and dumps every request it was sent, so the
# checks can read the helper's own tool list rather than a fixture's.
#
# What it proves, in order:
#   1. the helper's request carries `set_keybinding` and the app tools (the `keybindings` block
#      rides on the helper's `configure`; without it neither exists);
#   2. `app_action_list` answers the helper with the action's **current keys** — the Actions
#      brief's "with its keyboard shortcut beside it";
#   3. the rebind lands in keybindings.json and the running app picks it up — the notice bar says
#      "Keyboard shortcuts reloaded.", which is `Keymap`'s file watch firing its listeners, the
#      same listener that now re-sends the catalogue to the helper.
#
# Isolated HOME / XDG_* / TMPDIR under a short path (the 108-byte socket limit) and
# RELAY_KEYRING=off, so the run never touches the owner's real identity key or provider keys. No
# provider key of any kind is set: the one preset is a local stub endpoint. Needs Xvfb, xdotool,
# ImageMagick and tesseract. Each check writes one PASS/FAIL line to notes.txt.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../../.. && pwd)
# The shots here were taken with the binary land.py built from the exact tree it committed
# (/tmp/claude-1000/land/<me>/verify/build/relay): this checkout's build/relay carries other
# sessions' in-flight edits, so it is not evidence of what landed.
relay=${1:-/tmp/claude-1000/land/pf-helperkeys/verify/build/relay}
width=1500 height=940
port=${RELAY_QA_PORT:-8861}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-gmcf.XXXX)
dump=$sandbox/req
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp" "$dump"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
keys_file=$XDG_CONFIG_HOME/RelayTerminal/relay/keybindings.json

python3 "$PWD/stub-provider.py" "$port" "$dump" >/dev/null 2>&1 &
stub_pid=$!
sleep 1

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$out/notes.txt"
pass=0 fail=0
note() { echo "$*" >>"$out/notes.txt"; }
ok()   { note "PASS $*"; pass=$((pass+1)); }
bad()  { note "FAIL $*"; fail=$((fail+1)); }

win=
t() { xdotool type --delay 30 "$1"; }
k() { xdotool key --delay 60 "$@"; }
# There is no window manager under Xvfb, so the input focus follows the pointer: a screenshot that
# parks the pointer off the window also takes the keyboard away from it.
shotaway() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/$1.png"
    xdotool mousemove "$X" "$Y"
    xdotool windowfocus "$win" 2>/dev/null
    sleep 0.3
}
text() { tesseract "$out/$1.png" - --psm 6 2>/dev/null; }
words() {   # "word left top width height", read at 2x so the small type is legible
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'NF>=12 && $12 != "" {print $12, int($7/2), int($8/2), int($9/2), int($10/2)}'
}
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) print x, y}'; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
has() { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }

# The requests the helper actually sent, as the stub recorded them.
req() { cat "$dump"/req-*.json 2>/dev/null; }

(cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 10
best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4
# The first launch asks how tools are approved, in a pane of its own. Take the recommendation.
shotaway 00-approvals
yes=$(word_xy 00-approvals "recommend")
[[ -n $yes ]] && click_at ${yes% *} ${yes#* }
sleep 2

# ---- the Actions pane, and the helper panel on it -------------------------------------------
# F1 is `help.shortcuts`: "Actions: every action and the keys it answers to".
k F1; sleep 3
shotaway 01-actions-pane
has "the Actions pane is open" 01-actions-pane "actions"

k alt+q; sleep 3
shotaway 02-helper-panel
has "the helper panel opened on it (Alt+Q)" 02-helper-panel "helper"

# The composer, found by the one word only a prompt box's placeholder has.
at=$(word_xy 02-helper-panel "^sends,$")
[[ -n $at ]] && click_at ${at% *} ${at#* }
sleep 1
t "move the close pane shortcut to Ctrl+Alt+Shift+K please"
sleep 1
shotaway 03-typed
k Return
sleep 4
shotaway 04-notice      # the status line, before it fades: "Keyboard shortcuts reloaded."
sleep 8
shotaway 05-answered

# The Actions pane in front of us is the list the key was moved in: search it for the row and
# read the chip. This is the running app's own idea of the shortcut, not the file's.
at=$(word_xy 01-actions-pane "^Search$")
[[ -n $at ]] && click_at ${at% *} ${at#* }
t "close pane"; sleep 2
shotaway 06-actions-row

# ---- what the helper was given, and what it did ----------------------------------------------
if req | grep -q '"set_keybinding"'; then
    ok "the helper's request carries set_keybinding (stub dump)"
else
    bad "the helper's request has no set_keybinding — the keybindings block did not arrive"
fi
if req | grep -q '"app_action_list"'; then
    ok "the helper's request carries the app tools (stub dump)"
else
    bad "the helper's request has no app_action_list"
fi
# The tool result the helper read back: the palette with its shortcut beside it.
if req | grep -q 'pane.close' && req | grep -qi 'ctrl+w'; then
    ok "app_action_list answered with pane.close on its current key, Ctrl+W (stub dump)"
else
    bad "app_action_list did not answer the helper with the action's keys"
fi
if [[ -s $keys_file ]] && grep -q 'Ctrl+Alt+Shift+K' "$keys_file"; then
    ok "the rebind is in keybindings.json ($(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["bindings"])' "$keys_file"))"
else
    bad "keybindings.json does not hold the rebind: $(head -c 400 "$keys_file" 2>/dev/null)"
fi
has "the running app reloaded the keys" 04-notice "shortcuts reloaded"
if text 05-answered | tr -d ' ' | grep -qi 'pane.closeison'; then
    ok "the helper said what it had done (05-answered.png)"
else
    bad "the helper's answer is not on screen; see 05-answered.png"
fi
# OCR reads the chip as one token; the plus signs survive, the case does not always.
if text 06-actions-row | tr -d ' ' | grep -qi 'ctrl+alt+shift+k'; then
    ok "the Actions pane shows Close pane on the new key (06-actions-row.png)"
else
    bad "the Actions pane still shows the old key; see 06-actions-row.png"
fi

note ""
note "$pass passed, $fail failed"
cat "$out/notes.txt"
[[ $fail -eq 0 ]]
