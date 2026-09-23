"""Real GUI/shell lifecycle checks with an isolated, deterministic agent worker."""
import json, os, signal, socket, subprocess, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
sandbox = Path(tempfile.mkdtemp(prefix='pcbg-live-'))
env = dict(os.environ)
for key, name in [('XDG_CONFIG_HOME','config'), ('XDG_DATA_HOME','data'),
                  ('XDG_CACHE_HOME','cache'), ('XDG_RUNTIME_DIR','run'), ('TMPDIR','tmp')]:
    path = sandbox/name; path.mkdir(mode=0o700); env[key] = str(path)
conf = sandbox/'config/RelayTerminal'; conf.mkdir()
(conf/'relay.conf').write_text('[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n'
    '[models]\ntier\\main=guest:codex|fixture|high\ntier\\high=guest:codex|fixture|high\n'
    '[theme]\nname=relay-dark\n[windows]\nrestore=false\n')
fixture = sandbox/'fixture'; (fixture/'backend').mkdir(parents=True)
(fixture/'backend/worker.py').write_text(r'''
import json, os, sys, subprocess, threading, time
lock=threading.Lock(); child=None; current=None; running=False
def emit(**e):
 with lock: print(json.dumps(e),flush=True)
def log(r):
 with open(os.environ['PCBG_REQUESTS'],'a') as f: f.write(json.dumps(dict(pid=os.getpid(),**r))+'\n')
def finish():
 global running
 if running:
  running=False
  emit(event='agent_finished',id=current,outcome='completed',turn_id='fixture')
def progress():
 while True:
  time.sleep(.25)
  if running:
   emit(event='delta',text='PCBG progress\n'); log(dict(type='progress'))
threading.Thread(target=progress,daemon=True).start()
emit(event='ready')
try:
 for line in sys.stdin:
  r=json.loads(line); k=r.get('type'); ident=r.get('id'); log(r)
  if k=='shutdown': break
  if k=='presets': emit(event='presets',presets=[dict(id='guest:codex',label='Fixture',group='guest',harness=True,model='fixture',models=[dict(id='fixture',label='Fixture')])])
  elif k=='configure': emit(event='configured',model='fixture',mode='build',session_id='fixture-'+str(os.getpid()),agent_role='main',effort='high')
  elif k=='route': emit(event='route',id=ident,route='shell',text=r.get('text',''),valid=True)
  elif k=='ask':
   current=ident; running=True
   emit(event='agent_started',id=ident,turn_id='fixture')
   emit(event='delta',text='PCBG agent is working.\n')
   if 'jobonly' in r.get('text',''):
    child=subprocess.Popen(['sleep','180']); log(dict(type='child',child=child.pid))
    emit(event='jobs',jobs=[dict(job_id='job-1',command='sleep 180',running=True)])
    finish()
   elif 'finishsoon' in r.get('text',''): threading.Timer(2,finish).start()
  elif k=='cancel': finish()
  elif k=='poll' and running: emit(event='delta',text='PCBG progress\n')
finally:
 if child:
  child.terminate(); child.wait(timeout=5)
 log(dict(type='exited'))
''')
for name in ['shell','scripts','assets']: (fixture/name).symlink_to(ROOT/name)
display = next(':'+str(n) for n in range(460,500) if not Path('/tmp/.X11-unix/X'+str(n)).exists())
env.update(DISPLAY=display, RELAY_KEYRING='off', RELAY_NO_ISOLATION='1', RELAY_DATA_DIR=str(fixture),
           RELAY_QA_RECTS=str(sandbox/'rects.json'), PCBG_REQUESTS=str(sandbox/'requests.jsonl'))
