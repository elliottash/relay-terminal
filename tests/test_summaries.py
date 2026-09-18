"""Agent-written session summaries (protocol section 18.4).

Two or three sentences per session — what was wanted, what was done, what is left — written on the
same cheap chores call as the pane title, stored in the session file and its meta file, and
available on demand or in a batch for sessions nobody has open. Fake providers only; no network.
`XDG_DATA_HOME` points at a temporary directory throughout, so no test touches the real index.
"""
import json
import os
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import conv_index, sessions as session_files, titles
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig, ProviderError
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands
from relay_core.sessions import SessionStore

sys.path.insert(0, str(Path(__file__).parent))
from test_sessions import ScriptedProvider  # noqa: E402
from test_queue import Recorder  # noqa: E402

SUMMARY = ('{"summary": "Fix the FTS index going stale. Added reconcile on first use and an '
           'autosave hook. Left: backfill old summaries."}')
SUMMARY_TEXT = ("Fix the FTS index going stale. Added reconcile on first use and an autosave hook. "
                "Left: backfill old summaries.")


class CleanTests(unittest.TestCase):
    def test_markdown_quotes_and_preambles_are_stripped(self):
        self.assertEqual(titles.clean_summary('  "Fix the drag."  '), 'Fix the drag.')
        self.assertEqual(titles.clean_summary('**Fix** the `drag`; see [the card](http://x/y).'),
                         'Fix the drag; see the card.')
        self.assertEqual(titles.clean_summary('## Summary\n- Fixed the drag.\n- Left: the tests.'),
                         'Summary Fixed the drag. Left: the tests.')
        self.assertEqual(titles.clean_summary('Summary: fixed the drag.'), 'Fixed the drag.')
        self.assertEqual(titles.clean_summary("Here is a short summary: fixed the drag."),
                         'Fixed the drag.')
        self.assertEqual(titles.clean_summary('The user wanted to fix the drag.'), 'Fix the drag.')
        self.assertEqual(titles.clean_summary('```json\nFixed the drag.\n```'), 'Fixed the drag.')
        self.assertEqual(titles.clean_summary('Fixed   the\n\ndrag.'), 'Fixed the drag.')
        self.assertEqual(titles.clean_summary(None), '')
        self.assertEqual(titles.clean_summary('   '), '')

    def test_the_cap_falls_on_a_sentence_boundary(self):
        long = ('One sentence that says a thing. ' * 20).strip()
        cut = titles.clean_summary(long)
        self.assertLessEqual(len(cut), titles.MAX_SUMMARY)
        self.assertTrue(cut.endswith('.'), cut)
        self.assertNotIn('…', cut)
        # No sentence break anywhere near the cap: a word boundary and an ellipsis, as titles do.
        run = 'word ' * 200
        self.assertLessEqual(len(titles.clean_summary(run)), titles.MAX_SUMMARY)
        self.assertTrue(titles.clean_summary(run).endswith('…'))

    def test_junk_is_not_a_summary(self):
        class Provider:
            def __init__(self, reply):
                self.reply = reply

            def complete(self, messages, tools, emit, cancel):
                return {'role': 'assistant', 'content': self.reply}
        messages = [{'role': 'user', 'content': 'do the thing'}, {'role': 'assistant', 'content': 'done'}]
        self.assertEqual(titles.generate_summary(Provider('{"summary": "ok"}'), messages), '')
        self.assertEqual(titles.generate_summary(Provider('{"summary": "..."}'), messages), '')
        self.assertEqual(titles.generate_summary(Provider(SUMMARY), messages), SUMMARY_TEXT)
        # Nothing to summarise: no call is even needed.
        self.assertEqual(titles.generate_summary(Provider(SUMMARY), []), '')


