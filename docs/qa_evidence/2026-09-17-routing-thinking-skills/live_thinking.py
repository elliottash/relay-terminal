import json, sys, tempfile, time
sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[3] / 'backend'))
from relay_core import keystore
from relay_core.agent import Agent
from relay_core.presets import PRESETS
from relay_core.provider import ProviderConfig

for pid, effort_extra in (('kimi', {'reasoning_effort': 'high'}), ('glm-coding', {'thinking': {'type': 'enabled'}, 'reasoning_effort': 'high'})):
    p = PRESETS[pid]
    start = time.monotonic()
    timeline = []
    def emit(e, timeline=timeline):
        if e['event'] in ('thinking_delta', 'thinking_done', 'delta', 'done', 'turn_summary'):
            timeline.append((int((time.monotonic() - start) * 1000), e['event'], len(e.get('text') or ''),
                             {k: e[k] for k in ('elapsed_ms', 'chars', 'thinking_ms', 'turn_id') if k in e}))
    agent = Agent(ProviderConfig(p.base_url, p.model, keystore.lookup(pid), effort_extra, 4096),
                  tempfile.mkdtemp(), emit, preset_id=pid)
    agent.ask('Is 391 prime? Answer in one short sentence.', turn_id=f'live-{pid}')
    nonempty = [t for t in timeline if not (t[1] == 'delta' and t[2] == 0)]
    th = [t for t in nonempty if t[1] == 'thinking_delta']
    first_answer = next((t for t in nonempty if t[1] == 'delta'), None)
    done = [t for t in nonempty if t[1] == 'thinking_done']
    print(json.dumps({'preset': pid, 'thinking_deltas': len(th), 'first_thinking_ms': th[0][0] if th else None,
                      'last_thinking_ms': th[-1][0] if th else None, 'thinking_done': done,
                      'first_answer_ms': first_answer[0] if first_answer else None,
                      'done_before_answer': bool(done and first_answer and nonempty.index(done[0]) < nonempty.index(first_answer)),
                      'empty_delta_events': sum(1 for t in timeline if t[1] == 'delta' and t[2] == 0),
                      'summary': [t for t in nonempty if t[1] in ('turn_summary', 'done')]}))
