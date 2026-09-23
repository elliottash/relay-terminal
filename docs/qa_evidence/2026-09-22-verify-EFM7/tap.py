"""Byte-for-byte stdio forwarding to the real worker; record selected nonsecret fields only."""
import os,sys,json,subprocess,threading,time
from pathlib import Path
root=Path(os.environ['EFM7_SOURCE'])
p=subprocess.Popen([sys.executable,str(root/'backend/worker.py')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=sys.stderr)
fields={'type','event','id','model','preset','effort','guest_effort','applied','text','error','message','agent_role','guest_harness','harness','mode'}
events={'configure','configured','set_model','model_changed','set_effort','effort_changed','ask','agent_started','agent_done','error','assistant_delta','assistant','notice'}
def record(line,direction):
 try:
  obj=json.loads(line)
  if obj.get('event',obj.get('type')) not in events:return
  clean={k:v for k,v in obj.items() if k in fields}
  if isinstance(obj.get('guest'),dict):clean['guest']={k:v for k,v in obj['guest'].items() if k in {'id','model','effort','permissions'}}
  if isinstance(obj.get('extra'),dict):clean['extra']={k:v for k,v in obj['extra'].items() if k in {'reasoning','reasoning_effort','thinking','enable_thinking'}}
  with open(os.environ['EFM7_EVENTS'],'a') as f:f.write(json.dumps({'time':time.time(),'pid':os.getpid(),'direction':direction,**clean})+'\n')
 except Exception:pass

def feed():
 for line in sys.stdin.buffer:
  record(line,'gui-to-worker');p.stdin.write(line);p.stdin.flush()
 p.stdin.close()
threading.Thread(target=feed,daemon=True).start()
for line in p.stdout:
 record(line,'worker-to-gui');sys.stdout.buffer.write(line);sys.stdout.buffer.flush()
sys.exit(p.wait())