class DigestTests(unittest.TestCase):
    def messages(self, count=40):
        out = [{'role': 'user', 'content': 'FIRSTPROMPT teach the panes to summarise themselves'}]
        for n in range(count):
            out.append({'role': 'assistant', 'content': f'reply {n} ' + 'x' * 2000,
                        'tool_calls': [{'id': f'c{n}', 'type': 'function',
                                        'function': {'name': 'write_file', 'arguments': '{}'}}]})
            out.append({'role': 'tool', 'tool_call_id': f'c{n}', 'content': 'y' * 5000})
            out.append({'role': 'user', 'content': f'LATERPROMPT {n} ' + 'z' * 2000})
        return out

    def test_the_digest_is_bounded_and_keeps_what_matters(self):
        messages = self.messages()
        messages.insert(1, {'role': 'user', 'relay_kind': 'summary',
                            'content': 'COMPACTED earlier work on the index'})
        body = titles.digest(messages, files=['src/a.cpp', 'src/b.cpp'],
                             todos=['write the docs', 'ship it'])
        self.assertLessEqual(len(body), titles.SUMMARY_DIGEST_CHARS)
        self.assertIn('FIRSTPROMPT', body)
        self.assertIn('COMPACTED', body)
        self.assertIn('src/a.cpp', body)
        self.assertIn('write the docs', body)
        self.assertIn('LATERPROMPT 39', body)        # the tail is what survives the cap
        self.assertNotIn('LATERPROMPT 10', body)
        # Tool results are not messages the summary reads; tool names ride the assistant line.
        self.assertNotIn('yyyyy', body)
        self.assertIn('write_file', body)

    def test_a_tiny_conversation_digests_to_almost_nothing(self):
        body = titles.digest([{'role': 'user', 'content': 'hello'}, {'role': 'assistant', 'content': 'hi'}])
        self.assertIn('First request:', body)
        self.assertIn('hello', body)
        self.assertEqual(titles.digest([]), '')
        self.assertLess(len(body), 200)

    def test_saved_inputs_and_has_reply(self):
        data = {'messages': [{'role': 'user', 'content': 'a'}, {'role': 'assistant', 'content': 'b'}],
                'checkpoints': {'items': [{'turn': 1, 'files': {'src/a.cpp': {}, 'src/b.cpp': {}}},
                                          {'turn': 2, 'files': {'src/a.cpp': {}}}]},
                'todos': {'items': [{'id': 't1', 'text': 'open one', 'status': 'pending'},
                                    {'id': 't2', 'text': 'done one', 'status': 'completed'}]}}
        inputs = titles.saved_inputs(data)
        self.assertEqual(inputs['files'], ['src/a.cpp', 'src/b.cpp'])
        self.assertEqual(inputs['todos'], ['open one'])
        self.assertTrue(titles.has_reply(inputs['messages']))
        self.assertFalse(titles.has_reply([{'role': 'user', 'content': 'a'}]))
        self.assertFalse(titles.has_reply([{'role': 'assistant', 'content': ''}]))
        self.assertEqual(titles.approx_tokens(4000), 1000)


class CadenceRuleTests(unittest.TestCase):
    def test_first_reply_then_only_when_the_work_moves_on(self):
        self.assertFalse(titles.summary_due(0, 0, False))
        self.assertFalse(titles.summary_due(1, 0, False, has_reply=False))
        self.assertTrue(titles.summary_due(1, 0, False))
        for turn in range(2, 1 + titles.SUMMARY_REFRESH_TURNS):
            self.assertFalse(titles.summary_due(turn, 1, False), turn)
        self.assertTrue(titles.summary_due(1 + titles.SUMMARY_REFRESH_TURNS, 1, False))
        # A compaction moves the work on by itself.
        self.assertTrue(titles.summary_due(2, 1, True))
        # Rewound below the last summary: nothing new happened.
        self.assertFalse(titles.summary_due(3, 8, True))


class BranchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_branch_worktree_detached_and_no_repository(self):
        repo = self.root / 'repo'
        (repo / '.git').mkdir(parents=True)
        (repo / 'pkg').mkdir()
        (repo / '.git' / 'HEAD').write_text('ref: refs/heads/feature/summaries\n')
        self.assertEqual(session_files.git_branch(repo), 'feature/summaries')
        # A directory inside the repository reports the repository's branch.
        self.assertEqual(session_files.git_branch(repo / 'pkg'), 'feature/summaries')
        # Detached HEAD: the short commit stands in for a name.
        (repo / '.git' / 'HEAD').write_text('9f1c0de4b7a35c21f0b8e0c9d1a2b3c4d5e6f708\n')
        self.assertEqual(session_files.git_branch(repo), '9f1c0de4b7a3')
        # A worktree's .git is a file pointing at the real directory.
        tree = self.root / 'tree'
        tree.mkdir()
        real = self.root / 'repo' / '.git' / 'worktrees' / 'tree'
        real.mkdir(parents=True)
        (real / 'HEAD').write_text('ref: refs/heads/side\n')
        (tree / '.git').write_text(f'gitdir: {real}\n')
        self.assertEqual(session_files.git_branch(tree), 'side')
        # No repository anywhere above: no branch, no error.
        plain = self.root / 'plain'
        plain.mkdir()
        self.assertEqual(session_files.git_branch(plain), '')


class SummaryProvider(ScriptedProvider):
    """Answers title calls with a title and summary calls with a summary, recording both."""
    def __init__(self, *args, summary_reply=SUMMARY, title_reply='{"title": "Fixing the index"}', **kwargs):
        super().__init__(*args, **kwargs)
        self.summary_reply = summary_reply
        self.title_reply = title_reply

    def complete(self, messages, tools, emit, cancel):
        if not tools:
            self.side_requests.append(json.loads(json.dumps(messages)))
            system = messages[0].get('content')
            reply = self.summary_reply if system == titles.SUMMARY_SYSTEM else self.title_reply
            return {'role': 'assistant', 'content': reply}
        return super().complete(messages, tools, emit, cancel)

    def summary_calls(self):
        return [m for m in self.side_requests if m and m[0].get('content') == titles.SUMMARY_SYSTEM]


class BrokenSummaryProvider(SummaryProvider):
    def complete(self, messages, tools, emit, cancel):
        if not tools and messages[0].get('content') == titles.SUMMARY_SYSTEM:
            self.side_requests.append(json.loads(json.dumps(messages)))
            raise ProviderError('no key for this provider')
        return super().complete(messages, tools, emit, cancel)


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        patcher = mock.patch.dict(os.environ, {'XDG_DATA_HOME': str(self.home)})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.ws = self.home / 'ws'
        self.ws.mkdir()
        self.sessions = self.home / 'sessions'
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)
        self.cmds = SessionCommands(self.sup, self.rec)
        # worker.py wires the same hook: a finished main turn may be owed a title and a summary.
        self.rec.hook = self.cmds.observe
        self.addCleanup(self.sup.shutdown)
        # Summaries run on their own threads; let them finish before the directory goes away.
        self.addCleanup(self.drain)

    BACKGROUND = ('relay-session_summary', 'relay-conversation_summary', 'relay-conversations_summarize',
                  'relay-session_title')

    def drain(self):
        for _ in range(400):
            running = [t for t in threading.enumerate() if t.name in self.BACKGROUND]
            if not running:
                return
            running[0].join(0.05)

    def saved_session(self, store, prompt='index the scrollback', turns=1, summary=None):
        """One saved session nobody has open, as a worker would have left it."""
        session_id = session_files.new_id()
        now = time.time()
        data = {'version': 1, 'kind': 'relay_session', 'id': session_id, 'title': prompt[:40],
                'created': now, 'updated': now, 'workspace': str(self.ws), 'model': 'm',
                'turns': turns, 'epoch': 0, 'summary': summary or '',
                'messages': [{'role': 'user', 'content': prompt},
                             {'role': 'assistant', 'content': 'Indexed it, and wrote the reconcile hook.'}],
                'snapshots': {}, 'checkpoints': {'next_turn': turns + 1, 'items': [
                    {'turn': 1, 'prompt': prompt, 'prompt_preview': prompt[:20], 'time': now,
                     'locations': {'0': 1}, 'files': {'src/ConvIndex.cpp': {'before': None, 'after': 'a' * 64}}}]},
                'open_requests': 0, 'models': ['m'], 'usage': {}}
        store.save(data)
        return session_id

    def make_agent(self, provider, session_dir=None):
        agent = Agent(ProviderConfig('http://127.0.0.1:1/v1', 'm', ''), str(self.ws), self.sup.agent_emit,
                      provider=provider, session_dir=str(session_dir or self.sessions))
        self.sup.set_agent(agent)
        return agent

    def run_turn(self, prompt):
        before = len(self.rec.of('agent_finished'))
        self.sup.submit(prompt, 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and len(self.rec.of('agent_finished')) > before)

    def wait_summaries(self, count):
        self.rec.wait(lambda _e: len([e for e in self.rec.of('session_summary') if e['summary']]) >= count)
        return [e for e in self.rec.of('session_summary') if e['summary']]

    def settle(self):
        """No summary or title call is owed and none is running (both of them save the session)."""
        pause = threading.Event()
        for _ in range(500):
            agent = self.sup.agent
            if (agent is not None and not agent._summary_running and not agent.summary_due()
                    and not agent._title_running and not agent.title_due()):
                self.drain()
                return
            pause.wait(0.01)
        self.fail('a summary call never settled')


