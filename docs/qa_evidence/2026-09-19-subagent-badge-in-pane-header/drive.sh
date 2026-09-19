#!/usr/bin/env bash
# The subagent badge in the pane header (card #YMSR): implementer screenshots under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile
# points a local model endpoint at stub-provider.py on 127.0.0.1 (the harness of the 2026-09-19
# pane-live-state run, with the spawn modes this card needs).
#
#   docs/qa_evidence/2026-09-19-subagent-badge-in-pane-header/drive.sh [build-dir] [scene...]
#
# Scenes (all by default), each shot as implementer-<scene>.png plus the 200 % crops
# implementer-<scene>-bar.png (the tab bar) and implementer-<scene>-head.png (the header row):
#   none  a fresh pane: no badge, no state word — "if applicable" means absent, not a 0.
#   one   one background subagent, the main turn ended: the violet chip with "1" beside
#         "Subagents working".
#   two   two of them, started one per tool round: the badge counts 2.
#   busy  the main turn still running with one subagent live: the badge beside "Relaying…",
#         which is the point of a number — the word alone does not say how many.
#   ended the same pane twice: while its subagent runs (implementer-endedlive.png) and
#         after it has finished (implementer-ended.png, no badge) — the count is live.
#
# Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-none one two busy ended}
width=1440 height=900
port=${RELAY_QA_PORT:-8804}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-subagent-badge.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
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

# 40 ms a key: at 18 ms the app dropped characters while a pane was starting up, which is how
# the first run of this harness typed "spo one agent" (the OCR of the header caught it).
t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    # The pane's agent worker runs in a systemd user scope (src/Isolation.h) and the app
    # removes DBUS_SESSION_BUS_ADDRESS from that worker's environment, so systemd-run finds the
    # user manager through $XDG_RUNTIME_DIR/bus. A sandbox runtime dir has no bus of its own:
    # without this link the scope fails ("Failed to connect to bus") and the pane shows "The
    # agent worker exited." — the run would prove nothing. Linked rather than isolated away so
    # pane isolation stays ON, as it is on a desktop (checked by hand: `systemd-run --user
    # --scope -- true` succeeds with this link and fails without it).
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
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
}

start() {
    prepare
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 1.5
}

stop() {
    cp "$sandbox/relay.log" "$out/relay-$scene.log" 2>/dev/null
    mkdir -p "$out/logs-$scene" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs-$scene/" 2>/dev/null
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

shot() {   # shot <name>
    scene=$1
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x60+0+0 +repage -scale 200% "$out/implementer-$1-bar.png"
    # The pane header row (the state glyph, the word, the badge, the title) sits at y 40..72 in
    # this window: the first run cropped 36..96 and OCR'd the shell's sudo banner instead of the
    # header, which is what tesseract -tsv on the full frame said (see implementer-notes.txt).
    convert "$out/implementer-$1.png" -crop 1000x40+0+36 +repage -scale 300% "$out/implementer-$1-head.png"
    tesseract "$out/implementer-$1-head.png" - --psm 7 2>/dev/null \
        | sed 's/^/header: /' >>"$out/implementer-notes.txt"
}

none() { start; shot none; stop; }

one() { start; t '*spawn one agent'; k Return; sleep 12; shot one; stop; }

two() { start; t '*spawn2 two agents'; k Return; sleep 15; shot two; stop; }

busy() { start; t '*spawnbusy keep working'; k Return; sleep 9; shot busy; stop; }

# The count is live, not a session total: the same pane with the subagent still running
# (implementer-endedlive.png) and after it has finished (implementer-ended.png, no badge).
# The stub's SUBTASK sleeps 90 s, so the second shot is 100 s later.
ended() { start; t '*spawn one agent'; k Return; sleep 12; shot endedlive; sleep 100; shot ended; stop; }

rm -f "$out/implementer-notes.txt"
for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
