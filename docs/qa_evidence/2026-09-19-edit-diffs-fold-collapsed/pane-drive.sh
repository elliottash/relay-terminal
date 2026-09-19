#!/usr/bin/env bash
# Live check of #WXT6 (edit diffs fold under the row, collapsed by default), under Xvfb + xdotool.
# Reuses the #TK9C loopback stub (pane-stub-provider.py, copied unchanged): one turn runs a python
# heredoc, a short command, a failing command, four reads, a SMALL edit (alpha.py, +2 −1), a new
# file and a BIG edit (big.txt, +60 −60).
#
# This run checks three things, and finds its rows by OCR (tesseract) rather than counted pixels,
# so it survives a row layout change:
#   01  after the turn, every row is folded — alpha.py's diff is NOT printed under its row
#   02  a Ctrl+click on the alpha.py row unfolds the diff in place (served from the stored diff)
#   03  a Ctrl+click on the big.txt row still opens the diff pane
#
#   docs/qa_evidence/2026-09-19-edit-diffs-fold-collapsed/pane-drive.sh <build-dir> [tag]
#
# Writes <tag>-NN-*.png and run-<tag>.log next to this script. Needs Xvfb, xdotool, ImageMagick
# `import` and tesseract. Every XDG/HOME/TMPDIR path is isolated, so a Relay already running on
# this machine is untouched. The agent talks to the stub on 127.0.0.1 only: no network, no keys.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
build=$1
tag=${2:-run}
port=8741
log=run-$tag.log
: >"$log"
say() { echo "$*" | tee -a "$log"; }

display=
for n in $(seq 161 199); do
    [[ -e /tmp/.X11-unix/X$n ]] && continue
    display=:$n
    break
done
[[ -z $display ]] && { say "no free X display in 161..199"; exit 1; }

sandbox=$(mktemp -d)
export HOME=$sandbox/home
export XDG_CONFIG_HOME=$sandbox/config XDG_DATA_HOME=$sandbox/data XDG_CACHE_HOME=$sandbox/cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
# Relay Free must be unusable in this run, or the pane auto-configures to the hosted service before
# the first submission and never shows the key dialog. `available()` is an import probe for
# python3-cryptography (relay_core/hosted.py): a PYTHONPATH package that raises on import turns it
# off without touching the machine's own python.
mkdir -p "$sandbox/fakepy/cryptography"
echo 'raise ImportError("qa sandbox: Relay Free off for this run")' >"$sandbox/fakepy/cryptography/__init__.py"
export PYTHONPATH=$sandbox/fakepy
export RELAY_KEYRING=off RELAY_NO_URL_HANDLER=1
work=$sandbox/work
mkdir -p "$work"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[agent]
show_tool_output=false
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=relay-qa-stub
extra={}
max_tokens=1024
CONF

printf 'SMALL = 1\nkeep = True\n' >"$work/alpha.py"
for f in beta gamma delta; do printf '# %s\n%s\n' "$f" "$(seq 1 40)" >"$work/$f.py"; done
seq 1 80 | sed 's/^/line /' >"$work/big.txt"
printf 'nothing to find here\n' >"$work/notes.txt"

trap 'kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; rm -rf "$sandbox"' EXIT

python3 "$out/pane-stub-provider.py" "$port" &
stub_pid=$!
sleep 1

Xvfb "$display" -screen 0 1500x900x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { say "Xvfb died on $display (taken?)"; exit 1; }
export DISPLAY=$display
xdotool search --name . 2>/dev/null | grep -q . && { say "$display already has windows"; exit 1; }

shot() {
    import -window root "$out/$tag-$1.png"
    tesseract "$out/$tag-$1.png" stdout 2>/dev/null >>"$log"
    say "--- shot $1 above ---"
    kill -0 "${relay_pid:-0}" 2>/dev/null || say "relay is gone before $1"
}
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 45 "$@"; }

# The y centre of the row a word sits on, from tesseract TSV: $1 png, $2 word.
rowof() {
    tesseract "$1" stdout tsv 2>/dev/null | awk -F'\t' -v w="$2" '
        $12 == w && $11 > 40 { print $8 + $10 / 2; exit }'
}
# Ctrl+click a row (the OSC 8 anchor covers the whole row).
clickrow() {
    xdotool mousemove --sync 120 "$1"; sleep 0.4
    xdotool keydown ctrl; xdotool click 1; xdotool keyup ctrl; sleep "${2:-3}"
}

"$build/relay" --engine-core libvterm --workspace "$work" >"$out/relay-$tag-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { say "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 900
xdotool windowfocus "$win"
xdotool mousemove 750 450
sleep 2

# ---- configure against the loopback stub: the dialog sees a local model server --------------
# provider/base is a 127.0.0.1 URL, so the dialog disables the key and the consent box on its own
# (Pane.h isLocalServer); all this walk does is click Save, found by OCR.
xdotool windowfocus "$win"
t 'walk through the tools'; sleep 0.5
k ctrl+Return; sleep 5
dlg=$(xdotool search --name 'Bring your own key' | tail -1)
[[ -z $dlg ]] && { say "the key dialog did not open"; exit 1; }
import -window "$dlg" "$out/$tag-dlg.png"
say "--- the dialog ---"
tesseract "$out/$tag-dlg.png" stdout 2>/dev/null | tee -a "$log"
# Save's own word is not always OCR'd (a button with a focus ring), but Cancel beside it usually
# is, and both sit in the bottom-right QDialogButtonBox. Fallback: the corner itself — the box's
# geometry comes from xdotool, the 2026-09-18 run of this dialog had Save at WIDTH-153, HEIGHT-19.
eval "$(xdotool getwindowgeometry --shell "$dlg")"
# The OCR'd image is the dialog alone, so its coordinates are dialog-relative; add the window's
# own origin to click on the screen.
savexy=$(tesseract "$out/$tag-dlg.png" stdout tsv 2>/dev/null | awk -F'\t' '$12 == "Cancel" && $11 > 40 { x = $7 + $9 / 2; y = $8 + $10 / 2 } END { if (x) printf "%d %d", x - 60, y }')
[[ -n $savexy ]] && savexy="$((X + ${savexy%% *})) $((Y + ${savexy##* }))"
[[ -z $savexy ]] && savexy="$((X + WIDTH - 153)) $((Y + HEIGHT - 19))"
say "Save at $savexy (dialog ${WIDTH}x${HEIGHT} at $X,$Y)"
xdotool windowfocus "$dlg"; sleep 0.5
xdotool mousemove --sync $savexy; sleep 0.3; xdotool click --delay 120 1; sleep 20
shot 01-agent-ready

# ---- the turn ------------------------------------------------------------------------------
xdotool windowfocus "$win"
xdotool mousemove 750 700
t 'walk through the tools'; sleep 0.5
k ctrl+Return
sleep 40
shot 02-rows-folded

# ---- the small edit's row: click, and the diff unfolds from the stored diff ----------------
y=$(rowof "$out/$tag-02-rows-folded.png" alpha.py)
[[ -z $y ]] && { say "the alpha.py row was not found by OCR"; exit 1; }
say "alpha.py row at y=$y"
clickrow "$y" 3
shot 03-small-diff-unfolded

# ---- the big edit's row: the diff pane, unchanged behaviour --------------------------------
y=$(rowof "$out/$tag-03-small-diff-unfolded.png" big.txt)
[[ -z $y ]] && { say "the big.txt row was not found by OCR"; exit 1; }
say "big.txt row at y=$y"
clickrow "$y" 6
shot 04-big-diff-pane

say "wrote $out/$tag-*.png"
