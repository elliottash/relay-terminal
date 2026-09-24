#!/usr/bin/env bash
# #J0VY Try it — open a sandboxed Relay (the fixed binary) with eight panes, let it idle a
# minute, and count `app_catalog_updated` events. The old code flooded them continuously
# (~50k in three hours with ~37 panes); with the content gate an idle minute should be quiet.
#
#   ./stage.sh                  # opens the app on your display; leaves it open for you
#   TRYIT_HEADLESS=1 ./stage.sh # my mechanical pass: Xvfb, screenshot, no window for you
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
bin=${RELAY_BIN:-$root/build/relay}
driver=$root/scripts/relay-drive
ev=$root/docs/qa_evidence/2026-09-24-tryit-J0VY
fix=/tmp/claude-1000/tryit/j0vy

rm -rf "$fix"; mkdir -p "$fix/home/.config/RelayTerminal" "$fix/run" "$fix/tmp" "$fix/work"
chmod 700 "$fix/run"
export DISPLAY=${TRYIT_DISPLAY:-:0}
xvfb=
if [[ ${TRYIT_HEADLESS:-0} == 1 ]]; then
    for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
    Xvfb "$display" -screen 0 1640x1040x24 >/dev/null 2>&1 & xvfb=$!
    export DISPLAY=$display
    sleep 2
fi
export RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET   # discover only this disposable instance, never the caller's app
export HOME=$fix/home XDG_RUNTIME_DIR=$fix/run TMPDIR=$fix/tmp \
       XDG_CONFIG_HOME=$fix/home/.config XDG_DATA_HOME=$fix/home/.local/share XDG_CACHE_HOME=$fix/home/.cache
ln -sfn "/run/user/$(id -u)/bus" "$fix/run/bus"
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

cleanup() { [[ -n ${relay:-} ]] && kill -TERM "$relay" 2>/dev/null || true; [[ -n ${xvfb:-} ]] && kill -TERM "$xvfb" 2>/dev/null || true; }
[[ ${TRYIT_HEADLESS:-0} == 1 ]] && trap cleanup EXIT

(cd "$fix/work" && exec "$bin" --workspace "$fix/work" --clean-shell --fresh) > "$fix/relay.log" 2>&1 & relay=$!
sleep 12
for i in 1 2 3 4 5 6 7; do "$driver" action pane.splitDown || "$driver" action pane.splitRight; sleep 1; done
sleep 60   # eight panes idle: the old code spends this whole minute echoing catalogs

log=$fix/home/.local/share/relay/logs/relay.log
count=$(grep -c app_catalog_updated "$log" || true)
echo "panes open: $("$driver" panes 2>/dev/null | python3 -c 'import json,sys; print(len(json.load(sys.stdin)["panes"]))' || echo '?')"
echo "app_catalog_updated events in $log after 60 idle seconds with those panes: $count"
echo "count again after you have used the app:"
echo "  grep -c app_catalog_updated $log"
if [[ ${TRYIT_HEADLESS:-0} == 1 ]]; then
    import -window root "$ev/01-eight-panes-idle.png"
    echo "screenshot: $ev/01-eight-panes-idle.png"
else
    echo "Relay is open on your display ($DISPLAY); close it when you are done. Nothing of yours was touched: isolated HOME $fix/home."
fi
