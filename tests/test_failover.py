# SPDX-License-Identifier: AGPL-3.0-or-later
"""Failover (card #G9VE): a turn whose provider keeps failing continues on another one.

Where it continues is the Options › Models priority list below the pane's own model, in order —
the ``fallbacks`` option (owner, 2026-09-20) — then the same model on OpenRouter where the user
opted in, and then nothing: there is no catalog chain after the list and no pane-wide Relay Free
switch. Everything here is offline: the pane's provider is a stub that fails the way the test
wants, the failover targets are stubs behind a patched ``agent._provider_for``, and the roles
resolver hands out fake keys, so no request ever leaves the process.
"""
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from types import SimpleNamespace

from relay_core.agent import Agent, validate_turn_options
from relay_core.agents_defs import load_catalog
from relay_core.presets import PRESETS
from relay_core.provider import ProviderConfig, ProviderError, ProviderStalled, ProviderTruncated
from relay_core.roles import RoleResolver
from relay_core.subagents import SubagentFactory

MAIN = PRESETS['glm']
CONFIG = ProviderConfig(MAIN.base_url, MAIN.model, 'pane-key')
FLASH = ProviderConfig(MAIN.base_url, 'glm-5.3-flash', 'pane-key')   # this provider's Flash model
# Priority-list entries, as the GUI sends them: a preset id and the model the user ranked there
# (an empty model is the preset's own).
KIMI = [{'preset': 'kimi', 'model': ''}]
OPENAI_MINI = {'preset': 'openai', 'model': 'gpt-6-mini'}
RELAY_FREE = {'preset': 'relay-free', 'model': 'relay-main'}


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


def resolver(keys, config=CONFIG, preset_id='glm', roles=None):
    return RoleResolver(config, preset_id, roles or {},
                        key_lookup=lambda preset_id: keys.get(preset_id, ''))


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


class FallbackCandidateTests(unittest.TestCase):
    """`RoleResolver.fallback_candidate`: one entry of the Options › Models priority list (owner,
    2026-09-20), built on the same terms as any candidate."""

    def test_an_entry_is_built_on_the_same_terms_as_a_candidate(self):
        made = resolver({'kimi': 'k', 'openai': 'k'})
        with mock.patch('relay_core.hosted.available', return_value=False):
            found = made.fallback_candidate({'preset': 'openai', 'model': 'gpt-6-mini'}, 'main', {'glm'})
            self.assertEqual((found.preset_id, found.config.model, found.config.api_key, found.source),
                             ('openai', 'gpt-6-mini', 'k', 'failover'))
            # A missing model means the preset's own.
            self.assertEqual(made.fallback_candidate({'preset': 'kimi'}, 'main', {'glm'}).config.model, 'kimi-k3')
            # Not a spare: no stored key.
            self.assertIsNone(made.fallback_candidate({'preset': 'anthropic', 'model': 'claude-opus-5'}, 'main', {'glm'}))
            # The provider that just failed, and another key on the same host (glm-coding is api.z.ai too).
            self.assertIsNone(made.fallback_candidate({'preset': 'glm', 'model': 'glm-5.3'}, 'main', {'glm'}))
            self.assertIsNone(made.fallback_candidate({'preset': 'glm-coding', 'model': 'glm-5.3'}, 'main', {'glm'}))
            self.assertIsNone(made.fallback_candidate({'preset': 'kimi'}, 'main', set(), {'API.MOONSHOT.AI'}))
            # A guest harness, an unknown id, and shapes the option validator lets through as None.
            self.assertIsNone(made.fallback_candidate({'preset': 'guest:claude', 'model': 'opus'}, 'main', {'glm'}))
            self.assertIsNone(made.fallback_candidate({'preset': 'no-such', 'model': 'm'}, 'main', {'glm'}))
            self.assertIsNone(made.fallback_candidate(None, 'main', {'glm'}))
            self.assertIsNone(made.fallback_candidate({'model': 'kimi-k3'}, 'main', {'glm'}))

    def test_relay_free_is_an_entry_like_any_other_while_this_worker_can_use_it(self):
        # No pane-wide switch any more (owner, 2026-09-20): the user put it in the list. What still
        # gates it is whether this worker can talk to the gateway at all.
        with mock.patch('relay_core.hosted.available', return_value=True):
            on = resolver({}).fallback_candidate(RELAY_FREE, 'main', {'glm'})
        with mock.patch('relay_core.hosted.available', return_value=False):
            off = resolver({}).fallback_candidate(RELAY_FREE, 'main', {'glm'})
        self.assertTrue(on.config.hosted)
        self.assertEqual((on.preset_id, on.config.model), ('relay-free', 'relay-main'))
        self.assertIsNone(off)

    def test_a_model_server_on_this_machine_may_be_an_entry(self):
        # The user ranked it, so its answers are what they asked for.
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'local-models.json'
            path.write_text(json.dumps({'endpoints': [{'id': 'local:bonsai', 'label': 'Bonsai',
                                                       'base_url': 'http://127.0.0.1:8080/v1',
                                                       'model': 'bonsai-27b'}]}))
            with mock.patch.dict(os.environ, {'RELAY_LOCAL_MODELS': str(path)}):
                found = resolver({}).fallback_candidate({'preset': 'local:bonsai', 'model': 'bonsai-27b'},
                                                        'main', {'glm'})
        self.assertEqual((found.preset_id, found.config.model, found.config.api_key), ('local:bonsai', 'bonsai-27b', ''))
        self.assertTrue(found.config.local)


