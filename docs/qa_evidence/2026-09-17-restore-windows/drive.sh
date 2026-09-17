#!/usr/bin/env bash
# Live check of "reopen where I left off": build a two-window layout with splits and different
# directories, quit, restart, and see it come back — including a resumed conversation line.
#
#   docs/qa_evidence/2026-09-17-restore-windows/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script plus relay-*.log and the saved windows.json
# (implementer screenshots are not a QA verdict; see issues/README.md). Needs Xvfb,
# xdotool and ImageMagick `import`. An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of
# the real profile. RELAY_KEYRING=off means the desktop keyring is never touched; the provider key
# below is the literal string "offline-demo-not-a-key", so no real key exists in this run and none
# can be printed. Nothing here reaches the network: no agent turn is ever sent.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:93

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d) XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"
work=$(mktemp -d)
state=$XDG_DATA_HOME/relay/state/windows.json
export RELAY_KEYRING=off RELAY_KIMI_API_KEY=offline-demo-not-a-key RELAY_NO_URL_HANDLER=1
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$work"' EXIT

mkdir -p "$work/alpha" "$work/beta" "$work/gamma"

Xvfb "$display" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window root "$out/implementer-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
# No window manager under Xvfb: place each window explicitly before typing into it.
place() { xdotool windowmove "$1" "$2" "$3" windowsize "$1" "$4" "$5"; xdotool windowfocus "$1"; sleep 1; }
# xdotool matches helper X windows too; keep only the real top-level ones (Qt's popup and
# input-method helpers are tiny).
windows_of() {
    local id WIDTH HEIGHT
    for id in $(xdotool search --pid "$1" --name Relay 2>/dev/null); do
        WIDTH=0
        eval "$(xdotool getwindowgeometry --shell "$id" 2>/dev/null)"
        [[ ${WIDTH:-0} -ge 400 ]] && echo "$id"
    done
}
# Closing a window always asks. The dialog opens with Cancel focused; Left picks Close.
confirm_close() { sleep 1.5; k Left; sleep 0.5; k Return; sleep 3; }

start() {   # start(log-suffix, extra-args...)
    local suffix=$1; shift
    "$build/relay" "$@" >"$out/relay-$suffix.log" 2>&1 &
    relay_pid=$!
    sleep 7
}

# ===== run 1: build the layout ==============================================================
echo "run 1: building a two-window layout"
start run1 --workspace "$work/alpha"
win1=$(windows_of "$relay_pid" | tail -1)
[[ -z $win1 ]] && { echo "no Relay window"; exit 1; }
place "$win1" 40 30 1100 800
xdotool mousemove 500 400; sleep 2
shot 01-first-window

# Left pane stays in alpha; split right and send the new pane to beta.
k ctrl+p; sleep 4
k F12; sleep 0.5
t "cd $work/beta"; k Return; sleep 1.5
k F12; sleep 0.5
shot 02-window1-split-alpha-beta

# A second tab, in gamma.
k ctrl+t; sleep 4
k F12; sleep 0.5
t "cd $work/gamma"; k Return; sleep 1.5
k F12; sleep 0.5
shot 03-window1-second-tab-gamma
k ctrl+shift+Tab; sleep 1   # back to the first tab, so "current" is not the last one

# A second window.
k ctrl+n; sleep 6
win2=$(windows_of "$relay_pid" | grep -v "^$win1\$" | tail -1)
[[ -z $win2 ]] && { echo "no second Relay window"; exit 1; }
place "$win2" 620 260 900 640
sleep 2
shot 04-two-windows

# The debounced save runs 1 s after the last change.
sleep 3
cp "$state" "$out/windows-after-run1.json" 2>/dev/null || { echo "no saved layout at $state"; exit 1; }
python3 - "$state" <<'PY'
import json, sys
state = json.load(open(sys.argv[1]))
print(f"saved layout: version {state['version']}, {len(state['windows'])} window(s)")
for i, w in enumerate(state["windows"], 1):
    print(f"  window {i}: geometry {w.get('geometry')} screen {w.get('screen')!r} "
          f"tabs {len(w['tabs'])} current {w['current']} titles {w.get('titles')}")
PY
ls -l "$state"

# Seed one saved conversation so the restored pane has something to reattach to. The other panes
# have a session id but no saved file yet (no turn ever ran), which is the "gone session" case.
python3 - "$state" "$XDG_DATA_HOME" <<'PY'
import hashlib, json, pathlib, sys, time
state = json.load(open(sys.argv[1]))

def panes(node):
    if "split" in node:
        for child in node["children"]:
            yield from panes(child)
    elif "pane" in node:
        yield node["pane"]

