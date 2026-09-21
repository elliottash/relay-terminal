"""Drive the real meter under Xvfb with deterministic worker events; no model calls."""
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
def context(guest, reading):
    emit(event='context', window=128000, used_tokens=64000, percent=50,
         limit_tokens=71232, estimated=True, guest=guest, guest_context=reading)
emit(event='ready')
for line in sys.stdin:
    r=json.loads(line); kind=r.get('type')
    with open(os.environ['PROBE_LOG'],'a') as f: f.write(json.dumps(r)+'\n')
    if kind=='presets':
        emit(event='presets',presets=[dict(id='guest:codex',label='Codex',harness=True,
             model='gpt-6-astra', models=[dict(id='gpt-6-astra',name='gpt-6-astra',label='gpt-6-astra')])])
    elif kind=='configure':
        emit(event='configured',model='gpt-6-astra',preset='guest:codex',guest='codex',
             guest_session='fixture',session_id='fixture',mode='build',agent_role='main',effort='high')
        context('codex',{})
    elif kind=='route': emit(event='route',id=r.get('id'),route='agent',text=r.get('text',''))
    elif kind=='ask':
        emit(event='started',id=r.get('id'))
        emit(event='delta',text='Context meter fixture.\n')
        emit(event='done',id=r.get('id'))
        guest=os.environ['PROBE_CASE']
        if guest=='unknown': context('claude',{})
        else:
            window=258400 if guest=='codex' else 200000
            context(guest,dict(window=window,used_tokens=window//10,percent=10))
    elif kind=='context':
        guest=os.environ['PROBE_CASE']
        context('claude' if guest=='unknown' else guest, {} if guest=='unknown' else
                dict(window=258400 if guest=='codex' else 200000,
                     used_tokens=25840 if guest=='codex' else 20000,percent=10))
'''

def run(case):
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
             'PROBE_CASE':case,'QT_QPA_PLATFORM':'xcb'}
        xvfb=subprocess.Popen(['Xvfb',display,'-screen','0','1280x900x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        relay=None
        def x(*args): return subprocess.check_output(['xdotool',*args],env=env,text=True).strip()
        try:
            time.sleep(.4)
            with open(tmp+'/stderr','w') as err:
                relay=subprocess.Popen([os.environ.get('RELAY_TEST_BINARY',str(ROOT/'build/relay')),
                                        '--workspace',str(home/'work')],env=env,stdout=err,stderr=err)
                time.sleep(2)
                windows=x('search','--pid',str(relay.pid)).splitlines()
                win=max(windows,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
                x('windowfocus','--sync',win)
                x('mousemove','--window',win,'350','735');x('click','1')
                x('type','--clearmodifiers','--delay','10','explain this fixture')
                x('key','--clearmodifiers','ctrl+Return')
                time.sleep(2)
                # /context prints the very same selected reading and compaction ownership.
                x('type','--clearmodifiers','--delay','30','/context')
                x('key','--clearmodifiers','Return')
                time.sleep(1)
                subprocess.run(['import','-window',win,str(OUT/(case+'.png'))],env=env,check=True)
                text=subprocess.check_output(['tesseract',str(OUT/(case+'.png')),'stdout'],stderr=subprocess.DEVNULL,text=True)
                expected='context unknown' if case=='unknown' else '90% left'
                assert expected.lower().replace(' ', '') in text.lower().replace(' ', ''), (case,text)
                if case!='unknown':
                    assert ('258' if case=='codex' else '200') in text, text
                assert 'manages compaction' in text.lower(), text
                return case+': PASS\n'+text
        finally:
            if relay:
                relay.terminate()
                try: relay.wait(timeout=5)
                except subprocess.TimeoutExpired: relay.kill();relay.wait()
            xvfb.terminate();xvfb.wait()

if __name__=='__main__':
    results=[]
    for case in ('codex','claude','unknown'):
        results.append(run(case));print(case+': PASS',flush=True)
    (OUT/'gui.txt').write_text('\n'.join(results))
