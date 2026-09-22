import os,json,subprocess,time,tempfile
from pathlib import Path
root=Path(tempfile.mkdtemp(prefix='relay-alt-a-'));print(root,flush=True)
env=dict(os.environ,DISPLAY=':187',XDG_CONFIG_HOME=str(root/'config'),XDG_DATA_HOME=str(root/'data'),XDG_RUNTIME_DIR=str(root/'runtime'))
(root/'runtime').mkdir(mode=0o700)
state=root/'data/relay/state/windows.json';state.parent.mkdir(parents=True)
pane={'pane':{'cwd':'/tmp','workspace':'/tmp','engine':'relay','scrollback':'alt-a-owner'}}
sub={'subagents':{'owner':'alt-a-owner','cwd':'/tmp','current':'a1','tabs':[{'id':'a1','type':'general','description':'Alt+A check','status':'done','text':'Saved test transcript'}]}}
state.write_text(json.dumps({'version':1,'windows':[{'geometry':[0,0,1200,800],'current':0,'tabs':[{'split':'h','children':[pane,sub],'sizes':[600,600]}]}]}))
x=subprocess.Popen(['Xvfb',':187','-screen','0','1280x900x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
app=None
try:
 time.sleep(.5)
 app=subprocess.Popen(['build/relay'],env=env,stdout=open(root/'stdout','w'),stderr=open(root/'stderr','w'))
 time.sleep(4)
 def xd(*args):return subprocess.check_output(['xdotool',*args],env=env,text=True).strip()
 wid=xd('search','--onlyvisible','--class','relay').splitlines()[-1];xd('windowfocus',wid)
 xd('mousemove','900','450','click','1');time.sleep(.4)
 def count():
  time.sleep(1.5)
  s=json.loads(state.read_text())
  return json.dumps(s).count('"subagents"')
 assert count()==1,state.read_text()
 xd('key','alt+a');assert count()==0,state.read_text()
 print('PASS: actual Alt+A removes the active subagent pane from the saved layout',flush=True)
 xd('key','ctrl+shift+z');assert count()==1,state.read_text()
 print('PASS: restore closed brings the subagent view back',flush=True)
 xd('mousemove','200','450','click','1');xd('key','alt+a');assert count()==1,state.read_text()
 xd('key','alt+a');assert count()==0,state.read_text()
 print('PASS: Alt+A from the main pane focuses the subagent view, then closes it on the next press',flush=True)
finally:
 if app:app.terminate();app.wait(timeout=10)
 x.terminate();x.wait()
