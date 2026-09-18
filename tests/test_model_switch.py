"""Changing the model while the agent is working (issue 3ES1).

set_model is accepted mid-turn: the request in flight finishes on the old model, the next request of
the tool loop goes to the new one with the conversation so far, and a turn that ends first hands the
new model to the next turn. Scripted providers and local HTTP servers only.
"""
import json
import os
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock

from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands

sys.path.insert(0, str(Path(__file__).parent))
from test_sessions import ScriptedProvider, call, text, tools_msg  # noqa: E402
from test_queue import Recorder  # noqa: E402


class ModelSwitchMidTurnTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.ws = Path(self.temp.name) / 'ws'
        self.ws.mkdir()
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)
        self.hooked = []
        self.cmds = SessionCommands(self.sup, self.rec,
                                    on_model_changed=lambda agent: self.hooked.append(agent.config.model))
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(self.sup.shutdown)

    def make_agent(self, provider=None, config=None):
        agent = Agent(config or ProviderConfig('http://127.0.0.1:1/v1', 'old', ''), str(self.ws), self.sup.agent_emit,
                      provider=provider, session_dir=str(Path(self.temp.name) / 'sessions'))
        self.sup.set_agent(agent)
        return agent

    def index(self, pred):
        return next(i for i, e in enumerate(self.rec.events) if pred(e))

    def blocked_first_step(self, agent_ref, seen, first_reply):
        """A scripted response that records the model in force and waits until released."""
        gate, entered = threading.Event(), threading.Event()

        def step(_messages):
            seen.append(agent_ref[0].config.model)
            entered.set()
            gate.wait(5)
            return first_reply
        return step, gate, entered

    def test_next_step_runs_on_the_new_model_and_the_request_in_flight_finishes_on_the_old(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))

        def second(_messages):
            seen.append(ref[0].config.model)
            return text('finished on the new model')
        agent = self.make_agent(ScriptedProvider([step, second]))
        ref.append(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'id': 'm1'})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['id'], changed['model'], changed['applies'], changed['in_flight_model']),
                         ('m1', 'new', 'next_step', 'old'))
        self.assertEqual(agent.config.model, 'old')      # nothing swapped under the running request
        self.assertEqual(self.hooked, [])
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(seen, ['old', 'new'])
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['model'], applied['from_model'], applied['at'], applied['step']),
                         ('new', 'old', 'step', 2))
        self.assertEqual(self.hooked, ['new'])           # subagents and roles follow, at that moment
        self.assertLess(self.index(lambda e: e['event'] == 'model_changed'),
                        self.index(lambda e: e['event'] == 'model_applied'))
        self.assertLess(self.index(lambda e: e['event'] == 'model_applied'),
                        self.index(lambda e: e['event'] == 'done'))
        # The conversation went across whole: the new model saw the tool result of the old one's call.
        sent = agent.provider.requests[1][0]
        self.assertEqual(sent[-1]['role'], 'tool')
        self.assertEqual(len(self.rec.of('model_applied')), 1)

    def test_two_switches_before_the_next_request_last_one_wins(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))

        def second(_messages):
            seen.append(ref[0].config.model)
            return text('ok')
        agent = self.make_agent(ScriptedProvider([step, second]))
        ref.append(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'first'})
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:3/v1', 'model': 'second'})
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(seen, ['old', 'second'])
        self.assertEqual([e['model'] for e in self.rec.of('model_applied')], ['second'])
        self.assertEqual(agent.config.base_url, 'http://127.0.0.1:3/v1')

    def test_switching_back_before_the_next_request_cancels_the_switch(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))
        agent = self.make_agent(ScriptedProvider([step]))
        ref.append(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new'})
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:1/v1', 'model': 'old'})
        self.assertEqual(self.rec.of('model_changed')[-1]['applies'], 'now')
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(self.rec.of('model_applied'), [])
        self.assertEqual(agent.config.model, 'old')

    def test_a_turn_that_ends_first_hands_the_new_model_to_the_next_turn(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, text('answered without tools'))
        agent = self.make_agent(ScriptedProvider([step]))
        ref.append(agent)
        self.sup.submit('quick question', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'context_window': 200000})
        gate.set()
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['at'], applied['model'], applied['context_window']), ('turn_end', 'new', 200000))
        # After the turn's own end, never inside it: done stays the turn's last event.
        self.assertLess(self.index(lambda e: e['event'] == 'agent_finished'),
                        self.index(lambda e: e['event'] == 'model_applied'))
        self.assertEqual((agent.config.model, agent.context.window), ('new', 200000))
        self.sup.submit('next', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] != self.rec.of('agent_finished')[0]['id'])
        self.assertEqual(len(self.rec.of('model_applied')), 1)

    def test_a_smaller_window_is_checked_before_the_next_request(self):
        seen, ref = [], []
        # A long answer text: more than a 4096-token window holds, which the old model's did not mind.
        long_reply = {**tools_msg(call('list_directory', {'path': '.'})), 'content': 'notes ' * 4000}
        step, gate, entered = self.blocked_first_step(ref, seen, long_reply)
        agent = self.make_agent(ScriptedProvider([step, text('ok')]))
        ref.append(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'tiny', 'context_window': 4096})
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertTrue(applied.get('compacts'))
        self.assertLess(self.index(lambda e: e['event'] == 'model_applied'),
                        self.index(lambda e: e['event'] == 'compaction_started'))

    def test_a_missing_key_is_refused_at_once_even_mid_turn(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, text('ok'))
        agent = self.make_agent(ScriptedProvider([step]))
        ref.append(agent)
        self.sup.submit('q', 'now')
        self.assertTrue(entered.wait(5))
        with mock.patch.dict(os.environ, {'RELAY_KEYRING': 'off'}), \
                mock.patch('relay_core.keystore.lookup', return_value=''):
            with self.assertRaises(ValueError) as caught:
                self.cmds.handle('set_model', {'preset': 'kimi', 'use_stored_key': True})
        self.assertIn('No stored key', str(caught.exception))
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(self.rec.of('model_applied'), [])
        self.assertEqual(agent.config.model, 'old')

    def test_a_role_switch_carries_its_own_follow_up(self):
        # The worker's set_agent_role (Main <-> Flash) defers the same way, with its own hook: a role
        # switch must not run set_model's (which rebases the roles and puts the pane back on Main).
        seen, ref, followed = [], [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))
        agent = self.make_agent(ScriptedProvider([step, text('ok')]))
        ref.append(agent)
        agent.on_model_applied = lambda a: followed.append('set_model hook')
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        outcome = self.sup.now_or_later(lambda: None, lambda: agent.defer_model(
            ProviderConfig('http://127.0.0.1:2/v1', 'flash', ''), None,
            on_applied=lambda a: followed.append(('role', a.config.model)), fields={'agent_role': 'flash'}))
        self.assertEqual(outcome['applies'], 'next_step')
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(followed, [('role', 'flash')])
        self.assertEqual(self.rec.of('model_applied')[0]['agent_role'], 'flash')

    def test_idle_switch_still_applies_at_once(self):
        agent = self.make_agent(ScriptedProvider())
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new'})
        self.assertEqual(self.rec.wait(lambda e: e['event'] == 'model_changed')['applies'], 'now')
        self.assertEqual((agent.config.model, self.hooked), ('new', ['new']))
        self.assertEqual(self.rec.of('model_applied'), [])


