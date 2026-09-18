#!/usr/bin/env bash
# Two panes; the LEFT one raises a password prompt 4 s after we have moved to the RIGHT one.
set -u
S=$1; OUT=$2
H=$(mktemp -d "$S/home-XXXXXX"); T=$(mktemp -d "$S/tmp-XXXXXX"); mkdir -p $H/run; chmod 700 $H/run
export XDG_RUNTIME_DIR=$H/run XDG_CONFIG_HOME=$H/config XDG_DATA_HOME=$H/data XDG_STATE_HOME=$H/state XDG_CACHE_HOME=$H/cache TMPDIR=$T RELAY_KEYRING=off
Xvfb :96 -screen 0 1400x700x24 >/dev/null 2>&1 & xv=$!
sleep 1; export DISPLAY=:96
/home/elliott/repos/relay-terminal/build/relay --clean-shell --fresh >$H/stderr.log 2>&1 & rp=$!
sleep 6
xdotool type --delay 15 "sleep 5; python3 -c 'import getpass; getpass.getpass()'"; xdotool key Return
sleep 1
xdotool key ctrl+e          # new pane on the right; it takes the keyboard
sleep 3
xdotool type --delay 15 "typed-in-RIGHT-before"
sleep 4                      # the left pane's password prompt appears during this wait
xdotool type --delay 15 "+AND+AFTER"
sleep 1
import -window root $OUT 2>/dev/null || xwd -root -silent | convert xwd:- $OUT
kill -TERM $rp; sleep 2; kill $xv 2>/dev/null
