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


def resolver(keys):
    return RoleResolver(CONFIG, 'glm', key_lookup=lambda preset_id: keys.get(preset_id, ''))


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
        return [e for e in self.events if e['event'] == 'provider_retry']

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
