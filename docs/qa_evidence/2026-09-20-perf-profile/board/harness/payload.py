#!/usr/bin/env python3
"""What the `board` event is made of, field by field."""
import json, os, subprocess, sys, collections
ws = sys.argv[1]; src = '/tmp/claude-1000/pf4k/src'
env = dict(os.environ, RELAY_KEYRING='off', PYTHONPATH=f'{src}/backend')
p = subprocess.Popen([sys.executable, '-S', '-u', f'{src}/backend/worker.py'],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                     text=True, env=env, cwd=ws)
p.stdin.write(json.dumps({'type':'configure','workspace':ws,'agent_role':'switchboard',
    'use_stored_key':True,'api_key':'','max_tokens':0,'tab':'T1'})+'\n')
p.stdin.write(json.dumps({'type':'board_open','id':'r1'})+'\n'); p.stdin.flush()
while True:
    line = p.stdout.readline()
    if not line: sys.exit('EOF')
    m = json.loads(line)
    if m.get('event') == 'board': break
p.kill()
cards = m['cards']
print(f"cards={len(cards)} total_line={len(line)} bytes  ({len(line)/len(cards):.0f} B/card)")
size = collections.Counter()
for c in cards:
    for k, v in c.items():
        size[k] += len(json.dumps(v))
for k, v in size.most_common(14):
    print(f"  {k:18s} {v/1024:9.0f} KiB  {100*v/len(line):5.1f}%  {v/len(cards):6.0f} B/card")
rest = len(line) - sum(size.values())
print(f"  {'(non-card fields)':18s} {rest/1024:9.0f} KiB  {100*rest/len(line):5.1f}%")
