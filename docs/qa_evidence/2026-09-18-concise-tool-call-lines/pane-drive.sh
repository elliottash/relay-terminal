#!/usr/bin/env bash
# Live check of the terminal pane's tool-call lines (#TK9C, protocol section 23), under Xvfb +
# xdotool. One agent turn runs a python heredoc, a short command, a failing command, four
# consecutive reads, a small edit, a new file and a big edit; the pane must show one row per call,
# unfold a row where it is clicked, print the small diff under its row and open the big one in a
# diff pane.
#
#   docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-drive.sh [build-dir]
#
# Writes pane-NN-*.png next to this script plus pane-relay-stderr.log. Needs Xvfb, xdotool and
# ImageMagick `import`. HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME, XDG_RUNTIME_DIR and
# TMPDIR are all isolated: a Relay already running on this machine would otherwise share the open
# socket, the window-layout lock and /tmp/relay-* with the run. The agent talks to
# pane-stub-provider.py on 127.0.0.1 only: no network, no keys.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
port=8733
# Pick a display nobody else holds. Relay's QA scripts are run side by side on this machine, and a
# taken display is the dangerous case: Xvfb exits, the app lands on someone else's X server and
# xdotool types into their window. Refuse to drive a display we did not create.
display=
for n in $(seq 161 199); do
    [[ -e /tmp/.X11-unix/X$n ]] && continue
    display=:$n
    break
done
[[ -z $display ]] && { echo "no free X display in 161..199"; exit 1; }

sandbox=$(mktemp -d)
export HOME=$sandbox/home
export XDG_CONFIG_HOME=$sandbox/config XDG_DATA_HOME=$sandbox/data XDG_CACHE_HOME=$sandbox/cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
export RELAY_KEYRING=off RELAY_NO_URL_HANDLER=1 RELAY_CUSTOM_API_KEY=loopback-stub-not-a-key
work=$sandbox/work
mkdir -p "$work"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[agent]
show_tool_output=false
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=relay-qa-stub
extra={}
max_tokens=1024
CONF

# What the turn reads and edits.
printf 'SMALL = 1\nkeep = True\n' >"$work/alpha.py"
for f in beta gamma delta; do printf '# %s\n%s\n' "$f" "$(seq 1 40)" >"$work/$f.py"; done
seq 1 80 | sed 's/^/line /' >"$work/big.txt"
printf 'nothing to find here\n' >"$work/notes.txt"

trap 'kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; rm -rf "$sandbox"' EXIT

python3 "$out/pane-stub-provider.py" "$port" &
stub_pid=$!
sleep 1

Xvfb "$display" -screen 0 1500x900x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display (taken?)"; exit 1; }
export DISPLAY=$display
xdotool search --name . 2>/dev/null | grep -q . && { echo "$display already has windows"; exit 1; }

shot() {
    import -window root "$out/pane-$1.png"
    kill -0 "${relay_pid:-0}" 2>/dev/null || echo "relay is gone before $1"
}
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 45 "$@"; }

"$build/relay" --engine-core libvterm --workspace "$work" >"$out/pane-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 900
xdotool windowfocus "$win"
xdotool mousemove 750 450
sleep 2

# ---- let the pane configure itself against the loopback stub ------------------------------
# Submitting with no provider configured opens "Bring your own key" over the pane. The key comes
# The key, the consent box and Save are clicked by coordinate: without a window manager under
# Xvfb a dialog only takes input after xdotool windowfocus, and its tab order has changed twice this
# week. The placeholder is not a credential — the stub ignores the Authorization header.
xdotool windowfocus "$win"
t 'walk through the tools'; sleep 0.5
k ctrl+Return; sleep 5
dlg=$(xdotool search --name 'Bring your own key' | tail -1)
[[ -z $dlg ]] && { echo "the key dialog did not open"; exit 1; }
eval "$(xdotool getwindowgeometry --shell "$dlg")"
echo "key dialog $dlg at $X,$Y ${WIDTH}x${HEIGHT}"
shot 00-bring-your-own-key
xdotool windowfocus "$dlg"; sleep 0.5
xdotool mousemove --sync $((X + 390)) $((Y + 130)); sleep 0.4; xdotool click --delay 120 1; sleep 0.4
t 'loopback-stub-not-a-key'; sleep 0.4
xdotool mousemove --sync $((X + 19)) $((Y + 468)); sleep 0.4; xdotool click --delay 120 1; sleep 0.8
xdotool mousemove --sync $((X + 497)) $((Y + 501)); sleep 0.4; xdotool click --delay 120 1; sleep 20
shot 01-agent-ready

# ---- one turn, every kind of line ----------------------------------------------------------
xdotool windowfocus "$win"
xdotool mousemove 750 700
t 'walk through the tools'; sleep 0.5
k ctrl+Return
sleep 40
shot 02-lines-folded

# ---- what a click does ----------------------------------------------------------------------
# One click per run (PHASE=command|reads|diff; without PHASE the run stops at the folded shot, so
# all four shots take four runs): a fold that opens scrolls the view to keep the newest output on
# screen, so after the first click the rows are no longer where this script counted them. The rows are 19 pixels apart from the turn's first row, and Ctrl+click
# always opens a link, whether or not the pane is the active one.
row() { echo $((169 + 19 * $1)); }
click() { xdotool mousemove --sync 120 "$(row "$1")"; sleep 0.4; xdotool keydown ctrl; xdotool click 1; xdotool keyup ctrl; sleep "${2:-3}"; }

case ${PHASE:-} in
  command)
    click 2                                  # ran python3 script
    xdotool click --repeat 10 4; sleep 1     # the wheel puts the anchor row back on screen
    shot 03-command-unfolded ;;
  reads)
    click 5; shot 04-merged-reads-unfolded   # read 4 files, each member linking to its file
    click 5 2; shot 06-member-opened-the-file ;;   # a member link: the file, in a preview pane
  diff)
    click 15 6; shot 05-big-diff-pane ;;     # edited big.txt -> the diff pane
esac

echo "wrote $out/pane-*.png"
