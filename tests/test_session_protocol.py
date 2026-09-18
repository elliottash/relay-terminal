"""Presets/effort table, provider usage, instruction files, and the worker protocol handlers for
sessions (docs/AGENT-SESSIONS-PROTOCOL.md). Fake providers and local servers only."""
import io
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock

from relay_core import instructions, presets
from relay_core.agent import Agent
from relay_core.provider import ChatProvider, ProviderConfig, ProviderError
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_sessions import ScriptedProvider, call, text, tools_msg  # noqa: E402
from test_queue import Recorder  # noqa: E402


class PresetTests(unittest.TestCase):
    def test_effort_table(self):
        self.assertEqual(presets.apply_effort({}, 'kimi', 'medium')[1], {'reasoning_effort': 'high'})
        self.assertEqual(presets.apply_effort({'temperature': 1}, 'glm', 'low'),
                         ({'temperature': 1, 'thinking': {'type': 'enabled'}, 'reasoning_effort': 'low'},
                          {'thinking': {'type': 'enabled'}, 'reasoning_effort': 'low'}))
        self.assertEqual(presets.apply_effort({'reasoning': {'max_tokens': 5, 'exclude': True}}, 'openrouter', 'medium')[0],
                         {'reasoning': {'exclude': True, 'effort': 'medium'}})
        self.assertEqual(presets.distinct_efforts('kimi'), ['low', 'medium', 'max'])
        self.assertEqual(presets.distinct_efforts('openrouter'), ['low', 'medium', 'high', 'max'])
        self.assertEqual(presets.infer_effort('glm', presets.GLM_EXTRA), 'high')
        self.assertIsNone(presets.apply_effort({'a': 1}, 'kimi', None)[1] or None)

    def test_context_windows(self):
        self.assertEqual(presets.PRESETS['kimi'].context_window, 1_048_576)
        self.assertEqual(presets.PRESETS['glm-coding'].context_window, 1_000_000)
        self.assertEqual(presets.PRESETS['openrouter'].context_window, 1_048_576)
        self.assertEqual(presets.context_window_for(None), presets.DEFAULT_CONTEXT_WINDOW)
        self.assertEqual(presets.effort_style(None, {'reasoning': {}}), 'openrouter')


class ProviderUsageTests(unittest.TestCase):
    def test_usage_inside_choice_emitted_once(self):
        provider = ChatProvider(ProviderConfig('http://127.0.0.1:1/v1', 'm', ''))
        events = []
        usage = {'prompt_tokens': 10, 'completion_tokens': 2, 'total_tokens': 12}
        data = ('data: ' + json.dumps({'choices': [{'delta': {'content': 'hi'}, 'finish_reason': None}]}) + '\n\n'
                'data: ' + json.dumps({'choices': [{'delta': {}, 'finish_reason': 'stop', 'usage': usage}]}) + '\n\n'
                'data: [DONE]\n\n').encode()
        provider._stream(io.BytesIO(data), events.append, threading.Event())
        self.assertEqual([e for e in events if e['event'] == 'usage'], [{'event': 'usage', 'usage': usage}])

    def test_no_tools_key_for_side_calls(self):
        bodies = []

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                bodies.append(json.loads(self.rfile.read(int(self.headers['Content-Length']))))
                body = json.dumps({'choices': [{'message': {'content': 'ok'}, 'finish_reason': 'stop'}],
                                   'usage': {'total_tokens': 3}}).encode()
                self.send_response(200); self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)

            def log_message(self, *args):
                pass
        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        provider = ChatProvider(ProviderConfig(f'http://127.0.0.1:{server.server_port}/v1', 'm', ''))
        events = []
        provider.complete([{'role': 'user', 'content': 'x'}], [], events.append, threading.Event())
        self.assertNotIn('tools', bodies[0])
        self.assertIn({'event': 'usage', 'usage': {'total_tokens': 3}}, events)


class InstructionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = Path(self.temp.name) / 'home'
        self.repo = self.home / 'repo'
        self.ws = self.repo / 'pkg'
        self.ws.mkdir(parents=True)
        (self.repo / '.git').mkdir()
        patcher = mock.patch.dict(os.environ, {'HOME': str(self.home)})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self.temp.cleanup)

    def write(self, path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def test_scan_covers_conventions(self):
        self.write(self.repo / 'AGENTS.md', 'root agents')
        self.write(self.ws / 'CLAUDE.md', 'claude')
        self.write(self.ws / '.cursor/rules/style.mdc', '---\nalwaysApply: true\n---\nstyle')
        self.write(self.ws / '.kiro/steering/tech.md', 'kiro')
        self.write(self.home / '.claude/CLAUDE.md', 'global claude')
        self.write(self.home / '.codex/AGENTS.md', 'global codex')
        self.write(self.home / '.gemini/GEMINI.md', 'gemini')
        items = {i['path']: i for i in instructions.scan(self.ws)}
        for path in [self.repo / 'AGENTS.md', self.ws / 'CLAUDE.md', self.ws / '.cursor/rules/style.mdc',
                     self.ws / '.kiro/steering/tech.md', self.home / '.claude/CLAUDE.md', self.home / '.codex/AGENTS.md',
                     self.home / '.gemini/GEMINI.md']:
            self.assertTrue(items[str(path)]['exists'], path)
        self.assertEqual(items[str(self.home / '.claude/CLAUDE.md')]['scope'], 'global')
        self.assertEqual(items[str(self.ws / 'CLAUDE.md')]['scope'], 'project')
        self.assertEqual(items[str(self.ws / 'CLAUDE.md')]['bytes'], 6)
        self.assertFalse(items[str(self.ws / 'WARP.md')]['exists'])
        self.assertIn(str(self.home / '.config/opencode/AGENTS.md'), items)
        self.assertNotIn(str(self.repo / 'WARP.md'), items)  # missing files only listed for the workspace dir

    def test_project_auto_first_per_directory_with_imports_and_cap(self):
        self.write(self.repo / 'AGENTS.md', 'Use tabs. See @docs/style.md and @~/secret.md')
        self.write(self.repo / 'CLAUDE.md', '@AGENTS.md')
        self.write(self.repo / 'docs/style.md', 'STYLE GUIDE')
        self.write(self.home / 'secret.md', 'SECRET OUTSIDE PROJECT')
        self.write(self.ws / 'CLAUDE.md', 'package rules')
        self.write(self.ws / 'CLAUDE.local.md', 'my local notes')
        self.write(self.ws / '.claude/rules/conditional.md', '---\npaths: src/**\n---\nonly for src')
        global_file = self.write(self.home / '.claude/CLAUDE.md', 'GLOBAL PREFS')
        loaded = instructions.load({'files': [str(global_file)], 'project_auto': True}, self.ws)
        self.assertEqual(loaded.loaded, [str(global_file), str(self.repo / 'AGENTS.md'), str((self.repo / 'docs/style.md').resolve()),
                                         str(self.ws / 'CLAUDE.md'), str(self.ws / 'CLAUDE.local.md')])
        self.assertIn('STYLE GUIDE', loaded.section)
        self.assertNotIn('SECRET OUTSIDE PROJECT', loaded.section)
        self.assertNotIn('only for src', loaded.section)
        self.assertIn(f'<instructions path="{self.repo / "AGENTS.md"}"', loaded.section)
        self.write(self.ws / 'CLAUDE.md', 'x' * 100_000)
        capped = instructions.load({'project_auto': True}, self.ws)
        self.assertLessEqual(len(capped.section.encode()), instructions.TOTAL_CAP)
        self.assertIn('truncated', capped.section)
        off = instructions.load({'files': [], 'project_auto': False}, self.ws)
        self.assertEqual(off.section, '')
        with self.assertRaises(ValueError):
            instructions.load({'files': ['relative.md']}, self.ws)

    def test_warp_files_preferred_and_cap_configurable(self):
        self.write(self.ws / 'AGENTS.md', 'agents file')
        self.write(self.ws / 'WARP.md', 'warp file ' + 'w' * 5000)
        warp_global = self.write(self.home / '.warp/WARP.md', 'global warp rules')
        items = {i['path']: i for i in instructions.scan(self.ws)}
        self.assertEqual((items[str(warp_global)]['tool'], items[str(warp_global)]['scope']), ('Warp', 'global'))
        loaded = instructions.load({'files': [str(warp_global)], 'project_auto': True, 'max_bytes': 2048}, self.ws)
        self.assertEqual(loaded.loaded, [str(warp_global), str(self.ws / 'WARP.md')])
        self.assertEqual(loaded.truncated, [str(self.ws / 'WARP.md')])
        self.assertLessEqual(len(loaded.section.encode()), 2048)
        self.assertEqual(loaded.cap, 2048)
        with self.assertRaises(ValueError):
            instructions.load({'max_bytes': 10}, self.ws)

    def test_instructions_in_system_prompt(self):
        self.write(self.ws / 'AGENTS.md', 'ALWAYS RUN MAKE CHECK')
        loaded = instructions.load(None, self.ws)
        agent = Agent(ProviderConfig('http://127.0.0.1:1/v1', 'm', ''), str(self.ws), lambda e: None,
                      provider=ScriptedProvider(), instructions=loaded)
        self.assertIn('ALWAYS RUN MAKE CHECK', agent.messages[0]['content'])
        self.assertIn('lower-priority', agent.messages[0]['content'])

    def test_synthesize_writes_target_with_backup(self):
        a = self.write(self.home / '.claude/CLAUDE.md', 'Prefer pytest.')
        b = self.write(self.home / '.codex/AGENTS.md', 'Use ruff.')
        provider = ScriptedProvider(side_reply='```markdown\n# Relay instructions\n- pytest\n- ruff\n```')
        text_ = instructions.synthesize(provider, [str(a), str(b)])
        self.assertEqual(text_, '# Relay instructions\n- pytest\n- ruff\n')
        self.assertIn('Prefer pytest.', provider.side_requests[0][-1]['content'])
        target = self.home / '.config/relay/relay.md'
        self.write(target, 'old')
        path = instructions.write_synthesized(str(target), text_)
        self.assertEqual(path.read_text(), text_)
        self.assertEqual((self.home / '.config/relay/relay.md.bak').read_text(), 'old')


class ProtocolHandlerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.ws = Path(self.temp.name) / 'ws'
        self.ws.mkdir()
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)
        self.cmds = SessionCommands(self.sup, self.rec)
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(self.sup.shutdown)

    def make_agent(self, provider):
        agent = Agent(ProviderConfig('http://127.0.0.1:1/v1', 'm', ''), str(self.ws), self.sup.agent_emit,
                      provider=provider, session_dir=str(Path(self.temp.name) / 'sessions'))
        self.sup.set_agent(agent)
        return agent

    def run_turn(self, text_):
        before = len(self.rec.of('agent_finished'))
        self.sup.submit(text_, 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and len(self.rec.of('agent_finished')) > before)

    def test_compact_resume_recap_and_plan_execute(self):
        provider = ScriptedProvider(side_reply='{"summary": "Did three things.", "next_action": "Ship it"}')
        agent = self.make_agent(provider)
        for p in ['one', 'two', 'three']:
            self.run_turn(p)
        self.cmds.handle('compact', {'focus': 'tests'})
        self.rec.wait(lambda e: e['event'] == 'compacted')
        deadline = time.monotonic() + 5
        while self.sup.busy and time.monotonic() < deadline:
            time.sleep(0.01)
        self.cmds.handle('checkpoints', {'id': 'c'})
        self.assertEqual(len(self.rec.of('checkpoints')[-1]['items']), 3)
        self.cmds.handle('sessions', {'id': 's'})
        items = self.rec.wait(lambda e: e['event'] == 'sessions')['items']
        self.assertEqual(items[0]['id'], agent.session_id)
        self.assertEqual(items[0]['turns'], 3)
        fresh = self.make_agent(ScriptedProvider(side_reply='{"summary": "Did three things.", "next_action": "Ship it"}'))
        self.cmds.handle('resume', {'id': agent.session_id})
        loaded = self.rec.wait(lambda e: e['event'] == 'state_loaded')
        self.assertEqual(loaded['turns'], 3)
        recap = self.rec.wait(lambda e: e['event'] == 'recap')
        self.assertEqual((recap['text'], recap['next_action'], recap['reason']), ('Did three things.', 'Ship it', 'resume'))
        self.cmds.handle('recap_request', {'id': 'r', 'reason': 'away'})
        self.rec.wait(lambda e: e['event'] == 'recap' and e.get('id') == 'r')
        # plan execute re-reads the file from disk and switches to build mode
        fresh.set_mode('plan')
        plan = self.ws / 'plan.md'
        plan.write_text('# Plan\n1. edited by user')
        self.cmds.handle('plan_execute', {'path': str(plan), 'fresh': True, 'id': 'pe'})
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == self.rec.of('queued')[-1]['id'])
        sent = fresh.provider.requests[-1][0]
        self.assertEqual(fresh.mode, 'build')
        self.assertTrue(sent[-1]['content'].startswith(f'Execute the plan in {plan}'))
        self.assertIn('edited by user', sent[-1]['content'])
        self.assertEqual(len([m for m in sent if m['role'] == 'user']), 1)

    def test_busy_refusals_and_effort_while_idle(self):
        gate = threading.Event()

        def slow(_messages):
            gate.wait(5)
            return text('slow done')
        agent = self.make_agent(ScriptedProvider([slow]))
        self.sup.submit('slow', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        for kind, req in [('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'x'}), ('rewind', {'turn': 1}),
                          ('fork', {}), ('compact', {}), ('resume', {'id': 'a' * 32})]:
            with self.assertRaises(ValueError, msg=kind):
                self.cmds.handle(kind, req)
        self.cmds.handle('set_effort', {'effort': 'low', 'id': 'e'})
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'effort_changed')['applied'], {'reasoning_effort': 'low'})
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'other', 'context_window': 200000})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['model'], changed['context_window']), ('other', 200000))
        self.assertEqual(len(agent.messages), 3)  # conversation kept
        self.cmds.handle('fork', {'turn': 1, 'id': 'f'})
        state = self.rec.wait(lambda e: e['event'] == 'fork_state')['state']
        self.cmds.handle('load_state', {'state': state, 'id': 'l'})
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'state_loaded')['turns'], 1)

    def test_hooks_for_model_switch_and_replaced_conversation(self):
        seen = []
        cmds = SessionCommands(self.sup, self.rec, on_model_changed=lambda agent: seen.append(('model', agent.config.model)),
                               on_conversation_replaced=lambda: seen.append('replaced'))
        agent = self.make_agent(ScriptedProvider())
        self.run_turn('one')
        cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'next'})
        cmds.handle('fork', {})
        state = self.rec.wait(lambda e: e['event'] == 'fork_state')['state']
        cmds.handle('load_state', {'state': state})
        self.assertEqual(seen, [('model', 'next'), 'replaced'])
        from relay_core.subagents import effort_extra
        self.assertEqual(effort_extra('glm-coding', {}, 'max'), {'thinking': {'type': 'enabled'}, 'reasoning_effort': 'max'})

    def test_suggest_and_set_mode(self):
        self.make_agent(ScriptedProvider(side_reply='{"command": "make test", "reason": "build passed"}'))
        self.cmds.handle('suggest', {'kind': 'next_command', 'command': 'make', 'exit_status': 0, 'id': 's1'})
        event = self.rec.wait(lambda e: e['event'] == 'suggestion')
        self.assertEqual((event['id'], event['text'], event['kind']), ('s1', 'make test', 'next_command'))
        self.cmds.handle('set_mode', {'mode': 'plan'})
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'mode_changed')['mode'], 'plan')
        self.cmds.handle('suggest', {'kind': 'next_prompt', 'id': 's2'})
        self.assertEqual(self.rec.wait(lambda e: e.get('id') == 's2')['reason'], 'plan_mode')
        with self.assertRaises(ValueError):
            self.cmds.handle('suggest', {'kind': 'weather'})

    def test_a_failed_suggestion_is_reported_as_a_suggestion(self):
        # #308N: a suggestion that fails used to arrive as a bare {"event": "error"}. The GUI has no
        # way to tell that apart from an agent-turn failure, so it showed a naked provider line and
        # no ghost text, and the pane's busy state was cleared along the way. The failure has to be
        # a suggestion event carrying the error and the model it ran on.
        class Failing(ScriptedProvider):
            def complete(self, messages, tools, emit, cancel):
                if not tools:
                    raise ProviderError('Provider HTTP 401. Check endpoint, model access, key, quota, and parameters.')
                return super().complete(messages, tools, emit, cancel)

        self.make_agent(Failing())
        self.cmds.handle('suggest', {'kind': 'next_command', 'command': 'make', 'exit_status': 0, 'id': 's9'})
        event = self.rec.wait(lambda e: e.get('id') == 's9')
        self.assertEqual(event['event'], 'suggestion')
        self.assertEqual((event['kind'], event['text']), ('next_command', ''))
        self.assertIn('401', event['error'])
        self.assertTrue(event['model'])
        # No bare error event for the same request: that is what the GUI mistook for a turn failure.
        self.assertEqual([e for e in self.rec.events if e.get('event') == 'error' and e.get('id') == 's9'], [])


