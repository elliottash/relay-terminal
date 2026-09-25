#!/usr/bin/env bash
# #NX72 probe: what does the card page render right after Run (background)?
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../../../.." && pwd)
driver=$repo/scripts/relay-drive; bin=${RELAY_BIN:?}; work=${1:?}; out=${2:?}; mkdir -p "$out"
display=; for n in $(seq 200 229); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-nx72p.XXXX)
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done; sleep 1; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 1640x1040x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off; unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME="$sandbox/home/.config" XDG_DATA_HOME="$sandbox/home/.local/share" XDG_CACHE_HOME="$sandbox/home/.cache"
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) > "$out/relay.log" 2>&1 & relay_pid=$!
sleep 12
drive() { "$driver" "$@"; }
for i in $(seq 1 30); do drive panes > /dev/null 2>&1 && break; sleep 1; done
drive open TRY1
for i in $(seq 1 60); do drive read sections > "$out/sections-before.json" 2>/dev/null && break; sleep 1; done
drive read boardBusyLabel > "$out/busy-before.json" 2>&1 || true
drive press boardExecute
sleep 2
drive read sections > "$out/sections-after.json" 2>&1 || true
drive read boardBusyLabel > "$out/busy-after.json" 2>&1 || true
drive read boardCardError > "$out/error-after.json" 2>&1 || true
drive panes > "$out/panes-after.json"
drive read notice > "$out/notice-after.json" 2>&1 || true
# The first Run arms a "no plan yet" confirmation; the second hands the card over.
drive press boardExecute
import -window root "$out/10-just-after-second-press.png"
sleep 0.3; import -window root "$out/11-second-300ms.png"
sleep 1.2; import -window root "$out/12-second-1500ms.png"
drive read sections > "$out/sections-after2.json" 2>&1 || true
drive read notice > "$out/notice-after2.json" 2>&1 || true
drive panes > "$out/panes-after2.json"
echo probe-done
