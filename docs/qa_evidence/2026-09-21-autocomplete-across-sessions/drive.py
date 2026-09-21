#!/usr/bin/env python3
"""Isolated live check. Uses a harmless shell function named sudo, never system sudo."""
import os
from pathlib import Path
import subprocess as sp
import tempfile
import time
import sys

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build/relay'
sandbox = Path(tempfile.mkdtemp(prefix='relay-command-memory-'))
display = next(f':{n}' for n in range(210, 250) if not Path(f'/tmp/.X11-unix/X{n}').exists())
env = dict(os.environ, DISPLAY=display, RELAY_KEYRING='off', HISTFILE=str(sandbox / 'shell-history'))
for key, folder in [('XDG_CONFIG_HOME','config'), ('XDG_DATA_HOME','data'), ('XDG_CACHE_HOME','cache'), ('XDG_RUNTIME_DIR','run')]:
    path = sandbox / folder
    path.mkdir(mode=0o700)
    env[key] = str(path)
config = sandbox / 'config/RelayTerminal'
config.mkdir()
(config / 'relay.conf').write_text('[instructions]\nonboarded=true\n[isolation]\nenabled=false\n')
xvfb = sp.Popen(['Xvfb', display, '-screen', '0', '1200x850x24'], stdout=sp.DEVNULL, stderr=sp.DEVNULL)
relay = None
log = (sandbox / 'relay.log').open('w')
def run(*args):
    return sp.check_output(args, env=env, text=True).strip()
def key(*keys):
    run('xdotool', 'key', '--delay', '100', *keys)
def type_text(text):
    run('xdotool', 'type', '--clearmodifiers', '--delay', '45', text)
def start():
    global relay
    relay = sp.Popen([str(binary), '--clean-shell', '--workspace', str(sandbox)], env=env, stdout=log, stderr=log)
    time.sleep(5)
    windows = run('xdotool', 'search', '--pid', str(relay.pid)).splitlines()
    window = windows[-1]
    run('xdotool', 'windowsize', window, '1150', '800')
    run('xdotool', 'windowfocus', window)
    return window

def shot(name):
    time.sleep(1)
    run('import', '-window', 'root', str(out / name))
try:
    time.sleep(1)
    start()
    type_text('sudo() { printf "memory-test: %s\\n" "$*"; }')
    key('ctrl+shift+Return')
    time.sleep(2)
    type_text('sudo apt update')
    key('ctrl+shift+Return')
    time.sleep(2)
    store = sandbox / 'data/relay/state/command-history.txt'
    assert 'sudo apt update' in store.read_text(), store
    key('ctrl+t')
    time.sleep(3)
    type_text('sudo apt')
    shot('new-pane.png')
    key('Right')
    shot('accepted.png')
    # Clear the draft and quit; next launch starts a new pane with the same saved command store.
    key('ctrl+a', 'BackSpace')
    relay.terminate()
    relay.wait(timeout=10)
    # Remove only this isolated test's layout to ensure the restarted pane has no local history.
    for path in (sandbox / 'data/relay/state').glob('windows*.json'):
        path.unlink()
    start()
    type_text('sudo apt')
    shot('restarted.png')
    print('Saved commands:', store.read_text())
    print('Evidence:', out)
    print('Sandbox:', sandbox)
finally:
    if relay and relay.poll() is None:
        relay.terminate()
        relay.wait(timeout=10)
    xvfb.terminate()
    xvfb.wait(timeout=10)
    log.close()
