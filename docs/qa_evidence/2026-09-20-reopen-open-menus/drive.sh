#!/usr/bin/env bash
# Live check of #XAME: an Options or Actions pane open when Relay quits comes back where it was
# (mode, section tab, search text, highlighted row). Under Xvfb + xdotool, OCR'd with tesseract;
# the saved windows.json is the exact evidence, the screenshots the seen one.
#
#   01  Ctrl+Shift+M opens Options › Models; quit; windows.json carries the settings node
#   02  relaunch: the Options pane is back on the Models section
#   03  Ctrl+Shift+A opens Actions, a "theme" search is typed; quit; the node carries it
#   04  relaunch: the Actions pane is back with the search and its results
#
#   docs/qa_evidence/2026-09-20-reopen-open-menus/drive.sh <build-dir> [tag]
#
# Writes implementer-NN-*.png, state-NN-windows.json and run-<tag>.log next to this script.
# Needs Xvfb, xdotool, ImageMagick `import` and tesseract. Every XDG/HOME/TMPDIR path is
# isolated, so a Relay already running on this machine is untouched.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
build=$1
tag=${2:-run}
log=run-$tag.log
: >"$log"
fails=0
say() { echo "$*" | tee -a "$log"; }
ocr() { tesseract "$1" stdout 2>/dev/null; }
need() { if ocr "$1" | grep -qF "$2"; then say "PASS: $3"; else say "FAIL: $3 (wanted \"$2\" in $1)"; fails=$((fails+1)); fi; }
need_json() { if grep -qF "$2" "$1"; then say "PASS: $3"; else say "FAIL: $3 (wanted \"$2\" in $1)"; fails=$((fails+1)); fi; }

display=
for n in $(seq 199 -1 171); do
    [[ -e /tmp/.X11-unix/X$n ]] && continue
    display=:$n
    break
done
[[ -z $display ]] && { say "no free X display in 171..199"; exit 1; }
say "display $display"

sandbox=$(mktemp -d)
export HOME=$sandbox/home
export XDG_CONFIG_HOME=$sandbox/config XDG_DATA_HOME=$sandbox/data XDG_CACHE_HOME=$sandbox/cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
export RELAY_KEYRING=off RELAY_NO_URL_HANDLER=1
work=$sandbox/work
mkdir -p "$work"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
state=$XDG_DATA_HOME/relay/state/windows.json

relay_pid=
trap 'for pid in "${relay_pid:-}" "${xvfb_pid:-}"; do [[ -n $pid ]] && kill "$pid" 2>/dev/null; done; sleep 1; rm -rf "$sandbox"' EXIT

Xvfb "$display" -screen 0 1500x900x24 >"$out/xvfb-$tag.log" 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { say "Xvfb died on $display (taken?)"; exit 1; }
export DISPLAY=$display

win=
start_relay() {
    # No --workspace: an explicit one asks for a new window and skips the layout restore
    # (main.cpp). Launching from $work gives the pane its directory the other way.
    (cd "$work" && exec "$build/relay" --engine-core libvterm) >"$out/relay-$tag-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
    [[ -z $win ]] && { say "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" 1500 900
    xdotool windowactivate --sync "$win"
    xdotool mousemove 750 450
    sleep 2
}
stop_relay() {
    # SIGTERM is an ordinary quit in Relay (main.cpp's handler turns it into QCoreApplication::
    # quit(), and aboutToQuit saves the layout) — and unlike the window close it asks nothing.
    kill -TERM "$relay_pid"
    for _ in $(seq 1 20); do kill -0 "$relay_pid" 2>/dev/null || break; sleep 0.5; done
    kill -0 "$relay_pid" 2>/dev/null && { say "relay did not quit"; exit 1; }
    relay_pid=
}
shot() {
    import -window root "$out/implementer-$1.png"
    ocr "$out/implementer-$1.png" >>"$log"
    say "--- shot $1 above ---"
}
k() { xdotool key --delay 60 "$@"; }
t() { xdotool type --delay 20 "$1"; }

# ---- 01: Options › Models is saved ------------------------------------------------------------
start_relay
k ctrl+shift+m; sleep 3                      # Options, straight at the Models section
shot 01-options-models
need "$out/implementer-01-options-models.png" "Models" "01 Options opened at the Models section"
stop_relay
[[ -f $state ]] || { say "no windows.json after quit"; exit 1; }
cp "$state" "$out/state-01-windows.json"
need_json "$out/state-01-windows.json" '"settings"' "01 the layout carries the settings node"
need_json "$out/state-01-windows.json" '"models"' "01 the node names the Models section"

# ---- 02: it comes back where it was ------------------------------------------------------------
start_relay
sleep 2
shot 02-restored-options
need "$out/implementer-02-restored-options.png" "Models" "02 the restored window shows Options › Models"
need "$out/implementer-02-restored-options.png" "Options" "02 the restored pane wears the Options band"
stop_relay

# ---- 03: an Actions search is saved -------------------------------------------------------------
start_relay
k ctrl+shift+a; sleep 3
t 'theme'; sleep 2
shot 03-actions-search
stop_relay
cp "$state" "$out/state-03-windows.json"
need_json "$out/state-03-windows.json" '"actions"' "03 the layout carries the Actions mode"
need_json "$out/state-03-windows.json" '"theme"' "03 the node carries the search text"

# ---- 04: the search comes back -------------------------------------------------------------------
start_relay
sleep 2
shot 04-restored-actions
need "$out/implementer-04-restored-actions.png" "theme" "04 the restored Actions pane keeps the search"
stop_relay

say "fails=$fails"
[[ $fails -eq 0 ]] || exit 1
say "PASS"
