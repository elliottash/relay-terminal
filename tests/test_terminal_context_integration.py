"""Terminal evidence through real native Agent and guest bridge adapters (#TCXT)."""
import json
import tempfile
import threading
import unittest
from unittest import mock

from relay_core.agent import Agent
from relay_core import guest_harness_provider as guest_provider
from relay_core.guest_board_bridge import exchange
from relay_core.guest_harness import TurnResult
from tests.guest_harness_fake import FakeHarness
from tests.test_agent import CONFIG, FakeProvider
from tests.test_terminal_context import record, snapshot


class TerminalContextIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.events = []

    def native(self, calls=(), profile='full'):
        response = {'role': 'assistant', 'content': 'Evidence inspected.'}
        if calls:
            response['tool_calls'] = [
                {'id': f'term-{i}', 'type': 'function', 'function': {
                    'name': name, 'arguments': json.dumps(args)}}
                for i, (name, args) in enumerate(calls)]
        provider = FakeProvider(response)
        agent = Agent(CONFIG, self.tmp.name, self.events.append, provider=provider,
                      track_requests=False, completion_check=False, todo_tool=False,
                      prompt_profile=profile)
        return agent, provider

    def guest(self, requests):
        observed = []
        harness = FakeHarness()
        with mock.patch.object(guest_provider, 'make_harness', return_value=harness):
            provider = guest_provider.start_provider('guest:claude', {}, self.tmp.name)
        self.addCleanup(provider.close)
        agent = Agent(provider.config, self.tmp.name, self.events.append, provider=provider,
                      track_requests=False, completion_check=False, todo_tool=False)
        guest_provider.attach(agent, provider)
        cap = {'socket': provider.board_bridge.path, 'token': provider.board_bridge.token}

        def turn(prompt, attachments, emit, cancel, harness):
            results = [exchange(cap, 'tools/call', {'name': name, 'arguments': args},
                                f'context-{len(observed)}-{i}')
                       for i, (name, args) in enumerate(requests)]
            observed.append({'prompt': prompt, 'results': results,
                             'tools': exchange(cap, 'tools/list')['tools']})
            return TurnResult(text='Evidence inspected.', stop_reason='end', usage={})

        harness.script = [turn, turn]
        return agent, harness, observed

    @staticmethod
    def live_and_queued(agent):
        queued = snapshot(record(output='SELECTED_ORIGINAL', revision=1))
        agent.terminal_context.update(snapshot(
            record(output='SELECTED_LATER', revision=2),
            record('other', output='OTHER_PANE_PRIVATE', pane_id='pane-b')))
        return {'terminal_context': queued}

    def results(self):
        return [e['result'] for e in self.events if e.get('event') == 'tool_result']

    def test_native_payload_and_tools_use_selected_pinned_revision_without_shell(self):
        agent, provider = self.native([
            ('terminal_read', {'command_id': 'one'}),
            ('terminal_read', {'command_id': 'other'}),
            ('terminal_read', {'command_id': 'one', 'fresh': True}),
            ('terminal_history', {}),
        ])
        context = self.live_and_queued(agent)
        with mock.patch.object(agent.executor, 'execute', side_effect=AssertionError('must not execute shell')) as execute:
            agent.ask('What did my command print?', context=context)
        execute.assert_not_called()
        # Inspect submitted user context, separately from subsequent tool responses.
        payload = json.dumps([m for m in provider.messages if m['role'] == 'user'])
        self.assertIn('SELECTED_ORIGINAL', payload)
        self.assertNotIn('SELECTED_LATER', payload)
        self.assertNotIn('OTHER_PANE_PRIVATE', payload)
        pinned, denied, fresh, history = self.results()
        self.assertEqual(pinned['output'], 'SELECTED_ORIGINAL')
        self.assertEqual(pinned['record']['revision'], 1)
        self.assertFalse(denied['ok'])
        self.assertEqual(fresh['output'], 'SELECTED_LATER')
        self.assertEqual([r['command_id'] for r in history['records']], ['one'])

    def test_guest_payload_and_bridge_use_selected_pinned_revision_without_shell(self):
        agent, harness, observed = self.guest([
            ('terminal_read', {'command_id': 'one'}),
            ('terminal_read', {'command_id': 'other'}),
            ('terminal_read', {'command_id': 'one', 'fresh': True}),
        ])
        context = self.live_and_queued(agent)
        with mock.patch.object(agent.executor, 'execute', side_effect=AssertionError('must not execute shell')) as execute:
            agent.ask('What did my command print?', context=context)
        execute.assert_not_called()
        self.assertEqual(len(observed), 1)
        payload = observed[0]['prompt']
        self.assertIn('SELECTED_ORIGINAL', payload)
        self.assertNotIn('SELECTED_LATER', payload)
        self.assertNotIn('OTHER_PANE_PRIVATE', payload)
        pinned, denied, fresh = observed[0]['results']
        self.assertEqual(pinned['output'], 'SELECTED_ORIGINAL')
        self.assertEqual(pinned['record']['revision'], 1)
        self.assertFalse(denied['ok'])
        self.assertEqual(fresh['output'], 'SELECTED_LATER')
        self.assertTrue({'terminal_history', 'terminal_read'} <= {s['name'] for s in observed[0]['tools']})

    def test_off_before_native_dispatch_excludes_payload_and_revokes_read(self):
        agent, provider = self.native([('terminal_read', {'command_id': 'one'})])
        context = self.live_and_queued(agent)
        agent.terminal_context.update(snapshot(mode='off'))
        agent.ask('What did my command print?', context=context)
        payload = json.dumps(provider.messages)
        self.assertNotIn('SELECTED_ORIGINAL', payload)
        self.assertNotIn('SELECTED_LATER', payload)
        self.assertNotIn('OTHER_PANE_PRIVATE', payload)
        self.assertEqual(self.results()[0]['error'], 'revoked')

    def test_off_before_guest_dispatch_excludes_payload_and_revokes_read(self):
        agent, harness, observed = self.guest([('terminal_read', {'command_id': 'one'})])
        context = self.live_and_queued(agent)
        agent.terminal_context.update(snapshot(mode='off'))
        agent.ask('What did my command print?', context=context)
        self.assertEqual(len(observed), 1)
        self.assertNotIn('SELECTED_ORIGINAL', observed[0]['prompt'])
        self.assertNotIn('SELECTED_LATER', observed[0]['prompt'])
        self.assertEqual(observed[0]['results'][0]['error'], 'revoked')

    def test_next_native_turn_without_snapshot_clears_grants(self):
        agent, provider = self.native([('terminal_read', {'command_id': 'one'})])
        agent.ask('Read it', context=self.live_and_queued(agent))
        self.assertTrue(self.results()[0]['ok'])
        provider.calls = 0
        self.events.clear()
        agent.ask('Another question')
        self.assertEqual(self.results()[0]['error'], 'revoked')

    def test_next_guest_turn_without_snapshot_clears_grants(self):
        agent, harness, observed = self.guest([('terminal_read', {'command_id': 'one'})])
        agent.ask('Read it', context=self.live_and_queued(agent))
        agent.ask('Another question')
        self.assertEqual(len(observed), 2)
        self.assertTrue(observed[0]['results'][0]['ok'])
        self.assertEqual(observed[1]['results'][0]['error'], 'revoked')

    def test_unaccepted_steer_formats_without_changing_current_grants(self):
        agent, _ = self.native()
        current, incoming = record(output='ACTIVE_OUTPUT'), record('steer', output='STEER_OUTPUT')
        agent.terminal_context.update(snapshot(current, incoming))
        agent.terminal_context.set_snapshot(snapshot(current))
        entry = {'prompt': 'Read the attached command',
                 'context': {'terminal_context': snapshot(incoming)}}
        ctx = {'opening': [], 'requests': [], 'turn_id': 'active'}
        preview = agent._steer_message([entry], ctx, {'turn': 1}, record=False)
        self.assertIn('STEER_OUTPUT', preview['content'])
        self.assertEqual(agent.terminal_context.execute('terminal_read', {'command_id': 'one'})['output'], 'ACTIVE_OUTPUT')
        self.assertFalse(agent.terminal_context.execute('terminal_read', {'command_id': 'steer'})['ok'])
        agent._steer_message([entry], ctx, {'turn': 1}, record=True)
        self.assertEqual(agent.terminal_context.execute('terminal_read', {'command_id': 'steer'})['output'], 'STEER_OUTPUT')
        self.assertFalse(agent.terminal_context.execute('terminal_read', {'command_id': 'one'})['ok'])

    def test_guest_repeated_request_key_cannot_replay_revoked_terminal_evidence(self):
        agent, harness, _ = self.guest([])
        bridge = agent.provider.board_bridge
        cap = {'socket': bridge.path, 'token': bridge.token}
        for tool_name, arguments in (('terminal_read', {'command_id': 'one'}), ('terminal_history', {})):
            for revoke in ('off', 'end'):
                with self.subTest(tool=tool_name, revoke=revoke):
                    agent.terminal_context.update(snapshot(record(output='PRIVATE_OUTPUT', command='PRIVATE_COMMAND')))
                    agent.terminal_context.set_snapshot(snapshot(record(output='PRIVATE_OUTPUT', command='PRIVATE_COMMAND')))
                    bridge.begin(threading.Event())
                    key = f'{tool_name}-{revoke}'
                    params = {'name': tool_name, 'arguments': arguments}
                    first = exchange(cap, 'tools/call', params, key)
                    self.assertTrue(first['ok'])
                    if revoke == 'off':
                        agent.terminal_context.update(snapshot(mode='off'))
                    else:
                        bridge.end()
                    repeated = exchange(cap, 'tools/call', params, key)
                    self.assertNotIn('PRIVATE_OUTPUT', json.dumps(repeated))
                    self.assertNotIn('PRIVATE_COMMAND', json.dumps(repeated))
                    self.assertEqual(repeated.get('error') if revoke == 'off' else repeated.get('code'),
                                     'revoked' if revoke == 'off' else 'unavailable')
                    bridge.end()

    def test_repeat_attachment_deduplicates_excerpt_but_preserves_reads_and_reattaches(self):
        agent, provider = self.native([('terminal_read', {'command_id': 'one'})])
        context = {'terminal_context': snapshot(record(output='UNIQUE_REPEAT_EVIDENCE', revision=7))}
        agent.terminal_context.update(context['terminal_context'])
        agent.ask('Explain the command', context=context)
        first_user = [m for m in provider.messages if m['role'] == 'user'][-1]['content']
        self.assertIn('UNIQUE_REPEAT_EVIDENCE', first_user)
        self.assertEqual(self.results()[-1]['output'], 'UNIQUE_REPEAT_EVIDENCE')

        provider.calls = 0
        self.events.clear()
        agent.ask('Explain it again', context=context)
        second_user = [m for m in provider.messages if m['role'] == 'user'][-1]['content']
        self.assertNotIn('UNIQUE_REPEAT_EVIDENCE', second_user)
        self.assertIn('Terminal attachment unchanged', second_user)
        self.assertIn('one', second_user)
        self.assertIn('7', second_user)
        self.assertEqual(self.results()[-1]['output'], 'UNIQUE_REPEAT_EVIDENCE')
        self.assertEqual(self.results()[-1]['record']['revision'], 7)

        # Model compaction can remove the earlier user message containing evidence.
        agent.messages[:] = [m for m in agent.messages if m['role'] == 'system']
        provider.calls = 0
        self.events.clear()
        agent.ask('Explain after compaction', context=context)
        third_user = [m for m in provider.messages if m['role'] == 'user'][-1]['content']
        self.assertIn('UNIQUE_REPEAT_EVIDENCE', third_user)
        self.assertNotIn('Terminal attachment unchanged', third_user)
        self.assertEqual(self.results()[-1]['output'], 'UNIQUE_REPEAT_EVIDENCE')

    def test_remote_branch_preserves_context_and_short_profile_offers_tools(self):
        agent, provider = self.native(profile='short')
        context = self.live_and_queued(agent)
        context.update(foreground_program='ssh', remote_session={
            'host': 'example.test', 'user': 'alice', 'program': 'ssh', 'cwd': '/work'})
        agent.ask('What printed remotely?', context=context)
        payload = json.dumps(provider.messages)
        self.assertIn('example.test', payload)
        self.assertIn('SELECTED_ORIGINAL', payload)
        self.assertNotIn('SELECTED_LATER', payload)
        self.assertEqual(agent.profile(), 'short')
        self.assertTrue({'terminal_history', 'terminal_read'} <=
                        {spec['function']['name'] for spec in agent.tools()})

    @staticmethod
    def eligible(*records):
        """#XCXD: what the pane marks at submission — refresh-eligible and scoped."""
        return {'mode': 'automatic', 'source': 'automatic',
                'scope': {'pane_id': 'pane-a', 'generation': 'login-a'}, 'records': list(records)}

    def test_queued_native_turn_resolves_completed_result_at_actual_start(self):
        agent, provider = self.native([('terminal_read', {'command_id': 'one'})])
        queued = self.eligible(record(output='PARTIAL', state='running', ended_at=None,
                                       exit_status=None, revision=0))
        agent.terminal_context.update(snapshot(record(
            output='QUEUED_THEN_DONE', revision=1, exit_status=0, ended_at=2000)))
        # The prompt queued while the command ran; the command finished long before the
        # turn actually started. The turn reports result and exit status, not the stale
        # running excerpt it queued with, and never waits for a command.
        agent.ask('What did my command print?', context={'terminal_context': queued})
        payload = json.dumps([m for m in provider.messages if m['role'] == 'user'])
        self.assertIn('QUEUED_THEN_DONE', payload)
        self.assertNotIn('PARTIAL', payload)
        self.assertIn('exit_status', payload)
        self.assertEqual(self.results()[0]['output'], 'QUEUED_THEN_DONE')

    def test_queued_native_turn_marks_still_running_incomplete(self):
        agent, provider = self.native()
        queued = self.eligible()
        agent.terminal_context.update(snapshot(record(
            output='STILL_RUNNING_OUTPUT', state='running', ended_at=None, exit_status=None)))
        agent.ask('What is happening?', context={'terminal_context': queued})
        payload = json.dumps([m for m in provider.messages if m['role'] == 'user'])
        self.assertIn('STILL_RUNNING_OUTPUT', payload)
        self.assertIn('running/incomplete', payload)

    def test_queued_guest_turn_resolves_at_actual_start_without_shell(self):
        agent, harness, observed = self.guest([('terminal_read', {'command_id': 'one', 'fresh': True})])
        queued = self.eligible(record(output='GUEST_QUEUED_PARTIAL', state='running',
                                       ended_at=None, exit_status=None, revision=0))
        agent.terminal_context.update(snapshot(record(
            output='GUEST_QUEUED_DONE', revision=1, exit_status=0), mode='automatic'))
        with mock.patch.object(agent.executor, 'execute', side_effect=AssertionError('must not execute shell')) as execute:
            agent.ask('What did my command print?', context={'terminal_context': queued})
        execute.assert_not_called()
        self.assertEqual(len(observed), 1)
        self.assertIn('GUEST_QUEUED_DONE', observed[0]['prompt'])
        self.assertNotIn('GUEST_QUEUED_PARTIAL', observed[0]['prompt'])
        self.assertEqual(observed[0]['results'][0]['output'], 'GUEST_QUEUED_DONE')

    def test_pinned_queued_snapshot_stays_verbatim_at_actual_start(self):
        agent, provider = self.native([('terminal_read', {'command_id': 'one'})])
        pinned = {'mode': 'automatic', 'source': 'pinned',
                  'records': [record(output='PINNED_REVISION', revision=3)]}
        agent.terminal_context.update(snapshot(record(output='PINNED_REVISION_LATER', revision=4)))
        agent.ask('Explain the attached output', context={'terminal_context': pinned})
        payload = json.dumps([m for m in provider.messages if m['role'] == 'user'])
        self.assertIn('PINNED_REVISION', payload)
        self.assertNotIn('PINNED_REVISION_LATER', payload)
        self.assertEqual(self.results()[0]['record']['revision'], 3)

    def test_sharing_disabled_before_queued_dispatch_never_expands(self):
        agent, provider = self.native()
        queued = self.eligible(record(output='QUEUED_BEFORE_OFF'))
        agent.terminal_context.update(snapshot(record(output='FINISHED_WHILE_OFF', revision=1)))
        agent.terminal_context.update(snapshot(mode='off'))
        agent.ask('What did my command print?', context={'terminal_context': queued})
        payload = json.dumps([m for m in provider.messages if m['role'] == 'user'])
        self.assertNotIn('FINISHED_WHILE_OFF', payload)
        self.assertNotIn('QUEUED_BEFORE_OFF', payload)


if __name__ == '__main__':
    unittest.main()
