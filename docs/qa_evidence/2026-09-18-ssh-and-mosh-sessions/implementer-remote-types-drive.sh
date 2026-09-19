#!/usr/bin/env bash
# The host's folders and the other file types (#S5SH, docs/SSH-AND-MOSH.md section 9).
#
# The companion to implementer-remote-file-drive.sh: that one is one text file, opened, edited and
# saved. This one clicks a *folder* the host printed, walks it in an explorer pane, and opens the
# Markdown, the image and the PDF inside it — then edits the Markdown from its Source view and
# saves it with the **button**, so the shortcut hint that teaches Ctrl+S is photographed too.
#
# Everything it touches is a folder it makes in /tmp on the host and removes afterwards.
#
#   BUILD=/path/to/build QA_DISPLAY=91 ./implementer-remote-types-drive.sh
set -uo pipefail
D=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:?set BUILD to a build directory holding ./relay}
OUT=${OUT:-$D}; mkdir -p "$OUT"
HOSTDIR=/tmp/relay-remote-qa-$$
JAIL=$(mktemp -d)

mkdir -p "$HOSTDIR/sub"
cat >"$HOSTDIR/notes.md" <<'EOF'
# Notes on the host

These lines live on the host, and this pane renders them.

- a list item
- a [link to another file](service.conf) on the same machine

    a code block
EOF
printf 'deeper still\n' >"$HOSTDIR/sub/deeper.txt"
printf 'listen 8080;\n' >"$HOSTDIR/service.conf"
convert -size 200x120 gradient:navy-skyblue "$HOSTDIR/logo.png" 2>/dev/null || \
    printf 'no image tool\n' >"$HOSTDIR/logo.png"
cat >"$HOSTDIR/report.pdf" <<'EOF'
%PDF-1.4
1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj
2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj
3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]>>endobj
trailer<</Root 1 0 R>>
%%EOF
EOF

export RELAY_KEYRING=off HOME="$JAIL/home" XDG_CONFIG_HOME="$JAIL/config" XDG_DATA_HOME="$JAIL/data" \
       XDG_CACHE_HOME="$JAIL/cache" XDG_RUNTIME_DIR="$JAIL/run" TMPDIR="$JAIL/tmp"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$JAIL/work" "$TMPDIR"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
printf '[instructions]\nonboarded=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
export DISPLAY=:${QA_DISPLAY:-91}

Xvfb "$DISPLAY" -screen 0 1400x900x24 >/dev/null 2>&1 & XVFB=$!
cleanup() {
    kill "${APP:-0}" "$XVFB" 2>/dev/null
    rm -rf "$JAIL" "$HOSTDIR"
}
trap cleanup EXIT
sleep 2
cd "$JAIL/work"; "$BUILD/relay" >"$OUT/implementer-remote-types-stdout.txt" 2>&1 & APP=$!
sleep 8

shot() { import -window root "$OUT/implementer-remote-$1.png"; }
type_() { xdotool type --delay 15 "$1"; sleep 0.4; }
run_() { type_ "$1"; xdotool key Return; sleep "${2:-2}"; }
click() { xdotool mousemove "$1" "$2"; sleep 1.2; xdotool click 1; sleep "${3:-2}"; }

run_ "ssh localhost" 7
run_ "ls -d $HOSTDIR" 3

# The folder the host printed: hovered until the host vouches for it, then clicked. With two
# panes open the explorer takes the right half; rows start at y=171, 26 pixels apart.
click 200 377 4
shot 08-folder-on-the-host

# notes.md, in the explorer's third row. Opening it puts a third pane in the window, after which
# the explorer's rows are at x=530 and the preview's header buttons at the top right.
click 900 223 4
shot 09-markdown-rendered

# "Source (MD)" is where a rendered document is edited, exactly as locally; then the Save
# *button* — the slow path, which is what the shortcut hint is for.
xdotool mousemove 986 62; sleep 0.5; xdotool click 1; sleep 1.5
xdotool mousemove 950 300; sleep 0.5; xdotool click 1; sleep 0.5
xdotool key ctrl+End; xdotool key Return; type_ "A line typed in Relay, over ssh."; sleep 0.5
shot 10-markdown-source-edited
# The wait is the hint registry's doing, not the pane's: clicking from one pane into another has
# just taught "Alt+← / Alt+→ moves between panes", and no second hint may follow inside the
# global 20-second gap. A user who has not just been taught something sees this one at once.
sleep 22
xdotool mousemove 878 62; sleep 0.5; xdotool click 1; sleep 4
shot 11-saved-and-the-hint

# The image and the PDF from the same folder, in the same pane.
click 530 197 4
shot 12-image-from-the-host
click 530 249 4
shot 13-pdf-from-the-host

# Into a subfolder, and back up with the ↑ button.
click 530 171 4
shot 14-into-a-subfolder
xdotool mousemove 450 62; sleep 0.5; xdotool click 1; sleep 4
shot 15-back-up-again

echo "--- notes.md on the host, after the save ---"
cat "$HOSTDIR/notes.md"
