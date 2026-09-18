#!/usr/bin/env bash
# Live check of the wrong-mode hints under Xvfb + xdotool (issues/features/needs_qa_llm/
# 2026-09-17-wrong-mode-hints.md). A submission that errors and clearly belongs in the other
# input mode must flash the mode chip and show a hint naming input.toggle (Ctrl+I).
#
#   docs/qa_evidence/2026-09-17-wrong-mode-hints/drive.sh [build-dir]
#
# Two Relay launches (hint counters live in QSettings, and the per-id cooldown is 600 s, so
# the same hint id cannot fire twice in one session):
#   A1 terminal mode, clear request ("how do I list files by size"): invalid + agent_signal
#      -> nothing runs, the composer keeps the text, one ✗ line, toast, chip flash. No fix loop.
#   A2 agent mode, runnable command ("git stauts") whose run_command fails -> toast + flash,
#      at most once per turn. Needs >20 s after A1 (the global hint gap).
#   B1 terminal mode, natural command that runs and fails ("find the largest files") -> toast
#      + flash alongside the usual fix attempt.
#   B2 the same wrong submission again seconds later -> the ✗ line still prints, but no toast
#      and no flash (per-hint cooldown gates both).
#
# Writes implementer-NN-*.png next to this script plus relay-a-stderr.log / relay-b-stderr.log.
# Needs Xvfb, xdotool and ImageMagick import. Isolated XDG dirs keep the run out of the real
# profile; the agent talks to stub-provider.py on 127.0.0.1 only: no network, no keys.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
port=8733
sandbox=/tmp/relay-qa-wrong-mode
mkdir -p "$sandbox"

pick_display() {
    for n in $(seq 170 200); do
        [[ -e /tmp/.X11-unix/X$n ]] && continue
        echo ":$n"
        return
    done
    echo ""
}

pkill -f "stub-provider.py $port" 2>/dev/null; sleep 0.3
python3 "$out/stub-provider.py" "$port" &
stub_pid=$!
trap 'kill "${stub_pid:-0}" "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null' EXIT
sleep 1

shot() { import -window root "$out/implementer-$1.png"; }
t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }

