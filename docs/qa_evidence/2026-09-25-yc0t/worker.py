import json, os, sys

# Fixture for card #YC0T: Sources with the Z.AI Coding Plan and Kimi Code rows (keys stored,
# `accounts_allowed`), and `key_account_save` answered the way the real worker answers it — the
# account becomes a row of its own under `<plan>:<slug>`. The request is recorded without its key.

LOG = os.environ.get("RELAY_FIXTURE_LOG", "/dev/null")
accounts = []

def emit(event, **kw):
    print(json.dumps(dict(event=event, **kw)), flush=True)

def plan(pid, label, provider):
    return {"id": pid, "label": label, "provider": provider, "kind": "plan", "group": "plan",
            "has_stored_key": True, "key_source": "keyring", "accounts_allowed": True}

def presets():
    rows = [plan("glm-coding", "z.ai · glm-5.3 · coding plan", "Z.AI"),
            plan("kimi-code", "kimi code · k3", "Kimi")]
    for pid, name in accounts:
        base = next(r for r in rows if r["id"] == pid.split(":")[0])
        rows.append({**base, "id": pid, "label": f"{base['label']} ({name})", "account": pid.split(":")[1],
                     "account_label": name, "base_preset": base["id"], "accounts_allowed": False})
    return rows

emit('ready')
for line in sys.stdin:
    try:
        r = json.loads(line)
    except ValueError:
        continue
    t = r.get('type')
    record = {"type": t}
    if t == 'key_account_save':
        a = r.get('account') or {}
        record.update(preset=a.get('preset'), label=a.get('label'), key_length=len(a.get('api_key') or ''))
        pid = f"{a.get('preset')}:{(a.get('label') or '').lower()}"
        accounts.append((pid, a.get('label')))
        emit('key_account_saved', id=r.get('id'), account={"preset_id": pid})
        emit('presets', presets=presets())
    with open(LOG, "a") as log:
        log.write(json.dumps(record) + "\n")
    if t == 'presets':
        emit('presets', presets=presets())
    elif t == 'configure':
        emit('configured', model='fixture', context=r.get('context', {}), agent_role='switchboard')
    elif t == 'board_open':
        emit('board', config={'columns': ['inbox', 'done'], 'tabs': [{'id': 'features', 'folder': 'features'}]},
             cards=[], problems=[])
    elif t == 'shutdown':
        break
