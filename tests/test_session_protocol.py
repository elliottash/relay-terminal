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
from relay_core.agent import Agent, STATE_VERSION
from relay_core.provider import ChatProvider, ProviderConfig, ProviderError
from relay_core.queue import TurnSupervisor
from relay_core import session_protocol
from relay_core.session_protocol import SessionCommands

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_sessions import ScriptedProvider, call, text, tools_msg  # noqa: E402
from test_queue import Recorder  # noqa: E402


class PresetTests(unittest.TestCase):
    def test_every_catalog_model_sends_only_its_supported_effort(self):
        from relay_core import openrouter_catalog
        # Deterministic representatives of the router's dynamic catalog, including
        # a model with no knob. Built-in catalog rows are exercised in full.
        dynamic = [{"id": "test/reasoning", "efforts": ["low", "medium", "high", "xhigh"]},
                   {"id": "test/no-effort", "efforts": []}]
        with tempfile.TemporaryDirectory() as root, mock.patch.object(openrouter_catalog, "rows", return_value=dynamic):
            for pid, preset in presets.PRESETS.items():
                for row in presets.catalog_rows(pid):
                    levels = row["efforts"]
                    for level in levels or ["medium"]:
                        with self.subTest(preset=pid, model=row["id"], level=level):
                            config = ProviderConfig(preset.base_url, row["id"], "test",
                                                    presets.model_extra(pid, row["id"]))
                            agent = Agent(config, root, lambda e: None, provider=ScriptedProvider([]),
                                          preset_id=pid, effort=level, track_requests=False)
                            fields = session_protocol.configured_fields(agent)
                            if levels:
                                self.assertEqual(fields["effort"], level)
                                self.assertEqual(presets.infer_effort(preset.effort_style, config.extra), level)
                            else:
                                self.assertNotIn("reasoning_effort", config.extra)
                                self.assertNotIn("effort", config.extra.get("reasoning", {}))
                            # A later choice follows the same model-specific rules.
                            agent.set_effort(level)
                            self.assertEqual(session_protocol.configured_fields(agent)["effort"], fields["effort"])
                            if not levels:
                                self.assertNotIn("reasoning_effort", config.extra)
                                self.assertNotIn("effort", config.extra.get("reasoning", {}))
                                # Switching from an effort-capable model must also
                                # strip inherited provider fields, retaining other options.
                                inherited = ProviderConfig(preset.base_url, row["id"], "test",
                                                           {"reasoning_effort": "high",
                                                            "reasoning": {"effort": "high"},
                                                            "temperature": 0.2})
                                agent.set_model(inherited, pid)
                                self.assertEqual(inherited.extra, {"temperature": 0.2})

    def test_effort_table(self):
        self.assertEqual(presets.apply_effort({}, 'kimi', 'medium')[1], {'reasoning_effort': 'high'})
        self.assertEqual(presets.apply_effort({'temperature': 1}, 'glm', 'low'),
                         ({'temperature': 1, 'thinking': {'type': 'enabled'}, 'reasoning_effort': 'low'},
                          {'thinking': {'type': 'enabled'}, 'reasoning_effort': 'low'}))
        self.assertEqual(presets.apply_effort({'reasoning': {'max_tokens': 5, 'exclude': True}}, 'openrouter', 'medium')[0],
                         {'reasoning': {'exclude': True, 'effort': 'medium'}})
        # A provider offers its own words, and the word offered is the word sent (card #MDL1,
        # 2026-09-21): there is no second table saying what each one becomes, so "xhigh" is a
        # level on OpenAI and OpenRouter rather than a label for a stored "max".
        self.assertEqual(presets.effort_levels('kimi'), ['low', 'high', 'max'])
        self.assertEqual(presets.effort_levels('glm'), ['low', 'high', 'max'])
        self.assertEqual(presets.effort_levels('openrouter'), ['low', 'medium', 'high', 'xhigh'])
        self.assertEqual(presets.effort_levels('gemini'), ['low', 'medium', 'high'])
        for style in presets.EFFORT_LEVELS:
            offered = presets.effort_levels(style)
            self.assertEqual(offered, [w for w in presets.EFFORT_LADDER if w in offered], style)
            if style != 'none':
                self.assertIn('high' if style not in ('gemini', 'relay') else offered[-1], offered, style)
        # Nothing is silently sent as anything else any more, so the only note left is the one
        # that is not about vocabulary: Relay Free's ceiling is the gateway's.
        self.assertEqual(presets.effort_note('kimi'), '')
        self.assertEqual(presets.effort_note('gemini'), '')
        self.assertEqual(presets.effort_note('openrouter'), '')
        self.assertIn('gateway', presets.effort_note('relay'))
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

    def test_an_agents_md_that_imports_claude_md_brings_its_rules_along(self):
        # A Board-created AGENTS.md starts with `@CLAUDE.md` so that CLAUDE.md is not
        # shadowed by the first-hit rule; the companions under .claude/ follow it (#R9G7).
        self.write(self.repo / 'CLAUDE.md', 'claude body')
        self.write(self.repo / '.claude/rules/style.md', 'rule body')
        self.write(self.repo / 'AGENTS.md', '@CLAUDE.md\n\nagents body')
        picked = [p.name for p in instructions.auto_project_files(self.repo)]
        self.assertEqual(picked, ['AGENTS.md', 'style.md'])
        loaded = instructions.load({'files': [], 'project_auto': True}, self.repo)
        self.assertIn('claude body', loaded.section)
        self.assertIn('rule body', loaded.section)
        # An AGENTS.md that does not import CLAUDE.md shadows it, companions included, as before.
        self.write(self.repo / 'AGENTS.md', 'agents body only')
        self.assertEqual([p.name for p in instructions.auto_project_files(self.repo)], ['AGENTS.md'])

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

    def test_relay_project_instructions_route_to_existing_files(self):
        relay = self.write(self.ws / '.relay/relay.md', 'Relay project rules. See @../AGENTS.md')
        agents = self.write(self.ws / 'AGENTS.md', 'Existing agent rules')
        self.write(self.ws / 'WARP.md', 'Warp project rules')
        items = {i['path']: i for i in instructions.scan(self.ws)}
        self.assertEqual(items[str(relay)]['tool'], 'Relay')
        loaded = instructions.load({'project_auto': True}, self.ws)
        self.assertEqual(loaded.loaded, [str(relay), str(agents.resolve())])
        self.assertIn('Existing agent rules', loaded.section)
        self.assertNotIn('Warp project rules', loaded.section)

    def test_agents_preferred_to_warp_and_cap_configurable(self):
        self.write(self.ws / 'AGENTS.md', 'agents file')
        self.write(self.ws / 'WARP.md', 'warp file ' + 'w' * 5000)
        warp_global = self.write(self.home / '.warp/WARP.md', 'global warp rules')
        items = {i['path']: i for i in instructions.scan(self.ws)}
        self.assertEqual((items[str(warp_global)]['tool'], items[str(warp_global)]['scope']), ('Warp', 'global'))
        loaded = instructions.load({'files': [str(warp_global)], 'project_auto': True, 'max_bytes': 2048}, self.ws)
        self.assertEqual(loaded.loaded, [str(warp_global), str(self.ws / 'AGENTS.md')])
        self.assertEqual(loaded.truncated, [])
        self.assertLessEqual(len(loaded.section.encode()), 2048)
        self.assertEqual(loaded.cap, 2048)
        (self.ws / 'AGENTS.md').unlink()
        fallback = instructions.load({'files': [], 'project_auto': True, 'max_bytes': 2048}, self.ws)
        self.assertEqual(fallback.loaded, [str(self.ws / 'WARP.md')])
        self.assertEqual(fallback.truncated, [str(self.ws / 'WARP.md')])
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

    def test_clicked_session_id_resolves_exact_saved_row(self):
        session_id = '008c2701cc448d7b008c2701cc448d7b'
        row = {'session_id': session_id, 'source': 'agent', 'title': 'Earlier work',
               'session_dir': str(Path(self.temp.name) / 'sessions')}
        index = mock.Mock()
        index.search.return_value = {'items': [row]}
        with mock.patch.object(self.cmds, 'index', return_value=index):
            self.cmds.handle('conversation_open', {'id': 'output-session', 'session_id': session_id})
        event = self.rec.of('conversation_open')[-1]
        self.assertEqual(event['item']['session_id'], session_id)
        index.search.assert_called_once_with('', scope='all', session_ids=[session_id], limit=1)
        index.search.return_value = {'items': []}
        with mock.patch.object(self.cmds, 'index', return_value=index):
            with self.assertRaisesRegex(ValueError, 'No saved conversation'):
                self.cmds.handle('conversation_open', {'session_id': session_id})

    def test_load_state_of_a_guest_conversation_keeps_the_guest_cursor(self):
        """`load_state` is the other way a pane lands on a saved conversation (#PCJY).

        The guest-cursor bookkeeping a `resume` gets has to run here too: without it the
        conversation opens, but the harness started for the guest preset afterwards is fresh
        and the guest's own session — the agent's actual memory — is lost.
        """
        agent = self.make_agent(ScriptedProvider())
        self.run_turn('fix the login bug')
        path = Path(agent.store.directory) / f'{agent.session_id}.json'
        saved = json.loads(path.read_text())
        cursors = {'claude:': {'session': '2cc05b69-81a5-45e7-87f9-f2dda8132f49', 'messages': 192}}
        saved.update({'guest': 'claude', 'guest_session': '2cc05b69-81a5-45e7-87f9-f2dda8132f49',
                      'guest_cursors': cursors})
        path.write_text(json.dumps(saved))
        fresh = self.make_agent(ScriptedProvider(name='B'))
        ref = {'version': STATE_VERSION, 'kind': 'relay_agent_state_ref',
               'session_id': agent.session_id, 'session_dir': str(agent.store.directory)}
        self.cmds.handle('load_state', {'id': 'ls', 'state': ref})
        loaded = self.rec.wait(lambda e: e['event'] == 'state_loaded')
        self.assertEqual(loaded['guest'], 'claude')
        self.assertEqual(loaded['guest_session'], '2cc05b69-81a5-45e7-87f9-f2dda8132f49')
        self.assertEqual(getattr(fresh, '_guest_cursors', None), cursors)

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
        # The span survives compaction and a resume: it is read off the resumed session's turn
        # stamps, so it covers the three turns of work and not just this recap's own moment.
        self.assertEqual(recap['span_start'], agent.checkpoints.items[0]['time'])
        self.assertEqual(recap['span_end'], agent.checkpoints.items[-1]['ended'])
        self.assertRegex(recap['span_text'], r'\d\d:\d\d → .*\d\d:\d\d · ')
        self.assertRegex(recap['finished_text'], r'\d\d:\d\d')  # resume recaps state the finish (#MVGR)
        self.cmds.handle('recap_request', {'id': 'r', 'reason': 'away'})
        away = self.rec.wait(lambda e: e['event'] == 'recap' and e.get('id') == 'r')
        # The resume recap just covered these turns: an away recap over them again is a
        # duplicate and is skipped (#TKKA).
        self.assertEqual(away['skipped'], 'no_new_turns')
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

    def test_away_recap_not_repeated(self):
        # Card #TKKA: the pane and the worker count turns differently (the pane only turns that
        # ended `done`), so the pane's dedupe can stay open forever; the worker skips an away or
        # resume recap over turns the last recap already covered.
        provider = ScriptedProvider(side_reply='{"summary": "Did three things.", "next_action": null}')
        agent = self.make_agent(provider)
        for p in ['one', 'two', 'three']:
            self.run_turn(p)
        side_before = len(provider.side_requests)
        self.cmds.handle('recap_request', {'id': 'a', 'reason': 'away'})
        first = self.rec.wait(lambda e: e['event'] == 'recap' and e.get('id') == 'a')
        self.assertNotIn('skipped', first)
        self.assertEqual(agent.recap_turn, 3)
        # A second away recap over the same turns is a duplicate and costs no model call.
        self.cmds.handle('recap_request', {'id': 'b', 'reason': 'away'})
        dupe = self.rec.wait(lambda e: e['event'] == 'recap' and e.get('id') == 'b')
        self.assertEqual(dupe['skipped'], 'no_new_turns')
        self.assertEqual(len(provider.side_requests), side_before + 1)
        # A manual recap always runs.
        self.cmds.handle('recap_request', {'id': 'm', 'reason': 'manual'})
        manual = self.rec.wait(lambda e: e['event'] == 'recap' and e.get('id') == 'm')
        self.assertNotIn('skipped', manual)
        # A turn since the recap re-arms the away one.
        self.run_turn('four')
        self.cmds.handle('recap_request', {'id': 'c', 'reason': 'away'})
        again = self.rec.wait(lambda e: e['event'] == 'recap' and e.get('id') == 'c')
        self.assertNotIn('skipped', again)
        self.assertEqual(again['turns_covered'], 4)
        # The marker survives a save and resume: a resumed session with no new turns does not
        # recap again either.
        agent.autosave()
        self.make_agent(ScriptedProvider(side_reply='{"summary": "x"}'))
        self.cmds.handle('resume', {'id': agent.session_id})
        self.rec.wait(lambda e: e['event'] == 'state_loaded')
        resumed = self.rec.wait(lambda e: e['event'] == 'recap' and e.get('reason') == 'resume')
        self.assertEqual(resumed.get('skipped'), 'no_new_turns')

    def test_busy_refusals_and_effort_while_idle(self):
        gate = threading.Event()

        def slow(_messages):
            gate.wait(5)
            return text('slow done')
        agent = self.make_agent(ScriptedProvider([slow]))
        self.sup.submit('slow', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_started')
        # set_model is no longer among them: it is accepted mid-turn (issue 3ES1, ModelSwitchMidTurnTests).
        for kind, req in [('rewind', {'turn': 1}),
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

    def test_reset_is_preceded_by_the_new_conversations_context(self):
        # Issue 5PY9, end to end: the worker must emit the fresh conversation's `context` before
        # the `reset` event, or the pane's context chip keeps the previous conversation's reading
        # until the next turn.
        class Model(BaseHTTPRequestHandler):
            def log_message(self, fmt, *args):
                pass

            def do_POST(self):
                length = int(self.headers.get('Content-Length') or 0)
                json.loads(self.rfile.read(length) or b'{}')
                body = json.dumps({'id': 'm', 'object': 'chat.completion', 'model': 'mock',
                                   'choices': [{'index': 0, 'finish_reason': 'stop',
                                                'message': {'role': 'assistant', 'content': 'Done.'}}]}).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        server = ThreadingHTTPServer(('127.0.0.1', 0), Model)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        with tempfile.TemporaryDirectory() as temp:
            ws = Path(temp) / 'ws'
            ws.mkdir()
            env = {**os.environ, 'HOME': temp, 'XDG_DATA_HOME': str(Path(temp) / 'data'),
                   'PYTHONPATH': str(ROOT / 'backend')}
            proc = subprocess.Popen([sys.executable, '-S', str(ROOT / 'backend/worker.py')],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.DEVNULL, text=True, cwd=ROOT, env=env)
            events = []

            def reader():
                for line in proc.stdout:
                    events.append(json.loads(line))
            threading.Thread(target=reader, daemon=True).start()

            def send(msg):
                proc.stdin.write(json.dumps(msg) + '\n')
                proc.stdin.flush()

            def wait_for(kind):
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    if any(e['event'] == kind for e in events):
                        return True
                    time.sleep(0.05)
                return False
            try:
                send({'type': 'configure', 'base_url': 'http://127.0.0.1:%d/v1' % server.server_address[1],
                      'model': 'mock', 'api_key': '', 'workspace': str(ws)})
                self.assertTrue(wait_for('configured'), 'worker configured')
                first_session = next(e['session_id'] for e in events if e['event'] == 'configured')
                send({'type': 'ask', 'text': 'hello', 'id': 'a1'})
                self.assertTrue(wait_for('agent_finished'), 'turn ran')
                during = [e for e in events if e['event'] == 'context']
                self.assertTrue(during)          # the turn's own context events
                send({'type': 'reset'})
                self.assertTrue(wait_for('reset'), 'reset handled')
                kinds = [e['event'] for e in events]
                self.assertIn('context', kinds[kinds.index('agent_finished'):kinds.index('reset')])
                fresh = [e for e in events if e['event'] == 'context'][-1]
                self.assertLess(fresh['used_tokens'], during[-1]['used_tokens'])
                self.assertLess(fresh['percent'], 10.0)   # system prompt + tool schemas, no turns
                reset = next(e for e in events if e['event'] == 'reset')
                self.assertNotEqual(reset.get('session_id'), first_session)
            finally:
                send({'type': 'shutdown'})
                try:
                    proc.wait(5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                proc.stdout.close()
                proc.stdin.close()


if __name__ == '__main__':
    unittest.main()


class GuestSessionRows(unittest.TestCase):
    """The guest sources in the worker (protocol 26.7): a `conversations` request that names
    claude or codex lists their sessions with the tool's own resume argv, rename and pin stay in
    the index, delete drops the row and nothing under ~/.claude or ~/.codex is ever touched.

    The homes are synthetic and `$HOME`, `XDG_DATA_HOME` and `RELAY_INDEX` all point inside the
    test's temporary directory, so neither the owner's transcripts nor the real index is read.
    """

    def setUp(self):
        sys.path.insert(0, str(Path(__file__).parent))
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / 'home'
        (self.home / '.local' / 'share').mkdir(parents=True)
        self.env = mock.patch.dict(os.environ, {'HOME': str(self.home),
                                                'XDG_DATA_HOME': str(self.home / '.local' / 'share'),
                                                'RELAY_INDEX': 'on'})
        self.env.start()
        self.addCleanup(self.env.stop)
        # The throttle is what keeps a rescan off every keystroke; it has a test of its own, and
        # holding these ones to it would mean a five-second wait between two listings.
        quick = mock.patch.object(session_protocol, 'GUEST_RECONCILE_EVERY', 0.0)
        quick.start()
        self.addCleanup(quick.stop)
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)
        self.addCleanup(self.sup.shutdown)
        self.cmds = SessionCommands(self.sup, self.rec)

    # ----- fixtures ---------------------------------------------------------------------------
    def write_claude(self, session_id, *, cwd, prompt='fix the pane drag', title='Pane drag'):
        from test_guest_sessions import claude_lines, write_claude
        return write_claude(self.home, claude_lines(prompts=(prompt,), custom_title=title, cwd=cwd),
                            cwd=cwd, session_id=session_id)

    def ask(self, **extra):
        """One `conversations` request over every source, answered from the index.

        The answer wanted is the one `handle` emits on *this* thread. Counting events and taking
        the next one raced the background refresh, which emits `conversations` of its own — for
        whatever request was latest when it started — so a listing that named only Relay's own
        sources sometimes read back the guest-laden answer of the listing before it.
        """
        request = {'id': 'c', 'scope': 'all',
                   'sources': ['agent', 'terminal', 'claude', 'codex']}
        request.update(extra)
        mine, me, previous = [], threading.get_ident(), self.rec.hook
        self.rec.hook = lambda event: (mine.append(event)
                                       if event.get('event') == 'conversations'
                                       and threading.get_ident() == me else None)
        try:
            self.cmds.handle('conversations', request)
        finally:
            self.rec.hook = previous
        return mine[0]

    def listed(self, event):
        return {item['session_id']: item for item in event['items']}

    def settled(self, want, timeout=10.0):
        """The listing after the background reconcile has caught up with the guests' files."""
        deadline = time.monotonic() + timeout
        while True:
            items = self.listed(self.ask())
            if want(items) or time.monotonic() > deadline:
                return items
            time.sleep(0.05)

    # ----- the pane's own guest, followed while it runs ----------------------------------------
    def claude_turn_lines(self, session_id, *, cwd, index, prompt, reply):
        """One turn's worth of claude lines, in the order claude writes them."""
        from test_guest_sessions import claude_prompt_line, claude_reply_line, claude_tool_result_line
        return [claude_prompt_line(prompt, index=index, session_id=session_id, cwd=cwd),
                claude_tool_result_line(index=index, session_id=session_id, cwd=cwd),
                claude_reply_line(reply, index=index, session_id=session_id, cwd=cwd)]

    def start_claude(self, session_id, *, cwd, prompt='fix the pane drag', reply='Fixed it in Pane.h.'):
        """A transcript with one turn and no title lines, so more turns can be *appended* to it —
        which is what a running guest does, and what the tail is there to read."""
        from test_guest_sessions import claude_header, write_claude
        lines = claude_header(session_id) + self.claude_turn_lines(
            session_id, cwd=cwd, index=0, prompt=prompt, reply=reply)
        return write_claude(self.home, lines, cwd=cwd, session_id=session_id)

    def append_turn(self, path, session_id, *, cwd, index, prompt, reply):
        with path.open('a', encoding='utf-8') as handle:
            for line in self.claude_turn_lines(session_id, cwd=cwd, index=index, prompt=prompt, reply=reply):
                handle.write(json.dumps(line) + '\n')
        return path

    def wire_agent(self, provider=None, cwd=''):
        """A `configure`'s hand-off: the pane's new Agent, which may be a guest harness (Tier A),
        and its `ProgramControl`, which is where its `program_state` messages land (Tier B)."""
        from types import SimpleNamespace
        from relay_core.program_input import ProgramControl
        control = ProgramControl(lambda event: None, threading.Event())
        self.cmds.note_agent(SimpleNamespace(
            provider=provider, executor=SimpleNamespace(program=control,
                                                        workspace=SimpleNamespace(root=cwd))))
        return control

    def pane_program(self):
        """The pane's `ProgramControl` with no guest behind the prompt box: Tier B only."""
        return self.wire_agent()

    def tier_a_agent(self, session_id, *, cwd):
        """A pane whose *agent* is a guest harness (29.3). On `guest_harness_fake`: no test here
        starts a real claude either."""
        from guest_harness_fake import FakeHarness
        from relay_core import guest_harness_provider as ghp
        from relay_core.provider import ProviderConfig
        harness = FakeHarness([], session_id=session_id)
        harness.start(cwd=cwd)
        provider = ghp.HarnessProvider(ProviderConfig('harness://claude', 'claude-fake', '', {}, 32_768),
                                       harness, 'claude')
        self.addCleanup(provider.close)
        return provider, self.wire_agent(provider, cwd=cwd)

    def test_a_tier_a_pane_tails_the_session_its_harness_holds(self):
        """29.3: the pane's agent *is* the guest, and the harness knows which of the guest's own
        sessions it is — nothing has to tell Relay. What the user runs in the terminal below is
        their own shell and may not take the tail off the pane's agent."""
        self.no_reconcile()
        cwd = str(self.root / 'repo')
        mine = '44444444-0000-4000-8000-000000000044'
        theirs = '44444444-0000-4000-8000-000000000045'
        path = self.start_claude(mine, cwd=cwd, prompt='the harness')
        self.start_claude(theirs, cwd=cwd, prompt='the terminal')
        provider, control = self.tier_a_agent(mine, cwd=cwd)
        self.assertEqual(mine, provider.session_id)
        items = self.settled(lambda rows: mine in rows)
        self.assertIn(mine, items)
        self.assertEqual(path, self.cmds.guest_tail.path)
        control.update({'guest': 'claude', 'guest_session': theirs})
        self.assertEqual(path, self.cmds.guest_tail.path)
        self.assertNotIn(theirs, self.listed(self.ask()))

    def no_reconcile(self):
        """Hold the background scan off. Anything that reaches the listing after this came from
        the tail, which is the whole claim these tests make."""
        from relay_core import guest_sessions
        nothing = {'added': 0, 'refreshed': 0, 'removed': 0, 'ms': 0}
        patch = mock.patch.object(guest_sessions, 'reconcile', lambda index, *a, **k: dict(nothing))
        patch.start()
        self.addCleanup(patch.stop)

    def test_a_tier_b_guest_is_tailed_without_waiting_for_a_reconcile(self):
        """The guest runs in the pane's own shell, so the worker hears of it only through
        `program_state` — which carries the guest and the session it was launched with. From then
        on its row follows the transcript, with the reconcile held off entirely."""
        self.no_reconcile()
        cwd = str(self.root / 'repo')
        session = '77777777-0000-4000-8000-000000000077'
        path = self.start_claude(session, cwd=cwd)
        control = self.pane_program()
        self.assertIsNone(self.cmds.guest_tail)
        control.update({'guest': 'claude', 'guest_session': session})
        items = self.settled(lambda rows: session in rows)
        self.assertIn(session, items, 'the tail did not index the running session')
        self.assertEqual(2, items[session]['message_count'])       # one prompt and one reply
        self.assertEqual(path, self.cmds.guest_tail.path)
        # …and it keeps up: the guest answers again and the next listing has the new turn.
        self.append_turn(path, session, cwd=cwd, index=1, prompt='and the drop?', reply='Done.')
        before = path.read_bytes()
        items = self.settled(lambda rows: rows.get(session, {}).get('message_count') == 4)
        self.assertEqual(4, items[session]['message_count'])
        self.assertEqual(before, path.read_bytes(), 'the transcript belongs to the guest')

    def test_the_row_moves_while_nobody_is_typing(self):
        """The Sessions pane re-lists when the user touches it, not on a timer. A guest answering
        in front of an open, idle pane still moves its row: every `program_state` the pane sends
        while the guest works is a tick, and a tick that finds new turns sends the listing the
        pane last asked for over again, under that listing's own id."""
        self.no_reconcile()
        quick = mock.patch.object(session_protocol, 'GUEST_TAIL_POLL_EVERY', 0.0)
        quick.start()
        self.addCleanup(quick.stop)
        cwd = str(self.root / 'repo')
        session = '33333333-0000-4000-8000-000000000033'
        path = self.start_claude(session, cwd=cwd)
        control = self.pane_program()
        control.update({'guest': 'claude', 'guest_session': session})
        self.settled(lambda rows: session in rows)     # the pane has listed at least once
        state = self.cmds._guest_state()
        deadline = time.monotonic() + 10
        while state['running'] and time.monotonic() < deadline:
            time.sleep(0.01)
        # From here nothing asks for a listing. The guest answers, and all the pane says is that
        # its guest is busy.
        self.append_turn(path, session, cwd=cwd, index=1, prompt='and the drop?', reply='Done.')
        control.update({'guest': 'claude', 'guest_busy': True})
        event = self.rec.wait(lambda e: e['event'] == 'conversations' and any(
            item.get('session_id') == session and item.get('message_count') == 4
            for item in e['items']))
        self.assertEqual('c', event['id'])

    def test_two_claudes_in_one_directory_tail_their_own_sessions(self):
        """Why the session id is not optional (GT7X review, B7): two claudes in one project write
        two transcripts in the same folder, so "the newest file" is whichever of them typed last."""
        self.no_reconcile()
        cwd = str(self.root / 'repo')
        mine = '88888888-0000-4000-8000-000000000088'
        theirs = '88888888-0000-4000-8000-000000000089'
        path = self.start_claude(mine, cwd=cwd, prompt='mine')
        other = self.start_claude(theirs, cwd=cwd, prompt='theirs')   # written second: the newest
        os.utime(other, (path.stat().st_mtime + 10, path.stat().st_mtime + 10))
        control = self.pane_program()
        control.update({'guest': 'claude', 'guest_session': mine})
        items = self.settled(lambda rows: mine in rows)
        self.assertEqual(path, self.cmds.guest_tail.path)
        self.assertNotIn(theirs, items, 'the pane indexed the other claude of the same directory')
        # The other one answers. It is not this pane's session, so nothing of it reaches the row.
        self.append_turn(other, theirs, cwd=cwd, index=1, prompt='theirs again', reply='Done.')
        items = self.settled(lambda rows: theirs in rows, timeout=1.0)
        self.assertNotIn(theirs, items)
        self.assertEqual(2, items[mine]['message_count'])
        self.assertEqual(path, self.cmds.guest_tail.path)

    def test_indexing_the_guests_off_means_no_tail(self):
        """Options > Privacy (review B1): off, Relay opens nothing of the guests' — including the
        session running in the pane. It stops one that is already following, because the setting
        can be turned off while the guest is answering."""
        self.no_reconcile()
        cwd = str(self.root / 'repo')
        session = '66666666-0000-4000-8000-000000000066'
        path = self.start_claude(session, cwd=cwd)
        self.cmds.index_guests = False
        control = self.pane_program()
        control.update({'guest': 'claude', 'guest_session': session})
        items = self.settled(lambda rows: session in rows, timeout=1.0)
        self.assertNotIn(session, items)
        self.assertIsNone(self.cmds.guest_tail)
        # On again: the same want is still held, so the row arrives without another program_state.
        self.ask(index_guests=True)
        self.assertIn(session, self.settled(lambda rows: session in rows))
        self.assertIsNotNone(self.cmds.guest_tail)
        # And off again while it runs.
        self.ask(index_guests=False)
        self.settled(lambda rows: self.cmds.guest_tail is None, timeout=5.0)
        self.assertIsNone(self.cmds.guest_tail)
        self.assertTrue(path.exists(), 'the transcript belongs to the guest')

    def test_the_guest_leaving_stops_the_tail(self):
        self.no_reconcile()
        cwd = str(self.root / 'repo')
        session = '55555555-0000-4000-8000-000000000055'
        self.start_claude(session, cwd=cwd)
        control = self.pane_program()
        control.update({'guest': 'claude', 'guest_session': session})
        self.settled(lambda rows: session in rows)
        self.assertIsNotNone(self.cmds.guest_tail)
        # A `program_state` that names the guest but no session says nothing about which session
        # is being written, so it leaves the tail alone…
        control.update({'guest': 'claude'})
        self.assertIsNotNone(self.cmds.guest_tail)
        # …and the guest exiting stops it. The row stays in the index; the reconcile owns it again.
        control.update({'guest': ''})
        self.assertIsNone(self.cmds.guest_tail)
        self.assertIn(session, self.listed(self.ask()))

    # ----- the listing ------------------------------------------------------------------------
    def test_a_guest_session_is_listed_with_its_own_resume_command(self):
        cwd = str(self.root / 'repo')
        session = 'aaaaaaaa-0000-4000-8000-00000000000a'
        path = self.write_claude(session, cwd=cwd)
        # The first answer comes out of the index, which has not seen the guests yet: the
        # reconcile runs behind it (see test_the_reconcile_runs_behind_the_answer).
        items = self.settled(lambda rows: session in rows)
        self.assertIn(session, items)
        row = items[session]
        self.assertEqual('claude', row['source'])
        self.assertEqual(['claude', '-r', session], row['resume_command'])
        self.assertEqual(['claude', '-r', session, '--fork-session'], row['fork_command'])
        self.assertEqual(cwd, row['resume_cwd'])
        self.assertEqual(cwd, row['workspace'])
        self.assertEqual(session, row['id'])
        # Relay's own rows are untouched by the annotation.
        self.assertTrue(all('resume_command' not in item for item in self.ask()['items']
                            if item.get('source') not in ('claude', 'codex')))
        self.assertTrue(path.exists())

    def test_only_a_request_that_names_a_guest_lists_one(self):
        session = 'bbbbbbbb-0000-4000-8000-00000000000b'
        self.write_claude(session, cwd=str(self.root / 'repo'))
        self.settled(lambda rows: session in rows)
        event = self.ask(sources=['agent', 'terminal'])
        self.assertNotIn(session, self.listed(event))
        self.assertIn(session, self.listed(self.ask(sources=['claude'])))

    def test_an_unknown_source_is_refused_by_name(self):
        with self.assertRaises(ValueError) as caught:
            self.cmds.handle('conversations', {'id': 'c', 'sources': ['gemini']})
        for name in ('agent', 'terminal', 'subagent', 'claude', 'codex'):
            self.assertIn(name, str(caught.exception))

    def test_the_reconcile_runs_behind_the_answer(self):
        """The listing may never wait on the guests' files: the first run over a long claude
        history is seconds of parsing. The answer goes out, the rescan follows, and only a
        rescan that changed something sends the list again."""
        from relay_core import guest_sessions
        started, release = threading.Event(), threading.Event()
        real = guest_sessions.reconcile

        def blocking(index, *args, **kwargs):
            started.set()
            release.wait(5)
            return real(index, *args, **kwargs)

        session = 'cccccccc-0000-4000-8000-00000000000c'
        self.write_claude(session, cwd=str(self.root / 'repo'))
        with mock.patch.object(guest_sessions, 'reconcile', blocking):
            first = self.ask()                       # returns while the reconcile is blocked
            self.assertTrue(started.wait(5))
            self.assertNotIn(session, self.listed(first))
            release.set()
            # The reconcile found a session, so the same request id is answered a second time.
            self.rec.wait(lambda e: e['event'] == 'conversations'
                          and any(item.get('source') == 'claude' for item in e['items']))
        # Nothing changed since: a further request sends no extra answer of its own.
        self.assertIn(session, self.settled(lambda rows: session in rows))

    # ----- rename, pin and delete are index-only ------------------------------------------------
    def test_rename_and_pin_stay_in_the_index_and_survive_a_rescan(self):
        from relay_core import guest_sessions
        cwd = str(self.root / 'repo')
        session = 'dddddddd-0000-4000-8000-00000000000d'
        path = self.write_claude(session, cwd=cwd)
        self.settled(lambda rows: session in rows)
        self.cmds.handle('conversation_rename', {'session_id': session, 'title': 'Reading the drag'})
        self.rec.wait(lambda e: e['event'] == 'conversation_renamed' and e['session_id'] == session)
        self.cmds.handle('conversation_pin', {'session_id': session, 'pinned': True})
        self.rec.wait(lambda e: e['event'] == 'conversation_pinned' and e['session_id'] == session)
        row = self.listed(self.ask())[session]
        self.assertEqual('Reading the drag', row['title'])
        self.assertEqual(1, row['pinned'])
        # Nothing was written beside the transcript, and the transcript itself is untouched.
        self.assertEqual([path.name], [p.name for p in path.parent.iterdir()])
        before = path.read_bytes()
        # Every listing above set a background reconcile going — the throttle is 0 in these tests —
        # and one of those sometimes got to the touched file first, so the refresh this call is
        # counting had already happened and `refreshed` came back 0. Turn the throttle up so no new
        # one starts, and wait for any in flight to finish; then the rescan below is the only one.
        with mock.patch.object(session_protocol, 'GUEST_RECONCILE_EVERY', 3600.0):
            state = self.cmds._guest_state()
            deadline = time.monotonic() + 10
            while state['running'] and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertFalse(state['running'], 'a background reconcile is still going')
            # A rescan that re-reads the file keeps both: update_guest merges them per key.
            os.utime(path, (path.stat().st_mtime + 10, path.stat().st_mtime + 10))
            outcome = guest_sessions.reconcile(self.cmds.index())
            self.assertEqual(1, outcome['refreshed'])
            row = self.listed(self.ask())[session]
        self.assertEqual(('Reading the drag', 1), (row['title'], row['pinned']))
        self.assertEqual(before, path.read_bytes())

    def test_indexing_the_guests_is_a_setting_that_rides_the_listing(self):
        """Options › Privacy (GT7X review, B1): the pane sends `index_guests` with every listing.
        Off, the guests' rows leave Relay's index and their files are not even read; on again,
        they come back. The transcript itself is never touched either way."""
        cwd = str(self.root / 'repo')
        session = 'dddddddd-0000-4000-8000-00000000000d'
        path = self.write_claude(session, cwd=cwd)
        self.assertIn(session, self.settled(lambda rows: session in rows))
        self.ask(index_guests=False)
        self.assertNotIn(session, self.settled(lambda rows: session not in rows))
        self.assertIs(False, self.cmds.index_guests)
        self.assertTrue(path.exists(), 'the transcript belongs to the guest')
        self.ask(index_guests=True)
        self.assertIn(session, self.settled(lambda rows: session in rows))
        with self.assertRaises(ValueError):
            self.cmds.handle('conversations', {'id': 'c', 'index_guests': 'no'})

    def test_delete_drops_the_row_and_leaves_the_guests_transcript(self):
        cwd = str(self.root / 'repo')
        session = 'eeeeeeee-0000-4000-8000-00000000000e'
        path = self.write_claude(session, cwd=cwd)
        self.settled(lambda rows: session in rows)
        self.cmds.handle('conversation_delete', {'session_id': session})
        event = self.rec.wait(lambda e: e['event'] == 'conversation_deleted' and e['session_id'] == session)
        self.assertEqual(0, event['files'])
        self.assertTrue(path.exists(), 'the transcript belongs to the guest')
        # The row stays gone. The transcript is still there and still parses, so before the
        # forgotten record (GT7X review, B2) the next reconcile put the session the user had
        # just deleted straight back into the pane.
        from relay_core import guest_sessions
        self.assertNotIn(session, self.listed(self.ask()))
        guest_sessions.reconcile(self.cmds.index())
        self.assertNotIn(session, self.listed(self.ask()))
        # …until it is asked for again, which is what a "show forgotten" listing would do.
        self.cmds.index().unforget('claude', session)
        self.assertIn(session, self.settled(lambda rows: session in rows))

    def test_a_row_whose_transcript_went_away_is_pruned(self):
        cwd = str(self.root / 'repo')
        session = 'ffffffff-0000-4000-8000-00000000000f'
        path = self.write_claude(session, cwd=cwd)
        self.settled(lambda rows: session in rows)
        path.unlink()
        self.settled(lambda rows: session not in rows)
        self.assertNotIn(session, self.listed(self.ask()))

    # ----- the id the guest chose ----------------------------------------------------------------
    def test_a_guest_id_is_accepted_where_relays_own_shape_is_not(self):
        from relay_core.sessions import SESSION_ID
        cwd = str(self.root / 'repo')
        session = '11111111-0000-4000-8000-000000000011'
        self.write_claude(session, cwd=cwd, prompt='where does the drag go')
        self.settled(lambda rows: session in rows)
        self.assertIsNone(SESSION_ID.match(session))      # not Relay's 32 hex digits
        self.cmds.handle('conversation_get', {'id': 'g', 'session_id': session, 'query': 'drag'})
        event = self.rec.wait(lambda e: e['event'] == 'conversation' and e.get('id') == 'g')
        self.assertEqual(session, event['session_id'])
        # An id no guest row holds is still refused, in every shape.
        for bad in ('22222222-0000-4000-8000-000000000022', 'nonsense', '', 'x' * 400):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.cmds.handle('conversation_get', {'id': 'g', 'session_id': bad})

    # ----- the cost of keeping the rows fresh ------------------------------------------------------
    def test_a_rescan_is_not_queued_per_keystroke(self):
        """The pane queries on every keystroke; the guests' files do not change that fast, so a
        listing sets a reconcile going at most once every `GUEST_RECONCILE_EVERY` seconds."""
        from relay_core import guest_sessions
        session = '99999999-0000-4000-8000-000000000099'
        self.write_claude(session, cwd=str(self.root / 'repo'))
        calls = []
        real = guest_sessions.reconcile
        # The reconcile is held on its own thread until all three listings are in. It used to run
        # as soon as the first listing scheduled it, and on a loaded machine it *finished* before
        # the third query was typed, so the second answer came back for 'drag' — which is what this
        # test is about, and therefore not something to leave to how fast the machine is.
        asked_everything = threading.Event()
        self.addCleanup(asked_everything.set)

        def held(index, *a, **k):
            calls.append(1)
            asked_everything.wait(30)
            return real(index, *a, **k)

        with mock.patch.object(session_protocol, 'GUEST_RECONCILE_EVERY', 60.0), \
             mock.patch.object(guest_sessions, 'reconcile', held):
            for query in ('', 'drag', 'pane'):
                self.ask(query=query)
            asked_everything.set()
            # The rows the rescan found are sent again for the *latest* query, not for the one
            # that set it going: by then the user has typed two more letters.
            event = self.rec.wait(lambda e: e['event'] == 'conversations' and e['items']
                                  and e['items'][0].get('source') == 'claude')
            self.assertEqual('pane', event['query'])
            for query in ('drag', 'pane'):
                self.ask(query=query)
        self.assertEqual(1, len(calls))

    def test_a_warm_reconcile_reads_nothing(self):
        """Measured on a synthetic home, not the owner's: the first pass parses every transcript,
        and the second reads none of them, because a file whose mtime matches the indexed one is
        never opened. That is what lets the sessions pane reconcile on every query."""
        from relay_core import guest_sessions
        sessions = [f'{n:08x}-0000-4000-8000-00000000{n:04x}' for n in range(120)]
        for n, session in enumerate(sessions):
            self.write_claude(session, cwd=str(self.root / f'repo{n % 8}'), title=f'session {n}')
        index = self.cmds.index()
        cold = guest_sessions.reconcile(index)
        self.assertEqual((120, 0, 0), (cold['added'], cold['refreshed'], cold['removed']))
        warm = guest_sessions.reconcile(index)
        self.assertEqual((0, 0, 0), (warm['added'], warm['refreshed'], warm['removed']))
        self.assertLessEqual(warm['ms'], max(50, cold['ms']))
        print(f'\n  guest reconcile over {len(sessions)} synthetic claude sessions: '
              f'cold {cold["ms"]} ms, warm {warm["ms"]} ms', file=sys.stderr)
