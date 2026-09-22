import os,sys,subprocess,time,json,tempfile,hashlib,importlib.util
from pathlib import Path
REPO=Path('/home/elliott/repos/relay-terminal'); OUT=Path(tempfile.mkdtemp(prefix='hg26-v4-evidence-'))
sys.path.insert(0,str(REPO/'backend'))
from relay_core.keystore import lookup,env_name
preset=os.environ.get('VERIFY_PRESET','kimi'); model=os.environ.get('VERIFY_MODEL','kimi-k3')
key=lookup(preset); assert key,'No configured key'
root=Path(tempfile.mkdtemp(prefix='hg26-v4-live-')); (root/'board/features').mkdir(parents=True)
(root/'board/board.yaml').write_text('version: 1\ntabs: [{id: features, folder: features}]\ncolumns: [inbox, executing, needs-verification, done]\nagent: {autonomy: auto, max_creates_per_turn: 5}\n')
card=root/'board/features/fixture.md'
card.write_text("---\nid: T9QA\ntype: work\nstatus: inbox\nlabels: [bug]\nrank: m\ncreated: '2026-09-22'\nlinks: {plans: [], commits: [], evidence: [], related: [], github: null}\n---\n# Add a dark theme selector\n\n## Issue\nPlease add a new dark theme selector to this application. This is a new feature, not a bug.\n")
(root/'board/features/vocabulary.md').write_text("---\nid: F9QA\ntype: work\nstatus: inbox\nlabels: [feature]\nrank: n\ncreated: '2026-09-22'\nlinks: {plans: [], commits: [], evidence: [], related: [], github: null}\n---\n# Add export menu\n\n## Issue\nAdd a new export menu.\n")
subprocess.run(['git','init','-q',str(root)],check=True)
env=dict(os.environ)
for k,r in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_RUNTIME_DIR','run'),('TMPDIR','tmp')]:
 d=root/r;d.mkdir(mode=0o700);env[k]=str(d)
c=root/'config/RelayTerminal';c.mkdir();(c/'relay.conf').write_text(f'[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n[models]\ntier\\main={preset}|{model}|low\ntier\\flash={preset}|{model}|low\ntier\\high={preset}|{model}|low\n[roles]\nswitchboard\\preset={preset}\nswitchboard\\model={model}\n[suggestions]\nnext_command=false\nnext_prompt=false\n')
display=next(':'+str(n) for n in range(400,430) if not Path('/tmp/.X11-unix/X'+str(n)).exists());env.update(DISPLAY=display,RELAY_KEYRING='off',RELAY_NO_ISOLATION='1',RELAY_DATA_DIR=str(REPO),QT_QPA_PLATFORM='xcb');env[env_name(preset)]=key;env.pop('RELAY_OPEN_SOCKET',None)
x=subprocess.Popen(['Xvfb',display,'-screen','0','1600x1000x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);time.sleep(1)
f=(OUT/'live-stderr.log').open('w');r=subprocess.Popen([str(REPO/'build/relay'),'--workspace',str(root),'--clean-shell','--fresh'],env=env,cwd=root,stdout=f,stderr=f)
rows=[]
def drive(op,name='',text=''):
 p=subprocess.run(['python3',str(REPO/'scripts/relay-drive'),op,name,text],env=env,capture_output=True,text=True);v=json.loads(p.stdout);rows.append(dict(op=op,name=name,text=text,response=v,exit=p.returncode));return v
def wait(op,name='',needle=None,seconds=25):
 deadline=time.monotonic()+seconds
 while time.monotonic()<deadline:
  v=drive(op,name)
  if v.get('ok') and (needle is None or needle in v.get('text','')):return v
  time.sleep(.5)
 raise RuntimeError('Timed out '+op+' '+name+' '+str(v))
def shot(name):subprocess.run(['import','-window','root',str(OUT/name)],env=env,check=True)
try:
 time.sleep(5);wait('action','board.open');wait('read','boardChatCheck');assert drive('press','boardChatCheck')['ok'];wait('read','boardChatFindingsHead','Hygiene');wait('read','boardCleanup');shot('live-format.png');before=card.read_text();assert drive('press','boardCleanup')['ok'];print('LIVE START',root,flush=True)
 deadline=time.monotonic()+240
 while time.monotonic()<deadline:
  v=drive('read','boardCleanupHead');t=v.get('text','');print(t,flush=True)
  if 'preview' in t.lower() and ('proposed' in t.lower()):break
  if 'failed' in t.lower() or 'error' in t.lower():raise RuntimeError(t)
  time.sleep(3)
 shot('live-preview.png');drive('read','boardCleanupBody');assert card.read_text()==before,'Preview wrote fixture'
 # Apply is disambiguated by its visible label by the named driver.
 subprocess.run(['xdotool','mousemove','1077','439','click','1'],env=env,check=True);rows.append(dict(op='click',display=display,x=1077,y=439,reason='Apply after visually inspecting live-preview.png; fixed 1600x1000 Xvfb'))
 print('APPLY clicked',flush=True)
 deadline=time.monotonic()+240
 while time.monotonic()<deadline:
  v=drive('read','boardCleanupHead');t=v.get('text','');print(t,flush=True)
  if 'written' in t.lower() and 'preview' not in t.lower():break
  if 'failed' in t.lower() or 'error' in t.lower():raise RuntimeError(t)
  time.sleep(3)
 shot('live-applied.png');drive('read','boardCleanupBody');assert card.read_text()!=before,'Apply did not change fixture';assert 'labels: [feature]' in card.read_text(),card.read_text();print('LIVE PASS',flush=True)
finally:
 (OUT/'live-transcript.json').write_text(json.dumps(dict(binary=str(REPO/'build/relay'),sha256=hashlib.sha256((REPO/'build/relay').read_bytes()).hexdigest(),root=str(root),provider=preset,model=model,steps=rows),indent=2)+'\n')
 (OUT/'live-card-after.md').write_text(card.read_text())
 r.terminate();r.wait(timeout=8);x.terminate();x.wait();f.close()
