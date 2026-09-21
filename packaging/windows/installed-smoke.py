# SPDX-License-Identifier: AGPL-3.0-or-later
"""Start the installed GUI, wait for its real PowerShell bridge, then close normally."""
import ctypes
from ctypes import wintypes
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import time

root = Path(sys.argv[1]).resolve()
evidence = Path(sys.argv[2]).resolve()
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='relay-installed-') as tmp:
    env = dict(os.environ, TEMP=tmp, TMP=tmp, RELAY_CLEAN_SHELL='1',
               XDG_DATA_HOME=tmp, XDG_CONFIG_HOME=tmp)
    with (evidence / 'gui-stderr.txt').open('wb') as log:
        proc = subprocess.Popen([str(root / 'bin/relay.exe')], env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 45
            state = None
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    raise RuntimeError(f'Installed GUI exited early: {proc.returncode}')
                for path in Path(tmp).glob('relay-*/state.json'):
                    try:
                        candidate = json.loads(path.read_text(encoding='utf-8-sig'))
                        if candidate.get('event') == 'ready' or candidate.get('type') == 'ready':
                            state = candidate
                            break
                    except (OSError, ValueError):
                        pass
                if state is not None:
                    break
                time.sleep(.2)
            if state is None:
                raise RuntimeError('Installed GUI never received PowerShell ready event')
            (evidence / 'shell-ready.json').write_text(json.dumps(state, indent=2), encoding='utf-8')
            # Let the actual GUI worker finish startup before inspecting its first-run UI.
            worker_log = Path(tmp) / 'relay/logs/worker.log'
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if worker_log.exists() and 'worker_start' in worker_log.read_text(encoding='utf-8', errors='replace'):
                    break
                if proc.poll() is not None:
                    raise RuntimeError('GUI exited while starting the agent worker')
                time.sleep(.2)
            else:
                raise RuntimeError('Installed GUI did not start its bundled agent worker')
            time.sleep(5)
            screenshot = str(evidence / 'desktop.png').replace("'", "''")
            subprocess.run(['pwsh', '-NoProfile', '-Command',
                'Add-Type -AssemblyName System.Windows.Forms,System.Drawing; '
                '$b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds; '
                '$i=[System.Drawing.Bitmap]::new($b.Width,$b.Height); '
                '$g=[System.Drawing.Graphics]::FromImage($i); '
                '$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size); '
                f"$i.Save('{screenshot}'); $g.Dispose(); $i.Dispose()"], check=True)
            user32 = ctypes.WinDLL('user32', use_last_error=True)
            callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            def close_window(hwnd, _):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == proc.pid and user32.IsWindowVisible(hwnd):
                    user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE: save and normal teardown
                return True
            user32.EnumWindows(callback(close_window), 0)
            if proc.wait(timeout=20) != 0:
                raise RuntimeError(f'GUI shutdown failed: {proc.returncode}')
        finally:
            logs = Path(tmp) / 'relay/logs'
            if logs.exists():
                shutil.copytree(logs, evidence / 'logs', dirs_exist_ok=True)
            if proc.poll() is None:
                proc.kill()
                proc.wait()
print('PASS installed GUI: launch, native PowerShell ready, screenshot, normal exit')
