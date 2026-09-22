"""Independent fresh-profile startup; no model/network request; stop after 20 seconds."""
import os, subprocess, tempfile, time, json, hashlib
from pathlib import Path
out=Path(__file__).resolve().parent
root=Path(tempfile.mkdtemp(prefix='v561p-'))
gate=Path('/tmp/claude-1000/land/1cxd-b/verify')
binary=gate/'build/relay'
env=dict(os.environ)
for key, folder in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_RUNTIME_DIR','run'),('TMPDIR','tmp')]:
 p=root/folder;p.mkdir(mode=0o700);env[key]=str(p)
display=next(':'+str(n) for n in range(300,330) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
env.update(DISPLAY=display,RELAY_KEYRING='off',RELAY_NO_ISOLATION='1',RELAY_DATA_DIR=str(gate/'src'))
x=subprocess.Popen(['Xvfb',display,'-screen','0','1600x1000x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(1)
start=time.monotonic()
with (root/'stderr').open('w') as stderr:
 r=subprocess.Popen([str(binary),'--workspace',str(root),'--clean-shell','--fresh'],env=env,cwd=root,stdout=stderr,stderr=stderr)
 try:
  time.sleep(15)
  alive=r.poll() is None
  subprocess.run(['import','-window','root',str(out/'ui.png')],env=env,check=True)
  time.sleep(5)
  alive_stop=r.poll() is None
  elapsed=time.monotonic()-start
  if alive_stop:r.terminate()
  code=r.wait(timeout=8)
 finally:
  if r.poll() is None:r.kill();r.wait()
  x.terminate();x.wait()
log=(root/'data/relay/logs/relay.log').read_text()
(out/'startup.log').write_text(log+'\nSTDERR:\n'+(root/'stderr').read_text())
result=dict(revision='82acbc04993af406b9b091f659165e6ba356241c',binary=str(binary),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),data=str(gate/'src'),sandbox=str(root),display=display,empty_profile=True,alive_at_15s=alive,alive_before_deliberate_stop=alive_stop,elapsed_before_stop=elapsed,returncode=code,worker_ready='event type=ready' in log,gui_crash='gui_crash' in log,environment={k:env[k] for k in ['HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR','TMPDIR','RELAY_KEYRING','RELAY_NO_ISOLATION','RELAY_DATA_DIR']})
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));print(log)
