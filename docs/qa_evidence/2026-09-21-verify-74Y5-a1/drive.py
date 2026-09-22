"""Live named-control verification on isolated exact build; no coordinate input."""
import os,subprocess,time,json,tempfile,hashlib
from pathlib import Path
out=Path(__file__).resolve().parent; gate=Path('/tmp/claude-1000/land/driven/verify')
root=Path(tempfile.mkdtemp(prefix='v74y5-')); (root/'board/features').mkdir(parents=True)
(root/'board/board.yaml').write_text('version: 1\ntabs: [{id: features, folder: features}]\ncolumns: [inbox, executing, needs-verification, done]\nagent: {autonomy: auto, max_creates_per_turn: 5}\n')
(root/'board/features/fixture.md').write_text('---\nid: T9QA\ntype: work\nstatus: needs-verification\nlabels: [feature]\nrank: m\ncreated: \'2026-09-21\'\nlinks: {plans: [], commits: [], evidence: [], related: [], github: null}\n---\n# Named driver verification\n\n## Issue\nFresh named-control marker 74Y5.\n\n## Done means\nRead this exact marker by name.\n\n## Tests\n`tests/missing_test.py`\n')
subprocess.run(['git','init','-q',str(root)],check=True)
env=dict(os.environ)
for key,rel in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_RUNTIME_DIR','run'),('TMPDIR','tmp')]:
 d=root/rel;d.mkdir(mode=0o700);env[key]=str(d)
c=root/'config/RelayTerminal';c.mkdir();(c/'relay.conf').write_text('[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n')
display=next(':'+str(n) for n in range(300,330) if not Path('/tmp/.X11-unix/X'+str(n)).exists());env.update(DISPLAY=display,RELAY_KEYRING='off',RELAY_NO_ISOLATION='1',RELAY_DATA_DIR=str(gate/'src'));env.pop('RELAY_OPEN_SOCKET',None)
x=subprocess.Popen(['Xvfb',display,'-screen','0','1600x1000x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);time.sleep(1)
f=(root/'stderr').open('w');r=subprocess.Popen([str(gate/'build/relay'),'--workspace',str(root),'--clean-shell','--fresh'],env=env,cwd=root,stdout=f,stderr=f)
rows=[]
def drive(op,name='',text=''):
 p=subprocess.run(['python3',str(gate/'src/scripts/relay-drive'),op,name,text],env=env,capture_output=True,text=True);v=json.loads(p.stdout);rows.append(dict(op=op,name=name,text=text,response=v,exit=p.returncode));return v
def wait(op,name='',needle=None):
 deadline=time.monotonic()+15
 while time.monotonic()<deadline:
  v=drive(op,name)
  if v.get('ok') and (needle is None or needle in v.get('text','')):return v
  time.sleep(.3)
 raise RuntimeError('Timed out '+op+' '+name)
try:
 time.sleep(5)
 wait('action','board.open');wait('open','T9QA');wait('read','sections','Fresh named-control marker 74Y5.')
 assert not drive('press','nonexistent')['ok']
 assert not drive('type','composer','must not run')['ok']
 assert not drive('type','boardFilter','hidden')['ok']
 assert drive('press','boardTestsCheck')['ok'];wait('read','boardTestsFindings')
 assert drive('press','boardCardClose')['ok'];wait('read','boardFilter')
 assert drive('type','boardFilter','T9QA')['ok'];assert drive('read','boardFilter')['text']=='T9QA'
 assert drive('type','boardFilter','')['ok'];wait('open','T9QA');wait('read','sections','Fresh named-control marker 74Y5.')
 drive('read','notice');drive('panes');subprocess.run(['import','-window','root',str(out/'ui.png')],env=env,check=True)
finally:
 (out/'transcript.json').write_text(json.dumps(dict(binary=str(gate/'build/relay'),sha256=hashlib.sha256((gate/'build/relay').read_bytes()).hexdigest(),data=str(gate/'src'),root=str(root),display=display,steps=rows),indent=2)+'\n')
 r.terminate();r.wait(timeout=8);x.terminate();x.wait();f.close()
print('Completed',len(rows),'named operations')
