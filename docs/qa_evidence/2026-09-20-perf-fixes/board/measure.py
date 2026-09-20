#!/usr/bin/env python3
"""board_open / board_refresh / board_search against one backend.

    measure.py <backend src root> <workspace>
"""
import collections
import json
import os
import subprocess
import sys
import time

src, ws = sys.argv[1], sys.argv[2]
env = dict(os.environ, RELAY_KEYRING='off', PYTHONPATH=f'{src}/backend')
p = subprocess.Popen([sys.executable, '-S', '-u', f'{src}/backend/worker.py'],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                     text=True, env=env, cwd=ws)


def send(o):
    p.stdin.write(json.dumps(o) + '\n')
    p.stdin.flush()


def wait(evs):
    while True:
        line = p.stdout.readline()
        if not line:
            sys.exit('worker died')
        m = json.loads(line)
        if m.get('event') in evs:
            return m, len(line)


send({'type': 'configure', 'workspace': ws, 'agent_role': 'switchboard',
      'use_stored_key': True, 'api_key': '', 'max_tokens': 0, 'tab': 'T1'})
t = time.monotonic()
send({'type': 'board_open', 'id': 'r0'})
m, n = wait({'board'})
total = n
cards = list(m['cards'])
biggest = n
while m.get('more'):
    m, n = wait({'board_cards'})
    total += n
    biggest = max(biggest, n)
    cards += m['cards']
open_ms = (time.monotonic() - t) * 1000
print(f"board_open      {open_ms:7.0f} ms   {total/1e6:6.2f} MB total, biggest message "
      f"{biggest/1e6:5.2f} MB   {len(cards)} cards   {total/max(1,len(cards)):.0f} B/card")

size = collections.Counter()
for c in cards:
    for k, v in c.items():
        size[k] += len(json.dumps(v, default=str))
for k, v in size.most_common(4):
    print(f"    {k:16s} {100*v/total:5.1f}%  {v/len(cards):6.0f} B/card")

card = next(q for q in
            [os.path.join(dp, f) for dp, _, fs in os.walk(ws) for f in fs if f.endswith('.md')]
            if '/features/' in q)
for label, mutate in (('no change', False), ('one card touched', True)):
    times = []
    for i in range(3):
        if mutate:
            with open(card, 'a') as fh:
                fh.write(f"\n<!-- r{i} -->\n")
        t = time.monotonic()
        send({'type': 'board_refresh', 'id': f'x{i}'})
        m, n = wait({'board_changed'})
        times.append((time.monotonic() - t) * 1000)
    print(f"board_refresh   {min(times):7.0f} ms   ({label}, upserts={len(m['upserts'])}, best of 3)")

for q in ('zzzz', 'composer', 'switchboard pane'):
    times = []
    for _ in range(3):
        t = time.monotonic()
        send({'type': 'board_search', 'id': 's', 'query': q})
        m, n = wait({'board_search', 'error'})
        times.append((time.monotonic() - t) * 1000)
    if m.get('event') == 'error':
        print(f"board_search    unsupported ({m.get('text','')[:40]})")
        break
    print(f"board_search    {min(times):7.1f} ms   {n:6d} B   \"{q}\" -> {len(m['ids'])} ids")

print(f"worker RSS      {int(open(f'/proc/{p.pid}/status').read().split('VmRSS:')[1].split()[0])/1024:7.0f} MB")
p.kill()
