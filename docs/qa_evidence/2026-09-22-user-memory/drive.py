#!/usr/bin/env python3
"""Isolated live GUI/interview plumbing check; scripted local model, no external API."""
import json
import os
from pathlib import Path
import subprocess as sp
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]
REQUESTS = []
MEMORY = ('---\ntype: memory\nstatus: active\nname: explanation-style\nscope: user\n'
          'pinned: true\npaths: []\n---\n# Explanation style\n\nI prefer concise explanations.\n')


class Provider(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        REQUESTS.append(request)
        messages = request.get('messages', [])
        latest = max((i for i, m in enumerate(messages) if m.get('role') == 'user'), default=0)
        text = str(messages[latest].get('content', ''))
        results = [m for m in messages[latest:] if m.get('role') == 'tool']
        if not results:
            args = (dict(action='save', text=MEMORY, base_hash='') if 'Remember that I prefer' in text
                    else dict(action='list'))
            delta = {'role': 'assistant', 'tool_calls': [{'index': 0, 'id': 'memory-call',
                     'type': 'function', 'function': {'name': 'app_user_memory', 'arguments': json.dumps(args)}}]}
            finish = 'tool_calls'
        else:
            answer = ('Saved your preference. Review it in Globals > User memory.' if 'Remember that I prefer' in text
                      else 'What kind of work would you most like Relay to help you with? You can skip or stop at any time.')
            delta, finish = {'role': 'assistant', 'content': answer}, 'stop'
        data = ''.join('data: ' + json.dumps({'id': 'qa', 'object': 'chat.completion.chunk',
            'model': 'stub', 'choices': [{'index': 0, 'delta': d, 'finish_reason': f}]}) + '\n\n'
            for d, f in ((delta, None), ({}, finish))) + 'data: [DONE]\n\n'
        payload = data.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def main():
    server = ThreadingHTTPServer(('127.0.0.1', 0), Provider)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    children = []
    with tempfile.TemporaryDirectory(prefix='relay-memory-qa-') as tmp:
        root = Path(tmp)
        for folder in ('home', 'config/RelayTerminal', 'config/relay', 'data', 'cache', 'run', 'tmp', 'project'):
            (root / folder).mkdir(parents=True)
        (root / 'run').chmod(0o700)
        bus = Path(f'/run/user/{os.getuid()}/bus')
        if bus.exists(): (root / 'run/bus').symlink_to(bus)
        (root / 'config/RelayTerminal/relay.conf').write_text(
            '[instructions]\nonboarded=true\n[theme]\nname=relay-dark\n[provider]\npreset=local:stub\n'
            '[models]\ntier\\main=local:stub|stub|\n[security]\napprovals_chosen=true\napprovals_ask=@Invalid()\n')
        (root / 'config/relay/local-models.json').write_text(json.dumps({'version': 1, 'endpoints': [{
            'id': 'local:stub', 'label': 'Memory QA', 'base_url': f'http://127.0.0.1:{server.server_port}/v1',
            'model': 'stub', 'server': 'openai-compatible', 'context_window': 131072}]}))
        display = next(f':{n}' for n in range(160,200) if not Path(f'/tmp/.X11-unix/X{n}').exists())
        env = dict(os.environ, DISPLAY=display, HOME=str(root / 'home'), XDG_CONFIG_HOME=str(root / 'config'),
                   XDG_DATA_HOME=str(root / 'data'), XDG_CACHE_HOME=str(root / 'cache'),
                   XDG_RUNTIME_DIR=str(root / 'run'), TMPDIR=str(root / 'tmp'), RELAY_KEYRING='off',
                   RELAY_GLOBAL_SWITCHBOARD=str(root / 'config/relay/switchboard'))
        def run(*args): return sp.check_output(args, env=env, text=True).strip()
        def key(chord): run('xdotool', 'key', '--clearmodifiers', chord); time.sleep(1)
        def click(x, y): run('xdotool', 'mousemove', str(x), str(y), 'click', '1'); time.sleep(1)
        try:
            children.append(sp.Popen(['Xvfb', display, '-screen', '0', '1400x1000x24'], stdout=sp.DEVNULL, stderr=sp.DEVNULL))
            time.sleep(1)
            with (root / 'relay.log').open('w') as log:
                gui = sp.Popen([str(ROOT / 'build/relay'), '--workspace', str(root / 'project'), '--fresh', '--clean-shell'],
                               env=env, stdout=log, stderr=log)
                children.append(gui)
                time.sleep(7)
                wins = run('xdotool', 'search', '--pid', str(gui.pid), '--name', 'Relay').splitlines()
                def area(win):
                    data = dict(line.split('=', 1) for line in run('xdotool', 'getwindowgeometry', '--shell', win).splitlines() if '=' in line)
                    return int(data['WIDTH']) * int(data['HEIGHT'])
                win = max(wins, key=area)
                run('xdotool', 'windowmove', win, '0', '0', 'windowsize', win, '1400', '1000', 'windowfocus', win)
                key('ctrl+shift+g'); time.sleep(2)
                run('import', '-window', win, str(OUT / '01-user-memory.png'))
                # Fixed viewport coordinates, recorded so the drive is repeatable.
                click(890, 290)
                for _ in range(40):
                    if len(REQUESTS) >= 2: break
                    time.sleep(.5)
                run('import', '-window', win, str(OUT / '02-interview.png'))
                assert len(REQUESTS) >= 2, 'Interview did not reach the local model'
                assert any('ONE short' in str(m) for r in REQUESTS for m in r.get('messages', [])), 'Missing interview brief'
                run('xdotool', 'type', '--delay', '5', 'Remember that I prefer concise explanations.')
                key('Return')
                for _ in range(40):
                    if list((root / 'config/relay/switchboard/memory').glob('*.md')): break
                    time.sleep(.5)
                files = list((root / 'config/relay/switchboard/memory').glob('*.md'))
                assert len(files) == 1, str(REQUESTS[-1])[-1000:]
                assert 'I prefer concise explanations.' in files[0].read_text()
                time.sleep(2)
                click(1346, 290)
                click(930, 325)
                run('import', '-window', win, str(OUT / '03-saved.png'))
                result = {'interview_started': True, 'globals_brief_present': True,
                          'user_memory_tool_succeeded': True,
                          'saved_user_memory': True, 'provider': 'scripted localhost stub; no external API',
                          'limitation': 'Validates integration, not model interview quality.'}
                (OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
                print(json.dumps(result), flush=True)
        finally:
            for proc in reversed(children):
                proc.terminate()
                try: proc.wait(timeout=5)
                except sp.TimeoutExpired: proc.kill()
            server.shutdown()


if __name__ == '__main__': main()
