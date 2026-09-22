"""Check the independent real-SSH GUI protocol trace; no model calls."""
import json
from pathlib import Path
OUT = Path(__file__).resolve().parent
rows = [json.loads(x) for x in (OUT / 'events.jsonl').read_text().splitlines()]
history = [r['items'][-1] for r in rows if r['type'] == 'terminal_history']
checks = {}
def check(name, value):
    checks[name] = bool(value)
def find(prefix):
    return next(x for x in history if x['command'].startswith(prefix))
remote = find("printf 'REMOTE-ONE")
check('remote_clean_output_host_exit', remote['output'] == 'REMOTE-ONE\nREMOTE-TWO' and remote['host'] == 'localhost' and remote['exit_status'] == 0)
check('failure_exit_1', find('cd /tmp;')['exit_status'] == 1)
sleep = find('sleep 7')
queued = find('printf "QUEUED-AFTER')
check('queue_dispatched_after_sleep', queued['time'] - sleep['time'] >= 7 and queued['output'] == 'QUEUED-AFTER-SLEEP')
check('stdin_answer_is_distinct', find('read -r')['output'] == 'INPUT> ANSWER\nRECEIVED=ANSWER' and find('echo SHOULD')['output'] == 'SHOULD-BE-A-COMMAND')
check('multiline_one_clean_history_entry', find("printf 'MULTI-A")['output'] == 'MULTI-A\nMULTI-B')
check('zsh_exit_1', find("printf 'ZSH-OUTPUT")['exit_status'] == 1)
check('zsh_clean_output', find("printf 'ZSH-OUTPUT")['output'] == 'ZSH-OUTPUT')
start = next(i for i,r in enumerate(rows) if r['type'] == 'route' and '127.0.0.1' in r.get('text',''))
end = next(i for i,r in enumerate(rows[start:],start) if r['type'] == 'terminal_history' and '127.0.0.1' in r['items'][-1]['command'])
states = [r['remote_session'] for r in rows[start:end] if r['type'] == 'remote_session_update']
first_disabled = next(i for i,s in enumerate(states) if not s['reachable'])
check('nested_binding_withheld_until_return', all(not s['reachable'] and 'control_path' not in s for s in states[first_disabled:]))
check('outer_binding_restored', next(r['remote_session'] for r in rows[end:] if r['type'] == 'remote_session_update')['reachable'])
check('disconnect_clears_context', [r['remote_session'] for r in rows if r['type'] == 'remote_session_update'][-1] == {})
hand = [json.loads(x) for x in (OUT / 'handoff-events.jsonl').read_text().splitlines()]
report_i = next(i for i,r in enumerate(hand) if r['type'] == 'ask' and 'Terminal hand-over result' in r.get('text',''))
report = hand[report_i]
check('handoff_clean_output_exit_1', 'Exit status: 1.' in report['text'] and '\n```\nHANDOFF_STDOUT\n```\n' in report['text'])
check('handoff_reports_before_disconnect', report['context']['remote_session']['reachable'] and any(r['type'] == 'remote_session_update' and not r['remote_session'] for r in hand[report_i+1:]))
(OUT / 'event-checks.json').write_text(json.dumps(checks, indent=2) + '\n')
print(json.dumps(checks, indent=2))
