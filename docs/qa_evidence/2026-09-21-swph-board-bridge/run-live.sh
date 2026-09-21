#!/bin/bash
# #SWPH task 1, live: a paired phone (a stub sidecar playing the hub) operates the Switchboard of a
# desktop that has NO Switchboard pane open and whose tab is not even attached to the project yet.
# A short root (the 108-byte socket limit), an isolated HOME and XDG_*, the keyring off, and a
# provider that points at a closed port so that Execute's pane cannot reach a real model.
# Needs Xvfb, xdotool-free. Run from anywhere; build/relay must be built.
REPO=/home/elliott/repos/relay-terminal
HERE="$(cd "$(dirname "$0")" && pwd)"
R=/tmp/swph
rm -rf $R; mkdir -p $R/home/.config/RelayTerminal $R/home/.local/share $R/home/.cache $R/home/.state
mkdir -p $R/xdg $R/tmp $R/fake/remote $R/ws/issues/features
cp "$HERE/stub_gui_host.py" $R/fake/remote/gui_host.py
printf '%s\n' 'version: 1' 'tabs: [{id: features, folder: features}]' \
    'columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]' \
    'agent: {autonomy: auto, max_creates_per_turn: 5}' >$R/ws/issues/board.yaml
printf '%s\n' '---' 'id: K7Q2' 'type: work' 'status: inbox' 'labels: []' 'waiting_on: owner' \
    "created: '2026-09-21'" 'acceptance: the phone can answer it' '---' '# A card waiting on the owner' '' \
    '## Issue' '' 'Should we go ahead?' >$R/ws/issues/features/K7Q2.md
git -C $R/ws init -q; git -C $R/ws -c user.name=qa -c user.email=qa@example.invalid add .
git -C $R/ws -c user.name=qa -c user.email=qa@example.invalid commit -qm fixture
cat > $R/home/.config/RelayTerminal/relay.conf <<'CONF'
[remote]
alwaysOn=true

[provider]
preset=custom
base=http://127.0.0.1:9/v1
model=none
CONF
export HOME=$R/home XDG_CONFIG_HOME=$R/home/.config XDG_DATA_HOME=$R/home/.local/share
export XDG_CACHE_HOME=$R/home/.cache XDG_STATE_HOME=$R/home/.state XDG_RUNTIME_DIR=$R/xdg TMPDIR=$R/tmp
export RELAY_KEYRING=off RELAY_REMOTE_DIR=$R/fake SWPH_LOG=$R/sidecar.jsonl
export DISPLAY=:97
Xvfb :97 -screen 0 1600x1000x24 >$R/xvfb.log 2>&1 &
XPID=$!
sleep 2
(cd $R/ws && $REPO/build/relay --fresh --workspace $R/ws >$R/relay.log 2>&1) &
for i in $(seq 1 90); do grep -q '"stub": "done"' $R/sidecar.jsonl 2>/dev/null && break; sleep 2; done
sleep 2
import -window root $R/after.png 2>/dev/null
pkill -f "$REPO/build/relay --fresh --workspace $R/ws"; sleep 3
kill $XPID 2>/dev/null
echo "=== crashes: $(grep -c gui_crash $R/relay.log) ==="
echo "=== the card on disk ==="; cat $R/ws/issues/features/K7Q2.md; ls $R/ws/issues/features
echo "=== its thread ==="; cat $R/ws/issues/threads/K7Q2.md 2>/dev/null
# The evidence: every request the "hub" sent and every board_event the desktop wrote back, with the
# two big row lists shortened and this machine's paths shown for what they are (none should be left).
python3 - "$R/sidecar.jsonl" "$HERE" <<'PY'
import json, sys
src, here = sys.argv[1], sys.argv[2]
out = []
for line in open(src):
    message = json.loads(line)
    if "hub_to_gui" in message:
        out.append(json.dumps({"hub->gui": message["hub_to_gui"]}, ensure_ascii=False))
    elif message.get("t") == "board_event":
        out.append(json.dumps({"gui->hub": message}, ensure_ascii=False))
open(here + "/board-events.jsonl", "w").write("\n".join(out) + "\n")
print("=== %d lines in board-events.jsonl; any path left in a board_event: %s ===" % (
    len(out), any("/tmp/swph" in l for l in out if l.startswith('{"gui->hub"'))))
PY
grep -v '"t":"frame"' $R/sidecar.jsonl | grep -v hub_to_gui | cut -c1-400 >"$HERE/sidecar.jsonl"
cp $R/after.png "$HERE/after.png" 2>/dev/null
