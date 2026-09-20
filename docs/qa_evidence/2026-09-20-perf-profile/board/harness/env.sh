# shellcheck shell=bash
# Isolated environment for a profiling run of Relay's Switchboard.
#   source env.sh <display-number> <board-copy-dir> <run-tag>
# Keeps every path short: the abstract/unix socket paths are capped at 108 bytes.
R=/tmp/claude-1000/pf4k/board
D=${1:?display}
WS=${2:?workspace}
TAG=${3:?tag}
H=$R/run/$TAG
rm -rf "$H"; mkdir -p "$H"/{cfg,data,state,cache,tmp,xdg}
chmod 700 "$H/xdg"
# The pane agent asks systemd --user for a scope; without the session bus in the
# isolated XDG_RUNTIME_DIR that call fails and the worker exits code 1.
ln -sfn "/run/user/$(id -u)/bus" "$H/xdg/bus"
export DISPLAY=:$D
export XDG_CONFIG_HOME=$H/cfg XDG_DATA_HOME=$H/data XDG_STATE_HOME=$H/state
export XDG_CACHE_HOME=$H/cache XDG_RUNTIME_DIR=$H/xdg TMPDIR=$H/tmp
# A fresh profile otherwise opens the welcome dialog and the Approvals pane over the board,
# so the keystroke under test never reaches it.  Same settings the QA harnesses write.
mkdir -p "$H/cfg/RelayTerminal"
cat >"$H/cfg/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[theme]
name=relay-dark
[security]
approvals_chosen=true
approvals_ask=
CONF
export RELAY_KEYRING=off
export QT_QPA_PLATFORM=xcb
export RELAY_SESSION=pf4k-board
export RELAY_BIN=${RELAY_BIN:-/tmp/claude-1000/pf4k/build/relay}
export WS TAG H
