# SPDX-License-Identifier: AGPL-3.0-or-later
"""Provider refusals say which limit or credential failed (card #QK2Q).

Measured 2026-09-25 on pane f35e3cfe: Z.AI's Coding Plan answered 429 with code 1308, "Usage limit
reached for 5 hour. Your limit will reset at 2026-09-26 05:53:25", and Relay retried it six times a
turn as transient; Kimi Code answered 401 to a fifteen-minute login token the pane had read once,
while the keyring already held a fresh one. Everything here is offline.
"""
import base64
import io
import json
import tempfile
import threading
import time
import unittest
import urllib.error
from email.utils import formatdate
from unittest import mock

from relay_core import provider_errors as pe
from relay_core import provider_limits
from relay_core.agent import Agent
from relay_core.presets import PRESETS
from relay_core.provider import ChatProvider, ProviderConfig, ProviderError, ProviderQuotaExhausted
from relay_core.roles import RoleResolver

ZAI = 'https://api.z.ai/api/coding/paas/v4'
KIMI = 'https://api.kimi.ai/coding/v1'
# 2026-09-25 20:44:00 UTC, the moment of the live probe.
NOW = 1790369040


def refusal(status, body, headers=None, url=ZAI):
    return urllib.error.HTTPError(url, status, 'refused', headers or {},
                                  io.BytesIO(json.dumps(body).encode() if not isinstance(body, bytes) else body))


def zai_1308(with_clock=True):
    headers = {}
    if with_clock:
        # The provider's own clock in X-LOG-ID (UTC+8) and GMT in Date, as the live probe saw them.
        headers = {'X-LOG-ID': '202609260444008a9241d800714612', 'Date': formatdate(NOW, usegmt=True)}
    return refusal(429, {'error': {'code': '1308', 'message':
        'Usage limit reached for 5 hour. Your limit will reset at 2026-09-26 05:53:25 SECRET'}}, headers)


def jwt(exp):
    part = lambda obj: base64.urlsafe_b64encode(json.dumps(obj).encode()).decode().rstrip('=')
    return f"{part({'alg': 'HS512'})}.{part({'iss': 'kimi-auth', 'type': 'access', 'exp': exp})}.{'s' * 43}"


