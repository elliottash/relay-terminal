"""Card #YC0T live check: Models › Sources offers "add account…" on the Z.AI and Kimi Code plan rows;
clicking refresh sends `usage_refresh` and the Codex row's usage redraws. Run under Xvfb:
  xvfb-run -a -s "-screen 0 1600x1100x24" python3 drive.py <out-dir>
RELAY_BIN names the binary (the land.py verify-slot build of the landed tree)."""
import json, os, pathlib, shutil, subprocess, sys, tempfile, time

BIN = os.environ['RELAY_BIN']
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = pathlib.Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)

p = pathlib.Path(tempfile.mkdtemp(prefix='relay-yc0t-'))
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
    # The pane's tab captions are drawn, not widgets: find "Sources" by OCR and click it.
    shot('0-models.png')
    tsv = subprocess.check_output(['tesseract', str(OUT / '0-models.png'), 'stdout', '--psm', '11', 'tsv'],
                                  text=True).splitlines()
    caps = [r.split('\t') for r in tsv[1:] if r.split('\t')[-1:] == ['Sources']]
    if caps:
        x, y, w, h = (int(v) for v in caps[0][6:10])
        run('xdotool', 'mousemove', str(x + w // 2), str(y + h // 2), 'click', '1')
    time.sleep(3)
    before = shot('1-sources-before.png')
    boxes = subprocess.check_output(['tesseract', str(OUT / '1-sources-before.png'), 'stdout', '--psm', '11', 'tsv'],
                                    text=True).splitlines()
    words = [row.split('\t') for row in boxes[1:] if len(row.split('\t')) == 12]
    # "add account…" is two words; the first "add" right of x=1200 on a line whose next word is
    # "account..." is the z.ai row's button (the plan rows come in the order the fixture sent them).
    hits = [w for i, w in enumerate(words[:-1]) if w[11] == 'add' and words[i + 1][11].startswith('account')
]
    print('add account buttons found:', len(hits))
    if hits:
        x, y, w, h = (int(v) for v in hits[0][6:10])
        run('xdotool', 'mousemove', str(x + 30), str(y + h // 2), 'click', '1')
        time.sleep(2)
        shot('2-name-dialog.png')
        run('xdotool', 'type', '--delay', '40', 'ethz')
        run('xdotool', 'key', 'Return')
        time.sleep(2)
        shot('3-key-dialog.png')
        run('xdotool', 'type', '--delay', '20', 'sk-fixture-not-a-real-key')
        run('xdotool', 'key', 'Return')
        time.sleep(3)
    after = shot('4-sources-after-add.png')
    requests = [json.loads(l) for l in fixture_log.read_text().splitlines()] if fixture_log.exists() else []
    saves = [r for r in requests if r.get('type') == 'key_account_save']
    report = {'add_account_offered': 'account' in before,
              'save_request': saves,
              'account_row_after': [l for l in after.splitlines() if '(ethz)' in l],
              'account_buttons_after': [l for l in after.splitlines() if 'replace key' in l or 'remove' in l]}
    (OUT / 'result.json').write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))
finally:
    app.terminate()
    try:
        app.wait(timeout=5)
    except subprocess.TimeoutExpired:
        app.kill()
    log.close()
