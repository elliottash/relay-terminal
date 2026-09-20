"""The short prompt profile: what a model that pays for the prompt in seconds is sent.

#GMCF decision 7. `relay_core.prompt_profiles` holds the text and the eight tools; these tests pin
the three things that can silently go wrong with it — the wrong profile being chosen, a hard rule
of `SYSTEM` being contradicted rather than omitted, and the size creeping back up.
"""
import json
import tempfile
import unittest
from pathlib import Path

from relay_core import activity_tools, app_tools, board as board_mod, board_tools, prompt_profiles
from relay_core import instructions as instructions_mod, skills as skills_mod
from relay_core.agent import SYSTEM, Agent
from relay_core.presets import PRESETS, Preset
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


class ResolutionTests(unittest.TestCase):
    def test_a_pinned_profile_wins_over_everything(self):
        for pinned in ('full', 'short'):
            self.assertEqual(prompt_profiles.resolve(pinned, preset=LOCAL, context_window=1000000), pinned)

    def test_auto_is_short_on_a_local_endpoint_whatever_its_window(self):
        # The Local tier is the one where the prompt is paid in seconds, and Bonsai's window is
        # 131k: the window alone would have called it a big model (§3.2).
        self.assertEqual(prompt_profiles.resolve('auto', preset=LOCAL, context_window=131072), 'short')

    def test_auto_is_short_for_a_small_window_and_full_for_a_large_one(self):
        hosted = PRESETS['kimi']
        self.assertEqual(prompt_profiles.resolve('auto', preset=hosted, context_window=32768), 'short')
        self.assertEqual(prompt_profiles.resolve('auto', preset=hosted, context_window=32769), 'full')
        self.assertEqual(prompt_profiles.resolve('auto', preset=hosted, context_window=None), 'full')

    def test_a_value_that_is_not_one_of_the_three_is_refused(self):
        self.assertEqual(prompt_profiles.validate(None), 'auto')
        for bad in ('tiny', '', True, 3):
            with self.assertRaises(ValueError):
                prompt_profiles.validate(bad)


class TextTests(unittest.TestCase):
    """The short prompt may leave a rule out; it may not say the opposite of one."""

    def test_every_hard_rule_of_the_full_prompt_is_here_too(self):
        for rule in ('Never type into a password or passphrase prompt.',
                     'Never take destructive or irreversible action the user did not ask for.',
                     'Do not read secret files or upload data to third parties.',
                     'Never claim that you ran a command or changed a file unless a successful tool result proves it.'):
            self.assertIn(rule, SYSTEM, 'the full prompt lost a hard rule')
            self.assertIn(rule, prompt_profiles.SYSTEM_SHORT, f'the short prompt dropped: {rule}')

    def test_it_keeps_the_decisions_the_full_prompt_records(self):
        short = prompt_profiles.SYSTEM_SHORT
        # #TN4P: no prompt text may gate a terminal command on being asked. The words are the full
        # prompt's own, so `eval-requests.py`'s `act_unasked` probe reads both profiles.
        self.assertIn('expected to act', short)
        self.assertIn('expected to act', SYSTEM)
        self.assertIn('without waiting to be told each one', short)
        self.assertNotIn('when asked', short.lower())
        # The renderer's two rules: the folder slash (owner report 2026-09-19) and the three labels.
        self.assertIn("trailing `/`", short)
        self.assertIn('**Done:**', short)
        # One sentence per line, as SYSTEM is (2026-09-18).
        for line in short.splitlines():
            self.assertLessEqual(line.count('. '), 1, line)

    def test_it_stays_within_its_budget(self):
        size = len(prompt_profiles.SYSTEM_SHORT.encode('utf-8'))
        # 1,662 B measured 2026-09-20; the budget is ~1,500 tokens of prompt in all (§3.2), of
        # which the skills line and the project's instruction files take the rest.
        self.assertLess(size, 2 * 1024, f'the short prompt grew to {size} bytes')


class AgentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / 'ws').mkdir()
        (self.root / 'ws' / 'AGENTS.md').write_text('Project rule: run the tests.\n')
        library = self.root / 'skills'
        (library / 'alpha').mkdir(parents=True)
        (library / 'alpha' / 'SKILL.md').write_text(
            '---\nname: alpha\ndescription: Renames a project everywhere in one pass.\n---\nBody\n')
        (self.root / 'repo' / 'issues').mkdir(parents=True)
        (self.root / 'repo' / 'issues' / board_mod.BOARD_CONFIG).write_text(BOARD_CONFIG)
        self.library = library

    def tearDown(self):
        self.temp.cleanup()

    def agent(self, **kwargs) -> Agent:
        agent = Agent(CONFIG, str(self.root / 'ws'), lambda event: None, provider=object(), **kwargs)
        agent.executor.skills = skills_mod.SkillIndex.load([self.library])
        agent.instructions = instructions_mod.load({'project_auto': True}, str(self.root / 'ws'))
        agent.app = app_tools.AppTools(app_tools.AppCatalog.from_request(APP),
                                       app_tools.AppBridge(lambda event: None))
        agent.activity = activity_tools.ActivityTools(agent)
        agent.board = board_tools.BoardTools(
            board_mod.Board(self.root / 'repo' / 'issues', self.root / 'repo'), emit=lambda e: None,
            autonomy='auto', state_path=self.root / 'rate.json', pane_token='t' * 36,
            context=board_tools.ToolContext(actor='agent', model='m', pane='1'))
        agent.refresh_system_prompt()
        return agent

    def names(self, agent) -> list[str]:
        return [t['function']['name'] for t in agent.tools()]

    def test_a_small_window_picks_the_short_profile_on_its_own(self):
        agent = self.agent(context_window=8192)
        self.assertEqual(agent.profile(), 'short')
        self.assertEqual(self.agent(context_window=200000).profile(), 'full')

    def test_the_short_profile_sends_eight_tools_and_no_board(self):
        agent = self.agent(prompt_profile='short')
        self.assertEqual(self.names(agent), list(prompt_profiles.SHORT_TOOLS[:8]))
        prompt = agent.system_prompt()
        # The sub-question of decision 8, answered "none until the A/B says it can file a card":
        # no board tools and no board policy on the Local tier, and no todo, app or own-session text.
        for absent in ('Switchboard', 'update_todos', 'app_option_list', 'session_info'):
            self.assertNotIn(absent, prompt)
        # What a project says about itself stays: it is the user's own rules, not Relay's text.
        self.assertIn('Project rule: run the tests.', prompt)
        self.assertIn('alpha', prompt)                      # the skills are named, not described
        self.assertNotIn('Renames a project everywhere', prompt)
        size = len(prompt.encode('utf-8'))
        tools = len(json.dumps(agent.tools(), ensure_ascii=False).encode('utf-8'))
        self.assertLess(size, 4 * 1024, f'the short prompt grew to {size} bytes')
        self.assertLess(tools, 4 * 1024, f'the short tool list grew to {tools} bytes')

    def test_the_setting_is_the_override_and_takes_effect_at_once(self):
        agent = self.agent(context_window=8192)             # auto would say short
        self.assertEqual(agent.profile(), 'short')
        agent.set_options({'prompt_profile': 'full'})
        self.assertEqual(agent.profile(), 'full')
        self.assertIn('board_list', self.names(agent))
        self.assertIn('Switchboard', agent.messages[0]['content'])
        self.assertEqual(agent.options()['prompt_profile'], 'full')
        self.assertEqual(agent.options()['prompt_profile_in_effect'], 'full')
        agent.set_options({'prompt_profile': 'short'})
        self.assertNotIn('board_list', self.names(agent))
        self.assertNotIn('Switchboard', agent.messages[0]['content'])
        with self.assertRaises(ValueError):
            agent.set_options({'prompt_profile': 'medium'})

    def test_the_short_tools_keep_their_schemas_and_lose_their_prose(self):
        agent = self.agent(prompt_profile='short')
        full = {t['function']['name']: t for t in self.agent().tools()}
        for spec in agent.tools():
            name = spec['function']['name']
            self.assertEqual(spec['function']['parameters']['properties'].keys(),
                             full[name]['function']['parameters']['properties'].keys(), name)
            self.assertLess(len(spec['function']['description']),
                            len(full[name]['function']['description']) + 1, name)
        edit = next(s for s in agent.tools() if s['function']['name'] == 'edit_file')
        self.assertIn('byte for byte', edit['function']['description'])


if __name__ == '__main__':
    unittest.main()
