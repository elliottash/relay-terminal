#!/usr/bin/env bash
# Files on the host, in a real Relay under Xvfb (#S5SH, docs/SSH-AND-MOSH.md section 9).
#
# ssh localhost, print a path the host owns, walk to it with Ctrl+Shift+L, open it, edit it,
# save it over ssh, and read the bytes back on the host. Then end the login and try to save
# again, so the "the connection has ended" answer is photographed too.
#
# The file it edits is one this script makes in /tmp on the host and removes afterwards; no
# system file is touched. The GUI runs with its own HOME, XDG_* and TMPDIR.
#
#   BUILD=/path/to/build QA_DISPLAY=91 ./implementer-remote-file-drive.sh
set -uo pipefail
D=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:?set BUILD to a build directory holding ./relay}
OUT=${OUT:-$D}; mkdir -p "$OUT"
HOSTDIR=/tmp/relay-remote-qa-$$
JAIL=$(mktemp -d)

mkdir -p "$HOSTDIR"
cat >"$HOSTDIR/service.conf" <<'EOF'
# service.conf — this file lives on the host, not on the machine Relay runs on.
listen 8080;
workers 4;
log_level info;
EOF
chmod 640 "$HOSTDIR/service.conf"

export RELAY_KEYRING=off HOME="$JAIL/home" XDG_CONFIG_HOME="$JAIL/config" XDG_DATA_HOME="$JAIL/data" \
       XDG_CACHE_HOME="$JAIL/cache" XDG_RUNTIME_DIR="$JAIL/run" TMPDIR="$JAIL/tmp"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$JAIL/work" "$TMPDIR"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
printf '[instructions]\nonboarded=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
export DISPLAY=:${QA_DISPLAY:-91}

Xvfb "$DISPLAY" -screen 0 1400x900x24 >/dev/null 2>&1 & XVFB=$!
cleanup() {
    kill "${APP:-0}" "$XVFB" 2>/dev/null
    cp -r "$XDG_DATA_HOME/relay/logs" "$OUT/implementer-remote-file-logs" 2>/dev/null
    rm -rf "$JAIL" "$HOSTDIR"
}
trap cleanup EXIT
sleep 2
cd "$JAIL/work"; "$BUILD/relay" >"$OUT/implementer-remote-file-stdout.txt" 2>&1 & APP=$!
sleep 8

shot() { import -window root "$OUT/implementer-remote-file-$1.png"; }
type_() { xdotool type --delay 15 "$1"; sleep 0.4; }
run_() { type_ "$1"; xdotool key Return; sleep "${2:-2}"; }

run_ "ssh localhost" 7
run_ "ls $HOSTDIR/service.conf" 3
shot 01-the-host-printed-a-path

# Hover the path the host printed. It is underlined because the host answered `test -e` for it
# over this same connection — the first look queues the question, the answer lands a moment
# later, and the underline appears then. (Ctrl+Shift+L walks the same links, and for the same
# reason its first press on fresh output can come up empty; a second press finds them.)
xdotool mousemove 200 377; sleep 2
shot 02-the-links-are-the-hosts

xdotool click 1; sleep 4
shot 03-open-from-the-host

# The pane opens focused on the editor.
type_ "# edited in Relay over ssh"; xdotool key Return; sleep 1
shot 04-edited-not-yet-saved

xdotool key ctrl+s; sleep 3
shot 05-saved-over-ssh

# The bytes on the host, read by the host's own shell, in the terminal pane.
xdotool key alt+Left; sleep 1
run_ "cat $HOSTDIR/service.conf" 3
shot 06-the-host-has-the-new-bytes

# The login ends while the file is still open: the buffer stays, the pane says why. `exit` ends
# the session but not the shared master (ControlPersist keeps it for ten minutes, and a save
# still works over it, which is right); this asks the master itself to go, as closing the laptop
# lid would.
run_ "exit" 4
for sock in "$XDG_RUNTIME_DIR"/relay-ssh/*; do ssh -S "$sock" -O exit localhost >/dev/null 2>&1; done
sleep 1
xdotool key alt+Right; sleep 1
type_ "# typed after the connection ended"; xdotool key Return; sleep 1
xdotool key ctrl+s; sleep 2
shot 07-the-connection-ended

echo "--- the file on the host, after the save ---"
cat "$HOSTDIR/service.conf"
echo "--- mode (640 before, and after) ---"
stat -c %a "$HOSTDIR/service.conf"
