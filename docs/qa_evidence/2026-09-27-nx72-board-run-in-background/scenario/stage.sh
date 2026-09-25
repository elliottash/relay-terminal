#!/usr/bin/env bash
# #NX72 Try it staging — the AI's mechanical pass over the staged situation.
# Launches the scratch-built relay binary (working tree with the #NX72 fix) on a disposable
# board fixture under Xvfb on an isolated profile, opens card TRY1, presses Run (background,
# action boardExecute) and captures the layout at 0 / 300 / 1500 ms plus panes and notice.
# The pass assertion is in expected.md (sealed); the person's copy is the Try it block on the
# card. Usage: stage.sh <staged-dir> <out-dir> ; env: RELAY_BIN, S (scratch root not needed).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
driver=$repo/scripts/relay-drive
bin=${RELAY_BIN:?}
work=${1:?}; out=${2:?}; mkdir -p "$out"; width=1600 height=1000
display=; for n in $(seq 200 229); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-nx72.XXXX)
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done; sleep 1; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME="$sandbox/home/.local/share" XDG_CACHE_HOME="$sandbox/home/.cache"
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) > "$out/relay.log" 2>&1 & relay_pid=$!
sleep 12
drive() { "$driver" "$@"; }
shot() { import -window root "$out/$1.png"; }
for i in $(seq 1 30); do "$driver" panes > "$out/00-panes-boot.json" 2>/dev/null && break; sleep 1; done
drive open TRY1
for i in $(seq 1 60); do drive read sections > /dev/null 2>&1 && break; sleep 1; done
shot 01-card-open
drive panes > "$out/02-panes-before.json"
drive press boardExecute
sleep 0.4   # the first Run arms a "no plan yet" confirmation strip; the second hands over
drive press boardExecute
shot 03-just-after
sleep 0.3;  shot 04-after-300ms
sleep 1.2;  shot 05-after-1500ms
drive read sections > "$out/06-sections-after.json" || true
drive read notice > "$out/07-notice.json" || true
drive panes > "$out/08-panes-after.json"
echo done
