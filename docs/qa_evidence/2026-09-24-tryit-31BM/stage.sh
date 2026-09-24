#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Try-it staging for card #31BM: opens Relay (this checkout's build/relay) on your display with
# an isolated profile, a fake no-key model server, and a conversation already long enough that
# the next /model switch must compact first. The compaction summary streams for ~24 s, so there
# is time to watch the chip.
#
# Run: docs/qa_evidence/2026-09-24-tryit-31BM/stage.sh
# Then, in the Relay window it opens, type:  /model local:small   and watch the chip at the
# bottom of the pane while "Compacting the conversation…" is up.
#
# Rerunnable: kills a previous instance (pidfiles under $RUNDIR) and rebuilds the jail.
# CLEANUP=1 stage.sh removes the jail and stops the fake server instead of opening the app.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BIN=${RELAY_BIN:-$ROOT/build/relay}
HERE=$(cd "$(dirname "$0")" && pwd)
RUN=/tmp/claude-1000/tryit/31BM
PORT=${RELAY_QA_PORT:-18543}
export DISPLAY=${RELAY_QA_DISPLAY:-${DISPLAY:-:0}}

# Stop a previous run, if any.
for pidfile in "$RUN"/*.pid; do
  [ -e "$pidfile" ] || continue
  kill "$(cat "$pidfile")" 2>/dev/null || true
  rm -f "$pidfile"
done
rm -rf "$RUN"
mkdir -p "$RUN/home" "$RUN/config/RelayTerminal" "$RUN/config/relay" "$RUN/data" "$RUN/cache" "$RUN/run" "$RUN/tmp" "$RUN/work"
chmod 700 "$RUN/run"

export HOME="$RUN/home" XDG_CONFIG_HOME="$RUN/config" XDG_DATA_HOME="$RUN/data" \
       XDG_CACHE_HOME="$RUN/cache" XDG_RUNTIME_DIR="$RUN/run" TMPDIR="$RUN/tmp" RELAY_KEYRING=off

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
CONF
endpoint() { printf '{"id": "%s", "label": "%s", "base_url": "http://127.0.0.1:%s/v1", "model": "%s", "server": "openai-compatible", "context_window": %s}' "$1" "$2" "$PORT" "$3" "$4"; }
printf '{"version": 1, "endpoints": [%s, %s]}\n' \
  "$(endpoint local:big 'Fake big' big 131072)" "$(endpoint local:small 'Fake small' small 12000)" \
  >"$XDG_CONFIG_HOME/relay/local-models.json"

cp "$HERE/fake-provider.py" "$RUN/fake-provider.py"
: >"$RUN/requests.jsonl"
python3 "$RUN/fake-provider.py" "$PORT" "$RUN/requests.jsonl" >/dev/null 2>&1 &
echo $! >"$RUN/provider.pid"
sleep 1
kill -0 "$(cat "$RUN/provider.pid")" 2>/dev/null || { echo "port $PORT busy; rerun with RELAY_QA_PORT=<free>"; exit 1; }

if [[ -n "${CLEANUP:-}" ]]; then
  kill "$(cat "$RUN/provider.pid")" 2>/dev/null || true
  rm -rf "$RUN"
  echo "cleaned up"
  exit 0
fi

cd "$RUN/work"
"$BIN" >"$RUN/relay-stdout.txt" 2>&1 &
echo $! >"$RUN/app.pid"
sleep 8

# Mechanical part, played for you: switch to the big fake model and fill the conversation.
type_() { xdotool type --delay 20 "$1"; sleep 0.5; }
ask() { type_ "$1"; xdotool key ctrl+Return; }
turns_ended() { grep -rhs ' turn_end ' "$XDG_DATA_HOME/relay/logs" 2>/dev/null | wc -l; }
wait_turns() { for _ in $(seq 1 60); do [ "$(turns_ended)" -ge "$1" ] && return 0; sleep 1; done; return 1; }

type_ "/model local:big"; xdotool key Return; sleep 2
ask "Write an essay about progress indicators, part one."; wait_turns 1
ask "Write an essay about progress indicators, part two."; wait_turns 2

echo
echo "Relay is open on $DISPLAY with a long conversation on local:big (131k window)."
echo "Now type:  /model local:small   — and watch the chip at the bottom of the pane."
echo "(local:small has a 12,000-token window, so the switch must compact first; the summary"
echo " takes ~24 s. Afterwards small is expected to refuse the takeover — that refusal is"
echo " part of the fixture, not the thing under test.)"
echo "Clean up afterwards with: CLEANUP=1 $HERE/stage.sh"
