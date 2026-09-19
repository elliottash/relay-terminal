#!/usr/bin/env bash
# #T7AH live check: LineSpacing 5 -> 0, measured on screen.
# Same binary throughout; only RELAY_THEME_DIR differs (control copy with the old 5 vs
# the repo's 0). The pane shows only a handful of rows, so each case is captured twice:
#   pitch shot: shell prints `clear; seq 1 9`          -> row pitch
#   gap shot:   shell prints alternating text/blank    -> paragraph gap in pitches
# The pane's shell prints the pattern itself from the isolated HOME's rc files: no
# typing, no focus, nothing to drive.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=$root/build
display=${RELAY_QA_DISPLAY:-:94}
width=1400
height=900

qa_tmp=$(mktemp -d)
relay_pid=0
xvfb_pid=0
cleanup() {
    (( relay_pid )) && kill "$relay_pid" 2>/dev/null || true
    (( xvfb_pid )) && kill "$xvfb_pid" 2>/dev/null || true
    sleep 1
    rm -rf "$qa_tmp"
}
trap cleanup EXIT

# Control theme: a copy of the repo theme with the old value restored.
ctrl_theme=$qa_tmp/theme-line5
cp -r "$root/data/theme" "$ctrl_theme"
sed -i 's/^LineSpacing=0$/LineSpacing=5/' "$ctrl_theme/terminal.conf"
{
    printf 'control %s: ' "$ctrl_theme/terminal.conf"; grep '^LineSpacing' "$ctrl_theme/terminal.conf"
    printf 'repo    %s: ' "$root/data/theme/terminal.conf"; grep '^LineSpacing' "$root/data/theme/terminal.conf"
} | tee "$out/theme-values.txt"

Xvfb "$display" -screen 0 "${width}x${height}x24" >"$out/qa-xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 1
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours" >&2
    exit 1
fi
export DISPLAY=$display

shot() { import -window root "$out/$1"; }

run_case() {   # run_case <theme-dir> <tag> <mode> <rc-pattern>
    local theme=$1 tag=$2 mode=$3 pat=$4
    local home="$qa_tmp/home-$tag-$mode" ws="$qa_tmp/ws-$tag-$mode"
    local rt="$qa_tmp/runtime-$tag-$mode" tmp="$qa_tmp/tmp-$tag-$mode"
    mkdir -p "$home/RelayTerminal" "$home/data" "$home/cache" "$ws" "$rt" "$tmp"
    chmod 700 "$rt"
    printf '[instructions]\nonboarded=true\n[terminal]\nshell_integration=true\n' >"$home/RelayTerminal/relay.conf"
    printf 'PS1="\\$ "\nclear\n%s\n' "$pat" >"$home/.bashrc"
    printf 'clear\n%s\n' "$pat" >"$home/.profile"
    cp "$home/.profile" "$home/.bash_profile"
    env XDG_CONFIG_HOME="$home" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" \
        XDG_RUNTIME_DIR="$rt" TMPDIR="$tmp" RELAY_KEYRING=off RELAY_THEME_DIR="$theme" \
        HOME="$home" "$build/relay" --workspace "$ws" >"$out/relay-$tag-$mode.log" 2>&1 &
    relay_pid=$!
    sleep 15
    local win
    win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
    test -n "$win"
    xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height" windowfocus "$win"
    sleep 4
    shot "screen-$tag-$mode.png"
    (( relay_pid )) && kill "$relay_pid" 2>/dev/null || true
    wait "$relay_pid" 2>/dev/null || true
    relay_pid=0
    sleep 1
}

exec > >(tee "$out/qa-live-log.txt") 2>&1
printf 'binary: %s\n' "$(readlink -f "$build/relay")"
printf 'binary sha256: '; sha256sum "$build/relay" | cut -d' ' -f1

gap_pat='for i in 1 2 3 4 5 6 7 8 9; do echo para-$i; echo; done'

fail=0
run_case "$ctrl_theme" before-line5 pitch 'seq 1 9'
python3 "$out/measure.py" "$out/screen-before-line5-pitch.png" before pitch \
    >"$out/measure-before.txt" || fail=1
run_case "$ctrl_theme" before-line5 gap "$gap_pat"
python3 "$out/measure.py" "$out/screen-before-line5-gap.png" before gap \
    "$(sed -n 's/.*row pitch \([0-9.]*\)px.*/\1/p' "$out/measure-before.txt")" \
    >>"$out/measure-before.txt" || fail=1

run_case "$root/data/theme" after-line0 pitch 'seq 1 9'
python3 "$out/measure.py" "$out/screen-after-line0-pitch.png" after pitch \
    >"$out/measure-after.txt" || fail=1
run_case "$root/data/theme" after-line0 gap "$gap_pat"
python3 "$out/measure.py" "$out/screen-after-line0-gap.png" after gap \
    "$(sed -n 's/.*row pitch \([0-9.]*\)px.*/\1/p' "$out/measure-after.txt")" \
    >>"$out/measure-after.txt" || fail=1

echo '=== before (LineSpacing=5) ==='; cat "$out/measure-before.txt"
echo '=== after (LineSpacing=0) ==='; cat "$out/measure-after.txt"
(( fail )) && exit 3
exit 0
