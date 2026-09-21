#!/usr/bin/env bash
# #C9VT: "make it where, highlight to copy only works if there are at least 3 letters or numbers
# in the string". A highlight of one or two letters or digits is a slip of the mouse on the way to
# a click, and it must leave the clipboard and PRIMARY as they were; an explicit copy
# (Ctrl+Shift+C, the context menu) still takes a short selection, because it was asked for.
#
#   docs/qa_evidence/2026-09-21-copy-on-highlight-three-chars/drive.sh [build-dir]
#
# A live Relay under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_RUNTIME_DIR
# and TMPDIR, so it owns neither this machine's clipboard nor the profile being read. The terminal
# is the measured case: its highlight is two paths for one gesture -- the engine's PRIMARY write
# (TerminalView::mouseReleaseEvent) and the pane's clipboard write (Pane's app-wide event filter)
# -- and both must hold the rule.
#
# Where the text is comes from OCR, not from a guessed coordinate: the marker line echoed into the
# shell is found in a screenshot's word boxes (tesseract tsv), which give the line's y. The drag
# that covers exactly two characters is then found by sweeping, and every candidate is measured
# the same honest way: the SAME drag is first copied explicitly (Ctrl+C with the prompt box empty,
# whose behaviour the card leaves alone) and the clipboard read back, so the drive knows how many
# letters or digits the highlight holds before it arms the sentinel and repeats the drag with copy
# on highlight alone.
#
# Each check prints PASS/FAIL; the run exits non-zero if any failed. Needs Xvfb, xdotool, xclip,
# ImageMagick and tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-build}; [[ $build == /* ]] || build=$root/$build
width=1440 height=900
port=${RELAY_QA_PORT:-8797}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-c9vt.XXXXXX)
xvfb_pid= relay_pid= stub_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

log=$out/implementer-clipboard.txt
: >"$log"
pass=0 fail=0
say() { printf '%s\n' "$*" | tee -a "$log"; }
ok() {   # ok <label> <wanted> <got>
    if [[ $2 == "$3" ]]; then
        pass=$((pass + 1)); say "PASS  $1 (got '$3')"
    else
        fail=$((fail + 1)); say "FAIL  $1 (wanted '$2', got '$3')"
    fi
}

k() { xdotool key --delay 60 "$@"; }
# Relay's composer routes a line that is a command to the shell, but its auto-routing asks the
# agent, so the drive says so with the `!` prefix: this one line runs in the terminal.
type_line() { xdotool type --delay 18 "!$1"; sleep 0.3; xdotool key --delay 60 Return; }
# A real highlight: press at x1,y1, drag across to x2,y2 in two steps, release.
drag() {
    xdotool mousemove "$1" "$2" mousedown 1; sleep 0.25
    xdotool mousemove $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )); sleep 0.15
    xdotool mousemove "$3" "$4"; sleep 0.25
    xdotool mouseup 1; sleep 1.0
}

copied() { timeout 5 xclip -o -selection clipboard 2>/dev/null; }
on_primary() { timeout 5 xclip -o -selection primary 2>/dev/null; }
sentinel='SENTINEL-nothing-was-copied'
arm() {   # the sentinel on both selections, so a copy shows up as a change
    printf '%s' "$sentinel" | xclip -i -selection clipboard
    printf '%s' "$sentinel" | xclip -i -selection primary
    sleep 0.4
}

# The same drag, copied on purpose: Ctrl+C with the prompt box empty is the explicit copy (the
# pane copies the terminal's highlight for it), and #C9VT leaves that alone. What comes back is
# exactly what the highlight covered, short selection and all, so the drive can count it.
explicit() {
    drag "$1" "$2" "$3" "$4"
    xdotool key --delay 60 ctrl+c; sleep 0.7
    copied
}

# The owner's rule, in bash: how many letters and digits the string holds.
alnum_count() {
    local text=$1 n=0 c
    while IFS= read -r -n1 c; do
        [[ $c == [0-9A-Za-z] ]] && n=$((n + 1))
    done <<<"$text"
    printf '%s' "$n"
}

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
    # The setting under test, on, with the terminal in its shipped look.
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[terminal]
copy_on_select=true
[provider]
preset=local:stub
CONF
}

start() {
    prepare
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$((WIDTH * HEIGHT)); win=$w; }
    done
    [[ -z $win ]] && { say "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 1.5
}

