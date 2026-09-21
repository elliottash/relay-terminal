#!/usr/bin/env bash
# A command's own sentence survives the switch it asked for (card #MDL1): `model_changed` lands
# 100-300 ms after any model switch and its generic "model: … · conversation kept" used to replace
# whatever the command had just printed. /swap was fixed with a one-shot in e7cab7d2; /glm and
# /kimi now go through the same one (`sayAndSwitch`).
#
#   m1  /glm, half a second after Return: the command's own line
#   m2  the same line three seconds later, after `model_changed` has been and gone
#   m3  the box, unchanged by the two follow-up commits (conciseModel's removal, sayAndSwitch)
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-model-box-modes
build=$root/build
width=1600 height=900
display=
for n in $(seq 460 490); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }
sandbox=/tmp/claude-1000/bs
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; }
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key RELAY_GLM_API_KEY=xvfb-not-a-real-key-two
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-either
k() { xdotool key --delay 60 "$@"; }
t() { xdotool type --delay 35 "$1"; }
rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[isolation]
enabled=false
[provider]
preset=kimi-code
[models]
tier\\main=kimi-code|k3|high, glm-coding|glm-5.3|max, glm|glm-5.3|max
tier\\high=glm-coding|glm-5.3|max, openai|gpt-5.6-sol|max
tier\\flash=glm-coding|glm-5.3-flash|low, kimi-code|k3|high
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
approvals_ask=@Invalid()
[url_handler]
announced=true
CONF
win=
for w in $(true); do :; done
"$build/relay" --workspace "$work" >>"$out/relay-sentence-stderr.log" 2>&1 & relay_pid=$!
sleep 10
best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4
crop() { convert "$out/$1.png" -crop "${width}x420+0+$((height - 420))" +repage "$out/$1-box.png"; }

xdotool mousemove 300 $((height - 88)) click 1; sleep 0.6
t "/glm"; k Return
sleep 0.6; xdotool mousemove $((width + 20)) $((height + 20)); import -window root "$out/m1-glm-said.png"; crop m1-glm-said
sleep 3.0; xdotool mousemove $((width + 20)) $((height + 20)); import -window root "$out/m2-glm-still-said.png"; crop m2-glm-still-said
k alt+m; sleep 1.8
xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3
import -window root "$out/m3-box-after.png"; crop m3-box-after
k Escape; sleep 0.5
echo "done"