class OpenRouterTwinCandidateTests(unittest.TestCase):
    """`RoleResolver.openrouter_twin_candidate`: the same model on OpenRouter (owner, 2026-09-20),
    tried after the priority list and last of all, on the same terms as any candidate. Whether
    the user opted that model in is the agent's question, not the resolver's."""

    def test_the_twin_is_built_on_the_same_terms_as_a_candidate(self):
        made = resolver({'openrouter': 'k', 'kimi': 'k'})
        with mock.patch('relay_core.hosted.available', return_value=False):
            found = made.openrouter_twin_candidate('glm-5.3', 'main', {'glm'})
            self.assertEqual((found.preset_id, found.config.model, found.config.api_key, found.source, found.tier),
                             ('openrouter', 'z-ai/glm-5.3', 'k', 'failover', 'main'))
            self.assertEqual(found.config.base_url, PRESETS['openrouter'].base_url)
            # A Flash pane's model has its own twin; the tier is the record, not the lookup.
            self.assertEqual(made.openrouter_twin_candidate('glm-5.3-flash', 'flash', {'glm'}).config.model,
                             'z-ai/glm-5.3-flash')
            # High is max reasoning, in OpenRouter's own words.
            high = made.openrouter_twin_candidate('glm-5.3', 'high', {'glm'})
            self.assertEqual(high.config.extra, {'reasoning': {'effort': 'xhigh'}})
            # OpenRouter already asked this turn, and the failing host being openrouter.ai itself.
            self.assertIsNone(made.openrouter_twin_candidate('glm-5.3', 'main', {'glm', 'openrouter'}))
            self.assertIsNone(made.openrouter_twin_candidate('glm-5.3', 'main', set(), {'OPENROUTER.AI'}))
            # No twin: a Kimi Code alias, an id that is already a slug, nonsense.
            for model in ('kimi-for-coding', 'deepseek/deepseek-v4.1-flash', 'relay-main', '', None):
                self.assertIsNone(made.openrouter_twin_candidate(model, 'main', {'glm'}), model)
        # No OpenRouter key stored: not a spare, never an error.
        self.assertIsNone(resolver({'kimi': 'k'}).openrouter_twin_candidate('glm-5.3', 'main', {'glm'}))


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

    def agent(self, *, provider=None, failover=True, roles=None, config=CONFIG, preset_id='glm',
              fallbacks=None, fallback=None, effort=None, failover_openrouter=None, **extra):
        return Agent(config, self.temp.name, self.events.append, provider=provider,
                     preset_id=preset_id, failover=failover, fallbacks=fallbacks,
                     fallback=fallback, roles=roles, effort=effort,
                     failover_openrouter=failover_openrouter, **extra)

    def retries(self):
        """The moves, not the closing "back to the pane's own model" note."""
        return [e for e in self.events if e['event'] == 'provider_retry'
                and e['reason'] != 'failover_ended']

    def test_a_failing_provider_hands_the_turn_to_the_first_entry_of_the_list(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429 for glm-5.3.'))
        spare = Answerer()
        self.stubs['kimi-k3'] = spare
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
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

    def test_the_lists_order_is_the_order_whatever_the_catalogs(self):
        # Kimi comes first in PRESETS; the user ranked OpenAI's gpt-6-mini above it in Options ›
        # Models (owner, 2026-09-20), so that is where the turn goes.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429 for glm-5.3.'))
        self.stubs['kimi-k3'] = Answerer()
        spare = Answerer()
        self.stubs['gpt-6-mini'] = spare
        agent = self.agent(roles=resolver({'kimi': 'k', 'openai': 'k'}),
                           fallbacks=[OPENAI_MINI, *KIMI])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        moved = next(e for e in self.retries() if e['reason'] == 'failover')
        self.assertEqual((moved['to_model'], moved['to_preset']), ('gpt-6-mini', 'openai'))
        self.assertEqual((spare.calls, self.stubs['kimi-k3'].calls), (1, 0))
        self.assertEqual(agent.config.model, MAIN.model)          # for that turn only, as ever

    def test_when_the_first_entry_fails_too_the_second_follows(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        self.stubs['gpt-6-mini'] = Refuser(ProviderError('Provider HTTP 503 for gpt-6-mini.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k', 'openai': 'k'}),
                           fallbacks=[OPENAI_MINI, *KIMI])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual([(e['attempt'], e['max_attempts'], e['to_model']) for e in self.retries()],
                         [(1, 2, 'gpt-6-mini'), (2, 2, 'kimi-k3')])
        self.assertEqual(self.stubs['gpt-6-mini'].calls, 1)      # asked once, like any provider

    def test_a_keyed_preset_the_list_does_not_name_is_never_asked(self):
        # The list is the whole of where a turn may go (owner, 2026-09-20): no catalog chain
        # after it. Kimi has a key and is not named, so the turn fails rather than land there.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        self.stubs['kimi-k3'] = Answerer()
        for fallbacks in ([], [OPENAI_MINI]):
            with self.subTest(fallbacks=fallbacks):
                self.events.clear()
                self.stubs['gpt-6-mini'] = Refuser(ProviderError('Provider HTTP 503 for gpt-6-mini.'))
                agent = self.agent(roles=resolver({'kimi': 'k', 'openai': 'k'}), fallbacks=fallbacks)
                agent.ask('hello')
                self.assertEqual(self.events[-1]['event'], 'error')
                self.assertIn('503', self.events[-1]['text'])
                self.assertEqual([e['to_model'] for e in self.retries()],
                                 ['gpt-6-mini'] if fallbacks else [])
                self.assertEqual(self.stubs['kimi-k3'].calls, 0)

    def test_the_list_is_walked_as_far_as_it_goes(self):
        # "As many as you want, according to priority": four entries, the first three down.
        down = ProviderError('Provider HTTP 503.')
        self.stubs[MAIN.model] = Refuser(down)
        for model in ('gpt-6-mini', 'kimi-k3', 'claude-opus-5'):
            self.stubs[model] = Refuser(down)
        self.stubs['MiniMax-M3'] = Answerer()
        agent = self.agent(roles=resolver({'openai': 'k', 'kimi': 'k', 'anthropic': 'k', 'minimax': 'k'}),
                           fallbacks=[OPENAI_MINI, *KIMI, {'preset': 'anthropic', 'model': 'claude-opus-5'},
                                      {'preset': 'minimax', 'model': 'MiniMax-M3'}])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual([(e['attempt'], e['max_attempts'], e['to_model']) for e in self.retries()],
                         [(1, 4, 'gpt-6-mini'), (2, 4, 'kimi-k3'), (3, 4, 'claude-opus-5'), (4, 4, 'MiniMax-M3')])
        self.assertEqual(agent.config.model, MAIN.model)

    def test_an_entry_that_cannot_take_the_turn_is_skipped_for_the_next(self):
        # Itself, another key on the failing host, a preset with no stored key, a guest harness:
        # each is skipped silently and the entry below it is tried.
        for fallback in ({'preset': 'glm', 'model': 'glm-5.3'},
                         {'preset': 'glm-coding', 'model': 'glm-5.3'},
                         {'preset': 'openai', 'model': 'gpt-6-mini'},
                         {'preset': 'guest:claude', 'model': 'opus'}):
            with self.subTest(fallback=fallback):
                self.events.clear()
                self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
                self.stubs['kimi-k3'] = Answerer()
                self.stubs['gpt-6-mini'] = Answerer()
                agent = self.agent(roles=resolver({'kimi': 'k', 'glm-coding': 'k'}),
                                   fallbacks=[fallback, *KIMI])
                agent.ask('hello')
                self.assertEqual(self.events[-1]['event'], 'done')
                self.assertEqual([e['to_model'] for e in self.retries()], ['kimi-k3'])
                self.assertEqual(self.stubs['gpt-6-mini'].calls, 0)

    def test_an_entry_runs_at_the_panes_effort(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        seen = {}

        class Effortful(Answerer):
            def complete(inner, messages, tools, emit, cancel):
                seen['effort'] = agent.effort
                seen['extra'] = dict(agent.config.extra)
                return super().complete(messages, tools, emit, cancel)
        self.stubs['gpt-6-mini'] = Effortful()
        agent = self.agent(roles=resolver({'openai': 'k'}), effort='high', fallbacks=[OPENAI_MINI])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(seen['effort'], 'high')
        self.assertIn('high', json.dumps(seen['extra']))           # said in OpenAI's own words

    def test_a_model_the_user_opted_in_continues_on_its_openrouter_twin_after_the_list(self):
        # The list is empty and Kimi, keyed, is not on it; the user ticked "fall back to the same
        # model on OpenRouter" for glm-5.3 (owner, 2026-09-20), so that is where the turn goes.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429 for glm-5.3.'))
        self.stubs['kimi-k3'] = Answerer()
        twin = Answerer()
        self.stubs['z-ai/glm-5.3'] = twin
        agent = self.agent(roles=resolver({'kimi': 'k', 'openrouter': 'k'}),
                           failover_openrouter=['glm-5.3'])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        moved = next(e for e in self.retries() if e['reason'] == 'failover')
        self.assertEqual((moved['to_model'], moved['to_preset']), ('z-ai/glm-5.3', 'openrouter'))
        self.assertEqual((twin.calls, self.stubs['kimi-k3'].calls), (1, 0))
        # The note says what it is: the same model, through OpenRouter.
        glm, router = PRESETS['glm'].label, PRESETS['openrouter'].label
        self.assertEqual(moved['text'], f'{MAIN.model} ({glm}) keeps failing; continuing this turn on '
                                        f'the same model through OpenRouter (z-ai/glm-5.3 ({router})).')
        self.assertEqual(agent.config.model, MAIN.model)          # for that turn only, as ever

    def test_the_whole_list_goes_before_the_twin_and_the_turn_fails_after_it(self):
        # The order, exactly (owner, 2026-09-20): every entry of the list, then the twin, then
        # stop. Anthropic has a key and is not named: it is never asked.
        down = ProviderError('Provider HTTP 503.')
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        for model in ('gpt-6-mini', 'kimi-k3', 'z-ai/glm-5.3'):
            self.stubs[model] = Refuser(down)
        self.stubs['claude-opus-5'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k', 'openai': 'k', 'openrouter': 'k', 'anthropic': 'k'}),
                           fallbacks=[OPENAI_MINI, *KIMI], failover_openrouter=['glm-5.3'])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertTrue(self.events[-1]['text'].startswith(f'{MAIN.model} failed; '), self.events[-1]['text'])
        self.assertEqual([(e['attempt'], e['max_attempts'], e['to_model']) for e in self.retries()],
                         [(1, 3, 'gpt-6-mini'), (2, 3, 'kimi-k3'), (3, 3, 'z-ai/glm-5.3')])
        self.assertEqual([self.stubs[m].calls for m in ('gpt-6-mini', 'kimi-k3', 'z-ai/glm-5.3')], [1, 1, 1])
        self.assertEqual(self.stubs['claude-opus-5'].calls, 0)
        self.assertEqual(agent.config.model, MAIN.model)

    def test_the_twin_is_tried_once_even_when_the_list_names_openrouter(self):
        # OpenRouter in the list is asked there, and the twin does not ask it again.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        self.stubs['deepseek/deepseek-v4.1-flash'] = Refuser(ProviderError('Provider HTTP 503.'))
        self.stubs['z-ai/glm-5.3'] = Answerer()
        agent = self.agent(roles=resolver({'openrouter': 'k'}),
                           fallbacks=[{'preset': 'openrouter', 'model': ''}], failover_openrouter=['glm-5.3'])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual([e['to_model'] for e in self.retries()], ['deepseek/deepseek-v4.1-flash'])
        self.assertEqual(self.stubs['z-ai/glm-5.3'].calls, 0)

    def test_the_twin_is_off_by_default_and_opted_in_per_model(self):
        for opted in (None, [], ['glm-5.3-flash'], ['z-ai/glm-5.3']):
            with self.subTest(opted=opted):
                self.events.clear()
                self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
                self.stubs['kimi-k3'] = Answerer()
                self.stubs['z-ai/glm-5.3'] = Answerer()
                agent = self.agent(roles=resolver({'kimi': 'k', 'openrouter': 'k'}), fallbacks=KIMI,
                                   failover_openrouter=opted)
                agent.ask('hello')
                self.assertEqual(self.events[-1]['event'], 'done')
                self.assertEqual([e['to_model'] for e in self.retries()], ['kimi-k3'])
                self.assertEqual(self.stubs['z-ai/glm-5.3'].calls, 0)

    def test_a_twin_that_cannot_take_the_turn_is_skipped_silently(self):
        # No OpenRouter key stored; the failing provider is OpenRouter itself; a model with no
        # twin (a Kimi Code alias): each leaves the list as the whole of it. The twin is tried
        # after the list, so the list here holds one entry that cannot take the turn either.
        kimi_code = PRESETS['kimi-code']
        cases = (
            ('no key', CONFIG, 'glm', {'kimi': 'k'}, ['glm-5.3'], 'kimi', 'kimi-k3'),
            ('openrouter itself', ProviderConfig(PRESETS['openrouter'].base_url, 'glm-5.3', 'pane-key'),
             'openrouter', {'kimi': 'k', 'openrouter': 'k'}, ['glm-5.3'], 'kimi', 'kimi-k3'),
            ('no twin', ProviderConfig(kimi_code.base_url, 'kimi-for-coding', 'pane-key'),
             'kimi-code', {'glm': 'k', 'openrouter': 'k'}, ['kimi-for-coding'], 'glm', 'glm-5.3'),
        )
        for name, config, preset_id, keys, opted, spare, expected in cases:
            with self.subTest(name):
                self.events.clear()
                self.stubs.clear()
                self.stubs[config.model] = Refuser(ProviderError(f'Provider HTTP 503 for {config.model}.'))
                self.stubs['z-ai/glm-5.3'] = Answerer()
                self.stubs[expected] = Answerer()
                agent = self.agent(config=config, preset_id=preset_id, roles=resolver(keys, config, preset_id),
                                   fallbacks=[{'preset': spare, 'model': expected}], failover_openrouter=opted)
                agent.ask('hello')
                self.assertEqual(self.events[-1]['event'], 'done')
                self.assertEqual([e['to_model'] for e in self.retries()], [expected])
                self.assertEqual(self.stubs['z-ai/glm-5.3'].calls, 0)

    def test_the_twin_runs_at_the_panes_effort_in_openrouters_words(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        seen = {}

        class Effortful(Answerer):
            def complete(inner, messages, tools, emit, cancel):
                seen['effort'] = agent.effort
                seen['extra'] = dict(agent.config.extra)
                return super().complete(messages, tools, emit, cancel)
        self.stubs['z-ai/glm-5.3'] = Effortful()
        agent = self.agent(roles=resolver({'openrouter': 'k'}), effort='high',
                           failover_openrouter=['glm-5.3'])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(seen['effort'], 'high')
        self.assertEqual(seen['extra'], {'reasoning': {'effort': 'high'}})

    def test_each_provider_is_asked_once_and_the_turn_fails_when_none_answers(self):
        error = ProviderError('Provider HTTP 503 for everyone.')
        stubs = {MAIN.model: Refuser(error), 'kimi-k3': Refuser(error), 'gpt-6-astra': Refuser(error)}
        self.stubs.update(stubs)
        agent = self.agent(roles=resolver({'kimi': 'k', 'openai': 'k'}),
                           fallbacks=[*KIMI, {'preset': 'openai', 'model': ''}])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertIn('503', self.events[-1]['text'])
        self.assertEqual([e['attempt'] for e in self.retries()], [1, 2])
        self.assertEqual([stub.calls for stub in stubs.values()], [1, 1, 1])
        self.assertEqual(agent.config.model, MAIN.model)

    def test_the_option_off_keeps_the_pane_provider(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429 for glm-5.3.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(failover=False, roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.retries(), [])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)

    def test_a_stalled_provider_fails_over_but_a_truncated_step_does_not(self):
        self.stubs[MAIN.model] = Refuser(ProviderStalled(60.0))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
        agent.ask('hello')
        self.assertEqual([e['event'] for e in self.events[-1:]], ['done'])
        self.assertEqual(self.stubs[MAIN.model].calls, 2)      # the stall retry, then the move
        self.assertTrue(any(e['reason'] == 'failover' for e in self.retries()))

        self.events.clear()
        self.stubs[MAIN.model] = Refuser(ProviderTruncated('length', 8192))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertFalse(any(e['reason'] == 'failover' for e in self.retries()))
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)

    def test_no_resolver_or_an_injected_provider_never_moves(self):
        # A test agent has no resolver: it cannot know which providers are keyed. (A subagent does
        # have one since 2026-09-19 — SubagentFailoverTests below.)
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 500.'))
        agent = self.agent(roles=None)
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.retries(), [])
        # An injected provider belongs to whoever built it (tests, side calls).
        self.events.clear()
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 500.'))
        agent = self.agent(provider=Refuser(ProviderError('Provider HTTP 500.')),
                           roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.retries(), [])

    def test_a_flash_pane_fails_over_to_the_entrys_model(self):
        # The entry names the model (the GUI sends the one the user ranked), so a Flash pane goes
        # where the list says; the tier it carries is the record and the effort, not the lookup.
        flash = 'glm-5.3-flash'
        self.stubs[flash] = Refuser(ProviderError('Provider HTTP 429.'))
        spare = Answerer()
        self.stubs['kimi-k2.7-code-highspeed'] = spare
        agent = self.agent(roles=resolver({'kimi': 'k'}), config=FLASH,
                           fallbacks=[{'preset': 'kimi', 'model': 'kimi-k2.7-code-highspeed'}])
        self.assertEqual(agent._failover_tier(), 'flash')
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
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.retries(), [])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)
        # Reasoning alone is not an answer: that call still moves.
        self.events.clear()
        self.stubs[MAIN.model] = Streamer(ProviderError('Provider HTTP 500.'), kind='thinking_delta')
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
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
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
        agent.ask('hello')
        answered['n'] = self.stubs['kimi-k3'].calls
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(answered['n'], 1)

    def test_the_failed_provider_response_is_closed_before_the_swap(self):
        stub = OpenRefuser(ProviderError('Provider HTTP 429.'), self.events)
        self.stubs[MAIN.model] = stub
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
        agent.ask('hello')
        self.assertGreaterEqual(stub.cancels, 1)
        kinds = [e['event'] for e in self.events]
        moved = next(i for i, e in enumerate(self.events)
                     if e['event'] == 'provider_retry' and e['reason'] == 'failover')
        self.assertLess(kinds.index('stub_cancelled'), moved)

    def test_the_note_lands_after_the_thinking_block_it_interrupts(self):
        self.stubs[MAIN.model] = Streamer(ProviderError('Provider HTTP 500.'), kind='thinking_delta')
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
        agent.ask('hello')
        kinds = [e['event'] for e in self.events]
        moved = next(i for i, e in enumerate(self.events)
                     if e['event'] == 'provider_retry' and e['reason'] == 'failover')
        self.assertLess(kinds.index('thinking_done'), moved)

    def test_the_restore_is_announced_before_the_terminal_event(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 429.'))
        self.stubs['kimi-k3'] = Answerer()
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
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
                           config=config, preset_id='openrouter', fallbacks=KIMI)
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
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
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
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
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
                           config=config, preset_id=None,
                           fallbacks=[{'preset': 'glm', 'model': ''}, {'preset': 'glm-coding', 'model': ''}, *KIMI])
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
        agent = self.agent(roles=resolver({'kimi': 'k', 'openai': 'k'}, FLASH, 'glm'), config=FLASH,
                           fallbacks=[{'preset': 'kimi', 'model': 'kimi-k2.7-code-highspeed'},
                                      {'preset': 'openai', 'model': 'gpt-5.6-terra'}])
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
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
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
            agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=[*KIMI, RELAY_FREE])
            agent.ask('hello')
        failed = self.events[-1]
        self.assertEqual(failed['event'], 'error')
        self.assertTrue(failed['text'].startswith(f'{MAIN.model} failed; '), failed['text'])
        self.assertIn('kimi-k3', failed['text'])
        self.assertIn(PRESETS['relay-free'].label, failed['text'])
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
                           config=config, preset_id='relay-free', fallbacks=KIMI)
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
        agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=KIMI)
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
        with mock.patch.object(type(roles), 'fallback_candidate', side_effect=KeyError('local:bonsai')):
            agent = self.agent(roles=roles, fallbacks=KIMI)
            with self.assertLogs('relay.agent', level='ERROR') as caught:
                agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertIn('429', self.events[-1]['text'])
        self.assertTrue(any('provider_failover_unavailable' in line and 'KeyError' in line
                            for line in caught.output), caught.output)