class ClassifyTests(unittest.TestCase):
    def setUp(self):
        provider_limits._last.clear()
        self.addCleanup(provider_limits._last.clear)

    def test_zai_five_hour_limit_is_final_with_the_instant_from_the_providers_clock(self):
        got = pe.classify(zai_1308(), host='api.z.ai', now=NOW)
        self.assertEqual((got.kind, got.vendor_code, got.source), ('quota_5h', '1308', 'code'))
        self.assertTrue(got.final)
        # 05:53:25 at UTC+8 is 21:53:25 UTC.
        self.assertEqual(got.resets_at, 1790373205)

    def test_without_the_clock_headers_the_quota_poll_pins_the_reset(self):
        poll = {'updated_at': NOW - 60, 'windows': [
            {'kind': '5h', 'used_percent': 100.0, 'resets_at': 1790373205},
            {'kind': 'weekly', 'used_percent': 87.0, 'resets_at': 1790718789}]}
        got = pe.classify(zai_1308(with_clock=False), host='api.z.ai', poll=poll, now=NOW)
        self.assertEqual((got.kind, got.resets_at), ('quota_5h', 1790373205))

    def test_with_neither_the_reset_stays_in_provider_time(self):
        got = pe.classify(zai_1308(with_clock=False), host='api.z.ai', now=NOW)
        self.assertEqual((got.resets_at, got.provider_reset), (None, '2026-09-26 05:53:25'))
        self.assertIn('provider time', pe.reset_phrase(got, NOW))

    def test_zai_codes(self):
        for code, kind, final in (('1310', 'quota_period', True), ('1302', 'rate_limit', False),
                                  ('1113', 'balance', True), ('1309', 'plan_expired', True),
                                  ('1305', 'overloaded', False)):
            with self.subTest(code=code):
                got = pe.classify(refusal(429, {'error': {'code': code, 'message': 'x'}}),
                                  host='api.z.ai', now=NOW)
                self.assertEqual((got.kind, got.final), (kind, final))

    def test_openai_shaped_types_and_402(self):
        cases = ((429, {'error': {'type': 'exceeded_current_quota_error', 'message': 'x'}}, 'balance'),
                 (429, {'error': {'type': 'rate_limit_reached_error', 'message': 'x'}}, 'rate_limit'),
                 (402, {'error': {'code': 402, 'message': 'Insufficient credits'}}, 'balance'),
                 (401, {'error': {'type': 'invalid_authentication_error', 'message': 'x'}}, 'auth'))
        for status, body, kind in cases:
            with self.subTest(kind=kind, status=status):
                self.assertEqual(pe.classify(refusal(status, body, url=KIMI), host='x', now=NOW).kind, kind)

    def test_a_bare_quota_message_stays_retryable(self):
        # Gemini's per-minute limit says "check quota" and clears in seconds.
        got = pe.classify(refusal(429, {'error': {'code': 429, 'status': 'RESOURCE_EXHAUSTED',
                                                  'message': 'Resource has been exhausted (e.g. check quota).'}}),
                          host='generativelanguage.googleapis.com', now=NOW)
        self.assertEqual(got.kind, 'quota')
        self.assertFalse(got.final)

    def test_an_unexplained_429_on_a_plan_the_poll_says_is_spent_is_that_window(self):
        poll = {'updated_at': NOW - 60, 'windows': [{'kind': 'weekly', 'used_percent': 100, 'resets_at': NOW + 7200}]}
        got = pe.classify(refusal(429, b'{}', url=KIMI), host='api.kimi.ai', poll=poll, now=NOW)
        self.assertEqual((got.kind, got.resets_at, got.source), ('quota_weekly', NOW + 7200, 'poll'))

    def test_an_expired_login_token_is_named_as_one(self):
        got = pe.classify(refusal(401, b'{"error":{"message":"unauthorized"}}', url=KIMI),
                          host='api.kimi.ai', api_key=jwt(NOW - 60), now=NOW)
        self.assertEqual((got.kind, got.token_expired_at), ('token_expired', NOW - 60))
        live = pe.classify(refusal(401, b'{}', url=KIMI), host='api.kimi.ai', api_key=jwt(NOW + 600), now=NOW)
        self.assertEqual(live.kind, 'auth')

    def test_malformed_values_are_never_repeated(self):
        got = pe.classify(refusal(429, {'error': {'code': 'bad code <script>', 'message':
                                                  'Weekly limit, reset at 2030-99-99 99:99:99'}}),
                          host='api.z.ai', now=NOW)
        self.assertEqual((got.vendor_code, got.provider_reset), ('', ''))
        self.assertNotIn('2030-99', pe.sentence(got, 'm at h'))


