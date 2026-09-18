import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from relay_core.agent import Agent
from relay_core.keybindings import MAX_KEYS, KeybindingCatalog, KeybindingError, normalize_key
from relay_core.provider import ProviderConfig

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')
ACTIONS = [
    {'id': 'pane.close', 'description': 'Close pane, tab, or window', 'keys': ['Ctrl+W']},
    {'id': 'pane.splitRight', 'description': 'New pane to the right', 'keys': ['Ctrl+P']},
    {'id': 'tab.new', 'description': 'New tab', 'keys': ['Ctrl+T']},
]


def tool_call(arguments, name='set_keybinding'):
    return {'role': 'assistant', 'content': '', 'tool_calls': [
        {'id': 'call-1', 'type': 'function', 'function': {'name': name, 'arguments': json.dumps(arguments)}}]}


class FakeProvider:
    def __init__(self, response): self.response = response; self.calls = 0; self.messages = []; self.tools = []
    def complete(self, messages, tools, emit, cancel):
        self.messages = json.loads(json.dumps(messages)); self.tools = tools; self.calls += 1
        return self.response if self.calls == 1 else {'role': 'assistant', 'content': 'Done.'}
    def cancel(self): pass


class NormalizeTests(unittest.TestCase):
    def test_modifier_order_and_case(self):
        self.assertEqual(normalize_key('shift+ctrl+p'), 'Ctrl+Shift+P')
        self.assertEqual(normalize_key('Meta+Shift+Alt+Control+x'), 'Ctrl+Alt+Shift+Meta+X')
        self.assertEqual(normalize_key('alt+left'), 'Alt+Left')
        self.assertEqual(normalize_key('f12'), 'F12')
        self.assertEqual(normalize_key('Ctrl+pgdown'), 'Ctrl+PgDown')
        self.assertEqual(normalize_key('Ctrl+/'), 'Ctrl+/')
        self.assertEqual(normalize_key('Ctrl+`'), 'Ctrl+`')
        self.assertEqual(normalize_key('Ctrl+Shift+Backtab'), 'Ctrl+Shift+Backtab')

    def test_symbol_keys_and_aliases(self):
        from relay_core import keybindings as kb
        self.assertEqual(kb.normalize_key("ctrl+shift+("), "Ctrl+Shift+(")
        self.assertEqual(kb.normalize_key("Ctrl+|"), "Ctrl+|")
        self.assertEqual(kb.normalize_key("Ctrl+Esc"), "Ctrl+Escape")
        self.assertEqual(kb.normalize_key("Ctrl+PageDown"), "Ctrl+PgDown")
        with self.assertRaises(kb.KeybindingError):
            kb.normalize_key("Ctrl++")

    def test_invalid_keys(self):
        for bad in ['', 'Ctrl+', '+P', 'Hyper+P', 'Ctrl+Ctrl+P', 'Ctrl+F36', 'Ctrl+Pause', 'Ctrl+P, Ctrl+Q',
                    'Ctrl+é', 'Ctrl+PP', 7, None, 'x' * 80]:
            with self.assertRaises(KeybindingError, msg=repr(bad)):
                normalize_key(bad)


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'relay' / 'keybindings.json'
        self.catalog = KeybindingCatalog(str(self.path), json.loads(json.dumps(ACTIONS)))

    def tearDown(self):
        self.temp.cleanup()

    def test_validation(self):
        base = str(self.path)
        with self.assertRaises(KeybindingError): KeybindingCatalog('relative/keybindings.json', ACTIONS)
        with self.assertRaises(KeybindingError): KeybindingCatalog(str(self.path.with_name('other.json')), ACTIONS)
        with self.assertRaises(KeybindingError): KeybindingCatalog(base, [{'id': 'Bad', 'description': ''}])
        with self.assertRaises(KeybindingError): KeybindingCatalog(base, [{'id': 'noDot', 'description': ''}])
        with self.assertRaises(KeybindingError): KeybindingCatalog(base, [{'id': 'a.b', 'description': 'x' * 201}])
        with self.assertRaises(KeybindingError): KeybindingCatalog(base, [{'id': 'a.b', 'keys': ['A', 'B', 'C', 'D', 'E']}])
        with self.assertRaises(KeybindingError): KeybindingCatalog(base, [{'id': 'a.b'}] * 2)
        with self.assertRaises(KeybindingError): KeybindingCatalog(base, [{'id': f'a.b{i}'} for i in range(201)])
        self.assertTrue(self.path.parent.is_dir())
        self.assertEqual(stat.S_IMODE(self.path.parent.stat().st_mode) & 0o077, 0)

    def test_tool_spec_lists_catalog(self):
        spec = self.catalog.tool_spec()['function']
        self.assertEqual(spec['name'], 'set_keybinding')
        self.assertEqual(spec['parameters']['properties']['action']['enum'], ['pane.close', 'pane.splitRight', 'tab.new'])
        self.assertIn('pane.close: Close pane, tab, or window [Ctrl+W]', spec['description'])

    def test_enum_enforced(self):
        with self.assertRaises(KeybindingError): self.catalog.prepare({'action': 'window.explode', 'keys': []})
        with self.assertRaises(KeybindingError): self.catalog.prepare({'action': 'tab.new', 'keys': ['Ctrl+T'], 'extra': 1})
        with self.assertRaises(KeybindingError): self.catalog.prepare({'action': 'tab.new'})

    def test_write_preserves_other_content_and_reports_conflicts(self):
        self.path.write_text(json.dumps({'version': 1, 'bindings': {'tab.new': ['Ctrl+Shift+T']}, 'comment': 'mine'}))
        args, preview = self.catalog.prepare({'action': 'pane.splitRight', 'keys': ['ctrl+w', 'Alt+P', 'Ctrl+W']})
        self.assertEqual(preview, 'SET KEYBINDING\n\npane.splitRight: Ctrl+W, Alt+P')
        result = self.catalog.apply(args)
        self.assertEqual(result['conflicts'], ['pane.close'])
        self.assertEqual(result['keys'], ['Ctrl+W', 'Alt+P'])
        data = json.loads(self.path.read_text())
        self.assertEqual(data['comment'], 'mine')
        self.assertEqual(data['bindings'], {'tab.new': ['Ctrl+Shift+T'], 'pane.splitRight': ['Ctrl+W', 'Alt+P']})
        self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o600)
        self.assertEqual(self.catalog.actions['pane.splitRight'].keys, ['Ctrl+W', 'Alt+P'])
        self.assertEqual(list(self.path.parent.glob('.keybindings-*')), [])

    def test_unbind(self):
        args, preview = self.catalog.prepare({'action': 'pane.close', 'keys': []})
        self.assertTrue(preview.endswith('pane.close: unbound'))
        self.catalog.apply(args)
        self.assertEqual(json.loads(self.path.read_text())['bindings'], {'pane.close': []})

    def test_invalid_existing_file_is_not_overwritten(self):
        self.path.write_text('{not json')
        args, _ = self.catalog.prepare({'action': 'pane.close', 'keys': ['Ctrl+Q']})
        with self.assertRaises(KeybindingError): self.catalog.apply(args)
        self.assertEqual(self.path.read_text(), '{not json')


class AgentToolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_tool_absent_without_catalog(self):
        fake = FakeProvider(tool_call({'action': 'pane.close', 'keys': ['Ctrl+Q']}))
        events = []
        Agent(CONFIG, self.temp.name, events.append, provider=fake).ask('rebind')
        self.assertNotIn('set_keybinding', [t['function']['name'] for t in fake.tools])
        self.assertIn('error', json.loads(fake.messages[-1]['content']))
        self.assertFalse(any(e['event'] == 'tool_started' for e in events))

    def test_agent_sets_keybinding(self):
        path = self.root / 'cfg' / 'keybindings.json'
        catalog = KeybindingCatalog(str(path), json.loads(json.dumps(ACTIONS)))
        fake = FakeProvider(tool_call({'action': 'pane.close', 'keys': ['Ctrl+Shift+Q']}))
        events = []
        Agent(CONFIG, self.temp.name, events.append, provider=fake, keybindings=catalog).ask('rebind close')
        self.assertIn('set_keybinding', [t['function']['name'] for t in fake.tools])
        started = [e for e in events if e['event'] == 'tool_started'][0]
        self.assertEqual(started['preview'], 'SET KEYBINDING\n\npane.close: Ctrl+Shift+Q')
        self.assertEqual(json.loads(path.read_text())['bindings']['pane.close'], ['Ctrl+Shift+Q'])
        self.assertEqual(json.loads(fake.messages[-1]['content'])['note'], 'Relay reloads this file automatically.')


