#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# The crash the #SWPH hosted drive found, without a phone: a stub sidecar (stub.py) plays the hub.
# The Switchboard pane is opened (board_open is answered), closed by its own x, and four seconds
# later the "phone" files a card. Relay runs under gdb; gdb.log has the backtrace if it dies.
#   repro.sh <build dir> <source dir>
S=$(cd "$(dirname "$0")" && pwd); build=$1; src=$2
R=$(mktemp -d /tmp/swpr.XXXXXX); export PATH=/usr/local/bin:/usr/bin:/bin
mkdir -p $R/home $R/config/RelayTerminal $R/data $R/cache $R/state $R/tmp $R/fake/remote; mkdir -m 700 $R/run
cp $S/stub.py $R/fake/remote/gui_host.py
python3 $src/docs/qa_evidence/2026-09-21-swph-hosted-drive/fixture.py $R/harbour $src >/dev/null 2>&1 || python3 /home/elliott/repos/relay-terminal/docs/qa_evidence/2026-09-21-swph-hosted-drive/fixture.py $R/harbour $src
cat > $R/config/RelayTerminal/relay.conf <<'CONF'
[isolation]
enabled=false
[security]
approvals_chosen=true
[remote]
alwaysOn=true
[provider]
preset=custom
base=http://127.0.0.1:9/v1
model=none
[instructions]
onboarded=true
CONF
export HOME=$R/home XDG_CONFIG_HOME=$R/config XDG_DATA_HOME=$R/data XDG_CACHE_HOME=$R/cache XDG_STATE_HOME=$R/state XDG_RUNTIME_DIR=$R/run TMPDIR=$R/tmp
export RELAY_KEYRING=off RELAY_REMOTE_DIR=$R/fake SWPH_LOG=$R/sidecar.jsonl SWPH_CMDS=$R/cmds.jsonl
: > $R/cmds.jsonl
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { D=:$n; break; }; done
Xvfb $D -screen 0 1700x1100x24 >/dev/null 2>&1 & XP=$!; sleep 1; export DISPLAY=$D
gdb -q -batch -ex "set debuginfod enabled off" -ex "handle SIGPIPE nostop noprint pass" -ex "break exit" -ex "break _exit" -ex "break abort" -ex run -ex "bt 45" --args $build/relay --workspace $R/harbour > $R/gdb.log 2>&1 &
GP=$!
for i in $(seq 1 60); do W=$(xdotool search --onlyvisible --name "^Relay" 2>/dev/null | head -1); [[ -n $W ]] && break; sleep 1; done
sleep 6; W=$(xdotool search --onlyvisible --name "^Relay" | head -1)
xdotool windowmove $W 0 0 windowsize $W 1600 1000 windowfocus $W; sleep 2
xdotool mousemove --window $W 400 925 click 1; sleep 0.5; xdotool key --delay 70 ctrl+shift+s; sleep 6
echo '{"type":"board_open"}' >> $R/cmds.jsonl; sleep 4
import -window $W $S/r1-open.png
xdotool mousemove --window $W 1569 58 click 1; sleep 4
import -window $W $S/r2-closed.png
echo '{"type":"board_create","tab":"features","title":"From the phone","request":"after the pane closed","labels":[]}' >> $R/cmds.jsonl
sleep 8
if kill -0 $GP 2>/dev/null; then echo "STILL RUNNING after board_create"; timeout 10 import -window $W $S/r3-after.png; fi
grep -c board_event $R/sidecar.jsonl; grep '"board_event"' $R/sidecar.jsonl | tail -3 | cut -c1-300
pkill -TERM -P $GP 2>/dev/null; sleep 3; kill $GP 2>/dev/null
cp $R/gdb.log $S/gdb.log; tail -5 $R/data/relay/logs/relay.log | cut -c1-200
kill $XP; rm -rf $R
