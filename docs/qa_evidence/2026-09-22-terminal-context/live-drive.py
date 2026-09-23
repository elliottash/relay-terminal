"""Isolated real GUI/shell staging with a deterministic worker (no external model)."""
import json, os, subprocess, tempfile, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
sandbox = Path(tempfile.mkdtemp(prefix='tcxt-live-'))
env = dict(os.environ)
for key, name in [('XDG_CONFIG_HOME','config'), ('XDG_DATA_HOME','data'), ('XDG_CACHE_HOME','cache'), ('XDG_RUNTIME_DIR','run'), ('TMPDIR','tmp')]:
    path = sandbox / name; path.mkdir(mode=0o700); env[key] = str(path)
conf = sandbox/'config/RelayTerminal'; conf.mkdir()
(conf/'relay.conf').write_text('[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n[models]\ntier\\main=guest:codex|gpt-6-astra|high\ntier\\high=guest:codex|gpt-6-astra|high\n[index]\nterminal_history=false\nterminal_output=false\n[theme]\nname=relay-dark\n')
fixture=sandbox/'fixture'; (fixture/'backend').mkdir(parents=True)
(fixture/'backend/worker.py').write_text(r'''import json,sys,os
sys.path.insert(0, os.environ['TCXT_BACKEND'])
from relay_core.terminal_context import Service, format_snapshot
service=Service()
def emit(**event): print(json.dumps(event),flush=True)
emit(event='ready')
for line in sys.stdin:
 r=json.loads(line); kind=r.get('type'); ident=r.get('id')
 with open(os.environ['TCXT_REQUESTS'],'a') as f: f.write(json.dumps(r)+'\n')
 if kind=='shutdown': break
 if kind=='presets': emit(event='presets',presets=[dict(id='guest:codex',label='Fixture',group='guest',harness=True,model='gpt-6-astra',models=[dict(id='gpt-6-astra',label='Fixture')])])
 elif kind=='configure': emit(event='configured',model='gpt-6-astra',mode='agent',session_id='tcxt-fixture',agent_role='main',effort='high')
 elif kind=='route': emit(event='route',id=ident,route='shell',text=r.get('text',''),valid=True)
 elif kind=='terminal_context_update': service.update(r['payload'])
 elif kind=='terminal_context_preview': emit(event='terminal_context_preview',text=format_snapshot(r['payload']))
 elif kind=='ask':
  service.set_snapshot(r.get('context',{}).get('terminal_context'))
  emit(event='agent_started',id=ident,turn_id='fixture')
  emit(event='delta',text=format_snapshot(service.snapshot()) or 'No terminal evidence attached.')
  emit(event='agent_finished',id=ident,outcome='completed',turn_id='fixture')
''')
for name in ['shell','scripts','assets']: (fixture/name).symlink_to(ROOT/name)
display=next(':'+str(n) for n in range(410,450) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
env.update(DISPLAY=display, RELAY_KEYRING='off', RELAY_NO_ISOLATION='1', RELAY_DATA_DIR=str(fixture), TCXT_BACKEND=str(ROOT/'backend'), TCXT_REQUESTS=str(sandbox/'requests.jsonl'))
env.pop('RELAY_OPEN_SOCKET',None)
x=subprocess.Popen(['Xvfb',display,'-screen','0','1480x940x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(1)
log=(sandbox/'stderr.log').open('w')
app=subprocess.Popen([os.environ.get('RELAY_TEST_BINARY',str(ROOT/'build/relay')),'--workspace',str(sandbox),'--clean-shell','--fresh'],env=env,stdout=log,stderr=log)
def xd(*args): return subprocess.check_output(['xdotool',*map(str,args)],env=env,text=True).strip()
def type_line(text):
    xd('type','--clearmodifiers','--delay',5,text); xd('key','Return'); time.sleep(2)
def requests():
    p=sandbox/'requests.jsonl'
    return [json.loads(line) for line in p.read_text().splitlines()] if p.exists() else []
def wait(predicate, timeout=15):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if predicate(): return
        time.sleep(.1)
    raise AssertionError('Timed out; sandbox '+str(sandbox))
try:
    time.sleep(5)
    wins=xd('search','--pid',app.pid).splitlines()
    win=max(wins,key=lambda w: int(xd('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0])*int(xd('getwindowgeometry','--shell',w).split('HEIGHT=')[1].splitlines()[0]))
    xd('windowmove',win,0,0); xd('windowsize',win,1440,900); xd('windowfocus',win)
    # Configure the deferred worker without a shell command first.
    type_line('*Prepare the terminal context test')
    command="printf 'TCXT_FIRST\\nTCXT_LAST\\n'; false"
    before=sum(r.get('type')=='ask' for r in requests())
    type_line(command)
    wait(lambda: any(r.get('type')=='terminal_context_update' and any(x.get('command')==command and x.get('state')=='completed' for x in r['payload']['records']) for r in requests()))
    assert sum(r.get('type')=='ask' for r in requests())==before, 'Completion started inference'
    type_line('*What command did I just run and what did it say?')
    ask=[r for r in requests() if r.get('type')=='ask'][-1]
    records=ask['context']['terminal_context']['records']; assert len(records)==1
    record=records[0]; assert record['command']==command, record
    assert record['exit_status']==1 and record['state']=='completed', record
    assert 'TCXT_FIRST' in record['output'] and 'TCXT_LAST' in record['output'], record
    assert not any(r.get('type')=='terminal_history' for r in requests()), 'History indexing unexpectedly enabled'
    def menu(action):
        xd('mousemove',70,794); xd('click',1); time.sleep(.3)
        y={'remove':731,'manual':860,'off':890,'automatic':830,'attach':761}[action]
        xd('mousemove',140,y); time.sleep(.3)
        if action=='attach':
            xd('click',1); time.sleep(.5)
            subprocess.run(['import','-window','root',str(sandbox/'attach-menu.png')],env=env,check=True)
            xd('mousemove',440,761); xd('click',1)
        else: xd('click',1)
        time.sleep(.5)
    # Removing a chip must remove both output and read grants for this turn.
    menu('remove')
    xd('mousemove',240,827); xd('click',1)
    type_line('*Answer without the removed terminal output')
    removed=[r for r in requests() if r.get('type')=='ask'][-1]
    assert removed['context']['terminal_context']['records']==[], removed
    # Manual shares nothing until an explicit attachment is selected.
    menu('manual')
    xd('mousemove',240,827); xd('click',1)
    type_line('*Check manual mode without attachment')
    manual=[r for r in requests() if r.get('type')=='ask'][-1]['context']['terminal_context']
    assert manual=={'mode':'manual','records':[]}, manual
    # In manual mode Preview is disabled: Home selects Remove, next is Attach output.
    menu('attach')
    xd('mousemove',240,827); xd('click',1)
    type_line('*Read the output I attached explicitly')
    attached=[r for r in requests() if r.get('type')=='ask'][-1]['context']['terminal_context']
    assert attached['mode']=='manual' and attached['records'][0]['command_id']==record['command_id'], attached
    menu('off')
    xd('mousemove',240,827); xd('click',1)
    type_line('*Check sharing is off')
    off=[r for r in requests() if r.get('type')=='ask'][-1]['context']['terminal_context']
    assert off=={'mode':'off','records':[]}, off
    # Native Readline command, then a question from the composer.
    menu('automatic')
    xd('key','ctrl+h'); time.sleep(.3)
    native_command="printf 'TCXT_NATIVE\\n'"
    type_line(native_command)
    xd('key','F12'); time.sleep(.3)
    xd('mousemove',240,827); xd('click',1)
    type_line('*What native command just ran?')
    native=[r for r in requests() if r.get('type')=='ask'][-1]['context']['terminal_context']['records'][0]
    assert native['command']==native_command and 'TCXT_NATIVE' in native['output'], native
    xd('mousemove',1460,920); time.sleep(.5)
    subprocess.run(['import','-window',win,str(OUT/'terminal-context.png')],env=env,check=True)
    (OUT/'results.json').write_text(json.dumps({'checks':{'composer_capture':True,'exit_status':True,'head_tail_markers':True,'history_disabled':True,'completion_without_inference':True,'snapshot_in_ask':True,'removal':True,'manual':True,'explicit_attachment':True,'off':True,'native_bash':True},'record':record,'native_record':native,'sandbox':str(sandbox),'worker':'deterministic fixture; no external model'},indent=2)+'\n')
    print('PASS',sandbox)
finally:
    app.terminate(); app.wait(timeout=10); x.terminate(); x.wait(timeout=5)
