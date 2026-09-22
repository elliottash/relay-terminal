"""Stage the actual Relay window at three DPRs; assert chrome geometry and capture it."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
results = {}
for scale in (1, 1.5, 2):
    with tempfile.TemporaryDirectory(prefix='relay-atp7-') as directory:
        tmp = Path(directory)
        env = dict(os.environ)
        for key in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME', 'XDG_RUNTIME_DIR'):
            path = tmp / key
            path.mkdir(mode=0o700)
            env[key] = str(path)
        display = next(f':{n}' for n in range(610, 680) if not Path(f'/tmp/.X11-unix/X{n}').exists())
        env.update(DISPLAY=display, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR=str(scale),
                   QT_AUTO_SCREEN_SCALE_FACTOR='0', RELAY_QA_RECTS=str(tmp / 'rects.json'),
                   RELAY_NO_ISOLATION='1', RELAY_KEYRING='off', RELAY_DATA_DIR=str(ROOT))
        env.pop('RELAY_OPEN_SOCKET', None)
        xvfb = subprocess.Popen(['Xvfb', display, '-screen', '0', '2800x1800x24'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        app = None
        try:
            time.sleep(.5)
            with (tmp / 'stderr').open('w') as log:
                app = subprocess.Popen([str(ROOT / 'build/relay'), '--workspace', str(tmp)], env=env,
                                       stdout=log, stderr=log)
                for _ in range(120):
                    if app.poll() is not None:
                        raise RuntimeError((tmp / 'stderr').read_text())
                    try:
                        rects = json.loads((tmp / 'rects.json').read_text())
                        icon = rects['windowIcon']
                        bell = rects['windowBellButton']
                        break
                    except (FileNotFoundError, KeyError, json.JSONDecodeError):
                        time.sleep(.25)
                else:
                    raise RuntimeError('No chrome geometry produced')
                assert (icon['w'], icon['h']) == (22, 22), icon
                assert (bell['w'], bell['h']) == (26, 26), bell
                corner = rects['windowChromeLeft']
                assert corner['x'] <= icon['x'] and corner['y'] <= icon['y']
                assert icon['x'] + 22 <= corner['x'] + corner['w']
                assert icon['y'] + 22 <= corner['y'] + corner['h']
                results[str(scale)] = {'icon': icon, 'bell': bell, 'corner': corner}
                # Dismiss the optional first-run instructions dialog in this isolated profile.
                dialogs = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(app.pid),
                                          '--name', 'Agent instructions'], env=env, text=True, capture_output=True)
                for dialog in dialogs.stdout.splitlines():
                    subprocess.run(['xdotool', 'key', '--window', dialog, 'Escape'], env=env, check=True)
                time.sleep(.5)
                # Capture the full app window without resampling physical pixels.
                window = subprocess.check_output(['xdotool', 'search', '--onlyvisible', '--pid', str(app.pid)], env=env, text=True).splitlines()[-1]
                subprocess.run(['import', '-window', window, str(OUT / f'scale-{scale}.png')], env=env, check=True)
        finally:
            if app is not None:
                app.terminate()
                try:
                    app.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait()
            xvfb.terminate()
            xvfb.wait()
(OUT / 'geometry.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
print('PASS: 22x22 icon inside chrome at 1x, 1.5x and 2x; adjacent controls remain 26x26')
