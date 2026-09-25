import json, os, sys, time

# Fixture for card #EQH0: the Sources tab with three guest logins carrying `email`, and a
# `usage_refresh` that is recorded (RELAY_FIXTURE_LOG) and answered with fresh usage.

LOG = os.environ.get("RELAY_FIXTURE_LOG", "/dev/null")

def emit(event, **kw):
    print(json.dumps(dict(event=event, **kw)), flush=True)

def presets(used):
    return [
        {"id": "guest:claude", "provider": "claude", "label": "Claude Code", "guest": "claude",
         "logged_in": True, "email": "elliott.ash@gess.ethz.ch"},
        {"id": "guest:codex", "provider": "codex", "label": "Codex", "guest": "codex",
         "logged_in": True, "email": "e@elliottash.com",
         "limits": {"windows": [{"kind": "weekly", "used_percent": used, "resets_at": int(time.time()) + 86400}]}},
        {"id": "guest:codex:ashe-ethz-ch", "provider": "codex", "label": "Codex (ashe@ethz.ch)", "guest": "codex",
         "account": "ashe-ethz-ch", "account_label": "ashe@ethz.ch", "config_dir": "/tmp/codex-ashe",
         "logged_in": True, "email": "ashe@ethz.ch"},
    ]

used = 80.0
emit('ready')
for line in sys.stdin:
    try:
        r = json.loads(line)
    except ValueError:
        continue
    t = r.get('type')
    with open(LOG, "a") as log:
        log.write(t + "\n")
    if t == 'presets':
        emit('presets', presets=presets(used))
    elif t == 'usage_refresh':
        used = 12.0
        emit('usage_limits', preset='guest:codex', guest='codex', source='subscription_poll',
             windows=[{"kind": "weekly", "used_percent": used, "resets_at": int(time.time()) + 86400}])
        emit('presets', presets=presets(used))
        emit('usage_refreshed', id=r.get('id'), at=int(time.time()))
    elif t == 'configure':
        emit('configured', model='fixture', context=r.get('context', {}), agent_role='switchboard')
    elif t == 'board_open':
        emit('board', config={'columns': ['inbox', 'done'], 'tabs': [{'id': 'features', 'folder': 'features'}]},
             cards=[], problems=[])
    elif t == 'shutdown':
        break
