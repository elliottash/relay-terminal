"""Context accounting regression coverage for #C8WX, using recorded Claude requests."""
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
import threading
from unittest.mock import Mock

from relay_core import guest_harness_claude as claude
from relay_core import guest_harness_codex as codex
from relay_core import guest_harness_provider as provider
from relay_core.provider import ProviderConfig


class GuestContextMeterTests(unittest.TestCase):
    def test_recorded_multicall_claude_turn_uses_final_request_not_sum(self):
        harness = claude.ClaudeHarness()
        state = {"text": [], "saw_delta": True}
        events = []
        path = Path(__file__).parent / 'fixtures/guest_harness_claude/bash-tool.jsonl'
        for line in path.read_text().splitlines():
            message = json.loads(line).get('json', {})
            if message.get('type') == 'assistant':
                harness._on_assistant(message, state, events.append)
            elif message.get('type') == 'result':
                harness._finish(message, state, events.append)
        usage = [e.data for e in events if e.kind == 'usage'][-1]
        self.assertEqual(usage['context_tokens'], 22252)
        self.assertEqual(usage['context_window'], 200000)
        self.assertEqual(usage['context_pct'], 11.1)
        self.assertEqual(usage['input_tokens'] + usage['cache_read_input_tokens']
                         + usage['cache_creation_input_tokens'], 43585)

    def test_claude_uses_parent_model_window_not_first_auxiliary_model(self):
        usage = claude._usage_event({'modelUsage': {'small': {'contextWindow': 200000},
                                                  'large': {'contextWindow': 1000000}}},
                                   {'input_tokens': 100000}, 'large')
        self.assertEqual(usage['context_pct'], 10)
        self.assertEqual(usage['model'], 'large')

    def test_aggregate_without_request_measurement_is_unknown(self):
        usage = claude._usage_event({'usage': {'input_tokens': 999999},
                                    'modelUsage': {'claude': {'contextWindow': 200000}}})
        self.assertNotIn('context_tokens', usage)
        self.assertEqual(usage['input_tokens'], 999999)
        self.assertEqual(usage['context_window'], 200000)

    def test_codex_zero_context_is_a_measurement_not_missing_usage(self):
        harness = codex.CodexHarness()
        harness._usage = {'total': {'inputTokens': 100}, 'last': {'totalTokens': 0},
                          'modelContextWindow': 258400}
        usage = harness._usage_for(SimpleNamespace(usage_baseline={}))
        self.assertEqual(usage['context_tokens'], 0)
        self.assertEqual(usage['context_pct'], 0)

    def test_live_usage_refreshes_meter_and_old_model_cannot_restore_stale_numbers(self):
        harness = Mock(session_id='test-session')
        held = provider.HarnessProvider(ProviderConfig('harness://codex', 'old', '', {}, 32768),
                                        harness, 'codex')
        agent = SimpleNamespace(session_data=lambda: {},
                                context_event=lambda: {'event': 'context', 'window': 128000},
                                provider=held)
        provider.attach(agent, held)
        events = []
        turn = provider._Turn(held, agent, None, events.append, threading.Event())
        turn._on_usage({'input_tokens': 10, 'context_tokens': 100, 'context_window': 200000})
        self.assertEqual(events[-1]['guest_context']['used_tokens'], 100)
        harness.set_model.return_value = 'new'
        provider.switch_model(agent, 'codex', {'guest': {'model': 'new'}})
        turn._on_usage({'input_tokens': 20, 'context_tokens': 200, 'context_window': 200000})
        self.assertEqual(events[-1]['guest_context'], {})
        self.assertEqual(events[-2]['usage']['prompt_tokens'], 20)

    def test_guest_identity_before_usage_and_after_detach(self):
        for guest in ('claude', 'codex'):
            with self.subTest(guest=guest):
                harness = Mock(session_id='test-session')
                held = provider.HarnessProvider(ProviderConfig('harness://'+guest, 'old', '', {}, 32768),
                                                harness, guest)
                agent = SimpleNamespace(session_data=lambda: {},
                                        context_event=lambda: {'window': 128000},
                                        provider=held)
                provider.attach(agent, held)
                self.assertEqual(agent.context_event()['guest'], guest)
                self.assertEqual(agent.context_event()['guest_context'], {})
                held.guest_context = {'window': 200000, 'used_tokens': 10000}
                harness.set_model.return_value = 'new'
                provider.switch_model(agent, guest, {'guest': {'model': 'new'}})
                self.assertEqual(agent.context_event()['guest_context'], {})
                provider.detach(agent)
                self.assertNotIn('guest', agent.context_event())


if __name__ == '__main__':
    unittest.main()
