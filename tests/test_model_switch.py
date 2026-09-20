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
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock

from relay_core.agent import Agent
from relay_core.guest_harness_provider import HarnessProvider
from relay_core.provider import Cancelled, ProviderConfig
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
        # The conversation went across whole: the new model saw the tool result of the old one's
        # call, with the takeover note (#B9V4) after it saying the ask is still open.
        sent = agent.provider.requests[1][0]
        self.assertEqual(sent[-2]['role'], 'tool')
        self.assertEqual((sent[-1]['role'], sent[-1].get('relay_kind')), ('user', 'note'))
        self.assertIn('the model was switched mid-task (old → new)', sent[-1]['content'])
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
        self.assertEqual(applied['turn_id'], self.rec.of('agent_finished')[0]['id'])   # the turn it waited for
        # After the turn's own end, never inside it: done stays the turn's last event.
        self.assertLess(self.index(lambda e: e['event'] == 'agent_finished'),
                        self.index(lambda e: e['event'] == 'model_applied'))
        self.assertEqual((agent.config.model, agent.context.window), ('new', 200000))
        self.sup.submit('next', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] != self.rec.of('agent_finished')[0]['id'])
        self.assertEqual(len(self.rec.of('model_applied')), 1)

    def seed_long_history(self, agent, turns=3, chars=40000):
        """Older turns a 16k window cannot hold, then a short one; compaction can summarise them."""
        for n in range(turns):
            agent.messages += [{'role': 'user', 'content': f'old question {n}', 'relay_kind': 'prompt'},
                               {'role': 'assistant', 'content': f'old answer {n} ' + 'x' * chars}]
        agent.messages += [{'role': 'user', 'content': 'recent question', 'relay_kind': 'prompt'},
                           {'role': 'assistant', 'content': 'recent answer'}]

    def summary_models(self, agent):
        """The model in force at each summary call: proves which model summarised."""
        seen, side = [], agent.provider.complete

        def complete(messages, tools, emit, cancel):
            if not tools:
                seen.append(agent.config.model)
            return side(messages, tools, emit, cancel)
        agent.provider.complete = complete
        return seen

    def test_a_smaller_window_compacts_first_with_the_old_model_summarising(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))

        def second(messages):
            seen.append(ref[0].config.model)
            return text('ok')
        agent = self.make_agent(ScriptedProvider([step, second]))
        ref.append(agent)
        self.seed_long_history(agent)
        summarised_by = self.summary_models(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'small',
                                       'context_window': 16000})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        # Said up front, so the compaction is no surprise.
        self.assertEqual((changed['applies'], changed.get('will_compact')), ('next_step', True))
        # The context bar measures against the window that serves the next request (gap 1).
        bar = self.rec.of('context')[-1]
        self.assertEqual((bar['next']['model'], bar['next']['window'], bar['next']['in_flight_model']),
                         ('small', 16000, 'old'))
        self.assertTrue(bar['next']['will_compact'])
        self.assertGreater(bar['next']['percent'], 100)
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(seen, ['old', 'small'])
        self.assertEqual(summarised_by, ['old'])                 # the larger window summarised
        started = self.rec.of('compaction_started')[0]
        self.assertEqual((started['reason'], started['for_model']), ('model_switch', 'small'))
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertTrue(applied['compacted'])
        # Compaction, then the switch, then the request: nothing was sent over the new window.
        self.assertLess(self.index(lambda e: e['event'] == 'compacted'),
                        self.index(lambda e: e['event'] == 'model_applied'))
        used, _ = agent.context.used(agent.provider.requests[1][0], None)
        self.assertLess(used, 16000)
        self.assertEqual(len(self.rec.of('compaction_started')), 1)   # it fitted: no second pass
        self.assertNotIn('next', self.rec.of('context')[-1])

    def test_a_conversation_that_cannot_fit_even_compacted_is_refused_at_the_step(self):
        seen, ref = [], []
        # One long current turn: nothing older to summarise, and the latest answer stays whole.
        long_reply = {**tools_msg(call('list_directory', {'path': '.'})), 'content': 'notes ' * 4000}
        step, gate, entered = self.blocked_first_step(ref, seen, long_reply)

        def second(messages):
            seen.append(ref[0].config.model)
            return text('finished on the old model')
        agent = self.make_agent(ScriptedProvider([step, second]))
        ref.append(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'tiny', 'context_window': 8000})
        # The long answer is still in flight: nothing says it will not fit yet.
        self.assertIsNone(self.rec.wait(lambda e: e['event'] == 'model_changed').get('will_compact'))
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        refused = self.rec.wait(lambda e: e['event'] == 'model_switch_refused')
        self.assertEqual((refused['at'], refused['model'], refused['current_model']), ('step', 'tiny', 'old'))
        self.assertIn('Staying on old', refused['reason'])
        self.assertEqual(self.rec.of('model_applied'), [])
        self.assertEqual(seen, ['old', 'old'])                   # the turn carried on, not failed
        self.assertEqual((agent.config.model, agent.context.window), ('old', 128000))
        self.assertEqual(self.rec.of('agent_finished')[-1]['outcome'], 'done')
        # The bar goes back to the model in force.
        after = self.index(lambda e: e['event'] == 'model_switch_refused')
        self.assertNotIn('next', next(e for e in self.rec.events[after:] if e['event'] == 'context'))

    def test_a_window_smaller_than_the_system_prompt_and_tools_is_refused_at_once(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))
        agent = self.make_agent(ScriptedProvider([step, text('ok')]))
        ref.append(agent)
        # A long system prompt (a big relay.md, many skills): more than a 4096-token window holds.
        agent.messages[0]['content'] += ' standing instructions' * 800
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'micro',
                                       'context_window': 4096, 'id': 'm9'})
        refused = self.rec.wait(lambda e: e['event'] == 'model_switch_refused')
        self.assertEqual((refused['id'], refused['at'], refused['model'], refused['current_model']),
                         ('m9', 'request', 'micro', 'old'))
        self.assertEqual(self.rec.of('model_changed'), [])
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual((agent.config.model, self.rec.of('model_applied')), ('old', []))
        # Idle, the same.
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'micro', 'context_window': 4096})
        self.assertEqual(len(self.rec.of('model_switch_refused')), 2)
        self.assertEqual(agent.config.model, 'old')

    def test_an_idle_switch_to_a_smaller_window_compacts_first_off_the_protocol_thread(self):
        agent = self.make_agent(ScriptedProvider())
        self.seed_long_history(agent)
        summarised_by = self.summary_models(agent)
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'small', 'context_window': 16000})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['applies'], changed['in_flight_model'], changed['will_compact']),
                         ('after_compaction', 'old', True))
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['at'], applied['model'], applied['compacted']), ('now', 'small', True))
        # Idle, the last two turns kept whole are too much for 16k: a second pass keeps only the
        # last one. Both summaries by the old model.
        self.assertEqual(summarised_by, ['old', 'old'])
        self.assertEqual(len(self.rec.of('compacted')), 2)
        self.assertLess(self.index(lambda e: e['event'] == 'model_changed'),
                        self.index(lambda e: e['event'] == 'compaction_started'))
        for _ in range(100):         # the exclusive task hands the supervisor back
            if not self.sup.busy:
                break
            threading.Event().wait(0.02)
        self.assertFalse(self.sup.busy)
        self.assertEqual((agent.config.model, agent.context.window, self.hooked), ('small', 16000, ['small']))

    def test_a_switch_landing_at_turn_end_compacts_before_the_next_turn(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, text('answered without tools'))

        def later(messages):
            seen.append(ref[0].config.model)
            return text('next turn')
        agent = self.make_agent(ScriptedProvider([step, later]))
        ref.append(agent)
        self.seed_long_history(agent)
        self.sup.submit('quick question', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'small', 'context_window': 16000})
        self.sup.submit('and then', 'queue')
        gate.set()
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['at'], applied['compacted']), ('turn_end', True))
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and len(self.rec.of('agent_finished')) == 2)
        self.assertEqual(seen, ['old', 'small'])                  # the queued turn waited for it
        self.assertLess(self.index(lambda e: e['event'] == 'model_applied'),
                        max(i for i, e in enumerate(self.rec.events) if e['event'] == 'agent_started'))

    def test_a_stop_during_the_switch_compaction_keeps_the_old_model(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))
        agent = self.make_agent(ScriptedProvider([step, text('never')]))
        ref.append(agent)
        self.seed_long_history(agent)
        side = agent.provider.complete

        def complete(messages, tools, emit, cancel):
            if not tools:
                agent.stop()
                raise Cancelled()
            return side(messages, tools, emit, cancel)
        agent.provider.complete = complete
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'small', 'context_window': 16000})
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        refused = self.rec.wait(lambda e: e['event'] == 'model_switch_refused')
        self.assertIn('was stopped', refused['reason'])
        self.assertEqual(self.rec.of('agent_finished')[-1]['outcome'], 'cancelled')
        self.assertEqual((agent.config.model, self.rec.of('model_applied')), ('old', []))

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

    # ----- a takeover must not read as a fresh start (card #B9V4) -------------------------
    def test_the_model_taking_over_is_told_so_and_a_wrap_up_draws_the_completion_check(self):
        # The report: claude was switched to glm mid-turn, the pane stopped relaying and the user
        # had to type "continue". The conversation GLM inherited ended in claude's tool results,
        # nothing said the ask was unfinished, and a first reply in plain text ended the turn.
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))

        def wrap_up(_messages):
            seen.append(ref[0].config.model)
            return text('summarised what the previous model left')

        def working(_messages):
            seen.append(ref[0].config.model)
            return text('still open, doing it')

        def finished(_messages):
            seen.append(ref[0].config.model)
            return text('finished it now')

        agent = self.make_agent(ScriptedProvider([step, wrap_up, working, finished]))
        ref.append(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new'})
        self.rec.wait(lambda e: e['event'] == 'model_changed')
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(seen, ['old', 'new', 'new', 'new'])   # the turn carried on, not ended
        # The takeover note rides the new model's first request, after the old one's tool result.
        takeover_request = agent.provider.requests[1][0]
        self.assertEqual(takeover_request[-2]['role'], 'tool')
        note = takeover_request[-1]
        self.assertEqual((note['role'], note.get('relay_kind')), ('user', 'note'))
        self.assertIn('the model was switched mid-task (old → new)', note['content'])
        self.assertIn('still open', note['content'])
        # A wrap-up in plain text does not end the turn: the completion check names the request.
        checks = self.rec.of('completion_check')
        self.assertEqual([c['reminder'] for c in checks], [1, 2])
        self.assertTrue(all(c['open'] for c in checks))
        self.assertEqual(checks[0]['open'][0]['id'], 'R1')
        self.assertIn('completion check 1/2', agent.provider.requests[2][0][-1]['content'])
        self.assertLess(self.index(lambda e: e['event'] == 'completion_check'),
                        self.index(lambda e: e['event'] == 'done'))
        self.assertEqual(self.rec.of('agent_finished')[-1]['outcome'], 'done')

    def test_an_idle_switch_and_a_turn_end_landing_add_no_takeover_note(self):
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, text('answered without tools'))
        agent = self.make_agent(ScriptedProvider([step, text('next turn')]))
        ref.append(agent)
        # Mid-turn ask, but the turn answers in plain text and ends before the landing: the switch
        # lands at turn_end — a pane between turns, not a model taking over an unfinished ask.
        self.sup.submit('quick question', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new'})
        gate.set()
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual(applied['at'], 'turn_end')
        self.assertEqual(self.rec.of('completion_check'), [])
        self.sup.submit('next', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and len(self.rec.of('agent_finished')) == 2)
        # Idle, the same: applied at once, nothing added to any later turn.
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:3/v1', 'model': 'later'})
        self.rec.wait(lambda e: e['event'] == 'model_changed' and e['model'] == 'later')
        self.sup.submit('one more', 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and len(self.rec.of('agent_finished')) == 3)
        self.assertNotIn('switched mid-task', json.dumps(agent.messages))
        self.assertEqual(len(self.rec.of('model_applied')), 1)


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
        # The wrap-up in plain text draws the completion check twice (#B9V4) before the turn ends.
        b = Server(self, [{'role': 'assistant', 'content': 'done on B'},
                          {'role': 'assistant', 'content': 'still working'},
                          {'role': 'assistant', 'content': 'done on B'}])
        agent = self.make_agent(config=ProviderConfig(a.url, 'model-a', 'key-a', {'reasoning': {'effort': 'high'}}))
        self.sup.submit('look around', 'now')
        self.assertTrue(a.entered.wait(5))
        self.cmds.handle('set_model', {'preset': 'kimi', 'base_url': b.url, 'model': 'model-b', 'api_key': 'key-b'})
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(self.rec.of('agent_finished')[-1]['outcome'], 'done')
        self.assertEqual([body['model'] for body in a.bodies], ['model-a'])
        self.assertEqual([body['model'] for body in b.bodies], ['model-b'] * 3)
        assistant = next(m for m in b.bodies[0]['messages'] if m.get('tool_calls'))
        self.assertEqual(assistant['reasoning_content'], 'a-thoughts')
        # The tool result, then the takeover note (#B9V4) naming the switch and the open ask.
        self.assertEqual(b.bodies[0]['messages'][-2]['role'], 'tool')
        self.assertIn('the model was switched mid-task (model-a → model-b)',
                      b.bodies[0]['messages'][-1]['content'])
        applied = self.rec.of('model_applied')[0]
        self.assertTrue(applied['history_converted'])
        self.assertEqual((applied['from_model'], applied['model'], applied['preset']), ('model-a', 'model-b', 'kimi'))

    def test_a_mid_turn_switch_off_a_guest_ends_the_harness_at_the_landing(self):
        # Card #B9V4: `set_model` cannot replace an injected provider, so a deferred switch off a
        # guest harness adopted the new config while the guest kept serving the pane. The landing
        # now ends the harness first, as the idle path's apply_now always did.
        class FakeHarness:
            session_id = 'guest-session-1'
            closed = False

            def close(self):
                self.closed = True

        gate = threading.Event()
        seen, ref = [], []
        entered = threading.Event()

        def step(_messages):
            seen.append(ref[0].config.model)
            entered.set()
            gate.wait(5)
            return tools_msg(call('list_directory', {'path': '.'}))

        b = Server(self, [{'role': 'assistant', 'content': 'taken over, working'},
                          {'role': 'assistant', 'content': 'still working'},
                          {'role': 'assistant', 'content': 'done on the native model'}])
        harness = FakeHarness()
        guest = HarnessProvider(ProviderConfig('harness://claude', 'claude-guest', ''), harness, 'claude')
        scripted = ScriptedProvider([step])
        guest.complete = scripted.complete          # scripted turns; harness behaviour elsewhere
        agent = self.make_agent(provider=guest, config=ProviderConfig('harness://claude', 'claude-guest', ''))
        ref.append(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'preset': 'kimi', 'base_url': b.url, 'model': 'model-b',
                                       'api_key': 'key-b'})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['applies'], changed['in_flight_model']), ('next_step', 'claude-guest'))
        self.assertFalse(guest._closed)             # the request in flight finishes on the guest
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        # The harness ended at the landing, and the new provider — not the guest — served it.
        self.assertTrue((guest._closed, harness.closed))
        self.assertIsNot(agent.provider, guest)
        self.assertEqual([body['model'] for body in b.bodies], ['model-b'] * 3)
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['at'], applied['from_model'], applied['model'], applied['step']),
                         ('step', 'claude-guest', 'model-b', 2))
        self.assertEqual(len(self.rec.of('model_applied')), 1)
        self.assertEqual(self.hooked, ['model-b'])
        # The takeover note reached the native provider's first request, after the tool result.
        messages = b.bodies[0]['messages']
        self.assertEqual(messages[-2]['role'], 'tool')
        self.assertIn('the model was switched mid-task (claude-guest → model-b)', messages[-1]['content'])
        # A wrap-up in plain text did not end the turn: the completion check held it open.
        self.assertEqual([c['reminder'] for c in self.rec.of('completion_check')], [1, 2])
        self.assertEqual(self.rec.of('agent_finished')[-1]['outcome'], 'done')


