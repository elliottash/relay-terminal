"""Full app Xvfb drive. EFM7_SOURCE must point to clean revision export built by relay-build.
Start preserves HOME solely for real guest credentials; all Relay XDG state is isolated.
No replacement models/harnesses. API keys, if requested, are explicitly fake configuration-only keys.
"""
import os,sys,json,subprocess,time,tempfile,signal
from pathlib import Path
out=Path(__file__).resolve().parent
state=Path('/tmp/efm7-drive-state.json')
cmd=sys.argv[1]
if cmd=='start':
 root=Path(os.environ['EFM7_SOURCE']);name=sys.argv[2];preset,model,level=sys.argv[3:6]
 sandbox=Path(tempfile.mkdtemp(prefix='efm7-'+name+'-')); env=dict(os.environ)
 for key,rel in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_RUNTIME_DIR','run'),('TMPDIR','tmp')]:
  p=sandbox/rel;p.mkdir(mode=0o700);env[key]=str(p)
 display=next(':'+str(n) for n in range(710,750) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
 fixture=sandbox/'tap';(fixture/'backend').mkdir(parents=True)
 (fixture/'backend/worker.py').symlink_to(out/'tap.py')
 for n in ['shell','scripts','assets','data']:(fixture/n).symlink_to(root/n)
 env.update(DISPLAY=display,RELAY_KEYRING='off',RELAY_NO_ISOLATION='1',RELAY_DATA_DIR=str(fixture),EFM7_EVENTS=str(out/(name+'-events.jsonl')),EFM7_SOURCE=str(root))
 env.pop('RELAY_OPEN_SOCKET',None)
 for k in list(env):
  if k.startswith('RELAY_') and k.endswith('_API_KEY'):env.pop(k)
 if name=='api':env.update(RELAY_OPENAI_API_KEY='fake-configuration-only',RELAY_KIMI_API_KEY='fake-configuration-only')
 config=sandbox/'config/RelayTerminal';config.mkdir()
 (config/'relay.conf').write_text(f'''[instructions]
onboarded=true
[security]
approvals_chosen=true
[isolation]
enabled=false
[provider]
preset={preset}
model={model}
[agent]
effort={level}
[models]
tier\\main={preset}|{model}|{level}
tier\\high={preset}|{model}|{level}
tier\\flash={preset}|{model}|low
tier\\lite={preset}|{model}|low
[suggestions]
next_command=false
next_prompt=false
[theme]
name=relay-dark
[url_handler]
announced=true
''')
 (sandbox/'project').mkdir();(sandbox/'project/README.md').write_text('Isolated effort verification workspace. No changes needed.\n')
 x=subprocess.Popen(['Xvfb',display,'-screen','0','1640x940x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
 time.sleep(1)
 f=open(sandbox/'stderr.log','w')
 r=subprocess.Popen([str(root/'build/relay'),'--workspace',str(sandbox/'project'),'--clean-shell','--fresh'],env=env,stdout=f,stderr=f,start_new_session=True)
 time.sleep(16)
 def xd(*args):return subprocess.check_output(['xdotool',*map(str,args)],env=env,text=True).strip()
 wins=xd('search','--pid',r.pid).splitlines()
 win=max(wins,key=lambda w:int(xd('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0])*int(xd('getwindowgeometry','--shell',w).split('HEIGHT=')[1].splitlines()[0]))
 xd('windowmove',win,0,0);xd('windowsize',win,1600,900);xd('windowfocus',win)
 # Store only the X display and process metadata; never persist inherited credential environment.
 state.write_text(json.dumps(dict(display=display,win=win,relay=r.pid,xvfb=x.pid,sandbox=str(sandbox),name=name)))
 print(state.read_text());sys.exit()
s=json.loads(state.read_text());env=dict(os.environ,DISPLAY=s['display'])
def xd(*args):return subprocess.check_output(['xdotool',*map(str,args)],env=env,text=True).strip()
if cmd=='say':
 xd('mousemove',400,830);xd('click',1);xd('type','--delay',20,sys.argv[2]);time.sleep(.3);xd('key','Return');time.sleep(2)
elif cmd=='key':xd('key',*sys.argv[2:]);time.sleep(1)
elif cmd=='shot':
 xd('mousemove',1620,920);time.sleep(.5);subprocess.run(['import','-window','root',str(out/(sys.argv[2]+'.png'))],env=env,check=True)
elif cmd=='stop':
 os.killpg(s['relay'],signal.SIGTERM);time.sleep(2);os.kill(s['xvfb'],signal.SIGTERM)
elif cmd=='events':
 p=out/(s['name']+'-events.jsonl');print(p.read_text()[-12000:] if p.exists() else 'none')
