# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Local tier and the `local` pane role (card #JH22, protocol 13.7).

"local" is a fourth tier beside Main/Flash/Lite. It belongs to no provider: it resolves from the
local-endpoint registry (localmodels.py), and when nothing is set up it falls back to Main directly
with a note rather than stepping down through Lite and Flash.

The registry is isolated with RELAY_LOCAL_MODELS, so nothing here reads the machine's own endpoints;
no key lookup leaves the test and nothing touches the network.
"""
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import localmodels as L
from relay_core import presets as P
from relay_core import roles as model_roles
from relay_core.provider import ProviderConfig
from relay_core.roles import RoleResolver, validate_roles, validate_tiers

BONSAI = {'id': 'bonsai', 'label': 'Bonsai 2 27B', 'base_url': 'http://127.0.0.1:8080/v1',
          'model': 'bonsai-2-27b', 'server': 'llamacpp', 'context_window': 131072}
SMALL = {'id': 'small', 'label': 'Qwen 4B', 'base_url': 'http://localhost:11434/v1',
         'model': 'qwen3:4b', 'server': 'ollama', 'context_window': 32768}


def main_config(key='kimi-key'):
    return ProviderConfig('https://api.moonshot.ai/v1', 'kimi-k3', key, {'reasoning_effort': 'high'}, 8192)


class Case(unittest.TestCase):
    """A registry of its own, and a key store that only knows Kimi."""

    endpoints = (BONSAI, SMALL)

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.registry = str(Path(self.dir.name) / 'local-models.json')
        patch = mock.patch.dict(os.environ, {L.ENV_PATH: self.registry})
        patch.start()
        self.addCleanup(patch.stop)
        for spec in self.endpoints:
            L.save(spec)

    def resolver(self, roles=None, tiers=None, keys=('kimi',)):
        store = {name: f'{name}-key' for name in keys}
        lookup = mock.Mock(side_effect=lambda pid: store.get(pid, ''))
        return RoleResolver(main_config(), 'kimi', validate_roles(roles), key_lookup=lookup,
                            main_effort='high', tiers=validate_tiers(tiers))


class Empty(Case):
    endpoints = ()


# ----- the tier table ------------------------------------------------------------------------
class TierTableTests(unittest.TestCase):
    def test_local_is_a_tier_but_belongs_to_no_provider(self):
        # High was added above Main on 2026-09-20; like Local it has no per-provider row.
        self.assertEqual(P.TIERS, ('high', 'main', 'flash', 'lite', 'local'))
        self.assertEqual(P.PROVIDER_TIERS, ('main', 'flash', 'lite'))
        self.assertEqual(P.TIER_LABELS['local'], 'Local')
        self.assertTrue(P.TIER_HINTS['local'])
        for provider, table in P.TIER_DEFAULTS.items():
            self.assertNotIn('local', table, provider)
            self.assertIsNone(P.tier_default(provider, 'local'), provider)

    def test_local_falls_back_to_main_directly(self):
        # Never local -> lite -> flash -> main: Local is a different trade, not a smaller model.
        self.assertEqual(P.tier_fallbacks('local'), ('local', 'main'))
        self.assertEqual(P.tier_fallbacks('lite'), ('lite', 'flash', 'main'))
        self.assertEqual(P.tier_fallbacks('flash'), ('flash', 'main'))

    def test_the_tier_name_is_accepted_and_typos_are_not(self):
        self.assertEqual(P.validate_tier('local'), 'local')
        with self.assertRaises(ValueError):
            P.validate_tier('locale')


# ----- validation ----------------------------------------------------------------------------
class ValidateTests(Case):
    def test_a_saved_endpoint_is_accepted(self):
        # One object per tier is the form of before 2026-09-20: it is a one-element list now.
        self.assertEqual(validate_tiers({'local': {'preset': 'local:bonsai'}}),
                         {'local': [{'preset': 'local:bonsai'}]})
        self.assertEqual(validate_tiers({'local': [{'preset': 'local:bonsai', 'model': ''}]}),
                         {'local': [{'preset': 'local:bonsai', 'model': ''}]})

    def test_a_loopback_endpoint_is_accepted(self):
        table = validate_tiers({'local': {'base_url': 'http://127.0.0.1:9999/v1', 'model': 'm'}})
        self.assertEqual(table['local'][0]['base_url'], 'http://127.0.0.1:9999/v1')

    def test_a_hosted_preset_is_refused_with_a_sentence(self):
        with self.assertRaises(ValueError) as caught:
            validate_tiers({'local': {'preset': 'kimi'}})
        self.assertIn('model server on this machine', str(caught.exception))

    def test_an_endpoint_that_is_not_on_this_machine_is_refused(self):
        with self.assertRaises(ValueError):
            validate_tiers({'local': {'base_url': 'https://example.invalid/v1', 'model': 'm'}})
        with self.assertRaises(ValueError):
            validate_tiers({'local': {'preset': 'local:missing'}})

    def test_the_other_tiers_still_take_a_hosted_preset(self):
        self.assertEqual(validate_tiers({'flash': {'preset': 'glm'}}), {'flash': [{'preset': 'glm'}]})

    def test_a_local_list_drops_what_is_not_on_this_machine(self):
        """A list never raises (protocol 13.7): the entry that is not local is dropped, the rest stand."""
        table = validate_tiers({'local': [{'preset': 'kimi', 'model': 'kimi-k3'},
                                          {'preset': 'local:missing', 'model': 'm'},
                                          {'preset': 'local:bonsai', 'model': ''}]})
        self.assertEqual(table, {'local': [{'preset': 'local:bonsai', 'model': ''}]})
        self.assertEqual(validate_tiers({'local': [{'preset': 'kimi', 'model': 'kimi-k3'}]}), {})

    def test_a_role_may_follow_the_local_tier(self):
        self.assertEqual(validate_roles({'summaries': {'tier': 'local'}}),
                         {'summaries': {'tier': 'local'}})


# ----- resolution ----------------------------------------------------------------------------
class ResolveTests(Case):
    def test_with_no_override_the_local_tier_is_the_first_saved_endpoint(self):
        resolved = self.resolver().resolve('local')
        self.assertEqual((resolved.config.model, resolved.tier), ('bonsai-2-27b', 'local'))
        self.assertEqual(resolved.preset_id, 'local:bonsai')
        self.assertEqual(resolved.config.api_key, '')       # a local server needs none
        self.assertTrue(resolved.config.local)
        self.assertFalse(resolved.is_main)

    def test_an_override_picks_a_different_endpoint(self):
        resolved = self.resolver(tiers={'local': {'preset': 'local:small'}}).resolve('local')
        self.assertEqual(resolved.config.model, 'qwen3:4b')
        self.assertEqual(resolved.config.base_url, 'http://localhost:11434/v1')

    def test_a_role_pinned_to_the_local_tier_runs_on_the_endpoint(self):
        resolved = self.resolver(roles={'summaries': {'tier': 'local'}}).resolve('summaries')
        self.assertEqual((resolved.config.model, resolved.tier, resolved.source),
                         ('bonsai-2-27b', 'local', 'configured'))

    def test_the_hosted_tiers_are_untouched(self):
        made = self.resolver(keys=('kimi', 'openrouter'))
        self.assertEqual(made.resolve('flash').config.model, 'kimi-k2.7-code-highspeed')
        self.assertEqual(made.resolve('chores').config.model, 'google/gemini-3.8-flash')
        self.assertEqual(made.resolve('main').config.model, 'kimi-k3')

    def test_the_endpoints_own_window_clamps_the_output_limit(self):
        # 32768 asked for, a 32K server: a quarter of the window, as everywhere else (#24XJ).
        resolved = self.resolver(tiers={'local': {'preset': 'local:small'}}).resolve('local')
        self.assertEqual(resolved.config.context_window, 32768)
        self.assertEqual(resolved.config.max_tokens, 8192)

    def test_tier_summary_reports_the_local_row(self):
        summary = self.resolver().tier_summary()
        self.assertEqual(sorted(summary), ['flash', 'high', 'lite', 'local', 'main'])
        self.assertEqual(summary['local']['model'], 'bonsai-2-27b')
        self.assertEqual((summary['local']['label'], summary['local']['source'], summary['local']['using']),
                         ('Local', 'default', 'local'))
        self.assertNotIn('note', summary['local'])

    def test_the_summary_names_the_local_role(self):
        summary = self.resolver().summary()
        self.assertEqual(summary['local']['model'], 'bonsai-2-27b')
        self.assertEqual(summary['local']['label'], 'Local agent')
        self.assertEqual(summary['local']['tier'], 'local')

    def test_no_key_is_ever_looked_up_for_the_local_tier(self):
        made = self.resolver()
        made.resolve('local')
        self.assertNotIn('local:bonsai', [call.args[0] for call in made.key_lookup.mock_calls])


class NoEndpointTests(Empty):
    def test_the_local_tier_falls_back_to_main_with_a_note_and_no_warning(self):
        made = self.resolver()
        resolved = made.resolve('local')
        self.assertTrue(resolved.is_main)
        self.assertEqual(resolved.config.model, 'kimi-k3')
        self.assertEqual(resolved.note, 'No local model is set up; using Main.')
        self.assertIsNone(resolved.warning)
        self.assertEqual(made.warnings, [])          # an expected step, not a misconfiguration

    def test_a_role_on_the_local_tier_falls_back_the_same_way(self):
        resolved = self.resolver(roles={'summaries': {'tier': 'local'}}).resolve('summaries')
        self.assertTrue(resolved.is_main)
        self.assertIn('No local model is set up', resolved.note)

    def test_the_fallback_never_goes_through_lite_or_flash(self):
        # With a Flash model available, a Local tier with nothing set up must still land on Main.
        made = self.resolver(keys=('kimi', 'openrouter'))
        self.assertEqual(made.resolve('local').config.model, 'kimi-k3')

    def test_tier_summary_still_lists_local_with_its_note(self):
        summary = self.resolver().tier_summary()
        self.assertEqual(summary['local']['model'], 'kimi-k3')
        self.assertEqual(summary['local']['using'], 'main')
        self.assertIn('No local model is set up', summary['local']['note'])


# ----- the pane role -------------------------------------------------------------------------
class AgentRoleTests(Case):
    def test_agent_role_local_resolves_to_the_endpoint(self):
        self.assertEqual(model_roles.validate_role('local'), 'local')
        self.assertEqual(model_roles.ROLE_TIERS['local'], 'local')
        resolved = self.resolver().resolve('local')
        self.assertFalse(resolved.is_main)          # worker.py keeps agent_role "local"
        self.assertEqual(resolved.config.model, 'bonsai-2-27b')

    def test_the_event_carries_the_role_and_the_tier(self):
        event = self.resolver().event('local')
        self.assertEqual(event['agent_role'], 'local')
        self.assertEqual(event['roles']['local']['model'], 'bonsai-2-27b')
        self.assertEqual(event['tiers']['local']['model'], 'bonsai-2-27b')
        self.assertEqual(event['warnings'], [])

    def test_the_action_list_names_the_local_row(self):
        actions = {a['role']: a for a in model_roles.action_catalog()}
        self.assertEqual(actions['local']['tier'], 'local')
        self.assertTrue(actions['local']['settable'])
        self.assertIn('/local', actions['local']['label'])

    def test_the_catalog_lists_the_local_tier_last(self):
        catalog = model_roles.tier_catalog()
        # High joined the list above Main on 2026-09-20; Local stays last.
        self.assertEqual([t['id'] for t in catalog['tiers']], ['high', 'main', 'flash', 'lite', 'local'])
        self.assertEqual(catalog['tiers'][-1]['label'], 'Local')
        # The Local tier has no per-provider row; the GUI lists the `presets` rows with local: true.
        for table in catalog['providers'].values():
            self.assertNotIn('local', table)


class AgentRoleWithoutEndpointTests(Empty):
    def test_agent_role_local_reports_main_when_nothing_is_set_up(self):
        # worker.py: `new_role = "main" if resolved.is_main else role`.
        self.assertTrue(self.resolver().resolve('local').is_main)


if __name__ == '__main__':
    unittest.main()