class RoutedStepTests(unittest.TestCase):
    """A routed step drops back to the pane's own model before any failover (protocol 15.2.3).

    The plan and vision halves live with their own turn mechanics in tests/test_plan_turns.py and
    tests/test_images.py; what belongs here is the handover to the chain.
    """

    PLANNER = {'planning': {'preset': 'openai', 'model': 'gpt-6-astra'}}

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.events = []
        self.stubs = {}
        patcher = mock.patch('relay_core.agent._provider_for',
                             side_effect=lambda config, stall: self.stubs[config.model])
        patcher.start()
        self.addCleanup(patcher.stop)

    def plan_agent(self, keys, fallbacks=KIMI):
        agent = Agent(CONFIG, self.temp.name, self.events.append, preset_id='glm',
                      roles=resolver(keys, roles=self.PLANNER), fallbacks=fallbacks)
        agent.set_mode('plan')
        return agent

    def reasons(self):
        return [e['reason'] for e in self.events if e['event'] == 'provider_retry']

    def test_the_pane_model_is_tried_before_anyone_elses_and_then_the_list(self):
        down = ProviderError('Provider HTTP 503.')
        self.stubs['gpt-6-astra'] = Refuser(down)      # the pinned planning model
        self.stubs[MAIN.model] = Refuser(down)         # the pane's own, tried next
        spare = Answerer()
        self.stubs['kimi-k3'] = spare
        agent = self.plan_agent({'openai': 'k', 'kimi': 'k'})
        agent.ask('plan this')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(self.reasons(), ['route_dropped', 'failover', 'failover_ended'])
        self.assertEqual(spare.calls, 1)
        self.assertEqual(agent.config.model, MAIN.model)
        self.assertIsNone(agent._planning)
        self.assertIsNone(agent._failover)

    def test_the_dropped_provider_is_not_offered_back_as_a_spare(self):
        # It has just refused; asking it again under the same key is a wasted move, exactly as it
        # is for two keys on one host.
        down = ProviderError('Provider HTTP 503 for the planner.')
        self.stubs['gpt-6-astra'] = Refuser(down)
        self.stubs[MAIN.model] = Refuser(down)
        agent = self.plan_agent({'openai': 'k'}, [{'preset': 'openai', 'model': ''}])   # and the list names it
        agent.ask('plan this')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.reasons(), ['route_dropped'])
        self.assertEqual(self.stubs['gpt-6-astra'].calls, 1)

    def test_a_planning_model_that_streamed_an_answer_keeps_the_turn(self):
        # The same rule as a failover's: that text is on the user's screen.
        self.stubs['gpt-6-astra'] = Streamer(ProviderError('Provider HTTP 500 mid-stream.'))
        self.stubs[MAIN.model] = Answerer()
        agent = self.plan_agent({'openai': 'k', 'kimi': 'k'})
        agent.ask('plan this')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.reasons(), [])
        self.assertEqual(self.stubs[MAIN.model].calls, 0)


