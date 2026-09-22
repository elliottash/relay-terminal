import hashlib, json, os, pathlib, signal, subprocess, sys, tempfile, time
base = pathlib.Path('/home/elliott/relay-build/20260922-fff7eb8f')
debs = list(pathlib.Path('/home/elliott/relay-debs').glob('*gitfff7eb8f*amd64.deb'))
assert len(debs) == 1, debs
deb = debs[0]
print('PACKAGE', deb, flush=True)
print('SHA256', hashlib.sha256(deb.read_bytes()).hexdigest(), flush=True)
subprocess.run(['dpkg-deb', '-f', str(deb), 'Package', 'Version', 'Architecture'], check=True)
root = base / 'extracted'
subprocess.run(['dpkg-deb', '-x', str(deb), str(root)], check=True)
data = root / 'usr/share/relay'
binary = root / 'usr/bin/relay'
assert b'Relay(libghostty-vt)' in binary.read_bytes()
for rel in ['backend/worker.py', 'shell/integration.bash', 'shell/event.py', 'scripts/relay-open']:
    assert (data / rel).is_file(), rel
print('PASS package files and Ghostty core', flush=True)
with tempfile.TemporaryDirectory(prefix='relay-spb2-smoke-') as temp:
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', RELAY_DATA_DIR=str(data), RELAY_KEYRING='off')
    for key in ['XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME', 'XDG_RUNTIME_DIR']:
        p = pathlib.Path(temp) / key
        p.mkdir(mode=0o700)
        env[key] = str(p)
    for flag in ['--version', '--help']:
        r = subprocess.run([str(binary), flag], env=env, text=True, capture_output=True, timeout=20, check=True)
        print(r.stdout[:600], flush=True)
    r = subprocess.run(['python3', '-S', str(data / 'backend/worker.py')], input='{"type":"shutdown"}\n', env=env, text=True, capture_output=True, timeout=20, check=True)
    assert any(json.loads(line).get('event') == 'ready' for line in r.stdout.splitlines() if line.startswith('{')), r.stdout + r.stderr
    print('PASS extracted worker ready and shutdown', flush=True)
    with (base / 'smoke-gui.log').open('w') as log:
        proc = subprocess.Popen([str(binary), '--workspace', temp], env=env, stdout=log, stderr=log, start_new_session=True)
        descendants = set()
        try:
            time.sleep(8)
            assert proc.poll() is None, 'GUI exited early'
            rows = subprocess.check_output(['ps', '-eo', 'pid,ppid,args'], text=True).splitlines()[1:]
            entries = [line.strip().split(None, 2) for line in rows]
            descendants = {proc.pid}
            for _ in range(10):
                descendants.update(int(pid) for pid, ppid, *_ in entries if int(ppid) in descendants)
            commands = [args[0] for pid, ppid, *args in entries if int(pid) in descendants and args]
            print('\n'.join(commands), flush=True)
            assert any('integration.bash' in cmd for cmd in commands), 'No integration shell'
            assert any(str(data / 'backend/worker.py') in cmd for cmd in commands), 'No packaged worker'
            print('PASS GUI stays running with packaged worker and integration shell', flush=True)
        finally:
            for pid in sorted(descendants - {proc.pid}, reverse=True):
                try: os.kill(pid, signal.SIGTERM)
                except ProcessLookupError: pass
            try: os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError: pass
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
print('PASS extracted-package smoke', flush=True)
