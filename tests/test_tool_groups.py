"""Tool groups whose schemas arrive when the model asks for them (#GMCF decision 9).

Three groups — Relay's Options and actions, this pane's own session, the project's tests — are
8.6 KB of schemas that a minority of turns use. They are named in one line of the prompt and
fetched with `load_tools`, appended to the end of the tool list; calling one of the names before
its group is loaded is refused with a sentence saying how to load it.

The test that matters most is the last one: what a load does to the *prefix* of the tool list. A
group that appeared in the middle would cost every provider's cached prefix, which is the whole
reason the schemas are held back rather than simply sent.
"""
import json
import tempfile
import unittest
from pathlib import Path

from relay_core import activity_tools, app_tools, board as board_mod, board_tools, tool_groups
from relay_core.agent import Agent
from relay_core.presets import Preset
from relay_core.provider import ProviderConfig

CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')
BOARD_CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""
APP = {"tab": "t1", "writes_enabled": True, "options": [], "actions": []}
LOCAL = Preset('local:bonsai', 'bonsai', 'http://127.0.0.1:8080/v1', 'bonsai-2-27b',
               context_window=131072, group='local', local=True)


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'ws').mkdir()
        (self.root / 'repo' / 'issues').mkdir(parents=True)
        (self.root / 'repo' / 'issues' / board_mod.BOARD_CONFIG).write_text(BOARD_CONFIG)

    def agent(self, *, board=True, provider=None, **kwargs) -> Agent:
        agent = Agent(CONFIG, str(self.root / 'ws'), lambda event: None,
                      provider=provider or object(), **kwargs)
        agent.app = app_tools.AppTools(app_tools.AppCatalog.from_request(APP),
                                       app_tools.AppBridge(lambda event: None))
        # `live_info` is the pane's own session_info payload (§30.5); a stub here, so the
        # scripted conversation below can actually call the tool it loads.
        agent.activity = activity_tools.ActivityTools(
            agent, live_info=lambda: {'event': 'session_info', 'turns': 0, 'model': 'mock'})
        if board:
            agent.board = board_tools.BoardTools(
                board_mod.Board(self.root / 'repo' / 'issues', self.root / 'repo'),
                emit=lambda e: None, autonomy='auto', state_path=self.root / 'rate.json',
                pane_token='t' * 36,
                context=board_tools.ToolContext(actor='agent', model='m', pane='1'))
        agent.refresh_system_prompt()
        return agent

    def names(self, agent) -> list[str]:
        return [t['function']['name'] for t in agent.tools()]


class DeferralTests(Base):
    def test_the_groups_are_named_in_one_line_and_their_schemas_are_not_sent(self):
        agent = self.agent()
        names = self.names(agent)
        for held in ('app_option_set', 'session_info', 'tests_run'):
            self.assertNotIn(held, names)
        self.assertIn('load_tools', names)
        line = [l for l in agent.system_prompt().splitlines() if 'load_tools' in l]
        self.assertEqual(len(line), 1, 'the rule is one line')
        for named in ('app_option_set', 'session_info', 'tests_run'):
            self.assertIn(named, line[0])

    def test_nothing_is_deferred_on_the_local_tier_or_the_short_profile(self):
        # Section 4.1: there the tool list is rendered above the system prompt, so every load
        # re-prefills the whole request — the profile switch is what handles that tier.
        local = self.agent()
        local.preset = LOCAL
        self.assertEqual(local._deferred_groups(), ())
        self.assertNotIn('load_tools', self.names(local))
        short = self.agent(prompt_profile='short')
        self.assertEqual(short._deferred_groups(), ())
        self.assertNotIn('load_tools', self.names(short))

    def test_a_group_with_nothing_wired_up_is_not_offered(self):
        agent = self.agent(board=False)
        self.assertNotIn('tests', agent._deferred_groups())
        self.assertNotIn('tests_run', agent.system_prompt())


