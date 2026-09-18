#!/usr/bin/env bash
# Scrollback on the phone (#W5N2), live: share a real Relay pane that has scrolled a long way,
# pair a headless browser with it, drag the terminal down and read what scrolled away.
#
#   docs/qa_evidence/2026-09-18-remote-scrollback/drive.sh [build-dir]
#
# Writes desktop-NN-*.png and phone-NN-*.png next to this script, plus relay-stderr.log and
# phone.log. Isolated XDG_CONFIG_HOME/XDG_DATA_HOME/XDG_CACHE_HOME *and* XDG_RUNTIME_DIR and
# TMPDIR: a live Relay on this machine shares the runtime dir, and its sockets and temp files
# would otherwise be picked up by this run.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
rm -f "$out"/settled.flag "$out"/printed.flag

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
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"' EXIT

Xvfb "$display" -screen 0 1500x950x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window "$win" "$out/desktop-$1.png"; }
t() { xdotool type --delay 8 "$1"; }
k() { xdotool key --delay 40 "$@"; }

export RELAY_REMOTE_PAIR_FILE="$work/pair-url.txt"
"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
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

# Four hundred numbered lines, so there is a long way to scroll back and every row says where it
# came from. Every fifth one is bold and red, to prove the phone paints history with the run
# painter it uses for the live screen rather than as plain text.
t 'for i in $(seq 1 400); do if [ $((i % 5)) = 0 ]; then printf "\033[1;31mscrollback-%s\033[0m\n" "$i"; else printf "scrollback-%s\n" "$i"; fi; done'
k Return
sleep 6
shot 01-pane-with-scrollback

# Share this pane: the share chip is the last button in the composer's strip.
xdotool mousemove --window "$win" 1452 914 click 1
sleep 10
shot 02-after-clicking-share

url=$(cat "$work/pair-url.txt" 2>/dev/null)
[[ -z $url ]] && { echo "no pairing url; the sidecar did not start"; tail -20 "$out/relay-stderr.log"; exit 1; }
echo "pairing url: ${url:0:60}..."

( cd "$root" && python3 "$out/browser_scrollback.py" "$url" >"$out/phone.log" 2>&1 ) &
phone_pid=$!

for i in $(seq 1 60); do
  grep -q "phone shows code" "$out/phone.log" 2>/dev/null && break
  sleep 1
done
sleep 3
dlg=$(xdotool search --onlyvisible --name "Share this pane" | head -1)
if [[ -n $dlg ]]; then
  import -window "$dlg" "$out/desktop-03-confirm-code.png"
  xdotool windowfocus "$dlg"; sleep 1
  # Refuse holds the focus. Tab once for "Allow viewing": reading scrollback is watching, and
  # this run is also the check that a `view` device gets it.
  k Tab space
fi
sleep 2
[[ -n $dlg ]] && xdotool windowmove "$dlg" 2000 2000

# When the phone says it has settled up in the scrollback, print on the desktop and see whether
# anything moves over there.
for i in $(seq 1 120); do [[ -f $out/settled.flag ]] && break; sleep 1; done
if [[ -f $out/settled.flag ]]; then
  xdotool windowfocus "$win"; sleep 1
  t 'printf "printed-while-the-phone-was-reading\n"'; k Return
  sleep 3
  shot 04-desktop-printed-more
  touch "$out/printed.flag"
fi

wait $phone_pid
echo "--- what the phone saw ---"
cat "$out/phone.log"
rm -f "$out"/settled.flag "$out"/printed.flag
echo "screenshots in $out"
