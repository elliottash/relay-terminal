import os,subprocess,time,json
from pathlib import Path
root=Path('/tmp/jnyn-fresh-root').read_text().strip()
p=Path(root); env=dict(os.environ)
display=next(':'+str(n) for n in range(280,300) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
x=subprocess.Popen(['Xvfb',display,'-screen','0','1600x1000x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
for name,rel in [('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_RUNTIME_DIR','run'),('TMPDIR','tmp')]:
 d=p/rel; d.mkdir(exist_ok=True,mode=0o700); env[name]=str(d)
env.update(DISPLAY=display,RELAY_KEYRING='off')
conf=p/'config/RelayTerminal';conf.mkdir(exist_ok=True);(conf/'relay.conf').write_text('[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n')
time.sleep(1)
binary=str(Path('/tmp/claude-1000/land/jnyn/verify/build/relay').resolve()); f=open('/tmp/jnyn-fresh-runtime.log','w'); r=subprocess.Popen([binary,'--workspace',root,'--clean-shell','--fresh'],env=env,cwd=root,stdout=f,stderr=f)
time.sleep(10)
state=dict(root=root,display=display,xvfb=x.pid,relay=r.pid,binary=binary,exit=r.poll());Path('/tmp/jnyn-fresh-state.json').write_text(json.dumps(state)); print(state)
print(Path('/tmp/jnyn-fresh-runtime.log').read_text()[-3000:])

time.sleep(1200)
