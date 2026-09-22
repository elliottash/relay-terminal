"""Transparent stdin tee into the real worker; isolated fake-key QA only."""
import json, os, subprocess, sys, threading, time
p = subprocess.Popen([sys.executable, '-u', os.environ['QA_REAL_WORKER']], stdin=subprocess.PIPE)
def forward():
    for line in sys.stdin.buffer:
        try:
            msg = json.loads(line)
            if msg.get('type') in ('configure', 'ask', 'set_agent_options', 'board_ask'):
                with open(os.environ['QA_WIRE'], 'a') as f:
                    f.write(json.dumps({'time':time.time(),'pid':os.getpid(),'message':msg})+'\n')
        except Exception:
            pass
        p.stdin.write(line); p.stdin.flush()
    p.stdin.close()
threading.Thread(target=forward, daemon=True).start()
sys.exit(p.wait())
