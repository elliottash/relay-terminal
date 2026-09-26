#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #R660 — the linked pane chain: shell -> TeX editor -> PDF preview. Replays the live pass whose
# shots are beside this file (README.md says what each shows):
#
#   docs/qa_evidence/2026-09-25-tex-chains/drive.sh [relay-binary]
#
# Isolated HOME / XDG dirs / TMPDIR, RELAY_KEYRING=off, Xvfb display :110, no provider account.
# The scratch project is a fresh directory in the sandbox HOME, never the checkout's.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../../.." && pwd)
relay=${1:-$root/build/relay}
display=:110
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-r660.XXXX)
relay_pid= xvfb_pid=
cleanup() { kill $relay_pid $xvfb_pid 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 1540x980x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off RELAY_DATA_DIR=$root
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.config/relay" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
# A `relay` like the installed one: `relay open PATH` forwards to the open helper that Relay's
# panes get on their PATH; anything else runs this build's binary.
mkdir -p "$sandbox/bin"
cat >"$sandbox/bin/relay" <<SHIM
#!/usr/bin/env bash
if [[ \${1:-} == open ]]; then shift; exec "\$RELAY_OPEN_HELPER" "\$@"; fi
exec "$relay" "\$@"
SHIM
chmod +x "$sandbox/bin/relay"
export PATH="$sandbox/bin:$PATH"
printf "PS1='\\\\w \\\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

project=$HOME/project
mkdir -p "$project"
cat >"$project/main.tex" <<'TEX'
\documentclass{article}
\title{Chain notes}
\begin{document}
\maketitle
Hello from the chain: a shell, this editor, and a PDF preview.
\end{document}
TEX

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[security]
approvals_chosen=true
[terminal]
persistLocal=false
CONF

(cd "$project" && exec "$relay" --workspace "$project" --fresh) >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 12
win=$(for w in $(xdotool search --pid "$relay_pid"); do
          eval "$(xdotool getwindowgeometry --shell "$w")"; echo "$((WIDTH * HEIGHT)) $w"
      done | sort -n | tail -1 | cut -d' ' -f2)
[[ -z $win ]] && { echo "no Relay window"; tail -30 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 940
sleep 3

shot() { import -window root "$here/$1.png"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep "${3:-1}"; }
t() { xdotool type --delay 30 "$1"; }

# 1. The shell offers: `relay open main.tex` -> "Open beside, linked".
click 400 500 0.5; t 'relay open main.tex'; xdotool key Return; sleep 2.5
shot 01-open-beside-linked-offer
xdotool key Return; sleep 2.5          # the offer's default: Open beside, linked
shot 02-shell-and-editor-chained

# 2. The preview joins as the chain's tail.
python3 "$root/scripts/relay-drive" workspace next >/dev/null 2>&1; sleep 2
shot 03-chain-of-three

# 3. Build in the shell; the PDF lands in the preview with its generation.
click 300 500 0.4; t 'latexmk -pdf -interaction=nonstopmode main.tex'; xdotool key Return
sleep 14
shot 04-built-pdf-generation-1

# 4. Edit in the linked editor, build again: the generation moves.
click 1100 500 0.5; xdotool key End; t ' Edited across the chain.'; xdotool key ctrl+s; sleep 1.5
click 300 500 0.4; t 'latexmk -pdf -interaction=nonstopmode main.tex'; xdotool key Return
sleep 14
shot 05-edited-and-rebuilt-generation-2

# 5. What the tab saved (the chain, head-first, with upstreams).
python3 "$root/scripts/relay-drive" workspace state >"$here/06-workspace-state.json" 2>&1 || true

# 6. Restart: the three panes come back linked.
kill $relay_pid; wait $relay_pid 2>/dev/null; relay_pid=
sleep 3
(cd "$project" && exec "$relay" --workspace "$project") >"$sandbox/relay2.log" 2>&1 &
relay_pid=$!
sleep 12
shot 07-restarted-chain-restored
python3 "$root/scripts/relay-drive" panes >"$here/11-panes-after-restart.json" 2>&1 || true

# 7. Closing the head asks about the rest: the chrome's × on the shell (its pane spans x 0..329;
#    the panes start under the tab strip, so the chrome row is near y 90).
click 314 92 2
shot 08-close-the-linked-panes-ask
xdotool key Escape; sleep 1.5          # "Just this pane" — the chain heals, the shell stays
shot 09-shell-left-chain-heals
python3 "$root/scripts/relay-drive" panes >"$here/10-panes-after-just-this-pane.json" 2>&1 || true

echo "pass done; evidence beside $here"
