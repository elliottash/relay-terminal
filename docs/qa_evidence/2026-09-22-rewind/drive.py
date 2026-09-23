"""Live rewind and file-pane check with an isolated fake worker; no provider calls."""
import csv
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
WORKER = r'''
import json, os, sys, time
from pathlib import Path
turn = 0
prompts = []
def emit(**obj): print(json.dumps(obj), flush=True)
emit(event='ready')
for line in sys.stdin:
    r = json.loads(line)
    with open(os.environ['PROBE_LOG'], 'a') as f: f.write(json.dumps(r)+'\n')
    kind = r.get('type')
    if kind == 'shutdown': break
    if kind == 'presets':
        emit(event='presets', presets=[dict(id='guest:codex',label='codex',group='guest',harness=True,model='gpt-6-astra',models=[dict(id='gpt-6-astra',label='gpt-6-astra',name='gpt-6-astra')])])
    elif kind == 'configure':
        emit(event='configured',model='gpt-6-astra',mode='build',session_id='a'*32,session_dir=os.environ['PROBE_STORE'])
    elif kind == 'route':
        emit(event='route',id=r.get('id'),route='agent',text=r.get('text',''))
    elif kind == 'ask':
        turn += 1
        prompts.append(r['text'])
        emit(event='queued',request_id=r.get('id'),id=r.get('id'))
        emit(event='agent_started',id=r.get('id'))
        emit(event='delta',text='Retained answer before the rewind.\n' if turn == 1 else 'DISCARDED OUTPUT: this answer should move to the saved-output pane.\nSecond discarded line.\n')
        emit(event='done',id=r.get('id'))
        emit(event='agent_finished',id=r.get('id'),outcome='done')
    elif kind == 'checkpoints':
        emit(event='checkpoints',items=[dict(turn=2,prompt_preview=prompts[1],files=[],time=time.time())])
    elif kind == 'rewind':
        emit(event='rewound',turn=2,restore='conversation',prompt=prompts[1],rewound_n=1,restored_files=[],conflicts=[])
'''


def wait_for(check, message):
    for _ in range(160):
        if check(): return
        time.sleep(.1)
    raise AssertionError(message)


with tempfile.TemporaryDirectory(prefix='relay-rewind-ui-') as tmp:
    base = Path(tmp)
    for folder in ('config/RelayTerminal', 'data', 'cache', 'run', 'fixture/backend', 'work', 'store'):
        (base / folder).mkdir(parents=True)
    (base / 'run').chmod(0o700)
    (base / 'fixture/backend/worker.py').write_text(WORKER)
    for folder in ('shell', 'scripts', 'assets'):
        (base / 'fixture' / folder).symlink_to(ROOT / folder, target_is_directory=True)
    (base / 'config/RelayTerminal/relay.conf').write_text('''[instructions]
onboarded=true
[isolation]
enabled=false
[models]
tier\\main=guest:codex|gpt-6-astra|high
tier\\high=guest:codex|gpt-6-astra|high
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
[url_handler]
announced=true
''')
    display = next(':'+str(n) for n in range(450,490) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
    env = {**os.environ, 'DISPLAY': display, 'QT_QPA_PLATFORM': 'xcb',
           'XDG_CONFIG_HOME': str(base/'config'), 'XDG_DATA_HOME': str(base/'data'),
           'XDG_CACHE_HOME': str(base/'cache'), 'XDG_RUNTIME_DIR': str(base/'run'),
           'RELAY_DATA_DIR': str(base/'fixture'), 'RELAY_KEYRING': 'off',
           'PROBE_LOG': str(base/'requests.jsonl'), 'PROBE_STORE': str(base/'store')}
    xvfb = subprocess.Popen(['Xvfb',display,'-screen','0','1280x900x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    app = None
    def x(*args): return subprocess.check_output(['xdotool',*args],env=env,text=True).strip()
    def requests(kind):
        path = base/'requests.jsonl'
        return [r for line in path.read_text().splitlines() if (r:=json.loads(line)).get('type')==kind] if path.exists() else []
    try:
        time.sleep(.4)
        with (base/'stderr.log').open('w') as log:
            app = subprocess.Popen([str(ROOT/'build/relay'),'--workspace',str(base/'work')],env=env,stdout=log,stderr=log)
            wait_for(lambda: requests('presets'), 'No worker connection')
            time.sleep(1)
            windows = x('search','--pid',str(app.pid)).splitlines()
            win = max(windows,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
            x('windowfocus','--sync',win)
            for n, prompt in enumerate(('Keep this first turn','Discard this second turn'), 1):
                x('type','--clearmodifiers','--delay','15','* '+prompt)
                x('key','Return')
                wait_for(lambda: len(requests('ask'))>=n, 'Ask was not delivered')
                time.sleep(.8)
            x('type','--clearmodifiers','/rewind'); x('key','Return')
            wait_for(lambda: requests('checkpoints'), 'No rewind picker')
            time.sleep(.6); x('key','Return')
            wait_for(lambda: requests('rewind'), 'No rewind request')
            saved = base/'store'/('a'*32+'.rewound-1.scrollback.txt')
            wait_for(saved.exists, 'No saved rewind text')
            subprocess.run(['import','-window',win,str(OUT/'rewind.png')],env=env,check=True)
            print('SAVED:', saved.read_text()[:3000])
            assert 'DISCARDED OUTPUT' in saved.read_text()
            assert 'Retained answer' not in saved.read_text()
            time.sleep(.8)
            subprocess.run(['import','-window',win,str(OUT/'rewind.png')],env=env,check=True)
            tsv = subprocess.check_output(['tesseract',str(OUT/'rewind.png'),'stdout','tsv'],stderr=subprocess.DEVNULL,text=True)
            words = list(csv.DictReader(io.StringIO(tsv), delimiter='\t'))
            word = next(w for w in words if w['text'].lower() == 'rewound')
            x('mousemove','--window',win,str(int(word['left'])+int(word['width'])//2),str(int(word['top'])+int(word['height'])//2))
            x('click','1')
            time.sleep(1)
            subprocess.run(['import','-window',win,str(OUT/'viewer.png')],env=env,check=True)
            print('PASS: real rewind picker, saved branch, and mouse activation of the output link; inspect rewind.png and viewer.png')
    finally:
        if (base/'requests.jsonl').exists(): print('requests:', [(r.get('type'),r.get('text','')[:80]) for line in (base/'requests.jsonl').read_text().splitlines() if (r:=json.loads(line)).get('type') != 'route'])
        if app:
            app.terminate()
            try: app.wait(timeout=5)
            except subprocess.TimeoutExpired: app.kill(); app.wait()
        xvfb.terminate(); xvfb.wait()
