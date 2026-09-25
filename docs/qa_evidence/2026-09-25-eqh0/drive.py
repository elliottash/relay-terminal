"""Card #EQH0 live check: Models › Sources shows each login's email and a "usage · refresh" row;
clicking refresh sends `usage_refresh` and the Codex row's usage redraws. Run under Xvfb:
  xvfb-run -a -s "-screen 0 1600x1100x24" python3 drive.py <out-dir>
RELAY_BIN names the binary (the land.py verify-slot build of the landed tree)."""
import json, os, pathlib, shutil, subprocess, sys, tempfile, time

BIN = os.environ['RELAY_BIN']
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = pathlib.Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)

p = pathlib.Path(tempfile.mkdtemp(prefix='relay-eqh0-'))
for name in ['config/RelayTerminal', 'data/backend', 'runtime', 'workspace/issues', 'cache', 'share']:
    (p / name).mkdir(parents=True)
(p / 'runtime').chmod(0o700)
(p / 'config/RelayTerminal/relay.conf').write_text('[instructions]\nonboarded=true\n[theme]\nname=relay-dark\n')
(p / 'workspace/issues/board.yaml').write_text('tabs: [{id: features, folder: features}]\ncolumns: [inbox, done]\n')
shutil.copy(str(HERE / 'worker.py'), str(p / 'data/backend/worker.py'))
for name in ['shell', 'themes']:
    if (ROOT / name).exists():
        (p / 'data' / name).symlink_to(ROOT / name, target_is_directory=True)
fixture_log = p / 'requests.log'
e = os.environ.copy()
e.update(XDG_CONFIG_HOME=str(p / 'config'), XDG_DATA_HOME=str(p / 'share'),
         XDG_CACHE_HOME=str(p / 'cache'), XDG_RUNTIME_DIR=str(p / 'runtime'),
         RELAY_DATA_DIR=str(p / 'data'), RELAY_QA_RECTS=str(p / 'rects.json'), RELAY_KEYRING='off',
         RELAY_FIXTURE_LOG=str(fixture_log))

def run(*cmd):
    return subprocess.check_output(cmd, env=e, text=True).strip()

def shot(name):
    path = OUT / name
    run('import', '-window', 'root', str(path))
    return subprocess.check_output(['tesseract', str(path), 'stdout', '--psm', '11'], text=True)

log = open(p / 'relay.log', 'w')
app = subprocess.Popen([BIN, '--fresh', '--workspace', str(p / 'workspace')], env=e, stdout=log, stderr=log)
try:
    time.sleep(6)
    win = run('xdotool', 'search', '--onlyvisible', '--pid', str(app.pid)).splitlines()[0]
    run('xdotool', 'windowmove', win, '0', '0')
    run('xdotool', 'windowsize', win, '1500', '1050')
    run('xdotool', 'windowfocus', win)
    run('xdotool', 'key', 'ctrl+shift+m')
    time.sleep(6)
    rects = json.loads((p / 'rects.json').read_text()) if (p / 'rects.json').exists() else {}
    # Sources is the first drawn tab caption (not a widget); find it by OCR box if the rects lack it.
    tabs = [v for v in rects.values() if v.get('text') == 'Sources']
    if tabs:
        t = tabs[0]; run('xdotool', 'mousemove', str(t['x'] + t['w'] // 2), str(t['y'] + t['h'] // 2), 'click', '1')
    else:
        # The captions are drawn, not widgets: Sources is the first, at (797, 134) with the
        # window at the origin and 1500 px wide (seen in the first screenshot of this drive).
        run('xdotool', 'mousemove', '797', '134', 'click', '1')
    time.sleep(3)
    before = shot('1-sources-before.png')
    rects = json.loads((p / 'rects.json').read_text()) if (p / 'rects.json').exists() else {}
    # The row's button is drawn by the settings list, so find it by OCR: tesseract's box for the
    # word "refresh" on the right-hand side of the Sources page.
    boxes = subprocess.check_output(['tesseract', str(OUT / '1-sources-before.png'), 'stdout', '--psm', '11', 'tsv'],
                                    text=True).splitlines()
    hits = [row.split('\t') for row in boxes[1:] if row.split('\t')[-1:] == ['refresh']]
    hits = [h for h in hits if int(h[6]) > 1200]
    print('refresh buttons found:', len(hits))
    if hits:
        x, y, w, h = (int(v) for v in hits[0][6:10])
        run('xdotool', 'mousemove', str(x + w // 2), str(y + h // 2), 'click', '1')
        time.sleep(3)
    after = shot('2-sources-after-refresh.png')
    requests = fixture_log.read_text().split() if fixture_log.exists() else []
    report = {'emails_before': [m for m in ('elliott.ash@gess.ethz.ch', 'e@elliottash.com', 'ashe@ethz.ch') if m in before],
              'refresh_row_before': 'refresh' in before,
              'usage_refresh_sent': 'usage_refresh' in requests,
              'codex_left_before': [l for l in before.splitlines() if '% left' in l or 'left' in l][:4],
              'codex_left_after': [l for l in after.splitlines() if '% left' in l or 'left' in l][:4]}
    (OUT / 'result.json').write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))
finally:
    app.terminate()
    try:
        app.wait(timeout=5)
    except subprocess.TimeoutExpired:
        app.kill()
    log.close()