class RelayFreeInTheListTests(unittest.TestCase):
    """Relay Free is a failover target when the priority list names it, and only then (owner,
    2026-09-20): the pane-wide `failover_hosted` switch of 2026-09-19 is gone."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.events = []
        self.stubs = {}
        patcher = mock.patch('relay_core.agent._provider_for',
                             side_effect=lambda config, stall: self.stubs[config.model])
        patcher.start()
        self.addCleanup(patcher.stop)

    def agent(self, *, roles=None, config=CONFIG, preset_id='glm', fallbacks=None, **extra):
        return Agent(config, self.temp.name, self.events.append, preset_id=preset_id,
                     roles=roles, fallbacks=fallbacks, **extra)

    def retries(self):
        return [e for e in self.events if e['event'] == 'provider_retry'
                and e['reason'] != 'failover_ended']

    def test_unnamed_it_is_never_a_target_even_with_nothing_else_to_try(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        self.stubs['relay-main'] = Answerer()
        for fallbacks in ([], [OPENAI_MINI]):           # nothing, or an entry with no key
            with self.subTest(fallbacks=fallbacks):
                self.events.clear()
                with mock.patch('relay_core.hosted.available', return_value=True):
                    agent = self.agent(roles=resolver({}), fallbacks=fallbacks)
                    agent.ask('hello')
                self.assertEqual(self.events[-1]['event'], 'error')
                self.assertEqual(self.retries(), [])
                self.assertEqual(self.stubs['relay-main'].calls, 0)
        self.assertNotIn('failover_hosted', agent.options())

    def test_named_in_the_list_the_turn_continues_on_relays_hosted_service(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        spare = Answerer()
        self.stubs['relay-main'] = spare
        with mock.patch('relay_core.hosted.available', return_value=True):
            agent = self.agent(roles=resolver({}), fallbacks=[RELAY_FREE])
            agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(spare.calls, 1)
        moved = next(e for e in self.retries() if e['reason'] == 'failover')
        self.assertEqual((moved['to_model'], moved['to_preset']), ('relay-main', 'relay-free'))
        # The note says what it is, not just which model: this is Relay's own service.
        self.assertIn("continuing this turn on Relay's hosted service", moved['text'])
        self.assertEqual(agent.config.model, MAIN.model)

    def test_it_takes_the_place_the_user_gave_it(self):
        for fallbacks, expected in (([RELAY_FREE, *KIMI], ['relay-main']),
                                    ([*KIMI, RELAY_FREE], ['kimi-k3'])):
            with self.subTest(fallbacks=fallbacks):
                self.events.clear()
                self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
                self.stubs['kimi-k3'] = Answerer()
                self.stubs['relay-main'] = Answerer()
                with mock.patch('relay_core.hosted.available', return_value=True):
                    agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=fallbacks)
                    agent.ask('hello')
                self.assertEqual(self.events[-1]['event'], 'done')
                self.assertEqual([e['to_model'] for e in self.retries()], expected)

    def test_named_but_unusable_here_it_is_skipped_for_the_next_entry(self):
        # `hosted.available` is False when python3-cryptography is missing: the row is unusable,
        # so the entry is skipped the way a keyless preset is.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        self.stubs['kimi-k3'] = Answerer()
        self.stubs['relay-main'] = Answerer()
        with mock.patch('relay_core.hosted.available', return_value=False):
            agent = self.agent(roles=resolver({'kimi': 'k'}), fallbacks=[RELAY_FREE, *KIMI])
            agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual([e['to_model'] for e in self.retries()], ['kimi-k3'])
        self.assertEqual(self.stubs['relay-main'].calls, 0)

    def test_an_older_guis_failover_hosted_is_accepted_and_ignored(self):
        # A GUI from before the list still sends the switch. It must neither refuse the configure
        # nor put Relay Free back into the chain.
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        self.stubs['relay-main'] = Answerer()
        with mock.patch('relay_core.hosted.available', return_value=True):
            agent = self.agent(roles=resolver({}), fallbacks=[], failover_hosted=True)
            agent.set_options({'failover_hosted': True})
            agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.stubs['relay-main'].calls, 0)
        self.assertEqual(validate_turn_options({'failover_hosted': True}), {})
        self.assertEqual(validate_turn_options({'failover_hosted': 'yes'}), {})


class SubagentFailoverTests(unittest.TestCase):
    """A subagent follows the pane's list (owner, 2026-09-19; the list since 2026-09-20).

    `subagents.py` used to build its `Agent` with no roles resolver at all, and `_begin_failover`
    refuses every move without one — so a subagent whose provider kept failing simply failed, and
    the card's "a Flash subagent fails over within Flash" line could not be driven.
    """

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.events = []
        self.stubs = {}
        patcher = mock.patch('relay_core.agent._provider_for',
                             side_effect=lambda config, stall: self.stubs[config.model])
        patcher.start()
        self.addCleanup(patcher.stop)
        self.catalog = load_catalog(self.temp.name, [])

    def factory(self, keys, *, main=None, provider_factory=None):
        return SubagentFactory(CONFIG, self.temp.name, preset_id='glm',
                               key_lookup=lambda preset: keys.get(preset, ''),
                               roles=resolver(keys), main_agent=main,
                               provider_factory=provider_factory)

    def subagent(self, factory, model=None):
        agent, _label, _warnings = factory(self.catalog.get('explore'), model, None,
                                           self.events.append, 'a1')
        return agent

    def moves(self):
        return [e for e in self.events if e['event'] == 'provider_retry' and e['reason'] == 'failover']

    def test_a_subagent_continues_down_the_panes_list(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503 for glm-5.3.'))
        spare = Answerer()
        self.stubs['kimi-k3'] = spare
        sub = self.subagent(self.factory({'kimi': 'k'}, main=SimpleNamespace(failover=True, fallbacks=KIMI)))
        sub.ask('go')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(spare.calls, 1)
        self.assertEqual([e['to_model'] for e in self.moves()], ['kimi-k3'])

    def test_a_flash_subagent_follows_the_list_like_a_flash_pane(self):
        self.stubs['glm-5.3-flash'] = Refuser(ProviderError('Provider HTTP 429.'))
        spare = Answerer()
        self.stubs['kimi-k2.7-code-highspeed'] = spare
        main = SimpleNamespace(failover=True, fallbacks=[{'preset': 'kimi', 'model': 'kimi-k2.7-code-highspeed'}])
        sub = self.subagent(self.factory({'kimi': 'k'}, main=main), model='flash')
        self.assertEqual(sub.config.model, 'glm-5.3-flash')
        sub.ask('go')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual([e['to_model'] for e in self.moves()], ['kimi-k2.7-code-highspeed'])
        self.assertEqual(spare.calls, 1)

    def test_it_follows_the_panes_switches_and_not_its_own(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503.'))
        self.stubs['kimi-k3'] = Answerer()
        self.stubs['relay-main'] = Answerer()
        off = SimpleNamespace(failover=False, fallbacks=KIMI)
        sub = self.subagent(self.factory({'kimi': 'k'}, main=off))
        self.assertFalse(sub.failover)
        sub.ask('go')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.moves(), [])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)
        # And Relay Free is the pane's list's decision there too, not a second one hidden in a
        # subagent: unnamed it is never a target, named it is.
        self.events.clear()
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503.'))
        sub = self.subagent(self.factory({}, main=SimpleNamespace(failover=True, fallbacks=[])))
        self.assertEqual(sub.fallbacks, [])
        with mock.patch('relay_core.hosted.available', return_value=True):
            sub.ask('go')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.stubs['relay-main'].calls, 0)
        self.events.clear()
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503.'))
        sub = self.subagent(self.factory({}, main=SimpleNamespace(failover=True, fallbacks=[RELAY_FREE])))
        with mock.patch('relay_core.hosted.available', return_value=True):
            sub.ask('go')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(self.stubs['relay-main'].calls, 1)

    def test_it_follows_the_panes_list_in_the_panes_order(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503.'))
        self.stubs['kimi-k3'] = Answerer()
        self.stubs['gpt-6-mini'] = Answerer()
        main = SimpleNamespace(failover=True, fallbacks=[OPENAI_MINI, *KIMI])
        sub = self.subagent(self.factory({'kimi': 'k', 'openai': 'k'}, main=main))
        self.assertEqual(sub.fallbacks, main.fallbacks)
        # A pane double that says nothing about it: an empty list, as for the pane.
        self.assertEqual(self.subagent(self.factory({}, main=SimpleNamespace(failover=True))).fallbacks, [])
        sub.ask('go')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual([e['to_model'] for e in self.moves()], ['gpt-6-mini'])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)

    def test_it_follows_the_panes_openrouter_opt_in(self):
        self.stubs[MAIN.model] = Refuser(ProviderError('Provider HTTP 503.'))
        self.stubs['kimi-k3'] = Answerer()
        self.stubs['z-ai/glm-5.3'] = Answerer()
        main = SimpleNamespace(failover=True, failover_openrouter=['glm-5.3'])
        sub = self.subagent(self.factory({'kimi': 'k', 'openrouter': 'k'}, main=main))
        self.assertEqual(sub.failover_openrouter, ['glm-5.3'])
        sub.ask('go')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual([e['to_model'] for e in self.moves()], ['z-ai/glm-5.3'])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)
        # A pane double that says nothing about it: off, as for the pane.
        self.assertEqual(self.subagent(self.factory({}, main=SimpleNamespace(failover=True))).failover_openrouter, [])

    def test_an_injected_provider_is_never_replaced(self):
        # The tests' own provider factory, and a guest harness: its owner decides what serves.
        injected = Refuser(ProviderError('Provider HTTP 503.'))
        self.stubs['kimi-k3'] = Answerer()
        sub = self.subagent(self.factory({'kimi': 'k'}, main=SimpleNamespace(failover=True, fallbacks=KIMI),
                                         provider_factory=lambda config: injected))
        sub.ask('go')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.moves(), [])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)
        self.assertIs(sub.provider, injected)


class OptionTests(unittest.TestCase):
    def test_failover_is_a_boolean_turn_option(self):
        self.assertEqual(validate_turn_options({'failover': True}), {'failover': True})
        self.assertEqual(validate_turn_options({}), {})
        for bad in ('yes', 1):
            with self.assertRaises(ValueError):
                validate_turn_options({'failover': bad})

    def test_the_list_is_ordered_preset_and_model_pairs(self):
        # Each {"preset", "model"} is kept in order; a missing model is the preset's own; an
        # entry that is not that shape is dropped and a value that is not a list is the empty
        # list — never an error: the priority list is whatever the GUI has, and a row it cannot
        # express must not refuse the whole configure.
        self.assertEqual(validate_turn_options({'fallbacks': [OPENAI_MINI, {'preset': ' kimi '}]}),
                         {'fallbacks': [OPENAI_MINI, {'preset': 'kimi', 'model': ''}]})
        self.assertEqual(validate_turn_options({'fallbacks': [OPENAI_MINI, 'kimi', {}, {'preset': ''}, None,
                                                              {'model': 'x'}, {'preset': 7}, OPENAI_MINI]}),
                         {'fallbacks': [OPENAI_MINI]})                      # bad entries and the repeat dropped
        for bad in (None, 'openai', 3, {}, {'preset': 'openai'}, True):
            self.assertEqual(validate_turn_options({'fallbacks': bad}), {'fallbacks': []}, bad)
        self.assertEqual(validate_turn_options({'fallbacks': []}), {'fallbacks': []})
        self.assertNotIn('fallbacks', validate_turn_options({}))
        # `fallback` singular, the one-entry shape of 2026-09-20 morning, is a one-element list —
        # and `fallbacks` wins when a GUI sends both.
        self.assertEqual(validate_turn_options({'fallback': OPENAI_MINI}), {'fallbacks': [OPENAI_MINI]})
        self.assertEqual(validate_turn_options({'fallback': None}), {'fallbacks': []})
        self.assertEqual(validate_turn_options({'fallback': {'model': 'x'}}), {'fallbacks': []})
        self.assertEqual(validate_turn_options({'fallback': OPENAI_MINI, 'fallbacks': KIMI}), {'fallbacks': KIMI})
        self.assertEqual(validate_turn_options({'fallback': OPENAI_MINI, 'fallbacks': []}), {'fallbacks': []})

    def test_the_agent_reads_the_singular_shape_too(self):
        with tempfile.TemporaryDirectory() as temp:
            agent = Agent(CONFIG, temp, lambda e: None, preset_id='glm', provider=Answerer(),
                          fallback=OPENAI_MINI)
            self.assertEqual(agent.fallbacks, [OPENAI_MINI])
            agent = Agent(CONFIG, temp, lambda e: None, preset_id='glm', provider=Answerer(),
                          fallback=OPENAI_MINI, fallbacks=KIMI)
            self.assertEqual(agent.fallbacks, KIMI)

    def test_the_openrouter_opt_in_is_a_list_of_model_ids_or_nothing(self):
        # Per model, off by default (owner, 2026-09-20): a list of ids is kept, trimmed and
        # de-duplicated; anything else is the empty list, never an error, as for `fallback`.
        self.assertEqual(validate_turn_options({'failover_openrouter': ['glm-5.3-flash', ' glm-5.3 ', 'glm-5.3']}),
                         {'failover_openrouter': ['glm-5.3-flash', 'glm-5.3']})
        self.assertEqual(validate_turn_options({'failover_openrouter': []}), {'failover_openrouter': []})
        self.assertEqual(validate_turn_options({'failover_openrouter': None}), {'failover_openrouter': []})
        for bad in ('glm-5.3', True, 3, {'glm-5.3': True}, [3, '', None]):
            self.assertEqual(validate_turn_options({'failover_openrouter': bad}), {'failover_openrouter': []}, bad)
        self.assertNotIn('failover_openrouter', validate_turn_options({}))

    def test_set_options_applies_at_once_and_reports_back(self):
        with tempfile.TemporaryDirectory() as temp:
            events = []
            self.stubs = {}
            agent = Agent(CONFIG, temp, events.append, preset_id='glm', provider=Answerer())
            self.assertTrue(agent.options()['failover'])         # on until the user says otherwise
            agent.set_options({'failover': False})
            self.assertFalse(agent.options()['failover'])
            # The priority list: empty until Options › Models has a second row, changed between
            # turns, and null clears it.
            self.assertEqual(agent.options()['fallbacks'], [])
            agent.set_options({'fallbacks': [OPENAI_MINI, *KIMI]})
            self.assertEqual(agent.options()['fallbacks'], [OPENAI_MINI, *KIMI])
            agent.set_options({'failover': True})                 # says nothing about it: kept
            self.assertEqual(agent.fallbacks, [OPENAI_MINI, *KIMI])
            agent.set_options({'fallback': OPENAI_MINI})          # the singular shape still lands
            self.assertEqual(agent.options()['fallbacks'], [OPENAI_MINI])
            agent.set_options({'fallbacks': None})
            self.assertEqual(agent.options()['fallbacks'], [])
            # The OpenRouter opt-in: nothing until a model is ticked, and a list of ids after.
            self.assertEqual(agent.options()['failover_openrouter'], [])
            agent.set_options({'failover_openrouter': ['glm-5.3-flash']})
            self.assertEqual(agent.options()['failover_openrouter'], ['glm-5.3-flash'])
            agent.set_options({'failover': True})                 # says nothing about it: kept
            self.assertEqual(agent.failover_openrouter, ['glm-5.3-flash'])
            agent.set_options({'failover_openrouter': None})
            self.assertEqual(agent.options()['failover_openrouter'], [])
