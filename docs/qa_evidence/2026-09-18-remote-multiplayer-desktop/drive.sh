#!/usr/bin/env bash
# Multiplayer, the owner's side (#W5N2), live: share a real pane, make an invite link from the
# share window, have a real guest knock with it, watch the Sharing pane open by itself without
# taking the keyboard, admit them as an editor, let them type, approve the prompt they send, and
# take the keyboard back by typing.
#
#   docs/qa_evidence/2026-09-18-remote-multiplayer-desktop/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script, plus relay-stderr.log and guest.log.
#
# Isolation: XDG_CONFIG_HOME, XDG_DATA_HOME and XDG_CACHE_HOME keep the run out of the real profile
# (the remote identity key, devices.json and guests.json all live under XDG_DATA_HOME), and
# XDG_RUNTIME_DIR and TMPDIR are isolated too -- without those two a Relay already running on this
# machine shares sockets and temp files with the run and its saves look broken.
#
# The clicks are at fixed points in a 1600x1000 window, which is why the window is resized to
# exactly that and every click says what it is aiming at. If the layout moves, the screenshots are
# what show it: read them, do not trust the exit code.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}

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
CONF
trap 'kill "${relay_pid:-0}" "${guest_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null;
      rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"' EXIT

Xvfb "$display" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window "$win" "$out/implementer-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
waitfor() { for _ in $(seq 1 "${2:-40}"); do grep -q "$1" "$out/guest.log" 2>/dev/null && return 0; sleep 1; done; return 1; }

# The pairing and invite links carry one-time secrets and are never logged; Relay writes each out
# only when the matching QA-only variable names a file (RemoteShare::handle).
export RELAY_REMOTE_PAIR_FILE="$work/pair-url.txt"
export RELAY_REMOTE_INVITE_FILE="$work/invite-url.txt"
"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 14
win=$(for w in $(xdotool search --onlyvisible --pid "$relay_pid" --name "^Relay"); do
        g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)
        wdt=$(echo "$g" | sed -n 's/^WIDTH=//p'); hgt=$(echo "$g" | sed -n 's/^HEIGHT=//p')
        echo "$(( ${wdt:-0} * ${hgt:-0} )) $w"
      done | sort -rn | head -1 | cut -d' ' -f2)
[[ -z $win ]] && { echo "no Relay window"; tail -20 "$out/relay-stderr.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1600 1000 windowfocus "$win"
sleep 1

# Something on screen worth sharing.
t 'printf "the pane alice is about to join\n"'; xdotool key Return
sleep 2
shot 01-pane-before-sharing

# Share this pane: the chip at the end of the composer's strip, right after the microphone.
# (The palette's "Share this pane..." runs the same action.)
xdotool mousemove --window "$win" 1552 965 click 1
sleep 12
dlg=$(xdotool search --onlyvisible --name "Share this pane" | head -1)
[[ -z $dlg ]] && { echo "the share window did not open"; tail -20 "$out/relay-stderr.log"; exit 1; }
import -window "$dlg" "$out/implementer-02-share-window-pairing-first.png"

# The invite row is under the pairing QR: role, expiry, uses, "Make a link". Editor, so the knock
# row can offer both "Admit as viewer" and "Admit as editor" -- a viewer-only invite shows only the
# first, which is the rule that admitting may lower a role and never raise it.
xdotool mousemove 602 632 click 1            # the role combo
sleep 1
xdotool key --delay 60 Down Return           # Viewer -> Editor
sleep 1
import -window "$dlg" "$out/implementer-03-invite-role-editor.png"
xdotool mousemove 978 632 click 1            # "Make a link"
sleep 5
import -window "$dlg" "$out/implementer-04-invite-link-and-qr.png"
url=$(cat "$work/invite-url.txt" 2>/dev/null)
echo "invite url: ${url:0:46}${url:+...}"
[[ -z $url ]] && { echo "no invite url"; tail -30 "$out/relay-stderr.log"; exit 1; }

# Put the share window away (Esc) and leave the keyboard in the prompt box, so what happens to the
# focus when somebody knocks is something this run can actually see.
xdotool windowfocus "$dlg"; xdotool key Escape
sleep 2
xdotool windowfocus "$win"; xdotool mousemove --window "$win" 400 925 click 1
sleep 1
t 'the keyboard is here before the knock'
sleep 1
shot 05-typing-before-the-knock

# A real guest knocks with the link. remote/client.py is the protocol's own client, so this run
# does not depend on the web client. It asks for the keyboard 40 s after being admitted and sends
# a prompt 40 s after that, so each waiting row can be looked at and answered on its own.
( cd "$root" && python3 "$out/guest.py" "$url" --name alice --platform Chrome \
    --ask-control --prompt "run the tests and tell me what broke" --pause 40 --hold 900 \
    >"$out/guest.log" 2>&1 ) &
guest_pid=$!
waitfor "my code is"
guest_code=$(sed -n 's/^guest: my code is //p' "$out/guest.log" | head -1)
echo "the guest's code is $guest_code -- it must be the number on the knock row"
sleep 4
shot 06-knock-opened-the-sharing-pane
# The pane arrived on its own. Keep typing: the text must still be going to the prompt box, because
# the next keystroke would otherwise land on a button that admits a stranger.
t ' and it still is'
sleep 1
shot 07-focus-was-not-stolen

# Admit as editor: Refuse, Admit as viewer, Admit as editor, in that order, with Refuse holding
# the focus so that Return refuses.
xdotool mousemove --window "$win" 1111 320 click 1
sleep 6
shot 08-admitted-as-editor
waitfor "admitted as"

# "alice asks to type in this pane": Refuse, Let them type.
waitfor "asked for the keyboard" 60
sleep 3
shot 09-asked-for-the-keyboard
xdotool mousemove --window "$win" 964 240 click 1
sleep 5
shot 10-alice-is-typing

# "alice wrote a prompt for this pane's agent": Refuse, Approve once.
waitfor "sent a prompt" 90
sleep 3
shot 11-a-guest-prompt-waiting
xdotool mousemove --window "$win" 964 290 click 1
sleep 6
shot 12-prompt-approved

# The owner types in a pane a guest is driving: control comes straight back and the key is not
# swallowed (docs/REMOTE-PROTOCOL.md section 10.3).
xdotool windowfocus "$win"; xdotool mousemove --window "$win" 400 925 click 1
sleep 1
xdotool key ctrl+a                           # replace what is in the box, so the line reads clean
t 'typing here takes the keyboard back'
sleep 3
shot 13-typing-took-control-back

echo "--- what the guest saw ---"
cat "$out/guest.log"
echo "screenshots in $out"
