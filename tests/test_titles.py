"""Model-written pane titles (issue JRWQ, protocol section 18).

Covers the refresh cadence, that a title the user typed is never overwritten, that a title comes
back with a resumed session, and the fallback when no model answers. Fake providers only; no
network. Tab labels are the GUI's own since 2026-09-19 (protocol 18.3); the GUI-side rules are
tested in tests/panetitles_test.cpp.
"""
import json
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
from pathlib import Path

from relay_core import titles
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig, ProviderError
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands

sys.path.insert(0, str(Path(__file__).parent))
from test_sessions import ScriptedProvider  # noqa: E402
from test_queue import Recorder  # noqa: E402


class RuleTests(unittest.TestCase):
    def test_cadence_after_the_first_turn_then_only_when_work_moves_on(self):
        # Nothing before the first turn finishes, then always right after it.
        self.assertFalse(titles.due(0, '', 0, False))
        self.assertTrue(titles.due(1, '', 0, False))
        # Not on every turn: the work has to move on by REFRESH_TURNS turns.
        for turn in range(2, 1 + titles.REFRESH_TURNS):
            self.assertFalse(titles.due(turn, 'model', 1, False), turn)
        self.assertTrue(titles.due(1 + titles.REFRESH_TURNS, 'model', 1, False))
        # A compaction moves the work on by itself.
        self.assertTrue(titles.due(2, 'model', 1, True))
        # A name the user typed is never refreshed, whatever happened.
        self.assertFalse(titles.due(99, 'user', 1, True))
        # Rewound below the last title: nothing new to name.
        self.assertFalse(titles.due(3, 'model', 8, True))

    def test_a_model_reply_is_reduced_to_a_header_phrase(self):
        self.assertEqual(titles.clean('  "Fixing pane drag."\n'), 'Fixing pane drag')
        self.assertEqual(titles.clean('Title: Release notes'), 'Release notes')
        self.assertEqual(titles.clean('one two three four five six seven'), 'one two three four five six')
        self.assertEqual(titles.clean('x' * 200), 'x' * (titles.MAX_TITLE - 1) + '…')
        # A hand-set name keeps its words; only the length cap applies.
        self.assertEqual(titles.clean('one two three four five six seven', titles.MAX_USER_TITLE, max_words=0),
                         'one two three four five six seven')
        self.assertEqual(titles.clean(None), '')

    def test_the_fallback_is_todays_first_prompt_title(self):
        self.assertEqual(titles.fallback_title('  make   the tabs\nnicer '), 'make the tabs nicer')
        self.assertEqual(len(titles.fallback_title('word ' * 100)), 80)


class SideCallProvider(ScriptedProvider):
    """Answers no-tools calls with `side_reply`, and records only the title calls."""
    def title_calls(self):
        return [m for m in self.side_requests if m and m[0].get('content') == titles.TITLE_SYSTEM]


class BrokenSideProvider(SideCallProvider):
    def complete(self, messages, tools, emit, cancel):
        if not tools:
            self.side_requests.append(json.loads(json.dumps(messages)))
            raise ProviderError('no key for this provider')
        return super().complete(messages, tools, emit, cancel)


class SessionTitleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.ws = Path(self.temp.name) / 'ws'
        self.ws.mkdir()
        self.sessions = Path(self.temp.name) / 'sessions'
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)
        self.cmds = SessionCommands(self.sup, self.rec)
        # worker.py wires the same hook: a finished main turn may be owed a fresh pane title.
        self.rec.hook = self.cmds.observe
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(self.sup.shutdown)

    def make_agent(self, provider):
        agent = Agent(ProviderConfig('http://127.0.0.1:1/v1', 'm', ''), str(self.ws), self.sup.agent_emit,
                      provider=provider, session_dir=str(self.sessions))
        self.sup.set_agent(agent)
        return agent

    def run_turn(self, prompt):
        before = len(self.rec.of('agent_finished'))
        self.sup.submit(prompt, 'now')
        self.rec.wait(lambda e: e['event'] == 'agent_finished' and len(self.rec.of('agent_finished')) > before)

    def wait_titles(self, count):
        self.rec.wait(lambda _e: len(self.rec.of('session_title')) >= count)
        return self.rec.of('session_title')

    def settle(self):
        """No title call is owed and none is running."""
        deadline = threading.Event()
        for _ in range(500):
            agent = self.sup.agent
            if agent is not None and not agent._title_running and not agent.title_due():
                return
            deadline.wait(0.01)
        self.fail('a title call never settled')

    def test_title_written_after_the_first_turn_and_then_only_as_work_moves_on(self):
        provider = SideCallProvider(side_reply='{"title": "Fixing pane drag"}')
        agent = self.make_agent(provider)
        self.run_turn('please fix the pane drag')
        event = self.wait_titles(1)[0]
        self.assertEqual((event['title'], event['source'], event['session_id']),
                         ('Fixing pane drag', 'model', agent.session_id))
        self.assertEqual(len(provider.title_calls()), 1)
        # The next few turns cost no title call at all.
        for turn in range(2, 1 + titles.REFRESH_TURNS):
            self.run_turn(f'turn {turn}')
            self.settle()
            self.assertEqual(len(provider.title_calls()), 1, f'turn {turn} asked for a title')
        provider.side_reply = '{"title": "Release notes for 0.1"}'
        self.run_turn('now the release notes')
        self.wait_titles(2)
        self.assertEqual(len(provider.title_calls()), 2)
        self.assertEqual(agent.title, 'Release notes for 0.1')
        # The title is in the session file, so the conversation list and resume picker show it too.
        saved = json.loads((self.sessions / f'{agent.session_id}.json').read_text())
        self.assertEqual((saved['title'], saved['title_source']), ('Release notes for 0.1', 'model'))
        self.assertEqual(json.loads((self.sessions / f'{agent.session_id}.meta.json').read_text())['title'],
                         'Release notes for 0.1')

    def test_a_compaction_makes_a_fresh_title_due(self):
        provider = SideCallProvider(side_reply='{"title": "Fixing pane drag"}')
        agent = self.make_agent(provider)
        self.run_turn('one')
        self.wait_titles(1)
        self.run_turn('two')
        self.settle()
        self.assertEqual(len(provider.title_calls()), 1)
        agent.compact('manual')
        self.assertTrue(agent._title_stale)
        provider.side_reply = '{"title": "Tidying the transcript"}'
        self.run_turn('three')
        self.wait_titles(2)
        self.assertEqual(agent.title, 'Tidying the transcript')

    def test_a_title_the_user_typed_is_never_overwritten(self):
        provider = SideCallProvider(side_reply='{"title": "Fixing pane drag"}')
        agent = self.make_agent(provider)
        self.run_turn('one')
        self.wait_titles(1)
        self.cmds.handle('set_session_title', {'title': '  Deploy checklist ', 'id': 'r1'})
        event = self.rec.wait(lambda e: e['event'] == 'session_title' and e.get('id') == 'r1')
        self.assertEqual((event['title'], event['source']), ('Deploy checklist', 'user'))
        calls = len(provider.title_calls())
        for turn in range(2, 4 + titles.REFRESH_TURNS):
            self.run_turn(f'turn {turn}')
            self.settle()
        self.assertEqual(len(provider.title_calls()), calls)
        self.assertEqual((agent.title, agent.title_source), ('Deploy checklist', 'user'))
        # A model refresh that somehow arrives late is refused outright.
        self.assertEqual(agent.set_title('Fixing pane drag', 'model')['title'], 'Deploy checklist')
        self.assertEqual(agent.title, 'Deploy checklist')

    def test_clearing_a_hand_set_name_hands_it_back_to_the_model(self):
        provider = SideCallProvider(side_reply='{"title": "Fixing pane drag"}')
        agent = self.make_agent(provider)
        self.run_turn('one')
        self.wait_titles(1)
        self.cmds.handle('set_session_title', {'title': 'Mine'})
        self.assertEqual(agent.title_source, 'user')
        provider.side_reply = '{"title": "Back to automatic"}'
        self.cmds.handle('set_session_title', {'title': ''})
        self.rec.wait(lambda e: e['event'] == 'session_title' and e['title'] == 'Back to automatic')
        self.assertEqual(agent.title_source, 'model')
        with self.assertRaises(ValueError):
            self.cmds.handle('set_session_title', {'title': 17})

    def test_the_title_comes_back_with_a_resumed_session(self):
        provider = SideCallProvider(side_reply='{"title": "Fixing pane drag"}')
        agent = self.make_agent(provider)
        self.run_turn('one')
        self.wait_titles(1)
        self.cmds.handle('set_session_title', {'title': 'Deploy checklist'})
        session_id = agent.session_id
        self.make_agent(SideCallProvider(side_reply='{"title": "Something else"}'))
        self.cmds.handle('resume', {'id': session_id})
        self.rec.wait(lambda e: e['event'] == 'state_loaded')
        resumed = self.sup.agent
        self.assertEqual((resumed.title, resumed.title_source), ('Deploy checklist', 'user'))
        event = self.rec.of('session_title')[-1]
        self.assertEqual((event['title'], event['source'], event['session_id']),
                         ('Deploy checklist', 'user', session_id))
        # The resume picker lists the same text.
        listed = {item['id']: item['title'] for item in resumed.store.listing()}
        self.assertEqual(listed[session_id], 'Deploy checklist')

    def test_without_a_working_model_the_first_prompt_title_stands(self):
        provider = BrokenSideProvider()
        agent = self.make_agent(provider)
        self.run_turn('teach the panes to name themselves')
        event = self.wait_titles(1)[0]
        self.assertEqual((event['title'], event['source']),
                         ('teach the panes to name themselves', 'model'))
        self.assertEqual(agent.title_source, '')
        self.assertEqual(len(provider.title_calls()), 1)
        self.assertEqual(agent.title_turn, 0)
        self.assertTrue(agent.title_due())
        with self.assertLogs('relay.titles', level='INFO') as logged:
            self.run_turn('again')
            self.wait_titles(2)
        self.assertEqual(len(provider.title_calls()), 2)
        self.assertTrue(any('provider_call' in line and 'ProviderError' in line for line in logged.output))
        self.assertNotIn('no key for this provider', str(logged.output))

    def test_unusable_reply_keeps_fallback_then_recovers_next_turn(self):
        provider = SideCallProvider(side_reply='{"title": ""}')
        agent = self.make_agent(provider)
        with self.assertLogs('relay.titles', level='INFO') as logged:
            self.run_turn('please name this pane')
            self.wait_titles(1)
        self.assertIn('unusable_reply', str(logged.output))
        self.assertEqual(agent.title, 'please name this pane')
        self.assertEqual(agent.title_turn, 0)
        provider.side_reply = '{"title": "Fixing pane titles"}'
        self.run_turn('try again')
        self.wait_titles(2)
        self.assertEqual(agent.title, 'Fixing pane titles')
        self.assertEqual(agent.title_turn, 2)
        self.run_turn('next')
        self.settle()
        self.assertEqual(len(provider.title_calls()), 2)

    def test_provider_build_failure_retries_without_logging_exception_text(self):
        agent = self.make_agent(SideCallProvider(side_reply='{"title": "Naming panes"}'))
        with patch.object(self.cmds, 'maybe_summary'), patch.object(
                agent, 'side_provider', side_effect=ValueError('private provider text')):
            with self.assertLogs('relay.titles', level='INFO') as logged:
                self.run_turn('name this pane')
                self.wait_titles(1)
        self.assertIn('provider_build', str(logged.output))
        self.assertNotIn('private provider text', str(logged.output))
        self.assertTrue(agent.title_due())
        self.run_turn('retry')
        self.wait_titles(2)
        self.assertEqual(agent.title, 'Naming panes')

    def test_declining_guest_provider_is_logged_and_remains_due(self):
        provider = SideCallProvider()
        provider.serves_side_calls = False
        agent = self.make_agent(provider)
        with patch.object(self.cmds, 'maybe_summary'), patch.object(agent, 'side_provider', return_value=provider):
            with self.assertLogs('relay.titles', level='INFO') as logged:
                self.run_turn('name this pane')
                self.wait_titles(1)
        self.assertIn('side_provider_unavailable', str(logged.output))
        self.assertIn('guest=True', str(logged.output))
        self.assertTrue(agent.title_due())
        self.assertEqual(provider.title_calls(), [])

    def test_failed_refresh_preserves_existing_title_and_compaction_staleness(self):
        agent = self.make_agent(SideCallProvider(side_reply='{"title": "Naming panes"}'))
        self.run_turn('name this pane')
        self.wait_titles(1)
        self.run_turn('second turn')
        self.settle()
        agent._title_stale = True
        claim = agent.claim_title()
        self.assertIsNotNone(claim)
        agent.release_title('', claim)
        self.assertEqual((agent.title, agent.title_turn), ('Naming panes', 1))
        self.assertTrue(agent._title_stale)
        self.assertTrue(agent.title_due())

    def test_unusable_shapes_are_logged_without_reply_text(self):
        for reply in ('', '{"summary": "private reply"}', '{"title": 42}', '{"title": "x"}', '...'):
            with self.subTest(reply=reply), self.assertLogs('relay.titles', level='INFO') as logged:
                self.assertEqual(titles.generate(SideCallProvider(side_reply=reply),
                                                [{'role': 'user', 'content': 'private prompt'}]), '')
            self.assertIn('unusable_reply', str(logged.output))
            self.assertNotIn('private', str(logged.output))

    def test_a_new_conversation_drops_the_title(self):
        provider = SideCallProvider(side_reply='{"title": "Fixing pane drag"}')
        agent = self.make_agent(provider)
        self.run_turn('one')
        self.wait_titles(1)
        self.sup.reset()
        self.assertEqual((agent.title, agent.title_source, agent.title_turn), ('', '', 0))
        self.assertEqual(self.rec.of('session_title')[-1]['title'], '')


if __name__ == '__main__':
    unittest.main()
