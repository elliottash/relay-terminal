"""Live Relay/Xvfb with real worker and deterministic boundary fixtures; no paid calls."""
import json, os, shutil, subprocess as sp, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent
TMP=Path(tempfile.mkdtemp(prefix='msw7-drive-'))
for p in ('config/RelayTerminal','data','cache','run','fixture/backend','work'):
    (TMP/p).mkdir(parents=True)
(TMP/'run').chmod(0o700)
shutil.copy(OUT/'fixture_worker.py',TMP/'fixture/backend/worker.py')
for name in ('scripts','shell','assets'):
    (TMP/'fixture'/name).symlink_to(ROOT/name,target_is_directory=True)
(TMP/'config/RelayTerminal/relay.conf').write_text('''[logging]
level=debug
[instructions]
onboarded=true
[isolation]
enabled=false
[models]
tier\\main=kimi|kimi-k3|high, glm-coding|glm-5.3|max, openrouter|meta/muse-spark-1.3|high
tier\\flash=glm-coding|glm-5.3-flash|high
tier\\high=guest:codex|gpt-6-astra|high, guest:codex|gpt-5.6-sol|high
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
[url_handler]
announced=true
''')
display=next(':'+str(n) for n in range(510,560) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
env={**os.environ,'XDG_CONFIG_HOME':str(TMP/'config'),'XDG_DATA_HOME':str(TMP/'data'),
     'XDG_CACHE_HOME':str(TMP/'cache'),'XDG_RUNTIME_DIR':str(TMP/'run'),'DISPLAY':display,
     'RELAY_LOG_LEVEL':'debug','QT_QPA_PLATFORM':'xcb','RELAY_KEYRING':'off','RELAY_DATA_DIR':str(TMP/'fixture'),
     'MSW_ROOT':str(ROOT),'MSW_TRANSPORT':str(TMP/'transport.jsonl'),
     'RELAY_BOARD_MCP_URL':'','RELAY_CONTEXT':'','RELAY_SESSION_TOKEN':''}
xvfb=sp.Popen(['Xvfb',display,'-screen','0','1400x1000x24'],stdout=sp.DEVNULL,stderr=sp.DEVNULL)
time.sleep(.5)
err=(TMP/'stderr').open('w')
app=sp.Popen([str(ROOT/'build/relay'),'--workspace',str(TMP/'work'),'--fresh'],env=env,stdout=err,stderr=err)
def x(*args): return sp.check_output(['xdotool',*args],env=env,text=True).strip()
def key(k): x('key','--clearmodifiers',k);time.sleep(.5)
def shot(name):
    sp.run(['import','-window','root',str(OUT/(name+'.png'))],env=env,check=True)
    text=sp.check_output(['tesseract',str(OUT/(name+'.png')),'stdout','--psm','11'],stderr=sp.DEVNULL,text=True)
    (OUT/(name+'.txt')).write_text(text)
    return text
# Keep the isolated instance available for the investigator; resume via the saved control JSON.
time.sleep(3)
wins=x('search','--pid',str(app.pid)).splitlines()
win=max(wins,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
x('windowmove',win,'0','0','windowsize',win,'1400','1000','windowfocus',win)
time.sleep(1)
shot('01-new-pane')
key('alt+m');shot('02-initial-picker')
(TMP/'control.json').write_text(json.dumps({'env':env,'win':win,'app':app.pid,'xvfb':xvfb.pid}))
print(TMP,flush=True)

if '--interactive' not in __import__('sys').argv:
    (TMP/'command-1.json').write_text(json.dumps([['key', 'Up'], ['key', 'Up'], ['key', 'Return'], ['wait', 1], ['shot', '03-high-codex'], ['type', 'Reply with a short greeting'], ['key', 'ctrl+Return'], ['wait', 2], ['shot', '04-high-answer'], ['key', 'alt+m'], ['shot', '05-high-picker'], ['key', 'Down'], ['key', 'Down'], ['key', 'Down'], ['key', 'Down'], ['key', 'Return'], ['wait', 1], ['shot', '06-flash'], ['type', 'Reply on Flash'], ['key', 'ctrl+Return'], ['wait', 2], ['key', 'alt+m'], ['key', 'Up'], ['key', 'Up'], ['key', 'Return'], ['wait', 1], ['shot', '07-main-kimi'], ['type', 'Reply on Kimi'], ['key', 'ctrl+Return'], ['wait', 2], ['shot', '08-main-answer'], ['key', 'alt+m'], ['key', 'Up'], ['key', 'Return'], ['wait', 1], ['shot', '09-refused-high'], ['key', 'alt+m'], ['shot', '10-refused-picker'], ['key', 'Escape'], ['type', 'Reply after the refused pick'], ['key', 'ctrl+Return'], ['wait', 2], ['shot', '11-still-kimi'], ['key', 'alt+m'], ['key', 'Down'], ['key', 'Return'], ['wait', 1], ['type', 'Exercise quota and fallback'], ['key', 'ctrl+Return'], ['wait', 2], ['shot', '12-quota-fallback'], ['key', 'ctrl+t'], ['wait', 2], ['shot', '13-second-pane'], ['key', 'alt+m'], ['shot', '14-second-pane-picker']]))

try:
    last = 0
    while not (TMP/'stop').exists():
        for command in sorted(TMP.glob('command-*.json')):
            number = int(command.stem.split('-')[1])
            if number <= last: continue
            for action in json.loads(command.read_text()):
                if action[0] == 'key': key(action[1])
                elif action[0] == 'type': x('type','--clearmodifiers','--delay','10',action[1])
                elif action[0] == 'click': x('mousemove',str(action[1]),str(action[2]));x('click','1');time.sleep(.5)
                elif action[0] == 'wait': time.sleep(action[1])
                elif action[0] == 'shot': shot(action[1])
            last = number
            print('completed', number, flush=True)
            if '--interactive' not in __import__('sys').argv: (TMP/'stop').touch()
        time.sleep(.2)
finally:
    app.terminate()
    try: app.wait(timeout=5)
    except sp.TimeoutExpired: app.kill();app.wait()
    xvfb.terminate();xvfb.wait()
    for name in ('relay.log','worker.log'):
        source=TMP/'data/relay/logs'/name
        if source.exists(): shutil.copy(source,OUT/name)
    if (TMP/'transport.jsonl').exists(): shutil.copy(TMP/'transport.jsonl',OUT/'transport.jsonl')

if '--interactive' not in __import__('sys').argv:
    gui=(OUT/'relay.log').read_text()
    worker=(OUT/'worker.log').read_text()
    transport=[json.loads(line) for line in (OUT/'transport.jsonl').read_text().splitlines()]
    assert '"display":"Loading models…"' in gui
    assert 'model_switch_refused' in gui
    assert 'Base URL must' not in worker
    assert 'provider_http_retry' not in worker
    assert 'Resets at 2026-09-23 05:53:09' in worker and 'HTTP 401' in worker
    assert sum(e.get('model')=='glm-5.3' and e['action']=='http' for e in transport)==1
    assert sum(e.get('model')=='meta/muse-spark-1.3' and e['action']=='http' for e in transport)==1
    changes=[line for line in gui.splitlines() if 'type=model_changed ' in line]
    assert len(changes)==4, changes
    for line,model in zip(changes,['gpt-6-astra','glm-5.3-flash','kimi-k3','glm-5.3']):
        assert 'model='+model in line,line
    assert len([line for line in gui.splitlines() if '"reason":"initial"' in line])==2
    (OUT/'drive-result.txt').write_text('PASS: startup and second pane; High guest; Flash; Main; rejected High preserves Kimi; one quota request; separate fallback error; exactly four successful model acknowledgments.\n')
    print((OUT/'drive-result.txt').read_text(),flush=True)