class ProviderTests(unittest.TestCase):
    def setUp(self):
        provider_limits._last.clear()
        self.addCleanup(provider_limits._last.clear)

    def test_zai_1308_is_asked_once_and_says_which_limit_and_when(self):
        provider = ChatProvider(ProviderConfig(ZAI, 'glm-5.3', 'fixture'))
        error = zai_1308()
        opener = mock.Mock()
        opener.open.side_effect = error
        events = []
        with mock.patch('relay_core.provider.time.time', return_value=NOW), \
             mock.patch('relay_core.provider_errors.time.time', return_value=NOW):
            with self.assertRaises(ProviderQuotaExhausted) as caught:
                provider._open(opener, mock.Mock(), events.append, threading.Event(), time.monotonic())
        self.assertEqual(opener.open.call_count, 1)
        text = str(caught.exception)
        self.assertIn('5-hour usage limit reached', text)
        self.assertIn(time.strftime('%H:%M', time.localtime(1790373205)), text)
        self.assertNotIn('SECRET', text)
        self.assertEqual(caught.exception.resets_at, 1790373205)
        self.assertEqual(caught.exception.issue['kind'], 'quota_5h')
        self.assertEqual(caught.exception.issue['preset'], 'glm-coding')
        self.assertEqual(caught.exception.retry_after_s, 900.0)   # re-asked every 15 min at most

    def test_a_transient_rate_limit_is_retried_and_the_note_says_so(self):
        provider = ChatProvider(ProviderConfig(ZAI, 'glm-5.3', 'fixture'))
        opener = mock.Mock()
        opener.open.side_effect = [refusal(429, {'error': {'code': '1302', 'message': 'x'}}), 'ok']
        events = []
        with mock.patch.object(provider, '_wait_retry'):
            self.assertEqual(provider._open(opener, mock.Mock(), events.append, threading.Event(),
                                            time.monotonic()), 'ok')
        notes = [e['text'] for e in events if e.get('event') == 'provider_retry']
        self.assertIn('rate limited', notes[0])

    def test_a_rotated_stored_key_is_read_again_after_a_401(self):
        config = ProviderConfig(KIMI, 'k3', jwt(int(time.time()) - 60), key_source='kimi-code')
        provider = ChatProvider(config)
        fresh = jwt(int(time.time()) + 900)
        request = urllib.request.Request(KIMI + '/chat/completions', data=b'{}',
                                         headers={'Authorization': 'Bearer ' + config.api_key})
        opener = mock.Mock()
        opener.open.side_effect = [refusal(401, b'{}', url=KIMI), 'ok']
        with mock.patch('relay_core.keystore.lookup', return_value=fresh) as lookup:
            self.assertEqual(provider._open(opener, request, lambda e: None, threading.Event(),
                                            time.monotonic()), 'ok')
        lookup.assert_called_with('kimi-code')
        self.assertEqual(config.api_key, fresh)
        self.assertEqual(request.get_header('Authorization'), 'Bearer ' + fresh)

    def test_an_expired_token_the_keyring_has_not_replaced_says_so(self):
        stale = jwt(int(time.time()) - 60)
        provider = ChatProvider(ProviderConfig(KIMI, 'k3', stale, key_source='kimi-code'))
        opener = mock.Mock()
        opener.open.side_effect = refusal(401, b'{}', url=KIMI)
        with mock.patch('relay_core.keystore.lookup', return_value=stale):
            with self.assertRaises(urllib.error.HTTPError) as caught:
                provider._open(opener, mock.Mock(), lambda e: None, threading.Event(), time.monotonic())
        error = provider._http_error(caught.exception)
        self.assertIn('login token expired', str(error))
        self.assertEqual(error.issue['kind'], 'token_expired')
        self.assertEqual(opener.open.call_count, 1)

    def test_a_typed_in_key_is_never_swapped_for_a_stored_one(self):
        provider = ChatProvider(ProviderConfig(KIMI, 'k3', 'typed'))
        opener = mock.Mock()
        opener.open.side_effect = refusal(401, b'{}', url=KIMI)
        with mock.patch('relay_core.keystore.lookup', return_value='stored') as lookup:
            with self.assertRaises(urllib.error.HTTPError):
                provider._open(opener, mock.Mock(), lambda e: None, threading.Event(), time.monotonic())
        lookup.assert_not_called()


class Refuser:
    def __init__(self, error):
        self.error, self.calls = error, 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        raise self.error


class Answerer:
    def __init__(self):
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        return {'role': 'assistant', 'content': 'from the spare'}