class WorkerSubprocessTests(unittest.TestCase):
    def test_configure_fields_scan_and_attachments(self):
        with tempfile.TemporaryDirectory() as temp:
            ws = Path(temp) / 'ws'
            ws.mkdir()
            (ws / 'AGENTS.md').write_text('project rule')
            env = {**os.environ, 'HOME': temp, 'XDG_DATA_HOME': str(Path(temp) / 'data'),
                   'PYTHONPATH': str(ROOT / 'backend')}
            messages = [
                {'type': 'configure', 'base_url': 'http://127.0.0.1:1/v1', 'model': 'm', 'api_key': '', 'workspace': str(ws),
                 'preset': 'glm-coding', 'effort': 'max', 'compact_threshold': 0.7},
                {'type': 'scan_instructions', 'workspace': str(ws), 'id': 'scan'},
                {'type': 'context', 'id': 'ctx'},
                {'type': 'set_mode', 'mode': 'plan'},
                {'type': 'ask', 'text': 'hi', 'attachments': [{'path': 'missing.txt'}], 'id': 'att'},
                {'type': 'configure', 'base_url': 'http://127.0.0.1:1/v1', 'model': 'm', 'api_key': '', 'workspace': str(ws),
                 'compact_threshold': 0.2},
                {'type': 'shutdown'}]
            proc = subprocess.run([sys.executable, '-S', str(ROOT / 'backend/worker.py')],
                                  input=''.join(json.dumps(m) + '\n' for m in messages),
                                  text=True, capture_output=True, timeout=10, cwd=ROOT, env=env)
            events = [json.loads(line) for line in proc.stdout.splitlines()]
            configured = [e for e in events if e['event'] == 'configured'][0]
            self.assertEqual(configured['context_window'], 1_000_000)
            self.assertEqual(configured['effort'], 'max')
            self.assertEqual(configured['mode'], 'build')
            self.assertEqual(configured['instructions'], [str(ws / 'AGENTS.md')])
            self.assertRegex(configured['session_id'], '^[0-9a-f]{32}$')
            self.assertEqual(configured['limit_tokens'], 700_000)
            found = [e for e in events if e['event'] == 'instructions_found'][0]
            self.assertTrue(any(i['path'] == str(ws / 'AGENTS.md') and i['exists'] for i in found['items']))
            ctx = [e for e in events if e['event'] == 'context'][0]
            self.assertTrue(ctx['estimated'])
            self.assertEqual([e['mode'] for e in events if e['event'] == 'mode_changed'], ['plan'])
            errors = [e for e in events if e['event'] == 'error']
            self.assertTrue(any(e.get('id') == 'att' and 'not found' in e['text'] for e in errors))
            self.assertTrue(any('compact_threshold' in e['text'] for e in errors))


if __name__ == '__main__':
    unittest.main()