leaf = next(p for p in panes(state["windows"][0]["tabs"][0]) if p.get("session_id"))
# Same rule as relay_core.sessions.default_session_dir().
digest = hashlib.sha256(str(pathlib.Path(leaf["workspace"]).resolve()).encode()).hexdigest()[:16]
target = pathlib.Path(sys.argv[2]) / "relay/sessions" / digest
target.mkdir(parents=True, exist_ok=True)
sid = leaf["session_id"]
now = int(time.time())
session = {"id": sid, "title": "Where I left off", "created": now - 600, "updated": now, "turns": 2,
           "model": "kimi-k3", "preset": "kimi", "mode": "build", "epoch": 0, "snapshots": {},
           "checkpoints": {"items": []},
           "messages": [{"role": "user", "content": "Summarise what this repository does."},
                        {"role": "assistant", "content": "It is the Relay terminal."},
                        {"role": "user", "content": "Now add the restore-on-start feature."},
                        {"role": "assistant", "content": "Done; the layout is saved to windows.json."}]}
(target / f"{sid}.json").write_text(json.dumps(session))
(target / f"{sid}.meta.json").write_text(json.dumps(
    {k: session[k] for k in ("id", "title", "created", "updated", "turns", "model", "preset")}))
print(f"seeded conversation {sid} in {target}")
PY

# Quit the hard way: no aboutToQuit, so what comes back is purely the debounced file. This is the
# "a crash loses at most the debounce window" guarantee.
kill -TERM "$relay_pid"; wait "$relay_pid" 2>/dev/null
sleep 1

# ===== run 2: it comes back =================================================================
echo "run 2: reopening where we left off"
start run2
mapfile -t wins < <(windows_of "$relay_pid")
echo "reopened ${#wins[@]} window(s)"
sleep 6
place "${wins[0]}" 40 30 1100 800
[[ ${#wins[@]} -gt 1 ]] && place "${wins[1]}" 620 260 900 640
xdotool mousemove 800 900; sleep 3
shot 05-restored-two-windows
xdotool windowraise "${wins[0]}"; xdotool windowfocus "${wins[0]}"; sleep 2
shot 06-restored-window1-resumed-conversation
k ctrl+Tab; sleep 2
shot 07-restored-window1-second-tab-gamma
k ctrl+shift+Tab; sleep 1

# The controls: the setting and the fresh-start action, both in the actions palette.
k ctrl+shift+a; sleep 1
t 'Reopen windows'; sleep 1.5
shot 08-palette-reopen-setting
xdotool key --delay 40 ctrl+a; t 'fresh window set'; sleep 1.5
shot 09-palette-fresh-window-set
k Return; sleep 2
shot 10-fresh-window-set-cleared
[[ -e $state ]] && echo "UNEXPECTED: $state still exists" || echo "saved layout removed by the palette action"
kill -TERM "$relay_pid"; wait "$relay_pid" 2>/dev/null
sleep 1

# ===== run 3: a fresh start after "Start a fresh window set" ================================
echo "run 3: nothing saved -> one window"
start run3 --workspace "$work/alpha"
mapfile -t wins < <(windows_of "$relay_pid")
echo "opened ${#wins[@]} window(s)"
place "${wins[0]}" 40 30 1100 800
sleep 2
shot 11-fresh-single-window

# ===== run 4: closing one window of two drops it; quitting keeps the rest ===================
k ctrl+n; sleep 6
mapfile -t wins < <(windows_of "$relay_pid")
place "${wins[1]}" 620 260 900 640
sleep 3
shot 12-two-single-pane-windows
k ctrl+w                     # closes the second window; the first stays open
shot 13-close-confirmation
confirm_close
mapfile -t wins < <(windows_of "$relay_pid")
echo "after closing one window: ${#wins[@]} left"
python3 -c "import json,sys; print('saved windows:', len(json.load(open(sys.argv[1]))['windows']))" "$state"
xdotool windowfocus "${wins[0]}"; sleep 1
k ctrl+w                     # closes the last window: Relay quits and keeps what was open
confirm_close
timeout 20 tail --pid="$relay_pid" -f /dev/null
python3 -c "import json,sys; print('saved after quit:', len(json.load(open(sys.argv[1]))['windows']))" "$state"
cp "$state" "$out/windows-after-run4.json"

# ===== run 5: --fresh ignores the saved layout once =========================================
echo "run 5: --fresh"
start run5 --fresh
mapfile -t wins < <(windows_of "$relay_pid")
echo "--fresh opened ${#wins[@]} window(s)"
place "${wins[0]}" 40 30 1100 800
sleep 2
shot 14-fresh-flag
python3 -c "import json,sys; print('saved layout untouched by --fresh:', len(json.load(open(sys.argv[1]))['windows']))" "$state"
kill -TERM "$relay_pid"; wait "$relay_pid" 2>/dev/null

echo "done; screenshots in $out"
