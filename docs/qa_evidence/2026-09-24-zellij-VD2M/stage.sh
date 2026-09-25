#!/usr/bin/env bash
# Stage #VD2M's Try it: Relay with zellij on PATH, one pane running zellij, one seeded ssh
# host, and the new "Connect to host (persistent)" action one Actions-search away.
#
# Run with a DISPLAY already set to open Relay on your screen; without one it uses Xvfb (that
# is the machine pass, which leaves its screenshots beside this script). Everything lives under
# /tmp/claude-1000/tryit/vd2m and nothing touches your real HOME.
#
#   docs/qa_evidence/2026-09-24-zellij-VD2M/stage.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
bin=${RELAY_BIN:-$repo/build/relay}
zellij=${RELAY_TEST_ZELLIJ:-$HOME/.local/bin/zellij}
[[ -x $bin ]] || { echo "no relay binary at $bin (build it or set RELAY_BIN)" >&2; exit 1; }
[[ -x $zellij ]] || { echo "no zellij at $zellij (set RELAY_TEST_ZELLIJ)" >&2; exit 1; }

work=/tmp/claude-1000/tryit/vd2m
rm -rf "$work"; mkdir -p "$work/project" "$work/shots"
sandbox=$work/sandbox
mkdir -p "$sandbox/home/.local/bin" "$sandbox/home/.ssh" "$sandbox/home/.config/RelayTerminal" \
         "$sandbox/home/.config/zellij" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
ln -sfn "$zellij" "$sandbox/home/.local/bin/zellij"
cp /etc/skel/.profile "$sandbox/home/.profile" 2>/dev/null || true
# Without --clean-shell the pane shell sources .bashrc: exec zellij, so the first pane IS a
# zellij (tips overlay on first run; ESC dismisses it — the thing to judge is what is around it).
printf '[ -n "${ZELLIJ-}" ] || exec ~/.local/bin/zellij --session tryit\n' > "$sandbox/home/.bashrc"
printf 'Host filly-test\n    HostName 127.0.0.1\n    User %s\n' "$USER" > "$sandbox/home/.ssh/config"
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$sandbox/home/.config/RelayTerminal/relay.conf"

export RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET   # discover only this disposable instance, never the caller's app
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp \
       XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share \
       XDG_CACHE_HOME=$sandbox/home/.cache

if [[ -z ${DISPLAY:-} ]]; then
    display=; for n in $(seq 200 259); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
    [[ -n $display ]] || { echo "no free Xvfb display" >&2; exit 1; }
    Xvfb "$display" -screen 0 1680x1040x24 >/dev/null 2>&1 & echo $! > "$work/xvfb.pid"
    sleep 2; export DISPLAY=$display
fi

(cd "$work/project" && exec "$bin" --workspace "$work/project" --fresh) > "$work/relay.log" 2>&1 &
echo $! > "$work/relay.pid"
sleep 12
echo "Relay pid $(cat "$work/relay.pid") on DISPLAY=$DISPLAY, HOME=$sandbox/home"
echo "Pane 1 runs: zellij --session tryit (ESC dismisses its first-run tips)."
echo "Actions (Ctrl+T or 'palette.open') -> search 'persistent' -> Enter opens the host picker."
