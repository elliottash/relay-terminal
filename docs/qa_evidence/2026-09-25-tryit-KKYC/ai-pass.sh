#!/usr/bin/env bash
# The mechanical pass over the staged #KKYC situation: launch the staged fixture under Xvfb on an
# isolated profile and drive the four mouse chords on the folder and file links, asserting each
# outcome through the pane list (relay-drive), a `pwd` marker the shell writes at every prompt, a
# logging xdg-open shim, and OCR of the menus and window. Click coordinates come from tesseract
# TSV boxes of the idle screen, not guesses.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=/home/elliott/.cache/relay/scratch/tryit/kkyc-clicks
proj="$root/project"; sandbox="$root/sandbox"; state="$root/state"
repo=$(cd "$here/../../.." && pwd)
driver="$repo/scripts/relay-drive"
bin="$repo/build/relay"
out="$here/ai-pass"; rm -rf "$out"; mkdir -p "$out"

bash "$here/stage.sh" > "$out/stage.log"
rm -f "$state/cwd" "$state/xdg-open.log"

width=1640 height=1040
display=; for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done; sleep 1
           pkill -TERM -f '[d]bus-run-session' 2>/dev/null || true; }
trap cleanup EXIT
Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
export HOME="$sandbox/home" XDG_RUNTIME_DIR="$sandbox/run" TMPDIR="$sandbox/tmp" \
       XDG_CONFIG_HOME="$sandbox/home/.config" XDG_DATA_HOME="$sandbox/home/.local/share" \
       XDG_CACHE_HOME="$sandbox/home/.cache" RELAY_DATA_DIR="$sandbox/data" \
       RELAY_SHELL_INTEGRATION=1
export PATH="$sandbox/bin:$PATH" KKYC_PROJECT="$proj" KKYC_STATE="$state" KKYC_START="$root/start" RELAY_SHELL_INTEGRATION=1
(cd "$proj" && exec dbus-run-session -- "$bin" --clean-shell --fresh) > "$out/relay.log" 2>&1 & relay_pid=$!

shot() { import -window root "$out/$1.png"; }
ocr()  { tesseract "$out/$1.png" - 2>/dev/null || true; }
panes() { XDG_RUNTIME_DIR="$sandbox/run" "$driver" panes 2>/dev/null \
            | python3 -c 'import json,sys; d=json.load(sys.stdin); print(" ".join(p.get("title","?") for p in d.get("panes",[])))'; }

# 1. The listing is on screen; read the link rows' pixel boxes from it.
for i in $(seq 1 30); do [[ -f "$state/cwd" ]] && break; sleep 1; done
[[ -f "$state/cwd" ]] || { echo "FAIL: shell never printed the listing"; exit 1; }
shot 00-listing