class CadenceTests(Base):
    def test_written_after_the_first_reply_then_only_as_work_moves_on(self):
        provider = SummaryProvider()
        agent = self.make_agent(provider)
        self.run_turn('teach the panes to summarise themselves')
        event = self.wait_summaries(1)[0]
        self.assertEqual((event['summary'], event['turn'], event['session_id']),
                         (SUMMARY_TEXT, 1, agent.session_id))
        self.assertEqual(len(provider.summary_calls()), 1)
        # The next few turns cost no summary call at all.
        for turn in range(2, 1 + titles.SUMMARY_REFRESH_TURNS):
            self.run_turn(f'turn {turn}')
            self.settle()
            self.assertEqual(len(provider.summary_calls()), 1, f'turn {turn} asked for a summary')
        provider.summary_reply = '{"summary": "Ship the release notes. Wrote the 0.1 draft. Left: the screenshots."}'
        self.run_turn('now the release notes')
        self.wait_summaries(2)
        self.assertEqual(len(provider.summary_calls()), 2)
        self.assertEqual(agent.summary,
                         'Ship the release notes. Wrote the 0.1 draft. Left: the screenshots.')
        self.assertEqual(agent.summary_turn, 1 + titles.SUMMARY_REFRESH_TURNS)
        self.assertGreater(agent.summary_time, 0)

    def test_a_compaction_makes_a_fresh_summary_due(self):
        provider = SummaryProvider()
        agent = self.make_agent(provider)
        self.run_turn('one')
        self.wait_summaries(1)
        self.run_turn('two')
        self.settle()
        self.assertEqual(len(provider.summary_calls()), 1)
        agent.compact('manual')
        self.assertTrue(agent._summary_stale)
        provider.summary_reply = '{"summary": "Tidy the transcript. Compacted the history. Left: nothing."}'
        self.run_turn('three')
        self.wait_summaries(2)
        self.assertEqual(agent.summary, 'Tidy the transcript. Compacted the history. Left: nothing.')

    def test_never_two_summary_calls_at_once(self):
        agent = self.make_agent(SummaryProvider())
        self.run_turn('one')
        self.wait_summaries(1)
        first = agent.claim_summary(force=True)
        self.assertIsNotNone(first)
        self.assertIsNone(agent.claim_summary(force=True))
        self.assertIsNone(agent.claim_summary())
        # And the protocol hook is a no-op while one is in flight.
        self.cmds.maybe_summary()
        agent.release_summary('', first)
        self.assertFalse(agent._summary_running)

    def test_a_failed_call_keeps_the_summary_there_already_is(self):
        provider = SummaryProvider()
        agent = self.make_agent(provider)
        self.run_turn('one')
        self.wait_summaries(1)
        agent.provider = None
        broken = BrokenSummaryProvider()
        self.sup.agent.provider = broken
        self.sup.agent._injected_provider = broken
        for turn in range(2, 2 + titles.SUMMARY_REFRESH_TURNS):
            self.run_turn(f'turn {turn}')
        self.settle()
        self.assertGreaterEqual(len(broken.summary_calls()), 1)
        self.assertEqual(agent.summary, SUMMARY_TEXT)   # the old one stands
        # The cadence moved on, so a dead provider is not asked again after every turn.
        calls = len(broken.summary_calls())
        self.run_turn('again')
        self.settle()
        self.assertEqual(len(broken.summary_calls()), calls)

    def test_a_new_conversation_drops_the_summary(self):
        agent = self.make_agent(SummaryProvider())
        self.run_turn('one')
        self.wait_summaries(1)
        self.sup.reset()
        self.assertEqual((agent.summary, agent.summary_turn), ('', 0))
        self.assertEqual(self.rec.of('session_summary')[-1]['summary'], '')


