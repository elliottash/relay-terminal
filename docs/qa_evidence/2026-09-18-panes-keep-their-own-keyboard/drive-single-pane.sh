#!/usr/bin/env bash
set -u
S=$1; OUT=$2
H=$(mktemp -d "$S/home-XXXXXX"); T=$(mktemp -d "$S/tmp-XXXXXX"); mkdir -p $H/run; chmod 700 $H/run
export XDG_RUNTIME_DIR=$H/run XDG_CONFIG_HOME=$H/config XDG_DATA_HOME=$H/data XDG_STATE_HOME=$H/state XDG_CACHE_HOME=$H/cache TMPDIR=$T RELAY_KEYRING=off
Xvfb :96 -screen 0 1000x500x24 >/dev/null 2>&1 & xv=$!
sleep 1; export DISPLAY=:96
/home/elliott/repos/relay-terminal/build/relay --clean-shell --fresh >$H/stderr.log 2>&1 & rp=$!
sleep 6
xdotool type --delay 15 "python3 -c 'import getpass; print(len(getpass.getpass()))'"; xdotool key Return
sleep 3
xdotool type --delay 15 "hunter2"; sleep 0.5
import -window root ${OUT%.png}-a.png
xdotool key Return; sleep 3
xdotool type --delay 15 "echo back-in-the-prompt-box"; sleep 0.5
import -window root ${OUT%.png}-b.png
kill -TERM $rp; sleep 2; kill $xv 2>/dev/null
