#!/usr/bin/env python3
"""What one `board_refresh` costs the Switchboard worker, with nothing changed and with one card changed."""
import json, os, subprocess, sys, time, resource
ws = sys.argv[1]; src = '/tmp/claude-1000/pf4k/src'
env = dict(os.environ, RELAY_KEYRING='off', PYTHONPATH=f'{src}/backend')
p = subprocess.Popen([sys.executable, '-S', '-u', f'{src}/backend/worker.py'],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                     text=True, env=env, cwd=ws)
def send(o):
    p.stdin.write(json.dumps(o) + '\n'); p.stdin.flush()
def wait(evs):
    while True:
        line = p.stdout.readline()
        if not line: sys.exit('worker died')
        m = json.loads(line)
        if m.get('event') in evs: return m, len(line)
send({'type':'configure','workspace':ws,'agent_role':'switchboard','use_stored_key':True,
      'api_key':'','max_tokens':0,'tab':'T1'})
t=time.monotonic(); send({'type':'board_open','id':'r0'}); m,n = wait({'board'})
print(f"board_open      {(time.monotonic()-t)*1000:7.0f} ms   {n/1e6:6.2f} MB   {len(m['cards'])} cards")
card = next(p for p in
            [os.path.join(dp, f) for dp, _, fs in os.walk(ws) for f in fs if f.endswith('.md')]
            if '/features/' in p)
for label, mutate in (('no change', False), ('one card touched', True)):
    times = []
    for i in range(3):
        if mutate:
            with open(card, 'a') as fh: fh.write(f"\n<!-- r{i} -->\n")
        t = time.monotonic(); send({'type':'board_refresh','id':f'x{i}'})
        m, n = wait({'board_changed'}); times.append((time.monotonic()-t)*1000)
    print(f"board_refresh   {min(times):7.0f} ms   {n/1e6:6.2f} MB   ({label}, "
          f"upserts={len(m['upserts'])}, best of 3)")
kid = resource.getrusage(resource.RUSAGE_CHILDREN)
print(f"worker RSS      {int(open(f'/proc/{p.pid}/status').read().split('VmRSS:')[1].split()[0])/1024:7.0f} MB")
p.kill()
