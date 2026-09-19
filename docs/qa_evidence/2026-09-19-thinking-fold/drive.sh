#!/usr/bin/env bash
# The thinking fold (issue T8CN): implementer screenshots under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a local
# model endpoint at stub-provider.py on 127.0.0.1 (the harness of the 2026-09-19 subagent-badge
# run, #YMSR, with the scenes this issue needs).
#
#   docs/qa_evidence/2026-09-19-thinking-fold/drive.sh [build-dir] [scene...]
#
# Scenes (all by default), each shot as implementer-<scene>.png plus a 300 % crop of the header
# row and OCR notes in implementer-notes.txt:
#   collapse   the default: reasoning streams into an open fold (fold-stream.png, mid-turn),
#              folds away when it ends (fold-done.png, the anchor says "thought for N s"),
#              Alt+R reopens it (fold-reopened.png) and folds it again with a toast
#              (fold-closed.png).
#   always     agent/thinking_display=always: the fold is still open after the turn ended.
#   never      agent/thinking_display=never: no fold, just the single ✦ line thinking_done
#              always printed; Alt+R answers with the "Thinking display is off" toast.
#   migrate    a settings file that still has agent/show_thinking=false: migrated in place to
#              thinking_display=never on first ask (migrated-relay.conf is the proof).
#   twoblocks  reasoning, a partial answer, more reasoning: two anchors (thinking, thinking-2).
#   themes     the open fold in all five themes (line spacing 5, margin 16, muted markdown ink).
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-collapse always never migrate twoblocks themes}
width=1440 height=900
port=${RELAY_QA_PORT:-8814}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-thinking-fold.XXXXXX)
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

# 40 ms a key: at 18 ms the app dropped characters while a pane was starting up (#YMSR's lesson).
t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }

# EXTRA_CONF is appended to relay.conf by the scene: the thinking mode and the theme under test.
prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    # The pane's agent worker runs in a systemd user scope (src/Isolation.h) and the app removes
    # DBUS_SESSION_BUS_ADDRESS from that worker's environment, so systemd-run finds the user
    # manager through $XDG_RUNTIME_DIR/bus. A sandbox runtime dir has no bus of its own: without
    # this link the scope fails and the pane shows "The agent worker exited." Linked rather than
    # isolated away so pane isolation stays ON, as it is on a desktop.
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=${THEME:-relay-dark}
[appearance]
pane_colours=type
[provider]
preset=local:stub
${EXTRA_CONF:-}
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
    cp "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" "$out/relay-$scene.conf" 2>/dev/null
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

shot() {   # shot <name> [fast] — "fast" skips the mouse park and its wait, for a toast (1600 ms)
    scene=${scene:-$1}
    [[ ${2:-} == fast ]] || { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8; }
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop 1000x40+0+36 +repage -scale 300% "$out/implementer-$1-head.png"
    # The pane body: the anchor row, the fold's markdown, the answer. OCR the whole window too —
    # the anchor text ("✦ thinking", "✦ thought for N s") is the claim these shots make.
    convert "$out/implementer-$1.png" -crop ${width}x640+0+90 +repage -scale 150% "$out/implementer-$1-body.png"
    { echo "--- $1 (header)"; tesseract "$out/implementer-$1-head.png" - --psm 7 2>/dev/null
      echo "--- $1 (body)";   tesseract "$out/implementer-$1-body.png"  - --psm 6 2>/dev/null
    } >>"$out/implementer-notes.txt"
}

ask() { t "$1"; k Return; }

# The default mode: stream open, fold away on done, Alt+R both ways.
collapse() {
    scene=collapse; EXTRA_CONF=; THEME=relay-dark; start
    ask 'please think markdown about ponies'
    sleep 3.5; shot fold-stream      # mid-reasoning: open fold, "✦ thinking", header "Thinking…"
    sleep 9;   shot fold-done        # turn ended: collapsed, anchor "✦ thought for N s", bright answer
    k alt+r;   sleep 1.4; shot fold-reopened
    k alt+r;   sleep 0.4;  shot fold-closed fast   # toast: "Reasoning folded away · Alt+R shows it again"
    stop
}

always() {
    scene=always; EXTRA_CONF='[agent]
thinking_display=always'; THEME=relay-dark; start
    ask 'please think quick about ponies'
    sleep 8; shot always-open        # done, and the fold is still open
    stop
}

never() {
    scene=never; EXTRA_CONF='[agent]
thinking_display=never'; THEME=relay-dark; start
    ask 'please think quick about ponies'
    sleep 8; shot never-line         # no fold, no anchor: the single ✦ line only
    k alt+r; sleep 0.4; shot never-toast fast  # "Thinking display is off · Options › General …"
    stop
}

migrate() {
    scene=migrate; EXTRA_CONF='[agent]
show_thinking=false'; THEME=relay-dark; start
    ask 'please think quick about ponies'
    sleep 8; shot migrate-never      # behaves as never…
    stop                             # …and relay-never.conf (this scene's copy is relay-migrate.conf)
    cp "$out/relay-migrate.conf" "$out/migrated-relay.conf"
}

twoblocks() {
    scene=twoblocks; EXTRA_CONF=; THEME=relay-dark; start
    ask 'please think twoblocks about the river'
    sleep 10; shot twoblocks         # two anchors: ✦ thought for N s, and the second block's
    stop
}

themes() {
    local theme
    for theme in dark-copper gruvbox-dark ibm-beige relay-dark relay-light; do
        scene=theme-$theme; THEME=$theme; EXTRA_CONF='[agent]
thinking_display=always'; start
        ask 'please think quick about ponies'
        sleep 8; shot theme-$theme
        stop
    done
}

rm -f "$out/implementer-notes.txt"
for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
