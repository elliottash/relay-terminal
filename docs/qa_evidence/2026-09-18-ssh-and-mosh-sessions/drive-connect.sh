#!/usr/bin/env bash
# Connect to host…, Split on the same host and Options › Terminal › SSH (card #S5SH,
# docs/SSH-AND-MOSH.md section 8).
#
#   docs/qa_evidence/2026-09-18-ssh-and-mosh-sessions/drive-connect.sh [build-dir]
#
# One fresh Relay under Xvfb with a fully isolated profile (HOME, XDG_*, TMPDIR, RELAY_KEYRING=off).
# ~/.ssh/config lists filly (HostName, User, Port) and Includes config.d/*.conf with backup; a
# `Host *.internal` pattern must not be listed. `ssh` on PATH is a stand-in that prints its argv and
# waits, so nothing leaves the machine and the pane still sees an `ssh` in the foreground.
#
#   connect-palette      Actions, "connect to host": the recent host first, then the config hosts
#                        with user@hostname:port
#   connect-typed        "me@newbox" typed: a row for exactly that host
#   connect-new-tab      filly chosen: a new tab whose shell ran `ssh filly`
#   split-same-host      Split on the same host: a pane to the right running `ssh filly` again
#   options-ssh          Options › Terminal: the SSH rows
#
# Needs Xvfb, xdotool, ImageMagick and a C compiler.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${TMPDIR:-/tmp}/relay-ssh-connect-$$
xvfb_pid= relay_pid=
cleanup() {
    [[ -n $relay_pid ]] && kill "$relay_pid" 2>/dev/null
    [[ -n $xvfb_pid ]] && kill "$xvfb_pid" 2>/dev/null
    sleep 1; rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off

export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$HOME/run TMPDIR=$HOME/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work" "$XDG_RUNTIME_DIR" "$TMPDIR" \
         "$HOME/.ssh/config.d" "$HOME/bin"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$HOME/.ssh/config" <<'CONF'
Include config.d/*.conf
Host filly
    HostName 65.109.126.152
    User elliott
    Port 2222
Host *.internal
    User nobody
CONF
printf 'Host backup\n  HostName backup.example.org\n' >"$HOME/.ssh/config.d/backup.conf"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true

[ssh]
recent=old-box
CONF
cat >"$TMPDIR/fake-ssh.c" <<'C'
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv) {
    printf("FAKE-SSH argv:");
    for (int i = 0; i < argc; ++i) printf(" [%s]", argv[i]);
    printf("\n"); fflush(stdout);
    for (;;) pause();
}
C
cc -o "$HOME/bin/ssh" "$TMPDIR/fake-ssh.c" || exit 1
export PATH=$HOME/bin:$PATH

t() { xdotool type --delay 16 "$1"; }
k() { xdotool key --delay 60 "$@"; }
shot() { sleep 0.8; import -window root -crop ${width}x${height}+0+0 "$out/implementer-$1.png"; }

"$build/relay" --clean-shell --fresh --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 9
win=
for id in $(xdotool search --name "Relay" 2>/dev/null); do win=$id; done
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2

k ctrl+shift+a; sleep 1.2
t 'connect to host'; sleep 1.2
shot connect-palette
k ctrl+a BackSpace; t 'me@newbox'; sleep 1.2
shot connect-typed
k ctrl+a BackSpace; t 'connect filly'; sleep 1.2
k Return; sleep 5
shot connect-new-tab
k ctrl+shift+a; sleep 1.2
t 'same host'; sleep 1.2
k Return; sleep 5
shot split-same-host
k ctrl+shift+o; sleep 1.5
t 'ssh'; sleep 1.2
shot options-ssh
k Escape Escape; sleep 0.5

echo "ssh/recent after the run:"
grep -A2 '^\[ssh\]' "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
