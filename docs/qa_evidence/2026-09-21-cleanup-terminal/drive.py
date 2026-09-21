import os,json,tempfile,subprocess,time,pathlib,shutil
root=pathlib.Path('/home/elliott/repos/relay-terminal')
p=pathlib.Path(tempfile.mkdtemp(prefix='relay-cleanup-'))
for name in ['config/RelayTerminal','data/backend','runtime','workspace/issues','cache']:(p/name).mkdir(parents=True)
(p/'runtime').chmod(0o700)
(p/'config/RelayTerminal/relay.conf').write_text('[instructions]\nonboarded=true\n[theme]\nname=relay-dark\n')
(p/'workspace/issues/board.yaml').write_text('tabs: [{id: features, folder: features}]\ncolumns: [inbox, done]\n')
shutil.copy('/tmp/cleanup-worker.py',p/'data/backend/worker.py')
for name in ['shell','themes']:
 if (root/name).exists():(p/'data'/name).symlink_to(root/name,target_is_directory=True)
e=os.environ.copy();e.update(XDG_CONFIG_HOME=str(p/'config'),XDG_DATA_HOME=str(p/'share'),XDG_CACHE_HOME=str(p/'cache'),XDG_RUNTIME_DIR=str(p/'runtime'),RELAY_DATA_DIR=str(p/'data'),RELAY_QA_RECTS=str(p/'rects.json'),RELAY_KEYRING='off')
def run(*cmd):return subprocess.check_output(cmd,env=e,text=True).strip()
log=open(p/'relay.log','w')
app=subprocess.Popen([str(root/'build/relay'),'--fresh','--workspace',str(p/'workspace')],env=e,stdout=log,stderr=log)
try:
 time.sleep(4)
 win=run('xdotool','search','--onlyvisible','--pid',str(app.pid)).splitlines()[0]
 run('xdotool','windowsize',win,'1450','1000');run('xdotool','windowfocus',win)
 run('xdotool','key','ctrl+shift+s');time.sleep(4)
 rows=json.loads((p/'rects.json').read_text()); b=rows['boardCleanup']
 run('xdotool','mousemove',str(b['x']+b['w']//2),str(b['y']+b['h']//2),'click','1');time.sleep(2)
 rows=json.loads((p/'rects.json').read_text())
 b=next(v for v in rows.values() if 'Apply' in v.get('text','') and v.get('w',0)>0)
 run('xdotool','mousemove',str(b['x']+b['w']//2),str(b['y']+b['h']//2),'click','1');time.sleep(2)
 run('xdotool','windowsize',win,'1451','1000');time.sleep(.5)
 run('import','-window',win,'/tmp/cleanup-live.png')
 print('Fixture:',p);print(run('tesseract','/tmp/cleanup-live.png','stdout','--psm','11'))
finally:
 app.terminate()
 try:app.wait(timeout=5)
 except subprocess.TimeoutExpired:app.kill()
 log.close()