class StorageTests(Base):
    def test_the_summary_and_branch_round_trip_through_save_listing_and_resume(self):
        (self.ws / '.git').mkdir()
        (self.ws / '.git' / 'HEAD').write_text('ref: refs/heads/summaries\n')
        provider = SummaryProvider()
        agent = self.make_agent(provider)
        self.run_turn('teach the panes to summarise themselves')
        self.wait_summaries(1)
        self.settle()
        session_id = agent.session_id
        saved = json.loads((self.sessions / f'{session_id}.json').read_text())
        self.assertEqual(saved['summary'], SUMMARY_TEXT)
        self.assertEqual(saved['summary_turn'], 1)
        self.assertEqual(saved['branch'], 'summaries')
        self.assertGreater(saved['summary_time'], 0)
        meta = json.loads((self.sessions / f'{session_id}.meta.json').read_text())
        self.assertEqual((meta['summary'], meta['branch'], meta['summary_turn']), (SUMMARY_TEXT, 'summaries', 1))
        listed = {item['id']: item for item in agent.store.listing()}
        self.assertEqual(listed[session_id]['summary'], SUMMARY_TEXT)
        self.assertEqual(listed[session_id]['branch'], 'summaries')
        # A resumed pane puts the summary back before the first new turn.
        self.make_agent(SummaryProvider(summary_reply='{"summary": "Something else entirely here."}'))
        self.cmds.handle('resume', {'id': session_id})
        self.rec.wait(lambda e: e['event'] == 'state_loaded')
        resumed = self.sup.agent
        self.assertEqual((resumed.summary, resumed.summary_turn, resumed.branch),
                         (SUMMARY_TEXT, 1, 'summaries'))
        event = self.rec.of('session_summary')[-1]
        self.assertEqual((event['summary'], event['session_id']), (SUMMARY_TEXT, session_id))

    def test_an_autosave_does_not_drop_a_summary_written_while_nobody_had_it_open(self):
        agent = self.make_agent(SummaryProvider(summary_reply='{"summary": "not used"}'))
        self.run_turn('one')
        self.settle()
        session_id = agent.session_id
        conv_index.write_user_fields(self.sessions, session_id, summary='Written from elsewhere entirely.')
        agent.summary = ''      # as if this pane had never had one
        agent.autosave()
        meta = json.loads((self.sessions / f'{session_id}.meta.json').read_text())
        self.assertEqual(meta['summary'], 'Written from elsewhere entirely.')
        # The pane's own summary is the newer one once it has written it.
        agent.set_summary('Fix the index going stale. Added a reconcile. Left: the docs.')
        meta = json.loads((self.sessions / f'{session_id}.meta.json').read_text())
        self.assertEqual(meta['summary'], 'Fix the index going stale. Added a reconcile. Left: the docs.')


