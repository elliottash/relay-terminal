import json,sys

# Fixture for card #WBFM evidence: enough of the worker protocol for the models pane's Sources
# tab — two guest CLIs (one logged in, one not) and one keyed provider, no models needed.

def emit(event, **kw):
    print(json.dumps(dict(event=event, **kw)), flush=True)

PRESETS = [
    {"id": "guest:claude", "provider": "claude", "label": "Claude Code", "logged_in": True},
    {"id": "guest:codex", "provider": "codex", "label": "Codex", "logged_in": False},
    {"id": "anthropic", "provider": "Anthropic", "label": "Anthropic",
     "key_url": "https://console.anthropic.com", "has_stored_key": False},
]

emit('ready')
for line in sys.stdin:
    try:
        r = json.loads(line)
    except ValueError:
        continue
    t = r.get('type')
    if t == 'presets':
        emit('presets', presets=PRESETS)
    elif t == 'configure':
        emit('configured', model='fixture', context=r.get('context', {}), agent_role='switchboard')
    elif t == 'board_open':
        emit('board', config={'columns': ['inbox', 'done'], 'tabs': [{'id': 'features', 'folder': 'features'}]},
             cards=[], problems=[])
    elif t == 'shutdown':
        break
