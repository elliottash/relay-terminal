import os, subprocess, time, shutil, json
from pathlib import Path
root=Path('/home/elliott/repos/relay-terminal'); out=Path('/tmp/mdl1-real2-evidence'); out.mkdir(exist_ok=True)
home=Path('/tmp/mv-real2-auth'); home.mkdir(exist_ok=True)
for n in ('config/RelayTerminal','data','cache','run','work','.codex'): (home/n).mkdir(parents=True,exist_ok=True)
(home/'run').chmod(0o700)
(home/'.bashrc').write_text('export PATH=/home/elliott/.npm-global/bin:$PATH\n')
(home/'pyshim').mkdir(exist_ok=True)
(home/'pyshim/cryptography.py').write_text('raise ImportError("no hosted fallback in guest verification")\n')
# Copy only authentication into this disposable profile, never into evidence.
shutil.copyfile(Path('/home/elliott/.codex/auth.json'),home/'.codex/auth.json');(home/'.codex/auth.json').chmod(0o600)
conf='''[instructions]
onboarded=true
[isolation]
enabled=false
[models]
tier\\main=guest:codex|gpt-6-astra|low
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
[url_handler]
announced=true
'''
(home/'config/RelayTerminal/relay.conf').write_text(conf)
display=next(':'+str(n) for n in range(985,995) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
env={k:v for k,v in os.environ.items() if not (k.startswith('RELAY_') and ('KEY' in k or 'TOKEN' in k))}
env.update(PATH='/home/elliott/.npm-global/bin:'+env.get('PATH',''), PYTHONPATH=str(home/'pyshim'), HOME=str(home),XDG_CONFIG_HOME=str(home/'config'),XDG_DATA_HOME=str(home/'data'),XDG_CACHE_HOME=str(home/'cache'),XDG_RUNTIME_DIR=str(home/'run'),DISPLAY=display,RELAY_KEYRING='off',RELAY_OPENROUTER_CATALOG='off')
xvfb=subprocess.Popen(['Xvfb',display,'-screen','0','1600x940x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def x(*args):return subprocess.check_output(['xdotool',*args],env=env,text=True).strip()
def shot(name):subprocess.run(['import','-window','root',str(out/(name+'.png'))],env=env,check=True)
relay=None
try:
 time.sleep(.5)
 with (out/'stderr.txt').open('w') as err:
  relay=subprocess.Popen([str(root/'build/relay'),'--workspace',str(home/'work')],env=env,stdout=err,stderr=err)
  time.sleep(16)
  wins=x('search','--pid',str(relay.pid)).splitlines();win=max(wins,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
  x('windowmove',win,'0','0','windowsize',win,'1600','900');x('windowfocus',win)
  time.sleep(3);shot('before-first-prompt');print('READY for screenshot inspection',flush=True)
  for _ in range(60):
   if (out/'go').exists():break
   time.sleep(1)
  else:raise RuntimeError('Guest screenshot not approved for test')
  x('mousemove','300','830','click','1');x('type','--delay','15','Reply with exactly MDL1_OK. Do not use tools or read files.');x('key','ctrl+Return')
  time.sleep(15);shot('after-15s');print('Captured real first-prompt attempt',flush=True)
  time.sleep(20);shot('after-35s')
finally:
 if relay:
  relay.terminate()
  try:relay.wait(timeout=5)
  except subprocess.TimeoutExpired:relay.kill();relay.wait()
 xvfb.terminate();xvfb.wait()
 (home/'.codex/auth.json').unlink(missing_ok=True)
print('Finished; credential copy removed',flush=True)
