"""Size-based compaction (card #0C0V step 9): `compact_over_tokens` compacts between turns, after
`done`, when the turn's last single request's prompt passed N tokens. Off by default, never on a
guest harness, validated. Fake providers only; no network."""
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from relay_core import session_protocol
from relay_core.agent import Agent
from relay_core.context import DEFAULT_OVER_TOKENS, validate_over_tokens
from relay_core.provider import ProviderConfig

CONFIG = lambda: ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')  # noqa: E731


def call(name, arguments, call_id):
    return {'id': call_id, 'type': 'function', 'function': {'name': name, 'arguments': json.dumps(arguments)}}


class Provider:
    """Scripted replies for turn steps; `prompt` is the prompt size each step's usage reports (None:
    no usage). Calls without tools are summaries."""
    def __init__(self, responses=(), prompt=None):
        self.responses = list(responses)
        self.prompt = prompt
        self.side_requests = []

    def complete(self, messages, tools, emit, cancel):
        if not tools:
            self.side_requests.append(messages)
            return {'role': 'assistant', 'content': 'SUMMARY'}
        if self.prompt is not None:
            emit({'event': 'usage', 'usage': {'prompt_tokens': self.prompt, 'completion_tokens': 10,
                                              'total_tokens': self.prompt + 10}})
        if self.responses:
            return self.responses.pop(0)
        return {'role': 'assistant', 'content': 'ok'}

    def cancel(self):
        pass


class GuestProvider(Provider):
    serves_side_calls = False


class OverTokensTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'ws'
        self.root.mkdir()
        self.events = []

    def agent(self, provider, **kwargs):
        return Agent(CONFIG(), str(self.root), self.events.append, provider=provider,
                     session_dir=str(Path(self.temp.name) / 's'), context_window=1_000_000, **kwargs)

    def kinds(self):
        return [e['event'] for e in self.events]

    def of(self, kind):
        return [e for e in self.events if e['event'] == kind]

    def test_off_by_default(self):
        agent = self.agent(Provider(prompt=500_000))
        self.assertIsNone(agent.compact_over_tokens)
        for i in range(3):
            agent.ask(f'prompt {i}')
        self.assertEqual(self.of('compaction_started'), [])

    def test_fires_after_done_with_its_reason(self):
        provider = Provider(prompt=20_000)
        agent = self.agent(provider, compact_over_tokens=10_000)
        for i in range(3):
            agent.ask(f'prompt {i}')
        started = self.of('compaction_started')
        self.assertEqual(len(started), 3)       # once per turn: every turn's last request was over
        self.assertEqual(started[-1], {'event': 'compaction_started', 'reason': 'auto', 'trigger': 'over_tokens',
                                       'over_tokens': 10_000, 'last_prompt_tokens': 20_000})
        compacted = self.of('compacted')[-1]
        self.assertEqual((compacted['trigger'], compacted['over_tokens'], compacted['last_prompt_tokens']),
                         ('over_tokens', 10_000, 20_000))
        # The third turn has two older turns to summarise, measured against N rather than the window.
        self.assertTrue(provider.side_requests)
        self.assertGreater(compacted['summary_chars'], 0)
        kinds = self.kinds()
        for index, kind in enumerate(kinds):
            if kind == 'compaction_started':
                self.assertEqual(kinds[:index].count('done'), kinds[:index].count('compaction_started') + 1)

    def test_never_mid_turn(self):
        provider = Provider([{'role': 'assistant', 'content': '',
                              'tool_calls': [call('list_directory', {'path': '.'}, f'c{i}')]} for i in range(3)],
                            prompt=20_000)
        agent = self.agent(provider, compact_over_tokens=10_000)
        agent.ask('look around')
        kinds = self.kinds()
        self.assertEqual(kinds.count('compaction_started'), 1)
        self.assertLess(kinds.index('done'), kinds.index('compaction_started'))

    def test_under_the_bound_does_nothing(self):
        agent = self.agent(Provider(prompt=9_000), compact_over_tokens=10_000)
        agent.ask('small')
        self.assertEqual(self.of('compaction_started'), [])

    def test_the_last_request_counts_not_the_turn_total(self):
        # Three requests of 6k each: 18k over the turn, but no single request passed 10k.
        provider = Provider([{'role': 'assistant', 'content': '',
                              'tool_calls': [call('list_directory', {'path': '.'}, f'c{i}')]} for i in range(2)],
                            prompt=6_000)
        agent = self.agent(provider, compact_over_tokens=10_000)
        agent.ask('look around')
        self.assertEqual(self.of('compaction_started'), [])

    def test_without_usage_the_estimate_is_used(self):
        agent = self.agent(Provider(), compact_over_tokens=8_000)
        agent.ask('x ' * (4 * 8_000))
        self.assertTrue(agent.context.used(agent.messages, agent.tools())[1])   # estimated
        started, = self.of('compaction_started')
        self.assertEqual(started['trigger'], 'over_tokens')
        self.assertGreater(started['last_prompt_tokens'], 8_000)
        self.events.clear()
        quiet = self.agent(Provider(), compact_over_tokens=10_000_000)
        quiet.ask('hello')
        self.assertEqual(self.of('compaction_started'), [])

    def test_not_on_a_guest_harness(self):
        agent = self.agent(GuestProvider(prompt=500_000), compact_over_tokens=10_000)
        agent.ask('hello')
        agent.ask('again')
        self.assertEqual(self.of('compaction_started'), [])

    def test_guest_transcript_above_native_limit_does_not_auto_compact(self):
        agent = Agent(CONFIG(), str(self.root), self.events.append, provider=GuestProvider(),
                      session_dir=str(Path(self.temp.name) / 's'), context_window=128_000)
        # A summaries role used to make the guest eligible for Relay's native 71,232-token
        # limit, even though its own context was managed by the guest.
        agent.roles = SimpleNamespace(resolve=lambda role: SimpleNamespace(is_main=False))
        agent.messages.append({'role': 'user', 'relay_kind': 'prompt', 'content': 'x' * 300_000})
        self.assertEqual(agent.context.limit, 71_232)
        self.assertTrue(agent.context.over(agent.messages, agent.tools()))
        self.assertIsNone(agent._maybe_compact())
        self.assertEqual(self.of('compaction_started'), [])

    def test_not_after_a_cancelled_or_failed_turn(self):
        class Failing(Provider):
            def complete(self, messages, tools, emit, cancel):
                if tools:
                    emit({'event': 'usage', 'usage': {'prompt_tokens': 50_000, 'completion_tokens': 1}})
                    raise ValueError('boom')
                return super().complete(messages, tools, emit, cancel)
        agent = self.agent(Failing(), compact_over_tokens=10_000)
        agent.ask('hello')
        self.assertIn('error', self.kinds())
        self.assertEqual(self.of('compaction_started'), [])

    def test_window_trigger_names_itself(self):
        agent = Agent(CONFIG(), str(self.root), self.events.append, provider=Provider(),
                      session_dir=str(Path(self.temp.name) / 's'), context_window=8_000, compact_threshold=0.5)
        for i in range(3):
            agent.ask(f'task {i} ' + 'y' * 8000)
        started = self.of('compaction_started')
        self.assertTrue(started)
        self.assertEqual({(e['reason'], e['trigger']) for e in started}, {('auto', 'window')})


