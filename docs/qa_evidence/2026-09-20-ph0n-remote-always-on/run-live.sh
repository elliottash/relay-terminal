#!/bin/bash
R=/tmp/ph0n
rm -f $R/sidecar.jsonl $R/relay.log
cat > $R/home/.config/RelayTerminal/relay.conf <<'CONF'
[remote]
alwaysOn=false
CONF
export HOME=$R/home XDG_CONFIG_HOME=$R/home/.config XDG_DATA_HOME=$R/home/.local/share
export XDG_CACHE_HOME=$R/home/.cache XDG_STATE_HOME=$R/home/.state XDG_RUNTIME_DIR=$R/xdg TMPDIR=$R/tmp
export RELAY_KEYRING=off RELAY_REMOTE_DIR=$R/fake PH0N_LOG=$R/sidecar.jsonl
export DISPLAY=:97
Xvfb :97 -screen 0 1400x900x24 >$R/xvfb.log 2>&1 &
XPID=$!
sleep 2
/home/elliott/repos/relay-terminal/build/relay --fresh --workspace $R/ws >$R/relay.log 2>&1 &
RPID=$!
sleep 12
WIN=$(xdotool search --name "Relay" | tail -1)
xdotool windowfocus --sync $WIN 2>/dev/null; sleep 1
echo "=== 1. switch off: sidecar lines at launch: $(cat $R/sidecar.jsonl 2>/dev/null | wc -l) ==="
xdotool key ctrl+e; sleep 5    # two panes open before anything is switched on
echo "=== 2. two panes open, still nothing published: $(cat $R/sidecar.jsonl 2>/dev/null | wc -l) line(s) ==="
xdotool key ctrl+shift+o; sleep 3
xdotool type --delay 60 "remote control"; sleep 3
xdotool key Return; sleep 8
echo "=== 3. after switching Remote control on in Options ==="
grep -E '"t":"(start|pane)"' $R/sidecar.jsonl | cut -c1-200
echo "panes published: $(grep -c '"t":"pane"' $R/sidecar.jsonl)"
xdotool key Return; sleep 6
echo "=== 4. after switching it off again ==="
grep -E '"t":"(unpane|stop)"' $R/sidecar.jsonl | cut -c1-160
kill $RPID 2>/dev/null; sleep 3
echo "=== crashes: $(grep -c gui_crash $R/relay.log) ==="
grep -i "remote" $R/home/.config/RelayTerminal/relay.conf
kill $XPID 2>/dev/null
