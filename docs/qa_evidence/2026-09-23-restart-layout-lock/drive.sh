#!/usr/bin/env bash
# Reproduce a fast restart with two Relay processes overlapping. All state stays under /tmp.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
relay="$repo/build/relay"
if [[ ${1:-} != --inside ]]; then
    exec xvfb-run -a bash "$0" --inside
fi

root=$(mktemp -d /tmp/relay-layout-lock.XXXXXX)
export XDG_CONFIG_HOME="$root/config" XDG_DATA_HOME="$root/data"
export XDG_CACHE_HOME="$root/cache" XDG_RUNTIME_DIR="$root/runtime"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME/relay/state/scrollback" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$root/project"
chmod 700 "$XDG_RUNTIME_DIR"
state="$XDG_DATA_HOME/relay/state/windows.json"
export RELAY_LOCK_TEST_STATE="$state" RELAY_LOCK_TEST_PROJECT="$root/project"
python3 - <<'PY'
import json, os, pathlib
state = pathlib.Path(os.environ['RELAY_LOCK_TEST_STATE'])
project = os.environ['RELAY_LOCK_TEST_PROJECT']
ids = ['11111111-1111-4111-8111-111111111111', '22222222-2222-4222-8222-222222222222']
tabs = []
for i, pane_id in enumerate(ids):
    tabs.append({'node': {'pane': {'cwd': project, 'workspace': project,
                                   'scrollback': pane_id}},
                 'tab_id': f'tab-{i}', 'project': project})
    (state.parent / 'scrollback' / f'{pane_id}.txt').write_text(f'seed tab {i}\n')
state.write_text(json.dumps({'version': 1, 'saved': 1,
                             'windows': [{'current': 0, 'tabs': tabs,
                                          'titles': ['first', 'second']}]}, indent=2))
PY

a= b= c=
cleanup() {
    for pid in "$c" "$b" "$a"; do
        if [[ -n $pid ]]; then kill -TERM "$pid" 2>/dev/null || true; fi
    done
    for pid in "$c" "$b" "$a"; do
        if [[ -n $pid ]]; then wait "$pid" 2>/dev/null || true; fi
    done
}
trap cleanup EXIT
cd "$root/project"
"$relay" --clean-shell >"$root/a.log" 2>&1 & a=$!
for _ in $(seq 1 80); do
    [[ -e "$XDG_RUNTIME_DIR/relay/windows.lock" ]] && break
    sleep 0.1
done
[[ -e "$XDG_RUNTIME_DIR/relay/windows.lock" ]]
sleep 1
"$relay" --clean-shell >"$root/b.log" 2>&1 & b=$!
sleep 1
kill -TERM "$a"
wait "$a" || true
a=
sleep 2

export RELAY_LOCK_TEST_STEP=secondary
python3 - <<'PY'
import json, os, pathlib
state = pathlib.Path(os.environ['RELAY_LOCK_TEST_STATE'])
data = json.loads(state.read_text())
tabs = data['windows'][0]['tabs']
assert len(data['windows']) == 1 and len(tabs) == 2, (len(data['windows']), len(tabs))
for pane_id in ['11111111-1111-4111-8111-111111111111', '22222222-2222-4222-8222-222222222222']:
    assert (state.parent / 'scrollback' / f'{pane_id}.txt').exists(), pane_id
print('PASS: secondary instance did not replace the two-tab layout or prune its scrollback')
PY

kill -TERM "$b"
wait "$b" || true
b=
"$relay" --clean-shell >"$root/c.log" 2>&1 & c=$!
sleep 2
python3 - <<'PY'
import json, os, pathlib
data = json.loads(pathlib.Path(os.environ['RELAY_LOCK_TEST_STATE']).read_text())
tabs = data['windows'][0]['tabs']
assert len(tabs) == 2, len(tabs)
print('PASS: later clean launch preserved both tabs')
PY
printf 'Evidence: %s\n' "$root"