if __name__ == '__main__':
    unittest.main()


class RefusingServer:
    """Two endpoints on one port (card #DC4J): ``/old/v1`` refuses every request with a 429 and a
    ``Retry-After: 30``; ``/new/v1`` answers at once; ``/stream/v1`` starts an answer and holds it
    until ``release`` is set. Records the bodies each one saw."""

    def __init__(self, test):
        self.bodies, self.entered, self.release = {}, threading.Event(), threading.Event()
        outer = self

        def sse(delta=None, finish=None):
            return ('data: ' + json.dumps({'choices': [{'delta': delta or {}, 'finish_reason': finish}]})
                    + '\n\n').encode()

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                mount = self.path.rsplit('/chat', 1)[0]
                outer.bodies.setdefault(mount, []).append(
                    json.loads(self.rfile.read(int(self.headers['Content-Length']))))
                outer.entered.set()
                if mount == '/old/v1':
                    self.send_response(429); self.send_header('Retry-After', '30')
                    self.send_header('Content-Length', '0'); self.end_headers()
                    return
                if mount == '/stream/v1':
                    self.send_response(200); self.send_header('Content-Type', 'text/event-stream')
                    self.end_headers()
                    self.wfile.write(sse({'content': 'partial on old'})); self.wfile.flush()
                    outer.release.wait(5)
                    self.wfile.write(sse(finish='stop') + b'data: [DONE]\n\n')
                    return
                body = json.dumps({'choices': [{'message': {'role': 'assistant', 'content': 'finished on new'},
                                                'finish_reason': 'stop'}],
                                   'usage': {'prompt_tokens': 10, 'completion_tokens': 2, 'total_tokens': 12}}).encode()
                self.send_response(200); self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)

            def log_message(self, *args):
                pass
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        test.addCleanup(self.server.server_close)
        test.addCleanup(self.server.shutdown)
        test.addCleanup(self.release.set)
        self.base = f'http://127.0.0.1:{self.server.server_port}'

    def seen(self, mount) -> int:
        return len(self.bodies.get(mount, []))


