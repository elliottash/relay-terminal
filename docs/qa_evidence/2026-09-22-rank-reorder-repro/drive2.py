#!/usr/bin/env python3
"""Reproduce part 2: priorities tab rank change by mouse drag. Isolated env, seeded tier lists."""
import os, pathlib, re, subprocess as sp, tempfile, time
ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = pathlib.Path(__file__).resolve().parent
QA = pathlib.Path(tempfile.mkdtemp(prefix='relay-rank-drag-'))
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
def shot(name):
    run('import','-window','root',str(OUT/name))
    return OUT/name
def rows(png):
    """Return (sections, model_rows): y positions of class headers and of 'anthro' model rows."""
    out = sp.check_output(['tesseract', str(png), '-', 'tsv'], text=True,
                          stderr=sp.DEVNULL)
    sections, models = {}, []
    for line in out.splitlines()[1:]:
        f = line.split('\t')
        if len(f) < 12 or not f[11].strip(): continue
        left, top, text = int(f[6]), int(f[7]), f[11]
        if text in ('high', 'main', 'flash', 'local') and 300 < left < 500:
            sections.setdefault(text, top)
        if text.startswith('anthro') and 300 < left < 500:
            models.append(top)
    return sections, sorted(set(models))
def drag(x1, y1, x2, y2):
    run('xdotool','mousemove',str(x1),str(y1))
    time.sleep(0.3)
    run('xdotool','mousedown','1')
    time.sleep(0.4)
    mid = (y1 + y2)//2
    run('xdotool','mousemove',str(x1),str(mid)); time.sleep(0.4)
    run('xdotool','mousemove',str(x2),str(y2)); time.sleep(0.6)
    run('xdotool','mouseup','1')
    time.sleep(1.0)
def conf():
    p = config/'relay.conf'
    return p.read_text() if p.exists() else '(no conf)'
try:
    time.sleep(8)
    win=run('xdotool','search','--pid',str(app.pid),'--name','Relay').splitlines()[-1]
    run('xdotool','windowmove',win,'0','0','windowsize',win,'1400','1000','windowfocus',win)
    time.sleep(2)
    key('ctrl+shift+m')
    time.sleep(2)
    sections, models = rows(shot('d1-before.png'))
    print('sections:', sections, 'model row tops:', models, flush=True)
    main_y, high_y, flash_y = sections.get('main'), sections.get('high'), sections.get('flash')
    main_rows = [y for y in models if main_y and y > main_y and (not flash_y or y < flash_y)]
    high_rows = [y for y in models if high_y and y > high_y and y < main_y]
    print('high rows:', high_rows, 'main rows:', main_rows, flush=True)
    x = 450
    # Drag 1: main rank 1 -> below main rank 2 (within one section)
    if len(main_rows) >= 2:
        drag(x, main_rows[0]+8, x, main_rows[1]+18)
        shot('d2-after-within-section.png')
    # Drag 2: main rank 1 (whatever it is now) -> into the high section (across a header)
    sections, models = rows(OUT/'d2-after-within-section.png')
    main_rows = [y for y in models if main_y and y > main_y and (not flash_y or y < flash_y)]
    high_rows = [y for y in models if high_y and y > high_y and y < main_y]
    if main_rows and high_rows:
        drag(x, main_rows[0]+8, x, high_rows[-1]+18)
        shot('d3-after-cross-section.png')
    time.sleep(1)
finally:
    app.terminate(); time.sleep(2); xvfb.terminate()
print('--- relay.conf [models] after drive ---', flush=True)
for line in conf().splitlines():
    if 'tier' in line: print(line, flush=True)
