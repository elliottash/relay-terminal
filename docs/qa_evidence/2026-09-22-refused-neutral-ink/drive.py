#!/usr/bin/env python3
"""Live #25XG evidence: local scripted provider, real tools and GUI, isolated Xvfb."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]
REQUESTS = []

class Provider(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        messages = request.get('messages', [])
        REQUESTS.append(messages)
        (OUT / 'requests.json').write_text(json.dumps(REQUESTS, indent=2))
        results = [m for m in messages if m.get('role') == 'tool']
        if not results:
            calls = [('edit_file', {'path':'oversized.txt','old_string':'x','new_string':'y'}),
                     ('run_command', {'command':'ls /nonexistent'})]
            delta = {'role':'assistant', 'tool_calls':[
                {'index':i,'id':f'call-{i}','type':'function',
                 'function':{'name':name,'arguments':json.dumps(args)}}
                for i,(name,args) in enumerate(calls)]}
            finish = 'tool_calls'
        else:
            delta = {'role':'assistant','content':'The size guard refused the edit. The command ran and failed.'}
            finish = 'stop'
        payload = ''
        for d, f in ((delta,None), ({},finish)):
            payload += 'data: '+json.dumps({'id':'qa','object':'chat.completion.chunk',
                'model':'stub','choices':[{'index':0,'delta':d,'finish_reason':f}]})+'\n\n'
        payload += 'data: [DONE]\n\n'
        data = payload.encode()
        self.send_response(200)
        self.send_header('Content-Type','text/event-stream')
        self.send_header('Content-Length',str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    server = ThreadingHTTPServer(('127.0.0.1',0), Provider)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    children = []
    with tempfile.TemporaryDirectory(prefix='relay-25xg-') as tmp:
        root = Path(tmp)
        for name in ('config/RelayTerminal','config/relay','data','cache','run','tmp','project'):
            (root/name).mkdir(parents=True)
        (root/'run').chmod(0o700)
        bus = Path(f'/run/user/{os.getuid()}/bus')
        if bus.exists(): (root/'run/bus').symlink_to(bus)
        (root/'project/oversized.txt').write_text('x'*140000)
        (root/'config/RelayTerminal/relay.conf').write_text(
            '[instructions]\nonboarded=true\n[theme]\nname=relay-dark\n[provider]\npreset=local:stub\n[models]\ntier\\main=local:stub|stub|\n')
        (root/'config/relay/local-models.json').write_text(json.dumps({'version':1,'endpoints':[{
            'id':'local:stub','label':'Refusal QA','base_url':f'http://127.0.0.1:{server.server_port}/v1',
            'model':'stub','server':'openai-compatible','context_window':131072}]}))
        display = next(f':{n}' for n in range(160,200) if not Path(f'/tmp/.X11-unix/X{n}').exists())
        env = dict(os.environ, DISPLAY=display, XDG_CONFIG_HOME=str(root/'config'),
            XDG_DATA_HOME=str(root/'data'),XDG_CACHE_HOME=str(root/'cache'),
            XDG_RUNTIME_DIR=str(root/'run'), TMPDIR=str(root/'tmp'), RELAY_KEYRING='off')
        def run(*args): return subprocess.check_output(args,env=env,text=True)
        try:
            children.append(subprocess.Popen(['Xvfb',display,'-screen','0','1320x940x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
            time.sleep(1)
            with (root/'relay.log').open('w') as log:
                gui = subprocess.Popen([str(ROOT/'build/relay'),'--workspace',str(root/'project')],env=env,stdout=log,stderr=log)
                children.append(gui)
                time.sleep(7)
                wins = run('xdotool','search','--pid',str(gui.pid)).splitlines()
                win = max(wins,key=lambda w: len(run('xwininfo','-id',w))) if len(wins)>1 else wins[0]
                # Select the main window by its area.
                def area(w):
                    g=dict(line.split('=',1) for line in run('xdotool','getwindowgeometry','--shell',w).splitlines() if '=' in line)
                    return int(g['WIDTH'])*int(g['HEIGHT'])
                win=max(wins,key=area)
                run('xdotool','windowmove',win,'0','0','windowsize',win,'1280','880','windowfocus',win)
                run('xdotool','key','ctrl+i','ctrl+i')
                run('xdotool','type','--delay','20','Demonstrate the refusal and failure ink.')
                run('xdotool','key','Return')
                for _ in range(40):
                    if any(any(m.get('role')=='tool' for m in req) for req in REQUESTS): break
                    time.sleep(0.5)
                time.sleep(3)
                run('xdotool','mousemove','1300','920')
                run('import','-window',win,str(OUT/'live.png'))
                results=[m for req in REQUESTS for m in req if m.get('role')=='tool']
                assert any(json.loads(m['content']).get('refused') is True for m in results), (root/'relay.log').read_text()[-3000:]
                assert any(json.loads(m['content']).get('exit_code') == 2 for m in results)
                assert (root/'project/oversized.txt').read_text() == 'x'*140000
        finally:
            for proc in reversed(children):
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired: proc.kill()
            server.shutdown()
    print('Real size refusal and failed command captured; oversized file unchanged.')

if __name__ == '__main__': main()