class Server:
    """An OpenAI-compatible endpoint answering from a script; records every request body."""

    def __init__(self, test, replies, gate=None):
        self.bodies, self.replies, self.entered = [], list(replies), threading.Event()
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                outer.bodies.append(json.loads(self.rfile.read(int(self.headers['Content-Length']))))
                outer.entered.set()
                if gate is not None:
                    gate.wait(5)
                body = json.dumps({'choices': [{'message': outer.replies.pop(0), 'finish_reason': 'stop'}],
                                   'usage': {'prompt_tokens': 10, 'completion_tokens': 2, 'total_tokens': 12}}).encode()
                self.send_response(200); self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)

            def log_message(self, *args):
                pass
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        test.addCleanup(self.server.server_close)
        test.addCleanup(self.server.shutdown)
        self.url = f'http://127.0.0.1:{self.server.server_port}/v1'


class CrossProviderTests(unittest.TestCase):
    setUp = ModelSwitchMidTurnTests.setUp
    make_agent = ModelSwitchMidTurnTests.make_agent

    def test_the_next_request_goes_to_the_other_provider_with_its_history_converted(self):
        # Provider A answers in OpenRouter's shape (reasoning in "reasoning"); B is a Kimi-style
        # endpoint, which refuses an assistant tool call without "reasoning_content".
        gate = threading.Event()
        a = Server(self, [{'role': 'assistant', 'content': '', 'reasoning': 'a-thoughts',
                           'tool_calls': [call('list_directory', {'path': '.'}, 'call_a1')]}], gate)
        b = Server(self, [{'role': 'assistant', 'content': 'done on B'}])
        agent = self.make_agent(config=ProviderConfig(a.url, 'model-a', 'key-a', {'reasoning': {'effort': 'high'}}))
        self.sup.submit('look around', 'now')
        self.assertTrue(a.entered.wait(5))
        self.cmds.handle('set_model', {'preset': 'kimi', 'base_url': b.url, 'model': 'model-b', 'api_key': 'key-b'})
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(self.rec.of('agent_finished')[-1]['outcome'], 'done')
        self.assertEqual([body['model'] for body in a.bodies], ['model-a'])
        self.assertEqual([body['model'] for body in b.bodies], ['model-b'])
        assistant = next(m for m in b.bodies[0]['messages'] if m.get('tool_calls'))
        self.assertEqual(assistant['reasoning_content'], 'a-thoughts')
        self.assertEqual(b.bodies[0]['messages'][-1]['role'], 'tool')
        applied = self.rec.of('model_applied')[0]
        self.assertTrue(applied['history_converted'])
        self.assertEqual((applied['from_model'], applied['model'], applied['preset']), ('model-a', 'model-b', 'kimi'))


if __name__ == '__main__':
    unittest.main()
