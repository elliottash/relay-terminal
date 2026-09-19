#!/usr/bin/env bash
# The card thinking trace (#9K5H): implementer screenshots under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a local
# model endpoint at stub-provider.py on 127.0.0.1, whose Discuss turn thinks for ~5 s, posts a
# mid-turn question with board_comment, thinks again and answers; whose Plan turn reads the card,
# thinks for ~5 s, writes the card's `## Plan` with the hash it was told, and answers.
#
#   docs/qa_evidence/2026-09-19-switchboard-card-thinking-trace/drive.sh [build-dir]
#
# Shots:
#   implementer-discuss-stream.png   the trace streaming in the card's thread ("✦ thinking…" and
#                                    its tail above the reply box, the strip saying discussing)
#   implementer-question.png         the mid-turn question landed: the trace sealed ABOVE it,
#                                    the second block streaming under it
#   implementer-done.png             the turn ended: both sealed blocks and the answer in order
#   implementer-plan-stream.png      a Plan turn's trace streaming while it plans (the owner's
#                                    core ask: "especially in plan mode")
#   implementer-plan-written.png     the plan written on the card, the trace sealed above the
#                                    answer, the card file unchanged by the trace itself
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1280 height=880
port=${RELAY_QA_PORT:-8841}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-card-trace.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    [[ -f $sandbox/relay.log ]] && cp "$sandbox/relay.log" "$out/relay.log"
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

t() { xdotool type --delay 30 "$1"; }
k() { xdotool key --delay 60 "$@"; }

# The sandbox and its project: a board with one card, TRC1, whose discussion drives everything.
rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"   # the worker's systemd scope needs the bus
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
         "$work/issues/features" "$work/src"
printf 'PS1="\\\\w \\$ "\nunset PROMPT_COMMAND\n' >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"
printf '%s\n' \
    'version: 1' \
    'tabs: [{id: features, folder: features}]' \
    'columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]' \
    'agent: {autonomy: auto, max_creates_per_turn: 5}' \
    >"$work/issues/board.yaml"
printf '%s\n' \
    '---' \
    'id: TRC1' \
    'type: work' \
    'status: inbox' \
    'labels: []' \
    "created: '2026-09-19'" \
    '---' \
    '# show the thinking trace in the card' \
    '' \
    '## Issue' \
    '' \
    'In switchboard cards — when discussing with the agent, and especially in plan mode, show' \
    'the thinking traces same as in the terminals. The thinking should be in the thread in the' \
    'card, so that the agent can ask questions and they read after the reasoning.' \
    >"$work/issues/features/TRC1.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[theme]
name=relay-dark
[appearance]
pane_colours=type
[provider]
preset=local:stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

(cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 7
win=; best=0; w=
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 1.5

shot() {   # shot <name> — parks the mouse off the card so no hover row is in the picture
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.7
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x760+280+40 +repage -scale 140% "$out/implementer-$1-card.png"
}

# The board beside the terminal (Ctrl+Shift+S). The mouse path is deliberate: focus after the
# board opens is the list, whose single-letter keys ('e' edits, 'o' opens the file) eat typed
# words, and the filter eats the rest — so the section is clicked open, the card row clicked,
# and the reply box clicked twice (two slow clicks: the editor is not the document, so a double
# click there is harmless) before any letters are typed.
k ctrl+shift+s; sleep 3
xdotool mousemove 1050 250; xdotool click 1; sleep 0.8      # unfold Inbox
xdotool mousemove 1050 282; xdotool click 1; sleep 2        # the card opens beside the list
xdotool mousemove 1100 775; xdotool click 1; sleep 0.4; xdotool click 1; sleep 0.6

# ---- Discuss: the trace streams, the agent asks mid-turn, the answer lands ---------------
t "where should the thinking trace live?"
k Return
sleep 2.6
shot discuss-stream        # mid-block: the tail under a "✦ thinking…" header, in the thread
sleep 4.4
shot question              # the question entry with the first block sealed above it
sleep 6
shot done                  # both sealed blocks, the answer, the strip gone

# ---- Plan: the trace while a plan is being made ------------------------------------------
xdotool mousemove 1100 775; xdotool click 1; sleep 0.4; xdotool click 1; sleep 0.5
k ctrl+Return              # an empty box plans: the card is the brief
sleep 2.6
shot plan-stream           # reasoning streaming while the plan turn runs
sleep 9
shot plan-written          # `## Plan` on the card, the trace sealed above the closing answer

grep -q "✦" "$work/issues/features/TRC1.md" && echo "TRACE LEAKED INTO THE CARD FILE" || true
cp "$work/issues/features/TRC1.md" "$out/card-after.md"
cp "$work/issues/threads/TRC1.md" "$out/thread-after.md" 2>/dev/null || true
echo done
