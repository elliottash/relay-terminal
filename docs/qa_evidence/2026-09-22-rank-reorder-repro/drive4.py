#!/usr/bin/env python3
"""Live evidence for #RKP3: ▼ button click and a cross-section drag, persisted to relay.conf."""
import os, pathlib, subprocess as sp, tempfile, time
ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = pathlib.Path(__file__).resolve().parent
QA = pathlib.Path(tempfile.mkdtemp(prefix='relay-rkp3-'))
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
def grid(png):
    """Row tops of the priorities list: the grid is 20px a row, anchored at the first row (the
    high section's rank 1). OCR of the 'unavailable' cells is only a sanity check — it drops the
    selected row and misreads bands, which once produced a 33px phantom grid."""
    out = sp.check_output(['tesseract', str(png), '-', 'tsv'], text=True, stderr=sp.DEVNULL)
    tops = set()
    for line in out.splitlines()[1:]:
        f = line.split('\t')
        if len(f) < 12: continue
        if f[11].strip() == 'unavailable' and 560 < int(f[6]) < 640:
            tops.add(int(f[7]))
    anchor = min(tops) if tops else 247
    # h1, h2, (main header), m1, m2, m3 — the header takes one grid slot.
    return [anchor + 20*k for k in range(8)]
def drag(x1, y1, x2, y2):
    run('xdotool','mousemove',str(x1),str(y1)); time.sleep(0.3)
    run('xdotool','mousedown','1'); time.sleep(0.4)
    run('xdotool','mousemove',str(x1),str((y1+y2)//2)); time.sleep(0.4)
    run('xdotool','mousemove',str(x2),str(y2)); time.sleep(0.6)
    run('xdotool','mouseup','1'); time.sleep(1.0)
def conf():
    p = config/'relay.conf'
    return p.read_text() if p.exists() else '(no conf)'
# Seeded: high=[h1,h2], main=[m1,m2,m3]. Row grid: h1 h2 (main head) m1 m2 m3.
try:
    time.sleep(8)
    win=run('xdotool','search','--pid',str(app.pid),'--name','Relay').splitlines()[-1]
    run('xdotool','windowmove',win,'0','0','windowsize',win,'1400','1000','windowfocus',win)
    time.sleep(2)
    key('ctrl+shift+m')
    time.sleep(2)
    g = grid(shot('r1-priorities.png'))
    print('row grid:', g, flush=True)
    h2, m1 = g[1], g[3]
    # 1. ▼ on m1's row: the rank column ends ~x330, then ▲ (~334-352) and ▼ (~352-370); click the
    #    middle of ▼. Expect main = m2, m1, m3.
    run('xdotool','mousemove','358',str(m1+9),'click','1'); time.sleep(1.5)
    g2 = grid(shot('r2-after-down-button.png'))
    # 2. Cross-section drag: m1, now main's rank 2 (one grid step below its old row), onto the
    #    lower half of h2's row — dropped after h2, at the end of high.
    #    Expect high = h1, h2, m1 · main = m2, m3.
    drag(450, m1+20+9, 450, h2+13)
    shot('r3-after-cross-drag.png')
    time.sleep(1)
finally:
    app.terminate(); time.sleep(2); xvfb.terminate()
print('--- relay.conf [models] after drive ---', flush=True)
for line in conf().splitlines():
    if 'tier' in line: print(line, flush=True)
