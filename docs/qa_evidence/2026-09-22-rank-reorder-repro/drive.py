#!/usr/bin/env python3
"""Reproduce: priorities tab rank change (alt+down). Isolated env, seeded tier lists."""
import os, pathlib, subprocess as sp, tempfile, time
ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = pathlib.Path(__file__).resolve().parent
QA = pathlib.Path(tempfile.mkdtemp(prefix='relay-rank-'))
print(QA, flush=True)
env = dict(os.environ, HOME=str(QA/'home'), XDG_CONFIG_HOME=str(QA/'config'),
           XDG_DATA_HOME=str(QA/'data'), XDG_CACHE_HOME=str(QA/'cache'),
           XDG_RUNTIME_DIR=str(QA/'runtime'), TMPDIR=str(QA/'tmp'), RELAY_KEYRING='off')
for key in ('HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR','TMPDIR'):
    pathlib.Path(env[key]).mkdir(parents=True, exist_ok=True)
os.chmod(env['XDG_RUNTIME_DIR'], 0o700)
(pathlib.Path(env['XDG_RUNTIME_DIR'])/'bus').symlink_to(f'/run/user/{os.getuid()}/bus')
config = QA/'config/RelayTerminal'; config.mkdir()
(config/'relay.conf').write_text(
    '[instructions]\nonboarded=true\n\n'
    '[models]\n'
    'tier\\high=anthropic|claude-h1|, anthropic|claude-h2|\n'
    'tier\\main=anthropic|claude-m1|, anthropic|claude-m2|, anthropic|claude-m3|\n')
loose = QA/'home/Downloads'; loose.mkdir(parents=True)
display = next(f':{n}' for n in range(150,190) if not pathlib.Path(f'/tmp/.X11-unix/X{n}').exists())
env['DISPLAY']=display
xvfb=sp.Popen(['Xvfb',display,'-screen','0','1400x1000x24'],stdout=sp.DEVNULL,stderr=sp.DEVNULL)
time.sleep(1)
log=open(QA/'relay.log','w')
app=sp.Popen([str(ROOT/'build/relay'),'--workspace',str(loose),'--clean-shell','--fresh'],
             env=env,stdout=log,stderr=log)
def run(*args): return sp.check_output(args,env=env,text=True).strip()
def key(chord): run('xdotool','key','--clearmodifiers',chord); time.sleep(1.2)
def shot(name): run('import','-window','root',str(OUT/name))
def conf():
    p = config/'relay.conf'
    return p.read_text() if p.exists() else '(no conf)'
try:
    time.sleep(8)
    win=run('xdotool','search','--pid',str(app.pid),'--name','Relay').splitlines()[-1]
    run('xdotool','windowmove',win,'0','0','windowsize',win,'1400','1000','windowfocus',win)
    time.sleep(2)
    key('ctrl+shift+m')          # models pane, opens on priorities
    time.sleep(2)
    shot('01-priorities.png')
    key('alt+Down')              # move highlighted row down one rank
    shot('02-after-altdown.png')
    key('alt+Down')
    shot('03-after-altdown2.png')
    key('alt+Up')
    shot('04-after-altup.png')
    time.sleep(1)
finally:
    app.terminate(); time.sleep(2); xvfb.terminate()
print('--- relay.conf [models] after drive ---', flush=True)
for line in conf().splitlines():
    if 'tier' in line: print(line, flush=True)
