#!/usr/bin/env bash
# Skills as `/name` in the composer (feature intake 2026-09-18: "add skills as / commands like
# warp, eg /clean-commit").
#
#   docs/qa_evidence/2026-09-18-skills-as-slash-commands/drive.sh [build-dir]
#
# One fresh Relay under Xvfb with a fully isolated profile (HOME, XDG_*, TMPDIR, RELAY_KEYRING=off).
# ~/.warp/skills holds one skill, clean-commit, whose SKILL.md carries CLEAN-COMMIT-MARKER. The only
# model is fake-provider.py, registered as a local endpoint, which logs each request's user message.
#
#   a  "/cle" typed: the popup offers /clean-commit with its description
#   b  "/clean-commit tidy the readme" typed: the route line says SKILL
#   c  after Enter: the fake model's answer, which it gives only when the SKILL.md arrived
#   d  "/skill clean-commit again" sent: the same, by the explicit form
#   e  "/skill nosuch" sent: the status line names the closest skill
#   f  "use the clean-commit skill on this" sent as prose (no skill travels): at the turn's end,
#      the hint "Next time: /clean-commit runs that skill"
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=900 port=18931

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${TMPDIR:-/tmp}/relay-skill-slash-$$
xvfb_pid= relay_pid= fake_pid=
cleanup() {
    [[ -n $relay_pid ]] && kill "$relay_pid" 2>/dev/null
    [[ -n $fake_pid ]] && kill "$fake_pid" 2>/dev/null
    [[ -n $xvfb_pid ]] && kill "$xvfb_pid" 2>/dev/null
    sleep 1; rm -rf "$sandbox"
}
trap cleanup EXIT

rm -f "$out/requests.jsonl"
python3 "$out/fake-provider.py" $port "$out/requests.jsonl" &
fake_pid=$!

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy

export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$HOME/run TMPDIR=$HOME/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work" "$XDG_RUNTIME_DIR" "$TMPDIR" \
         "$HOME/.warp/skills/clean-commit"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$HOME/.warp/skills/clean-commit/SKILL.md" <<'MD'
---
name: clean-commit
description: Stage only your own hunks and write a commit message that says what changed and why
---
# Clean commit

CLEAN-COMMIT-MARKER: check `git status`, stage only the files you wrote, and commit.
MD
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
CONF
export RELAY_LOCAL_MODELS=$HOME/local-models.json
cat >"$RELAY_LOCAL_MODELS" <<JSON
{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake", "base_url": "http://127.0.0.1:$port/v1",
  "model": "fake", "server": "openai-compatible", "context_window": 65536, "tools": true, "thinking": false, "extra": {},
  "note": "", "first_token_timeout": 30.0, "parallel_tool_calls": false, "tool_text_recovery": false}]}
JSON

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

t '/cle'; sleep 1.5
shot a-popup-offers-the-skill
k ctrl+a BackSpace; sleep 0.5
t '/clean-commit tidy the readme'; sleep 1.5
shot b-route-line-says-skill
k Return; sleep 6
shot c-answer-with-the-skill
t '/skill clean-commit again'; k Return; sleep 6
shot d-explicit-skill-form
t '/skill nosuch'; k Return; sleep 1.2
shot e-unknown-skill-named
# Hints keep a 20 s gap between any two (an idle tip follows each finished turn), so wait it out.
sleep 22
t 'use the clean-commit skill on this'; k Return; sleep 4
shot f-prose-hint
sleep 4

echo "requests logged: $(wc -l <"$out/requests.jsonl")"
python3 - "$out/requests.jsonl" <<'PY'
import json, sys
for n, line in enumerate(open(sys.argv[1]), 1):
    user = json.loads(line)["user"]
    user = user if isinstance(user, str) else json.dumps(user)
    print(f"request {n}: marker={'CLEAN-COMMIT-MARKER' in user} ends={user.strip()[-40:]!r}")
PY
