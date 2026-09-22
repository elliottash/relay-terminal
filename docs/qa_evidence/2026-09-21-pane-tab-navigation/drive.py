#!/usr/bin/env python3
"""Isolated live GUI evidence for #PNAV; run from repository root."""
import hashlib, json, os, pathlib, subprocess as sp, tempfile, time
ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = pathlib.Path(__file__).resolve().parent
QA = pathlib.Path(tempfile.mkdtemp(prefix='relay-pnav-'))
print(QA, flush=True)
env = dict(os.environ, HOME=str(QA/'home'), XDG_CONFIG_HOME=str(QA/'config'),
           XDG_DATA_HOME=str(QA/'data'), XDG_CACHE_HOME=str(QA/'cache'),
           XDG_RUNTIME_DIR=str(QA/'runtime'), TMPDIR=str(QA/'tmp'), RELAY_KEYRING='off')
for key in ('HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR','TMPDIR'):
    pathlib.Path(env[key]).mkdir(parents=True, exist_ok=True)
os.chmod(env['XDG_RUNTIME_DIR'], 0o700)
(pathlib.Path(env['XDG_RUNTIME_DIR'])/'bus').symlink_to(f'/run/user/{os.getuid()}/bus')
config = QA/'config/RelayTerminal'; config.mkdir()
(config/'relay.conf').write_text('[instructions]\nonboarded=true\n')
project = QA/'home/widgetworks'; project.mkdir()
loose = QA/'home/Downloads'; loose.mkdir()
state = QA/'data/relay/state'; state.mkdir(parents=True)
(state/'projects.json').write_text(json.dumps({'version':1,'saved':int(time.time()),'projects':[
    {'path':str(project),'key':hashlib.sha256(str(project).encode()).hexdigest()[:16],
     'name':'widgetworks','board':'none','board_dir':'','reason':'picker','known_since':int(time.time())-86400,
     'last_attached':int(time.time())-3600}], 'declined':[]}))
display = next(f':{n}' for n in range(110,150) if not pathlib.Path(f'/tmp/.X11-unix/X{n}').exists())
env['DISPLAY']=display
(QA/'env.json').write_text(json.dumps(env))
xvfb=sp.Popen(['Xvfb',display,'-screen','0','1400x1000x24'],stdout=sp.DEVNULL,stderr=sp.DEVNULL)
time.sleep(1)
log=open(QA/'relay.log','w')
app=sp.Popen([os.environ.get('RELAY_QA_BINARY',str(ROOT/'build/relay')),'--workspace',str(loose),'--clean-shell','--fresh'],env=env,stdout=log,stderr=log)
def run(*args): return sp.check_output(args,env=env,text=True).strip()
def key(chord): run('xdotool','key','--clearmodifiers',chord); time.sleep(1.5)
def shot(name): run('import','-window','root',str(OUT/name))
try:
    time.sleep(8)
    win=run('xdotool','search','--pid',str(app.pid),'--name','Relay').splitlines()[-1]
    run('xdotool','windowmove',win,'0','0','windowsize',win,'1400','1000','windowfocus',win)
    time.sleep(2)
    key('ctrl+shift+p')
    key('Tab'); shot('01-forward.png')
    key('shift+Tab'); shot('02-reverse.png')
    key('shift+Tab'); shot('03-wrap-globals.png')
    key('Tab'); shot('04-wrap-projects.png')
    key('ctrl+shift+g')
    run('xdotool','mousemove','1000','650','click','1')
    key('Tab'); key('shift+Tab'); shot('05-editor-preserved.png')
    key('ctrl+shift+m'); key('Tab'); shot('06-models-forward.png')
    key('alt+3'); shot('07-alt-reserved.png')
    run('xdotool','mousemove','300','925','click','1')
    key('shift+Tab'); shot('08-console-plan.png')
    print('Keyboard sequence complete; inspect captures for selected tabs and Plan.', flush=True)
finally:
    app.terminate(); xvfb.terminate()
