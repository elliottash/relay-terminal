#!/usr/bin/env python3
"""Isolated live GUI evidence for #P7SJ / #Y2MP; run from repository root."""
import hashlib, json, os, pathlib, subprocess as sp, tempfile, time
ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = pathlib.Path(__file__).resolve().parent
QA = pathlib.Path(tempfile.mkdtemp(prefix='relay-p7sj-'))
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
    key('ctrl+shift+p'); shot('01-projects.png')
    key('ctrl+shift+y'); shot('02-sessions.png')
    key('ctrl+shift+g'); time.sleep(3); shot('03-globals.png')
    run('xdotool','mousemove','770','205','click','1')
    time.sleep(.3)
    memory = '---\ntype: memory\nstatus: active\nname: qa-global-memory\nscope: user\npinned: true\npaths: []\n---\n# QA global memory\n\nUse the widgetworks vocabulary.\n'
    run('xdotool','key','ctrl+a')
    # xdotool type does not reliably translate newlines for Qt; send Return explicitly.
    for line in memory.splitlines():
        if line: run('xdotool','type','--clearmodifiers','--delay','1','--',line)
        run('xdotool','key','Return')
    run('xdotool','mousemove','740','900','click','1'); time.sleep(2)
    shot('04-memory-saved.png')
    files = list((QA/'config/relay/switchboard/memory').glob('*.md'))
    assert len(files) == 1, files
    assert 'Use the widgetworks vocabulary.' in files[0].read_text()
    run('xdotool','mousemove','1000','700','click','1'); run('xdotool','key','ctrl+End')
    run('xdotool','type','--delay','1','Unsaved QA draft')
    key('ctrl+shift+p'); key('ctrl+shift+g'); shot('05-draft-restored.png')
    assert 'Unsaved QA draft' not in files[0].read_text()
    env['PYTHONPATH'] = str(ROOT/'backend')
    loaded = run('python3','-c', 'from relay_core.memories import prompt_section; print(prompt_section('+repr(str(loose))+'))')
    assert 'Use the widgetworks vocabulary.' in loaded, loaded
    # Close the original terminal while the manager survives as the tab's only leaf.
    run('xdotool','mousemove','674','60','click','1'); time.sleep(2)
    key('ctrl+shift+p'); key('ctrl+shift+g'); shot('06-owner-closed.png')
    # Globals must still talk to the current tab's helper and persist the preserved draft.
    run('xdotool','mousemove','40','900','click','1'); time.sleep(2)
    assert 'Unsaved QA draft' in files[0].read_text(), 'ownerless Globals Save did not persist'
    print('PASS: memory persisted and loaded into runtime context; draft survived tab switches; ownerless Globals saved.', flush=True)

finally:
    app.terminate(); xvfb.terminate()
