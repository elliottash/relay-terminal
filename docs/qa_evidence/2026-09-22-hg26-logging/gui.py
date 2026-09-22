"""HG26 bounded Xvfb worker-lifecycle drive; no live profile or provider calls."""
import os, shutil, subprocess as sp, tempfile, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='hg26-gui-') as temp:
    tmp = Path(temp)
    for path in ('config/RelayTerminal', 'data', 'cache', 'run', 'fixture/backend', 'work', 'home'):
        (tmp/path).mkdir(parents=True)
    (tmp/'run').chmod(0o700)
    shutil.copy(OUT/'fixture_worker.py', tmp/'fixture/backend/worker.py')
    for name in ('scripts', 'shell', 'assets'):
        (tmp/'fixture'/name).symlink_to(ROOT/name, target_is_directory=True)
    (tmp/'config/RelayTerminal/relay.conf').write_text('[logging]\nlevel=debug\n[instructions]\nonboarded=true\n[isolation]\nenabled=false\n[models]\ntier\\main=kimi|kimi-k3|high\n[security]\napprovals_chosen=true\n[url_handler]\nannounced=true\n')
    display = next(':'+str(n) for n in range(610,660) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
    env = dict(os.environ, HOME=str(tmp/'home'), XDG_CONFIG_HOME=str(tmp/'config'),
        XDG_DATA_HOME=str(tmp/'data'), XDG_CACHE_HOME=str(tmp/'cache'), XDG_RUNTIME_DIR=str(tmp/'run'),
        DISPLAY=display, QT_QPA_PLATFORM='xcb', RELAY_KEYRING='off', RELAY_DATA_DIR=str(tmp/'fixture'),
        HG_ROOT=str(ROOT), HG_TMP=str(tmp), RELAY_LOG_ORIGIN='qa', RELAY_LOG_RUN_ID='hg26-lifecycle',
        RELAY_BUILD_ID=(ROOT/'build/relay.build-id').read_text().strip(),
        RELAY_BOARD_MCP_URL='', RELAY_CONTEXT='', RELAY_SESSION_TOKEN='')
    xvfb = sp.Popen(['Xvfb', display, '-screen', '0', '1200x900x24'], stdout=sp.DEVNULL, stderr=sp.DEVNULL)
    time.sleep(.4)
    app = sp.Popen([str(ROOT/'build/relay'), '--workspace', str(tmp/'work'), '--fresh'], env=env,
                   stdout=(tmp/'stderr').open('w'), stderr=sp.STDOUT)
    log = tmp/'data/relay/logs/relay.log'
    def await_log(text):
        for _ in range(100):
            if log.exists() and text in log.read_text(): return
            time.sleep(.1)
        raise AssertionError('Missing '+text+'\n'+(log.read_text() if log.exists() else 'no log')+'\n'+(tmp/'stderr').read_text())
    def key(value):
        sp.run(['xdotool', 'key', '--clearmodifiers', value], env=env, check=True)
        time.sleep(.6)
    try:
        await_log('reason=reconfigure')
        time.sleep(2)
        win = sp.check_output(['xdotool', 'search', '--pid', str(app.pid)], env=env, text=True).splitlines()[0]
        sp.run(['xdotool', 'windowfocus', win], env=env, check=True)
        (tmp/'unexpected-exit').touch()
        await_log('reason=unexpected')
        key('ctrl+t')
        time.sleep(2)
        key('ctrl+w')
        await_log('reason=shutdown')
        lines = [line for line in log.read_text().splitlines() if 'worker_exit ' in line]
        (OUT/'worker-exits.log').write_text('\n'.join(lines)+'\n')
        assert any('INFO' in l and 'reason=reconfigure' in l for l in lines), lines
        assert any('INFO' in l and 'reason=shutdown' in l for l in lines), lines
        assert any('ERROR' in l and 'code=0 crashed=0 reason=unexpected' in l for l in lines), lines
        print('PASS: reconfigure INFO; shutdown INFO; unexpected clean exit ERROR')
    finally:
        app.terminate()
        try: app.wait(timeout=5)
        except sp.TimeoutExpired: app.kill(); app.wait()
        xvfb.terminate(); xvfb.wait()
