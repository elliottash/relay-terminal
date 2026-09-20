#!/usr/bin/env python3
"""Ctrl+Enter on an empty prompt box always sends Continue, 2026-09-20 (#SXF1): live check.

Drives the landed build against a mock OpenAI-compatible server on loopback, with an isolated
profile whose only provider is a local endpoint pointing at the mock. No network, no real key.

The rule this checks (owner, 2026-09-20: "ctrl+enter in an empty prompt should always send agent
prompt 'continue'"): an empty prompt box with an idle agent sends the ordinary prompt `Continue`,
whatever the last turn did — the limit/cut-off gate the card first landed is gone.

  D  an ORDINARY turn that finished normally: with the box empty, Ctrl+Enter sends "Continue"
     (the case the owner hit, where the old rule answered "Type a prompt first.")
  F  text in the box is still sent as itself: Ctrl+Enter with "hello there" typed reaches the
     model as "hello there", never as Continue
  A  the limit case still works: a turn that hits its step limit, empty box, Ctrl+Enter → Continue
  H  the slow path teaches the fast one: after /continue the hint names Ctrl+Enter and the new
     wording ("send Continue from an empty prompt box")
  G  a busy agent is untouched: while the /continue turn from H is still hanging, Ctrl+Enter on
     the empty box sends nothing (no model call), keeping the busy answer

Needs Xvfb, xdotool, ImageMagick, tesseract.

  Xvfb :161 -screen 0 1400x900x24 &
  DISPLAY=:161 python3 drive.py [path-to-relay-binary]

Writes implementer-*.png, mockserver.log, relay.out next to itself; prints PASS/FAIL.
"""
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = sys.argv[1] if len(sys.argv) > 1 else '/tmp/claude-1000/land/relay-sxf1-always/verify/build/relay'
RUN = '/tmp/claude-1000/sxf1-always-live'
PORT = 8793
DISPLAY = os.environ.get('DISPLAY', ':161')
STEP_LIMIT = 2

fails = []
server_log = []


def check(name, ok, detail=''):
    line = ('PASS ' if ok else 'FAIL ') + name + ((' — ' + detail) if detail else '')
    print(line, flush=True)
    if not ok:
        fails.append(name)


def run(cmd, **kw):
    return subprocess.run(cmd, check=False, text=True, capture_output=True, timeout=kw.pop('timeout', 90), **kw)


def xdo(*args):
    return run(['xdotool', *args])


# ---------------------------------------------------------------- the mock model server

class MockState:
    mode = 'text'           # 'tools' (one tool call per step) | 'text' (a finished turn) | 'hang'
    requests = []           # {'mode', 'users': [every user message in the request], 'at'}
    lock = threading.Lock()

    @classmethod
    def record(cls, users, side):
        with cls.lock:
            cls.requests.append({'mode': ('side' if side else cls.mode), 'users': users,
                                 'at': time.time()})
            return len(cls.requests) - 1

    @classmethod
    def tail_after(cls, requests, marker):
        # The last user message of every main (non-side) request at or after `marker`, which is a
        # COUNT taken before the action under test (`MockState.count()`), so the next request is
        # `requests[marker]` itself — not `marker + 1`.
        return [r['users'][-1] for i, r in enumerate(requests)
                if i >= marker and r['users'] and isinstance(r['users'][-1], str) and r['mode'] != 'side']

    @classmethod
    def wait_for(cls, predicate, timeout):
        # The predicate runs on a snapshot, OUTSIDE the lock: a plain Lock is not reentrant, and
        # calling a lock-taking helper from inside `with cls.lock` deadlocks the drive and freezes
        # every mock handler mid-record.
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with cls.lock:
                requests = list(cls.requests)
            if predicate(requests):
                return True
            time.sleep(0.2)
        return False

    @classmethod
    def count(cls):
        with cls.lock:
            return len(cls.requests)


def tool_call_response():
    body = {'id': 'mock', 'object': 'chat.completion', 'model': 'mock-opus',
            'choices': [{'index': 0, 'finish_reason': 'tool_calls', 'message': {
                'role': 'assistant', 'content': '',
                'tool_calls': [{'id': 'c1', 'type': 'function',
                                'function': {'name': 'run_command',
                                             'arguments': '{"command": "printf hi"}'}}]}}]}
    return json.dumps(body).encode()


def text_response():
    body = {'id': 'mock', 'object': 'chat.completion', 'model': 'mock-opus',
            'choices': [{'index': 0, 'finish_reason': 'stop', 'message': {
                'role': 'assistant', 'content': 'Turn finished.'}}]}
    return json.dumps(body).encode()


