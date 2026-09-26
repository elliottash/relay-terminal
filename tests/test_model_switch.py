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

    def test_deferred_guest_pick_starts_only_at_landing_and_serves_next_step(self):
        from relay_core import guest_harness_provider as ghp
        from guest_harness_fake import FakeHarness, ev
        ref, seen = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))
        agent = self.make_agent(ScriptedProvider([step]))
        agent.completion_check = False
        ref.append(agent)
        harness = FakeHarness([{'events': [ev('delta', text='guest answered')],
                                'result': ('guest answered', 'end', {})}], guest='codex', model='gpt-6-astra')
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        with mock.patch.object(ghp, 'make_harness', return_value=harness) as make:
            self.cmds.handle('set_model', {'preset': 'guest:codex', 'guest': {'model': 'gpt-6-astra'}})
            self.assertFalse(make.called)
            self.assertIsNone(ghp.agent_provider(agent))
            gate.set()
            self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(agent.config.model, 'gpt-6-astra')
        self.assertIs(ghp.agent_provider(agent).harness, harness)
        self.assertTrue(harness.sent)
        self.assertFalse(self.rec.of('error'))
        ghp.detach(agent)

    def test_deferred_guest_start_failure_keeps_current_provider(self):
        from relay_core import guest_harness_provider as ghp
        ref, seen = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))
        original = ScriptedProvider([step, text('old answered')])
        agent = self.make_agent(original)
        ref.append(agent)
        self.sup.submit('answer', 'now')
        self.assertTrue(entered.wait(5))
        with mock.patch.object(ghp, 'start_provider', side_effect=ValueError('fixture guest unavailable')):
            self.cmds.handle('set_model', {'preset': 'guest:codex', 'guest': {'model': 'gpt-6-astra'}})
            gate.set()
            refused = self.rec.wait(lambda e: e['event'] == 'model_switch_refused')
            self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertFalse(self.rec.of('error'))
        self.assertEqual(refused['code'], 'model_switch_failed')
        self.assertEqual(agent.config.model, 'old')
        self.assertIs(agent.provider, original)

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

    def test_every_model_a_switch_names_travels_with_its_name(self):
        """`model_name` on the events (card #MDL1, rule 1, protocol 13).

        The pane's status line and the phone's both read "model: kimi-k3 · conversation kept", so
        the name is computed once, by the worker, and sent beside the id the API takes. Claude
        Code's `opus` is "claude-opus-5-5"; "MiniMax-M3" is "minimax-m3"; "openai/gpt-6-sol" and
        "gpt-6-sol" are one name.
        """
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))
        agent = self.make_agent(ScriptedProvider([step, lambda _m: text('done')]),
                                config=ProviderConfig('http://127.0.0.1:1/v1', 'k3', ''))
        ref.append(agent)
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1',
                                       'model': 'openai/gpt-6-sol', 'id': 'm1'})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        # The id is what the API takes; the name is what a person reads. Both, side by side.
        self.assertEqual((changed['model'], changed['model_name']), ('openai/gpt-6-sol', 'gpt-6-sol'))
        # And the model still answering, so "· this turn finishes on kimi-k3" is a name too. `k3`
        # is the Kimi Coding Plan's id for Kimi K3 and cannot be derived, so it is a catalog name.
        self.assertEqual(changed['in_flight_model'], 'k3')
        self.assertEqual(changed['in_flight_model_name'], 'kimi-k3')
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['model'], applied['model_name']), ('openai/gpt-6-sol', 'gpt-6-sol'))
        self.assertEqual((applied['from_model'], applied['from_model_name']), ('k3', 'kimi-k3'))

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

    # ----- the queued `/model` entry and its ladder (card #7QH0) -----------------------------
    def two_step_turn(self):
        """A turn whose first step blocks until released and whose second records its model."""
        seen, ref = [], []
        step, gate, entered = self.blocked_first_step(ref, seen, tools_msg(call('list_directory', {'path': '.'})))

        def second(_messages):
            seen.append(ref[0].config.model)
            return text('done')
        agent = self.make_agent(ScriptedProvider([step, second, second]))
        ref.append(agent)
        return agent, seen, gate, entered

    def test_a_queued_switch_lets_the_turn_finish_whole_and_lands_at_its_end(self):
        agent, seen, gate, entered = self.two_step_turn()
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'queue'})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['applies'], changed['when'], changed['in_flight_model']), ('turn_end', 'queue', 'old'))
        self.assertNotIn('interrupting', changed)
        gate.set()
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual(seen, ['old', 'old'])             # the second step stayed on the old model
        self.assertEqual((applied['at'], applied['model']), ('turn_end', 'new'))
        self.assertLess(self.index(lambda e: e['event'] == 'agent_finished'),
                        self.index(lambda e: e['event'] == 'model_applied'))
        self.assertEqual(agent.config.model, 'new')

    def test_a_queued_switch_does_not_cut_a_retry_wait_short(self):
        agent, _seen, gate, entered = self.two_step_turn()
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'queue'})
        self.assertFalse(agent._switch_waiting())          # a held switch pre-empts no retry (#DC4J)
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'steer'})
        self.assertTrue(agent._switch_waiting())
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')

    def test_steering_a_queued_switch_lands_it_at_the_next_step(self):
        agent, seen, gate, entered = self.two_step_turn()
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'queue'})
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'steer'})
        self.assertEqual([(e['applies'], e['when']) for e in self.rec.of('model_changed')],
                         [('turn_end', 'queue'), ('next_step', 'steer')])
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(seen[:2], ['old', 'new'])          # (a takeover may add a completion check)
        self.assertEqual([(e['at'], e['model']) for e in self.rec.of('model_applied')], [('step', 'new')])

    def test_a_now_switch_stops_the_turn_lands_at_once_and_leaves_the_queue_running(self):
        agent, seen, gate, entered = self.two_step_turn()
        first = self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.sup.submit('then this', 'queue')
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'now'})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['applies'], changed['when'], changed['interrupting']), ('turn_end', 'now', True))
        self.assertTrue(agent.cancel_event.is_set())
        gate.set()
        finished = self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] == first)
        self.assertEqual(finished['outcome'], 'cancelled')
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['at'], applied['model']), ('turn_end', 'new'))
        # Not paused: the prompt queued behind the stopped turn runs, on the new model.
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and e['id'] != first)
        self.assertEqual(seen, ['old', 'new'])

    def test_a_second_queued_pick_replaces_the_first(self):
        agent, seen, gate, entered = self.two_step_turn()
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'first', 'when': 'queue'})
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:3/v1', 'model': 'second', 'when': 'queue'})
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual([e['model'] for e in self.rec.of('model_applied')], ['second'])
        self.assertEqual(seen, ['old', 'old'])

    def test_withdrawing_a_queued_switch_keeps_the_model(self):
        agent, seen, gate, entered = self.two_step_turn()
        self.sup.submit('look around', 'now')
        self.assertTrue(entered.wait(5))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'steer'})
        self.cmds.handle('model_withdraw', {'id': 'w1'})
        withdrawn = self.rec.wait(lambda e: e['event'] == 'model_switch_withdrawn')
        self.assertEqual((withdrawn['id'], withdrawn['withdrawn'], withdrawn['model'], withdrawn['current_model']),
                         ('w1', True, 'new', 'old'))
        gate.set()
        self.rec.wait(lambda e: e['event'] == 'agent_finished')
        self.assertEqual(seen, ['old', 'old'])
        self.assertEqual(self.rec.of('model_applied'), [])
        self.cmds.handle('model_withdraw', {})              # nothing left to take back
        self.assertFalse(self.rec.of('model_switch_withdrawn')[-1]['withdrawn'])

    def test_when_is_checked(self):
        self.make_agent(ScriptedProvider([]))
        with self.assertRaises(ValueError):
            self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'later'})

    def test_an_idle_switch_applies_at_once_whatever_when_says(self):
        agent = self.make_agent(ScriptedProvider([]))
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'new', 'when': 'queue'})
        changed = self.rec.of('model_changed')[-1]
        self.assertEqual(changed['applies'], 'now')
        self.assertNotIn('when', changed)
        self.assertEqual(agent.config.model, 'new')

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

    def test_a_switch_whose_overflow_sits_in_the_latest_turn_trims_that_turn(self):
        # Session 6a5b30a5 (2026-09-26): the latest turn was a long tool loop. Compaction keeps the
        # last turns whole, so "even compacted" the conversation still needed ~131,585 tokens and
        # the switch to a 128,000-token window was refused — the kept turn's old tool outputs were
        # never touched. Now they are elided like any older turn's, keeping the latest group intact.
        agent = self.make_agent(ScriptedProvider())
        agent.messages += [{'role': 'user', 'content': 'first request'},
                           {'role': 'assistant', 'content': 'first answer'},
                           {'role': 'user', 'content': 'build it'}]
        for n in range(12):
            agent.messages.append(tools_msg(call('run_command', {'command': f'./build {n}'}, call_id=f'c{n}')))
            agent.messages.append({'role': 'tool', 'tool_call_id': f'c{n}', 'content': 'x' * 20000})
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'small',
                                       'context_window': 32000})
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['at'], applied['model'], applied['compacted']), ('now', 'small', True))
        self.assertEqual(self.rec.of('model_switch_refused'), [])
        elided = [m for m in agent.messages
                  if m.get('role') == 'tool' and 'elided' in str(m.get('content'))]
        self.assertEqual(len(elided), 11)                        # every group but the latest
        self.assertEqual(agent.messages[-1].get('content'), 'x' * 20000)
        used, _ = agent.context.used(agent.messages, agent.tools())
        self.assertLess(used, 24000)                             # 32000 - room for a reply

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

    # ----- #SWCP (owner, 2026-09-23): a model change compacts above 128K tokens -------------
    def big_agent(self):
        """A conversation above SWITCH_COMPACT_TOKENS on a model whose own window still holds it."""
        agent = self.make_agent(ScriptedProvider())
        agent.context.window = 1_000_000
        self.seed_long_history(agent, turns=4, chars=160_000)
        from relay_core.agent import SWITCH_COMPACT_TOKENS
        self.assertGreater(agent.context.used(agent.messages, agent.tools())[0], SWITCH_COMPACT_TOKENS)
        return agent

    def wait_idle(self):
        for _ in range(100):
            if not self.sup.busy:
                break
            threading.Event().wait(0.02)

    def test_a_switch_above_128k_compacts_even_when_the_new_window_holds_it(self):
        from relay_core.agent import SWITCH_COMPACT_TOKENS
        agent = self.big_agent()
        summarised_by = self.summary_models(agent)
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'big',
                                       'context_window': 1_000_000})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['applies'], changed['will_compact']), ('after_compaction', True))
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['model'], applied['compacted']), ('big', True))
        self.assertEqual(summarised_by, ['old'])
        self.assertLess(self.rec.of('compacted')[0]['after_tokens'], SWITCH_COMPACT_TOKENS)
        self.wait_idle()
        self.assertEqual(agent.config.model, 'big')

    def test_a_switch_below_128k_that_fits_does_not_compact(self):
        agent = self.make_agent(ScriptedProvider())
        agent.context.window = 1_000_000
        self.seed_long_history(agent, turns=2, chars=40_000)
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'big',
                                       'context_window': 1_000_000})
        changed = self.rec.wait(lambda e: e['event'] == 'model_changed')
        self.assertEqual((changed['applies'], changed.get('will_compact')), ('now', None))
        self.assertEqual(self.rec.of('compaction_started'), [])
        self.assertEqual(agent.config.model, 'big')

    def test_a_128k_compaction_that_cannot_run_still_lands_the_switch_whole(self):
        # The model in force cannot summarise (a guest harness with no summaries role answers side
        # calls with nothing): the conversation fits the new window, so nothing is refused or lost.
        agent = self.big_agent()
        before = list(agent.messages)
        turn = agent.provider.complete

        def complete(messages, tools, emit, cancel):
            if not tools:
                return {'role': 'assistant', 'content': ''}
            return turn(messages, tools, emit, cancel)
        agent.provider.complete = complete
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'big',
                                       'context_window': 1_000_000})
        applied = self.rec.wait(lambda e: e['event'] == 'model_applied')
        self.assertEqual((applied['model'], applied.get('compacted', False)), ('big', False))
        self.assertEqual(self.rec.of('model_switch_refused'), [])
        self.assertTrue(any('takes over with the whole conversation' in e['text']
                            for e in self.rec.of('status')))
        self.assertEqual(agent.messages[1:], before[1:])

    def test_a_required_compaction_that_cannot_run_is_still_refused(self):
        agent = self.make_agent(ScriptedProvider())
        self.seed_long_history(agent)
        turn = agent.provider.complete

        def complete(messages, tools, emit, cancel):
            if not tools:
                return {'role': 'assistant', 'content': ''}
            return turn(messages, tools, emit, cancel)
        agent.provider.complete = complete
        self.cmds.handle('set_model', {'base_url': 'http://127.0.0.1:2/v1', 'model': 'small',
                                       'context_window': 16000})
        refused = self.rec.wait(lambda e: e['event'] == 'model_switch_refused')
        self.assertIn('compaction failed', refused['reason'])
        self.wait_idle()
        self.assertEqual(agent.config.model, 'old')

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
