"""Exercise Alt+I open/close/reopen with real keys under isolated Xvfb; no model calls."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
FAKE = r'''
import json, os, sys
from pathlib import Path
def emit(**data): print(json.dumps(data), flush=True)
emit(event='ready')
for line in sys.stdin:
    r=json.loads(line); kind=r.get('type')
    with open(os.environ['PROBE_LOG'],'a') as f: f.write(json.dumps(r)+'\n')
    if kind=='presets':
        emit(event='presets',presets=[dict(id='guest:codex',label='Codex',harness=True,
             model='gpt-6-astra', models=[dict(id='gpt-6-astra',name='gpt-6-astra',label='gpt-6-astra')])])
    elif kind=='route': emit(event='route',id=r.get('id'),route='agent',text=r.get('text',''))
    elif kind=='ask':
        emit(event='started',id=r.get('id')); emit(event='done',id=r.get('id'))
    elif kind=='configure':
        emit(event='configured',model='gpt-6-astra',preset='guest:codex',guest='codex',
             guest_session='fixture',session_id='fixture',mode='build',agent_role='main',effort='high')
'''

def run():
    with tempfile.TemporaryDirectory(prefix='relay-context-') as tmp:
        home=Path(tmp)
        for folder in ('config/RelayTerminal','data','cache','run','fixture/backend','work'):
            (home/folder).mkdir(parents=True)
        (home/'run').chmod(0o700)
        (home/'fixture/backend/worker.py').write_text(FAKE)
        for folder in ('shell','scripts','assets'):
            (home/'fixture'/folder).symlink_to(ROOT/folder,target_is_directory=True)
        (home/'config/RelayTerminal/relay.conf').write_text('''[instructions]
onboarded=true
[isolation]
enabled=false
[models]
tier\\main=guest:codex|gpt-6-astra|high
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
             'RELAY_DATA_DIR':tmp+'/fixture','RELAY_BOARD_MCP_URL':'','RELAY_CONTEXT':'','RELAY_SESSION_TOKEN':'','RELAY_KEYRING':'off','PROBE_LOG':tmp+'/requests.jsonl',
             'QT_QPA_PLATFORM':'xcb'}
        xvfb=subprocess.Popen(['Xvfb',display,'-screen','0','1280x900x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        relay=None
        def x(*args): return subprocess.check_output(['xdotool',*args],env=env,text=True).strip()
        try:
            time.sleep(.4)
            with open(tmp+'/stderr','w') as err:
                relay=subprocess.Popen([os.environ.get('RELAY_TEST_BINARY',str(ROOT/'build/relay')),
                                        '--workspace',str(home/'work')],env=env,stdout=err,stderr=err)
                time.sleep(5)
                windows=x('search','--pid',str(relay.pid)).splitlines()
                win=max(windows,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
                x('windowfocus','--sync',win)
                def key(chord):
                    x('key','--clearmodifiers',chord)
                    time.sleep(.6)
                def visible(name, expected):
                    image=OUT/(name+'.png')
                    subprocess.run(['import','-window',win,str(image)],env=env,check=True)
                    text=subprocess.check_output(['tesseract',str(image),'stdout','--psm','11'],stderr=subprocess.DEVNULL,text=True)
                    (OUT/(name+'.txt')).write_text(text)
                    assert ('Esc closes' in text) == expected, (name,text)
                x('type','--clearmodifiers','--delay','15','fixture')
                key('ctrl+Return');time.sleep(1)
                key('alt+i');visible('01-open',True)
                key('alt+i');visible('02-closed',False)
                x('type','--clearmodifiers','--delay','15','focus-returned')
                visible('03-owner-focus',False)
                assert 'focus-returned' in (OUT/'03-owner-focus.txt').read_text()
                key('ctrl+a');key('BackSpace')
                key('alt+i');visible('04-reopened',True)
                key('Escape');visible('05-escape',False)
                key('alt+i');visible('06-open-again',True)
                key('alt+i');visible('07-closed-again',False)
                return 'PASS: Alt+I opens, closes, restores composer focus, reopens; Escape still closes; repeated toggle succeeds.'
        finally:
            for name in ("stderr", "requests.jsonl"):
                if (home/name).exists(): (OUT/name).write_text((home/name).read_text())
            if relay:
                relay.terminate()
                try: relay.wait(timeout=5)
                except subprocess.TimeoutExpired: relay.kill();relay.wait()
            xvfb.terminate();xvfb.wait()

if __name__=='__main__':
    print(run(),flush=True)
