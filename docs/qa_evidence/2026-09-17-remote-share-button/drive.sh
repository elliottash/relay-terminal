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

# The share button is the chip right after the microphone in the composer's strip.
share=$(xdotool search --onlyvisible --name "^Relay" >/dev/null; echo)
python3 - "$win" <<'PY'
# Find the share button by its accessible name is not available through xdotool, so click by
# geometry: the strip sits at the bottom right of the composer. The button order is
# ... model picker, mic, share ... and both chips are 14px icons in a 6px-spaced row.
import subprocess, sys
win = sys.argv[1]
geometry = subprocess.run(["xdotool", "getwindowgeometry", "--shell", win],
                          capture_output=True, text=True).stdout
values = dict(line.split("=", 1) for line in geometry.strip().splitlines())
width, height = int(values["WIDTH"]), int(values["HEIGHT"])
print(f"window {width}x{height}")
PY
echo "clicking the share chip"
# The strip's right edge holds the interrupt button (hidden), then share, then mic.
xdotool mousemove --window "$win" 1416 872 click 1
sleep 6
shot 02-share-dialog

# The dialog prints the URL; read it from the sidecar's own view of things instead of OCR.
url=$(grep -ao 'https\?://[^ "]*#v=1[^ "]*' "$out/relay-stderr.log" | tail -1)
echo "pairing url from the log: ${url:-<none>}"

# Pair a headless browser with it and type into the real pane.
if [[ -n $url ]]; then
  ( cd "$root" && python3 - "$url" <<'PY' >"$out/pair.log" 2>&1
import asyncio, sys
sys.path.insert(0, ".")
from tests.browser import Browser

async def main():
    url = sys.argv[1]
    browser = Browser()
    await browser.start()
    try:
        await browser.navigate(url)
        await browser.wait_for("!document.getElementById('screen-inbox').hidden", timeout=60)
        print("paired")
        await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        await browser.wait_for("!document.getElementById('terminal-pane').hidden", timeout=30)
        await browser.wait_for("document.querySelectorAll('.screen-row').length > 1", timeout=30)
        print("screen:", (await browser.evaluate(
            "document.querySelector('.screen-grid').textContent"))[:200])
        await browser.evaluate("document.getElementById('term-take').click()")
        await browser.wait_for("!document.getElementById('term-composer').hidden", timeout=20)
        await browser.evaluate(
            "(() => { const b = document.getElementById('term-line');"
            " b.value = 'printf \"typed from the phone\\\\n\"';"
            " document.getElementById('term-send').click(); return true; })()")
        await asyncio.sleep(3)
        print("done")
    finally:
        await browser.stop()

asyncio.run(asyncio.wait_for(main(), 180))
PY
  )
fi
sleep 2
shot 03-after-phone-typed
echo "screenshots in $out"
