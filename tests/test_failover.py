# SPDX-License-Identifier: GPL-3.0-or-later
"""Failover (card #G9VE): a turn whose provider keeps failing continues on another one.

Everything here is offline: the pane's provider is a stub that fails the way the test wants,
the failover targets are stubs behind a patched ``agent._provider_for``, and the roles resolver
hands out fake keys, so no request ever leaves the process.
"""
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core.agent import Agent, validate_turn_options
from relay_core.presets import PRESETS
from relay_core.provider import ProviderConfig, ProviderError, ProviderStalled, ProviderTruncated
from relay_core.roles import RoleResolver

MAIN = PRESETS['glm']
CONFIG = ProviderConfig(MAIN.base_url, MAIN.model, 'pane-key')
FLASH = ProviderConfig(MAIN.base_url, 'glm-5.3-flash', 'pane-key')   # this provider's Flash model


class Refuser:
    """A provider that always fails the way it was told."""
    def __init__(self, error):
        self.error = error
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        raise self.error


class Answerer:
    def __init__(self):
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        return {'role': 'assistant', 'content': 'from the spare'}


def resolver(keys, config=CONFIG, preset_id='glm'):
    return RoleResolver(config, preset_id, key_lookup=lambda preset_id: keys.get(preset_id, ''))


class Streamer:
    """A provider that puts something on the user's screen and then fails."""
    def __init__(self, error, kind='delta'):
        self.error = error
        self.kind = kind
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        emit({'event': self.kind, 'text': 'half an answer'})
        raise self.error


class OpenRefuser(Refuser):
    """A provider that fails with its HTTP response still open, and records its own cancel in
    the event stream so the order against the failover note is visible."""
    def __init__(self, error, events):
        super().__init__(error)
        self.events = events
        self.cancels = 0

    def response_open(self):
        return True

    def cancel(self):
        self.cancels += 1
        self.events.append({'event': 'stub_cancelled'})


class Recorder:
    """Answers, and records what the agent looked like while it was the provider."""
    def __init__(self, agent_ref=None):
        self.agent_ref = agent_ref
        self.calls = 0
        self.seen = []
        self.window = None

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        self.seen = [dict(m) for m in messages]
        agent = self.agent_ref() if self.agent_ref else None
        if agent is not None:
            self.window = agent.context.window
        return {'role': 'assistant', 'content': 'from the spare'}


class CandidateTests(unittest.TestCase):
    def test_keyed_presets_then_relay_free_in_catalog_order(self):
        with mock.patch('relay_core.hosted.available', return_value=True):
            found = resolver({'kimi': 'k', 'openai': 'k'}).failover_candidates('main', {'glm'})
        self.assertEqual([r.preset_id for r in found], ['kimi', 'openai', 'relay-free'])
        self.assertEqual([r.config.model for r in found], ['kimi-k3', 'gpt-6-astra', 'relay-main'])
        self.assertTrue(all(r.config.api_key or r.config.hosted for r in found))
        self.assertTrue(found[2].config.hosted)                     # Relay Free, always last

    def test_a_flash_pane_fails_over_within_flash(self):
        with mock.patch('relay_core.hosted.available', return_value=False):
            found = resolver({'kimi': 'k'}).failover_candidates('flash', {'glm'})
        self.assertEqual([(r.preset_id, r.config.model) for r in found], [('kimi', 'kimi-k2.7-code-highspeed')])

    def test_two_keys_for_one_service_are_one_provider(self):
        # glm and glm-coding are two plans on api.z.ai: a host that is down is down for both, so
        # trying the second is a wasted move, not a failover.
        with mock.patch('relay_core.hosted.available', return_value=False):
            found = resolver({'glm-coding': 'k', 'kimi': 'k'}).failover_candidates('main', {'glm'})
        self.assertEqual([r.preset_id for r in found], ['kimi'])
        with mock.patch('relay_core.hosted.available', return_value=False):
            found = resolver({'glm': 'k', 'kimi': 'k'}).failover_candidates('main', {'glm-coding'})
        self.assertEqual([r.preset_id for r in found], ['kimi'])

    def test_a_hostname_the_caller_names_is_skipped_like_a_tried_preset(self):
        # The pane's own endpoint may match no preset at all, so its host cannot be expressed as a
        # preset id: `hosts` is how the agent says "this one is already down".
        with mock.patch('relay_core.hosted.available', return_value=False):
            found = resolver({'glm': 'k', 'glm-coding': 'k', 'kimi': 'k'}).failover_candidates(
                'main', set(), {'API.Z.AI'})
        self.assertEqual([r.preset_id for r in found], ['kimi'])
        with mock.patch('relay_core.hosted.available', return_value=False):
            found = resolver({'glm': 'k', 'kimi': 'k'}).failover_candidates('main', set(), ())
        self.assertEqual([r.preset_id for r in found], ['kimi', 'glm'])

    def test_no_key_no_candidate_and_the_tried_ones_are_not_returned(self):
        with mock.patch('relay_core.hosted.available', return_value=False):
            self.assertEqual(resolver({}).failover_candidates('main', {'glm'}), [])
            found = resolver({'kimi': 'k', 'openai': 'k'}).failover_candidates('main', {'glm', 'kimi'})
        self.assertEqual([r.preset_id for r in found], ['openai'])


class FailoverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.events = []
        self.stubs = {}
        patcher = mock.patch('relay_core.agent._provider_for',
                             side_effect=lambda config, stall: self.stubs[config.model])
        patcher.start()
        self.addCleanup(patcher.stop)

    def agent(self, *, provider=None, failover=True, roles=None, config=CONFIG, preset_id='glm'):
        return Agent(config, self.temp.name, self.events.append, provider=provider,
                     preset_id=preset_id, failover=failover, roles=roles)

    def retries(self):
        """The moves, not the closing "back to the pane's own model" note."""
        return [e for e in self.events if e['event'] == 'provider_retry'
                and e['reason'] != 'failover_ended']

    def test_a_failing_provider_hands_the_turn_to_a_keyed_preset(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429 for glm-5.3.'))
        spare = Answerer()
        self.stubs['kimi-k3'] = spare
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual([e['event'] for e in self.events[-1:]], ['done'])
        retry = next(e for e in self.retries() if e['reason'] == 'failover')
        self.assertEqual(retry['from_model'], MAIN.model)
        self.assertEqual(retry['to_model'], 'kimi-k3')
        self.assertIn('kimi-k3', retry['text'])
        self.assertEqual(spare.calls, 1)
        self.assertIn('from the spare', self.events[-1].get('text', '') or agent.messages[-1]['content'])
        # The swap was for that turn only: the pane's own model is back.
        self.assertEqual(agent.config.model, MAIN.model)
        self.assertIs(agent.provider, self.stubs[MAIN.model])

    def test_each_provider_is_asked_once_and_the_turn_fails_when_none_answers(self):
        error = ProviderError('Provider HTTP 503 for everyone.')
        stubs = {MAIN.model: Refuser(error), 'kimi-k3': Refuser(error), 'gpt-6-astra': Refuser(error)}
        self.stubs.update(stubs)
        agent = self.agent(roles=resolver({'kimi': 'k', 'openai': 'k'}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertIn('503', self.events[-1]['text'])
        self.assertEqual([e['attempt'] for e in self.retries()], [1, 2])
        self.assertEqual([stub.calls for stub in stubs.values()], [1, 1, 1])
        self.assertEqual(agent.config.model, MAIN.model)

    def test_the_option_off_keeps_the_pane_provider(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429 for glm-5.3.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(failover=False, roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.retries(), [])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)

    def test_a_stalled_provider_fails_over_but_a_truncated_step_does_not(self):
        self.stubs[MAIN.model] = Refuser(ProviderStalled(60.0))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual([e['event'] for e in self.events[-1:]], ['done'])
        self.assertEqual(self.stubs[MAIN.model].calls, 2)      # the stall retry, then the move
        self.assertTrue(any(e['reason'] == 'failover' for e in self.retries()))

        self.events.clear()
        self.stubs[MAIN.model] = Refuser(ProviderTruncated('length', 8192))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertFalse(any(e['reason'] == 'failover' for e in self.retries()))
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)

    def test_no_resolver_or_an_injected_provider_never_moves(self):
        # A subagent or a test agent has no resolver: it cannot know which providers are keyed.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 500.'))
        agent = self.agent(roles=None)
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.retries(), [])
        # An injected provider belongs to whoever built it (tests, side calls).
        self.events.clear()
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 500.'))
        agent = self.agent(provider=Refuser(ProviderError('Provider HTTP 500.')),
                           roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.retries(), [])

    def test_a_flash_pane_fails_over_to_a_flash_model(self):
        flash = 'glm-5.3-flash'
        self.stubs[flash] = Refuser(ProviderError('Provider HTTP 429.'))
        spare = Answerer()
        self.stubs['kimi-k2.7-code-highspeed'] = spare
        agent = self.agent(roles=resolver({'kimi': 'k'}), config=FLASH)
        agent.ask('hello')
        self.assertEqual([e['event'] for e in self.events[-1:]], ['done'])
        self.assertEqual(spare.calls, 1)
        moved = next(e for e in self.retries() if e['reason'] == 'failover')
        self.assertEqual(moved['to_model'], 'kimi-k2.7-code-highspeed')


    # ----- review findings, 2026-09-19 ------------------------------------------------
    def test_a_provider_that_streamed_an_answer_is_never_failed_over(self):
        # Half an answer is on the user's screen; a second provider would write a second one
        # under it. The same rule the stall retry follows (protocol 15.2).
        self.stubs[MAIN.model] = Streamer(ProviderError('Provider HTTP 500 mid-stream.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.retries(), [])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)
        # Reasoning alone is not an answer: that call still moves.
        self.events.clear()
        self.stubs[MAIN.model] = Streamer(ProviderError('Provider HTTP 500.'), kind='thinking_delta')
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(self.stubs['kimi-k3'].calls, 1)

    def test_a_step_that_produced_nothing_still_moves_after_one_that_did(self):
        # The flag is per call, not per turn: a first step that streamed an answer does not stop
        # a later step from failing over.
        answered = {'n': 0}

        class Half:
            calls = 0

            def complete(_self, messages, tools, emit, cancel):
                _self.calls += 1
                if _self.calls == 1:
                    emit({'event': 'delta', 'text': 'thinking about it'})
                    return {'role': 'assistant', 'content': '',
                            'tool_calls': [{'id': 'c1', 'type': 'function',
                                            'function': {'name': 'read_file',
                                                         'arguments': '{"path": "nope.txt"}'}}]}
                raise ProviderError('Provider HTTP 503 on the second step.')

        self.stubs[MAIN.model] = Half()
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        answered['n'] = self.stubs['kimi-k3'].calls
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(answered['n'], 1)

    def test_the_failed_provider_response_is_closed_before_the_swap(self):
        stub = OpenRefuser(ProviderError('Provider HTTP 429.'), self.events)
        self.stubs[MAIN.model] = stub
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertGreaterEqual(stub.cancels, 1)
        kinds = [e['event'] for e in self.events]
        moved = next(i for i, e in enumerate(self.events)
                     if e['event'] == 'provider_retry' and e['reason'] == 'failover')
        self.assertLess(kinds.index('stub_cancelled'), moved)

    def test_the_note_lands_after_the_thinking_block_it_interrupts(self):
        self.stubs[MAIN.model] = Streamer(ProviderError('Provider HTTP 500.'), kind='thinking_delta')
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        kinds = [e['event'] for e in self.events]
        moved = next(i for i, e in enumerate(self.events)
                     if e['event'] == 'provider_retry' and e['reason'] == 'failover')
        self.assertLess(kinds.index('thinking_done'), moved)

    def test_the_restore_is_announced_before_the_terminal_event(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        kinds = [e['event'] for e in self.events]
        back = next(i for i, e in enumerate(self.events)
                    if e['event'] == 'provider_retry' and e['reason'] == 'failover_ended')
        self.assertLess(back, kinds.index('turn_summary'))
        self.assertLess(kinds.index('turn_summary'), kinds.index('done'))
        note = self.events[back]
        # The preset is named as well as the model: two stored keys for one vendor serve the
        # same model id, and the user needs to know which one the turn came back to.
        self.assertEqual(note['text'], f'Back to {MAIN.model} ({PRESETS["glm"].label}).')
        self.assertEqual((note['from_model'], note['from_preset']), ('kimi-k3', 'kimi'))
        self.assertEqual((note['to_model'], note['to_preset']), (MAIN.model, 'glm'))

    def test_a_main_pane_whose_flash_is_its_main_model_stays_on_main(self):
        # OpenRouter's Flash row is the same DeepSeek model as its Main row, so "is this a Flash
        # pane?" cannot be answered by comparing models through provider_tier_model's fallback.
        preset = PRESETS['openrouter']
        config = ProviderConfig(preset.base_url, preset.model, 'pane-key')
        self.stubs[preset.model] = Refuser(ProviderError('Provider HTTP 429.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}, config, 'openrouter'),
                           config=config, preset_id='openrouter')
        self.assertEqual(agent._failover_tier(), 'main')
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        moved = next(e for e in self.retries() if e['reason'] == 'failover')
        self.assertEqual(moved['to_model'], 'kimi-k3')          # Main, not kimi's Flash model
        # A pane really on its provider's Flash model still fails over within Flash.
        self.assertEqual(self.agent(config=FLASH, provider=Answerer())._failover_tier(), 'flash')
        self.assertEqual(self.agent(provider=Answerer())._failover_tier(), 'main')

    def test_the_swap_adapts_the_history_and_the_context_window(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429.'))
        spare = Recorder()
        self.stubs['kimi-k3'] = spare
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        spare.agent_ref = lambda: agent
        agent.messages.append({'role': 'assistant', 'content': '',
                               'tool_calls': [{'id': 'c0', 'type': 'function',
                                               'function': {'name': 'read_file', 'arguments': '{}'}}]})
        agent.messages.append({'role': 'tool', 'tool_call_id': 'c0', 'content': 'ok'})
        agent.ask('hello')
        # Kimi refuses an assistant tool-call message with no reasoning_content: the history has to
        # be converted for the provider the turn moves to, exactly as set_model converts it.
        adapted = [m for m in spare.seen if m.get('tool_calls')]
        self.assertTrue(adapted and adapted[0].get('reasoning_content'))
        self.assertEqual(spare.window, PRESETS['kimi'].context_window)
        self.assertEqual(agent.context.window, PRESETS['glm'].context_window)   # and back again
        self.assertEqual(agent.config.model, MAIN.model)

    def test_a_model_switch_during_a_failed_over_turn_lands_after_the_restore(self):
        target = PRESETS['openai']
        other = ProviderConfig(target.base_url, target.model, 'k')
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429.'))
        self.stubs[target.model] = Answerer()
        seen = {}

        class Switcher:
            calls = 0

            def complete(_self, messages, tools, emit, cancel):
                _self.calls += 1
                # What the worker does for a set_model that arrives mid-turn, then what the turn
                # loop does at the next step boundary.
                seen['deferred'] = agent.defer_model(other, 'openai')
                seen['at_step'] = agent.apply_pending_model('t1', 1, 'step')
                seen['model_at_step'] = agent.config.model
                return {'role': 'assistant', 'content': 'from the spare'}

        self.stubs['kimi-k3'] = Switcher()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual(seen['deferred']['applies'], 'turn_end')
        self.assertIsNone(seen['at_step'])                      # not while the swap is in force
        self.assertEqual(seen['model_at_step'], 'kimi-k3')
        self.assertEqual(agent.config.model, MAIN.model)        # the restore ran, and kept nothing
        self.assertEqual(agent.apply_pending_model(at='turn_end')['model'], target.model)
        self.assertEqual(agent.config.model, target.model)

    def test_a_pane_on_its_own_base_url_never_gets_the_turn_handed_back_to_that_host(self):
        # No preset matches this endpoint, so `self.preset` is None and the tried-preset set is
        # empty: without the host the pane is on, the glm preset on the same host looks like a
        # fresh provider and the turn would be sent straight back to the service that is down.
        config = ProviderConfig('https://api.z.ai/v1', 'glm-experimental', 'pane-key')
        self.stubs['glm-experimental'] = Refuser(ProviderError('Provider HTTP 503.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'glm': 'k', 'glm-coding': 'k', 'kimi': 'k'},
                                          config, None),
                           config=config, preset_id=None)
        self.assertIsNone(agent.preset)
        with mock.patch('relay_core.hosted.available', return_value=False):
            agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual([e['to_model'] for e in self.retries() if e['reason'] == 'failover'],
                         ['kimi-k3'])

    def test_the_tier_is_the_panes_own_and_is_decided_once_for_the_turn(self):
        # By the second move `self.config` is a spare provider's, so re-deriving the tier would
        # read that provider's tier table: a Flash turn could finish on a Main model.
        error = ProviderError('Provider HTTP 503 for everyone.')
        self.stubs[FLASH.model] = Refuser(error)
        self.stubs['kimi-k2.7-code-highspeed'] = Refuser(error)
        self.stubs['gpt-5.6-terra'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k', 'openai': 'k'}, FLASH, 'glm'),
                           config=FLASH)
        with mock.patch.object(Agent, '_failover_tier', autospec=True,
                               side_effect=Agent._failover_tier) as tier:
            with mock.patch('relay_core.hosted.available', return_value=False):
                agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(tier.call_count, 1)                    # asked once, not once per move
        self.assertEqual([e['to_model'] for e in self.retries() if e['reason'] == 'failover'],
                         ['kimi-k2.7-code-highspeed', 'gpt-5.6-terra'])

    def test_a_save_while_the_turn_is_failed_over_records_the_panes_own_model(self):
        # `_autosave_soon` fires every MID_TURN_SAVE_S, and a title or summary thread saves from
        # its own thread: neither may write the spare provider into the session file, which is what
        # the sessions list, the resume picker and the index read.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429.'))
        saved = {}

        class Saver:
            calls = 0

            def complete(_self, messages, tools, emit, cancel):
                _self.calls += 1
                saved.update(agent.session_data())
                return {'role': 'assistant', 'content': 'from the spare'}

        self.stubs['kimi-k3'] = Saver()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(saved['model'], MAIN.model)
        self.assertEqual(saved['preset'], 'glm')
        # The spare still counts as a model this conversation ran on.
        self.assertEqual(agent.session_data()['model'], MAIN.model)

    def test_the_turn_reports_the_first_failure_not_the_last_providers(self):
        first = ProviderError('The pane key was rejected: HTTP 401 for glm-5.3.')
        spent = ProviderError('Your Relay Free allowance is spent.', 'quota_exhausted', 1758326400)
        self.stubs[MAIN.model] = Refuser(first)
        self.stubs['kimi-k3'] = Refuser(ProviderError('Provider HTTP 503 for kimi-k3.'))
        self.stubs['relay-main'] = Refuser(spent)
        with mock.patch('relay_core.hosted.available', return_value=True):
            agent = self.agent(roles=resolver({'kimi': 'k'}))
            agent.ask('hello')
        failed = self.events[-1]
        self.assertEqual(failed['event'], 'error')
        self.assertTrue(failed['text'].startswith(f'{MAIN.model} failed; '), failed['text'])
        self.assertIn('kimi-k3', failed['text'])
        self.assertIn('Relay Free', failed['text'])
        self.assertIn(str(first), failed['text'])
        self.assertNotIn('allowance', failed['text'])
        # Relay Free's code and reset time belong to Relay Free, not to this pane's failure.
        self.assertNotIn('code', failed)
        self.assertNotIn('resets_at', failed)

    def test_the_first_providers_own_code_still_travels(self):
        spent = ProviderError('Your Relay Free allowance is spent.', 'quota_exhausted', 1758326400)
        hosted_preset = PRESETS['relay-free']
        config = ProviderConfig(hosted_preset.base_url, hosted_preset.model, '')
        self.stubs[hosted_preset.model] = Refuser(spent)
        self.stubs['kimi-k3'] = Refuser(ProviderError('Provider HTTP 503 for kimi-k3.'))
        agent = self.agent(roles=resolver({'kimi': 'k'}, config, 'relay-free'),
                           config=config, preset_id='relay-free')
        agent.ask('hello')
        failed = self.events[-1]
        self.assertEqual(failed['code'], 'quota_exhausted')
        self.assertEqual(failed['resets_at'], 1758326400)
        self.assertIn('allowance is spent', failed['text'])

    def test_the_move_and_the_return_name_the_preset_on_the_note_and_on_the_status(self):
        # One shape for all three of the mechanisms that move a turn (protocol 15.2.2): the status
        # line named the bare model while the transcript note named model plus preset label, so the
        # pane's status bar and its transcript disagreed about where the turn was.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}))
        agent.ask('hello')
        glm, kimi = PRESETS['glm'].label, PRESETS['kimi'].label
        moved = next(e for e in self.retries() if e['reason'] == 'failover')
        self.assertEqual(moved['text'], f'{MAIN.model} ({glm}) keeps failing; '
                                        f'continuing this turn on kimi-k3 ({kimi}).')
        back = next(e for e in self.events if e.get('reason') == 'failover_ended')
        self.assertEqual(back['text'], f'Back to {MAIN.model} ({glm}).')
        statuses = [e['text'] for e in self.events if e['event'] == 'status']
        self.assertIn(f'{MAIN.model} ({glm}) failed · continuing on kimi-k3 ({kimi})', statuses)
        self.assertIn(f'Back to {MAIN.model} ({glm})', statuses)

    def test_a_resolver_that_raises_is_logged_and_the_turn_fails_on_its_own_error(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429.'))
        roles = resolver({'kimi': 'k'})
        with mock.patch.object(type(roles), 'failover_candidates', side_effect=KeyError('local:bonsai')):
            agent = self.agent(roles=roles)
            with self.assertLogs('relay.agent', level='ERROR') as caught:
                agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertIn('429', self.events[-1]['text'])
        self.assertTrue(any('provider_failover_unavailable' in line and 'KeyError' in line
                            for line in caught.output), caught.output)


class OptionTests(unittest.TestCase):
    def test_failover_is_a_boolean_turn_option(self):
        self.assertEqual(validate_turn_options({'failover': True}), {'failover': True})
        self.assertEqual(validate_turn_options({}), {})
        for bad in ('yes', 1):
            with self.assertRaises(ValueError):
                validate_turn_options({'failover': bad})

    def test_set_options_applies_at_once_and_reports_back(self):
        with tempfile.TemporaryDirectory() as temp:
            events = []
            self.stubs = {}
            agent = Agent(CONFIG, temp, events.append, preset_id='glm', provider=Answerer())
            self.assertTrue(agent.options()['failover'])         # on until the user says otherwise
            agent.set_options({'failover': False})
            self.assertFalse(agent.options()['failover'])