class ValidationTests(unittest.TestCase):
    def test_values(self):
        self.assertIsNone(validate_over_tokens(None))
        self.assertIsNone(validate_over_tokens(0))
        self.assertEqual(validate_over_tokens(DEFAULT_OVER_TOKENS), 256_000)
        self.assertEqual(validate_over_tokens(8_000), 8_000)
        self.assertEqual(validate_over_tokens(10_000_000), 10_000_000)
        for bad in (7_999, 10_000_001, -1, True, False, 256000.0, '256000', [1]):
            with self.assertRaises(ValueError, msg=repr(bad)) as caught:
                validate_over_tokens(bad)
            self.assertIn('compact_over_tokens', str(caught.exception))

    def test_configure_flows_it_to_the_agent(self):
        with tempfile.TemporaryDirectory() as temp:
            options = session_protocol.agent_options({'compact_over_tokens': 256_000}, temp)
            self.assertEqual(options['compact_over_tokens'], 256_000)
            self.assertIsNone(session_protocol.agent_options({}, temp)['compact_over_tokens'])
            with self.assertRaises(ValueError):
                session_protocol.agent_options({'compact_over_tokens': 100}, temp)
            options['session_dir'] = str(Path(temp) / 's')
            agent = Agent(CONFIG(), temp, lambda e: None, provider=Provider(), **options)
            self.assertEqual(agent.compact_over_tokens, 256_000)
            self.assertEqual(session_protocol.configured_fields(agent)['compact_over_tokens'], 256_000)


if __name__ == '__main__':
    unittest.main()