env.pop('RELAY_OPEN_SOCKET',None)
binary = os.environ.get('RELAY_TEST_BINARY',str(ROOT/'build/relay'))
x = subprocess.Popen(['Xvfb',display,'-screen','0','1500x1000x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(1)
log = (sandbox/'stderr.log').open('w')
app = subprocess.Popen([binary,'--workspace',str(sandbox),'--clean-shell','--fresh'],env=env,stdout=log,stderr=log)
results = []
def wait(f, timeout=12):
    end = time.monotonic()+timeout
    while time.monotonic()<end:
        value=f()
        if value: return value
        time.sleep(.1)
    raise AssertionError('Timed out in '+str(sandbox))
def xd(*args): return subprocess.check_output(['xdotool',*map(str,args)],env=env,text=True,stderr=subprocess.DEVNULL).strip()
def key(*keys): xd('key','--clearmodifiers',*keys); time.sleep(.35)
def submit(text): xd('type','--clearmodifiers','--delay','1',text); key('Return'); time.sleep(.8)
def records():
    p=sandbox/'requests.jsonl'
    return [json.loads(line) for line in p.read_text().splitlines()] if p.exists() else []
def visible(title):
    try: return xd('search','--onlyvisible','--name',title).splitlines()
    except subprocess.CalledProcessError: return []
def modal(): return visible('^Work is still running$')
def rects():
    try: return json.loads((sandbox/'rects.json').read_text())
    except (FileNotFoundError,json.JSONDecodeError): return {}
def click_named(name):
    rect=wait(lambda: rects().get(name))
    xd('mousemove',rect['x']+rect['w']//2,rect['y']+rect['h']//2); xd('click',1); time.sleep(.6)
def shot(name): subprocess.run(['import','-window','root',str(OUT/name)],env=env,check=True)
def choose(name):
    dialog=wait(modal)[-1]
    focus(dialog)
    tmp=sandbox/'dialog.png'
    subprocess.run(['import','-window',dialog,str(tmp)],env=env,check=True)
    tsv=subprocess.check_output(['tesseract',str(tmp),'stdout','tsv'],stderr=subprocess.DEVNULL,text=True)
    word='stop' if name=='closeStopJob' else 'continue'
    row=[r.split('\t') for r in tsv.splitlines()[1:] if len(r.split('\t'))>=12 and r.split('\t')[11]==word][-1]
    geometry=dict(line.split('=',1) for line in xd('getwindowgeometry','--shell',dialog).splitlines())
    xd('mousemove',int(geometry['X'])+int(row[6])+int(row[8])//2,
       int(geometry['Y'])+int(row[7])+int(row[9])//2)
    xd('click',1); wait(lambda:not modal())
def alive(pid):
    try:
        # A terminated child may await its parent's next reap; zombies are not running work.
        return Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[0] != 'Z'
    except FileNotFoundError: return False
def focus(win): xd('windowfocus',win); time.sleep(.3)
def drive(op,name=''):
    address=(sandbox/'run/relay/open-socket').read_text().strip()
    with socket.socket(socket.AF_UNIX) as conn:
        conn.settimeout(5); conn.connect(address)
        conn.sendall((json.dumps(dict(type='drive',op=op,name=name))+'\n').encode())
        return json.loads(conn.makefile('rb').readline())
try:
    wait(lambda: visible('Relay'))
    time.sleep(3)
    wins=xd('search','--onlyvisible','--pid',app.pid).splitlines()
    win=max(wins,key=lambda w:int(xd('getwindowgeometry','--shell',w).split('WIDTH=')[1].splitlines()[0]))
    xd('windowmove',win,0,0); xd('windowsize',win,1440,900); focus(win)
    # Real shell job, cancelled close, then stop via the mouse close button.
    submit('!sleep 180 & echo $! > '+str(sandbox/'shell.pid'))
    shellpid=int(wait(lambda:(sandbox/'shell.pid').read_text().strip() if (sandbox/'shell.pid').exists() else ''))
    key('ctrl+w'); wait(modal); shot('modal.png'); key('Escape'); wait(lambda:not modal())
    assert alive(shellpid); results.append('Ctrl+W detects a shell background job; Escape preserves it')
    focus(win); xd('mousemove',1000,80); time.sleep(.8)
    close=wait(lambda:next((r for n,r in rects().items() if n.startswith('paneChromeButton') and r.get('text')=='×'),None))
    xd('mousemove',close['x']+close['w']//2,close['y']+close['h']//2); xd('click',1)
    wait(modal); results.append('Pane × invokes the same active-job dialog')
    # Background the last pane, preserving the real child process.
    choose('closeBackgroundJob'); assert alive(shellpid); assert app.poll() is None
    results.append('Backgrounding last pane preserves shell child and leaves Relay reachable')
    focus(win)
    # Open Sessions and select its Background tab by the real tab text/rectangle.
    key('ctrl+shift+p'); time.sleep(2)
    # Click the actual Background tab using OCR, not assumed widget state.
    shot('background.png')
    def ocr_click(word):
        tmp=sandbox/'screen.png'
        subprocess.run(['import','-window','root',str(tmp)],env=env,check=True)
        tsv=subprocess.check_output(['tesseract',str(tmp),'stdout','tsv'],stderr=subprocess.DEVNULL,text=True)
        rows=[r.split('\t') for r in tsv.splitlines()[1:]]
        row=[r for r in rows if len(r)>=12 and r[11]==word][-1]
        xd('mousemove',int(row[6])+int(row[8])//2,int(row[7])+int(row[9])//2); xd('click',1); time.sleep(.8)
    ocr_click('Background'); shot('background.png')
    old_windows=set(xd('search','--onlyvisible','--pid',app.pid).splitlines())
    ocr_click('Reopen'); time.sleep(1)
    reopened=wait(lambda:next(iter(set(xd('search','--onlyvisible','--pid',app.pid).splitlines())-old_windows),None))
    focus(reopened)
    assert alive(shellpid)
    results.append('Sessions → Background reopens the original running shell job')
    # The reopened pane is now in its own visible window. Ctrl+W stop kills its shell job.
    key('ctrl+w'); choose('closeStopJob'); wait(lambda:not alive(shellpid))
    results.append('Close and stop job terminates the real shell background child')
    focus(win); xd('mousemove',150,830); xd('click',1)
    submit('*hold')
    worker=wait(lambda:next((r['pid'] for r in reversed(records()) if r.get('type')=='ask' and 'hold' in r.get('text','')),None))
    key('ctrl+w'); wait(modal); key('Return'); wait(lambda:not modal())
    assert alive(worker); results.append('An active agent turn prompts; default Enter cancels')
    key('ctrl+w'); choose('closeBackgroundJob'); assert alive(worker)
    before=len([r for r in records() if r['pid']==worker and r.get('type')=='progress'])
    time.sleep(2)
    after=len([r for r in records() if r['pid']==worker and r.get('type')=='progress'])
    assert after>before and not any(r['pid']==worker and r.get('type') in ('cancel','shutdown') for r in records())
    results.append('Backgrounding agent preserves worker PID and continued output without cancel/shutdown')
    focus(win); key('ctrl+t'); submit('*jobonly')
    child=wait(lambda:next((r['child'] for r in reversed(records()) if r.get('type')=='child'),None))
    key('ctrl+w'); wait(modal); choose('closeStopJob'); wait(lambda:not alive(child))
    results.append('Worker job still prompts after agent finishes; Stop shuts down child')
    focus(win)
    # Closing the final visible window warns about retained background sessions.
    key('ctrl+w'); wait(lambda:visible('^Close window\\?$')); key('Escape')
    assert alive(worker); results.append('Final-window exit warns about background sessions; Cancel keeps them alive')
    (OUT/'live-results.json').write_text(json.dumps(dict(binary=binary,sandbox=str(sandbox),passed=results),indent=2)+'\n')
    print(json.dumps(results,indent=2))
finally:
    if app.poll() is None:
        app.terminate()
        try: app.wait(timeout=15)
        except subprocess.TimeoutExpired: app.kill(); app.wait()
    x.terminate(); x.wait(timeout=5)
    if not (OUT/'live-results.json').exists():
        print(json.dumps(dict(sandbox=str(sandbox),passed=results)))
