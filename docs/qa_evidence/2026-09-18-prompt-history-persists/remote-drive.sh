#!/usr/bin/env bash
# A line typed away from the desktop is kept too (#H8VP, owner: "these should always be saved").
# A real guest joins a shared pane over the remote protocol and sends a prompt; this checks the
# prompt reaches $XDG_DATA_HOME/relay/state/prompt-history.txt and that Up in the desktop's own
# prompt box then offers it.
#
#   docs/qa_evidence/2026-09-18-prompt-history-persists/remote-drive.sh [build-dir]
#
# The sharing half is the flow docs/qa_evidence/2026-09-18-remote-multiplayer-desktop/drive.sh
# established (#W5N2), down to the click points in a 1600x1000 window, and it borrows that run's
# guest: remote/client.py driven by guest.py, not the web client. Writes remote-NN-*.png,
# remote-guest.log, remote-relay-stderr.log and remote-history-file.txt next to this script.
#
# Isolation: XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME (the remote identity key, devices.json
# and guests.json live under XDG_DATA_HOME, as does the history file this checks), and
# XDG_RUNTIME_DIR and TMPDIR, without which a Relay already running here shares sockets with it.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
guest_py=$root/docs/qa_evidence/2026-09-18-remote-multiplayer-desktop/guest.py
[[ -f $guest_py ]] || { echo "the multiplayer run's guest is not there: $guest_py"; exit 1; }
rm -f "$out/remote-guest.log"

display=
for n in $(seq 140 180); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free X display"; exit 1; }
echo "display $display"

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export XDG_RUNTIME_DIR=$(mktemp -d) TMPDIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
[windows]
restore=false
CONF
trap 'kill "${relay_pid:-0}" "${guest_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null;
      rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"' EXIT

Xvfb "$display" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

history_file=$XDG_DATA_HOME/relay/state/prompt-history.txt
guest_prompt='the line alice typed on her phone'
shot() { import -window "$win" "$out/remote-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
waitfor() { for _ in $(seq 1 "${2:-40}"); do grep -q "$1" "$out/remote-guest.log" 2>/dev/null && return 0; sleep 1; done; return 1; }

export RELAY_REMOTE_PAIR_FILE="$work/pair-url.txt"
export RELAY_REMOTE_INVITE_FILE="$work/invite-url.txt"
"$build/relay" --workspace "$work" >"$out/remote-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 14
win=$(for w in $(xdotool search --onlyvisible --pid "$relay_pid" --name "^Relay"); do
        g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)
        wdt=$(echo "$g" | sed -n 's/^WIDTH=//p'); hgt=$(echo "$g" | sed -n 's/^HEIGHT=//p')
        echo "$(( ${wdt:-0} * ${hgt:-0} )) $w"
      done | sort -rn | head -1 | cut -d' ' -f2)
[[ -z $win ]] && { echo "no Relay window"; tail -20 "$out/remote-relay-stderr.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1600 1000 windowfocus "$win"
sleep 1

# One line typed at the desktop, so the file has an owner's entry to sit under the guest's.
t 'printf "the desktop typed this one\n"'; xdotool key Return
sleep 3
shot 01-desktop-line-before-sharing

# Share this pane: the chip at the end of the composer's strip.
xdotool mousemove --window "$win" 1552 965 click 1
sleep 12
dlg=$(xdotool search --onlyvisible --name "Share this pane" | head -1)
[[ -z $dlg ]] && { echo "the share window did not open"; tail -20 "$out/remote-relay-stderr.log"; exit 1; }
xdotool mousemove 602 632 click 1            # the invite role combo
sleep 1
xdotool key --delay 60 Down Return           # Viewer -> Editor
sleep 1
xdotool mousemove 978 632 click 1            # "Make a link"
sleep 5
import -window "$dlg" "$out/remote-02-invite-link.png"
url=$(cat "$work/invite-url.txt" 2>/dev/null)
[[ -z $url ]] && { echo "no invite url"; tail -30 "$out/remote-relay-stderr.log"; exit 1; }
echo "invite url: ${url:0:46}..."
xdotool windowfocus "$dlg"; xdotool key Escape
sleep 2
xdotool windowfocus "$win"

( cd "$root" && python3 "$guest_py" "$url" --name alice --platform Chrome \
    --ask-control --prompt "$guest_prompt" --pause 20 --hold 600 \
    >"$out/remote-guest.log" 2>&1 ) &
guest_pid=$!
waitfor "my code is" || { echo "the guest never knocked"; tail -20 "$out/remote-guest.log"; exit 1; }
sleep 4
shot 03-knock-on-the-sharing-pane
xdotool mousemove --window "$win" 1111 320 click 1     # Admit as editor
sleep 6
waitfor "admitted as" || echo "note: the guest did not report being admitted"
shot 04-admitted-as-editor

waitfor "asked for the keyboard" 60
sleep 3
xdotool mousemove --window "$win" 964 240 click 1      # Let them type
sleep 5
shot 05-alice-is-typing

waitfor "sent a prompt" 90 || { echo "the guest never sent a prompt"; tail -20 "$out/remote-guest.log"; exit 1; }
sleep 3
shot 06-a-guest-prompt-waiting
xdotool mousemove --window "$win" 964 290 click 1      # Approve once
sleep 8
shot 07-prompt-approved

echo "== the history file after the guest's prompt"
cat "$history_file" 2>&1 | sed 's/^/   /'
cp "$history_file" "$out/remote-history-file.txt" 2>/dev/null
if grep -qxF "$guest_prompt" "$history_file" 2>/dev/null; then
    echo "PASS the guest's line is in the prompt history"
else
    echo "FAIL the guest's line is NOT in the prompt history"
fi

# And the desktop's own prompt box offers it: its next browse re-reads the file.
xdotool mousemove --window "$win" 400 925 click 1
sleep 1
xdotool key ctrl+a; xdotool key Delete
sleep 1
shot 08-empty-desktop-prompt-box
xdotool key Up
sleep 1
shot 09-up-offers-the-line-alice-typed
echo "== done"