class Handler(BaseHTTPRequestHandler):
    timeout = 60          # a dead client must not hold a handler thread forever

    def log_message(self, fmt, *args):
        server_log.append(fmt % args)

    def do_POST(self):
        try:
            self._handle()
        except Exception:
            import traceback
            with open(os.path.join(HERE, 'mock-errors.log'), 'a', encoding='utf-8') as f:
                f.write(traceback.format_exc())

    def _handle(self):
        if not self.path.endswith('/chat/completions'):
            self.send_error(404)
            return
        length = int(self.headers.get('Content-Length') or 0)
        payload = json.loads(self.rfile.read(length) or b'{}')
        users = [m.get('content') for m in payload.get('messages', []) if m.get('role') == 'user']
        side = not payload.get('tools')          # a side call (recap, title) never gets tools
        MockState.record(users, side)
        mode = MockState.mode
        if mode == 'hang' and not side:
            time.sleep(150)                      # the turn is still running while we press keys
        body = tool_call_response() if (mode == 'tools' and not side) else text_response()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.send_error(404)


def start_server():
    server = ThreadingHTTPServer(('127.0.0.1', PORT), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


# ---------------------------------------------------------------- profile, launch, drive

def isolate():
    shutil.rmtree(RUN, ignore_errors=True)
    for name in ('home', 'config/relay', 'config/RelayTerminal', 'data', 'run', 'tmp', 'workspace'):
        os.makedirs(os.path.join(RUN, name), exist_ok=True)
    with open(os.path.join(RUN, 'config/RelayTerminal/relay.conf'), 'w', encoding='utf-8') as f:
        f.write('[General]\nprovider/preset=local:mockopus\nagent/max_steps=%d\n\n'
                '[instructions]\nonboarded=true\n\n[isolation]\nenabled=false\n' % STEP_LIMIT)
    with open(os.path.join(RUN, 'config/relay/local-models.json'), 'w', encoding='utf-8') as f:
        json.dump({'endpoints': [{'id': 'local:mockopus', 'label': 'Mock Opus (local)',
                                  'base_url': 'http://127.0.0.1:%d/v1' % PORT, 'model': 'mock-opus',
                                  'server': 'openai-compatible', 'context_window': 32768}]}, f)
    env = dict(os.environ)
    env.update({'HOME': os.path.join(RUN, 'home'), 'XDG_CONFIG_HOME': os.path.join(RUN, 'config'),
                'XDG_DATA_HOME': os.path.join(RUN, 'data'), 'XDG_RUNTIME_DIR': os.path.join(RUN, 'run'),
                'TMPDIR': os.path.join(RUN, 'tmp'), 'RELAY_KEYRING': 'off',
                'RELAY_DATA_DIR': os.path.join(RUN, 'data'), 'DISPLAY': DISPLAY,
                'QT_QPA_PLATFORM': 'xcb'})
    for name in ('RELAY_ENGINE_CORE', 'PYTHONPATH'):
        env.pop(name, None)
    return env


def launch(env):
    out = open(os.path.join(HERE, 'relay.out'), 'ab')
    return subprocess.Popen([BIN], env=env, cwd=os.path.join(RUN, 'workspace'),
                            stdout=out, stderr=subprocess.STDOUT)


def window_of(proc, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ids = xdo('search', '--onlyvisible', '--pid', str(proc.pid)).stdout.split()
        ids = [i for i in ids if i]
        if ids:
            return ids[0]
        time.sleep(0.4)
    return None


def activate(win):
    xdo('windowactivate', '--sync', win)
    time.sleep(0.3)


def shot(name, win):
    path = os.path.join(HERE, 'implementer-%s.png' % name)
    run(['import', '-window', win, path])
    return path


def composer_click(win):
    geo = xdo('getwindowgeometry', '--shell', win).stdout
    values = dict(line.split('=', 1) for line in geo.splitlines() if '=' in line)
    w, h = int(values['WIDTH']), int(values['HEIGHT'])
    xdo('mousemove', '--window', win, str(w // 2), str(h - 70), 'click', '1')
    time.sleep(0.25)


def type_submit(text):
    xdo('type', '--delay', '40', text)
    time.sleep(0.3)
    xdo('key', '--clearmodifiers', 'Return')


def ctrl_enter():
    xdo('key', '--clearmodifiers', 'ctrl+Return')


def ocr_bottom(png, lines=16):
    crop = png + '.crop'
    run(['convert', png, '-gravity', 'South', '-crop', '100%%x%d+0+8' % (lines * 18), crop])
    text = run(['tesseract', crop, '-', '--psm', '6']).stdout
    os.path.exists(crop) and os.remove(crop)
    return text


def main():
    server = start_server()
    env = isolate()
    proc = launch(env)
    win = window_of(proc)
    check('window came up', bool(win))
    if not win:
        return 1
    activate(win)
    time.sleep(8)                                   # worker start, configure, session start
    shot('01-start', win)

    # ---- D: an ORDINARY finished turn, empty box, Ctrl+Enter --------------------------
    MockState.mode = 'text'
    composer_click(win)
    marker = MockState.count()
    type_submit('finish quickly')
    ok = MockState.wait_for(lambda rs: MockState.tail_after(rs, marker), 30)
    check('D: an ordinary turn ran and finished (no limit, no cut-off)', ok)
    time.sleep(6)                                   # the turn ends, the box clears
    shot('02-ordinary-turn-done', win)
    composer_click(win)
    marker = MockState.count()
    ctrl_enter()
    ok = MockState.wait_for(lambda rs: any(t.rstrip().endswith('Continue')
                                           for t in MockState.tail_after(rs, marker)), 20)
    check('D: Ctrl+Enter on the empty box sent the prompt "Continue" after an ordinary turn', ok)
    time.sleep(2.5)
    ordinary = ocr_bottom(shot('03-continue-after-ordinary', win), lines=20)
    check('D: no "Type a prompt first." status on screen', 'Type a prompt first' not in ordinary)
    time.sleep(5)                                   # the continued turn finishes; the agent is idle

    # ---- F: text in the box is sent as itself ------------------------------------------
    MockState.mode = 'text'
    composer_click(win)
    marker = MockState.count()
    type_submit('hello there')
    ok = MockState.wait_for(lambda rs: any(t.rstrip().endswith('hello there')
                                           for t in MockState.tail_after(rs, marker)), 25)
    check('F: Ctrl+Enter with text in the box sent that text, not Continue', ok)
    with MockState.lock:
        tails = [r['users'][-1].rstrip() for r in MockState.requests[marker:] if r['users']]
    check('F: the request was the typed line and never Continue',
          bool(tails) and all(not t.endswith('Continue') for t in tails))
    time.sleep(6)                                   # that turn ends before the next one starts

    # ---- A: the limit case still continues --------------------------------------------
    MockState.mode = 'tools'
    composer_click(win)
    marker = MockState.count()
    type_submit('count carefully')
    ok = MockState.wait_for(lambda rs: len(MockState.tail_after(rs, marker)) >= STEP_LIMIT, 30)
    check('A: a turn ran to its step limit (%d model calls)' % STEP_LIMIT, ok)
    time.sleep(5)                                   # done {stop_reason: limit} lands, box clears
    shot('04-limit', win)
    MockState.mode = 'text'                         # the continued turn finishes at once
    composer_click(win)
    marker = MockState.count()
    ctrl_enter()
    ok = MockState.wait_for(lambda rs: any(t.rstrip().endswith('Continue')
                                           for t in MockState.tail_after(rs, marker)), 20)
    check('A: Ctrl+Enter on the empty box sent "Continue" after the step limit', ok)
    time.sleep(6)
    shot('05-continue-after-limit', win)

    # ---- H: the slow path (/continue) teaches the fast one ----------------------------
    MockState.mode = 'tools'
    composer_click(win)
    marker = MockState.count()
    type_submit('two more steps')
    MockState.wait_for(lambda rs: len(MockState.tail_after(rs, marker)) >= STEP_LIMIT, 30)
    time.sleep(1)
    composer_click(win)
    # Type /continue at once — a non-empty editor stops the idle-tip timer (4 s after a turn
    # ends), whose showing would open ShortcutHints' 20 s global gap and silently swallow the
    # continue.slow hint — then let the limit line's own hints (turn.link, call.fold) age out
    # before submitting, so nothing stands in front of the hint toast.
    xdo('type', '--delay', '40', '/continue')
    time.sleep(23)
    MockState.mode = 'hang'      # the turn must not finish and retire the toast before the shot
    xdo('key', '--clearmodifiers', 'Return')
    time.sleep(1.5)                                 # the hint rides the pane's toast queue (5 s)
    hint_png = shot('06-hint', win)
    text = ocr_bottom(hint_png, lines=40)
    line = next((l for l in text.splitlines() if 'Next time' in l), '')
    ok = 'Next time:' in line and ('Ctrl+Enter' in line or 'Ctrl+Return' in line)
    check('H: /continue shows the "Next time: Ctrl+Enter" hint', ok,
          line if line else '(no hint line found; window OCR: %s)' % ' | '.join(
              l.strip() for l in text.splitlines() if l.strip())[:200])

    # ---- G: a busy agent keeps its answer (the /continue turn above is still hanging) --
    time.sleep(6)                                   # the hint toast retires; the turn does not
    composer_click(win)
    before = MockState.count()
    ctrl_enter()                                    # empty box, agent busy
    time.sleep(4)
    check('G: Ctrl+Enter on the empty box sent nothing to a busy agent',
          MockState.count() == before, 'model calls: %d → %d' % (before, MockState.count()))
    busy = ocr_bottom(shot('07-busy-empty', win), lines=20)
    check('G: the pane kept its busy answer ("interrupts the agent")',
          'interrupts the agent' in busy.replace('\n', ' '))

    proc.terminate()
    try:
        proc.wait(5)
    except subprocess.TimeoutExpired:
        proc.kill()
    with open(os.path.join(HERE, 'mockserver.log'), 'w', encoding='utf-8') as f:
        json.dump(MockState.requests, f, indent=1)
        f.write('\n'.join(server_log))
    for name in ('relay.log', 'worker.log'):
        src = os.path.join(RUN, 'data', 'relay', 'logs', name)   # the logs live in logs/, one level down
        if os.path.exists(src):
            shutil.copy(src, os.path.join(HERE, name))
    print('FAILURES: %d' % len(fails) if fails else 'ALL PASS', flush=True)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
