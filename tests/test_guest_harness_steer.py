"""Mid-turn input with real adapter loops and fake transports; no guest subscription calls."""
import tempfile
import threading
import unittest
from unittest.mock import Mock

from relay_core.agent import Agent
from relay_core.queue import TurnSupervisor
from relay_core.guest_harness import HarnessError, HarnessSteerUncertain, HarnessEvent, TurnResult
from relay_core.guest_harness_claude import ClaudeHarness
from relay_core.guest_harness_codex import CodexHarness, _TurnState
from relay_core.guest_harness_provider import HarnessProvider, attach
from relay_core.provider import ProviderConfig
from tests.guest_harness_fake import FakeHarness


class SteeringTests(unittest.TestCase):
    def integration(self, failure=False, acknowledge=True, during_tool=False):
        events = []
        supervisor = TurnSupervisor(events.append)
        self.addCleanup(supervisor.shutdown)
        calls = []
        def native(prompt, *, accepted):
            calls.append(prompt)
            self.assertFalse(any(e['event'] == 'steer_delivered' for e in events))
            if failure:
                raise HarnessError('refused')
            if acknowledge:
                accepted()
                accepted()  # duplicate acknowledgement must not duplicate transcript/ledger
        def script(prompt, attachments, emit, cancel, harness):
            def submit():
                supervisor.submit('change direction', 'steer', 'steer-1',
                                  context={'terminal_cwd': '/tmp/example'})
            if not during_tool:
                submit()
            emit(HarnessEvent('delta', {'text': 'thinking'}))
            self.assertEqual(calls, [])
            emit(HarnessEvent('tool_started', {'call_id': 'one', 'tool': 'read_file'}))
            self.assertEqual(len(calls), 0 if during_tool else 1)
            if during_tool:
                submit()
            emit(HarnessEvent('tool_result', {'call_id': 'one', 'output': 'ok', 'ok': True}))
            self.assertEqual(len(calls), 1)
            return TurnResult('done')
        harness = FakeHarness([script])
        harness.steer = native
        config = ProviderConfig('harness://claude', 'fake', '', {}, 32768)
        with tempfile.TemporaryDirectory() as root:
            provider = HarnessProvider(config, harness, 'claude')
            agent = Agent(config, root, events.append, provider=provider,
                          track_requests=True, completion_check=False, todo_tool=False)
            attach(agent, provider)
            supervisor.set_agent(agent)
            # Drive ask on this thread while the dispatcher remains idle; submit still takes
            # the real running-turn queue path.
            supervisor._running = 'test-turn'
            agent.ask('original task')
            supervisor._running = None
            self.assertEqual(len(calls), 1)
            delivered = [e for e in events if e['event'] == 'steer_delivered']
            messages = [m for m in agent.messages if m.get('relay_kind') == 'steer']
            if failure or not acknowledge:
                self.assertEqual(delivered, [])
                self.assertEqual(messages, [])
                self.assertEqual([i['prompt'] for i in supervisor._steer], ['change direction'])
            else:
                self.assertEqual(len(delivered), 1)
                self.assertEqual(len(messages), 1)
                self.assertIn('change direction', messages[0]['content'])
                self.assertIn('/tmp/example', calls[0])
                self.assertEqual(supervisor._steer, [])

    def test_tool_boundary_delivery_and_duplicate_ack(self):
        self.integration()

    def test_input_arriving_during_tool_delivered_at_result(self):
        self.integration(during_tool=True)

    def test_rejected_input_remains_pending_without_retry(self):
        self.integration(failure=True)

    def test_unacknowledged_input_is_returned(self):
        self.integration(acknowledge=False)

    def test_codex_uses_turn_precondition_and_waits_for_reply(self):
        harness = CodexHarness()
        harness._turn = _TurnState(lambda e: None)
        harness._turn.turn_id = 'active'
        harness._session_id = 'thread'
        accepted = Mock()
        def request(method, params, **kwargs):
            accepted.assert_not_called()
            self.assertEqual(method, 'turn/steer')
            self.assertEqual(params['expectedTurnId'], 'active')
            self.assertEqual(params['threadId'], 'thread')
            return {'turnId': 'active'}
        harness._request = request
        harness.steer('new direction', accepted=accepted)
        accepted.assert_called_once()
        harness._request = Mock(side_effect=HarnessError('turn ended'))
        accepted.reset_mock()
        with self.assertRaises(HarnessError):
            harness.steer('too late', accepted=accepted)
        accepted.assert_not_called()

    def claude_loop(self, cross_result):
        harness = ClaudeHarness()
        harness._sending = True
        writes, acknowledgements, events = [], [], []
        def write(message):
            writes.append(message)
            if len(writes) == 1:
                harness._inbox.put({'type': 'assistant', 'message': {'content': [
                    {'type': 'tool_use', 'id': 'tool1', 'name': 'Read', 'input': {'file_path': '/tmp/x'}}]}})
            else:
                self.assertEqual(acknowledgements, [])
                if cross_result:
                    harness._inbox.put({'type': 'result', 'result': 'first answer'})
                harness._inbox.put({'type': 'user', 'uuid': message['uuid'], 'isReplay': True,
                                    'message': message['message']})
                harness._inbox.put({'type': 'user', 'uuid': message['uuid'], 'isReplay': True,
                                    'message': message['message']})
                harness._inbox.put({'type': 'result', 'result': 'steered answer'})
        harness._write = write
        def emit(event):
            events.append(event)
            if event.kind == 'tool_started':
                harness.steer('steer now', accepted=lambda: acknowledgements.append('accepted'))
        result = harness._run_turn('original', [], emit, threading.Event())
        self.assertEqual(result.text, 'first answer\n\nsteered answer' if cross_result else 'steered answer')
        self.assertEqual([e.data['text'] for e in events if e.kind == 'done'], [result.text])
        self.assertEqual(acknowledgements, ['accepted'])
        self.assertEqual(len([e for e in events if e.kind == 'done']), 1)
        self.assertEqual(writes[1]['priority'], 'next')
        self.assertTrue(harness._inbox.empty())

    def test_claude_ack_in_same_turn(self):
        self.claude_loop(False)

    def test_claude_retains_followup_across_result_boundary(self):
        self.claude_loop(True)

    def test_claude_failed_write_does_not_ack(self):
        harness = ClaudeHarness()
        harness._sending = True
        harness._write = Mock(side_effect=HarnessError('broken pipe'))
        accepted = Mock()
        with self.assertRaises(HarnessError):
            harness.steer('keep this', accepted=accepted)
        accepted.assert_not_called()
        self.assertEqual(harness._steer_pending, {})

    def test_codex_timeout_is_uncertain_and_never_acknowledged(self):
        harness = CodexHarness()
        harness._write = Mock()
        with self.assertRaises(HarnessSteerUncertain):
            harness._request('turn/steer', {}, timeout=0, steering=True)
        self.assertEqual(harness._pending, {})

    def test_late_duplicate_cannot_acknowledge_new_batch(self):
        from relay_core.guest_harness_provider import _Turn
        from types import SimpleNamespace
        batches = [[{'prompt': 'first'}], [{'prompt': 'second'}]]
        callbacks, transcript, settled = [], [], []
        agent = SimpleNamespace(_turn_ctx={'turn_id': 't'}, _turn={}, messages=transcript,
                                steer_reserve=lambda: batches.pop(0),
                                steer_settle=settled.append,
                                _steer_message=lambda items, *a, **kw: {'content': items[0]['prompt']})
        provider = SimpleNamespace(context_generation=0, harness=SimpleNamespace(
            steer=lambda prompt, accepted: callbacks.append(accepted)))
        turn = _Turn(provider, agent, None, lambda e: None, threading.Event())
        turn.deliver_steer()
        callbacks[0]()
        turn.deliver_steer()
        callbacks[0]()
        self.assertEqual(len(transcript), 1)
        callbacks[1]()
        self.assertEqual([m['content'] for m in transcript], ['first', 'second'])
        self.assertEqual(settled, [True, True])

    def test_claude_missing_echo_closes_transport_and_retains_input(self):
        from unittest.mock import patch
        harness = ClaudeHarness()
        harness._sending = True
        accepted = Mock()
        harness.close = Mock()
        def write(message):
            if message.get('uuid'):
                harness._inbox.put({'type': 'result', 'result': 'old turn'})
            else:
                harness._inbox.put({'type': 'assistant', 'message': {'content': [
                    {'type': 'tool_use', 'id': 'tool', 'name': 'Read', 'input': {}}]}})
        harness._write = write
        def emit(event):
            if event.kind == 'tool_started':
                harness.steer('pending', accepted=accepted)
        with patch('relay_core.guest_harness_claude.time.monotonic', side_effect=[0, 0, 31]):
            with self.assertRaisesRegex(HarnessError, 'did not acknowledge'):
                harness._run_turn('original', [], emit, threading.Event())
        accepted.assert_not_called()
        harness.close.assert_called_once()

    def test_uncertain_delivery_propagates_out_of_codex_event_callback(self):
        harness = CodexHarness()
        def emit(event):
            raise HarnessSteerUncertain('unknown')
        with self.assertRaises(HarnessSteerUncertain):
            harness._emit(_TurnState(emit), 'tool_started', {})

    def test_leased_input_stays_visible_and_cannot_be_withdrawn(self):
        from types import SimpleNamespace
        events = []
        supervisor = TurnSupervisor(events.append)
        self.addCleanup(supervisor.shutdown)
        agent = SimpleNamespace(track_requests=False, stop=Mock())
        supervisor.set_agent(agent)
        supervisor._running = 'active'
        try:
            ident = supervisor.submit('leased', 'steer', 'request')
            supervisor.reserve_steer()
            supervisor.clear()
            snapshot = [e for e in events if e['event'] == 'queue_changed'][-1]
            self.assertEqual([i['id'] for i in snapshot['steering']], [ident])
            self.assertFalse(supervisor.unsteer('request'))
            with self.assertRaises(ValueError):
                supervisor.remove(ident)
            supervisor.cancel()
            self.assertEqual(len(supervisor._steer_inflight), 1)
            agent.stop.assert_called_once()
            # End-of-turn safety net restores the lease even if the provider did not settle.
            with supervisor._lock:
                supervisor._return_steer_locked()
            self.assertEqual(supervisor._steer_inflight, [])
            self.assertEqual([e['id'] for e in events if e['event'] == 'steer_returned'], [ident])
            self.assertFalse(any(e['event'] == 'steer_delivered' for e in events))
        finally:
            supervisor._running = None

    def test_reset_clears_any_idle_lease(self):
        from types import SimpleNamespace
        supervisor = TurnSupervisor(lambda e: None)
        self.addCleanup(supervisor.shutdown)
        agent = SimpleNamespace(track_requests=False, reset_conversation=Mock())
        supervisor.set_agent(agent)
        supervisor._steer_inflight = [{'id': 'orphan'}]
        supervisor.reset()
        self.assertEqual(supervisor._steer_inflight, [])