class LoadTests(Base):
    def test_calling_a_deferred_tool_first_is_refused_with_the_group_named(self):
        agent = self.agent()
        for name, group in (('app_option_list', 'app'), ('activity', 'own_session'),
                            ('tests_check', 'tests')):
            with self.assertRaises(ValueError) as caught:
                agent._prepare(name, {})
            message = str(caught.exception)
            self.assertIn(name, message)
            self.assertIn(f'group="{group}"', message)

    def test_loading_a_group_brings_its_schemas_and_lets_it_be_called(self):
        agent = self.agent()
        result = agent._execute(agent._prepare('load_tools', {'group': 'own_session'}), {})
        self.assertEqual(result['loaded'], 'own_session')
        self.assertEqual(result['tools'], ['session_info', 'activity'])
        self.assertFalse(result['already_loaded'])
        self.assertIn('session_info', self.names(agent))
        self.assertEqual(agent._prepare('activity', {}).name, 'activity')
        # The other groups are still held back.
        self.assertNotIn('app_option_list', self.names(agent))
        again = agent._execute(agent._prepare('load_tools', {'group': 'own_session'}), {})
        self.assertTrue(again['already_loaded'])

    def test_a_bad_group_is_refused_and_a_new_conversation_forgets_the_loads(self):
        agent = self.agent()
        for bad in ({'group': 'everything'}, {}, {'group': 'app', 'extra': 1}):
            with self.assertRaises(ValueError):
                agent._prepare('load_tools', bad)
        agent._execute(agent._prepare('load_tools', {'group': 'app'}), {})
        self.assertIn('app_option_list', self.names(agent))
        agent.reset_conversation()
        self.assertEqual(agent.loaded_tool_groups, set())
        self.assertNotIn('app_option_list', self.names(agent))

    def test_a_load_only_appends_to_the_tool_list(self):
        # The point of the whole mechanism: what the provider has already read does not move.
        agent = self.agent()
        before = agent.tools()
        agent._execute(agent._prepare('load_tools', {'group': 'app'}), {})
        after = agent.tools()
        self.assertEqual(json.dumps(after[:len(before)], ensure_ascii=False),
                         json.dumps(before, ensure_ascii=False))
        self.assertEqual([t['function']['name'] for t in after[len(before):]],
                         list(tool_groups.GROUPS['app'][0]))
        # And the system prompt does not move at all: the rule names the groups either way.
        prompt = agent.system_prompt()
        agent._execute(agent._prepare('load_tools', {'group': 'tests'}), {})
        self.assertEqual(agent.system_prompt(), prompt)
        self.assertEqual([t['function']['name'] for t in agent.tools()[:len(before)]],
                         [t['function']['name'] for t in before])


class HelperTests(Base):
    """A tab's helper (30.7) never defers: it is the agent the app tools exist for.

    Deferral is a pane's bargain. The helper is asked from Options, Actions and Sessions, so its
    first action is an app call every time and holding the schemas back buys it nothing but a round
    trip. The board is not what tells the two apart — a tab with no project attached has no board —
    so the flag `_build_page_agent` sets is, and these pin both sides of it.
    """

    def helper(self, **kwargs) -> Agent:
        agent = self.agent(helper=True, **kwargs)
        agent.refresh_system_prompt()
        return agent

    def test_a_helper_is_eager_with_a_board_and_without_one(self):
        for board in (True, False):
            with self.subTest(board=board):
                agent = self.helper(board=board)
                self.assertEqual(agent._deferred_groups(), ())
                names = self.names(agent)
                for eager in ('app_option_list', 'app_action_run', 'session_info', 'activity'):
                    self.assertIn(eager, names)
                # Nothing is held back, so there is nothing to fetch and no rule line to say so.
                self.assertNotIn('load_tools', names)
                self.assertNotIn('load_tools', agent.system_prompt())
                # The app group's own rules stay in the prompt, where they were before decision 9.
                self.assertIn(app_tools.prompt_section(agent.app), agent.system_prompt())

    def test_a_board_attached_helper_is_eager_in_its_chat_scope_too(self):
        # The scope branch of `Agent.tools` hands over the app tools whatever the flag says, so
        # what this pins is the prompt it was built with: it used to carry the one-line rule for a
        # `load_tools` tool that branch never offers.
        agent = self.helper()
        agent.board.begin_chat_turn()
        self.addCleanup(agent.board.end_chat_turn)
        self.assertEqual(agent._deferred_groups(), ())
        self.assertNotIn('load_tools', agent.messages[0]['content'])
        self.assertIn('app_option_set', agent.messages[0]['content'])

    def test_an_ordinary_pane_still_defers(self):
        agent = self.agent()
        self.assertEqual(agent._deferred_groups(), ('app', 'own_session', 'tests'))
        names = self.names(agent)
        self.assertIn('load_tools', names)
        for held in ('app_option_list', 'session_info', 'tests_run'):
            self.assertNotIn(held, names)


class ConversationTests(Base):
    """One scripted turn: the model reaches for a deferred tool, is told how to get it, loads it,
    and calls it — which is the round trip the prompt line promises."""

    def test_the_model_is_told_to_load_the_group_and_then_the_call_works(self):
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from test_sessions import ScriptedProvider, call, text, tools_msg
        provider = ScriptedProvider([
            tools_msg(call('session_info', {}, 'c1')),
            tools_msg(call('load_tools', {'group': 'own_session'}, 'c2')),
            tools_msg(call('session_info', {}, 'c3')),
            text('**Done:** this conversation is on the mock model.')])
        agent = self.agent(provider=provider)
        agent.ask('how much context is left?')
        results = [json.loads(m['content']) for m in agent.messages if m.get('role') == 'tool']
        self.assertIn('load_tools with group="own_session"', results[0]['error'])
        self.assertEqual(results[1]['loaded'], 'own_session')
        self.assertNotIn('error', results[2])
        # The request that followed the load is the first one to carry the schema.
        offered = [names for _messages, names in provider.requests]
        self.assertNotIn('session_info', offered[0])
        self.assertNotIn('session_info', offered[1])
        self.assertIn('session_info', offered[2])
        self.assertEqual(offered[2][:len(offered[1])], offered[1])


if __name__ == '__main__':
    unittest.main()
