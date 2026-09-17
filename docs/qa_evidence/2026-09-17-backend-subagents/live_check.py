import json, os, subprocess, sys, threading, time, queue
WORKER, WS, PRESET, LOG = sys.argv[1:5]
os.makedirs(WS + '/fixture', exist_ok=True)
for name, text in {'alpha.txt': 'alpha: apples are red\n', 'beta.txt': 'beta: bananas are yellow\nsecond line\n',
                   'notes.md': '# Notes\nCherries ripen in June.\n'}.items():
    open(f'{WS}/fixture/{name}', 'w').write(text)
sys.path.insert(0, os.path.dirname(WORKER))
from relay_core.presets import PRESETS
p = PRESETS[PRESET]
proc = subprocess.Popen([sys.executable, WORKER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
events = queue.Queue()
log = open(LOG, 'w')


def reader():
    for line in proc.stdout:
        e = json.loads(line)
        events.put(e)
        if e.get('event') == 'delta' or (e.get('event') == 'subagent_event' and e['payload'].get('event') == 'delta'):
            continue
        log.write(f"{time.strftime('%H:%M:%S')} {json.dumps(e, ensure_ascii=False)[:1500]}\n")
        log.flush()


threading.Thread(target=reader, daemon=True).start()


def send(m):
    proc.stdin.write(json.dumps(m) + '\n')
    proc.stdin.flush()


send({'type': 'configure', 'preset': PRESET, 'base_url': p.base_url, 'model': p.model, 'extra': p.extra,
      'use_stored_key': True, 'workspace': WS})
send({'type': 'agents_list', 'id': 'L'})
send({'type': 'ask', 'text': 'Use the explore subagent in the background (background: true) to list and summarize '
      'the files in the fixture directory. Do not read the files yourself. After starting it, just tell me it is '
      'running and end your turn.'})
deadline = time.time() + 600
finished_turns, sub_done, subscribed = 0, False, False
while time.time() < deadline:
    try:
        e = events.get(timeout=1)
    except queue.Empty:
        continue
    kind = e.get('event')
    if kind == 'subagent_started' and not subscribed:
        send({'type': 'agent_subscribe', 'id': e['id'], 'on': True})
        subscribed = True
    if kind == 'subagent_finished':
        sub_done = True
    if kind == 'agent_finished':
        finished_turns += 1
        if finished_turns >= 2 and sub_done:
            break
send({'type': 'shutdown'})
time.sleep(1)
print('turns', finished_turns, 'sub_done', sub_done)
