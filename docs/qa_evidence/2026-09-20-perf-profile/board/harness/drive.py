#!/usr/bin/env python3
"""Drive backend/worker.py by hand: configure on a board, then board_open, and time it."""
import json, os, subprocess, sys, time
ws = sys.argv[1]
src = '/tmp/claude-1000/pf4k/src'
env = dict(os.environ, RELAY_KEYRING='off', PYTHONPATH=f'{src}/backend')
p = subprocess.Popen([sys.executable, '-S', '-u', f'{src}/backend/worker.py'],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                     text=True, env=env, cwd=ws)
def send(o):
    p.stdin.write(json.dumps(o) + '\n'); p.stdin.flush()
t0 = time.monotonic()
send({'type': 'configure', 'workspace': ws, 'agent_role': 'switchboard',
      'use_stored_key': True, 'api_key': '', 'max_tokens': 0, 'tab': 'T1'})
send({'type': 'board_open', 'id': 'r1'})
deadline = time.monotonic() + 120
while time.monotonic() < deadline:
    line = p.stdout.readline()
    if not line:
        print('EOF; rc=', p.poll()); print('STDERR:', p.stderr.read()[-4000:]); break
    try: msg = json.loads(line)
    except Exception: print('raw:', line[:200]); continue
    ev = msg.get('event') or msg.get('type')
    print(f"{(time.monotonic()-t0)*1000:8.0f}ms  {ev}  bytes={len(line)}"
          + (f" cards={len(msg.get('cards', []))}" if ev == 'board' else ''))
    if ev == 'board':
        print('  total board payload bytes:', len(line))
        break
p.kill()
