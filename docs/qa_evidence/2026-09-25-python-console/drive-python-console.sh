#!/bin/bash
# Card #83YV, t:7c — live evidence for the Python console pane, driven under Xvfb :107 with an
# isolated profile. The app, the worker, the shared kernel and the pty are real; the model half
# is the local stub-provider.py (this repo's evidence pattern, see
# docs/qa_evidence/2026-09-20-agent-console-extraction/). Produces 01-10 PNGs, panes.json,
# state-copy/ and drive.log beside this script.

set -uxo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$DIR/../../.." && pwd)"
OUT="$DIR"
: "${VENV:=/tmp/83yv-venv}"            # ipykernel, jupyter_client, jupyter_console
: "${PORT:=8791}"
: "${DISPLAY_NUM:=107}"

cleanup() {
  [ -n "${APP_PID:-}" ] && kill "$APP_PID" 2>/dev/null
  [ -n "${STUB_PID:-}" ] && kill "$STUB_PID" 2>/dev/null
  [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
  sleep 1; pkill -f "Xvfb :$DISPLAY_NUM" 2>/dev/null; true
}
trap cleanup EXIT

# --- isolated profile -----------------------------------------------------------------------
PROFILE=$(mktemp -d /tmp/83yv-profile.XXXXXX)
export HOME="$PROFILE"
export XDG_RUNTIME_DIR="$HOME/run"; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
export XDG_CONFIG_HOME="$HOME/.config"; mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
export RELAY_KEYRING=off
WS=$(mktemp -d /tmp/83yv-ws.XXXXXX); echo "notes" > "$WS/notes.txt"
cat > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<EOF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[approvals]
mode=allow
EOF
# The endpoint of the evidence pattern (drive-console.sh of 2026-09-20): a local OpenAI-compatible
# provider whose model is stub-provider.py, so no key is needed and nothing leaves the machine.
mkdir -p "$XDG_CONFIG_HOME/relay"
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub-1", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$PORT" >"$XDG_CONFIG_HOME/relay/local-models.json"

# --- display, stub, app ---------------------------------------------------------------------
Xvfb :$DISPLAY_NUM -screen 0 1600x1000x24 & XVFB_PID=$!
sleep 2
export DISPLAY=:$DISPLAY_NUM
python3 "$DIR/stub-provider.py" "$PORT" & STUB_PID=$!
sleep 1
PATH="$VENV/bin:$PATH" "$REPO/build/relay" --fresh --workspace "$WS" >"$OUT/drive.log" 2>&1 &
APP_PID=$!

WIN=""
for i in $(seq 1 40); do
  WIN=$(xdotool search --class relay 2>/dev/null | head -1)
  [ -n "$WIN" ] && break; sleep 1
done
[ -z "$WIN" ] && { echo "NO WINDOW"; exit 1; }
xdotool windowactivate --sync "$WIN"; sleep 3
shot() { import -window root "$OUT/$1"; }

# publishSocketAddress (RelayWindow.h) writes $XDG_RUNTIME_DIR/relay/open-socket — a regular
# file whose contents are the local socket address relay-drive talks to.
SOCKET_FILE="$XDG_RUNTIME_DIR/relay/open-socket"
for i in $(seq 1 30); do [ -f "$SOCKET_FILE" ] && break; sleep 1; done
[ -f "$SOCKET_FILE" ] || { echo "NO DRIVE SOCKET"; exit 1; }
DRIVE="python3 $REPO/scripts/relay-drive --socket $(cat "$SOCKET_FILE")"
shot 01-window.png

# --- palette: New Python console (t:8m) ------------------------------------------------------
$DRIVE action palette.open; sleep 1
xdotool type --delay 40 'new python'; sleep 1
shot 02-palette.png
xdotool key Return

# --- the console pane attaches (t:8m): kernel up, then `jupyter console --existing` in the pty
for i in $(seq 1 60); do pgrep -af "ipykernel" | grep -F "$HOME" >/dev/null && { echo "kernel up after ${i}s"; break; }; sleep 1; done
pgrep -af "ipykernel|jupyter" | grep -F "$HOME" >>"$OUT/drive.log" 2>&1 || true
for i in $(seq 1 40); do pgrep -af "jupyter-console|jupyter console" | grep -F "$HOME" >/dev/null && { echo "console attached after ${i}s"; break; }; sleep 1; done
sleep 8                                        # banner and In [1] prompt
$DRIVE panes >"$OUT/panes.json" 2>&1 || true
shot 03-console-attached.png

# --- t:4t/t:dq: statements to the program ----------------------------------------------------
# The console pane is the right half; its composer is the strip above the status row at the bottom
# of the WINDOW (which need not fill the screen). The pane's composer starts in PROGRAM mode
# (#S976) — statements go straight to the program — so put it in AUTO for the routing half of
# t:dq: the router then sends Python to the program and prose to the agent.
eval $(xdotool getwindowgeometry --shell "$WIN")   # X, Y, WIDTH, HEIGHT
INPUT_X=$((X + WIDTH * 3 / 4)); INPUT_Y=$((Y + HEIGHT - 85))
TERM_X=$((X + WIDTH * 3 / 4)); TERM_Y=$((Y + HEIGHT / 2))
xdotool mousemove $TERM_X $TERM_Y click 1; sleep 1     # focus the console pane
$DRIVE action input.modeAuto; sleep 1
xdotool mousemove $INPUT_X $INPUT_Y click 1; sleep 1
xdotool type --delay 60 'x = 41'; xdotool key Return; sleep 3
shot 04-x-is-41.png

# Tab completes from the python/ipython table (t:dq)
xdotool type --delay 60 'pri'
xdotool key --delay 120 Tab; sleep 1
shot 05-tab-completion.png
xdotool key Escape; xdotool mousemove $TERM_X $TERM_Y click 1; sleep 1   # clear the line at the prompt
xdotool key ctrl+c; sleep 1

# --- prose to the agent (t:dq), shared kernel: pty shows print(x + 1) and 42 -----------------
xdotool type --delay 40 'add one to x and print it'; xdotool key Return
sleep 20                                        # agent turn: py_run_cell, py_variables, reply
shot 06-agent-42.png
sleep 3
shot 07-py-variables.png

# --- Ctrl+C interrupts a time.sleep -----------------------------------------------------------
xdotool type --delay 60 'import time'; xdotool key Return; sleep 2
xdotool type --delay 60 'time.sleep(30)'; xdotool key Return; sleep 2
xdotool mousemove $TERM_X $TERM_Y click 1; sleep 1      # the interrupt goes to the pty
xdotool key ctrl+c; sleep 3
shot 08-interrupt.png

# --- restart clears x (t:7c) -------------------------------------------------------------------
xdotool mousemove $TERM_X $TERM_Y click 1; sleep 1     # focus the console pane
$DRIVE action pane.restartShell
for i in $(seq 1 40); do pgrep -af "jupyter-console|jupyter console" | grep -F "$HOME" >/dev/null && break; sleep 1; done
sleep 8
xdotool mousemove $INPUT_X $INPUT_Y click 1; sleep 1
xdotool type --delay 60 'x'; xdotool key Return; sleep 4
shot 09-restart-cleared.png

# --- the menu entry (t:8m): "New Python console" beside the shell entries ---------------------
xdotool mousemove $TERM_X $TERM_Y click 3; sleep 1
shot 10-menu.png
xdotool key Escape

# --- wrap up: the pane's own text, then everything down ----------------------------------------
sleep 2; kill "$APP_PID"; sleep 4
mkdir -p "$OUT/state-copy"
cp -r "$XDG_CONFIG_HOME/RelayTerminal" "$OUT/state-copy/" 2>/dev/null || true
cp -r "$HOME/.local/share/relay/logs" "$OUT/state-copy/logs" 2>/dev/null || true
pgrep -af "ipykernel|jupyter|relay" | grep -F "$HOME" >>"$OUT/drive.log" 2>&1 || true
echo DONE
