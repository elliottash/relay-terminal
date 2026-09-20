"""The short prompt profile: what a model that pays for the prompt in seconds is sent.

#GMCF decision 7, and the owner's two answers of 2026-09-20: the Lite tier defaults to it, and a
pane with a Switchboard keeps five board tools and the tiered policy. `relay_core.prompt_profiles`
holds the text and the tool list; these tests pin the four things that can silently go wrong with
it — the wrong profile being chosen, a hard rule of `SYSTEM` being contradicted rather than
omitted, the size creeping back up, and a mid-turn model swap leaving the prompt and the tool list
describing two different agents.
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
from relay_core.roles import RoleResolver

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
#: Two of the five Options › Models lists (`tiers` in `configure`, d28ca077), as a user who has
#: ranked Google's models would have them. Every model here has a ~1M window, so nothing but the
#: list itself can say which of them is the Lite one.
TIERS = {'main': [{'preset': 'gemini', 'model': 'gemini-3.1-pro-preview'},
                  {'preset': 'gemini', 'model': 'gemini-3.8-flash'}],
         'lite': [{'preset': 'gemini', 'model': 'gemini-3.5-flash-lite'}]}


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

    def test_auto_is_short_on_the_lite_tier_whatever_its_window(self):
        # The owner's decision of 2026-09-20: the Lite tier defaults to the short profile. A Lite
        # model's window says nothing — gemini flash-lite has a million — so only the tier can.
        hosted = PRESETS['gemini']
        self.assertEqual(prompt_profiles.resolve('auto', preset=hosted, context_window=1000000,
                                                 tier='lite'), 'short')
        for tier in ('high', 'main', 'flash', None):
            self.assertEqual(prompt_profiles.resolve('auto', preset=hosted, context_window=1000000,
                                                     tier=tier), 'full', tier)
        # And the pin still wins over the tier, both ways round.
        self.assertEqual(prompt_profiles.resolve('full', preset=hosted, tier='lite'), 'full')
        self.assertEqual(prompt_profiles.resolve('short', preset=hosted, tier='main'), 'short')

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

    def agent(self, *, board: bool = True, config: ProviderConfig = CONFIG, **kwargs) -> Agent:
        agent = Agent(config, str(self.root / 'ws'), lambda event: None, provider=object(), **kwargs)
        agent.executor.skills = skills_mod.SkillIndex.load([self.library])
        agent.instructions = instructions_mod.load({'project_auto': True}, str(self.root / 'ws'))
        agent.app = app_tools.AppTools(app_tools.AppCatalog.from_request(APP),
                                       app_tools.AppBridge(lambda event: None))
        agent.activity = activity_tools.ActivityTools(agent)
        if board:
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

    def test_the_short_profile_sends_eight_tools_when_there_is_no_board(self):
        agent = self.agent(prompt_profile='short', board=False)
        self.assertEqual(self.names(agent), list(prompt_profiles.SHORT_TOOLS[:8]))
        prompt = agent.system_prompt()
        # A pane with no Switchboard pays nothing for one, and the todo, app and own-session text
        # is out of the short profile whatever else is in it.
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

    def test_a_board_adds_five_tools_and_the_policy_and_nothing_else(self):
        # The owner's decision of 2026-09-20 (#GMCF, the second open question): the Local and Lite
        # tiers get the five-tool board set. A card round trip needs exactly these five — file it,
        # find it again, claim it, say what happened — and none of the three that rewrite, move or
        # bulk-import a card, which is where a small model does quiet damage.
        agent = self.agent(prompt_profile='short')
        self.assertEqual(self.names(agent), list(prompt_profiles.SHORT_TOOLS[:13]))
        self.assertEqual(list(prompt_profiles.SHORT_BOARD_TOOLS),
                         ['board_list', 'board_read', 'board_create_card', 'board_claim',
                          'board_comment'])
        for absent in ('board_update_card', 'board_move_card', 'board_import_items',
                       'board_signals', 'tests_check', 'tests_run'):
            self.assertNotIn(absent, self.names(agent))
        prompt = agent.system_prompt()
        # Decision 8's tiered policy block, and the claims line last of all (`session_note`), which
        # is what lets the model call `board_claim` without being told a session token.
        self.assertIn('Switchboard rules', prompt)
        self.assertIn('Your Switchboard session: tttttttt.', prompt.rstrip().splitlines()[-1])
        # The board policy names `update_todos` in its own rule 1, so the todo rules are tested for
        # by a sentence only they have.
        for absent in ('Requests and todos:', 'app_option_list', 'session_info'):
            self.assertNotIn(absent, prompt)
        size = len(prompt.encode('utf-8'))
        tools = len(json.dumps(agent.tools(), ensure_ascii=False).encode('utf-8'))
        # Measured 2026-09-20: 6.6 KB of prompt and 10.2 KB of tools with a board, against 2.9 KB
        # and 3.1 KB without one. Still a sixth of the full profile's 12.5 + 23.2 KB.
        self.assertLess(size, 7 * 1024, f'the short prompt with a board grew to {size} bytes')
        self.assertLess(tools, 11 * 1024, f'the short tool list with a board grew to {tools} bytes')

    def test_the_board_five_keep_the_rules_decision_8_moved_into_them(self):
        # Decision 8 took rules out of `board_policy.md` and put them in the tool descriptions, so
        # a short description here would delete a rule rather than tighten one: these five are sent
        # byte for byte as the full profile sends them.
        short = {t['function']['name']: t for t in self.agent(prompt_profile='short').tools()}
        full = {t['function']['name']: t for t in self.agent().tools()}
        for name in prompt_profiles.SHORT_BOARD_TOOLS:
            self.assertEqual(json.dumps(short[name], ensure_ascii=False),
                             json.dumps(full[name], ensure_ascii=False), name)
        # And nobody may quietly add one to the short table later: these five are equal or they
        # are not the tools the policy was tiered against.
        for name in prompt_profiles.SHORT_BOARD_TOOLS:
            self.assertNotIn(name, prompt_profiles.SHORT_DESCRIPTIONS)

    def test_the_setting_is_the_override_and_takes_effect_at_once(self):
        agent = self.agent(context_window=8192)             # auto would say short
        self.assertEqual(agent.profile(), 'short')
        agent.set_options({'prompt_profile': 'full'})
        self.assertEqual(agent.profile(), 'full')
        self.assertIn('board_move_card', self.names(agent))
        self.assertIn('Requests and todos:', agent.messages[0]['content'])
        self.assertEqual(agent.options()['prompt_profile'], 'full')
        self.assertEqual(agent.options()['prompt_profile_in_effect'], 'full')
        agent.set_options({'prompt_profile': 'short'})
        self.assertNotIn('board_move_card', self.names(agent))
        self.assertNotIn('Requests and todos:', agent.messages[0]['content'])
        with self.assertRaises(ValueError):
            agent.set_options({'prompt_profile': 'medium'})

    def tiered(self, model: str) -> Agent:
        """A pane on `model`, with the two Options › Models lists that name it (owner, 2026-09-20).

        The lists are the only thing that can say a model is a Lite model: `gemini-3.5-flash-lite`
        has a million-token window and a provider's own tier table is not the user's ranking.
        """
        config = ProviderConfig('https://generativelanguage.googleapis.com/v1beta/openai', model, 'k')
        resolver = RoleResolver(config, 'gemini', tiers=TIERS, key_lookup=lambda *_a, **_k: 'k')
        return self.agent(preset_id='gemini', roles=resolver, config=config)

    def test_a_pane_on_the_lite_list_sends_the_short_profile(self):
        main = self.tiered('gemini-3.1-pro-preview')
        self.assertEqual(main._model_tier(), 'main')
        self.assertEqual(main.profile(), 'full')
        lite = self.tiered('gemini-3.5-flash-lite')
        self.assertEqual(lite._model_tier(), 'lite')
        self.assertEqual(lite.profile(), 'short')
        self.assertIn('board_list', self.names(lite))
        self.assertNotIn('board_move_card', self.names(lite))
        # A pane with no resolver, or one whose lists name nothing, is unchanged: the endpoint and
        # the window still decide, as they did before the lists had a say.
        self.assertIsNone(self.agent()._model_tier())
        self.assertEqual(self.agent().profile(), 'full')

    def test_a_failover_across_tiers_switches_the_profile_and_switches_it_back(self):
        # `_adopt_model` is the one path a failover swap and its restore both take. The prompt is
        # `messages[0]` and is not rebuilt per request, so before this it could sit at the full
        # text while `tools()` — which *is* rebuilt — had already dropped to the short list.
        agent = self.tiered('gemini-3.1-pro-preview')
        full_prompt, full_tools = agent.messages[0]['content'], self.names(agent)
        lite = ProviderConfig('https://generativelanguage.googleapis.com/v1beta/openai',
                              'gemini-3.5-flash-lite', 'k')
        agent._adopt_model(lite, agent.preset)
        self.assertEqual(agent.profile(), 'short')
        self.assertEqual(agent.messages[0]['content'], agent.system_prompt())
        self.assertNotIn('update_todos', self.names(agent))
        agent._adopt_model(ProviderConfig(lite.base_url, 'gemini-3.1-pro-preview', 'k'), agent.preset)
        self.assertEqual(agent.profile(), 'full')
        self.assertEqual(agent.messages[0]['content'], full_prompt)
        self.assertEqual(self.names(agent), full_tools)

    def test_a_swap_inside_one_tier_leaves_the_prompt_object_alone(self):
        # The rewrite is what costs the prefix, so it happens only when the profile changed: a
        # model swap that stays on the Main list must not touch `messages[0]` at all.
        agent = self.tiered('gemini-3.1-pro-preview')
        before = agent.messages[0]
        agent._adopt_model(ProviderConfig(agent.config.base_url, 'gemini-3.8-flash', 'k'), agent.preset)
        self.assertIs(agent.messages[0], before)

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
