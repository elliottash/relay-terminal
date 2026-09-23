# SPDX-License-Identifier: AGPL-3.0-or-later
"""The five tier lists (owner, 2026-09-20; protocol 13.7 and 15.2.2).

Options › Models holds five ordered lists — main, high, flash, lite, local — and each entry is a
model plus a reasoning level. A tier resolves to the first entry that can take a call; a failing
turn walks the rest of the list it is on; a model picked by hand, off the list, falls back to the
Main list from the top. The levels are shown in the provider's own words (`effort_labels`), and
the two default fillings of the lists are computed by the backend (`tier_list_defaults`).

Offline throughout: providers are stubs behind a patched ``agent._provider_for``, keys are a dict.
"""
import json
import tempfile
import unittest
from unittest import mock

from relay_core import model_ranking as MR
from relay_core import presets as P
from relay_core.agent import Agent
from relay_core.presets import PRESETS
from relay_core.provider import ProviderConfig, ProviderError
from relay_core.roles import RoleResolver, validate_tiers

GLM = PRESETS['glm']
CONFIG = ProviderConfig(GLM.base_url, GLM.model, 'pane-key', dict(GLM.extra))


def resolver(keys, tiers=None, config=CONFIG, preset_id='glm', roles=None, guests=()):
    """``guests`` are the guest ids whose harness "runs here": the check is injected so no test
    ever finds the real claude or codex on this machine's PATH (protocol 29: no test starts one)."""
    return RoleResolver(config, preset_id, roles or {}, tiers=validate_tiers(tiers),
                        key_lookup=lambda preset: keys.get(preset, ''), main_effort='high',
                        guest_check=lambda guest_id: guest_id in guests)


class Refuser:
    def __init__(self):
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        raise ProviderError('Provider HTTP 503.')


class Answerer:
    def __init__(self, text='from the spare'):
        self.calls, self.text, self.config = 0, text, None

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        return {'role': 'assistant', 'content': self.text}


# ----- validation ----------------------------------------------------------------------------
class ValidationTests(unittest.TestCase):
    def test_every_tier_takes_an_ordered_list_of_model_and_level(self):
        sent = {'main': [{'preset': 'glm-coding', 'model': 'glm-5.3', 'effort': 'high'},
                         {'preset': 'kimi', 'model': 'kimi-k3'}],
                'high': [{'preset': 'openai', 'model': 'gpt-6-astra', 'effort': 'xhigh'}],
                'flash': [{'preset': 'glm-coding', 'model': 'glm-5.3-flash', 'effort': 'low'}],
                'lite': [{'preset': 'openrouter', 'model': 'google/gemini-3.8-flash'}]}
        self.assertEqual(validate_tiers(sent), sent)
        self.assertEqual(list(validate_tiers(sent)['main'][0]), ['preset', 'model', 'effort'])
        # A level is its provider's own word now (card #MDL1, 2026-09-21), and `xhigh` above is
        # the OpenAI API's top. An older client still sends Relay's four; the entry keeps the
        # level that model has for it rather than a word the endpoint would refuse.
        older = {'high': [{'preset': 'openai', 'model': 'gpt-6-astra', 'effort': 'max'}]}
        self.assertEqual(validate_tiers(older)['high'][0]['effort'], 'xhigh')
        self.assertEqual(validate_tiers({'flash': [{'preset': 'gemini', 'model': 'gemini-3.8-flash',
                                                    'effort': 'max'}]})['flash'][0]['effort'], 'high')

    def test_a_list_drops_what_it_cannot_read_and_never_raises(self):
        table = validate_tiers({'flash': ['junk', None, 7, {'model': 'no preset'},
                                          {'preset': 'nobody-knows-this'}, {'preset': 9},
                                          {'preset': 'kimi', 'model': 'kimi-k3', 'effort': 'ludicrous'},
                                          {'preset': 'kimi', 'model': 'kimi-k3', 'effort': 'max'},   # a repeat
                                          {'preset': 'openai', 'model': 'm' * 500},
                                          {'preset': 'glm', 'model': '  glm-5.3-flash ', 'zzz': 1}],
                                'turbo': [{'preset': 'glm'}],              # not a tier: ignored
                                'lite': [], 'high': None})
        self.assertEqual(table, {'flash': [{'preset': 'kimi', 'model': 'kimi-k3'},   # 'ludicrous' is no level of any model: dropped, but not the entry
                                           {'preset': 'glm', 'model': 'glm-5.3-flash'}]})

    def test_the_one_object_form_is_a_one_element_list(self):
        self.assertEqual(validate_tiers({'flash': {'preset': 'glm', 'effort': 'low'}}),
                         {'flash': [{'preset': 'glm', 'effort': 'low'}]})
        made = resolver({'kimi': 'k'}, {'flash': {'preset': 'kimi'}})
        self.assertEqual(made.resolve('flash').config.model, 'kimi-k2.7-code-highspeed')
        with self.assertRaises(ValueError):                 # and it is as strict as it always was
            validate_tiers({'flash': {'preset': 'nope'}})

    def test_a_guest_is_kept_with_its_own_words_for_a_level(self):
        table = validate_tiers({'main': [{'preset': 'guest:codex', 'model': 'gpt-6-astra', 'effort': 'xhigh'},
                                         {'preset': 'guest:', 'model': 'x'}]})
        self.assertEqual(table, {'main': [{'preset': 'guest:codex', 'model': 'gpt-6-astra', 'effort': 'xhigh'}]})


