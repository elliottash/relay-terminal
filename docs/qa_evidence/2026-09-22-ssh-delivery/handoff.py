"""Real localhost SSH in isolated Relay/Xvfb; stub worker prevents provider calls."""
import os, subprocess, tempfile, time, json, shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent
FAKE='''import sys,json,os,threading
print(json.dumps({'event':'ready'}),flush=True)
fired=False
for line in sys.stdin:
 r=json.loads(line); t=r.get('type')
 with open(os.environ['SSH_DRIVE_EVENTS'],'a') as f: f.write(json.dumps(r)+'\\n')
 if t=='shutdown': break
 if t=='presets': o={'event':'presets','presets':[]}
 elif t=='configure': o={'event':'configured','model':'audit','mode':'build','session_id':'ssh-audit'}
 elif t=='ask': o={'event':'done'}
 elif t=='terminal_history' and not fired and any('HANDOFF_TRIGGER' in x['command'] for x in r['items']):
  fired=True
  print(json.dumps({'event':'configured','model':'audit','mode':'build','session_id':'ssh-handoff'}),flush=True)
  threading.Timer(.5,lambda:print(json.dumps({'event':'terminal_command','id':'ssh-live-handoff','command':"printf 'HANDOFF_STDOUT\\n'; false",'intent':'Verify SSH handoff','mode':'run','report_back':True}),flush=True)).start()
  continue
 elif t=='route': o={'event':'route','id':r.get('id'),'route':'shell','text':r.get('text','').removeprefix('!')}
 else: continue
 print(json.dumps(o),flush=True)
'''
with tempfile.TemporaryDirectory(prefix='relay-ssh-audit-') as tmp:
 p=Path(tmp)
 for n in ['config/RelayTerminal','data','cache','run','fixture/backend','work','tmp']:(p/n).mkdir(parents=True)
 (p/'run').chmod(0o700)
 (p/'remote').mkdir()
 (p/'remote/REMOTE_ONLY_PROOF.txt').write_text('REMOTE FILE\n')
 (p/'work/collision.txt').write_text('LOCAL FILE MUST NOT ATTACH\n')
 (p/'remote/collision.txt').write_text('REMOTE COLLISION CONTENT\n')
 (OUT/'handoff-events.jsonl').write_text('')
 (p/'work/LOCAL_ONLY_SSH_AUDIT.txt').write_text('Local fixture; remote cwd is /tmp.\n')
 (p/'fixture/backend/worker.py').write_text(FAKE)
 for n in ['shell','scripts','assets']:(p/'fixture'/n).symlink_to(ROOT/n)
 (p/'config/RelayTerminal/relay.conf').write_text('[instructions]\nonboarded=true\n[agent]\nterminal_handoff=agent\n[isolation]\nenabled=false\n[suggestions]\nnext_command=false\nnext_prompt=false\n[security]\napprovals_chosen=true\n[url_handler]\nannounced=true\n')
 display=next(':'+str(n) for n in range(450,490) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
 env={**os.environ,'SSH_DRIVE_EVENTS':str(OUT/'handoff-events.jsonl'),'OMP_THREAD_LIMIT':'1','DISPLAY':display,'XDG_CONFIG_HOME':str(p/'config'),'XDG_DATA_HOME':str(p/'data'),'XDG_CACHE_HOME':str(p/'cache'),'XDG_RUNTIME_DIR':str(p/'run'),'TMPDIR':str(p/'tmp'),'RELAY_DATA_DIR':str(p/'fixture'),'RELAY_KEYRING':'off','QT_QPA_PLATFORM':'xcb'}
 xvfb=subprocess.Popen(['Xvfb',display,'-screen','0','1300x950x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 app=None
 def x(*a): return subprocess.check_output(['xdotool',*a],env=env,text=True).strip()
 def shot(name):
  subprocess.run(['import','-window',win,str(OUT/(name+'.png'))],env=env,check=True)
  with (OUT/(name+'.txt')).open('w') as f: subprocess.run(['tesseract',str(OUT/(name+'.png')),'stdout'],stdout=f,stderr=subprocess.DEVNULL,env=env)
  print(name,flush=True)
 def send(s,wait=2):
  x('type','--clearmodifiers','--delay','3',s); x('key','--clearmodifiers','Return');time.sleep(wait)
 try:
  time.sleep(.5)
  with (OUT/'handoff-gui.log').open('w') as log:
   app=subprocess.Popen([str(ROOT/'build/relay'),'--clean-shell','--workspace',str(p/'work')],env=env,stdout=log,stderr=log)
   time.sleep(5)
   windows=x('search','--pid',str(app.pid)).splitlines()
   win=max(windows,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
   x('windowsize',win,'1200','900'); x('windowfocus','--sync',win); time.sleep(1)
   send("!ssh -t localhost 'HISTFILE=/dev/null bash --noprofile --norc -i'",7)
   send("!printf 'HANDOFF_TRIGGER\\n'",4);shot('12-terminal-handoff')
   send('!exit',3)
   for f in (p/'data').rglob('*'):
    if f.is_file() and f.suffix in ['.jsonl','.log']:
     dest=OUT/'runtime'/f.relative_to(p/'data'); dest.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(f,dest)
 finally:
  if app:
   app.terminate()
   try:app.wait(timeout=5)
   except subprocess.TimeoutExpired:app.kill();app.wait()
  # Terminate only the multiplexers this isolated runtime created.
  for s in (p/'run/relay-ssh').glob('*'):
   subprocess.run(['ssh','-S',str(s),'-O','exit','localhost'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  xvfb.terminate();xvfb.wait()
