"""Compaction progress (card #31BM): the summary call streams, and between ``compaction_started``
and ``compacted`` the pane emits throttled ``compaction_progress {chars, estimate, phase}`` events
the context chip turns into ``compacting… N%``. Fake providers only; no network."""
import json
import tempfile
import unittest
from pathlib import Path

from relay_core import context, sidecall
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig

CONFIG = lambda: ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')  # noqa: E731

SUMMARY = "The user did research, fixed a bug, and asked for a progress indicator. " * 8


class StreamingProvider:
    """Streams the summary in deltas (content first, then more content); tools-calls are turns."""

    def __init__(self, thinking_first=False):
        self.thinking_first = thinking_first
        self.streamed = []

    def complete(self, messages, tools, emit, cancel):
        if tools:
            return {'role': 'assistant', 'content': 'ok'}
        if self.thinking_first:
            emit({'event': 'thinking_delta', 'text': 'let me think ' * 4})
        for i in range(0, len(SUMMARY), 60):
            piece = SUMMARY[i:i + 60]
            self.streamed.append(piece)
            emit({'event': 'delta', 'text': piece})
        emit({'event': 'usage', 'usage': {'prompt_tokens': 500, 'completion_tokens': len(SUMMARY) // 4}})
        return {'role': 'assistant', 'content': SUMMARY}

    def cancel(self):
        pass


class SidecallTests(unittest.TestCase):
    def test_on_delta_receives_streamed_pieces(self):
        seen = []

        class P:
            def complete(self, messages, tools, emit, cancel):
                emit({'event': 'thinking_delta', 'text': 'hmm'})
                emit({'event': 'delta', 'text': 'abc'})
                emit({'event': 'delta', 'text': 'def'})
                emit({'event': 'usage', 'usage': {'total_tokens': 7}})
                return {'role': 'assistant', 'content': 'abcdef'}

            def cancel(self):
                pass

        text, usage = sidecall.call(P(), 'system', 'user', on_delta=lambda t, thinking: seen.append((t, thinking)))
        self.assertEqual(text, 'abcdef')
        self.assertEqual(usage, {'total_tokens': 7})
        self.assertEqual(seen, [('hmm', True), ('abc', False), ('def', False)])


class SummarizeTests(unittest.TestCase):
    def _messages(self):
        return [
            {'role': 'system', 'content': 'sys'},
            {'role': 'user', 'content': 'hello ' * 300},
            {'role': 'assistant', 'content': 'hi ' * 300},
            {'role': 'user', 'content': 'again ' * 300},
            {'role': 'assistant', 'content': 'ok ' * 300},
        ]

    def test_progress_counts_content_only_and_clamps_the_estimate(self):
        seen = []
        summary = context.summarize(StreamingProvider(thinking_first=True), self._messages(), None, None,
                                    400_000, on_progress=lambda c, e, t: seen.append((c, e, t)))
        self.assertEqual(summary, SUMMARY.strip())
        # Thinking arrives first and counts for nothing; content chars grow monotonically.
        self.assertEqual(seen[0], (0, seen[0][1], True))
        chars = [c for c, _est, _t in seen]
        self.assertEqual(chars, sorted(chars))
        self.assertEqual(chars[-1], len(SUMMARY))
        # A small transcript's estimate is the 2,000-char floor, inside the clamped range.
        self.assertTrue(all(2_000 <= est <= 12_000 for _c, est, _t in seen))

    def test_expected_chars_is_the_denominator(self):
        seen = []
        context.summarize(StreamingProvider(), self._messages(), None, None, 400_000,
                          expected_chars=len(SUMMARY), on_progress=lambda c, e, t: seen.append((c, e, t)))
        self.assertTrue(all(est == len(SUMMARY) for _c, est, _t in seen))


class AgentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'ws'
        self.root.mkdir()
        self.events = []

    def test_compaction_progress_flows_and_the_next_compaction_learns(self):
        provider = StreamingProvider()
        agent = Agent(CONFIG(), str(self.root), self.events.append, provider=provider,
                      session_dir=str(Path(self.temp.name) / 's'), context_window=1_000_000)
        agent.ask("one")
        agent.ask("two")
        agent.ask("three")
        agent.ask("four")
        for e in list(self.events):
            self.assertNotEqual(e.get('event'), 'error')
        agent.compact("manual")

        kinds = [e.get('event') for e in self.events]
        self.assertIn('compaction_started', kinds)
        self.assertIn('compacted', kinds)
        progress = [e for e in self.events if e.get('event') == 'compaction_progress']
        self.assertTrue(progress, "no compaction_progress events were emitted")
        self.assertLess(kinds.index('compaction_started'), [i for i, k in enumerate(kinds) if k == 'compaction_progress'][0])
        self.assertGreater(kinds.index('compacted'), kinds.index('compaction_progress'))
        # Phase is the summary stream, chars grow, and the estimate is positive.
        self.assertTrue(all(p.get('phase') == 'summary' for p in progress))
        self.assertEqual([p['chars'] for p in progress], sorted(p['chars'] for p in progress))
        self.assertTrue(all(p['estimate'] > 0 for p in progress))
        # The learned denominator is the summary that just landed (the stored, stripped one).
        self.assertEqual(agent._last_summary_chars, len(SUMMARY.strip()))


if __name__ == '__main__':
    unittest.main()
