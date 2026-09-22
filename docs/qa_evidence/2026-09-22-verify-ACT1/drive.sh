#!/usr/bin/env bash
set -euo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-22-verify-ACT1
sandbox=$(mktemp -d /tmp/rl-act.XXXX)
export DISPLAY=:798 RELAY_KEYRING=off RELAY_OPENROUTER_CATALOG=off
export HOME=$sandbox XDG_CONFIG_HOME=$sandbox/config XDG_DATA_HOME=$sandbox/data XDG_CACHE_HOME=$sandbox/cache XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp RELAY_QA_RECTS=$sandbox/rects.json
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_RUNTIME_DIR" "$TMPDIR" "$sandbox/project"
chmod 700 "$XDG_RUNTIME_DIR"
cat > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[isolation]
enabled=false
[security]
approvals_chosen=true
[url_handler]
announced=true
CONF
Xvfb "$DISPLAY" -screen 0 1500x1000x24 > "$out/xvfb.txt" 2>&1 & xp=$!
rp=
trap '[[ -z "$rp" ]] || kill "$rp" 2>/dev/null; kill "$xp" 2>/dev/null' EXIT
sleep 2
launch() {
 "$root/build/relay" "$@" >> "$out/stderr.txt" 2>&1 & rp=$!
 sleep 12
 win=$(xdotool search --pid "$rp" --onlyvisible | tail -1)
 xdotool windowmove "$win" 0 0 windowsize "$win" 1450 950 windowfocus "$win"
 sleep 3
}
launch --workspace "$sandbox/project"
xdotool key alt+shift+r
sleep 5
import -window root "$out/before-quit.png"
cp "$RELAY_QA_RECTS" "$out/before-rects.json"
kill -TERM "$rp"; wait "$rp" || true; rp=
cp "$XDG_DATA_HOME/relay/state/windows.json" "$out/saved-layout.json"
launch
import -window root "$out/after-reopen.png"
cp "$RELAY_QA_RECTS" "$out/after-rects.json"
kill -TERM "$rp"; wait "$rp" || true; rp=
cp "$XDG_DATA_HOME/relay/state/windows.json" "$out/restored-layout.json"
echo "$sandbox" > "$out/sandbox-path.txt"
