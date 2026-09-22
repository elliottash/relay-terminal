"""Deterministic protocol fixture: only the tab helper discovers providers, on a file trigger."""
import json
import os
from pathlib import Path
import sys
import threading
import time

late = False
lock = threading.Lock()
helper = os.environ.get('RELAY_PANE_ID') == 'switchboard'
def emit(event):
    with lock:
        print(json.dumps(event), flush=True)
        with open(os.environ['RELAY_STAGE_EVENTS'], 'a') as log:
            log.write(json.dumps({'pid': os.getpid(), 'helper': helper, 'direction': 'event', **event}) + '\n')
def presets():
    rows = []
    if helper:
        rows = [dict(id='openrouter', label='openrouter', provider='openrouter', plan='pay-as-you-go',
                     has_stored_key=late, key_source='keyring' if late else '', model='vendor/late-model',
                     models=[dict(id='vendor/late-model', name='late-model', tier='main', efforts=['low', 'high'])]),
                dict(id='guest:codex', label='Codex', guest='codex', installed=late, harness=late,
                     logged_in=late, model='', has_stored_key=False, key_source='guest',
                     models=[dict(id=f'gpt-stage-{i}', name=f'gpt-stage-{i}', efforts=['low', 'high']) for i in range(7)] if late else [])]
    emit(dict(event='presets', presets=rows, tier_list_defaults={}))
def delayed():
    global late
    while not Path(os.environ['RELAY_STAGE_TRIGGER']).exists():
        time.sleep(.05)
    if helper:
        late = True
        emit(dict(event='key_stored', preset='openrouter'))
        presets()  # same asynchronous push emitted when the guest scan completes
threading.Thread(target=delayed, daemon=True).start()
emit(dict(event='ready', version='stage'))
for line in sys.stdin:
    request = json.loads(line)
    kind = request.get('type')
    with lock:
        with open(os.environ['RELAY_STAGE_EVENTS'], 'a') as log:
            log.write(json.dumps(dict(pid=os.getpid(), helper=helper, direction='request', type=kind))+'\n')
    if kind == 'presets': presets()
    elif kind == 'configure':
        emit(dict(event='configured', model='stage', preset='stage', commands=[]))
    elif kind == 'aliases': emit(dict(event='aliases', aliases=[]))
