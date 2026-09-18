# SPDX-License-Identifier: GPL-3.0-or-later
"""A model server on this machine has no key: configure, the role table, the Test button and the
`presets` event (card #24XJ).

The rule under test is one sentence. Keyless means plain HTTP to a loopback host, never "no key was
found": an https endpoint without a key fails exactly as it always did.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import keytest, localmodels as L, session_protocol as S
from relay_core.presets import PRESETS
from relay_core.provider import ProviderConfig
from relay_core.roles import RoleResolver, validate_roles, validate_tiers

ROOT = Path(__file__).resolve().parents[1]
BONSAI = {'id': 'bonsai', 'label': 'Bonsai 2 27B', 'base_url': 'http://127.0.0.1:8080/v1',
          'model': 'bonsai-2-27b', 'server': 'llamacpp', 'context_window': 131072}
SMALL = {'id': 'small', 'base_url': 'http://localhost:11434/v1', 'model': 'qwen3:4b', 'server': 'ollama',
         'context_window': 32768, 'first_token_timeout': 120, 'tool_text_recovery': True}


class Case(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.registry = str(Path(self.dir.name) / 'local-models.json')
        patch = mock.patch.dict(os.environ, {L.ENV_PATH: self.registry})
        patch.start()
        self.addCleanup(patch.stop)
        L.save(BONSAI); L.save(SMALL)
        self.looked_up = []
        real = S.keystore.lookup
        S.keystore.lookup = self.lookup
        self.addCleanup(setattr, S.keystore, 'lookup', real)

    def lookup(self, preset_id):
        self.looked_up.append(preset_id)
        return {'kimi': 'moonshot-key'}.get(preset_id, '')


class ConfigureTests(Case):
    def test_a_local_endpoint_configures_with_no_key_and_no_keyring_lookup(self):
        config = S.provider_config({'preset': 'local:bonsai', 'use_stored_key': True})
        self.assertEqual((config.base_url, config.model, config.api_key), ('http://127.0.0.1:8080/v1', 'bonsai-2-27b', ''))
        self.assertEqual((config.local, config.first_token_timeout, config.context_window), (True, 300.0, 131072))
        self.assertEqual((config.parallel_tool_calls, config.tool_text_recovery), (False, False))
        self.assertEqual(self.looked_up, [])

    def test_the_endpoints_own_settings_travel_with_it(self):
        config = S.provider_config({'preset': 'local:small', 'use_stored_key': True})
        self.assertEqual((config.first_token_timeout, config.tool_text_recovery), (120.0, True))

    def test_the_output_limit_fits_the_served_window(self):
        self.assertEqual(S.provider_config({'preset': 'local:small', 'use_stored_key': True}).max_tokens, 8192)
        self.assertEqual(S.provider_config({'preset': 'local:small', 'max_tokens': 2048}).max_tokens, 2048)
        self.assertEqual(S.provider_config({'preset': 'local:bonsai', 'use_stored_key': True}).max_tokens, 32768)
        self.assertEqual(L.clamp_max_tokens(32768, {}), 32768)                 # a hosted provider: untouched

    def test_an_unsaved_loopback_url_is_local_too(self):
        config = S.provider_config({'preset': 'custom', 'base_url': 'http://localhost:1234/v1', 'model': 'x',
                                    'use_stored_key': True})
        self.assertEqual((config.local, config.api_key, config.first_token_timeout), (True, '', 300.0))
        self.assertEqual(self.looked_up, [])

    def test_a_key_is_never_sent_to_a_local_server_even_when_one_is_given(self):
        config = S.provider_config({'preset': 'local:bonsai', 'api_key': 'sk-pasted-by-mistake'})
        self.assertEqual(config.api_key, '')

    def test_an_https_endpoint_without_a_key_fails_as_it_always_did(self):
        with self.assertRaises(ValueError) as caught:
            S.provider_config({'preset': 'custom', 'base_url': 'https://127.0.0.1:8443/v1', 'model': 'x', 'use_stored_key': True})
        self.assertIn('No stored key', str(caught.exception))
        with self.assertRaises(ValueError):
            S.provider_config({'preset': 'openai', 'use_stored_key': True})
        self.assertFalse(S.provider_config({'preset': 'kimi', 'use_stored_key': True}).local)

    def test_an_endpoint_that_was_removed_says_so(self):
        with self.assertRaises(ValueError) as caught:
            S.provider_config({'preset': 'local:gone', 'use_stored_key': True})
        self.assertIn('local:gone', str(caught.exception))
        self.assertIn('relay-local.py', str(caught.exception))


class RoleTests(Case):
    def resolver(self, main='kimi', roles=None, tiers=None):
        if main.startswith('local:'):
            config, preset_id = S.provider_config({'preset': main}), main
        else:
            config, preset_id = ProviderConfig(PRESETS[main].base_url, PRESETS[main].model, 'moonshot-key', {}, 32768), main
        return RoleResolver(config, preset_id, validate_roles(roles), key_lookup=self.lookup, main_effort='high',
                            tiers=validate_tiers(tiers))

    def test_a_local_endpoint_can_be_flash_and_lite(self):
        made = self.resolver(tiers={'flash': {'preset': 'local:bonsai'}, 'lite': {'preset': 'local:small'}})
        flash, lite = made.resolve('flash'), made.resolve('chores')
        self.assertEqual((flash.config.model, flash.config.local, flash.config.api_key, flash.source != 'fallback'),
                         ('bonsai-2-27b', True, '', True))
        self.assertEqual((lite.config.model, lite.config.max_tokens), ('qwen3:4b', 8192))
        self.assertEqual(made.warnings, [])
        self.assertNotIn('local:bonsai', self.looked_up)       # a local id never reaches the keyring

    def test_a_role_can_be_pinned_to_a_local_endpoint(self):
        made = self.resolver(roles={'subagent': {'preset': 'local:bonsai'}})
        resolved = made.resolve('subagent')
        self.assertEqual((resolved.config.base_url, resolved.preset_id, resolved.is_main), ('http://127.0.0.1:8080/v1', 'local:bonsai', False))
        self.assertNotIn('reasoning_effort', resolved.config.extra)     # no effort knob on a local server

    def test_a_local_main_keeps_every_tier_on_itself(self):
        made = self.resolver(main='local:bonsai')
        self.assertEqual(made.main_preset_id, 'local:bonsai')
        for role in ('flash', 'summaries', 'subagent', 'chores'):
            self.assertEqual(made.resolve(role).config.model, 'bonsai-2-27b', role)
        self.assertFalse(made.has_key('local:bonsai'))          # honest: there is no key

    def test_a_hosted_role_without_a_key_still_falls_back_under_a_local_main(self):
        made = self.resolver(main='local:bonsai', roles={'subagent': {'preset': 'openai'}})
        resolved = made.resolve('subagent')
        self.assertEqual((resolved.source, resolved.config.model), ('fallback', 'bonsai-2-27b'))
        self.assertTrue(any('no stored key' in w for w in made.warnings))

    def test_unknown_local_ids_are_refused_at_validation(self):
        with self.assertRaises(ValueError):
            validate_roles({'subagent': {'preset': 'local:gone'}})
        with self.assertRaises(ValueError):
            validate_tiers({'flash': {'preset': 'local:gone'}})


class TestButtonTests(Case):
    def test_the_test_button_needs_no_key_for_a_local_endpoint(self):
        events, made = [], []

        def factory(preset_id, key):
            made.append((preset_id, key))
            raise OSError('stand-in: no server in this test')

        thread = keytest.run('local:bonsai', events.append, 7, lookup=self.lookup, factory=factory)
        thread.join(10)
        self.assertEqual(made, [('local:bonsai', '')])
        self.assertEqual(self.looked_up, [])
        self.assertEqual([(e['event'], e['id'], e['preset'], e['model'], e['ok']) for e in events],
                         [('key_tested', 7, 'local:bonsai', 'bonsai-2-27b', False)])

    def test_the_provider_it_builds_is_the_local_transport(self):
        provider = keytest._provider('local:small', '')
        self.assertEqual((provider.config.local, provider.config.api_key, provider.config.max_tokens), (True, '', 1024))
        self.assertEqual(provider.first_token_timeout, 120.0)

    def test_a_hosted_preset_without_a_key_is_answered_as_before(self):
        events = []
        self.assertIsNone(keytest.run('openai', events.append, 1, lookup=self.lookup))
        self.assertEqual(events[0]['error'], 'No key is stored for this provider.')
        with self.assertRaises(ValueError):
            keytest.run('local:gone', events.append)


class WorkerTests(Case):
    def test_the_presets_event_lists_local_endpoints_as_usable_without_a_key(self):
        env = {**os.environ, 'RELAY_KEYRING': 'off', L.ENV_PATH: self.registry, 'PYTHONPATH': str(ROOT / 'backend')}
        requests = [{'type': 'presets', 'id': 1}, {'type': 'local_endpoints', 'id': 2}, {'type': 'shutdown'}]
        done = subprocess.run([sys.executable, str(ROOT / 'backend' / 'worker.py')], text=True, capture_output=True,
                              input=''.join(json.dumps(r) + '\n' for r in requests), env=env, timeout=60)
        events = [json.loads(line) for line in done.stdout.splitlines() if line.startswith('{')]
        presets = next(e for e in events if e.get('event') == 'presets')
        rows = {row['id']: row for row in presets['presets']}
        self.assertEqual([i for i in rows if i.startswith('local:')], ['local:bonsai', 'local:small'])
        self.assertEqual(list(rows)[:len(PRESETS)], list(PRESETS))             # the built-in rows are first and unchanged
        bonsai = rows['local:bonsai']
        self.assertEqual((bonsai['local'], bonsai['has_stored_key'], bonsai['key_source'], bonsai['group'], bonsai['server']),
                         (True, False, 'local', 'local', 'llamacpp'))
        self.assertEqual((bonsai['context_window'], bonsai['efforts']), (131072, []))
        self.assertTrue(all(row['local'] is False for i, row in rows.items() if i in PRESETS))
        listed = next(e for e in events if e.get('event') == 'local_endpoints')
        self.assertEqual([i['id'] for i in listed['items']], ['local:bonsai', 'local:small'])


if __name__ == '__main__':
    unittest.main()
