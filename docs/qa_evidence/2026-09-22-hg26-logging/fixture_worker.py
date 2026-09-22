"""Real worker; injected first configure failure and opt-in unexpected exit, no provider calls."""
import os, sys, threading, time
from pathlib import Path
sys.path.insert(0, os.environ['HG_ROOT'] + '/backend')
from relay_core import keystore, openrouter_catalog, relay_pro, guest_harness_provider
keystore.lookup = lambda *a, **k: 'fixture-key'
openrouter_catalog.start_refresh = lambda *a, **k: None
relay_pro.start_refresh = lambda *a, **k: None
guest_harness_provider.start_catalog_scan = lambda *a, **k: None
import worker
original = worker.Agent
marker = Path(os.environ['HG_TMP']) / 'configured-once'
def agent(*a, **k):
    if not marker.exists():
        marker.touch()
        raise AttributeError('isolated configure failure')
    return original(*a, **k)
worker.Agent = agent
def exit_when_requested():
    marker = Path(os.environ['HG_TMP']) / 'unexpected-exit'
    while not marker.exists(): time.sleep(.1)
    marker.unlink()
    os._exit(0)
threading.Thread(target=exit_when_requested, daemon=True).start()
worker.main()
