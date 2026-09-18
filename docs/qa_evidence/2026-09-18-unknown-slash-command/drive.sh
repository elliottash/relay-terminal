#!/usr/bin/env bash
# An unknown `/command` is answered by Relay, not by the shell (owner report, 2026-09-18).
#
#   docs/qa_evidence/2026-09-18-unknown-slash-command/drive.sh [build-dir]
#
# One fresh Relay under Xvfb with an isolated profile, on a throwaway workspace (this
# repository is never opened). No provider is configured and nothing is ever sent to a model:
# every line in the run is decided inside the pane.
#
#   1. `/comapct` typed, not yet submitted (the route text for it goes to the mode chip's
#      tooltip, which Qt only rebuilds on the next picker refresh, exactly as it does for a
#      known command — so this shot is the composer, not the label);
#   2. `/comapct` submitted — Relay's line, with `/compact` suggested;
#   3. `/nosuchthing` submitted — the same line with nothing close enough to suggest;
#   4. `/help` — the card `?` shows, reached by the command the unknown-command line names;
#   5. `/light` — a real slash command still runs (the theme switches to IBM Beige);
#   6. `/bin/echo relay-shell-ok` — an absolute path is still a command for the shell;
#   7. `!` then `/nosuchthing` — terminal mode gets the same line: a slash command is Relay's
#      in every mode, exactly as `/new` always was;
#   8. `/tmp` — a single-segment path that exists is NOT intercepted; the router decides, and
#      with no provider configured the pane says so. The point is that Relay's line is absent.
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1280 height=800
display=${RELAY_QA_DISPLAY:-:187}

sandbox=/tmp/relay-unknown-slash-$$
xvfb_pid= relay_pid=
cleanup() {
    [[ -n $relay_pid ]] && kill "$relay_pid" 2>/dev/null
    [[ -n $xvfb_pid ]] && kill "$xvfb_pid" 2>/dev/null
    rm -rf "$sandbox"
}
trap cleanup EXIT

[[ -e /tmp/.X11-unix/X${display#:} ]] && { echo "display $display is taken"; exit 1; }
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export HTTP_PROXY=http://127.0.0.1:1 HTTPS_PROXY=http://127.0.0.1:1 ALL_PROXY=http://127.0.0.1:1
export http_proxy=$HTTP_PROXY https_proxy=$HTTPS_PROXY all_proxy=$ALL_PROXY

export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF

t() { xdotool type --delay 16 "$1"; }
k() { xdotool key --delay 45 "$@"; }
park() { xdotool mousemove $((width + 20)) $((height + 20)); }

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

"$build/relay" --workspace "$work" >"$out/implementer-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 9
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2
park

# ---- 00: the line in the composer, before Enter -----------------------------------------
t '/comapct'; sleep 2
import -window "$win" "$out/implementer-00-preview-composer.png"

# ---- 01: submitted, with a suggestion ---------------------------------------------------
k Return; sleep 2.5
import -window "$win" "$out/implementer-01-unknown-with-suggestion.png"

# ---- 02: submitted, nothing close enough ------------------------------------------------
t '/nosuchthing'; sleep 1
k Return; sleep 2.5
import -window "$win" "$out/implementer-02-unknown-no-suggestion.png"
convert "$out/implementer-02-unknown-no-suggestion.png" -crop ${width}x260+0+40 +repage \
    "$out/implementer-02b-both-lines-detail.png"

# ---- 03: /help, the card the line points at ---------------------------------------------
t '/help'; sleep 1
k Return; sleep 2.5
import -window "$win" "$out/implementer-03-help-card.png"
k Escape; sleep 1

# ---- 04: a real slash command still runs ------------------------------------------------
t '/light'; sleep 1
k Return; sleep 3
import -window "$win" "$out/implementer-04-real-command-runs.png"

# ---- 05: an absolute path is still the shell's ------------------------------------------
t '/bin/echo relay-shell-ok'; sleep 1.5
import -window "$win" "$out/implementer-05a-path-preview-is-terminal.png"
k Return; sleep 4
import -window "$win" "$out/implementer-05-absolute-path-runs-in-shell.png"

# ---- 06: terminal mode gets the same answer ---------------------------------------------
t '!'; sleep 0.6
t '/nosuchthing'; sleep 1
k Return; sleep 2.5
import -window "$win" "$out/implementer-06-terminal-mode-same-line.png"

# ---- 07: an existing single-segment path is not a command -------------------------------
t '/tmp'; sleep 1
k Return; sleep 3
import -window "$win" "$out/implementer-07-existing-path-not-intercepted.png"

printf 'done: %s\n' "$out"
