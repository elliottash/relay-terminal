#!/usr/bin/env python3
"""Ctrl+Enter continues a stopped turn, 2026-09-20 (#SXF1): live check under Xvfb.

Drives the landed build (land.py's verify tree, commit 472ae1a2) against a mock
OpenAI-compatible server on loopback, with an isolated profile whose only provider is a
local endpoint pointing at the mock. No network, no real key. Checks:

  A  a turn that hits its step limit: with the prompt box empty, Ctrl+Enter sends the
     prompt "Continue" to the model (the mock sees it) — not "Type a prompt first."
  B  a turn cut off by a kill -9 of Relay mid-flight: relaunch restores the session
     (layout autosave), and Ctrl+Enter on the empty box sends "Continue" again.
     The hung turn first completes one tool step: the worker's mid-turn autosave only
     fires at step boundaries, and a turn killed on its first model call never reaches
     the session file at all (turn_open is then rightly false — nothing to continue).
  C  the slow path teaches the fast one: after /continue, the hint line names Ctrl+Enter
     (built live from agent.interrupt's binding, not hard-coded).

Also captured: the limit line with the ▸ Continue link (which now teaches the key) and
relay/worker logs. Needs Xvfb, xdotool, ImageMagick, tesseract.

  Xvfb :159 -screen 0 1400x900x24 &
  DISPLAY=:159 python3 drive.py [path-to-relay-binary]

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
BIN = sys.argv[1] if len(sys.argv) > 1 else '/tmp/claude-1000/land/sxf1/verify/build/relay'
RUN = '/tmp/claude-1000/sxf1-live'
PORT = 8791
DISPLAY = os.environ.get('DISPLAY', ':159')
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
    mode = 'tools'          # 'tools' | 'text' | 'hang' | 'tools_then_hang'
    hang_calls = 0          # tools_then_hang: model calls of the hung turn; the 2nd+ never answer
    requests = []           # {'mode', 'users': [every user message in the request], 'at'}
    lock = threading.Lock()

    @classmethod
    def record(cls, users, side):
        with cls.lock:
            cls.requests.append({'mode': ('side' if side else cls.mode), 'users': users,
                                 'at': time.time()})
            return len(cls.requests) - 1

    @classmethod
    def seen_continue_after_marker(cls, requests, marker):
        # A turn's user message carries a Relay-context preamble before the prompt itself, so the
        # "Continue" the pane submits is the tail of the message, never the whole string.
        with cls.lock:
            return any(r['users'] and isinstance(r['users'][-1], str)
                       and r['users'][-1].rstrip().endswith('Continue') and i > marker
                       for i, r in enumerate(requests))


    @classmethod
    def wait_for(cls, predicate, timeout, what=''):
        # The predicate runs on a snapshot, OUTSIDE the lock: seen_continue_after_marker
        # takes the lock itself, and a plain Lock is not reentrant — calling it from
        # inside `with cls.lock` deadlocks the drive (and, with the lock held, freezes
        # every mock handler mid-record, which is what stalls the agent's turn).
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with cls.lock:
                requests = list(cls.requests)
            if predicate(requests):
                return True
            time.sleep(0.2)
        return False


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
        hang = False
        if mode == 'hang' and not side:
            hang = True
        if mode == 'tools_then_hang' and not side:
            # The session's mid-turn autosave fires at step boundaries, never mid-call: a turn
            # killed on its FIRST model call is never written, its checkpoint never exists on
            # disk and `turn_open` is (correctly) false. So the hung turn first answers one
            # tool call — the autosave then writes it without `ended` — and only the second
            # model call hangs, and that is the moment we kill Relay.
            with MockState.lock:
                MockState.hang_calls += 1
                hang = MockState.hang_calls >= 2
        if hang:
            time.sleep(150)          # the turn is still running when we kill Relay
        body = tool_call_response() if (mode in ('tools', 'tools_then_hang') and not side) else text_response()
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

    # ---- A: run a turn into the step limit, then Ctrl+Enter on the empty box ----------
    MockState.mode = 'tools'
    composer_click(win)
    type_submit('count carefully')
    ok = MockState.wait_for(lambda rs: len(rs) >= STEP_LIMIT, 30, 'limit steps')
    check('turn ran to its step limit (%d model calls)' % STEP_LIMIT, ok)
    time.sleep(3)                                   # done {stop_reason: limit} lands, box clears
    shot('02-limit', win)
    composer_click(win)
    with MockState.lock:
        marker = len(MockState.requests) - 1
    xdo('key', '--clearmodifiers', 'ctrl+Return')
    MockState.mode = 'text'
    ok = MockState.wait_for(lambda rs: MockState.seen_continue_after_marker(rs, marker), 20, 'Continue')
    check('A: Ctrl+Enter on the empty box sent the prompt "Continue"', ok)
    time.sleep(2.5)
    shot('03-continue-sent', win)

    # ---- B: one finished turn, then kill -9 mid-turn, relaunch, Ctrl+Enter -------------
    MockState.mode = 'text'
    composer_click(win)
    type_submit('finish quickly')
    MockState.wait_for(lambda rs: len(rs) >= marker + 3, 30, 'finished turn')
    time.sleep(12)                                  # let the 10 s autosave throttle lapse, so the
                                                    # hung turn's first step boundary DOES save it
    MockState.hang_calls = 0
    MockState.mode = 'tools_then_hang'
    composer_click(win)
    type_submit('hang around')
    MockState.wait_for(lambda rs: len(rs) >= marker + 5, 30, 'hung turn started')
    time.sleep(13)                                  # the tool step saved the open turn; the 2nd call hangs
    shot('04-midturn', win)
    proc.kill()
    proc.wait()
    time.sleep(1)
    proc = launch(env)
    win = window_of(proc)
    check('relaunched after kill -9', bool(win))
    if not win:
        return 1
    activate(win)
    time.sleep(9)                                   # layout restore -> resume -> state_loaded
    shot('05-restored', win)
    restored_text = ocr_bottom(shot('05-restored-ocr', win), lines=22)
    check('B: session restored ("Session loaded" line)', 'Session loaded' in restored_text,
          restored_text.strip().splitlines()[-1] if restored_text.strip() else '(no text)')
    composer_click(win)
    with MockState.lock:
        marker = len(MockState.requests) - 1
    MockState.mode = 'text'
    xdo('key', '--clearmodifiers', 'ctrl+Return')
    ok = MockState.wait_for(lambda rs: MockState.seen_continue_after_marker(rs, marker), 25, 'Continue')
    check('B: Ctrl+Enter on the empty box sent "Continue" after the cut-off restart', ok)
    time.sleep(2.5)
    shot('06-continue-after-restart', win)

    # ---- C: the slow path (/continue) teaches the fast one ----------------------------
    MockState.mode = 'tools'
    composer_click(win)
    type_submit('two more steps')
    MockState.wait_for(lambda rs: len(rs) >= marker + 1 + STEP_LIMIT, 30, 'second limit')
    time.sleep(1)
    composer_click(win)
    # Type /continue at once — a non-empty editor stops the idle-tip timer (4 s after a turn
    # ends, an idle tip would fire, and its showing opens ShortcutHints' 20 s global gap,
    # which would silently swallow the continue.slow hint) — then let that 4 s window pass
    # before submitting, so nothing at all stands in front of the hint toast.
    xdo('type', '--delay', '40', '/continue')
    # The limit line's own hints (turn.link on the ▸ Continue link, call.fold on the tool-calls
    # fold) showed a moment ago and ShortcutHints keeps a 20 s global gap between hints — wait
    # it out, or continue.slow is silently dropped at Pane::hint's mayShow gate.
    time.sleep(23)
    MockState.mode = 'hang'      # the turn must not finish and retire the toast before the shot
    xdo('key', '--clearmodifiers', 'Return')
    time.sleep(1.5)                                 # the hint rides the pane's toast queue (5 s)
    hint_png = shot('07-hint', win)
    MockState.mode = 'text'
    text = ocr_bottom(hint_png, lines=40)
    line = next((l for l in text.splitlines() if 'Next time' in l), '')
    ok = 'Next time:' in line and ('Ctrl+Enter' in line or 'Ctrl+Return' in line)
    check('C: /continue shows the "Next time: Ctrl+Enter" hint', ok,
          line if line else '(no hint line found; window OCR: %s)' % ' | '.join(
              l.strip() for l in text.splitlines() if l.strip())[:200])

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
