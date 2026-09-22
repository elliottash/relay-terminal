#!/usr/bin/env bash
# Reuse only fixture setup and UI helpers, never the earlier run's assertions/results.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=${CFG1_OUT:-$root/docs/qa_evidence/2026-09-22-verify-CFG1/post-fix}
export RELAY_QA_PORT=8997
set -- "${RELAY_QA_BINARY:-$root/build/relay}" "$out"
source <(sed -n '1,339p' "$root/docs/qa_evidence/2026-09-21-agents-are-consoles/drive.sh" | sed 's|cd "$(dirname "${BASH_SOURCE\[0\]}")"|cd /home/elliott/repos/relay-terminal/docs/qa_evidence/2026-09-21-agents-are-consoles|')
mkdir -p "$sandbox/data/backend"
for path in "$root"/*; do [[ ${path##*/} == backend ]] || ln -s "$path" "$sandbox/data/${path##*/}"; done
for path in "$root/backend"/*; do [[ ${path##*/} == worker.py ]] || ln -s "$path" "$sandbox/data/backend/${path##*/}"; done
cp "$root/docs/qa_evidence/2026-09-22-verify-CFG1/worker-tap.py" "$sandbox/data/backend/worker.py"
export RELAY_DATA_DIR=$sandbox/data QA_REAL_WORKER=$root/backend/worker.py QA_WIRE=$out/wire.jsonl
: > "$QA_WIRE"
cat >> "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[models]
tier\main=local:stub|stub|
tier\flash=local:stub|stub|
tier\lite=local:stub|stub|
[isolation]
enabled=false
[security]
approvals_chosen=true
[url_handler]
announced=true
CONF
launch || exit 1
k ctrl+shift+s; sleep 5
shot 01-folded
click_word 01-folded '^inbox$'
sleep 2; shot 02-list
click_word 02-list console
sleep 4; shot 03-card
# Both contexts now exist. Observe a full minute without interacting.
date -u +%s > "$out/idle-start.txt"
sleep 30
sleep 31
date -u +%s > "$out/idle-end.txt"
shot 04-after-idle
click_rect_in composerEditor $((width / 2)) 0
ask 'say hello to the card'
sleep 15; shot 05-card-answer
# Use the named Back widget; Escape from the console does not navigate.
click_rect boardBack; sleep 3; shot 06-back-list
click_rect_in composerEditor $((width / 2)) 0
ask 'say hello to the list'
sleep 15; shot 07-list-answer
# Options search finds the actual turn-limit row.
k ctrl+shift+o; sleep 4
click_rect settingsSearch; k ctrl+a; t 'Step limit per turn'; sleep 3; shot 08-options-search
click_rect qt_spinbox_lineedit
date -u +%s > "$out/limit-change-start.txt"
k ctrl+a; t '37'; click_rect settingsSearch; sleep 3; shot 09-limit-37
click_rect settingsSearch; k ctrl+a; t 'Tool-call limit per turn'; sleep 2
click_rect qt_spinbox_lineedit; k ctrl+a; t '43'; click_rect settingsSearch; sleep 3; shot 10-tool-limit-43
cp "$RELAY_QA_RECTS" "$out/options-rects.json"
# Keep the sandbox available for follow-up inspection during this verification run.
echo "$sandbox" > "$out/sandbox-path.txt"
echo "$DISPLAY $relay_pid $win" > "$out/live.txt"
cp "$XDG_DATA_HOME/relay/logs/relay.log" "$out/relay-log.txt" 2>/dev/null || true
cp "$XDG_DATA_HOME/relay/logs/worker.log" "$out/worker-log.txt" 2>/dev/null || true


python3 - "$out" <<'CHECK'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]); rows=[json.loads(x) for x in (p/'wire.jsonl').read_text().splitlines()]
start=int((p/'idle-start.txt').read_text());end=int((p/'idle-end.txt').read_text())
assert end-start >= 60
assert not [r for r in rows if start <= r['time'] <= end and r['message']['type']=='configure']
helper=[r for r in rows if r['message'].get('context',{}).get('name')=='card'][0]['pid']
configs=[r['message'] for r in rows if r['pid']==helper and r['message']['type']=='configure']
assert configs[-1]['max_steps']==37, configs[-1].get('max_steps')
assert configs[-1]['context']['name']=='switchboard'
assert configs[-1]['max_tool_calls']==43
asks=[r for r in rows if r['pid']==helper and r['message']['type'] in ('ask','board_ask')]
assert {r['message']['type'] for r in asks} == {'ask','board_ask'}
for request in asks:
    before=[r['message'] for r in rows if r['pid']==helper and r['message']['type']=='configure' and r['time']<request['time']]
    assert before[-1]['context']['name']==('card' if request['message']['type']=='board_ask' else 'switchboard')
change=int((p/'limit-change-start.txt').read_text())
assert not [r for r in asks if r['time']>=change], 'limit update must arrive without a further ask'
updated=[r for r in rows if r['pid']==helper and r['message']['type']=='configure' and r['time']>=change]
assert updated and updated[-1]['message']['max_steps']==37
assert all(r['message']['context']['name']=='switchboard' for r in updated), 'no context switch after limit edit'
(p/'result.json').write_text(json.dumps({'verdict':'PASS','idle_seconds':end-start,'configures_during_idle':0,'helper_pid':helper,'asks':[{'time':r['time'],'type':r['message']['type']} for r in asks],'max_steps_after_edit':37,'max_tool_calls_after_edit':43,'asks_after_limit_edit':0,'context_switches_after_limit_edit':0},indent=2)+'\n')
print('PASS: idle',end-start,'seconds; card/list context switching; helper max_steps=37, max_tool_calls=43 without a further ask or context switch')
CHECK
