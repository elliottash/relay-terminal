#!/usr/bin/env bash
# Switchboard materials (#8E4Q): drive the board in three themes and shoot it.
set -uo pipefail
build=${BUILD:-/tmp/claude-1000/v-board/build}
out=${OUT:-/tmp/claude-1000/v-board-x/shots}
root=/tmp/claude-1000/v-board-x
display=${DISP:-:87}
width=1400; height=900
mkdir -p "$out"

export HOME=$root/home
export XDG_CONFIG_HOME=$root/home/.config
export XDG_DATA_HOME=$root/home/.local/share
export XDG_CACHE_HOME=$root/home/.cache
export XDG_RUNTIME_DIR=$root/run
export TMPDIR=$root/tmp
export RELAY_KEYRING=off
rm -rf "$HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"

make_project() {
    local dir=$1; shift
    mkdir -p "$dir/switchboard/features" "$dir/src"
    printf '%s\n' 'version: 1' 'tabs: [{id: features, folder: features}]' \
        'columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]' \
        'agent: {autonomy: auto, max_creates_per_turn: 5}' > "$dir/switchboard/board.yaml"
    local n=0
    while [ $# -gt 0 ]; do
        n=$((n+1))
        local id=${1%%:*} rest=${1#*:} status title lane
        status=${rest%%:*}; title=${rest#*:}
        lane=""
        case $status in
            needs-review) lane=needs_review/;;
            needs-qa-llm) lane=needs_qa_llm/;;
            done) lane=done/;;
        esac
        mkdir -p "$dir/switchboard/features/$lane"
        printf '%s\n' '---' "id: $id" 'type: work' "status: $status" 'labels: [feature]' \
            'component: [gui]' "created: '2026-09-1$n'" 'links: {plans: [], commits: [], evidence: []}' \
            '---' "# $title" '' 'A fixture card for the Switchboard materials evidence.' '' '## Tasks' \
            '- [x] one done task <!-- t:a1 -->' '- [ ] one open task <!-- t:b2 -->' \
            > "$dir/switchboard/features/$lane$id.md"
        shift
    done
    git -C "$dir" init -q
    git -C "$dir" config user.name 'board QA'; git -C "$dir" config user.email 'qa@example.invalid'
    git -C "$dir" add .; git -C "$dir" commit -qm fixture
}

proj=$root/proj; empty=$root/emptyproj
rm -rf "$proj" "$empty"
make_project "$proj" \
  'K1AA:inbox:Cord hardware reads as brass, not as a flag' \
  'K2BB:discussing:The board face is the ground of the pane' \
  'K3CC:ready:Jack rings only on an empty board' \
  'K4DD:in-progress:One lit rule at a time, under the pointer' \
  'K5EE:needs-review:Amber still means one thing' \
  'K6FF:needs-qa-llm:Every text token clears 4.5:1 on the face' \
  'K7GG:done:Hairline form when a theme refuses the material'
make_project "$empty"

Xvfb "$display" -screen 0 "${width}x${height}x24" > "$root/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 1
DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || { echo "display $display busy"; exit 1; }
export DISPLAY=$display
cleanup() { kill ${relay_pid:-0} $xvfb_pid 2>/dev/null; }
trap cleanup EXIT

shot() { import -window root "$out/$1.png"; }
key() { xdotool key --delay 45 "$@"; }

printf 'binary: %s\n' "$(readlink -f "$build/relay")"
printf 'binary sha256: '; sha256sum "$build/relay" | cut -d' ' -f1
printf 'fixture: %s (7 cards, one per section) and %s (a board with no cards)\n' "$proj" "$empty"
for theme in ${THEMES:-dark-copper ibm-beige relay-light}; do
    printf '\n=== %s ===\n' "$theme"
    printf '[instructions]\nonboarded=true\n[terminal]\nshell_integration=true\n[theme]\nname=%s\n' "$theme" \
        > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
    "$build/relay" --clean-shell --fresh --workspace "$proj" > "$root/relay-$theme.log" 2>&1 &
    relay_pid=$!
    sleep 8
    win=$(xdotool search --pid $relay_pid --name Relay | tail -1)
    [ -n "$win" ] || { echo "no window"; continue; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height windowfocus "$win"
    sleep 2
    key ctrl+shift+s; sleep 7
    xdotool mousemove 669 97 click 1; sleep 1          # dismiss the "worker exited" notice
    xdotool mousemove 700 700; sleep 1
    shot "$theme-01-board"
    # the pointer on a section header: exactly one rule is lit
    xdotool mousemove 800 288; sleep 0.4
    shot "$theme-02-section-hover"
    # a card open: the detail half on the same face
    xdotool mousemove 900 317 click 1; sleep 1; key Return; sleep 4; xdotool mousemove 1200 820; sleep 0.5
    shot "$theme-03-card"
    key Escape; sleep 2
    kill $relay_pid 2>/dev/null; sleep 3
    # the empty board: its own run, on a project whose board has no cards yet
    "$build/relay" --clean-shell --fresh --workspace "$empty" > "$root/relay-$theme-empty.log" 2>&1 &
    relay_pid=$!
    sleep 8
    win=$(xdotool search --pid $relay_pid --name Relay | tail -1)
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height windowfocus "$win"
    sleep 2
    key ctrl+shift+s; sleep 8
    xdotool mousemove 669 97 click 1; sleep 1
    xdotool mousemove 1200 820; sleep 1
    shot "$theme-04-empty-board"
    kill $relay_pid 2>/dev/null; sleep 3
done
echo done
ls -1 "$out"