stop() { [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null; relay_pid=; }

shot() {   # shot <name>
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/implementer-$1.png"
}

# The marker line, from the screenshot's word boxes: `x y w h` of the echoed letters. Only the
# terminal's band of the window is considered (the transcript below repeats what was typed), and
# the lowest match wins, which is the shell's own echo of the line rather than the command.
marker_box() {
    shot "0-grid"
    tesseract "$out/implementer-0-grid.png" "$out/ocr-grid" --psm 11 tsv >/dev/null 2>&1
    awk -F'\t' '$12 ~ /AAAABBBB/ && $7 < 900 && $8 >= 80 && $8 <= 220 && $8 > besty {besty=$8; x=$7; y=$8; w=$9; h=$10; found=1}
                END {if (!found) exit 1; printf "%d %d %d %d\n", x, y, w, h}' "$out/ocr-grid.tsv"
}

start
# A long run of letters, so a two-character highlight has room either side of it.
type_line 'echo ZZZAAAABBBBCCCCDDDDEEEEFFFFGGGG'; sleep 1.5
read -r mx my mw mh < <(marker_box) || { say "the marker line was not found on screen"; stop; exit 1; }
y=$((my + mh / 2))
say "the marker line is at x=$mx y=$y ($mw px over 31 characters: a cell is about $((mw / 31))px)"
say ""
say "--- sweeping the drag at y=$y: what each one covers (Ctrl+C on it), and what copy on highlight alone does with it"
w2= w3= press2= press3= text2= text3=
for off in 0 2 4 6 8; do
    px=$((mx + off))
    for dx in $(seq 2 2 40); do
        text=$(explicit "$px" "$y" $((px + dx)) "$y")
        n=$(alnum_count "$text")
        [[ -z $text ]] && continue
        say "    press x=$px, release x=$((px + dx)) ($dx px) -> an explicit copy of it holds '$text' ($n letters or digits)"
        if [[ -z $w2 && $n == 2 ]]; then w2=$dx; press2=$px; text2=$text; fi
        if [[ -z $w3 && $n == 3 ]]; then w3=$dx; press3=$px; text3=$text; fi
    done
    [[ -n $w2 && -n $w3 ]] && break
done
if [[ -z $w2 || -z $w3 ]]; then
    say "FAIL  a drag of exactly two and of exactly three characters was never measured"
    fail=$((fail + 1))
else
    say ""
    say "measured: a highlight of '$text2' ($w2 px from x=$press2) and of '$text3' ($w3 px from x=$press3)"

    # The rule: two characters, copied on highlight alone, take nothing.
    arm; drag "$press2" "$y" $((press2 + w2)) "$y"
    say "    two characters, copy on highlight alone: CLIPBOARD '$(copied)', PRIMARY '$(on_primary)'"
    ok "a two-character highlight copies nothing" "$sentinel" "$(copied)"
    ok "a two-character highlight leaves PRIMARY alone" "$sentinel" "$(on_primary)"
    shot "1-terminal-two-chars"

    # ... and the same two characters, asked for, still go to the clipboard.
    asked=$(explicit "$press2" "$y" $((press2 + w2)) "$y")
    ok "an explicit copy of two characters still works" "copied" \
       "$([[ $asked == "$sentinel" || -z $asked ]] && echo nothing || echo copied)"
    say "    the explicit copy on them holds '$asked'"

    # Three characters, and it is a copy again -- on PRIMARY (the engine) and the clipboard (the pane).
    arm; drag "$press3" "$y" $((press3 + w3)) "$y"
    say "    three characters, copy on highlight alone: CLIPBOARD '$(copied)', PRIMARY '$(on_primary)'"
    ok "a three-character highlight copies the clipboard" "$text3" "$(copied)"
    ok "a three-character highlight copies PRIMARY" "$text3" "$(on_primary)"
    shot "1-terminal-three-chars"

    # The wide drag the terminal has always copied with.
    arm; drag "$mx" "$y" $((mx + 400)) "$y"
    wide=$(copied)
    say "    a wide highlight: CLIPBOARD '$(printf '%s' "$wide" | head -c 60)'"
    ok "a wide highlight still copies" "copied" \
       "$([[ $wide == "$sentinel" || -z $wide ]] && echo nothing || echo copied)"
    shot "1-terminal-wide"
fi
stop

say ""
say "$pass passed, $fail failed"
[[ $fail == 0 ]]