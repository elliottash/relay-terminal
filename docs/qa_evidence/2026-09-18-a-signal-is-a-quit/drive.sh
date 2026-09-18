#!/usr/bin/env bash
# Start Relay isolated under Xvfb, SIGTERM it, report what it left behind.
set -u
BIN=${1:-/home/elliott/repos/relay-terminal/build/relay}
H=$(mktemp -d "$2/home-XXXXXX"); T=$(mktemp -d "$2/tmp-XXXXXX")
mkdir -p $H/run && chmod 700 $H/run; export XDG_RUNTIME_DIR=$H/run XDG_CONFIG_HOME=$H/config XDG_DATA_HOME=$H/data XDG_STATE_HOME=$H/state XDG_CACHE_HOME=$H/cache TMPDIR=$T RELAY_KEYRING=off
Xvfb :97 -screen 0 1280x800x24 >/dev/null 2>&1 & xv=$!
sleep 1
DISPLAY=:97 "$BIN" --clean-shell >$H/stderr.log 2>&1 & rp=$!
sleep 6
echo "before: tmp dirs=$(ls $T | wc -l)  workers=$(pgrep -f -c "worker.py" -P $rp 2>/dev/null)"
kill -TERM $rp; 
for i in $(seq 1 50); do kill -0 $rp 2>/dev/null || break; sleep 0.1; done
wait $rp; echo "relay exit status=$?"
sleep 0.5
echo "after:  tmp dirs left=$(ls $T | wc -l) -> $(ls $T | tr '\n' ' ')"
echo "layout saved: $(find $H -name 'windows.json' | wc -l)   scrollback files: $(find $H -path '*scrollback*' -type f | wc -l)"
kill $xv 2>/dev/null
