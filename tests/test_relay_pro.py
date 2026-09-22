# SPDX-License-Identifier: AGPL-3.0-or-later
"""Client access, transport, defaults and worker integration against a real local gateway."""
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from relay_core import hosted, presets, relay_pro, roles
from relay_core.provider import HostedChatProvider, ProviderConfig, ProviderError
from tests.test_gateway import FakeUpstream, Gateway, KEY_ENV, config_for

ROOT = Path(__file__).resolve().parents[1]


class ProClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.upstream = FakeUpstream()

    @classmethod
    def tearDownClass(cls):
        cls.upstream.stop()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.upstream.requests.clear()
        data = config_for(self.upstream.base)
        for role in relay_pro.MODELS:
            data['roles'][role] = {**data['roles']['relay-main'], 'max_effort': 'high'}
        self.gateway = Gateway(data)
        self.addCleanup(self.gateway.close)
        self.secret = self.gateway.store.issue_pro_code('person')
        self.base = f'http://127.0.0.1:{self.gateway.port}/v1'
        self.env = patch.dict(os.environ, {'RELAY_HOSTED_URL': self.base, 'RELAY_KEYRING': 'off',
            'XDG_DATA_HOME': self.temp.name, KEY_ENV: 'upstream-secret'})
        self.env.start()
        self.addCleanup(self.env.stop)
        hosted.reset(hosted.Session(base=self.base))
        self.addCleanup(hosted.reset)
        relay_pro.set_listener(None)
        relay_pro.invalidate()
        self.saved = {}
        for name, callback in (
            ('lookup', lambda preset: self.saved.get(preset, '')),
            ('key_source', lambda preset: 'keyring' if preset in self.saved else ''),
            ('store', lambda preset, value: self.saved.update({preset: value})),
            ('remove', lambda preset: self.saved.pop(preset, None) is not None),
        ):
            patcher = patch.object(relay_pro.keystore, name, side_effect=callback)
            patcher.start()
            self.addCleanup(patcher.stop)

    def operation(self, action, value=''):
        events = []
        thread = relay_pro.run(action, events.append, 'id', value)
        thread.join(5)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(events), 1)
        self.assertNotIn(self.secret, json.dumps(events))
        return events[0]

    def test_store_test_remove_validate_access_without_generation(self):
        self.assertEqual(self.operation('store', 'bad')['event'], 'error')
        self.assertFalse(self.saved)
        self.assertEqual(self.operation('store', self.secret)['event'], 'key_stored')
        self.assertEqual(self.saved['relay-pro'], self.secret)
        self.assertTrue(relay_pro.status()['available'])
        self.assertEqual(relay_pro.status()['key_source'], 'keyring')
        self.assertTrue(self.operation('test')['ok'])
        self.assertEqual(self.upstream.requests, [])
        self.assertTrue(self.operation('remove')['removed'])
        self.assertFalse(relay_pro.status()['available'])
        self.assertFalse(relay_pro.status()['has_stored_key'])

    def test_revoked_test_keeps_storage_but_disables_availability(self):
        self.operation('store', self.secret)
        self.gateway.store.revoke_pro_code('person')
        result = self.operation('test')
        self.assertFalse(result['ok'])
        self.assertTrue(relay_pro.status()['has_stored_key'])
        self.assertFalse(relay_pro.status()['available'])

    def test_real_transport_rechecks_code_each_call_and_revocation_invalidates(self):
        self.operation('store', self.secret)
        config = ProviderConfig(self.base, 'relay-pro-main', '', hosted=True)
        provider = HostedChatProvider(config)
        events = []
        provider.complete([{'role': 'user', 'content': 'hi'}], [], events.append, threading.Event())
        self.assertEqual(len(self.upstream.requests), 1)
        self.assertNotIn(self.secret, repr(config))
        self.assertNotIn(self.secret, json.dumps(events))
        self.assertNotIn(self.secret, json.dumps(self.upstream.requests))
        self.gateway.store.revoke_pro_code('person')
        with self.assertRaises(ProviderError) as caught:
            provider.complete([{'role': 'user', 'content': 'hi'}], [], events.append, threading.Event())
        self.assertEqual(caught.exception.code, 'pro_access_denied')
        self.assertFalse(relay_pro.status()['available'])
        self.assertEqual(len(self.upstream.requests), 1)
        self.operation('remove')
        with self.assertRaises(ProviderError):
            provider.complete([{'role': 'user', 'content': 'hi'}], [], events.append, threading.Event())
        free = HostedChatProvider(ProviderConfig(self.base, 'relay-lite', '', hosted=True))
        free.complete([{'role': 'user', 'content': 'hi'}], [], events.append, threading.Event())
        self.assertEqual(len(self.upstream.requests), 2)

    def test_code_is_not_attached_to_an_overridden_gateway_endpoint(self):
        self.operation('store', self.secret)
        provider = HostedChatProvider(ProviderConfig(self.upstream.base, 'relay-pro-main', '', hosted=True))
        with self.assertRaises(ProviderError) as caught:
            provider.complete([{'role': 'user', 'content': 'hi'}], [], lambda event: None, threading.Event())
        self.assertEqual(caught.exception.code, 'pro_access_denied')
        self.assertEqual(self.upstream.requests, [])

    def test_background_check_is_nonblocking_and_stale_check_cannot_reactivate(self):
        self.saved['relay-pro'] = self.secret
        entered, release, finished = threading.Event(), threading.Event(), threading.Event()
        relay_pro.set_listener(finished.set)
        self.addCleanup(relay_pro.set_listener, None)
        def slow(value):
            entered.set()
            release.wait(5)
            return list(relay_pro.MODELS)
        with patch.object(relay_pro, 'validate', side_effect=slow):
            before = time.monotonic()
            relay_pro.start_refresh()
            self.assertLess(time.monotonic() - before, 0.2)
            self.assertTrue(entered.wait(2))
            self.assertFalse(relay_pro.status()['available'])
            self.saved.clear()
            relay_pro.invalidate()
            finished.clear()
            release.set()
            self.assertTrue(finished.wait(2))
        self.assertFalse(relay_pro.status()['available'])

    def test_changed_code_does_not_reuse_validated_availability(self):
        self.operation('store', self.secret)
        self.saved['relay-pro'] = 'different'
        self.assertFalse(relay_pro.status()['available'])

    def test_models_effort_tiers_and_free_lite(self):
        self.operation('store', self.secret)
        row = presets.PRESETS['relay-pro'].to_dict()
        self.assertFalse(row['effort_fixed'])
        self.assertEqual(row['efforts'], ['low', 'medium', 'high', 'max'])
        self.assertTrue(all(not r['effort_fixed'] for r in row['models']))
        self.assertNotIn(self.secret, json.dumps(row))
        for model in relay_pro.MODELS:
            self.assertEqual(presets.match_preset(presets.PRESETS['relay-pro'].base_url, model).id, 'relay-pro')
        lists = presets.tier_list_defaults(['relay-free', 'relay-pro'])['plain']
        for tier in ('high', 'main', 'flash'):
            self.assertEqual(lists[tier][0]['model'], 'relay-pro-' + tier)
        self.assertEqual(lists['lite'][0]['preset'], 'relay-free')
        config = ProviderConfig(presets.PRESETS['relay-pro'].base_url, 'relay-pro-main', '', hosted=True)
        resolver = roles.RoleResolver(config, 'relay-pro')
        self.assertTrue(resolver.has_key('relay-pro'))
        self.assertEqual(resolver.resolve('high').model, 'relay-pro-high')
        self.assertEqual(resolver.resolve('chores').model, 'relay-lite')
        relay_pro.invalidate(self.secret)
        self.assertFalse(resolver.has_key('relay-pro'))

    def test_worker_background_validation_and_test_key_do_not_generate(self):
        env = {**os.environ, 'HOME': self.temp.name, 'PYTHONPATH': str(ROOT / 'backend'),
               relay_pro.keystore.env_name('relay-pro'): self.secret}
        proc = subprocess.Popen([sys.executable, '-S', str(ROOT / 'backend/worker.py')],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, cwd=ROOT, env=env)
        received = queue.Queue()
        def read():
            for line in proc.stdout:
                received.put(json.loads(line))
        reader = threading.Thread(target=read, daemon=True)
        reader.start()
        events = []
        try:
            proc.stdin.write(json.dumps({'type': 'presets', 'id': 'p'}) + '\n')
            proc.stdin.write(json.dumps({'type': 'test_key', 'preset': 'relay-pro', 'id': 'test'}) + '\n')
            proc.stdin.flush()
            deadline = time.monotonic() + 15
            active = tested = False
            while not (active and tested) and time.monotonic() < deadline:
                event = received.get(timeout=5)
                events.append(event)
                if event['event'] == 'presets':
                    row = next(r for r in event['presets'] if r['id'] == 'relay-pro')
                    active |= row['available']
                    self.assertTrue(row['has_stored_key'])
                    self.assertEqual(row['key_source'], 'env')
                if event['event'] == 'key_tested':
                    tested = event['ok']
            self.assertTrue(active and tested, events)
            self.assertEqual(self.upstream.requests, [])
            self.assertNotIn(self.secret, json.dumps(events))
        finally:
            proc.stdin.write('{"type":"shutdown"}\n')
            proc.stdin.flush()
            proc.wait(timeout=5)
            reader.join(2)
            stderr = proc.stderr.read()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()
        self.assertNotIn(self.secret, stderr)


if __name__ == '__main__':
    unittest.main()