class FailoverSkipsSpentPlansTests(unittest.TestCase):
    """The pane of #QK2Q: Kimi 401s, and the failover went to a Z.AI plan whose own quota poll had
    the 5-hour window at 100%."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.events, self.stubs = [], {}
        patcher = mock.patch('relay_core.agent._provider_for',
                             side_effect=lambda config, stall: self.stubs[config.model])
        patcher.start()
        self.addCleanup(patcher.stop)
        provider_limits._last.clear()
        self.addCleanup(provider_limits._last.clear)

    def test_a_spent_plan_is_skipped_and_the_note_names_both_causes(self):
        now = int(time.time())
        provider_limits._last['glm-coding'] = {'updated_at': now, 'windows': [
            {'kind': '5h', 'used_percent': 100.0, 'resets_at': now + 3900}]}
        issue = {'kind': 'token_expired', 'label': 'login token expired', 'preset': 'kimi-code'}
        self.stubs['k3'] = Refuser(ProviderError('Provider HTTP 401 …', issue=issue))
        self.stubs['glm-5.3'] = Answerer()
        self.stubs['gpt-6-mini'] = Answerer()
        kimi = PRESETS['kimi-code']
        config = ProviderConfig(kimi.base_url, 'k3', 'k')
        roles = RoleResolver(config, 'kimi-code', {}, key_lookup=lambda p: 'k')
        agent = Agent(config, self.temp.name, self.events.append, preset_id='kimi-code', roles=roles,
                      fallbacks=[{'preset': 'glm-coding', 'model': 'glm-5.3'},
                                 {'preset': 'openai', 'model': 'gpt-6-mini'}])
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'done')
        self.assertEqual(self.stubs['glm-5.3'].calls, 0)
        self.assertEqual(self.stubs['gpt-6-mini'].calls, 1)
        move = [e for e in self.events if e.get('reason') == 'failover'][0]
        self.assertIn('failed (login token expired)', move['text'])
        self.assertIn('5-hour usage limit reached', move['text'])
        self.assertEqual(move['issue']['kind'], 'token_expired')

    def test_a_tied_rank_is_drawn_by_remaining_quota_like_a_new_panes_default(self):
        # Owner, 2026-09-25: failovers pick the way a pane's default is picked. Two spares share
        # rank 1; Kimi has 90% of its window left, Z.AI 10%, both resetting in an hour.
        now = int(time.time())
        for preset, used in (('glm-coding', 90.0), ('kimi-code', 10.0)):
            provider_limits._last[preset] = {'updated_at': now, 'windows': [
                {'kind': '5h', 'used_percent': used, 'resets_at': now + 3600}]}
        openai = PRESETS['openai']
        config = ProviderConfig(openai.base_url, 'gpt-6-mini', 'k')
        picked = {}
        for u in (0.05, 0.95):
            with self.subTest(u=u):
                self.events.clear()
                self.stubs.update({'gpt-6-mini': Refuser(ProviderError('Provider HTTP 503.')),
                                   'glm-5.3': Answerer(), 'k3': Answerer()})
                # The ranked Main list, as the GUI sends it (`tiers`); the older `fallbacks` option
                # has no ranks, so nothing in it ever ties.
                roles = RoleResolver(config, 'openai', {}, key_lookup=lambda p: 'k', tiers={'main': [
                    {'preset': 'glm-coding', 'model': 'glm-5.3', 'rank': 1},
                    {'preset': 'kimi-code', 'model': 'k3', 'rank': 1}]})
                agent = Agent(config, self.temp.name, self.events.append, preset_id='openai', roles=roles)
                with mock.patch('relay_core.roles.random.random', return_value=u), \
                     mock.patch('relay_core.logs.routing_draw'):
                    agent.ask('hello')
                picked[u] = [e['to_model'] for e in self.events if e.get('reason') == 'failover']
        # Kimi's weight is nine times Z.AI's: most draws land on it, a low one on Z.AI.
        self.assertEqual(picked[0.95], ['k3'])
        self.assertNotEqual(picked[0.05], picked[0.95])

    def test_a_turn_that_fails_carries_its_issue(self):
        issue = {'kind': 'auth', 'label': 'API key rejected', 'preset': 'kimi-code'}
        self.stubs['k3'] = Refuser(ProviderError('Provider HTTP 401 …', issue=issue))
        kimi = PRESETS['kimi-code']
        agent = Agent(ProviderConfig(kimi.base_url, 'k3', 'k'), self.temp.name, self.events.append,
                      preset_id='kimi-code', failover=False)
        agent.ask('hello')
        self.assertEqual(self.events[-1]['event'], 'error')
        self.assertEqual(self.events[-1]['issue'], issue)


if __name__ == '__main__':
    unittest.main()
