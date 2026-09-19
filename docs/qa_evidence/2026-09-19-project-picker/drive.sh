#!/usr/bin/env bash
# Implementer evidence for card #916B: the project picker, the tab chip, the known-projects list in
# Options, the Sessions pane's Project chooser and the Switchboard's hide/show-folder action, driven
# under Xvfb with an isolated HOME / XDG_* / TMPDIR and RELAY_KEYRING=off.
#
#   drive.sh [build-dir]      build-dir defaults to <repo>/build; RELAY_QA_DISPLAY picks the display,
#                             else the first free one from :90 up.
#
# Fixture: a registry (projects.json) with two known projects — `widgetworks` (a board in
# .switchboard/, a git checkout) and `notes` (no board) — and a loose `Downloads` directory Relay is
# started in. The clicks below are at coordinates read off a 1400x900 window; the keyboard drives
# everything the keyboard can drive.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
[[ $build != /* ]] && build="$root/$build"
display=${RELAY_QA_DISPLAY:-}
if [[ -z $display ]]; then for n in $(seq 90 140); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done; fi
width=1400; height=900

qa=${RELAY_QA_TMP:-/tmp/claude-1000/v-916b-x/evidence}; rm -rf "$qa"; mkdir -p "$qa"
export HOME="$qa/h" XDG_CONFIG_HOME="$qa/config" XDG_DATA_HOME="$qa/data" XDG_CACHE_HOME="$qa/cache" XDG_RUNTIME_DIR="$qa/rt" TMPDIR="$qa/tmp" RELAY_KEYRING=off
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME/relay/state" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"; chmod 700 "$XDG_RUNTIME_DIR"
# The pane's worker runs in a systemd user scope and finds the user manager through $XDG_RUNTIME_DIR/bus.
ln -sfn "/run/user/$(id -u)/bus" "$XDG_RUNTIME_DIR/bus"
printf '[instructions]\nonboarded=true\n[terminal]\nshell_integration=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
known_a="$HOME/widgetworks"; known_b="$HOME/notes"; loose="$HOME/Downloads"
mkdir -p "$known_a/.switchboard" "$known_b" "$loose"
printf 'version: 1\ntabs: [{id: features, folder: features}]\ncolumns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]\n' >"$known_a/.switchboard/board.yaml"
mkdir -p "$known_a/.switchboard/features"
printf -- '---\nid: AAAA\ntype: work\nstatus: inbox\nlabels: []\ncreated: '"'"'2026-09-19'"'"'\n---\n# A card in widgetworks\n' >"$known_a/.switchboard/features/AAAA.md"
git -C "$known_a" init -q; git -C "$known_a" config user.name QA; git -C "$known_a" config user.email qa@example.invalid
git -C "$known_a" add -A; git -C "$known_a" commit -qm fixture
python3 - "$XDG_DATA_HOME/relay/state/projects.json" "$known_a" "$known_b" <<'PY'
import json, sys, hashlib, time
path, a, b = sys.argv[1:]
key = lambda p: hashlib.sha256(p.encode()).hexdigest()[:16]
now = int(time.time())
json.dump({"version": 1, "saved": now, "projects": [
  {"path": a, "key": key(a), "name": "widgetworks", "board": "repo", "board_dir": a + "/.switchboard", "reason": "switchboard", "known_since": now - 86400 * 3, "last_attached": now - 3600},
  {"path": b, "key": key(b), "name": "notes", "board": "none", "board_dir": "", "reason": "card-command", "known_since": now - 86400 * 9, "last_attached": now - 86400 * 2}],
  "declined": [{"path": a + "-old", "at": now - 86400}]}, open(path, "w"))
PY
mkdir -p "$known_a-old" "$known_a/sub"
# Saved agent sessions for the Sessions pane: two in widgetworks (one from a subdirectory), one in
# Downloads. The worker's index picks them up on its first listing.
PYTHONPATH="$root/backend:$root/tests" python3 - "$known_a" "$known_a/sub" "$loose" <<'PY'
import sys
from relay_core import conv_index
from relay_core.sessions import SessionStore
from test_conv_index import session
for n, (workspace, title) in enumerate([(sys.argv[1], "Widget colours"), (sys.argv[2], "The sub folder"), (sys.argv[3], "A loose chat in Downloads")]):
    store = SessionStore(conv_index.sessions_root() / conv_index.workspace_digest(workspace))
    store.save(session(chr(ord("a") + n) * 32, workspace=workspace, title=title, updated=2000.0 + n))
PY

cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true; }
trap cleanup EXIT
Xvfb "$display" -screen 0 "${width}x${height}x24" >"$qa/xvfb.log" 2>&1 & xvfb_pid=$!; sleep 1
kill -0 $xvfb_pid || { echo "display $display is not free" >&2; exit 1; }
export DISPLAY=$display
shot() { import -window root "$out/implementer-$1.png"; }
key() { xdotool key --delay 45 "$@"; }
type_() { xdotool type --delay 20 -- "$1"; }
click() { [[ ${1:-0} -gt 0 && ${2:-0} -gt 0 ]] || return 0; xdotool mousemove "$1" "$2" click 1; sleep "${3:-1}"; }
run_command() { click 300 400 0.3; key F12; sleep 0.4; type_ "$1"; key Return; sleep "${2:-2}"; key F12; sleep 0.4; }
disk() { printf '\n== %s ==\n' "$1"; shift; "$@" 2>&1; }

exec > >(tee "$qa/drive.log") 2>&1
printf 'binary: %s\n' "$(readlink -f "$build/relay")"; printf 'build id: '; cat "$build/relay.build-id" 2>/dev/null || echo '(none)'
"$build/relay" --workspace "$loose" --clean-shell --fresh >"$qa/relay-stderr.log" 2>&1 & relay_pid=$!
sleep 8
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1); test -n "$win" || { echo "no Relay window" >&2; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height" windowfocus "$win"; sleep 2

echo "SCENE 1: Ctrl+Shift+S in ~/Downloads (no project, no candidate) opens the picker"
key ctrl+shift+s; sleep 2; shot 01-picker
echo "SCENE 2: typing filters; Enter attaches the tab to the match and opens its Switchboard"
type_ "wid"; sleep 1; shot 02-picker-filtered
key Return; sleep 5; shot 03-attached-chip
echo "SCENE 3: the Switchboard's gear page names the folder and offers Show this board's folder"
click "${GEAR_X:-878}" "${GEAR_Y:-160}" 1.5; shot 04-gear-folder
click "${FOLDER_BTN_X:-1287}" "${FOLDER_BTN_Y:-824}" 4; shot 05-folder-shown
echo "SCENE 4: one click on the chip detaches (the Switchboard pane stays open); then the pane is closed"
click "${CHIP_X:-126}" "${CHIP_Y:-24}" 2; shot 06-detached
click "${BOARD_CLOSE_X:-1369}" "${BOARD_CLOSE_Y:-60}" 1.5
echo "SCENE 5: the Sessions pane's Project chooser"
click 300 400 0.5
key ctrl+shift+y; sleep 3; shot 07-sessions
click "${PROJECT_COMBO_X:-1153}" "${PROJECT_COMBO_Y:-179}" 1; key Down; key Return; sleep 2; shot 08-sessions-project
click "${SESSIONS_CLOSE_X:-1369}" "${SESSIONS_CLOSE_Y:-60}" 1.5   # the pane's × in its header
echo "SCENE 6: Initialize new project here — the board, git init, the created line, the Switchboard"
click 300 400 0.5
key ctrl+shift+s; sleep 2; key Return; sleep 6; shot 09-init-here
echo "SCENE 7: Options › Agent › Switchboard: the known projects, with Remove; a declined one with Undo"
key ctrl+comma; sleep 3; type_ "known"; sleep 1.5; shot 10-options-known
key Return; sleep 2; shot 11-options-removed   # Enter on the current (first) row presses its Remove
key ctrl+comma; sleep 1

{
disk "Downloads after Initialize new project here" ls -a "$loose" "$loose/.switchboard"
disk "git in Downloads" git -C "$loose" rev-parse --is-inside-work-tree
disk "registry after Remove" python3 -c "import json;d=json.load(open('$XDG_DATA_HOME/relay/state/projects.json'));print([(p['name'],p['reason']) for p in d['projects']]);print('declined',[p['path'] for p in d['declined']])"
disk "Downloads' files are untouched by Remove (the first row, most recently attached)" ls -a "$loose"
disk "widgetworks after Show this board's folder" ls -a "$known_a"
disk "git status of widgetworks" git -C "$known_a" status --short
} >"$out/implementer-disk.txt"
cat "$out/implementer-disk.txt"
grep -i "project init\|board_folder\|tab chip" "$XDG_DATA_HOME/relay/logs/relay.log" | tail -8