# ----- resolution ----------------------------------------------------------------------------
class ResolutionTests(unittest.TestCase):
    FLASH = {'flash': [{'preset': 'openai', 'model': 'gpt-5.6-terra', 'effort': 'medium'},
                       {'preset': 'kimi', 'model': 'kimi-k2.7-code-highspeed', 'effort': 'high'},
                       {'preset': 'glm', 'model': 'glm-5.3-flash', 'effort': 'max'}]}

    def test_a_tier_is_its_first_usable_entry_at_that_entrys_level(self):
        made = resolver({'openai': 'k', 'kimi': 'k'}, self.FLASH)
        flash = made.resolve('flash')
        self.assertEqual((flash.preset_id, flash.config.model, flash.effort, flash.tier),
                         ('openai', 'gpt-5.6-terra', 'medium', 'flash'))
        self.assertEqual(flash.config.extra['reasoning_effort'], 'medium')
        self.assertIsNone(flash.note)

    def test_entries_without_a_key_are_skipped_and_the_note_says_so(self):
        made = resolver({}, self.FLASH)                     # only the pane's own key
        flash = made.resolve('summaries')
        self.assertEqual((flash.preset_id, flash.config.model, flash.effort), ('glm', 'glm-5.3-flash', 'max'))
        self.assertEqual(flash.config.extra['reasoning_effort'], 'max')
        self.assertIn('first 2 of the flash list', flash.note)   # TIER_LABELS, lower-case (#MDL1 rule 1)

    def test_a_level_is_not_sent_to_a_model_with_no_knob(self):
        made = resolver({'kimi': 'k'}, self.FLASH)
        flash = made.resolve('flash')
        self.assertEqual((flash.config.model, flash.effort), ('kimi-k2.7-code-highspeed', None))
        self.assertNotIn('reasoning_effort', flash.config.extra)

    def test_a_named_model_runs_with_its_own_extras_wherever_it_is_ranked(self):
        made = resolver({}, {'high': [{'preset': 'glm', 'model': 'glm-5.3-flash'}]})
        self.assertEqual(made.resolve('high').config.extra['reasoning_effort'], 'low')

    GUESTED = {tier: [{'preset': 'guest:claude', 'model': 'fable', 'effort': 'max'},
                      {'preset': 'kimi', 'model': 'kimi-k3', 'effort': 'max'}]
               for tier in ('high', 'flash', 'lite')}

    def test_a_guest_serves_a_flash_pane_but_never_a_background_job(self):
        """Owner, 2026-09-21: "the worker should allow the harness for flash, and defaults should
        be the same across plans / apis / harnesses".

        Flash is the one tier that is both a pane and a set of chores. The `flash` role is the
        pane /flash switches to, and a harness can own that conversation from its first turn.
        Every other role on the tier — terminal use, summaries, suggestions — and every role on
        Lite is a side call into a conversation running somewhere else, which a whole agent of
        its own cannot be handed: those skip the guest entry and take the next one.
        """
        made = resolver({'kimi': 'k'}, self.GUESTED, guests=('claude',))
        flash = made.resolve('flash')
        self.assertEqual((flash.preset_id, flash.config.base_url, flash.effort),
                         ('guest:claude', 'harness://claude', 'max'))
        for role in ('terminal_use', 'summaries', 'suggestions', 'chores', 'audit', 'loop_check'):
            with self.subTest(role=role):
                self.assertEqual(made.resolve(role).preset_id, 'kimi')
                self.assertIsNone(made.resolve(role).note)   # a guest is not a missing key
        # A guest that cannot run here is passed over by the pane role too, as it always was.
        away = resolver({'kimi': 'k'}, self.GUESTED, guests=())
        for role in ('flash', 'chores'):
            with self.subTest(role=role, guests=()):
                self.assertEqual(away.resolve(role).preset_id, 'kimi')
        self.assertEqual([e['usable'] for e in away.tier_summary()['flash']['list']], [False, True])
        self.assertEqual([e['usable'] for e in made.tier_summary()['flash']['list']], [True, True])
        # Lite is never a guest's at all, however well the harness runs.
        self.assertEqual([e['usable'] for e in made.tier_summary()['lite']['list']], [False, True])
        alone = resolver({}, {'flash': [{'preset': 'guest:claude', 'model': 'fable'}]}, guests=())
        self.assertTrue(alone.resolve('flash').is_main)

    def test_a_harness_pane_runs_its_background_jobs_on_relay_free(self):
        """Owner, 2026-09-21: "so if somebody just has a harness, the flash chores run on relay
        flash?" — "i agree".

        A pane whose own model is a harness cannot serve a side call from it, so "using main" is
        no answer: with nothing usable in the Flash or Lite list those jobs land on Relay Free's
        role for the tier. The pane's own /flash turn never does — it *is* the harness.
        """
        guest_main = ProviderConfig('harness://claude', 'fable', '', {}, 32_768)
        made = RoleResolver(guest_main, 'guest:claude', key_lookup=lambda p: '',
                            tiers={'flash': [{'preset': 'guest:claude', 'model': 'fable'}]},
                            guest_check=lambda g: True)
        with mock.patch('relay_core.hosted.available', return_value=True):
            chores = made.resolve('chores')
            self.assertEqual((chores.preset_id, chores.config.model), ('relay-free', 'relay-lite'))
            self.assertIn('relay free', chores.note)
            terminal = made.resolve('terminal_use')
            self.assertEqual((terminal.preset_id, terminal.config.model), ('relay-free', 'relay-flash'))
            # The pane's own /flash turn is the harness itself, not Relay Free.
            self.assertEqual(made.resolve('flash').preset_id, 'guest:claude')
        # Without Relay Free there is nothing to fall through to, and it is main as before.
        with mock.patch('relay_core.hosted.available', return_value=False):
            self.assertTrue(RoleResolver(guest_main, 'guest:claude', key_lookup=lambda p: '',
                                         guest_check=lambda g: True).resolve('chores').is_main)

    def test_a_guest_whose_harness_runs_here_serves_the_high_tier(self):
        """Owner, 2026-09-20: "claude and codex weren't showing up under 'high' models" — and
        "for codex planning you pick xhigh". The first usable entry of the High list may be a
        guest: the /high role resolves to it, on the harness scheme, at its level in its own
        words. Since #HR5E a plan turn leaves the pane's own model only for a pin — and a pin
        onto the tier takes the guest exactly as /high does."""
        made = resolver({'kimi': 'k'}, self.GUESTED, guests=('claude',))
        high = made.resolve('high')
        self.assertEqual((high.preset_id, high.config.base_url, high.config.model,
                          high.effort, high.tier, high.source),
                         ('guest:claude', 'harness://claude', 'fable', 'max', 'high', 'default'))
        self.assertEqual(high.config.api_key, '')
        self.assertIsNone(high.note)
        # Unpinned planning is the pane as it is (plan mode puts the pane on /high itself, owner
        # 2026-09-22); a pin onto the tier routes each plan turn to the guest.
        self.assertTrue(made.resolve('planning').is_main)
        pinned = resolver({'kimi': 'k'}, self.GUESTED, roles={'planning': {'tier': 'high'}},
                          guests=('claude',))
        target = pinned.resolve('planning')
        self.assertEqual((target.preset_id, target.config.base_url, target.config.model,
                          target.effort), ('guest:claude', 'harness://claude', 'fable', 'max'))
        self.assertIs(pinned.planning_target(), target)
        # Asked to plan without the guests (the harness would not start): the entry below it.
        without = pinned.planning_target(guests=False)
        self.assertEqual((without.preset_id, without.effort), ('kimi', 'max'))
        # The GUI greys nothing here: the guest is usable in High, and in Main, and nowhere else.
        summary = made.tier_summary()
        self.assertEqual(summary['high']['preset'], 'guest:claude')
        self.assertEqual([e['usable'] for e in summary['high']['list']], [True, True])

    def test_a_guest_that_cannot_run_here_is_skipped_in_high_without_a_key_note(self):
        made = resolver({'kimi': 'k'}, self.GUESTED, guests=())
        high = made.resolve('high')
        self.assertEqual(high.preset_id, 'kimi')
        self.assertIn('guest that cannot run here', high.note)
        # Unpinned planning never reads the list: it is the pane as it is.
        planning = made.resolve('planning')
        self.assertTrue(planning.is_main)
        self.assertIsNone(planning.note)
        summary = made.tier_summary()
        self.assertEqual([e['usable'] for e in summary['high']['list']], [False, True])
        # A High list of nothing but a guest that cannot run: Main, as any list with nothing usable.
        alone = resolver({}, {'high': [{'preset': 'guest:codex', 'effort': 'xhigh'}]})
        self.assertTrue(alone.resolve('high').is_main)
        # Main keeps saying a guest is usable there: it is where a guest serves a pane.
        listed = resolver({}, {'main': [{'preset': 'guest:codex'}]})
        self.assertEqual([e['usable'] for e in listed.tier_summary()['main']['list']], [True])

    def test_empty_lists_change_nothing(self):
        bare, listed = resolver({'openrouter': 'k'}), resolver({'openrouter': 'k'}, {'main': [{'preset': 'kimi'}]})
        for role in ('planning', 'flash', 'chores', 'local', 'subagent'):
            with self.subTest(role=role):
                self.assertEqual(bare.resolve(role).to_dict(), listed.resolve(role).to_dict())
        self.assertTrue(bare.resolve('planning').is_main)
        self.assertEqual(bare.resolve('chores').config.model, 'google/gemini-3.8-flash')

    def test_main_is_the_panes_own_model_whatever_the_main_list_says(self):
        made = resolver({'kimi': 'k'}, {'main': [{'preset': 'kimi', 'model': 'kimi-k3'}]})
        self.assertEqual(made.resolve('subagent').config.model, 'glm-5.3')
        summary = made.tier_summary()['main']
        self.assertEqual((summary['model'], summary['list']),
                         ('glm-5.3', [{'preset': 'kimi', 'model': 'kimi-k3', 'effort': None, 'usable': True}]))
        self.assertIsNone(made.resolve('subagent').note)