# Print the centre ("x y") of a word's OCR bounding box, in screen pixels. Tries the raw
# frame first, then an upscaled inverted one (the theme is light-on-dark); coordinates from the
# 150% variant are scaled back down.
word_xy() {  # word_xy <png> <word>
    local png=$1 word=$2 found=""
    for variant in plain inverted; do
        local img=$png
        [[ $variant == inverted ]] && { convert "$png" -resize 150% -colorspace Gray -negate "$png.inv.png"; img=$png.inv.png; }
        found=$(tesseract "$img" - tsv 2>/dev/null | awk -F'\t' -v w="$word" -v inv="$variant" '
            $1==5 && $12==w && $11+0>=30 && !seen {seen=1; x=$7+$9/2; y=$8+$10/2}
            END { if (seen) printf "%d %d", (inv=="inverted"? x/1.5 : x), (inv=="inverted"? y/1.5 : y) }')
        [[ $variant == inverted ]] && rm -f "$png.inv.png"
        [[ -n $found ]] && { echo "$found"; return 0; }
    done
    return 1
}

# Point the pane at the loopback stub. The placeholder key is not a credential: the stub ignores
# the Authorization header. Searching the palette for "provider" no longer reaches the dialog
# (that word now only matches model-related aliases; the row itself is "Advanced provider
# settings", two levels deep), so walk the tree: Settings » Models, then the row. The consent
# checkbox is OCR-located and clicked -- the dialog has grown rows (workspace picker, Warp
# import) since the older evidence scripts, so a counted tab-walk no longer lands on it. Return
# then fires the dialog's default Save button.
configure_pane() {
    local tag=$1 xy
    k ctrl+shift+a; sleep 1
    t 'models'; sleep 1
    k Return; sleep 1          # open the Models settings submenu
    t 'advanced'; sleep 1
    k Return; sleep 2          # run "Advanced provider settings" -> BYOK dialog
    k Tab Tab Tab; sleep 0.3   # preset -> base -> model -> API key; Extra JSON below swallows Tab
    t 'loopback-stub-not-a-key'; sleep 0.5
    shot "tmp-provider-dialog-$tag"
    xy=$(word_xy "$out/implementer-tmp-provider-dialog-$tag.png" Send) \
        || { echo "OCR lost the consent checkbox ($tag)"; exit 1; }
    xdotool mousemove --sync ${xy% *} ${xy#* } click 1; sleep 0.5
    k Return; sleep 4
    xdotool windowfocus "$win"
}

launch() {  # launch <tag> -> sets $win/$relay_pid/$xvfb_pid with fresh XDG dirs and hint counts
    tag=$1
    display=$(pick_display)
    [[ -z $display ]] && { echo "no free X display"; exit 1; }
    export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
    export RELAY_KEYRING=off
    work=$(mktemp -d)
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=relay-qa-stub
extra={}
max_tokens=1024
CONF
    Xvfb "$display" -screen 0 1400x800x24 >/dev/null 2>&1 &
    xvfb_pid=$!
    sleep 2
    kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display (taken?)"; exit 1; }
    export DISPLAY=$display
    xdotool search --name . 2>/dev/null | grep -q . && { echo "$display already has windows"; exit 1; }

    "$build/relay" --workspace "$work" >"$out/relay-$tag-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 6
    # The pid also owns an 8x19 helper window named "relay" (xdotool matches --name
    # case-insensitively), and sizing that one paints a black sheet over everything: require
    # the titled main window ("Relay — <workspace>") and sanity-check its width.
    win=$(xdotool search --pid "$relay_pid" --name '^Relay ' | head -1)
    [[ -z $win ]] && { echo "no Relay window ($tag)"; exit 1; }
    width=$(xdotool getwindowgeometry --shell "$win" | awk -F= '/^WIDTH/{print $2}')
    [[ ${width:-0} -ge 400 ]] || { echo "Relay window too small ($tag, ${width:-?}px)"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" 1400 800
    xdotool windowfocus "$win"
    xdotool mousemove 700 400
    sleep 2
}

# ---------- launch A: the two toast-bearing scenarios ------------------------------------------
launch a
configure_pane a
shot 00-launch-a-ready

# A1: auto -> terminal mode is one Ctrl+I. A clear request in terminal mode must not run, must
# keep the text, must print one ✗ line and toast the Ctrl+I hint while the chip flashes violet.
k ctrl+i; sleep 1.5
t 'how do I list files by size'; k Return; sleep 0.7
shot 01-terminal-clear-request-hint
sleep 0.4
shot 02-terminal-clear-request-flash-blink
sleep 6   # let the toast fade so the kept text and the ✗ line are visible
shot 03-terminal-clear-request-kept-text

# A2: one more Ctrl+I lands in agent mode. A1 kept its text in the composer on purpose, so
# select-all + Delete clears it before typing the command. The stub reproduces the prompt as
# `cd <sandbox> && git stauts`; its failure must toast the Ctrl+I hint and flash the chip cyan.
sleep 16   # the global hint gap is 20 s; A1's toast was ~23 s ago by the time this turn's hint fires
k ctrl+i; sleep 1.5
k ctrl+a; k Delete; sleep 0.3
t 'git stauts'; k Return; sleep 2.5
shot 04-agent-shell-command-hint
sleep 0.4
shot 05-agent-shell-command-flash-blink
sleep 8
shot 06-agent-shell-command-after

kill "$relay_pid" "$xvfb_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; unset relay_pid xvfb_pid win
sleep 1

# ---------- launch B: fail-after-running, and the cooldown -------------------------------------
launch b
configure_pane b

# B1: terminal mode again; `find the largest files` is valid (find exists), runs, fails, and
# reads like a request -> hint + flash alongside the usual fix attempt.
k ctrl+i; sleep 1.5
t 'find the largest files'; k Return; sleep 1.2
shot 07-terminal-runs-and-fails-hint
sleep 6
shot 08-terminal-runs-and-fails-fix-attempt

# B2: the same wrong submission seconds later: the ✗ line prints, but no toast and no flash.
t 'how do I list files by size'; k Return; sleep 1.2
shot 09-second-submission-no-toast

echo "wrote $out/implementer-*.png"
