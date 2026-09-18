#!/usr/bin/env bash
# The whole of #W5N2 in one Relay session, live: a phone, a guest, a second device, and the
# desktop that answers all three.
#
#   docs/qa_evidence/2026-09-18-remote-end-to-end/drive.sh [build-dir]
#
# This script is only the sandbox: the isolation, the X server, the certificates, the local model
# and the Relay process. Everything after that — the clicks, the three headless browsers, the fake
# push service and every screenshot — is `run.py`, because the run is one long sequence in which
# the desktop and the browsers take turns, and a shell driving that through flag files is a worse
# description of it than a coroutine is.
#
# Writes NN-<side>-<what>.png next to this script (side: desktop, share, phone, guest, viewer),
# plus relay-stderr.log, run.log, notes.txt and audit.txt.
#
# Isolation: XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME **and XDG_RUNTIME_DIR and TMPDIR**.
# Without the last two a Relay already running on this machine shares sockets and temp files with
# the run, and its saves look broken. TMPDIR also has to be short: Chrome's singleton socket path
# has a hard length limit, and a deep temp directory kills every browser in the run at startup.
#
# What it needs on the machine: Xvfb, xdotool, ImageMagick's `import`, a Chrome or Chromium, and
# llama-server on 127.0.0.1:8080 (`systemctl --user start llama-bonsai`) so the pane has a real
# agent that costs nothing. RELAY_KEYRING=off, so the run cannot reach the owner's provider keys:
# the only agent in it is the local one.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}

[[ -x $build/relay ]] || { echo "no $build/relay; cmake --build build first"; exit 1; }
for tool in Xvfb xdotool import; do
  command -v "$tool" >/dev/null || { echo "$tool is not on PATH"; exit 1; }
done
if ! curl -s -m 3 http://127.0.0.1:8080/v1/models >/dev/null; then
  echo "warning: no llama-server on 127.0.0.1:8080 — the agent steps will show a refusal."
  echo "         systemctl --user start llama-bonsai"
fi

# Which tree this run is of. Several sessions commit to this checkout while a run is going, and
# the Python half is read live off disk while `build/relay` is whatever was last built, so the
# answer to "what was this evidence taken against" has to be written down rather than assumed.
{
  echo "HEAD          $(git -C "$root" rev-parse --short HEAD) $(git -C "$root" log -1 --format=%s)"
  echo "build/relay   $(date -r "$build/relay" '+%Y-%m-%d %H:%M:%S')"
  echo "dirty files   $(git -C "$root" status --porcelain | wc -l)"
  git -C "$root" status --porcelain -- app remote rendezvous src | sed 's/^/  /'
} >"$out/provenance.txt"

# A free display, without taking one another session is starting on. Two drives probing the same
# range at the same time both see :140 free, because the socket only appears once the server is up;
# the X lock file appears first, so both are checked, and a lock of our own is taken with `noclobber`
# before the server starts. Only the pids this script started are ever killed — the EXIT trap below
# names them one by one — because somebody else's drive is very likely on the next display up.
display=
for n in $(seq 140 199); do
  [[ -e /tmp/.X11-unix/X$n || -e /tmp/.X$n-lock ]] && continue
  if (set -o noclobber; echo $$ >"/tmp/.relay-e2e-X$n.lock") 2>/dev/null; then
    display=:$n
    lock=/tmp/.relay-e2e-X$n.lock
    break
  fi
done
[[ -z $display ]] && { echo "no free X display"; exit 1; }
echo "display $display"

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export XDG_RUNTIME_DIR=$(mktemp -d) TMPDIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF

# The agent in this run is the local llama-server and nothing else: the keyring is off, so no
# provider key of the owner's can be reached and no paid call can be made by accident.
export RELAY_KEYRING=off
export RELAY_LOCAL_MODELS=$work/local-models.json
cat >"$RELAY_LOCAL_MODELS" <<'JSON'
{"version": 1, "endpoints": [{"id": "local:bonsai", "label": "Bonsai 2 27B",
  "base_url": "http://127.0.0.1:8080/v1", "model": "bonsai-2-27b", "server": "llamacpp",
  "context_window": 131072, "tools": true, "thinking": true, "extra": {}, "note": "",
  "first_token_timeout": 300.0, "parallel_tool_calls": false, "tool_text_recovery": false}]}
JSON

# A notification has to reach something. `run.py` stands a fake push service up on loopback with
# the same development certificate the desktop serves the app with; the desktop's rendezvous is
# the one that posts to it (section 9.2), and it is a separate process, so the only way to make it
# trust a self-signed certificate is the environment. SSL_CERT_FILE is read by OpenSSL itself, so
# it covers urllib inside the sidecar without a line of code being added for the test.
pushcerts=$work/push-service
mkdir -p "$pushcerts"
python3 - "$pushcerts" <<'PY' || { echo "could not make the push service's certificate"; exit 1; }
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[0]))
sys.path.insert(0, "/home/elliott/repos/relay-terminal")
from remote import devtls
devtls.ensure_cert(Path(sys.argv[1]))
PY
export SSL_CERT_FILE=$pushcerts/dev-cert.pem
export RELAY_PUSH_CERTS=$pushcerts

trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null;
      rm -f "${lock:-/dev/null}";
      rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"' EXIT

# A root bigger than the window on purpose: the share window grows past 1000 px once a device is
# paired and a link is made, and X does not keep the pixels of a window hanging off the screen —
# an `import` of one comes back cropped. The Relay window sits at 0,0 and the share window beside
# it, both wholly on the root.
Xvfb "$display" -screen 0 2600x1300x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

# The pairing and invite links carry one-time secrets and are never logged: Relay writes each out
# only when the matching QA-only variable names a file (RemoteShare::handle).
export RELAY_REMOTE_PAIR_FILE=$work/pair-url.txt
export RELAY_REMOTE_INVITE_FILE=$work/invite-url.txt
"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 14
win=$(for w in $(xdotool search --onlyvisible --pid "$relay_pid" --name "^Relay"); do
        g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)
        wdt=$(echo "$g" | sed -n 's/^WIDTH=//p'); hgt=$(echo "$g" | sed -n 's/^HEIGHT=//p')
        echo "$(( ${wdt:-0} * ${hgt:-0} )) $w"
      done | sort -rn | head -1 | cut -d' ' -f2)
[[ -z $win ]] && { echo "no Relay window"; tail -20 "$out/relay-stderr.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1600 1000 windowfocus "$win"
sleep 1

export RELAY_QA_WIN=$win RELAY_QA_OUT=$out RELAY_QA_WORK=$work
( cd "$root" && python3 "$out/run.py" ) 2>&1 | tee "$out/run.log"
status=${PIPESTATUS[0]}

# The audit log is the story of the run in one file (section 10.6); it lives under the run's own
# XDG_DATA_HOME and would go with the sandbox.
cp "$XDG_DATA_HOME"/relay/remote/audit-*.jsonl "$out/audit.jsonl" 2>/dev/null
python3 - "$out/audit.jsonl" >"$out/audit.txt" 2>/dev/null <<'PY'
import json, sys
for line in open(sys.argv[1]):
    row = json.loads(line)
    kind = row.pop("kind", "?")
    row.pop("at", None)
    print(f"{kind:20} " + " ".join(f"{k}={v!r}" for k, v in row.items()))
PY

echo "screenshots and logs in $out"
exit "$status"
