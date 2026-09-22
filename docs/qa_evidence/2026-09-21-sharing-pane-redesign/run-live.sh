#!/bin/bash
# #SHRP, live: the Sharing pane in each of its three states, on the real app under Xvfb with a
# stub sidecar behind RELAY_REMOTE_DIR (stub_gui_host.py). One scene per run:
#   run-live.sh quiet | guests | knock
# A short root (the 108-byte socket limit), an isolated HOME, XDG_* and TMPDIR, RELAY_KEYRING=off
# (docs/VALIDATION.md). Needs Xvfb, xdotool, ImageMagick's import and build/relay.
SCENE=${1:-quiet}
R=/tmp/shrp/$SCENE
OUT=${SHRP_OUT:-$(cd "$(dirname "$0")" && pwd)}
rm -rf $R
mkdir -p $R/home/.config/RelayTerminal $R/home/.local/share $R/home/.cache $R/home/.state
mkdir -p $R/xdg $R/tmp $R/ws/build $R/ws/logs $R/ws/deploy $R/fake/remote
chmod 700 $R/xdg
cp "$(dirname "$0")/stub_gui_host.py" $R/fake/remote/gui_host.py
cat > $R/home/.config/RelayTerminal/relay.conf <<'CONF'
[remote]
alwaysOn=true
address=relay-terminal.ai
CONF
export HOME=$R/home XDG_CONFIG_HOME=$R/home/.config XDG_DATA_HOME=$R/home/.local/share
export XDG_CACHE_HOME=$R/home/.cache XDG_STATE_HOME=$R/home/.state XDG_RUNTIME_DIR=$R/xdg TMPDIR=$R/tmp
export RELAY_KEYRING=off RELAY_REMOTE_DIR=$R/fake SHRP_LOG=$R/sidecar.jsonl SHRP_SCENE=$SCENE
export DISPLAY=:97
Xvfb :97 -screen 0 1500x1000x24 >$R/xvfb.log 2>&1 &
XPID=$!
sleep 2
/home/elliott/repos/relay-terminal/build/relay --fresh --workspace $R/ws >$R/relay.log 2>&1 &
RPID=$!
sleep 14
WIN=$(xdotool search --name "Relay" | tail -1)
xdotool windowfocus --sync $WIN 2>/dev/null; sleep 1
# Three panes with names of their own (a pane is titled by its directory; "!" is the shell
# prefix, since auto mode would ask a worker this fresh profile has none of), each published
# to the phones as it appears (always on). The Sharing pane opens beside the last one.
for title in build logs deploy; do
    xdotool type --delay 40 "!cd $R/ws/$title"; xdotool key Return; sleep 2
    if [ $title != deploy ]; then xdotool key ctrl+t; sleep 3; fi
done
if [ $SCENE = knock ]; then
    # The knock itself opens the Sharing pane, without taking the keyboard.
    sleep 4
else
    xdotool key ctrl+shift+a; sleep 3
    xdotool type --delay 60 "Sharing"; sleep 2
    xdotool key Return; sleep 5
fi
import -window root $OUT/$SCENE.png 2>/dev/null
echo "=== $SCENE: sidecar lines: $(wc -l < $R/sidecar.jsonl); panes published: $(grep -c '"t":"pane"' $R/sidecar.jsonl); devices asked: $(grep -c '"t":"devices"' $R/sidecar.jsonl) ==="
grep -o '"t":"[a-z_]*"' $R/sidecar.jsonl | sort | uniq -c | sort -rn | head -12
kill $RPID 2>/dev/null; sleep 3
echo "=== crashes: $(grep -c gui_crash $R/relay.log) ==="
kill $XPID 2>/dev/null
