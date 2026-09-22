"""Real localhost SSH in isolated Relay/Xvfb; stub worker prevents provider calls."""
import os, subprocess, tempfile, time, json, shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent
FAKE='''import sys,json
print(json.dumps({'event':'ready'}),flush=True)
for line in sys.stdin:
 r=json.loads(line); t=r.get('type')
 if t=='shutdown': break
 if t=='presets': o={'event':'presets','presets':[]}
 elif t=='configure': o={'event':'configured','model':'audit','mode':'build','session_id':'ssh-audit'}
 elif t=='route': o={'event':'route','id':r.get('id'),'route':'shell','text':r.get('text','').removeprefix('!')}
 else: continue
 print(json.dumps(o),flush=True)
'''
with tempfile.TemporaryDirectory(prefix='relay-ssh-audit-') as tmp:
 p=Path(tmp)
 for n in ['config/RelayTerminal','data','cache','run','fixture/backend','work','tmp']:(p/n).mkdir(parents=True)
 (p/'run').chmod(0o700)
 (p/'work/LOCAL_ONLY_SSH_AUDIT.txt').write_text('Local fixture; remote cwd is /tmp.\n')
 (p/'fixture/backend/worker.py').write_text(FAKE)
 for n in ['shell','scripts','assets']:(p/'fixture'/n).symlink_to(ROOT/n)
 (p/'config/RelayTerminal/relay.conf').write_text('[instructions]\nonboarded=true\n[isolation]\nenabled=false\n[suggestions]\nnext_command=false\nnext_prompt=false\n[security]\napprovals_chosen=true\n[url_handler]\nannounced=true\n')
 display=next(':'+str(n) for n in range(450,490) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
 env={**os.environ,'DISPLAY':display,'XDG_CONFIG_HOME':str(p/'config'),'XDG_DATA_HOME':str(p/'data'),'XDG_CACHE_HOME':str(p/'cache'),'XDG_RUNTIME_DIR':str(p/'run'),'TMPDIR':str(p/'tmp'),'RELAY_DATA_DIR':str(p/'fixture'),'RELAY_KEYRING':'off','QT_QPA_PLATFORM':'xcb'}
 xvfb=subprocess.Popen(['Xvfb',display,'-screen','0','1300x950x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 app=None
 def x(*a): return subprocess.check_output(['xdotool',*a],env=env,text=True).strip()
 def shot(name):
  subprocess.run(['import','-window',win,str(OUT/(name+'.png'))],env=env,check=True)
  with (OUT/(name+'.txt')).open('w') as f: subprocess.run(['tesseract',str(OUT/(name+'.png')),'stdout'],stdout=f,stderr=subprocess.DEVNULL)
  print(name,flush=True)
 def send(s,wait=2):
  x('type','--clearmodifiers','--delay','3',s); x('key','--clearmodifiers','Return');time.sleep(wait)
 try:
  time.sleep(.5)
  with (OUT/'gui.log').open('w') as log:
   app=subprocess.Popen([str(ROOT/'build/relay'),'--clean-shell','--workspace',str(p/'work')],env=env,stdout=log,stderr=log)
   time.sleep(5)
   windows=x('search','--pid',str(app.pid)).splitlines()
   win=max(windows,key=lambda w:int(x('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
   x('windowsize',win,'1200','900'); x('windowfocus','--sync',win); time.sleep(1)
   send("!printf 'LOCAL-ONE\\nLOCAL-TWO\\n'");shot('01-local')
   send("!ssh -t localhost 'HISTFILE=/dev/null bash --noprofile --norc -i'",7);shot('02-ssh-idle')
   send("!printf 'REMOTE-ONE\\nREMOTE-TWO\\n'");shot('03-remote-command')
   send('!cd /tmp; pwd; false');shot('04-remote-failure-cwd')
   x('type','--clearmodifiers','--delay','3','cat LOCAL_ONLY_SSH_AU'); x('key','Tab'); time.sleep(.5);shot('04b-local-completion-in-ssh');x('key','ctrl+a','BackSpace')
   send('!sleep 7',1);shot('05-remote-busy');time.sleep(7);shot('06-remote-idle-again')
   send("!read -r -p 'INPUT> ' value; printf 'RECEIVED=%s\\n' \"$value\"",1)
   send('!echo SHOULD-BE-A-COMMAND');shot('07-input-consumed')
   send('!exit',3);shot('08-local-again')
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
