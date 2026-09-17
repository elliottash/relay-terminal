#!/usr/bin/env bash
# Live check of the two model modals and the compact Settings window, under Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-17-model-settings/drive.sh [build-dir]
#
# Writes NN-*.png next to this script plus relay-stderr.log. Needs Xvfb, xdotool and
# ImageMagick `import`. XDG_CONFIG_HOME/XDG_DATA_HOME are isolated so the run never touches the
# real profile. The desktop keyring is NOT isolated (it is per user, not per XDG dir), and that is
# deliberate: steps 4 and 7 prove the Test button and a real agent turn work on a key that only the
# worker ever sees. No key is typed, printed or screenshotted at any point.
#
# WARNING: because the keyring is the real one, this script must never click "Remove" in the keys
# modal. It would delete one of your provider keys for good. Remove is guarded by a confirmation
# dialog, but do not rely on that when editing the coordinates below.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:93

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[agent]
show_thinking=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

echo "hello from the model-settings QA run" >"$work/README.md"

Xvfb "$display" -screen 0 1500x950x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window root "$out/$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
# No window manager: place and focus a window by title before touching it.
place() {
  local win
  win=$(xdotool search --name "$1" | tail -1)
  [[ -z $win ]] && { echo "no window matching '$1'"; return 1; }
  xdotool windowmove "$win" "${2:-60}" "${3:-40}" windowactivate "$win" windowfocus "$win"
  sleep 1
  echo "$win"
}

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 950
xdotool windowfocus "$win"
sleep 2
shot 01-start

# --- 1. the gear at the bottom of the pane's model box ----------------------------------------
# The model box sits in the row under the terminal; click it to drop the list open.
xdotool mousemove 1148 796 click 1
sleep 1.5
shot 02-model-dropdown-with-gear

# The gear is the last entry. End then Return picks it without knowing its pixel position.
k End; sleep 0.5
k Return; sleep 3
shot 03-roles-modal-from-gear
place "Model roles" 80 60 >/dev/null
shot 04-roles-modal-placed

# --- 2. Advanced options: one row per job (the disclosure remembers it is open) ----------------
xdotool mousemove 170 369 click 1
sleep 2
shot 05-roles-advanced-open

# --- 3. switch the default provider and watch the tiers change --------------------------------
# Kimi -> Z.AI GLM-5.3 Coding Plan (the 4th entry); the three tier rows must all change.
xdotool mousemove 470 87 click 1
sleep 1.5
shot 06-provider-list-open
k Down Down Down; sleep 0.5
shot 07-provider-list-moving
k Return; sleep 5
place "Model roles" 80 60 >/dev/null
shot 08-tiers-after-provider-switch
shot 09-advanced-after-provider-switch
# Scroll the Advanced list to the bottom: chores, the request audit, images, and the pinned
# command-routing row with its reason.
for _ in 1 2 3 4 5 6 7 8; do xdotool mousemove 400 700 click 5; done
sleep 1.5
shot 09b-advanced-bottom-command-routing

# --- 4. the keys modal, from the roles modal ---------------------------------------------------
xdotool mousemove 800 87 click 1
sleep 3
place "API keys" 120 80 >/dev/null
shot 10-keys-modal-groups-and-status

# Test the OpenRouter key: one minimal call, reported in the status line, never printing the key.
# Row order with the modal at 120,80: Subscriptions header 190, Kimi Code 210, GLM Coding Plan 230,
# MiniMax 250, Aggregator header 270, OpenRouter 290, ... Buttons sit at y=510, Test at x=382.
# NOTE: Remove is at x=306 on the same row. This script never clicks it — see the warning above.
xdotool mousemove 300 290 click 1; sleep 1
shot 11-keys-modal-openrouter-selected
xdotool mousemove 382 510 click 1
sleep 6
shot 12-keys-modal-testing
# A reasoning model spends the test budget thinking, so give it room before reading the verdict.
sleep 45
shot 12b-keys-modal-test-result

k Escape; sleep 1
place "Model roles" 80 60 >/dev/null
k Escape; sleep 1
shot 13-modals-closed

# --- 5. the compact Settings window ------------------------------------------------------------
xdotool windowactivate "$win"; xdotool windowfocus "$win"; sleep 1
k ctrl+comma; sleep 3
place "Settings" 100 50 >/dev/null
shot 14-settings-general

for i in 1 2 3 4 5; do
  xdotool mousemove 190 $(( 61 + i * 20 )) click 1
  sleep 1.2
  shot "$(printf '%02d' $(( 14 + i )))-settings-section-$i"
done

# Models section holds the two buttons; open both from here.
xdotool mousemove 190 81 click 1; sleep 1.2
shot 20-settings-models
xdotool mousemove 780 150 click 1; sleep 3
place "API keys" 140 100 >/dev/null
shot 21-keys-modal-from-settings
k Escape; sleep 1
place "Settings" 100 50 >/dev/null
xdotool mousemove 780 195 click 1; sleep 3
place "Model roles" 140 100 >/dev/null
shot 22-roles-modal-from-settings
k Escape; sleep 1
place "Settings" 100 50 >/dev/null
k Escape; sleep 1.5
shot 23-settings-closed

# --- 6. the palette still reaches every setting ------------------------------------------------
xdotool windowactivate "$win"; xdotool windowfocus "$win"; sleep 1
k ctrl+shift+a; sleep 1.5
t 'Settings'; sleep 1.5
shot 24-palette-settings-search
# Esc clears the filter, then goes back, then closes: three to leave the palette entirely.
k Escape; sleep 0.5; k Escape; sleep 0.5; k Escape; sleep 1

# --- 7. a real turn on the configured main model, key from the keystore ------------------------
xdotool windowactivate "$win"; xdotool windowfocus "$win"; sleep 1
xdotool mousemove 700 880 click 1; sleep 1
t 'in one short sentence, what is in README.md here?'
sleep 1
shot 25-prompt-typed
k ctrl+Return
sleep 45
shot 26-real-turn-answered
sleep 20
shot 27-real-turn-final

# The screenshots are only meaningful while the X server is up; a black frame means Xvfb died
# under this run, not that Relay did (its stderr log would say so).
xdotool getdisplaygeometry >/dev/null 2>&1 || echo "WARNING: Xvfb died during the run; re-run."
echo "screenshots in $out"
grep -ci 'traceback\|Exception' "$out/relay-stderr.log" && echo "NOTE: check relay-stderr.log"
