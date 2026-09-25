import os, json, tempfile, subprocess, time, pathlib, shutil, sys

BIN = os.environ.get('RELAY_BIN',
                     '/home/elliott/.local/state/relay/land/verify-slots/relay-terminal-1006c7a3-0/build/relay')
ROOT = pathlib.Path('/home/elliott/repos/relay-terminal')
OUT = sys.argv[1]

p = pathlib.Path(tempfile.mkdtemp(prefix='relay-wbfm-'))
for name in ['config/RelayTerminal', 'data/backend', 'runtime', 'workspace/issues', 'cache', 'share']:
    (p / name).mkdir(parents=True)
(p / 'runtime').chmod(0o700)
(p / 'config/RelayTerminal/relay.conf').write_text('[instructions]\nonboarded=true\n[theme]\nname=relay-dark\n')
(p / 'workspace/issues/board.yaml').write_text('tabs: [{id: features, folder: features}]\ncolumns: [inbox, done]\n')
shutil.copy(str(ROOT / 'docs/qa_evidence/2026-09-25-wbfm-c1/worker.py'), str(p / 'data/backend/worker.py'))
for name in ['shell', 'themes']:
    if (ROOT / name).exists():
        (p / 'data' / name).symlink_to(ROOT / name, target_is_directory=True)

e = os.environ.copy()
e.update(XDG_CONFIG_HOME=str(p / 'config'), XDG_DATA_HOME=str(p / 'share'),
         XDG_CACHE_HOME=str(p / 'cache'), XDG_RUNTIME_DIR=str(p / 'runtime'),
         RELAY_DATA_DIR=str(p / 'data'), RELAY_QA_RECTS=str(p / 'rects.json'), RELAY_KEYRING='off')

def run(*cmd):
    return subprocess.check_output(cmd, env=e, text=True).strip()

log = open(p / 'relay.log', 'w')
app = subprocess.Popen([BIN, '--fresh', '--workspace', str(p / 'workspace')], env=e, stdout=log, stderr=log)
try:
    time.sleep(5)
    win = run('xdotool', 'search', '--onlyvisible', '--pid', str(app.pid)).splitlines()[0]
    run('xdotool', 'windowsize', win, '1450', '1000')
    run('xdotool', 'windowfocus', win)
    run('xdotool', 'key', 'ctrl+shift+m')   # agent.modelOptions: the models pane
    time.sleep(8)
    rects = {}
    if (p / 'rects.json').exists():
        rects = json.loads((p / 'rects.json').read_text())
    print('RECTS:', [(k, v.get('text', '')[:40], v.get('x'), v.get('y'), v.get('w'), v.get('h'))
                     for k, v in rects.items() if v.get('text')])
    # The pane's tab captions are drawn, not widgets, so RELAY_QA_RECTS does not name them: the
    # row sits under the "Shared model settings" line (y≈111) above the content (y≈198), and
    # Sources is its first tab. Click it by position.
    run('xdotool', 'mousemove', '778', '150', 'click', '1')
    time.sleep(4)
    run('xdotool', 'windowsize', win, '1451', '1000')  # nudge a repaint
    time.sleep(2)
    run('import', '-window', win, OUT)
    print('Fixture:', p)
    print(subprocess.check_output(['tesseract', OUT, 'stdout', '--psm', '11'], text=True))
finally:
    app.terminate()
    try:
        app.wait(timeout=5)
    except subprocess.TimeoutExpired:
        app.kill()
    log.close()