class Handler(BaseHTTPRequestHandler):
    responses = []
    requests = []
    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        Handler.requests.append(json.loads(body))
        message = Handler.responses.pop(0) if Handler.responses else {'role': 'assistant', 'content': 'Done.'}
        payload = json.dumps({'choices': [{'message': message, 'finish_reason': 'tool_calls' if message.get('tool_calls') else 'stop'}]}).encode()
        self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(payload)))
        self.end_headers(); self.wfile.write(payload)
    def log_message(self, *args): pass


class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
        Handler.responses, Handler.requests = [], []
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.temp.cleanup()

    def run_worker(self, messages, until, timeout=15):
        proc = subprocess.Popen([sys.executable, '-S', '-u', str(ROOT / 'backend/worker.py')], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT)
        events = []
        try:
            for message in messages:
                proc.stdin.write(json.dumps(message) + '\n'); proc.stdin.flush()
            timer = threading.Timer(timeout, proc.kill); timer.start()
            for line in proc.stdout:
                events.append(json.loads(line))
                if until(events):
                    break
            timer.cancel()
        finally:
            try:
                proc.stdin.write(json.dumps({'type': 'shutdown'}) + '\n'); proc.stdin.flush()
            except (BrokenPipeError, ValueError):
                pass
            proc.wait(timeout=5)
            for stream in (proc.stdin, proc.stdout, proc.stderr):
                try:
                    stream.close()
                except (BrokenPipeError, OSError):
                    pass
        return events

    def configure(self, keybindings=None):
        message = {'type': 'configure', 'base_url': f'http://127.0.0.1:{self.server.server_port}/v1', 'model': 'mock',
                   'api_key': '', 'workspace': str(self.root)}
        if keybindings is not None:
            message['keybindings'] = keybindings
        return message

    def test_configure_with_catalog_and_tool_call_writes_file(self):
        path = self.root / 'conf' / 'keybindings.json'
        Handler.responses = [tool_call({'action': 'tab.new', 'keys': ['Ctrl+Alt+T']})]
        events = self.run_worker([self.configure({'path': str(path), 'actions': ACTIONS}),
                                  {'type': 'ask', 'text': 'make new tab ctrl alt t'}],
                                 until=lambda ev: any(e['event'] in ('done', 'error') for e in ev))
        self.assertEqual(events[1]['event'], 'configured')
        self.assertTrue(any(e['event'] == 'tool_started' and e['preview'] == 'SET KEYBINDING\n\ntab.new: Ctrl+Alt+T' for e in events))
        self.assertEqual(json.loads(path.read_text())['bindings'], {'tab.new': ['Ctrl+Alt+T']})
        self.assertIn('set_keybinding', [t['function']['name'] for t in Handler.requests[0]['tools']])

    def test_configure_without_catalog_has_no_tool(self):
        events = self.run_worker([self.configure(), {'type': 'ask', 'text': 'hi'}],
                                 until=lambda ev: any(e['event'] in ('done', 'error') for e in ev))
        self.assertEqual(events[1]['event'], 'configured')
        self.assertNotIn('set_keybinding', [t['function']['name'] for t in Handler.requests[0]['tools']])

    def test_invalid_catalog_rejected(self):
        events = self.run_worker([self.configure({'path': 'relative/keybindings.json', 'actions': ACTIONS})],
                                 until=lambda ev: len(ev) >= 2)
        self.assertEqual(events[1]['event'], 'error')
        self.assertIn('absolute', events[1]['text'])

    def test_keybindings_update_keeps_conversation(self):
        path = self.root / 'kb' / 'keybindings.json'
        updated = json.loads(json.dumps(ACTIONS)); updated[0]['keys'] = ['Ctrl+Q']
        events = self.run_worker([self.configure({'path': str(path), 'actions': ACTIONS}),
                                  {'type': 'ask', 'text': 'first'}],
                                 until=lambda ev: any(e['event'] == 'agent_finished' for e in ev))
        self.assertTrue(any(e['event'] == 'done' for e in events))
        # Second worker session: configure, ask, update catalog, ask again; both requests share history.
        Handler.requests = []
        events = self.run_worker([self.configure({'path': str(path), 'actions': ACTIONS}),
                                  {'type': 'ask', 'text': 'first'},
                                  {'type': 'keybindings', 'path': str(path), 'actions': updated},
                                  {'type': 'ask', 'text': 'second', 'when': 'queue'}],
                                 until=lambda ev: sum(e['event'] == 'agent_finished' for e in ev) >= 2)
        self.assertTrue(any(e['event'] == 'keybindings_updated' for e in events))
        # The pane title (protocol 18) is a no-tools side call that may land after the turn's own
        # request, so pick the last request that carried tools.
        last = [r for r in Handler.requests if r.get('tools')][-1]
        self.assertIn('first', [m.get('content') for m in last['messages']])
        spec = [t for t in last['tools'] if t['function']['name'] == 'set_keybinding'][0]
        self.assertIn('pane.close: Close pane, tab, or window [Ctrl+Q]', spec['function']['description'])

    def test_keybindings_update_requires_agent(self):
        events = self.run_worker([{'type': 'keybindings', 'path': str(self.root / 'keybindings.json'), 'actions': ACTIONS}],
                                 until=lambda ev: len(ev) >= 2)
        self.assertEqual(events[1]['event'], 'error')


