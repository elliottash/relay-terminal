#!/usr/bin/env bash
# #308N — does a next-command suggestion reach the prompt box as ghost text?
#
#   docs/qa_evidence/2026-09-17-bugfix-batch1/suggestions.sh [ok|fail] [build-dir]
#
# `fail` makes the stand-in refuse the suggestion call the way a provider refuses a Flash-tier
# model the key has no access to, so the pane keeps working and only the side call fails.
#
# The whole path runs for real — the GUI, the real worker, the real role resolution and the real
# protocol — against a local OpenAI-compatible endpoint that stands in for the model, so no key
# leaves the machine and no provider is called. The stand-in is wired in by pointing the `kimi`
# preset at 127.0.0.1 (a shim in RELAY_DATA_DIR that patches presets.PRESETS before running the
# real worker) and by putting a dummy key in RELAY_KIMI_API_KEY, which is what the keystore reads
# first. The Flash tier of that preset points at the same host, so the suggestions role does too.
#
# Steps: run a command from the prompt box, wait, and shoot the composer. A suggestion shows as
# grey ghost text in the empty prompt box; Tab accepts it.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
mode=${1:-ok}
build=${2:-$root/build}
display=${RELAY_QA_DISPLAY:-:91}
width=1400 height=900

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export XDG_RUNTIME_DIR=$(mktemp -d)
work=$(mktemp -d) shim=$(mktemp -d)
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" "${model_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$work" "$shim"' EXIT

# ----- the stand-in model -----------------------------------------------------------------------
cat >"$shim/model.py" <<'PY'
import json, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG = sys.argv[1]
MODE = sys.argv[3]

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        system = next((m["content"] for m in body.get("messages", []) if m["role"] == "system"), "")
        with open(LOG, "a") as log:
            log.write(json.dumps({"model": body.get("model"), "system": system[:60]}) + "\n")
        if MODE == "fail" and ("next shell command" in system or "next short request" in system):
            refusal = b'{"error": {"message": "model not available to this key"}}'
            self.send_response(401)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(refusal)))
            self.end_headers()
            self.wfile.write(refusal)
            return
        if "next shell command" in system:
            reply = json.dumps({"command": "git status --short", "reason": "see what changed"})
        elif "next short request" in system:
            reply = json.dumps({"prompt": "run the tests"})
        else:
            reply = "ok"
        payload = ("data: " + json.dumps({"choices": [{"delta": {"content": reply}, "finish_reason": None}]}) + "\n\n"
                   "data: " + json.dumps({"choices": [{"delta": {}, "finish_reason": "stop"}]}) + "\n\n"
                   "data: [DONE]\n\n").encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
    def log_message(self, *a): pass

server = ThreadingHTTPServer(("127.0.0.1", int(sys.argv[2])), Handler)
server.serve_forever()
PY

port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
python3 "$shim/model.py" "$out/model-calls-$mode.log" "$port" "$mode" &
model_pid=$!
sleep 1

# ----- a data root whose worker points the kimi preset at the stand-in --------------------------
mkdir -p "$shim/root/backend"
for entry in "$root"/*; do
    name=$(basename "$entry")
    [[ $name == backend ]] && continue
    ln -s "$entry" "$shim/root/$name"
done
ln -s "$root/backend/relay_core" "$shim/root/backend/relay_core"
cat >"$shim/root/backend/worker.py" <<PY
import dataclasses, runpy, sys
sys.path.insert(0, "$root/backend")
from relay_core import presets
local = "http://127.0.0.1:$port/v1"
presets.PRESETS["kimi"] = dataclasses.replace(presets.PRESETS["kimi"], base_url=local)
runpy.run_path("$root/backend/worker.py", run_name="__main__")
PY
export RELAY_DATA_DIR="$shim/root"
export RELAY_KIMI_API_KEY=local-stand-in-not-a-real-key
export RELAY_KEYRING=off

mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
[suggestions]
next_command=true
next_prompt=true
[provider]
preset=kimi
CONF

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display
: >"$out/model-calls-$mode.log"

"$build/relay" --engine=relay --workspace "$work" >"$out/suggestions-$mode-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 9
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool mousemove 700 450   # no window manager: X focus is PointerRoot
sleep 3
import -window "$win" "$out/suggestions-$mode-01-configured.png" 2>/dev/null

echo "=== run a command from the prompt box ==="
xdotool type --delay 15 'echo hello'
sleep 1
xdotool key --delay 40 Return
sleep 8
import -window "$win" "$out/suggestions-$mode-02-after-the-command.png" 2>/dev/null
echo "--- calls the stand-in model received ---"
cat "$out/model-calls-$mode.log"
echo "--- worker/GUI log lines about suggestions ---"
grep -aiE "suggest|error" "$out/suggestions-$mode-relay-stderr.log" | tail -20
# Accept the suggestion with Tab and shoot the result.
xdotool key --delay 40 Tab
sleep 2
import -window "$win" "$out/suggestions-$mode-03-after-tab.png" 2>/dev/null
echo "screenshots in $out"