class OnDemandTests(Base):
    def test_a_saved_session_is_summarised_into_its_meta_file_only(self):
        provider = SummaryProvider()
        agent = self.make_agent(provider)
        self.run_turn('the pane keeps its own conversation')
        self.settle()
        other = SessionStore(self.home / 'others', index=False)
        session_id = self.saved_session(other)
        before = (other.directory / f'{session_id}.json').read_text()
        self.cmds.handle('conversation_summarize',
                         {'id': 'q1', 'session_id': session_id, 'session_dir': str(other.directory)})
        event = self.rec.wait(lambda e: e['event'] == 'conversation_summary' and e.get('id') == 'q1')
        self.assertEqual(event['summary'], SUMMARY_TEXT)
        self.assertEqual(event['session_id'], session_id)
        self.assertNotIn('error', event)
        # The session file is another worker's to write; only the meta file changed.
        self.assertEqual((other.directory / f'{session_id}.json').read_text(), before)
        meta = json.loads((other.directory / f'{session_id}.meta.json').read_text())
        self.assertEqual(meta['summary'], SUMMARY_TEXT)
        # The digest came from that session, not from this pane's conversation.
        digest = provider.summary_calls()[-1][-1]['content']
        self.assertIn('index the scrollback', digest)
        self.assertIn('src/ConvIndex.cpp', digest)
        # This pane's own summary is untouched.
        self.assertNotEqual(agent.session_id, session_id)

    def test_the_session_this_pane_holds_is_summarised_in_place(self):
        agent = self.make_agent(SummaryProvider())
        self.run_turn('teach the panes to summarise themselves')
        self.wait_summaries(1)
        agent.summary = ''
        self.cmds.handle('conversation_summarize', {'id': 'q2', 'session_id': agent.session_id})
        event = self.rec.wait(lambda e: e['event'] == 'conversation_summary' and e.get('id') == 'q2')
        self.assertEqual((event['summary'], event['live']), (SUMMARY_TEXT, True))
        self.assertEqual(agent.summary, SUMMARY_TEXT)
        self.assertTrue([e for e in self.rec.of('session_summary') if e['summary'] == SUMMARY_TEXT])

    def test_an_unreadable_session_answers_with_an_error_not_an_exception(self):
        self.make_agent(SummaryProvider())
        self.run_turn('one')
        missing = session_files.new_id()
        self.cmds.handle('conversation_summarize',
                         {'id': 'q3', 'session_id': missing, 'session_dir': str(self.home / 'others')})
        event = self.rec.wait(lambda e: e['event'] == 'conversation_summary' and e.get('id') == 'q3')
        self.assertIn('No saved session', event['error'])
        with self.assertRaises(ValueError):
            self.cmds.handle('conversation_summarize', {'session_id': 'not-an-id'})


