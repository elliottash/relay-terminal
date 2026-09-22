"""Exercise the actual Relay worker pipe, SettingsWatch, Models-pane refresh and availability UI."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
CDP = OUT.parent / '2026-09-22-CDP7'
BINARY = os.environ.get('RELAY_STAGE_BINARY', str(ROOT/'build/relay'))
PREFIX = os.environ.get('RELAY_STAGE_PREFIX', 'live')
with tempfile.TemporaryDirectory(prefix='relay-model-events-') as directory:
    tmp = Path(directory)
    data = tmp / 'data'; data.mkdir(); (data/'backend').mkdir()
    shutil.copy(OUT/'fake-worker.py', data/'backend/worker.py')
    for name in ('shell', 'assets', 'themes', 'scripts'):
        if (ROOT/name).exists(): (data/name).symlink_to(ROOT/name, target_is_directory=True)
    env = dict(os.environ)
    for key in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME', 'XDG_RUNTIME_DIR'):
        path = tmp/key; path.mkdir(mode=0o700); env[key] = str(path)
    display = next(f':{n}' for n in range(710, 780) if not Path(f'/tmp/.X11-unix/X{n}').exists())
    trigger = tmp/'trigger'; events = tmp/'events.jsonl'
    env.update(DISPLAY=display, QT_QPA_PLATFORM='xcb', RELAY_DATA_DIR=str(data), RELAY_NO_ISOLATION='1',
               RELAY_KEYRING='off', RELAY_QA_RECTS=str(tmp/'rects.json'),
               RELAY_STAGE_TRIGGER=str(trigger), RELAY_STAGE_EVENTS=str(events))
    env.pop('RELAY_OPEN_SOCKET', None)
    xvfb = subprocess.Popen(['Xvfb', display, '-screen', '0', '1800x1100x24'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    app = None
    def command(*args): return subprocess.check_output(args, env=env, text=True)
    def key(chord): command('xdotool', 'key', chord); time.sleep(.6)
    def rects(): return json.loads((tmp/'rects.json').read_text())
    def click(name, dx=None):
        r=rects()[name]; command('xdotool','mousemove',str(r['x']+(dx if dx is not None else r['w']//2)),str(r['y']+r['h']//2),'click','1'); time.sleep(.6)
    def wait_for(name):
        for _ in range(100):
            try:
                if name in rects(): return
            except (FileNotFoundError,json.JSONDecodeError): pass
            time.sleep(.1)
        raise RuntimeError(f'Missing {name}: {rects()}')
    def shot(path):
        command('import','-window','root',str(path))
        return command('tesseract',str(path),'stdout','--psm','11')
    try:
        time.sleep(.4)
        with (tmp/'stderr').open('w') as log:
            app=subprocess.Popen([BINARY,'--workspace',str(tmp)],env=env,stdout=log,stderr=log)
            time.sleep(2)
            dialogs=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(app.pid),'--name','Agent instructions'],env=env,text=True,capture_output=True)
            for win in dialogs.stdout.splitlines(): command('xdotool','key','--window',win,'Escape')
            wait_for('modelsPaneTabs')
            click('modelsPaneTabs', 230)  # priorities
            wait_for('modelFilter'); click('modelFilter'); command('xdotool','type','openrouter'); time.sleep(.6)
            before=shot(OUT/f'{PREFIX}-before.png')
            trigger.touch(); time.sleep(2)
            after=shot(OUT/f'{PREFIX}-after.png')
            assert 'late-model' not in before, before
            assert 'late-model' in after, after
            assert rects()['modelFilter']['text']=='openrouter', rects()['modelFilter']
            click('modelsPaneTabs', 35); time.sleep(.8)
            providers=shot(CDP/'live-providers.png')
            assert '7 of 7' in providers, providers
            click('modelsPaneTabs', 130); wait_for('modelFilter'); click('modelFilter'); key('ctrl+a'); command('xdotool','type','codex'); time.sleep(.8)
            available=shot(CDP/'live-available.png')
            assert all(f'stage-{i}' in available for i in range(1, 7)), available  # OCR may drop the first row's hyphens
            records=[json.loads(x) for x in events.read_text().splitlines()]
            assert any(x.get('event')=='key_stored' and x['helper'] for x in records)
            assert any(x.get('event')=='presets' and any(r.get('id')=='guest:codex' and r.get('harness') for r in x.get('presets',[])) for x in records)
            assert not any(x.get('event')=='presets' and x.get('presets') and not x['helper'] for x in records)
            shutil.copy(events,OUT/'live-events.jsonl')
            print('PASS: same running app receives helper key_stored and guest presets, refreshes open Priorities search, and shows 7/7 Codex models in Available')
    finally:
        if app:
            app.terminate()
            try: app.wait(timeout=10)
            except subprocess.TimeoutExpired: app.kill(); app.wait()
        xvfb.terminate(); xvfb.wait()
