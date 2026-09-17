#!/usr/bin/env bash
# Live check of "the prompt box is the only keyboard input" (Warp-style), under Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-17-prompt-box-only-input/drive.sh [build-dir] [engine]
#
# engine: "relay" (the process default) or "konsole". Writes implementer-NN-*.png next to this script
# plus relay-stderr.log. Needs Xvfb, xdotool and ImageMagick `import`. An isolated
# XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the real profile.
#
# No real secret is ever typed: the password checks use a throwaway wrong password.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
engine=${2:-relay}
display=:93
suffix=""
[[ $engine == konsole ]] && suffix="-konsole"

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

# A program that asks for a password with echo off (ICANON on, ECHO off), like sudo.
cat >"$work/askpass.py" <<'PY'
import getpass
secret = getpass.getpass("Repository password: ")
print("length %d, starts with %r" % (len(secret), secret[:1]))
PY
# A program that asks an ordinary [Y/n] question on stdin.
cat >"$work/confirm.py" <<'PY'
import sys
sys.stdout.write("Do you want to continue? [Y/n] ")
sys.stdout.flush()
answer = sys.stdin.readline().strip()
print("answer=%r" % answer)
PY

Xvfb "$display" -screen 0 1400x900x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window root "$out/implementer-$1$suffix.png"; }
place() { xdotool windowmove "$win" 0 0 windowsize "$win" 1400 900; xdotool windowfocus "$win"; sleep 1; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }

"$build/relay" --engine "$engine" --workspace "$work" >"$out/relay-stderr$suffix.log" 2>&1 &
relay_pid=$!
sleep 6
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
place
xdotool mousemove 400 300
sleep 2
shot 01-start

# 1. Clicking the terminal must not take the keyboard: click in the middle of the terminal
#    area, then type. The text has to appear in the prompt box, not in the shell.
xdotool mousemove 700 300 click 1; sleep 1
t 'echo typed after clicking the terminal'
sleep 1
shot 02-click-terminal-then-type-goes-to-prompt-box
# Drag-select in the terminal still works (the selection shows in the screenshot).
k ctrl+a; k BackSpace; sleep 0.5
t 'seq 1 5'; k Return; sleep 2
xdotool mousemove 120 200 mousedown 1 mousemove 400 260 sleep 0.4 mouseup 1
sleep 0.6
shot 03-selection-still-works
k ctrl+a; k BackSpace; sleep 0.5

# 2. Full-screen program: vim. No automatic switch any more — a "Take control (Ctrl+H)"
#    button appears and the prompt box keeps the focus.
t 'vim -u NONE -N notes.txt'; k Return; sleep 3
shot 04-vim-take-control-button
t 'still typing into the prompt box'
sleep 1
shot 05-vim-prompt-box-still-has-focus
k ctrl+a; k BackSpace; sleep 0.5
# Ctrl+H hands the keyboard over; the prompt box hides and vim sees the keys.
k ctrl+h; sleep 1.5
k i; t 'typed inside vim after Ctrl+H'; sleep 0.5
shot 06-vim-native-after-ctrl-h
k Escape; sleep 0.5
# Ctrl+Shift+H comes back to the prompt box while vim is still running.
k ctrl+shift+h; sleep 1.5
shot 07-back-to-prompt-box-while-vim-runs
# Leave vim the way the user would: take control again and quit.
k ctrl+h; sleep 1; k Escape; sleep 0.3; t ':q!'; k Return; sleep 2
shot 08-after-vim

# 3. Password prompt: echo off with canonical input. The prompt box becomes masked and the
#    line goes to the program. The password below is a throwaway, not a real secret.
place
t 'python3 askpass.py'; k Return; sleep 3
shot 09-password-masked-prompt-box
t 'wrong-password-not-a-real-secret'; sleep 1
shot 10-password-typed-masked
k Return; sleep 2.5
shot 11-password-sent-to-program
# Prompt history must not hold it: Up in the empty prompt box shows the last command.
sleep 1
k Up; sleep 1
shot 12-history-after-password-has-no-secret
k ctrl+a; k BackSpace; sleep 0.5

# 3b. The same with a real elevator. `sudo -k true; sudo true` only asks where sudo is
#     configured to; `su root -c true` always does. Both are opaque programs (Relay cannot
#     read their syscalls), so only the line discipline gives the password prompt away.
#     The password below is a throwaway and is expected to be rejected.
t 'sudo -k true'; k Return; sleep 2
t 'sudo true'; k Return; sleep 3
shot 13a-sudo
k ctrl+a; k BackSpace; sleep 0.5
t 'su root -c true'; k Return; sleep 3
shot 13-su-masked-chip
t 'wrong-password-not-a-real-secret'; k Return; sleep 5
shot 14-su-after-wrong-password
k ctrl+a; k BackSpace; sleep 0.5

# 4. An ordinary program reading stdin: the submitted line answers it ("Sent to python3").
place
t 'python3 confirm.py'; k Return; sleep 3
shot 15-confirm-waiting-for-input
t 'n'; sleep 0.5
k Return; sleep 2.5
shot 16-confirm-answered-from-prompt-box

# 5. F12 still works for people who want the old behaviour.
place
k F12; sleep 1.5
t 'echo native input still works'; k Return; sleep 1.5
shot 17-f12-native-input
k F12; sleep 1.5
shot 18-final
echo "screenshots in $out"