# The link rows' pixel boxes, re-read before every step: docking panes re-flow the terminal.
# A plain click on blank space first collapses any selection a menu step left behind, which
# would otherwise render the rows highlighted and unreadable to OCR. x=1000, y=600 is blank:
# the listing text ends near x=740 and the prompt sits at the bottom.
settle() { xdotool mousemove 1000 600; xdotool click 1; sleep 0.5; }
locate() {  # locate <prefix> -> prints "x y" for the folder link, retrying through redraws
    for try in 1 2 3; do
        shot "$1"
        if tesseract "$out/$1.png" stdout tsv 2>/dev/null | python3 -c "
import csv, sys
folder = None
for r in csv.reader(sys.stdin, delimiter='\t'):
    if len(r) == 12 and r[11].strip() and all(c.isdigit() for c in (r[6], r[7], r[8], r[9])):
        x, y, w, h = int(r[6]), int(r[7]), int(r[8]), int(r[9])
        if 'reports' in r[11] and 150 < y < 700 and folder is None: folder = (x + 5, y + h // 2)
if not folder: sys.exit(1)
print(folder[0], folder[1])
"; then return 0; fi
        sleep 1
    done
    echo "FAIL: folder link not found on screen" >&2; exit 1
}
locate_file() {  # the file link's box, same way; 'notes' is unique to the listing line
    for try in 1 2 3; do
        shot "$1"
        if tesseract "$out/$1.png" stdout tsv 2>/dev/null | python3 -c "
import csv, sys
file = None
for r in csv.reader(sys.stdin, delimiter='\t'):
    if len(r) == 12 and r[11].strip() and all(c.isdigit() for c in (r[6], r[7], r[8], r[9])):
        x, y, w, h = int(r[6]), int(r[7]), int(r[8]), int(r[9])
        if 'notes' in r[11] and 150 < y < 700 and file is None: file = (x + 5, y + h // 2)
if not file: sys.exit(1)
print(file[0], file[1])
"; then return 0; fi
        sleep 1
    done
    echo "FAIL: file link not found on screen" >&2; exit 1
}

# 2. Left click on the folder opens the explorer pane, and closing it restores the layout.
settle; read -r folder_x folder_y < <(locate 01-locate)
xdotool mousemove "$folder_x" "$folder_y"; xdotool click 1; sleep 3
shot 01-folder-left; panes > "$out/01-panes.txt"
ocr 01-folder-left | grep -qiE "explorer|budget\.csv|notes\.md|date modified" \
    || { echo "FAIL: left click on the folder opened no explorer"; exit 1; }
xdotool key ctrl+w; sleep 2   # put the layout back before the next chord

# 3. Ctrl+click on the folder opens the folder click menu at the pointer.
settle; read -r folder_x folder_y < <(locate 02-locate)
xdotool mousemove "$folder_x" "$folder_y"; xdotool keydown ctrl; xdotool click 1; xdotool keyup ctrl; sleep 1.5
shot 02-folder-ctrl-menu
ocr 02-folder-ctrl-menu | grep -qiE "open.?in.?explorer|navigate here" \
    || { echo "FAIL: ctrl+click menu missing (no 'Open in explorer')"; exit 1; }
xdotool key Escape; sleep 1

# 4. Alt+click navigates the pane's shell into the folder.
before=$(cat "$state/cwd")
settle; read -r folder_x folder_y < <(locate 03-locate)
xdotool mousemove "$folder_x" "$folder_y"; xdotool keydown alt; xdotool click 1; xdotool keyup alt; sleep 4
after=$(cat "$state/cwd"); shot 03-folder-alt
[[ "$after" == "$proj/reports" && "$after" != "$before" ]] \
    || { echo "FAIL: alt+click did not cd into reports ('$before' -> '$after')"; exit 1; }

# 5. Shift+click hands the folder to the system file manager (the shim logs it).
: > "$state/xdg-open.log"
settle; read -r folder_x folder_y < <(locate 04-locate)
xdotool mousemove "$folder_x" "$folder_y"; xdotool keydown shift; xdotool click 1; xdotool keyup shift; sleep 3
shot 04-folder-shift
grep -q "project/reports" "$state/xdg-open.log" \
    || { echo "FAIL: shift+click did not reach xdg-open ($(cat "$state/xdg-open.log" 2>/dev/null))"; exit 1; }

# 6. Right-click opens the same folder menu.
settle; read -r folder_x folder_y < <(locate 05-locate)
xdotool mousemove "$folder_x" "$folder_y"; xdotool click 3; sleep 1.5
shot 05-folder-right-menu
ocr 05-folder-right-menu | grep -qiE "copy.?path" || { echo "FAIL: right-click menu missing Copy path"; exit 1; }
xdotool key Escape; sleep 1

# 7. The file mirrors the modifiers: left opens it in Relay.
settle; read -r file_x file_y < <(locate_file 06-locate)
xdotool mousemove "$file_x" "$file_y"; xdotool click 1; sleep 3
shot 06-file-left
ocr 06-file-left | grep -q "notes.md" || { echo "FAIL: left click on the file opened nothing"; exit 1; }
xdotool key ctrl+w; sleep 2

# 8. Ctrl+click on the file opens the file menu, with Edit inside.
settle; read -r file_x file_y < <(locate_file 07-locate)
xdotool mousemove "$file_x" "$file_y"; xdotool keydown ctrl; xdotool click 1; xdotool keyup ctrl; sleep 1.5
shot 07-file-ctrl-menu
# "Edit" often OCRs as noise against the terminal behind it; "Navigate to its folder" is the
# entry only the file menu has (the folder menu says "Navigate here"), so it names the menu.
ocr 07-file-ctrl-menu | grep -qiE "navigate.?to.?its.?folder" \
    || { echo "FAIL: file menu missing (no 'Navigate to its folder')"; exit 1; }
xdotool key Escape; sleep 1

# 9. Alt+click on the file cds the shell to its folder.
before=$(cat "$state/cwd")
settle; read -r file_x file_y < <(locate_file 08-locate)
xdotool mousemove "$file_x" "$file_y"; xdotool keydown alt; xdotool click 1; xdotool keyup alt; sleep 4
after=$(cat "$state/cwd"); shot 08-file-alt
[[ "$after" == "$proj" && "$after" != "$before" ]] \
    || { echo "FAIL: alt+click on the file did not cd to its folder ('$before' -> '$after')"; exit 1; }

# 10. Shift+click hands the file to the default app.
: > "$state/xdg-open.log"
settle; read -r file_x file_y < <(locate_file 09-locate)
xdotool mousemove "$file_x" "$file_y"; xdotool keydown shift; xdotool click 1; xdotool keyup shift; sleep 3
shot 09-file-shift
grep -q "notes.md" "$state/xdg-open.log" \
    || { echo "FAIL: shift+click on the file did not reach xdg-open"; exit 1; }

echo "ai-pass: all ten chord outcomes observed"
