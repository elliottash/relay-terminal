"""Full Relay keyboard dispatch staging; synthetic stdio worker, isolated XDG."""
import json, os, subprocess, tempfile, time
from pathlib import Path
root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
sandbox = Path(tempfile.mkdtemp(prefix='azp7-live-'))
env = dict(os.environ)
for key, rel in [('XDG_CONFIG_HOME','config'), ('XDG_DATA_HOME','data'), ('XDG_CACHE_HOME','cache'), ('XDG_RUNTIME_DIR','run'), ('TMPDIR','tmp')]:
    p=sandbox/rel; p.mkdir(mode=0o700); env[key]=str(p)
c=sandbox/'config/RelayTerminal'; c.mkdir(); (c/'relay.conf').write_text('[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n[models]\ntier\\main=guest:codex|gpt-6-astra|high\ntier\\high=guest:codex|gpt-6-astra|high\ntier\\flash=guest:codex|gpt-6-astra|low\n[theme]\nname=relay-dark\n')
fixture=sandbox/'fixture';(fixture/'backend').mkdir(parents=True)
(fixture/'backend/worker.py').write_text(r'''import json,sys

def emit(**value): print(json.dumps(value),flush=True)
emit(event='ready')
for line in sys.stdin:
 r=json.loads(line);kind=r.get('type');ident=r.get('id');turn='zoom-fixture'
 if kind=='shutdown':break
 if kind=='presets':emit(event='presets',presets=[dict(id='guest:codex',label='Fixture',group='guest',harness=True,model='gpt-6-astra',models=[dict(id='gpt-6-astra',label='Fixture',name='Fixture')])])
 elif kind=='configure':emit(event='configured',model='gpt-6-astra',mode='build',session_id='zoom-fixture',agent_role='main',effort='high',roles={'main':{'model':'gpt-6-astra'}})
 elif kind=='route':emit(event='route',id=ident,route='agent',text=r.get('text',''))
 elif kind=='ask':
  emit(event='started',id=ident,turn_id=turn)
  emit(event='thinking_delta',turn_id=turn,text='Checking auxiliary keyboard zoom.\nActivity text must grow while the owning terminal stays unchanged.\nReset restores this original size.')
  emit(event='thinking_done',turn_id=turn,elapsed_ms=1200,chars=150)
  emit(event='delta',id=ident,turn_id=turn,text='Owner terminal stays at its original text size.')
  emit(event='done',id=ident,turn_id=turn)
  emit(event='agent_finished',id=ident,turn_id=turn,outcome='done')
''')
for name in ['shell','scripts','assets']:(fixture/name).symlink_to(root/name)
display=next(':'+str(n) for n in range(350,390) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
gate=Path('/tmp/claude-1000/land/azp7/verify')
env.update(DISPLAY=display,RELAY_KEYRING='off',RELAY_NO_ISOLATION='1',RELAY_DATA_DIR=str(fixture));env.pop('RELAY_OPEN_SOCKET',None)
x=subprocess.Popen(['Xvfb',display,'-screen','0','1480x940x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(1)
f=(sandbox/'stderr.log').open('w')
r=subprocess.Popen([str(gate/'build/relay'),'--workspace',str(sandbox),'--clean-shell','--fresh'],env=env,stdout=f,stderr=f)
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
    key('alt+shift+r');key('alt+Left')
    xd('type','--delay',30,'Verify auxiliary zoom');key('Return');time.sleep(2)
    key('alt+Right');time.sleep(1)
    shot('before')
    key('ctrl+equal','ctrl+equal','ctrl+equal');shot('plus')
    key('ctrl+minus');shot('minus')
    key('ctrl+0');shot('reset')
    # Exercise a Tab traversal before another keyboard zoom/reset.
    key('Tab');key('ctrl+equal','ctrl+equal');shot('ask-plus')
    key('ctrl+0');shot('ask-reset')
    (out/'live-results.json').write_text(json.dumps({'binary':str(gate/'build/relay'),'sandbox':str(sandbox),'display':display,'keys':['Alt+Shift+R','Ctrl+= x3','Ctrl+-','Ctrl+0','Tab traversal, Ctrl+= x2','Ctrl+0']},indent=2)+'\n')
finally:
    r.terminate();r.wait(timeout=10);x.terminate();x.wait(timeout=5)

# Measure the rendered word and compare stable text regions (exclude cursor and notices).
from PIL import Image, ImageChops
import csv, io, hashlib
base=Image.open(out/'live-before.png').convert('RGB')
result=json.loads((out/'live-results.json').read_text())
result['binary_sha256']=hashlib.sha256(Path(result['binary']).read_bytes()).hexdigest()
result['measurements']={}
for name in ['before','plus','minus','reset','ask-plus','ask-reset']:
    im=Image.open(out/f'live-{name}.png').convert('RGB')
    tsv=subprocess.check_output(['tesseract',str(out/f'live-{name}.png'),'stdout','tsv'],stderr=subprocess.DEVNULL,text=True)
    word=next(row for row in csv.DictReader(io.StringIO(tsv),delimiter='\t') if row['text']=='Checking')
    owner=ImageChops.difference(base.crop((20,80,680,700)),im.crop((20,80,680,700))).getbbox() is None
    reset=ImageChops.difference(base.crop((735,155,1420,700)),im.crop((735,155,1420,700))).getbbox() is None
    result['measurements'][name]={'checking_word_width':int(word['width']),'checking_word_height':int(word['height']),
                                'owner_terminal_content_identical':owner,'activity_content_matches_baseline':reset}
measure=result['measurements']
assert all(value['owner_terminal_content_identical'] for value in measure.values())
assert measure['plus']['checking_word_width'] > measure['minus']['checking_word_width'] > measure['before']['checking_word_width']
assert measure['reset']['activity_content_matches_baseline'] and measure['ask-reset']['activity_content_matches_baseline']
(out/'live-results.json').write_text(json.dumps(result,indent=2)+'\n')