# ----- the chain a failing turn walks ---------------------------------------------------------
class ChainTests(unittest.TestCase):
    MAIN = [{'preset': 'kimi', 'model': 'kimi-k3'}, {'preset': 'glm', 'model': 'glm-5.3', 'effort': 'max'},
            {'preset': 'openai', 'model': 'gpt-6-astra'}, {'preset': 'guest:claude', 'model': 'fable'}]

    def test_from_the_entry_after_the_panes_own_model(self):
        made = resolver({}, {'main': self.MAIN})
        self.assertEqual(made.failover_chain('main', 'glm', 'glm-5.3'), self.MAIN[2:])

    def test_from_the_top_for_a_model_picked_by_hand(self):
        made = resolver({}, {'main': self.MAIN})
        self.assertEqual(made.failover_chain('main', 'anthropic', 'claude-opus-5-5'), self.MAIN)
        self.assertEqual(made.failover_chain('main', 'glm', 'glm-5.3-flash'), self.MAIN)   # same preset, another model

    def test_fallbacks_alone_is_the_main_chain_and_the_main_list_wins(self):
        older = [{'preset': 'openai', 'model': 'gpt-6-mini'}]
        self.assertEqual(resolver({}).failover_chain('main', 'glm', 'glm-5.3', older), older)
        self.assertEqual(resolver({}, {'main': self.MAIN}).failover_chain('main', 'glm', 'glm-5.3', older),
                         self.MAIN[2:])

    def test_each_tier_walks_its_own_list(self):
        made = resolver({}, {'main': self.MAIN,
                             'high': [{'preset': 'openai', 'model': 'gpt-6-astra', 'effort': 'max'},
                                      {'preset': 'kimi', 'model': 'kimi-k3', 'effort': 'max'}],
                             'flash': [{'preset': 'glm', 'model': 'glm-5.3-flash'}]})
        self.assertEqual([e['preset'] for e in made.failover_chain('high', 'openai', 'gpt-6-astra')], ['kimi'])
        self.assertEqual(made.failover_chain('flash', 'glm', 'glm-5.3-flash'), [])   # then the turn fails
        self.assertEqual(made.failover_chain('lite', 'openrouter', 'x'), self.MAIN)  # no Lite list: Main's
        self.assertEqual(resolver({}).failover_chain('high', 'glm', 'glm-5.3'), [])  # High's default has no chain
        self.assertEqual(made.turn_tier('glm', 'glm-5.3-flash'), 'flash')
        self.assertEqual(made.turn_tier('glm', 'glm-5.3'), 'main')
        self.assertEqual(made.turn_tier('anthropic', 'claude-opus-5-5', 'main'), 'main')

    def test_a_candidate_runs_at_its_entrys_level_and_a_guest_is_never_one(self):
        made = resolver({'kimi': 'k', 'openai': 'k'})
        spare = made.fallback_candidate({'preset': 'openai', 'model': 'gpt-6-astra', 'effort': 'low'}, 'main', {'glm'})
        self.assertEqual((spare.effort, spare.config.extra['reasoning_effort']), ('low', 'low'))
        plain = made.fallback_candidate({'preset': 'openai', 'model': 'gpt-6-astra'}, 'main', {'glm'})
        self.assertEqual((plain.effort, plain.config.extra['reasoning_effort']), (None, 'high'))
        high = made.fallback_candidate({'preset': 'openai', 'model': 'gpt-6-astra'}, 'high', {'glm'})
        # No level implied on High either: the entry means the same first or reached by a failover.
        self.assertEqual((high.effort, high.config.extra['reasoning_effort']), (None, 'high'))
        self.assertIsNone(made.fallback_candidate({'preset': 'guest:claude', 'model': 'fable'}, 'main', {'glm'}))
        # Nor on High, even when its harness runs here: a turn under way is never moved onto one.
        runs = resolver({'kimi': 'k'}, guests=('claude',))
        self.assertIsNone(runs.fallback_candidate({'preset': 'guest:claude', 'model': 'fable'}, 'high', {'glm'}))

    def test_a_guest_entry_matches_the_guest_whatever_model_the_cli_reported(self):
        # A plan turn on a guest walks the High list from the entry after the guest: the entry
        # said "fable", the CLI reported claude-fable-5-1 once started.
        high = [{'preset': 'guest:claude', 'model': 'fable'}, {'preset': 'kimi', 'model': 'kimi-k3'}]
        made = resolver({'kimi': 'k'}, {'high': high}, guests=('claude',))
        self.assertEqual(made.failover_chain('high', 'guest:claude', 'claude-fable-5-1'), [high[1]])
        self.assertEqual(made.failover_chain('high', 'guest:claude', ''), [high[1]])


