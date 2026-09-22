"""Isolated Xvfb regression drive; fake stdio worker, no provider calls or real settings."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
FAKE = r'''
import json, os, sys, time
from pathlib import Path
log = Path(os.environ['PROBE_LOG'])
mode = 'build'
def record(**value):
    with log.open('a') as f: f.write(json.dumps(dict(pid=os.getpid(), **value))+'\n')
def emit(**value):
    print(json.dumps(value), flush=True)
record(type='process_start')
emit(event='ready')
for line in sys.stdin:
    r=json.loads(line); kind=r.get('type'); record(request=r, type=kind, effective_mode=mode)
    if kind == 'shutdown': break
    if kind == 'presets':
        emit(event='presets', presets=[dict(id='guest:codex',label='codex',group='guest',harness=True,model='gpt-6-astra',models=[dict(id='gpt-6-astra',label='gpt-6-astra',name='gpt-6-astra')])])
    elif kind == 'configure':
        marker=Path(os.environ['PROBE_MARKER'])
        failure=os.environ['PROBE_FAILURE']
        if failure == 'always' or (failure == 'once' and not marker.exists()):
            marker.touch()
            emit(event='error', code='configure_failed', exception='NameError', text="name 'fixture_dependency' is not defined", restart_worker=True, agent_busy=False)
        else:
            time.sleep(.3)
            emit(event='configured',model='gpt-6-astra',mode='build',agent_role='main',effort='high',session_id='fixture',roles={'main':{'model':'gpt-6-astra'}},skill_commands=[{'name':n,'description':'Fixture skill'} for n in ['deliver','clean-commit','local-model-setup']])
    elif kind == 'set_mode':
        mode=r['mode']; emit(event='mode_changed',mode=mode)
    elif kind == 'route':
        emit(event='route',id=r.get('id'),route='shell' if r.get('text')=='ls' else 'agent',text=r.get('text',''))
    elif kind == 'ask':
        emit(event='started',id=r.get('id')); emit(event='done',id=r.get('id')); emit(event='agent_finished',id=r.get('id'),outcome='done'); emit(event='session_title',title='Fixing pane title retries',source='model')
'''

def wait_for(check, message, seconds=12):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        value=check()
        if value: return value
        time.sleep(.1)
    raise AssertionError(message)

def run_case(name, failure, toggles, expected):
    with tempfile.TemporaryDirectory(prefix='relay-start-') as tmp:
        home=Path(tmp)
        for p in ('config/RelayTerminal','data','cache','run','fixture/backend','work'):
            (home/p).mkdir(parents=True)
        (home/'run').chmod(0o700)
        (home/'fixture/backend/worker.py').write_text(FAKE)
        for p in ('shell','scripts','assets'):
            if (ROOT/p).exists(): (home/'fixture'/p).symlink_to(ROOT/p, target_is_directory=True)
        (home/'config/RelayTerminal/relay.conf').write_text('''[instructions]
onboarded=true
[isolation]
enabled=false
[models]
tier\\main=guest:codex|gpt-6-astra|high
tier\\high=guest:codex|gpt-6-astra|high
tier\\flash=guest:codex|gpt-6-astra|low
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
[url_handler]
announced=true
''')
        display=next(':'+str(n) for n in range(450,490) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
        env={**os.environ,'XDG_CONFIG_HOME':tmp+'/config','XDG_DATA_HOME':tmp+'/data',
             'XDG_CACHE_HOME':tmp+'/cache','XDG_RUNTIME_DIR':tmp+'/run','DISPLAY':display,
             'RELAY_DATA_DIR':tmp+'/fixture','RELAY_KEYRING':'off','PROBE_LOG':tmp+'/requests.jsonl',
             'PROBE_MARKER':tmp+'/failed','PROBE_FAILURE':failure,'QT_QPA_PLATFORM':'xcb'}
        xvfb=subprocess.Popen(['Xvfb',display,'-screen','0','1280x900x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        relay=None
        def records():
            p=home/'requests.jsonl'
            return [json.loads(s) for s in p.read_text().splitlines()] if p.exists() else []
        def commands(kind): return [r for r in records() if r.get('type')==kind]
        def x(*args): return subprocess.check_output(['xdotool',*args],env=env,text=True).strip()
        try:
            time.sleep(.4)
            with (OUT/(name+'-stderr.log')).open('w') as err:
                relay=subprocess.Popen([str(Path(os.environ.get('RELAY_TEST_BINARY', ROOT/'build/relay'))),'--workspace',str(home/'work')],env=env,stdout=err,stderr=err)
                wait_for(lambda: commands('presets'),'worker never requested presets')
                time.sleep(1)
                windows=x('search','--pid',str(relay.pid)).splitlines()
                win=max(windows,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
                x('windowfocus','--sync',win)
                for _ in range(toggles): x('key','--clearmodifiers','shift+Tab'); time.sleep(.15)
                assert not commands('configure'), 'Plan toggle eagerly started the guest'
                subprocess.run(['import','-window',win,str(OUT/(name+'-before.png'))],env=env,check=True)
                x('type','--clearmodifiers','--delay','10','explain this fixture')
                x('key','--clearmodifiers','ctrl+Return')
                if failure=='always':
                    wait_for(lambda: len(commands('configure'))>=2,'missing automatic retry')
                    time.sleep(2)
                    assert len(commands('configure'))==2, 'unbounded automatic retry'
                    assert not commands('ask'), 'ask sent before configuration'
                    subprocess.run(['import','-window',win,str(OUT/(name+'-blocked.png'))],env=env,check=True)
                    # Repair the fixture, then use the existing Restart shortcut/banner action.
                    worker=home/'fixture/backend/worker.py'
                    worker.write_text(FAKE.replace("failure=os.environ['PROBE_FAILURE']", "failure='none'"))
                    x('key','--clearmodifiers','ctrl+shift+r')
                wait_for(lambda: commands('ask'),'queued first prompt never sent')
                asks=commands('ask')
                assert len(asks)==1, asks
                assert asks[0]['effective_mode']==expected, asks
                time.sleep(1)
                subprocess.run(['import','-window',win,str(OUT/'header.png')],env=env,check=True)
                print('PASS: session_title delivered to live GUI; inspect header.png',flush=True)
        finally:
            if (home/'requests.jsonl').exists(): (OUT/'requests.jsonl').write_text((home/'requests.jsonl').read_text())
            if relay:
                relay.terminate()
                try: relay.wait(timeout=5)
                except subprocess.TimeoutExpired: relay.kill(); relay.wait()
            xvfb.terminate(); xvfb.wait()

if __name__=='__main__':
    run_case('title','none',0,'build')
