#!/bin/bash
# #FR1C task 1, live: the palette's "Pair a phone" on a desktop whose remote control is off.
# A short root (the 108-byte socket limit), an isolated HOME and XDG_*, and a stub sidecar behind
# RELAY_REMOTE_DIR. Run it from anywhere; it needs Xvfb, xdotool and build/relay.
R=/tmp/fr1c
mkdir -p $R/home/.config/RelayTerminal $R/home/.local/share $R/home/.cache $R/home/.state
mkdir -p $R/xdg $R/tmp $R/ws $R/fake/remote
cp "$(dirname "$0")/stub_gui_host.py" $R/fake/remote/gui_host.py
rm -f $R/sidecar.jsonl $R/relay.log
cat > $R/home/.config/RelayTerminal/relay.conf <<'CONF'
[remote]
alwaysOn=false
CONF
export HOME=$R/home XDG_CONFIG_HOME=$R/home/.config XDG_DATA_HOME=$R/home/.local/share
export XDG_CACHE_HOME=$R/home/.cache XDG_STATE_HOME=$R/home/.state XDG_RUNTIME_DIR=$R/xdg TMPDIR=$R/tmp
export RELAY_KEYRING=off RELAY_REMOTE_DIR=$R/fake FR1C_LOG=$R/sidecar.jsonl
export DISPLAY=:98
Xvfb :98 -screen 0 1400x1000x24 >$R/xvfb.log 2>&1 &
XPID=$!
sleep 2
/home/elliott/repos/relay-terminal/build/relay --fresh --workspace $R/ws >$R/relay.log 2>&1 &
RPID=$!
sleep 14
WIN=$(xdotool search --name "Relay" | tail -1)
xdotool windowfocus --sync $WIN 2>/dev/null; sleep 1
echo "=== 1. remote control off: sidecar lines at launch: $(cat $R/sidecar.jsonl 2>/dev/null | wc -l) ==="
# The palette, then the action the plug menu's first row runs.
xdotool key ctrl+shift+a; sleep 3
xdotool type --delay 60 "Pair a phone"; sleep 3
xdotool key Return; sleep 8
echo "=== 2. after Pair a phone: what the GUI sent ==="
cut -c1-160 $R/sidecar.jsonl
echo "=== 3. the settings it wrote ==="
grep -iA3 "^\[remote\]" $R/home/.config/RelayTerminal/relay.conf
import -window root $R/pairing-dialog.png 2>/dev/null
echo "=== 4. closing the pairing window ==="
# With no window manager the dialog does not take the focus by itself, so Escape is sent to every
# window that answers to its title.
for W in $(xdotool search --name "Share this pane"); do
    xdotool windowfocus --sync $W 2>/dev/null
    xdotool key --window $W Escape 2>/dev/null
done
sleep 4
grep pair_code_revoke $R/sidecar.jsonl | cut -c1-160
kill $RPID 2>/dev/null; sleep 3
echo "=== crashes: $(grep -c gui_crash $R/relay.log) ==="
kill $XPID 2>/dev/null
