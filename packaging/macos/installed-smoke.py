# SPDX-License-Identifier: AGPL-3.0-or-later
"""Mount a DMG, install elsewhere, and exercise its private runtimes and native GUI."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time


def check_shell(resources, env, root, evidence):
    runtime = root / 'shell-runtime'
    runtime.mkdir()
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 120, 0, 0))
    shellenv = dict(env, TERM='xterm-256color', RELAY_RUNTIME_DIR=str(runtime),
                    RELAY_START_DIR=str(root), RELAY_SESSION_TOKEN='installed-smoke',
                    RELAY_SHELL_EVENT=str(resources / 'relay/shell/event.py'))
    shell = subprocess.Popen([str(resources / 'bash/bin/bash'), '--noprofile', '--rcfile',
                              str(resources / 'relay/shell/integration.bash'), '-i'],
                             stdin=slave, stdout=slave, stderr=slave, env=shellenv, cwd=root)
    os.close(slave)
    output = bytearray()
    def wait(stage):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if select.select([master], [], [], .05)[0]:
                output.extend(os.read(master, 65536))
            try:
                state = json.loads((runtime / 'state.json').read_text())
                if state['event'] == stage:
                    return state
            except (OSError, ValueError):
                pass
            if shell.poll() is not None:
                raise RuntimeError(f'Bundled Bash exited {shell.returncode}: {output!r}')
        raise RuntimeError(f'Bundled Bash did not emit {stage}: {output!r}')
    try:
        wait('ready')
        command = "printf 'héllo 世界\\n' > composer-result.txt\nfalse".encode()
        (runtime / 'input.txt').write_bytes(command)
        os.write(master, b'\x18\x12')
        state = wait('loaded')
        assert state['input_sha256'] == hashlib.sha256(command).hexdigest(), state
        assert not (root / 'composer-result.txt').exists(), 'Composer executed before acknowledgement'
        os.write(master, b'\r')
        state = wait('ready')
        assert state['status'] == 1, state
        assert (root / 'composer-result.txt').read_text() == 'héllo 世界\n'
        os.write(master, b'exit 0\r')
        if shell.wait(timeout=10) != 0:
            raise RuntimeError('Bundled Bash did not exit normally')
    finally:
        if shell.poll() is None:
            shell.kill()
            shell.wait(timeout=10)
        os.close(master)
        (evidence / 'bundled-shell-pty.txt').write_bytes(output)


def main():
    image, evidence = map(lambda p: Path(p).resolve(), sys.argv[1:3])
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='relay-macos-installed-') as tmp:
        root = Path(tmp)
        mount = root / 'disk image'
        mount.mkdir()
        subprocess.run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', str(mount), str(image)], check=True)
        installed = root / 'Installed applications/Relay.app'
        try:
            installed.parent.mkdir()
            subprocess.run(['ditto', str(mount / 'Relay.app'), str(installed)], check=True)
        finally:
            subprocess.run(['hdiutil', 'detach', str(mount)], check=True)
        resources = installed / 'Contents/Resources'
        python = resources / 'python/bin/python3'
        bash = resources / 'bash/bin/bash'
        work = root / 'profile'
        work.mkdir()
        env = dict(os.environ, HOME=str(work), TMPDIR=str(work) + '/',
                   XDG_DATA_HOME=str(work), XDG_CONFIG_HOME=str(work),
                   RELAY_CLEAN_SHELL='1', RELAY_KEYRING='off', RELAY_INDEX='off',
                   RELAY_BASH=str(bash), RELAY_PYTHON=str(python), PYTHONDONTWRITEBYTECODE='1',
                   PYTHONPATH=os.pathsep.join(map(str, (resources / 'relay/backend', resources / 'relay',
                                                       resources / 'python/lib/python3.13/site-packages'))),
                   PATH='/usr/bin:/bin:/usr/sbin:/sbin')
        for key in ('QT_PLUGIN_PATH', 'QTDIR', 'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'PYTHONHOME', 'QT_QPA_PLATFORM', 'SSL_CERT_FILE', 'SSL_CERT_DIR'):
            env.pop(key, None)
        subprocess.run([str(python), '-S', '-c',
                        'import cryptography,ssl; from relay_core import agent, board; import remote.gui_host'],
                       env=env, check=True)
        # Validate real certificate trust after relocating, with no runner CA environment.
        tls_check = subprocess.run([str(python), '-S', '-c',
            "import json,ssl,urllib.request; "
            "context=ssl.create_default_context(); "
            "assert context.cert_store_stats()['x509_ca'] > 0, ssl.get_default_verify_paths(); "
            "response=urllib.request.urlopen('https://www.python.org/',context=context,timeout=20); "
            "assert response.status == 200; "
            "print(json.dumps({'ca_paths':ssl.get_default_verify_paths()._asdict(),'ca_count':context.cert_store_stats()['x509_ca'],'https_status':response.status}))"],
            env=env, check=True, capture_output=True, text=True)
        (evidence / 'private-python-tls.json').write_text(tls_check.stdout)
        subprocess.run([str(python), '-S', 'tests/test_worker_encoding.py' , '--worker',
                        str(resources / 'relay/backend/worker.py')], env=env, check=True)
        check_shell(resources, env, work, evidence)
        with (evidence / 'gui-stderr.txt').open('wb') as log:
            proc = subprocess.Popen([str(installed / 'Contents/MacOS/relay')],
                                    env=env, cwd=work, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 60
                state = None
                worker_log = work / 'relay/logs/worker.log'
                while time.monotonic() < deadline:
                    if proc.poll() is not None:
                        raise RuntimeError(f'Installed GUI exited early: {proc.returncode}')
                    for path in work.glob('relay-*/state.json'):
                        try:
                            candidate = json.loads(path.read_text())
                            if candidate.get('event') == 'ready':
                                state = candidate
                        except (OSError, ValueError):
                            pass
                    if state and worker_log.exists() and 'worker_start' in worker_log.read_text(errors='replace'):
                        break
                    time.sleep(.1)
                else:
                    raise RuntimeError('Installed GUI did not start its native Bash and Python worker')
                command = subprocess.check_output(['ps', '-p', str(state['shell_pid']), '-o', 'command='], text=True)
                if str(bash) not in command:
                    raise RuntimeError(f'GUI used an external shell: {command}')
                children = subprocess.check_output(['ps', '-axo', 'pid=,ppid=,command='], text=True)
                if not any(str(python) in line and re.match(r'\s*\d+\s+' + str(proc.pid) + r'\s', line)
                           for line in children.splitlines()):
                    raise RuntimeError('GUI worker did not use its private Python')
                (evidence / 'shell-ready.json').write_text(json.dumps(state, indent=2))
                screenshot = subprocess.run(['/usr/sbin/screencapture', '-x', str(evidence / 'desktop.png')],
                                            capture_output=True, text=True)
                (evidence / 'screenshot-status.txt').write_text(screenshot.stderr)
                proc.send_signal(signal.SIGTERM)
                if proc.wait(timeout=20) != 0:
                    raise RuntimeError(f'GUI did not shut down normally: {proc.returncode}')
            finally:
                if (work / 'relay/logs').exists():
                    shutil.copytree(work / 'relay/logs', evidence / 'logs', dirs_exist_ok=True)
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
        # Running a copied app must not modify sealed runtime resources.
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(installed)], check=True)
    print('PASS installed macOS DMG: relocation, private Python/Bash, PTY handshake, GUI worker, clean exit')

if __name__ == '__main__':
    main()