class GuiDefaultsTests(unittest.TestCase):
    """The action registry lives in src/main.cpp; these read it as text (no window needed)."""

    def defaults(self, action):
        source = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
        match = re.search(r'add\("' + re.escape(action) + r'",.*?\{(.*?)\}\);', source, re.S)
        self.assertIsNotNone(match, f'{action} is not in the action registry')
        return re.findall(r'QStringLiteral\("([^"]+)"\)', match.group(1))

    def test_the_shortcuts_overlay_binds_every_spelling_of_ctrl_question(self):
        # #T9ZS: "Ctrl+?" is one gesture with several spellings. Qt reports the main-row key as
        # Key_Question on a US layout but as Key_Slash on others and on the keypad, always with
        # Shift held, so a lone "Ctrl+?" binding never fires on those keyboards.
        keys = self.defaults('help.shortcuts')
        for spelling in ('Ctrl+?', 'Ctrl+Shift+/', 'Ctrl+/'):
            self.assertIn(spelling, keys)
        self.assertIn('F1', keys, 'F1 must keep working')
        for key in keys:
            normalize_key(key)
        self.assertLessEqual(len(keys), MAX_KEYS, 'the agent tool caps an action at MAX_KEYS keys')

    def test_no_other_action_claims_a_shortcuts_overlay_key(self):
        source = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
        claimed = set(self.defaults('help.shortcuts'))
        for action, block in re.findall(r'add\("([a-zA-Z.]+)", "[a-z]+", "[^"]*",\s*\{(.*?)\}\);', source, re.S):
            if action == 'help.shortcuts':
                continue
            for key in re.findall(r'QStringLiteral\("([^"]+)"\)', block):
                self.assertNotIn(key, claimed, f'{action} collides with help.shortcuts on {key}')


if __name__ == '__main__':
    unittest.main()