# ----- turns ----------------------------------------------------------------------------------
class TurnCase(unittest.TestCase):
    MAIN = [{'preset': 'kimi', 'model': 'kimi-k3'}, {'preset': 'glm', 'model': 'glm-5.3'},
            {'preset': 'guest:codex', 'model': ''}, {'preset': 'anthropic', 'model': 'claude-opus-5-5'},
            {'preset': 'openai', 'model': 'gpt-6-astra', 'effort': 'low'}]

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.events: list = []
        self.stubs: dict = {}
        self.built: list = []

        def provider_for(config, stall):
            self.built.append(config)
            self.stubs[config.model].config = config       # as a real transport keeps the one it was built with
            return self.stubs[config.model]
        patcher = mock.patch('relay_core.agent._provider_for', side_effect=provider_for)
        patcher.start()
        self.addCleanup(patcher.stop)

    def agent(self, roles, config=CONFIG, preset_id='glm', **extra):
        return Agent(config, self.temp.name, self.events.append, preset_id=preset_id, roles=roles,
                     effort='high', track_requests=False, todo_tool=False, completion_check=False, **extra)

    def moves(self):
        return [(e['from_model'], e['to_model']) for e in self.events
                if e['event'] == 'provider_retry' and e['reason'] == 'failover']


class TurnTests(TurnCase):
    def test_a_main_turn_walks_the_main_list_from_the_entry_after_its_own(self):
        """Kimi is ranked above the pane's model and is never asked; the guest and the entry with
        no key are skipped; the spare runs at its entry's level, and the pane's comes back."""
        self.stubs.update({'glm-5.3': Refuser(), 'kimi-k3': Answerer('not me'), 'gpt-6-astra': Answerer()})
        agent = self.agent(resolver({'kimi': 'k', 'openai': 'k'}, {'main': self.MAIN}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(self.moves(), [('glm-5.3', 'gpt-6-astra')])
        self.assertEqual(self.stubs['kimi-k3'].calls, 0)
        spare = next(c for c in self.built if c.model == 'gpt-6-astra')
        self.assertEqual(spare.extra['reasoning_effort'], 'low')
        self.assertEqual((agent.config.model, agent.effort), ('glm-5.3', 'high'))

    def test_a_model_picked_by_hand_falls_back_to_the_main_list_from_the_top(self):
        config = ProviderConfig(PRESETS['anthropic'].base_url, 'claude-fable-5-1', 'pane-key')
        self.stubs.update({'claude-fable-5-1': Refuser(), 'kimi-k3': Answerer()})
        keys = {'kimi': 'k', 'openai': 'k', 'anthropic': 'k'}
        agent = self.agent(resolver(keys, {'main': self.MAIN}, config, 'anthropic'), config, 'anthropic')
        agent.ask('hello')
        self.assertEqual(self.moves(), [('claude-fable-5-1', 'kimi-k3')])

    def test_the_list_runs_out_and_the_turn_fails(self):
        self.stubs.update({'glm-5.3': Refuser(), 'gpt-6-astra': Refuser()})
        agent = self.agent(resolver({'openai': 'k'}, {'main': self.MAIN}))
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.moves(), [('glm-5.3', 'gpt-6-astra')])

    def test_fallbacks_alone_still_works(self):
        self.stubs.update({'glm-5.3': Refuser(), 'kimi-k3': Answerer()})
        agent = self.agent(resolver({'kimi': 'k'}), fallbacks=[{'preset': 'kimi', 'model': ''}])
        agent.ask('hello')
        self.assertEqual((self.events[-1]['event'], self.moves()), ('done', [('glm-5.3', 'kimi-k3')]))

    def test_a_flash_pane_walks_the_flash_list(self):
        config = ProviderConfig(GLM.base_url, 'glm-5.3-flash', 'pane-key')
        tiers = {'main': self.MAIN, 'flash': [{'preset': 'glm', 'model': 'glm-5.3-flash'},
                                              {'preset': 'openai', 'model': 'gpt-5.6-terra'}]}
        self.stubs.update({'glm-5.3-flash': Refuser(), 'gpt-5.6-terra': Answerer(), 'kimi-k3': Answerer('main list')})
        agent = self.agent(resolver({'kimi': 'k', 'openai': 'k'}, tiers, config), config)
        agent.ask('hello')
        self.assertEqual(self.moves(), [('glm-5.3-flash', 'gpt-5.6-terra')])

    HIGH = {'high': [{'preset': 'openai', 'model': 'gpt-6-astra', 'effort': 'max'},
                     {'preset': 'guest:claude', 'model': 'fable', 'effort': 'max'},
                     {'preset': 'anthropic', 'model': 'claude-opus-5-5'},       # no key
                     {'preset': 'kimi', 'model': 'kimi-k3', 'effort': 'max'}],
            'main': [{'preset': 'glm', 'model': 'glm-5.3'}, {'preset': 'minimax', 'model': 'MiniMax-M3'}]}

    def test_a_plan_turn_walks_the_high_list(self):
        # Planning pinned onto the High tier (since #HR5E the pin is what takes a plan turn off
        # the pane's own model): a failing entry fails over down the list, as before.
        self.stubs.update({'gpt-6-astra': Refuser(), 'kimi-k3': Answerer('the plan'), 'glm-5.3': Answerer('own')})
        agent = self.agent(resolver({'openai': 'k', 'kimi': 'k', 'minimax': 'k'}, self.HIGH,
                                    roles={'planning': {'tier': 'high'}}))
        agent.set_mode('plan')
        agent.ask('plan this')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(self.moves(), [('gpt-6-astra', 'kimi-k3')])
        retry = next(e for e in self.events if e['event'] == 'provider_retry')
        self.assertEqual((retry['tier'], retry['attempt'], retry['max_attempts']), ('high', 1, 3))
        self.assertIn('plan turn', retry['text'])
        planned = next(c for c in self.built if c.model == 'kimi-k3')
        self.assertEqual(planned.extra['reasoning_effort'], 'max')
        self.assertEqual(self.stubs['glm-5.3'].calls, 0)          # the pane's own model was not needed
        # Still a plan turn: it ends the way one does, and the pane is back on its own model.
        ended = next(e for e in self.events if e['event'] == 'plan_route_ended')
        self.assertEqual((ended['model'], ended['was']), ('glm-5.3', 'kimi-k3'))
        self.assertEqual((agent.config.model, agent.effort), ('glm-5.3', 'high'))

    def test_a_spent_high_list_returns_the_turn_to_the_panes_own_model(self):
        self.stubs.update({'gpt-6-astra': Refuser(), 'kimi-k3': Refuser(), 'glm-5.3': Answerer('own')})
        agent = self.agent(resolver({'openai': 'k', 'kimi': 'k'}, self.HIGH,
                                    roles={'planning': {'tier': 'high'}}))
        agent.set_mode('plan')
        agent.ask('plan this')
        self.assertEqual(self.events[-1]['event'], 'done')
        reasons = [e['reason'] for e in self.events if e['event'] == 'provider_retry']
        self.assertEqual(reasons, ['failover', 'route_dropped'])
        self.assertEqual(self.stubs['glm-5.3'].calls, 1)

    def test_a_side_call_walks_its_tiers_list(self):
        tiers = {'lite': [{'preset': 'openrouter', 'model': 'google/gemini-3.8-flash'},
                          {'preset': 'guest:claude', 'model': 'haiku'},
                          {'preset': 'openai', 'model': 'gpt-6-luna', 'effort': 'low'}]}
        self.stubs.update({'glm-5.3': Answerer(), 'google/gemini-3.8-flash': Refuser(),
                           'gpt-6-luna': Answerer('a title')})
        agent = self.agent(resolver({'openrouter': 'k', 'openai': 'k'}, tiers))
        provider = agent.side_provider(cheap=True, role='chores', max_tokens=64)
        self.assertEqual(provider.config.model, 'google/gemini-3.8-flash')
        reply = provider.complete([], [], lambda event: None, mock.Mock(is_set=lambda: False))
        self.assertEqual(reply['content'], 'a title')
        self.assertEqual(provider.config.model, 'gpt-6-luna')
        self.assertEqual(provider.config.max_tokens, 64)                  # the spare keeps the call's budget

    def test_a_side_call_with_nowhere_to_go_fails_with_its_own_models_error(self):
        tiers = {'lite': [{'preset': 'openrouter', 'model': 'google/gemini-3.8-flash'},
                          {'preset': 'openai', 'model': 'gpt-6-luna'}]}
        self.stubs.update({'glm-5.3': Answerer(), 'google/gemini-3.8-flash': Refuser(), 'gpt-6-luna': Refuser()})
        provider = self.agent(resolver({'openrouter': 'k', 'openai': 'k'}, tiers)).side_provider(role='chores')
        with self.assertRaises(ProviderError):
            provider.complete([], [], lambda event: None, mock.Mock(is_set=lambda: False))
        self.assertEqual(self.stubs['gpt-6-luna'].calls, 1)
        # One entry, or no list: the provider a side call always got, not a chain.
        bare = self.agent(resolver({'openrouter': 'k'})).side_provider(role='chores')
        self.assertIs(bare, self.stubs['google/gemini-3.8-flash'])


class SubagentTests(TurnCase):
    def test_a_subagent_inherits_the_lists(self):
        """The lists live in the resolver the pane hands its subagents, so there is nothing to
        copy: a subagent on the pane's model walks the same Main list from the same place."""
        from types import SimpleNamespace
        from relay_core.agents_defs import load_catalog
        from relay_core.subagents import SubagentFactory
        self.stubs.update({'glm-5.3': Refuser(), 'kimi-k3': Answerer('not me'), 'gpt-6-astra': Answerer()})
        keys = {'kimi': 'k', 'openai': 'k'}
        factory = SubagentFactory(CONFIG, self.temp.name, preset_id='glm', key_lookup=lambda p: keys.get(p, ''),
                                  roles=resolver(keys, {'main': self.MAIN}),
                                  main_agent=SimpleNamespace(failover=True, fallbacks=[]))
        sub, _label, _warnings = factory(load_catalog(self.temp.name, []).get('general'), None, None,
                                         self.events.append, 'a1')
        sub.ask('go')
        self.assertEqual((self.events[-1]['event'], self.moves()), ('done', [('glm-5.3', 'gpt-6-astra')]))


# ----- a level is the provider's own word --------------------------------------------------------
class EffortLevelTests(unittest.TestCase):
    """`effort_labels` is retired (card #MDL1, 2026-09-21). There is nothing to label: the word
    the picker offers is the word that is sent, so a row's `efforts` is the whole answer and
    `effort_fixed` says whether the box may be moved at all."""

    def test_no_row_carries_a_second_table_of_words(self):
        with mock.patch('relay_core.openrouter_catalog.rows', return_value=[]):
            for preset in PRESETS.values():
                row = preset.to_dict()
                self.assertNotIn('effort_labels', row, preset.id)
                self.assertEqual(row['efforts'], P.effort_levels(preset.effort_style), preset.id)
                for model in row['models']:
                    with self.subTest(preset=preset.id, model=model['id']):
                        self.assertNotIn('effort_labels', model)
                        self.assertIn('effort_fixed', model)
        self.assertFalse(hasattr(P, 'effort_labels'))
        self.assertFalse(hasattr(P, 'EFFORT_MAP'))

    def test_a_row_offers_exactly_what_its_endpoint_takes(self):
        models = {m['id']: m for m in P.catalog_rows('openai')}
        self.assertEqual(models['gpt-6-astra']['efforts'], ['low', 'medium', 'high', 'xhigh'])
        self.assertFalse(models['gpt-6-astra']['effort_fixed'])
        models = {m['id']: m for m in P.catalog_rows('kimi')}
        self.assertEqual(models['kimi-k3']['efforts'], ['low', 'high', 'max'])
        self.assertEqual(models['kimi-k2.7-code-highspeed']['efforts'], [])
        self.assertTrue(models['kimi-k2.7-code-highspeed']['effort_fixed'])
        # Relay Free offers two and greys the box anyway: the gateway clamps each role.
        for model in P.catalog_rows('relay-free'):
            self.assertEqual(model['efforts'], ['low', 'medium'])
            self.assertTrue(model['effort_fixed'])


# ----- the two defaults -------------------------------------------------------------------------
LISTING = [{'id': 'z-ai/glm-5.3', 'efforts': ['low'], 'price_completion_per_mtok': 2.86},
           {'id': 'z-ai/glm-5.3-flash', 'efforts': ['low'], 'price_completion_per_mtok': 0.3},
           {'id': 'moonshotai/kimi-k3', 'efforts': ['low'], 'price_completion_per_mtok': 8.5},
           {'id': 'openai/gpt-6-astra', 'efforts': ['low'], 'price_completion_per_mtok': 50.0},
           {'id': 'openai/gpt-5.6-terra', 'efforts': ['low'], 'price_completion_per_mtok': 12.0},
           {'id': 'openai/gpt-6-luna', 'efforts': [], 'price_completion_per_mtok': 1.2},
           {'id': 'minimax/minimax-m3', 'efforts': [], 'price_completion_per_mtok': 3.0}]
GUESTS = [{'id': 'guest:claude', 'harness': True, 'logged_in': None, 'guest': 'claude',
           'efforts': ['low', 'medium', 'high', 'xhigh', 'max'],
           'models': [{'id': 'fable', 'default_effort': None}, {'id': 'opus'}]},
          {'id': 'guest:codex', 'harness': True, 'logged_in': False, 'models': []},      # signed out
          {'id': 'guest:other', 'harness': False, 'logged_in': True, 'models': []}]     # no adapter here
CODEX = {'id': 'guest:codex', 'harness': True, 'logged_in': True, 'guest': 'codex',
         'efforts': ['low', 'medium', 'high', 'xhigh', 'max', 'ultra'],
         'models': [{'id': 'gpt-5.5-codex', 'efforts': ['low', 'medium', 'high', 'xhigh'],
                     'default_effort': 'medium'}]}


def pairs(entries):
    return [(e['preset'], e['model'], e.get('effort')) for e in entries]


class DefaultsTests(unittest.TestCase):
    USABLE = ['openai', 'kimi', 'glm-coding', 'minimax', 'relay-free']

    def defaults(self, usable, **kwargs):
        kwargs.setdefault('listing', LISTING)
        return P.tier_list_defaults(usable, **kwargs)

    def test_plain(self):
        # Six providers here (openai, kimi, z.ai, minimax, Claude Code, the custom one), so two per
        # class — the rules themselves are tested in tests/test_model_ranking.py; this is the shape
        # the `presets` event carries. Relay Free is in `usable` and appears nowhere: it is what
        # "no providers" means, never one of them (card #MDL1, design 5.4).
        plain = self.defaults(self.USABLE, guests=GUESTS, local=[('local:bonsai', 'bonsai-2-27b')],
                              custom=[('custom:acme', 'acme-1')])['plain']
        # By score (model-ranking.md), one per provider: claude-opus-5-5 51 — which is what the
        # guest's `opus` is, so it is ranked by name like anyone else's model — then gpt-6-sol 47
        # (the owner, 2026-09-22: astra is openai's high, sol its main). opus starts at its Levels
        # cell, `main = high`; sol has no row, so at the provider's own default, openai's `high`.
        self.assertEqual(pairs(plain['main']),
                         [('guest:claude', 'opus', 'high'), ('openai', 'gpt-6-sol', 'high')])
        # And the file's `high` cells, each in the vocabulary of the provider that will run it.
        # High is Claude Code's `fable` (claude-fable-5.1, 53, which the owner classed for high on
        # 2026-09-21) ahead of gpt-6-astra on the same score, because the harnesses sort first in
        # his Providers table — and at `high`, not `xhigh`, because that is the cell he wrote.
        self.assertEqual(pairs(plain['high']),
                         [('guest:claude', 'fable', 'high'), ('openai', 'gpt-6-astra', 'xhigh')])
        # Flash takes its level from the file too — the owner wrote `glm-5.3-flash | flash =
        # high`, which is not the "lowest level" rule that applies where a cell is blank — and
        # leaves it out for a model with no knob at all. Nobody's flash model is scored, so the
        # tie goes to the provider order: the coding plan, then minimax.
        self.assertEqual(pairs(plain['flash']),
                         [('glm-coding', 'glm-5.3-flash', 'high'),
                          ('minimax', 'MiniMax-M2.7-highspeed', None)])
        # Lite is Relay Free and nothing else, however many keys are stored (owner, 2026-09-21:
        # "for lite ... everybody is on relay free by default, or openrouter if they want
        # privacy"). It is the one place Relay Free appears beside other providers.
        self.assertEqual(pairs(plain['lite']), [('relay-free', 'relay-lite', 'low')])
        self.assertEqual(pairs(plain['local']), [('local:bonsai', 'bonsai-2-27b', None)])
        # Every entry is one `tiers` takes back unchanged.
        with mock.patch('relay_core.roles._preset', side_effect=lambda p: PRESETS.get(p) or mock.Mock()), \
                mock.patch('relay_core.roles._is_local_endpoint', return_value=True):
            self.assertEqual(validate_tiers(plain), {t: e for t, e in plain.items() if e})
        json.dumps(plain)

    def test_a_harness_may_be_a_flash_default_but_never_a_lite_one(self):
        """Owner, 2026-09-21: "the worker should allow the harness for flash, and defaults should
        be the same across plans / apis / harnesses". Lite is nothing but background jobs, which a
        harness cannot serve, so no guest is ranked into it."""
        self.assertEqual(P.GUEST_CLASSES, ('high', 'main', 'flash'))
        with_sonnet = [dict(GUESTS[0], models=[{'id': 'sonnet'}])]
        plain = self.defaults(['relay-free'], guests=with_sonnet)['plain']
        # claude-sonnet-6 is the file's flash model for anthropic, and the guest serves it here.
        self.assertEqual(pairs(plain['flash']), [('guest:claude', 'sonnet', 'low')])
        self.assertEqual(pairs(plain['lite']), [('relay-free', 'relay-lite', 'low')])

    def test_codex_plans_at_xhigh_when_its_model_offers_it_else_at_its_last_level(self):
        # Owner, 2026-09-20: "for codex planning you pick xhigh, not max". Two providers here (the
        # coding plan and codex), so one model each: codex's own catalogue names a model
        # model-ranking.md has never heard of, and an unscored guest is still the provider it is —
        # it falls back to the first model its list names, as it always did.
        WITH_CODEX = ['glm-coding']
        high = pairs(self.defaults(WITH_CODEX, guests=[CODEX])['plain']['high'])
        self.assertEqual(high, [('glm-coding', 'glm-5.3', 'max'),
                                ('guest:codex', 'gpt-5.5-codex', 'xhigh')])
        main = pairs(self.defaults(WITH_CODEX, guests=[CODEX])['plain']['main'])
        self.assertIn(('guest:codex', 'gpt-5.5-codex', 'medium'), main)      # Main: its own default
        capped = dict(CODEX, models=[{'id': 'gpt-5.5-mini', 'efforts': ['low', 'medium', 'high']}])
        high = pairs(self.defaults(WITH_CODEX, guests=[capped])['plain']['high'])
        self.assertIn(('guest:codex', 'gpt-5.5-mini', 'high'), high)
        # A guest whose model names no levels falls back to the row's; none at all means no level.
        bare = dict(CODEX, models=[{'id': 'gpt-5.5-codex'}])
        self.assertIn(('guest:codex', 'gpt-5.5-codex', 'xhigh'), pairs(self.defaults(WITH_CODEX, guests=[bare])['plain']['high']))
        none = dict(CODEX, efforts=[], models=[{'id': 'gpt-5.5-codex'}])
        self.assertIn(('guest:codex', 'gpt-5.5-codex', None), pairs(self.defaults(WITH_CODEX, guests=[none])['plain']['high']))
        # ... and a signed-out or harness-less guest is in neither list, and is not a provider.
        both = self.defaults(self.USABLE, guests=GUESTS)['plain']
        self.assertNotIn('guest:codex', [e['preset'] for e in both['main'] + both['high']])
        self.assertNotIn('guest:other', [e['preset'] for e in both['main'] + both['high']])

    def test_without_an_openrouter_key_the_two_are_the_same(self):
        both = self.defaults([p for p in self.TWINS if p != 'openrouter'], guests=GUESTS)
        self.assertEqual(both['openrouter'], both['plain'])

    # Four providers whose twins the LISTING above prices, so every branch of the cost rule is
    # exercised: since card #MDL1 each plain list holds two models, and the twins follow them.
    TWINS = ['glm-coding', 'minimax', 'openai', 'openrouter']

    def test_openrouter_appends_the_cheap_twins_after_everything(self):
        both = self.defaults(self.TWINS)
        plain, routed = both['plain'], both['openrouter']
        self.assertEqual(P.OPENROUTER_TWIN_MAX_COMPLETION_USD_PER_MTOK, 3.0)
        for tier in ('main', 'high', 'flash'):
            self.assertEqual(routed[tier][:len(plain[tier])], plain[tier], tier)     # after ALL of them
        # $2.86 is in; GPT-6 ($50) is not, so main's two models yield one twin.
        self.assertEqual(pairs(plain['main']),
                         [('openai', 'gpt-6-sol', 'high'), ('glm-coding', 'glm-5.3', 'high')])
        self.assertEqual(pairs(routed['main'][len(plain['main']):]),
                         [('openrouter', 'z-ai/glm-5.3', None)])
        # High runs a twin at max too, unless the listing says the model takes no level.
        self.assertEqual(pairs(routed['high'][len(plain['high']):]),
                         [('openrouter', 'z-ai/glm-5.3', 'max')])
        # Flash: glm's is cheap; minimax m2.7 has no price here, and an unknown price is kept in
        # Flash and Lite (and was left out of Main and High above). A Flash or Lite twin says
        # "low" like the rest of its list.
        self.assertEqual(pairs(routed['flash'][len(plain['flash']):]),
                         [('openrouter', 'z-ai/glm-5.3-flash', 'low'),
                          ('openrouter', 'minimax/minimax-m2.7', 'low')])
        # Lite starts with OpenRouter's **own** lite pick, which is `model-ranking.md`'s Provider
        # picks row rather than a constant in the code (`LITE_LIST_FIRST` is retired), at low.
        self.assertEqual(pairs(routed['lite'])[0], ('openrouter', 'google/gemini-3.5-flash-lite', 'low'))
        self.assertEqual(P.provider_model_id('openrouter', MR.load().pick('openrouter', 'lite')),
                         'google/gemini-3.5-flash-lite')
        self.assertFalse(hasattr(P, 'LITE_LIST_FIRST'))
        self.assertEqual([e for e in pairs(routed['lite']) if e[1] == 'google/gemini-3.5-flash-lite'],
                         [('openrouter', 'google/gemini-3.5-flash-lite', 'low')])
        self.assertNotIn('google/gemini-3.8-flash', [e[1] for e in pairs(routed['lite'])])

    def test_an_unknown_price_keeps_a_twin_out_of_main_and_high(self):
        routed = self.defaults(['glm-coding', 'openrouter'], listing=[])['openrouter']
        self.assertNotIn('z-ai/glm-5.3', [e['model'] for e in routed['main'] + routed['high']])
        self.assertIn('z-ai/glm-5.3-flash', [e['model'] for e in routed['flash']])

    def test_the_presets_event_carries_them(self):
        from pathlib import Path
        source = (Path(__file__).resolve().parent.parent / 'backend' / 'worker.py').read_text()
        self.assertIn('"tier_list_defaults": list_defaults', source)


# ----- where a hand-added model starts (card #TKN7) ---------------------------------------------
# Options › Models' `+ add a model…` used to start every new entry at the model's top level, so
# gpt-6-sol went into Main at codex's `ultra` (owner report, 2026-09-21). One rule decides it
# now, `tier_start_efforts`, and every `models` row carries its answer as `tier_effort`.
class StartEffortTests(unittest.TestCase):
    CODEX_LEVELS = ['low', 'medium', 'high', 'xhigh', 'max', 'ultra']

    def test_main_is_the_providers_own_default_not_the_top_level(self):
        self.assertEqual(P.tier_start_efforts(self.CODEX_LEVELS, 'low', 'codex'),
                         {'main': 'low', 'high': 'xhigh', 'flash': 'low', 'lite': 'low'})
        self.assertEqual(P.tier_start_efforts(self.CODEX_LEVELS, 'medium', 'codex')['main'], 'medium')

    def test_high_is_the_top_level_and_a_codex_model_takes_xhigh(self):
        # Claude Code has no `xhigh`: its top level is the High list's answer, as for a cloud model.
        claude = ['low', 'medium', 'high', 'max']
        self.assertEqual(P.tier_start_efforts(claude, 'medium', 'claude')['high'], 'max')
        self.assertEqual(P.tier_start_efforts(['low', 'high', 'max'], 'high')['high'], 'max')
        # A codex model without `xhigh` (gpt-5.5) still takes its own top level.
        self.assertEqual(P.tier_start_efforts(['low', 'medium', 'high'], 'medium', 'codex')['high'], 'high')

    def test_flash_and_lite_are_the_lowest_level_and_a_model_with_no_knob_starts_empty(self):
        self.assertEqual(P.tier_start_efforts(['low', 'high', 'max'], 'high'),
                         {'main': 'high', 'high': 'max', 'flash': 'low', 'lite': 'low'})
        self.assertEqual(P.tier_start_efforts([], None),
                         {'main': None, 'high': None, 'flash': None, 'lite': None})

    def test_a_default_the_model_does_not_offer_is_no_level_at_all(self):
        # Never a level the model would refuse: the entry then carries none and the model's own
        # default applies at run time.
        self.assertIsNone(P.tier_start_efforts(['low', 'high'], 'max')['main'])

    def test_every_cloud_row_carries_both_keys(self):
        with mock.patch('relay_core.openrouter_catalog.rows', return_value=[]):
            for preset_id in P.PRESETS:
                for row in P.catalog_rows(preset_id):
                    with self.subTest(preset=preset_id, model=row['id']):
                        self.assertEqual(set(row['tier_effort']), {'main', 'high', 'flash', 'lite'})
                        self.assertEqual(row['tier_effort'],
                                         P.tier_start_efforts(row['efforts'], row['default_effort'],
                                                              '', row['name']))
                        # A level named is a level the model offers; None is None.
                        for level in row['tier_effort'].values():
                            self.assertIn(level, [None, *row['efforts']])
        rows = {r['id']: r for r in P.catalog_rows('openai')}
        # A model no tier names runs at the preset's own extras: terra, since luna took flash.
        self.assertEqual(rows['gpt-5.6-terra']['default_effort'], 'high')
        self.assertEqual(rows['gpt-6-sol']['default_effort'], 'high')
        self.assertEqual(rows['gpt-6-luna']['default_effort'], 'low')
        # Anthropic's models carry no levels at all: no default, no tier level.
        self.assertEqual({r['default_effort'] for r in P.catalog_rows('anthropic')}, {None})


if __name__ == '__main__':
    unittest.main()
