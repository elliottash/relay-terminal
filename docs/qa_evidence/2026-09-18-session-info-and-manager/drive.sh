#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Live check of the ⓘ conversation info pane (card #Y63Z) and the session manager pane (card
# #R6J0), under Xvfb + xdotool, offline:
#
#   docs/qa_evidence/2026-09-18-session-info-and-manager/drive.sh [build-dir]
#
# Isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME, XDG_RUNTIME_DIR and TMPDIR: the
# owner's sessions and index are never read or written. The pane runs on fake-provider.py, a
# loopback server registered as a local model endpoint (no key, no network, no credits); its
# "delegate" turn starts two real subagents, whose threads the worker saves beside the session.
# A second workspace's session with a thread and a nested thread is seeded from files first (a
# thread cannot start a thread in Relay today, so nesting can only be shown from files); it is
# the only seeded data. Writes implementer-NN-*.png next to this script.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
OUT=$PWD
ROOT=$(cd ../../.. && pwd)
BUILD=${1:-$ROOT/build}
PORT=${RELAY_QA_PORT:-18561}
JAIL=$(mktemp -d)
export RELAY_KEYRING=off HOME="$JAIL/home" XDG_CONFIG_HOME="$JAIL/config" XDG_DATA_HOME="$JAIL/data"
export XDG_CACHE_HOME="$JAIL/cache" XDG_RUNTIME_DIR="$JAIL/run" TMPDIR="$JAIL/tmp" DISPLAY=:${RELAY_QA_DISPLAY:-88}
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
         "$TMPDIR" "$JAIL/work/src" "$JAIL/other"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
printf '# notes\nThe capybara notes.\n' >"$JAIL/work/notes.md"
(cd "$JAIL/work" && git init -q -b main)
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[provider]
preset=local:fake

[instructions]
onboarded=true
CONF
printf '{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake local", "base_url": "http://127.0.0.1:%s/v1", "model": "fake-qa", "server": "openai-compatible", "context_window": 131072}]}\n' \
  "$PORT" >"$XDG_CONFIG_HOME/relay/local-models.json"

# The seeded session in another workspace, written through Relay's own SessionStore.
PYTHONPATH="$ROOT/backend" python3 - "$JAIL/other" <<'PY'
import sys, time
from relay_core import sessions
workspace = sys.argv[1]
store = sessions.SessionStore(sessions.default_session_dir(workspace))
owner, parent, child = "1" * 32, "2" * 32, "3" * 32
now = time.time() - 7200
store.save({"version": 1, "kind": "relay_session", "id": owner, "title": "Older work in another project",
            "created": now, "updated": now, "workspace": workspace, "model": "glm-5", "turns": 1,
            "messages": [{"role": "user", "content": "audit the parser"},
                         {"role": "assistant", "content": "Audited with two helpers."}],
            "checkpoints": {"items": [{"turn": 1, "prompt": "audit the parser", "time": now,
                                       "locations": {"0": 1}, "files": {}}]}})
def thread(tid, parent_id, call, text, agent, messages):
    return {"version": 1, "kind": sessions.THREAD_KIND, "id": tid, "agent_id": agent, "type": "general",
            "description": text, "title": text, "status": "done", "owner_session": owner,
            "parent_thread": parent_id, "spawn_turn": 1, "spawn_call": call, "workspace": workspace,
            "model": "glm-5", "models": ["glm-5"], "created": now + (1 if parent_id else 0), "updated": now + 5,
            "runs": 1, "usage": {"prompt_tokens": 900, "completion_tokens": 80, "total_tokens": 980, "requests": 2},
            "messages": messages}
store.save_thread(thread(parent, None, "c1", "Audit the parser", "a1", [
    {"role": "user", "content": "Audit the parser"},
    {"role": "assistant", "content": "", "tool_calls": [{"id": "n1", "type": "function",
                                                         "function": {"name": "agent", "arguments": "{}"}}]},
    {"role": "tool", "tool_call_id": "n1", "content": "The tokenizer is fine."},
    {"role": "assistant", "content": "The parser is sound."}]))
store.save_thread(thread(child, parent, "n1", "Check the tokenizer", "a2", [
    {"role": "user", "content": "Check the tokenizer"},
    {"role": "assistant", "content": "The tokenizer is fine."}]))
PY

python3 "$OUT/fake-provider.py" "$PORT" &
FAKE=$!
Xvfb "$DISPLAY" -screen 0 1500x950x24 >/dev/null 2>&1 &
XVFB=$!
cleanup() { kill "${APP:-0}" "$XVFB" "$FAKE" 2>/dev/null; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 2
cd "$JAIL/work"
"$BUILD/relay" >"$JAIL/relay.log" 2>&1 &
APP=$!
sleep 9

shot() { import -window root "$OUT/implementer-$1.png"; kill -0 "$APP" 2>/dev/null || echo "relay is gone before $1"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep "${3:-2}"; }
say() { xdotool type --delay 15 "$1"; sleep 0.3; }
win=$(xdotool search --pid "$APP" --name Relay | tail -1)
xdotool windowmove "$win" 0 0 windowfocus "$win"   # its default size, 1320x855: the clicks below are for it
sleep 1

# Two turns: the first starts two subagents (two threads), the second is a plain question.
click 600 787 0.5
say "please delegate the survey of this workspace"; xdotool key ctrl+Return; sleep 12
say "which rodent is largest"; xdotool key ctrl+Return; sleep 6
shot 01-pane-with-info-button

# ⓘ (the painted button, first in the header row): the pane's own session, live, with each
# thread at the turn that started it. The links are reached with Tab, as a keyboard user would:
# the session file, then a1's thread link.
click 1129 60 3
shot 02-info-view-with-subagent-links
xdotool key Tab Tab; sleep 0.3; xdotool key Return; sleep 2
shot 03-thread-history-with-way-back
# The first link on a thread page is "↑ owner session".
xdotool key Tab; sleep 0.3; xdotool key Return; sleep 2
shot 04-back-on-the-owner-session
xdotool key Escape; sleep 1

# The session manager (Ctrl+Shift+Y), threads off by default, then on.
xdotool key ctrl+shift+y; sleep 3
shot 05-session-manager-threads-off
click "${THREADS_X:-975}" "${THREADS_Y:-143}"
shot 06-session-manager-threads-on
# All projects: the seeded session, its thread and the nested thread under it.
click "${SCOPE_X:-704}" "${SCOPE_Y:-143}" 1; xdotool key Down Return; sleep 2
shot 07-all-projects-nested-thread
# Search finds threads by what they did; Enter on a thread opens its history.
click 900 106 0.5; say "tokenizer"; sleep 2
shot 08-search-finds-a-thread
# The nested thread (a2, under a1): Enter opens its history, with both ways back up.
xdotool key Down; sleep 0.5; xdotool key Return; sleep 3
shot 09-nested-thread-opens-its-history
# Back to the manager: Enter on a session row resumes it here. A new chat first, so there is
# something to replace; then /resume with words, which opens the manager searching for them.
xdotool key Escape; sleep 1
click 300 780 0.5; say "/new"; xdotool key Return; sleep 2
say "/resume capybara"; xdotool key Return; sleep 3
shot 10-resume-with-words-prefills-search
xdotool key Return; sleep 4
shot 11-enter-resumed-the-session-here
# The same session from a second pane: the first pane is focused instead of loading it twice.
xdotool key ctrl+e; sleep 4
say "/resume capybara"; xdotool key Return; sleep 3; xdotool key Return; sleep 3
shot 12-already-open-focuses-that-pane
