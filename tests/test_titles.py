"""Model-written pane titles and tab labels (issue JRWQ, protocol section 18).

Covers the refresh cadence, that a title the user typed is never overwritten, that a title comes
back with a resumed session, and the fallback when no model answers. Fake providers only; no
network. The GUI-side rules are tested in tests/panetitles_test.cpp.
"""
import json
import sys
import tempfile
import threading
import unittest
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

    def test_tab_labels_join_unrelated_panes_with_a_semicolon(self):
        panes = ['Fixing pane drag', 'Release notes']
        self.assertFalse(titles.related_text(panes))
        self.assertEqual(titles.join(panes, False), 'Fixing pane drag; Release notes')
        self.assertTrue(titles.related_text(['Fixing pane drag', 'Pane drag drop zones']))
        self.assertEqual(titles.join(panes, True, 'Pane drag work'), 'Pane drag work')
        self.assertEqual(titles.distinct(['a', '', ' A ', 'b']), ['a', 'b'])
        # One pane, or none, needs no judgement at all.
        self.assertEqual(titles.label(None, ['Release notes'])['label'], 'Release notes')
        self.assertEqual(titles.label(None, [])['label'], '')

    def test_tab_label_without_a_model_uses_the_text_comparison(self):
        result = titles.label(None, ['Fixing pane drag', 'Release notes'])
        self.assertEqual((result['related'], result['source']), (False, 'text'))
        self.assertEqual(result['label'], 'Fixing pane drag; Release notes')


class SideCallProvider(ScriptedProvider):
    """Answers no-tools calls with `side_reply`, and records only the title calls."""
    def title_calls(self):
        return [m for m in self.side_requests if m and m[0].get('content') == titles.TITLE_SYSTEM]

    def label_calls(self):
        return [m for m in self.side_requests if m and m[0].get('content') == titles.LABEL_SYSTEM]


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
        # A provider that cannot answer is not asked again after every turn.
        self.run_turn('again')
        self.settle()
        self.assertEqual(len(provider.title_calls()), 1)

    def test_tab_label_asks_the_chores_role_whether_the_panes_are_on_one_job(self):
        provider = SideCallProvider(side_reply='{"related": true, "label": "Pane titles and tabs"}')
        self.make_agent(provider)
        self.cmds.handle('tab_label', {'id': 't1', 'titles': ['Fixing pane drag', 'Release notes']})
        event = self.rec.wait(lambda e: e['event'] == 'tab_label' and e.get('id') == 't1')
        self.assertEqual((event['label'], event['related'], event['source']),
                         ('Pane titles and tabs', True, 'model'))
        self.assertEqual(len(provider.label_calls()), 1)
        # Unrelated panes keep their own titles, joined with a semicolon.
        provider.side_reply = '{"related": false}'
        self.cmds.handle('tab_label', {'id': 't2', 'titles': ['Fixing pane drag', 'Release notes']})
        event = self.rec.wait(lambda e: e['event'] == 'tab_label' and e.get('id') == 't2')
        self.assertEqual(event['label'], 'Fixing pane drag; Release notes')
        with self.assertRaises(ValueError):
            self.cmds.handle('tab_label', {'titles': 'not a list'})

    def test_a_failed_tab_label_call_falls_back_to_the_text_comparison(self):
        self.make_agent(BrokenSideProvider())
        self.cmds.handle('tab_label', {'id': 't3', 'titles': ['Fixing pane drag', 'Pane drag zones']})
        event = self.rec.wait(lambda e: e['event'] == 'tab_label' and e.get('id') == 't3')
        self.assertEqual((event['related'], event['source'], event['label']),
                         (True, 'text', 'Fixing pane drag'))

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