class BatchTests(Base):
    def prepare(self, count=3, provider=None):
        """A pane, plus `count` saved sessions of its own with no summary."""
        agent = self.make_agent(provider or SummaryProvider())
        store = agent.store
        ids = [self.saved_session(store, prompt=f'saved session {n}') for n in range(count)]
        return agent, store, ids

    def test_the_estimate_counts_only_sessions_that_have_no_summary(self):
        agent, store, ids = self.prepare()
        store.note_summary(ids[0], 'Already summarised. Nothing more to do here. Left: nothing.')
        self.saved_session(store, prompt='never ran', turns=0)
        self.cmds.handle('conversations_summarize_estimate', {'id': 'e1', 'scope': 'project'})
        event = self.rec.wait(lambda e: e['event'] == 'conversations_summarize_estimate')
        self.assertEqual(event['count'], 2)            # two of the three, and never the 0-turn one
        self.assertEqual(event['sessions'], 4)
        self.assertGreater(event['approx_input_tokens'], 0)
        self.assertEqual(event['approx_output_tokens'], 2 * titles.SUMMARY_OUTPUT_TOKENS)
        self.assertEqual(event['model'], agent.config.model)
        with self.assertRaises(ValueError):
            self.cmds.handle('conversations_summarize_estimate', {'scope': 'everything'})

    def test_a_batch_reports_progress_and_finishes(self):
        agent, store, ids = self.prepare()
        self.cmds.handle('conversations_summarize_all', {'id': 'b1', 'scope': 'project'})
        final = self.rec.wait(lambda e: e['event'] == 'conversations_summarize_progress' and e.get('finished'),
                              timeout=10)
        self.assertEqual((final['done'], final['total'], final['failed']), (3, 3, 0))
        self.assertFalse(final['cancelled'])
        steps = [e for e in self.rec.of('conversations_summarize_progress') if not e.get('finished')]
        self.assertEqual([s['done'] for s in steps], [1, 2, 3])
        self.assertEqual({s['session_id'] for s in steps}, set(ids))
        for session_id in ids:
            meta = json.loads((store.directory / f'{session_id}.meta.json').read_text())
            self.assertEqual(meta['summary'], SUMMARY_TEXT)
        # Everything is summarised now, so a second estimate has nothing left to do.
        self.cmds.handle('conversations_summarize_estimate', {'id': 'e2'})
        event = self.rec.wait(lambda e: e['event'] == 'conversations_summarize_estimate')
        self.assertEqual(event['count'], 0)

    def test_only_one_batch_at_a_time_and_cancel_stops_it(self):
        gate = threading.Event()

        class SlowProvider(SummaryProvider):
            def complete(self, messages, tools, emit, cancel):
                if not tools and messages[0].get('content') == titles.SUMMARY_SYSTEM:
                    gate.wait(5)
                return super().complete(messages, tools, emit, cancel)
        agent, store, ids = self.prepare(count=3, provider=SlowProvider())
        self.cmds.handle('conversations_summarize_all', {'id': 'b2', 'scope': 'project'})
        with self.assertRaises(ValueError):
            self.cmds.handle('conversations_summarize_all', {'id': 'b3', 'scope': 'project'})
        self.cmds.handle('conversations_summarize_cancel', {'id': 'c1'})
        cancelled = self.rec.wait(lambda e: e['event'] == 'conversations_summarize_cancelled')
        self.assertTrue(cancelled['running'])
        gate.set()
        final = self.rec.wait(lambda e: e['event'] == 'conversations_summarize_progress' and e.get('finished'),
                              timeout=10)
        # It stops after the session it was on, not part way through it.
        self.assertTrue(final['cancelled'])
        self.assertEqual(final['done'], 1)
        self.assertEqual(final['total'], 3)
        # The batch is over, so another one may start.
        self.cmds.handle('conversations_summarize_cancel', {'id': 'c2'})
        self.assertFalse(self.rec.wait(lambda e: e['event'] == 'conversations_summarize_cancelled'
                                       and e.get('id') == 'c2')['running'])

    def test_a_batch_honours_its_limit(self):
        agent, store, ids = self.prepare(count=4)
        self.cmds.handle('conversations_summarize_all', {'id': 'b4', 'limit': 2})
        final = self.rec.wait(lambda e: e['event'] == 'conversations_summarize_progress' and e.get('finished'),
                              timeout=10)
        self.assertEqual((final['done'], final['total']), (2, 2))
        with self.assertRaises(ValueError):
            self.cmds.handle('conversations_summarize_all', {'limit': 'lots'})


if __name__ == '__main__':
    unittest.main()