class RetryPreemptionTests(unittest.TestCase):
    """A model switch during the retries of a refused request takes effect at once (card #DC4J).

    Before this, a pane whose provider answered 429 sat out every Retry-After while the picker's
    choice waited for a step boundary that never came; the owner had to Esc the turn to switch.
    """
    setUp = ModelSwitchMidTurnTests.setUp
    make_agent = ModelSwitchMidTurnTests.make_agent
    index = ModelSwitchMidTurnTests.index

    def test_a_switch_during_a_retry_wait_re_issues_the_step_on_the_new_model_at_once(self):
        srv = RefusingServer(self)
        agent = self.make_agent(config=ProviderConfig(srv.base + '/old/v1', 'old', 'key-old'))
        self.sup.submit('hello', 'now')
        refused = self.rec.wait(lambda e: e['event'] == 'provider_retry' and e['reason'] == 'http')
        self.assertIn('429', refused['text'])
        self.assertIn('30 s', refused['text'])                 # the wait it would have sat out
        started = time.monotonic()
        self.cmds.handle('set_model', {'base_url': srv.base + '/new/v1', 'model': 'new',
                                       'api_key': 'key-new', 'id': 'm1'})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['id'], changed['model'], changed['applies'], changed['in_flight_model']),
                         ('m1', 'new', 'next_step', 'old'))
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertLess(time.monotonic() - started, 1.0)      # not after the 30 s Retry-After
        self.assertEqual((applied['model'], applied['from_model'], applied['at'], applied['step']),
                         ('new', 'old', 'step', 1))
        finished = self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(finished['outcome'], 'done')
        self.assertLess(time.monotonic() - started, 2.0)
        # The transcript line the GUI prints for it: what was refused, where the step went.
        switch = next(e for e in self.rec.of('provider_retry') if e['reason'] == 'switch')
        self.assertEqual((switch['status'], switch['attempt'], switch['step'], switch['from_model'], switch['to_model']),
                         (429, 1, 1, 'old', 'new'))
        self.assertIn('switching to new now', switch['text'])
        self.assertLess(self.index(lambda e: e['event'] == 'provider_retry' and e['reason'] == 'http'),
                        self.index(lambda e: e['event'] == 'provider_retry' and e['reason'] == 'switch'))
        self.assertLess(self.index(lambda e: e['event'] == 'provider_retry' and e['reason'] == 'switch'),
                        self.index(lambda e: e['event'] == 'model_applied'))
        self.assertLess(self.index(lambda e: e['event'] == 'model_applied'),
                        self.index(lambda e: e['event'] == 'done'))
        # One refused request on the old provider and never another; the same step went to the new
        # one (whose plain-text wrap-up then draws the completion check, as any takeover's does,
        # #B9V4) and the answer came from it. The pane's own model is now the new one, permanently.
        self.assertEqual(srv.seen('/old/v1'), 1)
        self.assertGreaterEqual(srv.seen('/new/v1'), 1)
        self.assertEqual({body['model'] for body in srv.bodies['/new/v1']}, {'new'})
        self.assertTrue(any(m['role'] == 'user' and 'hello' in str(m['content'])
                            for m in srv.bodies['/new/v1'][0]['messages']))
        self.assertIn('finished on new', ''.join(e['text'] for e in self.rec.of('delta')))
        self.assertEqual((agent.config.model, agent.config.base_url), ('new', srv.base + '/new/v1'))
        self.assertEqual(self.hooked, ['new'])
        self.assertEqual(len(self.rec.of('model_applied')), 1)
        self.assertFalse(agent.provider.response_open())

    def test_a_switch_while_the_reply_is_streaming_still_waits_and_says_so(self):
        # The existing rule for a request that has started answering: it finishes on the model it
        # started on, and the switch lands after it. The pane is told in the status line.
        srv = RefusingServer(self)
        agent = self.make_agent(config=ProviderConfig(srv.base + '/stream/v1', 'old', 'key-old'))
        self.sup.submit('hello', 'now')
        self.rec.wait(lambda e: e['event'] == 'delta' and 'partial on old' in e['text'])
        self.cmds.handle('set_model', {'base_url': srv.base + '/new/v1', 'model': 'new', 'api_key': 'key-new'})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual(changed['applies'], 'next_step')
        status = self.rec.wait(lambda e: e['event'] == 'status' and 'takes over' in e['text'])
        self.assertEqual(status['text'], 'new takes over from the next step · old is answering now')
        self.assertFalse(any(e['event'] == 'model_applied' for e in self.rec.events))
        srv.release.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['model'], applied['at']), ('new', 'turn_end'))
        self.assertFalse(any(e['event'] == 'provider_retry' for e in self.rec.events))
        self.assertEqual((srv.seen('/stream/v1'), srv.seen('/new/v1')), (1, 0))
        self.assertEqual(agent.config.model, 'new')
