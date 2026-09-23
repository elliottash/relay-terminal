# SPDX-License-Identifier: AGPL-3.0-or-later
"""Guest MCP -> real Relay manager/tasks/transcripts, with fake models only."""
import json
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from relay_core.agent import Agent
from relay_core.agents_defs import load_catalog
from relay_core.guest_board_bridge import Bridge, DELEGATION_ALLOW, exchange
from relay_core import guest_harness_provider as P
from relay_core.guest_harness import TurnResult
from relay_core.subagents import SubagentFactory, SubagentManager
from tests.guest_harness_fake import FakeHarness, ev
from tests.test_subagents import CONFIG, Hub, SubProvider
from tests.test_guest_harness_codex import HarnessCase as CodexBase, load


class DelegationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.events = []
        self.hub = Hub()
        self.harness = FakeHarness(guest='codex')
        with mock.patch.object(P, 'make_harness', return_value=self.harness):
            self.provider = P.start_provider('guest:codex', {}, self.tmp.name)
        self.addCleanup(self.provider.close)
        self.agent = Agent(self.provider.config, self.tmp.name, self.events.append,
                           provider=self.provider, completion_check=False,
                           session_dir=self.tmp.name + '/sessions')
        P.attach(self.agent, self.provider)
        self.manager = SubagentManager(self.events.append)
        self.manager.configure(load_catalog(self.tmp.name, []), SubagentFactory(
            CONFIG, self.tmp.name, provider_factory=lambda config: SubProvider(self.hub)))
        self.manager.attach(self.agent)
        self.addCleanup(self.manager.shutdown)
        self.bridge = self.provider.board_bridge
        self.cap = {'socket': self.bridge.path, 'token': self.bridge.token}
        self.bridge.begin(self.agent.cancel_event)
        self.count = 0

    def call(self, name, args, key=None):
        self.count += 1
        return exchange(self.cap, 'tools/call', {'name': name, 'arguments': args}, key or str(self.count))

    def spawn(self, **extra):
        return self.call('agent', dict(description='Review chapter', prompt='chapter gate:review',
                                       subagent_type='general', background=extra.pop('background', True), **extra))

    def test_discovery_before_binding_and_without_board(self):
        cold = Bridge(False)
        self.addCleanup(cold.close)
        self.assertEqual({s['name'] for s in cold.specs()} & DELEGATION_ALLOW, DELEGATION_ALLOW)
        specs = exchange(self.cap, 'tools/list')['tools']
        self.assertEqual({s['name'] for s in specs} & DELEGATION_ALLOW, DELEGATION_ALLOW)
        self.assertEqual(next(s for s in specs if s['name'] == 'agent_wait')
                         ['inputSchema']['properties']['timeout_seconds']['maximum'], 1800)
        self.assertNotIn('enum', next(s for s in specs if s['name'] == 'agent')
                         ['inputSchema']['properties']['background'])
        self.agent.subagents = None
        self.assertEqual({s['name'] for s in self.bridge.specs()} & DELEGATION_ALLOW, {'update_todos'})

    def test_task_child_events_result_and_saved_transcript(self):
        tasks = self.call('update_todos', {'items': [{'text': 'Review chapter', 'status': 'pending'}]})
        task = tasks['items'][0]['id']
        child = self.spawn(todo_id=task)
        self.assertTrue(child['background'])
        self.assertEqual(self.agent.todos.items[0]['subagent'], child['id'])
        self.assertEqual(self.agent.todos.items[0]['status'], 'in_progress')
        self.manager.subscribe(child['id'], True)
        self.assertTrue(any(e['event'] == 'subagent_transcript' for e in self.events))
        self.hub.open('review')
        result = self.call('agent_wait', {'id': child['id']})
        self.assertIn('REPORT[chapter]', result['agents'][0]['result'])
        self.assertEqual(self.agent.todos.items[0]['status'], 'completed')
        sub = self.manager._agents[child['id']]
        data = self.manager.thread_data(sub)
        self.assertEqual(data['owner_session'], self.agent.session_id)
        saved = json.loads(self.agent.store.thread_path(self.agent.session_id, data['id']).read_text())
        self.assertEqual(saved['status'], 'done')
        self.assertTrue(any('REPORT[chapter]' in m.get('content', '') for m in saved['messages']))
        self.assertTrue(any(e['event'] == 'subagent_finished' for e in self.events))
        self.assertEqual(self.call('agent_message', {'id': child['id'], 'text': 'Check again'})['delivered'], 'resumed')
        self.assertFalse(self.call('agent_wait', {'id': child['id']})['timed_out'])

    def test_deduplicates_spawn_and_refuses_replay_after_stop(self):
        args = dict(description='Review', prompt='chapter gate:review', subagent_type='general', background=True)
        first = self.call('agent', args, 'same')
        self.assertEqual(first, self.call('agent', args, 'same'))
        self.assertEqual(len(self.manager.list()), 1)
        self.agent.cancel_event.set()
        self.assertEqual(self.call('agent', args)['code'], 'unavailable')
        self.hub.open('review')

    def test_plan_mode_spawns_children(self):
        self.agent.mode = 'plan'
        self.assertIn('id', self.spawn())
        self.hub.open('review')

    def test_readonly_and_card_scope_cannot_spawn(self):
        self.agent.readonly_turn = True
        self.assertIn('writes nothing', self.spawn()['error'])
        self.agent.readonly_turn = False
        self.agent.card_turn = ('discuss', 'TEST')
        self.assertIn('error', self.spawn())
        self.assertEqual(self.manager.list(), [])

    def test_wait_uses_native_timeout_and_cancel_interrupts(self):
        child = self.spawn()
        with mock.patch.object(self.manager, 'wait', return_value={'timed_out': True}) as wait:
            self.call('agent_wait', {'id': child['id'], 'timeout_seconds': 1800})
            self.assertEqual(wait.call_args.args[1], 1800)
        results = []
        waiter = threading.Thread(target=lambda: results.append(self.call('agent_wait', {'id': child['id']})))
        waiter.start()
        self.agent.cancel_event.set()
        waiter.join(2)
        self.assertFalse(waiter.is_alive())
        self.assertIn('error', results[0])
        self.hub.open('review')

    def test_foreground_child_waits_and_stop_can_revoke_it(self):
        reports = []
        foreground = threading.Thread(target=lambda: reports.append(self.spawn(background=False)))
        foreground.start()
        self.assertTrue(self.hub.started.wait(2))
        self.assertTrue(foreground.is_alive())
        self.hub.open('review')
        foreground.join(3)
        self.assertFalse(foreground.is_alive())
        self.assertEqual(reports[0]['status'], 'done', reports)
        self.assertIn('REPORT[chapter]', reports[0]['result'])

        self.hub.started.clear()
        blocked = threading.Thread(target=lambda: reports.append(self.call('agent', {
            'description': 'Review another chapter', 'prompt': 'other gate:blocked',
            'subagent_type': 'general', 'background': False})))
        blocked.start()
        self.assertTrue(self.hub.started.wait(2))
        self.agent.cancel_event.set()
        blocked.join(3)
        self.assertFalse(blocked.is_alive())
        self.assertIn('error', reports[1])

    def test_pending_child_result_does_not_replace_user_prompt(self):
        self.harness.script = [{'result': ('Compared', 'end', {})}]
        self.agent.todos.replace({'items': [{'text': 'Compare the chapters',
                                             'status': 'in_progress'}]}, [], 'turn')
        self.agent.messages += [{'role': 'assistant', 'content': 'Earlier reply'},
                                {'role': 'user', 'content': 'Now compare the chapters'},
                                {'role': 'user', 'relay_kind': 'note', 'content': 'Child A report'},
                                {'role': 'user', 'relay_kind': 'note', 'content': 'Child B report'}]
        self.provider.complete(self.agent.messages, [], self.events.append, self.agent.cancel_event)
        prompt = self.harness.sent[-1]['prompt']
        for expected in ('Now compare the chapters', 'Child A report', 'Child B report', 'Relay tasks'):
            self.assertIn(expected, prompt)

    def test_empty_tasks_do_not_add_a_guest_prompt_block(self):
        self.harness.script = [{'result': ('Done', 'end', {})}]
        self.agent.messages.append({'role': 'user', 'content': 'Review the change'})
        self.provider.complete(self.agent.messages, [],
                               self.events.append, self.agent.cancel_event)
        self.assertEqual(self.harness.sent[-1]['prompt'], 'Review the change')

    def test_bridge_result_keeps_native_child_link(self):
        turn = P._Turn(self.provider, self.agent, None, self.events.append, self.agent.cancel_event)
        turn._on_tool_started({'call_id': 'spawn', 'tool': 'other', 'input': {
            'server': 'relay_board', 'tool': 'agent', 'arguments': {'description': 'Review'}}})
        turn._on_tool_result({'call_id': 'spawn', 'ok': True, 'output': json.dumps({
            'content': [{'type': 'text', 'text': json.dumps({'id': 'a1', 'background': True})}]})})
        event = self.events[-1]
        self.assertEqual(event['result']['id'], 'a1')
        self.assertEqual(event['label']['open'], {'type': 'subagent', 'id': 'a1'})

    def test_inherited_guest_child_is_lazy_resumes_and_closes(self):
        children = [FakeHarness(guest='codex', session_id='child-session', script=[{
            'events': [ev('tool_started', call_id='read', tool='read_file', input={'path': 'chapter.md'}),
                       ev('tool_result', call_id='read', tool='read_file', output='Chapter text', ok=True),
                       ev('delta', text='Child report')], 'result': ('Child report', 'end', {})}]),
                    FakeHarness(guest='codex', session_id='child-session', script=[{
                        'result': ('Follow-up report', 'end', {})}])]
        self.manager.configure(load_catalog(self.tmp.name, []), SubagentFactory(
            self.provider.config, self.tmp.name, main_agent=self.agent))
        with mock.patch.object(P, 'make_harness', side_effect=children) as make:
            child = self.call('agent', {'description': 'Guest review', 'prompt': 'Review chapter',
                                        'subagent_type': 'general', 'background': True})
            result = self.call('agent_wait', {'id': child['id']})
            self.assertEqual(result['agents'][0]['status'], 'done', result)
            self.assertIn('Child report', result['agents'][0]['result'])
            self.assertTrue(children[0].closed)
            self.assertIn('You cannot start other agents', children[0].instructions)
            sub = self.manager._agents[child['id']]
            saved = json.loads(self.agent.store.thread_path(self.agent.session_id, sub.thread_id).read_text())
            self.assertTrue(any(m.get('tool_calls') for m in saved['messages']))
            self.assertFalse({s['name'] for s in sub.agent._guest_session_data.board_bridge.specs()} & DELEGATION_ALLOW)
            self.call('agent_message', {'id': child['id'], 'text': 'Follow up'})
            followed = self.call('agent_wait', {'id': child['id']})
            self.assertEqual(followed['agents'][0]['status'], 'done', followed)
            self.assertEqual(children[1].starts[0]['resume'], 'child-session')
            self.assertTrue(children[1].closed)
            self.assertEqual(make.call_count, 2)

    def test_guest_general_start_failure_and_model_change(self):
        from relay_core.provider import ProviderConfig
        factory = SubagentFactory(self.provider.config, self.tmp.name, main_agent=self.agent)
        self.manager.configure(load_catalog(self.tmp.name, []), factory)
        failed = FakeHarness(guest='codex', start_error=RuntimeError('startup failed'))
        with mock.patch.object(P, 'make_harness', return_value=failed):
            child = self.call('agent', {'description': 'Investigate', 'prompt': 'Read only',
                                        'subagent_type': 'general', 'background': True})
            result = self.call('agent_wait', {'id': child['id']})
        self.assertEqual(result['agents'][0]['status'], 'failed', result)
        self.assertEqual(failed.starts[0]['permissions'], 'bypass')
        self.assertTrue(failed.closed)
        sub = self.manager._agents[child['id']]
        next_config = ProviderConfig('harness://codex', 'another-model', '')
        self.manager._apply_model(sub, next_config, 'guest:codex')
        self.assertEqual(sub.agent.provider.config.model, 'another-model')
        self.assertIsNone(sub.agent.provider.session_id)
        self.manager._apply_model(sub, CONFIG, None)
        self.assertIs(sub.agent.provider.config, CONFIG)

    def test_guest_blocked_report_persists_and_general_inherits_permissions(self):
        self.manager.configure(load_catalog(self.tmp.name, []), SubagentFactory(
            self.provider.config, self.tmp.name, main_agent=self.agent))
        self.call('update_todos', {'items': [{'text': 'Inspect checkout', 'status': 'pending'}]})
        report = 'Status: blocked\nCannot read checkout: sandbox initialization failed.'
        child_harness = FakeHarness(guest='codex', script=[{'result': (report, 'end', {})}])
        with mock.patch.object(P, 'make_harness', return_value=child_harness):
            child = self.call('agent', {'description': 'Inspect checkout', 'prompt': 'Investigate; do not edit',
                                       'subagent_type': 'general', 'todo_id': 'T1', 'background': True})
            result = self.call('agent_wait', {'id': child['id']})
        self.assertEqual(child_harness.starts[0]['permissions'], self.provider.permissions)
        self.assertIn('Status: blocked', child_harness.instructions)
        self.assertEqual(result['agents'][0]['status'], 'blocked', result)
        self.assertIn(report, result['agents'][0]['result'])
        self.assertEqual(self.agent.todos.items[0]['status'], 'blocked')
        sub = self.manager._agents[child['id']]
        saved = json.loads(self.agent.store.thread_path(self.agent.session_id, sub.thread_id).read_text())
        self.assertEqual(saved['status'], 'blocked')

    def test_custom_readonly_guest_keeps_explicit_restriction(self):
        from relay_core.agents_defs import AgentDefinition, READ_ONLY_TOOLS
        factory = SubagentFactory(self.provider.config, self.tmp.name, main_agent=self.agent)
        definition = AgentDefinition('reader', 'Read only', tools=READ_ONLY_TOOLS, read_only=True)
        self.assertEqual(factory.guest_provider(self.provider.config, definition, None, 'a1').permissions, 'deny')

    def test_stopping_guest_closes_harness_and_routes_questions(self):
        from relay_core.provider import Cancelled
        entered = threading.Event()
        def running(prompt, attachments, emit, cancel, harness):
            entered.set()
            cancel.wait(3)
            if cancel.is_set():
                raise Cancelled('Stopped.')
            return TurnResult('late', 'end', {})
        harness = FakeHarness(guest='codex', script=[running])
        self.manager.configure(load_catalog(self.tmp.name, []), SubagentFactory(
            self.provider.config, self.tmp.name, main_agent=self.agent))
        with mock.patch.object(P, 'make_harness', return_value=harness):
            child = self.call('agent', {'description': 'Stop me', 'prompt': 'Wait',
                                        'subagent_type': 'general', 'background': True})
            self.assertTrue(entered.wait(2))
            sub = self.manager._agents[child['id']]
            with mock.patch.object(sub.agent.provider.live, 'resolve_question', return_value=True) as resolve:
                self.assertTrue(self.manager.resolve_question({'id': 'child-question'}))
                resolve.assert_called_once()
            self.manager.stop(child['id'])
            result = self.call('agent_wait', {'id': child['id']})
            self.assertEqual(result['agents'][0]['status'], 'stopped')
        self.assertTrue(harness.closed)


class CodexDelegationTests(unittest.TestCase):
    def setUp(self):
        CodexBase.setUp(self)

    def test_launch_and_start_resume_fork_disable_internal_agents(self):
        harness, proc = CodexBase.harness(self, load('ok-turn.jsonl'))
        spawn = harness._spawn
        seen = []
        harness._spawn = lambda argv, cwd: (seen.append(argv), spawn(argv, cwd))[1]
        descriptor = {'command': '/python', 'args': ['/proxy', '/cap']}
        harness.start(cwd='/tmp', board_bridge=descriptor)
        self.assertIn('features.multi_agent=false', seen[0])
        self.assertIn('features.multi_agent_v2=false', seen[0])
        with mock.patch.object(harness, '_request', return_value={}) as request:
            for resume, fork in [(None, False), ('saved', False), ('saved', True)]:
                harness._start_thread(cwd='/tmp', model=None, resume=resume, fork=fork)
                config = request.call_args.args[1]['config']
                self.assertIs(config['features.multi_agent'], False)
                self.assertIs(config['features.multi_agent_v2'], False)


if __name__ == '__main__':
    unittest.main()
