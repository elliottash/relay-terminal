#!/usr/bin/env bash
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
if [[ $build != /* ]]; then build="$root/$build"; fi
display=${RELAY_QA_DISPLAY:-:86}
width=1400
height=900

qa_tmp=$(mktemp -d)
export XDG_CONFIG_HOME="$qa_tmp/config"
export XDG_DATA_HOME="$qa_tmp/data"
export XDG_CACHE_HOME="$qa_tmp/cache"
export XDG_RUNTIME_DIR="$qa_tmp/runtime"
export TMPDIR="$qa_tmp/tmp"
export RELAY_KEYRING=off
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
printf '[instructions]\nonboarded=true\n[terminal]\nshell_integration=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

make_project() {
    local dir=$1 id=$2 title=$3
    mkdir -p "$dir/issues/features" "$dir/src"
    printf '%s\n' \
        'version: 1' \
        'tabs: [{id: features, folder: features}]' \
        'columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]' \
        'agent: {autonomy: auto, max_creates_per_turn: 5}' \
        >"$dir/issues/board.yaml"
    printf '%s\n' \
        '---' \
        "id: $id" \
        'type: work' \
        'status: inbox' \
        'labels: []' \
        "created: '2026-09-19'" \
        '---' \
        "# $title" \
        >"$dir/issues/features/$id.md"
    git -C "$dir" init -q
    git -C "$dir" config user.name 'JN7X QA'
    git -C "$dir" config user.email 'qa@example.invalid'
    git -C "$dir" add .
    git -C "$dir" commit -qm fixture
}

proj_a="$qa_tmp/projA"
proj_b="$qa_tmp/projB"
no_board="$qa_tmp/no-board"
third="$qa_tmp/third"
make_project "$proj_a" AAAA 'Card in project A'
make_project "$proj_b" BBBB 'Card in project B'
mkdir -p "$no_board" "$third"

cleanup() {
    kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true
    rm -rf "$qa_tmp"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 "${width}x${height}x24" >"$out/qa-xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 1
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours" >&2
    exit 1
fi
export DISPLAY=$display

shot() { import -window root "$out/qa-$1.png"; }
key() { xdotool key --delay 45 "$@"; }
type() { xdotool type --delay 10 -- "$1"; }
focus_terminal() { xdotool mousemove 300 830 click 1; sleep 0.5; }
run_command() {
    xdotool mousemove 300 400 click 1; key F12; sleep 0.5
    type "$1"; key Return; sleep "${2:-2}"; key F12; sleep 0.5
}
workers() {
    local count=0 pid env
    for pid in /proc/[0-9]*; do
        env=$({ tr '\0' '\n' <"$pid/environ"; } 2>/dev/null) || continue
        if grep -Fxq 'RELAY_PANE_ID=switchboard' <<<"$env" \
            && grep -Fxq "XDG_CONFIG_HOME=$XDG_CONFIG_HOME" <<<"$env"; then
            printf '%s ' "${pid##*/}"
            count=$((count + 1))
        fi
    done
    printf '(count=%s)\n' "$count"
}
record_git() {
    printf '%s\n' "projA:"; git -C "$proj_a" status --short
    printf '%s\n' "projB:"; git -C "$proj_b" status --short
}

exec > >(tee "$out/qa-live-log.txt") 2>&1
printf 'binary: %s\n' "$(readlink -f "$build/relay")"
printf 'binary sha256: '; sha256sum "$build/relay"
printf 'fixture root: %s\n' "$qa_tmp"

"$build/relay" --workspace "$proj_a" >"$out/qa-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
test -n "$win"
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height" windowfocus "$win"
sleep 2

printf '\nCHECK 1: no-board directory in a fresh tab\n'
xdotool mousemove 370 20 click 1; sleep 4
run_command "cd '$no_board'"
key ctrl+shift+s; sleep 3
shot 01-no-board

printf '\nOpen project A board in first tab and project B board in second tab\n'
xdotool mousemove 180 20 click 1; sleep 1
key ctrl+shift+s; sleep 5
shot 02-project-a
xdotool mousemove 520 20 click 1; sleep 1
run_command "cd '$proj_b'"
key ctrl+shift+s; sleep 5
shot 03-project-b
printf 'workers after both boards: '; workers

printf '\nCHECK 2: drag A card from inbox to discussing\n'
xdotool mousemove 180 20 click 1; sleep 1
xdotool mousemove 900 193 mousedown 1; sleep 1
xdotool mousemove 900 210; sleep 0.5
xdotool mousemove 900 231; sleep 1
xdotool mouseup 1; sleep 4
shot 04-project-a-after-drag
record_git

printf '\nCHECK 3: quick-add one card per board\n'
xdotool mousemove 1220 102 click 1; sleep 1
type 'Quick A'; key ctrl+Return; sleep 4
shot 05-project-a-quick-add
xdotool mousemove 520 20 click 1; sleep 2
xdotool mousemove 1220 102 click 1; sleep 1
type 'Quick B'; key ctrl+Return; sleep 4
shot 06-project-b-quick-add
record_git
printf 'workers after quick-adds: '; workers

printf '\nCHECK 4: existing A board while terminal moves to B\n'
xdotool mousemove 180 20 click 1; sleep 2
run_command "cd '$proj_b'"
key ctrl+shift+s; sleep 3
shot 07-a-board-pane-in-b

printf '\nCHECK 5: /card and # picker from project A/src in a fresh tab\n'
xdotool mousemove 670 20 click 1; sleep 4
run_command "cd '$proj_a/src'"
focus_terminal; type '/card test'; key Return; sleep 6
shot 08-card-from-src
focus_terminal; type '#'; sleep 3
shot 09-picker-from-src
key Escape; sleep 1
printf 'cards created under project A issues:\n'; find "$proj_a/issues" -type f -maxdepth 3 -printf '%P\n' | sort
printf 'board-rate locations:\n'; find "$proj_a" -name board-rate.json -printf '%P\n' | sort

printf '\nCHECK 7: other-project card link from project A output\n'
xdotool mousemove 120 20 click 1; sleep 2
run_command "clear; echo; echo '#BBBB'; echo"
shot 10-cross-project-link-before-click
xdotool mousemove 100 210 click 1; sleep 5
shot 11-cross-project-link-result

printf '\nCHECK 3 lifecycle: close B board, reopen it, then close window\n'
xdotool mousemove 520 20 click 1; sleep 2
key ctrl+shift+s; sleep 4
printf 'workers after closing B board: '; workers
key ctrl+shift+s; sleep 4
printf 'workers after reopening B board: '; workers
key alt+F4; sleep 6
printf 'workers after closing window: '; workers

printf '\nCHECK 6: relaunch from third directory and inspect restored A/B boards\n'
"$build/relay" --workspace "$third" >>"$out/qa-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 9
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
test -n "$win"
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height" windowfocus "$win"
sleep 3
shot 12-restored-first-tab
xdotool mousemove 520 20 click 1; sleep 3
shot 13-restored-second-tab
printf 'workers after restore: '; workers

printf '\nFixture paths retained until script exit only.\n'
