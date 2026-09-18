#!/usr/bin/env bash
# The share button (#W5N2), live: click it in an engine pane, get a QR, pair a headless browser
# with it, and type from that browser into the real pane.
#
#   docs/qa_evidence/2026-09-17-remote-share-button/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script plus relay-stderr.log and pair.log.
# An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the real profile — including the
# remote identity key and the paired-device list, which live under XDG_DATA_HOME/relay/remote.
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
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

Xvfb "$display" -screen 0 1500x950x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window "$win" "$out/implementer-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }

# The pairing URL holds a one-time secret, so Relay writes it out only when asked to.
export RELAY_REMOTE_PAIR_FILE="$work/pair-url.txt"
"$build/relay" --engine=relay --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(for w in $(xdotool search --onlyvisible --pid "$relay_pid" --name "^Relay"); do
        g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)
        wdt=$(echo "$g" | sed -n 's/^WIDTH=//p'); hgt=$(echo "$g" | sed -n 's/^HEIGHT=//p')
        echo "$(( ${wdt:-0} * ${hgt:-0} )) $w"
      done | sort -rn | head -1 | cut -d' ' -f2)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 950 windowfocus "$win"
sleep 1

# Something on screen to look at from the phone.
t 'printf "shared from the desktop\\n"'; k Return
sleep 2
shot 01-pane-before-sharing

# Share this pane by clicking the share chip, the last button in the composer's strip, right
# after the microphone. (The palette action "Share this pane with a phone" does the same thing.)
shot 02-strip-with-share-button
xdotool mousemove --window "$win" 1452 914 click 1
sleep 10
shot 03-after-clicking-share

# The dialog is its own window; capture it so the shot shows the QR, not the pane behind it.
dlg=$(xdotool search --onlyvisible --name "Share this pane" | head -1)
[[ -n $dlg ]] && import -window "$dlg" "$out/implementer-04-qr-dialog.png"

url=$(cat "$work/pair-url.txt" 2>/dev/null)
echo "pairing url: ${url:0:60}${url:+...}"
[[ -z $url ]] && { echo "no pairing url; the sidecar did not start"; tail -20 "$out/relay-stderr.log"; exit 1; }

# Pair a headless browser and type into the real pane. Pairing needs the person at the desktop to
# allow the device, so the browser runs in the background while the script answers the dialog.
( cd "$root" && python3 "$out/browser_pair.py" "$url" >"$out/pair.log" 2>&1 ) &
pair_pid=$!

for i in $(seq 1 40); do
  grep -q "phone shows code" "$out/pair.log" 2>/dev/null && break
  sleep 1
done
sleep 3
dlg=$(xdotool search --onlyvisible --name "Share this pane" | head -1)
if [[ -n $dlg ]]; then
  import -window "$dlg" "$out/implementer-05-confirm-code.png"
  xdotool windowfocus "$dlg"; sleep 1
  # Allowing is deliberate: Refuse holds the focus, so Return would turn the phone away. The
  # buttons are Refuse, Allow viewing, Allow typing — this run wants the last one.
  k Tab Tab space
fi
sleep 2
[[ -n $dlg ]] && import -window "$dlg" "$out/implementer-06-paired.png"

wait $pair_pid
# Move the dialog out of the way so the last shot is the pane itself.
[[ -n $dlg ]] && xdotool windowmove "$dlg" 2000 2000
sleep 2
shot 07-pane-after-phone-typed
echo "--- what the browser saw ---"
cat "$out/pair.log"
echo "screenshots in $out"
