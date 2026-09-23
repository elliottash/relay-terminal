#!/usr/bin/env bash
# Exercise the crash path in an isolated profile; never touches the user's live state.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
if [[ ${1:-} != --inside ]]; then exec xvfb-run -a bash "$0" --inside; fi

root=$(mktemp -d /tmp/relay-crash-recovery.XXXXXX)
export XDG_CONFIG_HOME="$root/config" XDG_DATA_HOME="$root/data"
export XDG_CACHE_HOME="$root/cache" XDG_RUNTIME_DIR="$root/runtime"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME/relay/state" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$root/project"
chmod 700 "$XDG_RUNTIME_DIR"
state="$XDG_DATA_HOME/relay/state/windows.json"
export RECOVERY_STATE="$state" RECOVERY_PROJECT="$root/project"
python3 - <<'PY'
import json, os, pathlib
path = pathlib.Path(os.environ['RECOVERY_STATE'])
project = os.environ['RECOVERY_PROJECT']
pane = {'cwd': project, 'workspace': project, 'preset': 'guest:codex',
        'model': 'gpt-6-sol', 'agent_role': 'main', 'agent_mode': 'build',
        'session_id': '11111111111141118111111111111111',
        'scrollback': '11111111-1111-4111-8111-111111111111'}
path.write_text(json.dumps({'version': 1, 'saved': 1, 'windows': [
    {'current': 0, 'tabs': [{'node': {'pane': pane}, 'tab_id': 'test', 'project': project}],
     'titles': ['test']}]}))
PY

pid=
cleanup() { if [[ -n $pid ]]; then kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; }
trap cleanup EXIT
cd "$root/project"
"$repo/build/relay" --clean-shell >"$root/first.log" 2>&1 & pid=$!
sleep 34
kill -KILL "$pid"
wait "$pid" 2>/dev/null || true
pid=
python3 - <<'PY'
import json, os, pathlib
state = pathlib.Path(os.environ['RECOVERY_STATE'])
pane = json.loads(state.read_text())['windows'][0]['tabs'][0]['node']['pane']
assert pane.get('session_id') == '11111111111141118111111111111111', pane
scrollback = state.parent / 'scrollback' / (pane['scrollback'] + '.txt')
assert scrollback.exists() and scrollback.stat().st_size > 0, scrollback
print('PASS: crash leaves session ID and checkpointed terminal text')
PY
"$repo/build/relay" --clean-shell >"$root/second.log" 2>&1 & pid=$!
sleep 4
python3 - <<'PY'
import json, os, pathlib
state = pathlib.Path(os.environ['RECOVERY_STATE'])
pane = json.loads(state.read_text())['windows'][0]['tabs'][0]['node']['pane']
assert pane.get('session_id') == '11111111111141118111111111111111', pane
print('PASS: deferred guest startup keeps session ID on restart')
PY
printf 'Evidence: %s\n' "$root"
