#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Implementer evidence driver for image context (issue EM1E). Runs Relay under Xvfb with an
# isolated HOME/XDG_CONFIG_HOME/XDG_DATA_HOME/XDG_CACHE_HOME, exercises the palette route to
# "Screenshot this pane", pasting an image into the prompt box, one real agent turn carrying that
# image, and the Vision model row in Settings › Models. Screenshots land beside this script.
#
# The keyring is left ENABLED so the pane runs on the stored glm-coding key: the point of the run is
# that a real image turn swaps to glm-5.3-flash and back. No key is printed or captured anywhere.
#
# Usage: docs/qa_evidence/2026-09-17-image-context/implementer-driver.sh [repo root] [build dir]
set -euo pipefail

ROOT=${1:-$(cd "$(dirname "$0")/../../.." && pwd)}
BUILD=${2:-$ROOT/build}
OUT=$(cd "$(dirname "$0")" && pwd)
JAIL=$(mktemp -d)

export HOME="$JAIL/home"
export XDG_CONFIG_HOME="$JAIL/config"
export XDG_DATA_HOME="$JAIL/data"
export XDG_CACHE_HOME="$JAIL/cache"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$JAIL/work"
export DISPLAY=:97

# Start configured, so the run is about images and not about onboarding.
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[provider]
preset=glm-coding

[instructions]
onboarded=true

[hints]
enabled=true
CONF

Xvfb "$DISPLAY" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB=$!
cleanup() { kill "${APP:-0}" "${CLIP:-0}" "$XVFB" 2>/dev/null || true; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 2

# The picture to send: a plain red square, small enough that the live turn costs almost nothing.
python3 - "$JAIL/work/red-square.png" <<'PY'
import struct, sys, zlib
w = h = 96
rows = b"".join(b"\x00" + bytes([220, 40, 40] * w) for _ in range(h))
def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
open(sys.argv[1], "wb").write(
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    + chunk(b"IDAT", zlib.compress(rows))
    + chunk(b"IEND", b""))
PY

cd "$JAIL/work"
"$BUILD/relay" >"$JAIL/relay-stdout.txt" 2>&1 &
APP=$!
sleep 7

shot() { import -window root "$OUT/implementer-$1.png"; sleep 1; }
type_() { xdotool type --delay 30 "$1"; sleep 1; }
clear_box() { xdotool key ctrl+a; xdotool key BackSpace; sleep 1; }

shot 01-startup

# 1. The palette knows the action and shows its shortcut.
xdotool key ctrl+shift+a; sleep 2; type_ "screenshot"; shot 02-palette-screenshot-action

# 2. Running it from the palette is the slow path: it attaches the capture and hints Ctrl+Shift+G.
xdotool key Return; sleep 3
shot 03-pane-screenshot-attached

# 3. Paste: the PNG goes on the clipboard as image data, Ctrl+V attaches it.
clear_box
python3 - "$JAIL/work/red-square.png" <<'PY' &
import sys
from PyQt5.QtWidgets import QApplication
from PyQt5.QtGui import QImage
app = QApplication([])
app.clipboard().setImage(QImage(sys.argv[1]))
app.exec_()
PY
CLIP=$!
sleep 4
xdotool key ctrl+v; sleep 2
shot 04-pasted-image

# 4. One real agent turn carrying that image: Ctrl+Enter forces the agent.
type_ "Look at the attached image and answer with one word only, running no commands: what colour fills it?"
shot 05-prompt-ready
xdotool key ctrl+Return; sleep 8
shot 06-image-turn-running
sleep 25
shot 07-image-turn-answer

# 5. The Vision model row, beside the Main/Flash/Lite tiers.
xdotool key ctrl+shift+a; sleep 2; type_ "model roles"; sleep 1
xdotool key Return; sleep 4
shot 08-vision-model-row

cp "$XDG_DATA_HOME/relay/logs/relay.log" "$OUT/implementer-relay.log" 2>/dev/null || true
echo "screenshots in $OUT"
