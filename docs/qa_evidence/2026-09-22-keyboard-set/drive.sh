#!/usr/bin/env bash
# #QWAS #MAGP #CPRQ #SPSG #KYPR: the new keys, pressed in a real Relay under Xvfb with an isolated
# HOME, XDG_*, TMPDIR and RELAY_KEYRING=off. No model provider: nothing here runs an agent turn.
#
#   docs/qa_evidence/2026-09-22-keyboard-set/drive.sh [build-dir]
#
# 01 start · 02 Ctrl+Shift+P empty palette · 03 typing "rest" · 04 a group dropdown ·
# 05 right-click › Change shortcut… dialog · 06 Ctrl+Shift+A Board · 07 again: closed ·
# 08 Ctrl+Shift+S Sessions & Projects · 09 again: closed · 10 Ctrl+Shift+D explorer ·
# 11 a draft in the prompt box · 12 Ctrl+Q cleared · 13 Ctrl+Z restored · 14 Ctrl+? palette
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=1000
display=
for n in $(seq 600 639); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }
sandbox=$(mktemp -d /tmp/rl-keys.XXXX)
xvfb_pid= relay_pid=
cleanup() { local pid; for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
echo "notes" > "$work/README.md"
printf "PS1='\\\\w \\\\\$ '\nunset PROMPT_COMMAND\n" > "$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[security]
approvals_chosen=true
[url_handler]
announced=true
CONF
: >"$out/relay.log"
(cd "$work" && exec "$build/relay" --workspace "$work" --fresh --engine-core libvterm) >>"$out/relay.log" 2>&1 & relay_pid=$!
sleep 8
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$out/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2
t() { xdotool type --delay 25 "$1"; }
k() { xdotool key --delay 80 "$@"; }
shot() { sleep 1; import -window root -crop ${width}x${height}+0+0 "$out/$1.png"; }
shot 01-start
k ctrl+shift+p; shot 02-palette-empty
t 'rest'; shot 03-palette-search
k Escape; sleep 0.5; k ctrl+shift+p; sleep 0.5; k Tab; sleep 0.3; k Return; shot 04-group-dropdown
k Escape; sleep 0.3; k Escape; sleep 0.3; k Escape; sleep 0.5
k ctrl+shift+p; sleep 0.5; t 'board'; sleep 0.5; k Menu; sleep 0.8; k Return; shot 05-change-shortcut
k Escape; sleep 0.8
k ctrl+shift+a; sleep 2; shot 06-board-open
k ctrl+shift+a; shot 07-board-closed
k ctrl+shift+s; sleep 2; shot 08-sessions-open
k ctrl+shift+s; shot 09-sessions-closed
k ctrl+shift+d; sleep 2; shot 10-explorer
k ctrl+shift+d; sleep 1
t 'a long draft @README.md that I do not want to lose'; shot 11-draft
k ctrl+q; shot 12-cleared
k ctrl+z; shot 13-restored
k ctrl+question; shot 14-help-palette
k Escape
printf 'done: %s\n' "$out"
