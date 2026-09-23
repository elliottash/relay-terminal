"""Relaying notifier staging; synthetic stdio worker, isolated XDG."""
import json, os, subprocess, tempfile, time
from pathlib import Path
root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
sandbox = Path(tempfile.mkdtemp(prefix='stpc-live-'))
env = dict(os.environ)
for key, rel in [('XDG_CONFIG_HOME','config'), ('XDG_DATA_HOME','data'), ('XDG_CACHE_HOME','cache'), ('XDG_RUNTIME_DIR','run'), ('TMPDIR','tmp')]:
    p=sandbox/rel; p.mkdir(mode=0o700); env[key]=str(p)
c=sandbox/'config/RelayTerminal'; c.mkdir(); (c/'relay.conf').write_text('[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n[models]\ntier\\main=guest:codex|gpt-6-astra|high\ntier\\high=guest:codex|gpt-6-astra|high\ntier\\flash=guest:codex|gpt-6-astra|low\n[theme]\nname=relay-dark\n')
fixture=sandbox/'fixture';(fixture/'backend').mkdir(parents=True)
(fixture/'backend/worker.py').write_text(r'''import json,sys

def emit(**value): print(json.dumps(value),flush=True)
emit(event='ready')
for line in sys.stdin:
 r=json.loads(line);kind=r.get('type');ident=r.get('id');turn='notifier-fixture'
 if kind=='shutdown':break
 if kind=='presets':emit(event='presets',presets=[dict(id='guest:codex',label='Fixture',group='guest',harness=True,model='gpt-6-astra',models=[dict(id='gpt-6-astra',label='Fixture',name='Fixture')])])
 elif kind=='configure':emit(event='configured',model='gpt-6-astra',mode='build',session_id='notifier-fixture',agent_role='main',effort='high',roles={'main':{'model':'gpt-6-astra'}})
 elif kind=='route':emit(event='route',id=ident,route='agent',text=r.get('text',''))
 elif kind=='ask':
  emit(event='agent_started',id=ident,turn_id=turn)
  emit(event='status',text='Requesting model · step 7/500')
''')
for name in ['shell','scripts','assets']:(fixture/name).symlink_to(root/name)
display=next(':'+str(n) for n in range(350,390) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
binary=Path(os.environ.get('RELAY_TEST_BINARY', str(root/'build/relay'))).expanduser().resolve()
env.update(DISPLAY=display,RELAY_KEYRING='off',RELAY_NO_ISOLATION='1',RELAY_DATA_DIR=str(fixture));env.pop('RELAY_OPEN_SOCKET',None)
x=subprocess.Popen(['Xvfb',display,'-screen','0','1480x940x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(1)
f=(sandbox/'stderr.log').open('w')
r=subprocess.Popen([str(binary),'--workspace',str(sandbox),'--clean-shell','--fresh'],env=env,stdout=f,stderr=f)
def xd(*args):return subprocess.check_output(['xdotool',*map(str,args)],env=env,text=True).strip()
def key(*args):xd('key','--delay','100',*args);time.sleep(.5)
def shot(name):
    xd('mousemove',1460,920);time.sleep(.4)
    subprocess.run(['import','-window',win,str(out/f'live-{name}.png')],env=env,check=True)
try:
    time.sleep(6)
    wins=xd('search','--pid',r.pid).splitlines()
    win=max(wins,key=lambda w:int(xd('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0])*int(xd('getwindowgeometry','--shell',w).split('HEIGHT=')[1].splitlines()[0]))
    xd('windowmove',win,0,0);xd('windowsize',win,1440,900);xd('windowfocus',win);time.sleep(1)
    xd('type','--delay',30,'*Check notifier');key('Return');time.sleep(5)
    shot('notifier')
    (out/'live-results.json').write_text(json.dumps({'binary':str(binary),'status_event':'Requesting model · step 7/500','display':display},indent=2)+'\n')
finally:
    r.terminate();r.wait(timeout=10);x.terminate();x.wait(timeout=5)

