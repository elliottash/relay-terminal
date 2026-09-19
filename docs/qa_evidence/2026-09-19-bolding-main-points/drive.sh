#!/usr/bin/env bash
# Live bolding-main-points check (card #CVHT): implementer screenshots under Xvfb with an isolated
# HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a local
# model endpoint at stub-provider.py on 127.0.0.1, whose reply carries **Done:**, **Problem:**,
# **Need:** and a plain **Bold**.
#
#   docs/qa_evidence/2026-09-19-bolding-main-points/drive.sh [build-dir] [scene...]
#
# Scenes (both by default):
#   dark     the shipped relay-dark theme (which defines no terminal.palette, so the engine's
#            built-in ANSI palette applies): implementer-dark.png
#   palette  a user theme (relay/themes/qa-bold.toml) whose terminal.palette names unmistakable
#            colours for red/green/yellow and their bright variants: implementer-palette.png
#            The same ANSI bytes must come out in *these* colours — that is the "not burnt in"
#            half of the check: the renderer emits index 35/34/31, the engine resolves it from the
#            active theme at paint time.
#
# Needs Xvfb, xdotool, ImageMagick and Pillow (measure.py reads the pixels back).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-dark palette}
width=1440 height=900
port=${RELAY_QA_PORT:-8807}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-bold-points.XXXXXX)
stub_pid= xvfb_pid= relay_pid= win=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    if [[ -n ${RELAY_QA_KEEP:-} ]]; then
        echo "kept sandbox: $sandbox"; tail -30 "$sandbox/relay.log"
    else
        rm -rf "$sandbox"
    fi
}
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }

# The theme file the "palette" scene installs: relay-dark with unmistakable ANSI colours, so a
# screenshot can tell "the theme's green" from "a green someone baked into the reply". The
# shipped file's own multi-line `palette = [...]` block is replaced, not appended to — a second
# `palette` key would just lose to the first one in the reader's flat map.
qa_theme() {
    local src=$root/data/theme/themes/relay-dark.toml
    sed -e 's/^name = .*/name = "QA Bold"/' \
        -e '/^palette = \[/,/^\]/c\
palette = ["#000000", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff", "#00ffff", "#ffffff", "#808080", "#ff8080", "#80ff80", "#ffff80", "#8080ff", "#ff80ff", "#80ffff", "#ffffff"]' \
        "$src" >"$XDG_CONFIG_HOME/relay/themes/qa-bold.toml"
    grep -q '^palette = \["#000000", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff"' \
        "$XDG_CONFIG_HOME/relay/themes/qa-bold.toml" \
        || { echo "qa theme: the palette line was not written"; exit 1; }
}

prepare() {   # prepare <theme-id> [qa-theme]
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay/themes" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    [[ ${2:-} == qa-theme ]] && qa_theme
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=$1
[appearance]
pane_colours=type
[isolation]
enabled=false
[provider]
preset=local:stub
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
}

start() {   # start <theme-id> [qa-theme]
    prepare "$@"
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

stop() { [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null; relay_pid=; }

scene() {   # scene <name> <theme-id> [qa-theme] <expected colours for measure.py>
    local name=$1 theme=$2; shift 2
    local qa= ; [[ ${1:-} == qa-theme ]] && { qa=qa-theme; shift; }
    start "$theme" $qa
    t '*say the labels'; k Return
    sleep 10                                  # the stub answers at once; the pane renders as it arrives
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$name.png"
    convert "$out/implementer-$name.png" -crop ${width}x460+0+60 +repage -scale 150% \
        "$out/implementer-$name-reply.png"
    stop
    printf '\n== %s (theme %s)\n' "$name" "$theme" >>"$out/implementer-notes.txt"
    python3 "$out/measure.py" "$out/implementer-$name.png" "$@" >>"$out/implementer-notes.txt" 2>&1
}

rm -f "$out/implementer-notes.txt"
for s in $scenes; do
    case $s in
    dark)
        # relay-dark's own palette (data/theme/themes/relay-dark.toml): 2 green #7dd399,
        # 3 amber #ecc476, 1 red #f2777a; the bright variants 10 #98e5b0, 11 #f8d58e, 9 #ff8c8f are
        # accepted too, because an engine may paint bold+colour as the intense index — and in
        # practice it does. Green/amber/red, not the plan's magenta/blue/red: card #4E13 made amber
        # mean "something is waiting on you" everywhere, so `**Need:**` is amber and `**Done:**`
        # takes the Done glyph's green (be81edb, src/MarkdownAnsi.h Palette::done/need/problem).
        scene dark relay-dark \
            done=#7dd399,#98e5b0 need=#ecc476,#f8d58e problem=#f2777a,#ff8c8f \
            plain=#d8dce3,#f5f7fa ;;
    palette)
        scene palette qa-bold qa-theme \
            done=#00ff00,#80ff80 need=#ffff00,#ffff80 problem=#ff0000,#ff8080 \
            plain=#d8dce3,#f5f7fa ;;
    *) echo "unknown scene $s"; exit 1 ;;
    esac
done
printf '\ndone: %s\n' "$out"
