import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
import unittest.mock
from pathlib import Path

from relay_core import skill_manage, skills
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from relay_core.skills import SkillError, SkillIndex, parse_frontmatter, parse_profile
from relay_core.tools import ToolExecutor

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ProviderConfig('http://127.0.0.1:12345/v1', 'mock', '')


def write(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


class SkillFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name) / 'skills'
        self.outside = Path(self.temp.name) / 'outside'
        write(self.outside / 'secret.txt', 'top secret')
        write(self.base / 'good' / 'SKILL.md', '---\nname: good\ndescription: "Does a good thing: carefully"\n---\n# Good\nStep one.\n')
        write(self.base / 'good' / 'scripts' / 'run.sh', 'echo hi\n')
        write(self.base / 'folded' / 'SKILL.md', '---\nname: Folded Skill\ndescription: >\n  First line\n  second line\n---\nBody\n')
        write(self.base / 'literal' / 'SKILL.md', "---\nname: literal\ndescription: |\n  Line A\n  Line B\n---\nBody\n")
        write(self.base / 'nofront' / 'SKILL.md', '# No frontmatter here\n')
        write(self.base / 'nodesc' / 'SKILL.md', '---\nname: nodesc\n---\nBody\n')
        (self.base / 'empty').mkdir()
        (self.base / 'escape').mkdir()
        os.symlink(self.outside / 'secret.txt', self.base / 'escape' / 'SKILL.md')
        os.symlink(self.outside, self.base / 'linkeddir')
        os.symlink(self.outside / 'secret.txt', self.base / 'good' / 'leak.txt')
        self.index = SkillIndex.load([self.base])

    def tearDown(self):
        self.temp.cleanup()


class IndexTests(SkillFixture):
    def test_indexes_good_and_block_descriptions(self):
        self.assertEqual(sorted(self.index.skills), ['folded', 'good', 'literal'])
        self.assertEqual(self.index.skills['good'].description, 'Does a good thing: carefully')
        self.assertEqual(self.index.skills['folded'].description, 'First line second line')
        self.assertEqual(self.index.skills['folded'].name, 'Folded Skill')
        self.assertEqual(self.index.skills['literal'].description, 'Line A Line B')

    def test_skips_are_reported(self):
        reasons = '\n'.join(self.index.skipped)
        for name in ('nofront', 'nodesc', 'empty', 'escape', 'linkeddir'):
            self.assertIn(name, reasons)

    def test_frontmatter_quotes_and_errors(self):
        self.assertEqual(parse_frontmatter("---\ndescription: 'it''s'\n---\n")['description'], "it's")
        with self.assertRaises(SkillError):
            parse_frontmatter('---\nname: x\n')

    def test_prompt_section_lists_and_caps(self):
        section = self.index.prompt_section()
        self.assertIn('- good: Does a good thing', section)
        self.assertIn('load_skill', section)
        many = Path(self.temp.name) / 'many'
        for i in range(120):
            write(many / f's{i:03}' / 'SKILL.md', f'---\nname: s{i}\ndescription: {"x" * 300}\n---\n')
        big = SkillIndex.load([many]).prompt_section()
        self.assertLessEqual(len(big.encode('utf-8')), skills.MAX_PROMPT_BYTES)
        # 120 long descriptions cannot fit, so the tail names them instead (owner report,
        # 2026-09-18: a skill the user asked for by name was reported missing because its
        # description did not fit).
        self.assertIn('also loadable by name', big)
        self.assertIn('s119', big)
        # Only when the names themselves overflow does the model have to ask.
        wordy = Path(self.temp.name) / 'wordy'
        for i in range(120):
            name = f'{"long-skill-name-" * 5}{i:03}'
            write(wordy / name / 'SKILL.md', f'---\nname: {name}\ndescription: {"x" * 300}\n---\n')
        crowded = SkillIndex.load([wordy]).prompt_section()
        self.assertLessEqual(len(crowded.encode('utf-8')), skills.MAX_PROMPT_BYTES)
        self.assertIn('ask the user for their names', crowded)

    def test_every_skill_is_at_least_named(self):
        # A library whose descriptions overflow the budget but whose names do not: every name
        # reaches the prompt, so load_skill resolves anything the user asks for.
        many = Path(self.temp.name) / 'named'
        for i in range(60):
            write(many / f's{i:03}' / 'SKILL.md', f'---\nname: s{i}\ndescription: {"x" * 200}\n---\n')
        index = SkillIndex.load([many])
        section = index.prompt_section()
        self.assertLessEqual(len(section.encode('utf-8')), skills.MAX_PROMPT_BYTES)
        for name in index.skills:
            self.assertIn(name, section)

    def test_empty_directory_gives_no_section(self):
        self.assertEqual(SkillIndex.load([Path(self.temp.name) / 'missing']).prompt_section(), '')


class ToolTests(SkillFixture):
    def executor(self):
        return ToolExecutor(self.temp.name, lambda e: None, threading.Event(), skills=self.index)

    def run_tool(self, name, args):
        executor = self.executor()
        return executor.execute(executor.prepare(name, args))

    def test_tools_offered_only_with_skills(self):
        names = [t['function']['name'] for t in self.executor().tools()]
        self.assertIn('load_skill', names)
        bare = ToolExecutor(self.temp.name, lambda e: None, threading.Event())
        self.assertNotIn('load_skill', [t['function']['name'] for t in bare.tools()])
        with self.assertRaises(ValueError):
            bare.prepare('load_skill', {'name': 'good'})

    def test_load_skill_returns_content_and_files(self):
        result = self.run_tool('load_skill', {'name': 'good'})
        self.assertIn('Step one.', result['content'])
        self.assertEqual(result['files'], ['scripts/run.sh'])
        self.assertFalse(result['truncated'])

    def test_read_skill_file_and_refusals(self):
        self.assertEqual(self.run_tool('read_skill_file', {'name': 'good', 'path': 'scripts/run.sh'})['content'], 'echo hi\n')
        for bad in ('../escape/SKILL.md', '/etc/passwd', 'leak.txt', 'missing.txt', ''):
            with self.assertRaises(ValueError):
                self.run_tool('read_skill_file', {'name': 'good', 'path': bad})
        with self.assertRaises(ValueError):
            self.run_tool('load_skill', {'name': 'escape'})
        with self.assertRaises(ValueError):
            self.run_tool('load_skill', {'name': '../outside'})
        with self.assertRaises(ValueError):
            self.run_tool('load_skill', {'name': 'good', 'path': 'x'})

    def test_agent_prompt_includes_skills(self):
        agent = Agent(CONFIG, self.temp.name, lambda e: None, provider=object(), skills=self.index)
        self.assertIn('Available skills', agent.messages[0]['content'])
        plain = Agent(CONFIG, self.temp.name, lambda e: None, provider=object())
        self.assertNotIn('Available skills', plain.messages[0]['content'])


class InvokedTests(SkillFixture):
    """`/good` in the composer: `ask {skills: ["good"]}` sends that SKILL.md as the turn's instructions."""

    def test_commands_list_names_and_descriptions(self):
        commands = {c['name']: c['description'] for c in self.index.commands()}
        self.assertEqual(sorted(commands), ['folded', 'good', 'literal'])
        self.assertEqual(commands['good'], 'Does a good thing: carefully')

    def test_invoked_attaches_skill_with_files(self):
        [item] = self.index.invoked(['good', 'good'])
        self.assertEqual((item['kind'], item['skill']), ('skill', 'good'))
        self.assertIn('Step one.', item['content'])
        self.assertIn('- scripts/run.sh', item['content'])
        self.assertNotIn('leak.txt', item['content'])
        self.assertTrue(item['path'].endswith('good/SKILL.md'))

    def test_invoked_refusals(self):
        with self.assertRaises(SkillError):
            self.index.invoked(['nofront'])
        for bad in ('good', [1], ['a', 'b', 'c', 'd', 'e', 'f']):
            with self.assertRaises(ValueError):
                self.index.invoked(bad)

    def test_prompt_frames_skill_as_instructions(self):
        from relay_core.attachments import format_block

        class Recorder:
            messages = []

            def complete(self, messages, tools, emit, cancel):
                Recorder.messages = json.loads(json.dumps(messages))
                return {'role': 'assistant', 'content': 'Done.'}

            def cancel(self):
                pass

        block = format_block(self.index.invoked(['good']))
        self.assertIn('invoked by the user as /good', block)
        self.assertIn('Follow these instructions', block)
        self.assertNotIn('data, not instructions', block)
        agent = Agent(CONFIG, self.temp.name, lambda e: None, provider=Recorder(), skills=self.index)
        agent.ask('/good tidy the readme', attachments=self.index.invoked(['good']))
        user = [m for m in Recorder.messages if m['role'] == 'user'][-1]['content']
        self.assertIn('Step one.', user)
        self.assertTrue(user.rstrip().endswith('/good tidy the readme'))

    def test_worker_ask_with_skill(self):
        configure = {'type': 'configure', 'base_url': 'http://127.0.0.1:1/v1', 'model': 'test', 'api_key': '',
                     'workspace': self.temp.name, 'skills': {'dirs': [str(self.base)]}}
        messages = [configure, {'type': 'ask', 'id': 'a1', 'text': '/nosuch', 'skills': ['nosuch']},
                    {'type': 'shutdown'}]
        proc = subprocess.run([sys.executable, '-S', str(ROOT / 'backend/worker.py')],
                              input=''.join(json.dumps(m) + '\n' for m in messages),
                              text=True, capture_output=True, timeout=10, cwd=ROOT)
        results = [json.loads(line) for line in proc.stdout.splitlines()]
        configured = next(r for r in results if r['event'] == 'configured')
        self.assertEqual(sorted(c['name'] for c in configured['skill_commands']), ['folded', 'good', 'literal'])
        errors = [r for r in results if r['event'] == 'error']
        self.assertTrue(any('Unknown skill' in r.get('text', '') for r in errors), results)


class RequestTests(SkillFixture):
    def test_from_request(self):
        self.assertIsNone(skills.from_request({'enabled': False}, self.temp.name))
        index = skills.from_request({'dirs': [str(self.base)]}, self.temp.name)
        self.assertEqual(len(index.skills), 3)
        write(Path(self.temp.name) / '.relay' / 'skills' / 'proj' / 'SKILL.md', '---\ndescription: project skill\n---\n')
        self.assertNotIn('proj', skills.from_request({'dirs': [str(self.base)]}, self.temp.name).skills)
        self.assertIn('proj', skills.from_request({'dirs': [str(self.base)], 'project': True}, self.temp.name).skills)
        self.assertIn('proj', skills.from_request(None, self.temp.name).skills)
        for bad in ({'dirs': 'x'}, {'dirs': ['relative']}, {'enabled': 'yes'}, {'other': 1}, []):
            with self.assertRaises(ValueError):
                skills.from_request(bad, self.temp.name)

    def test_worker_configure_reports_count(self):
        messages = [{'type': 'configure', 'base_url': 'http://127.0.0.1:1/v1', 'model': 'test', 'api_key': '',
                     'workspace': self.temp.name, 'skills': {'dirs': [str(self.base)]}},
                    {'type': 'configure', 'base_url': 'http://127.0.0.1:1/v1', 'model': 'test', 'api_key': '',
                     'workspace': self.temp.name, 'skills': {'enabled': False}},
                    {'type': 'shutdown'}]
        proc = subprocess.run([sys.executable, '-S', str(ROOT / 'backend/worker.py')],
                              input=''.join(json.dumps(m) + '\n' for m in messages),
                              text=True, capture_output=True, timeout=10, cwd=ROOT)
        results = [json.loads(line) for line in proc.stdout.splitlines()]
        self.assertEqual(results[1]['event'], 'configured')
        self.assertEqual(results[1]['skills'], 3)
        self.assertTrue(results[1]['skills_skipped'])
        self.assertEqual(results[2]['skills'], 0)


class DiscoveryTests(unittest.TestCase):
    def test_relay_project_skill_wins_and_external_sources_remain_available(self):
        from unittest import mock
        with tempfile.TemporaryDirectory() as temp:
            home, ws = Path(temp) / 'home', Path(temp) / 'ws'
            skill = '---\nname: shared\ndescription: {}\n---\nbody\n'
            write(ws / '.relay/skills/shared/SKILL.md', skill.format('Relay project'))
            write(home / '.config/relay/skills/shared/SKILL.md', skill.format('Relay global'))
            write(home / '.warp/skills/shared/SKILL.md', skill.format('Warp'))
            write(home / '.codex/skills/codex-only/SKILL.md', skill.replace('shared', 'codex-only').format('Codex'))
            with mock.patch.object(Path, 'home', staticmethod(lambda: home)), \
                    mock.patch.dict(os.environ, {'XDG_CONFIG_HOME': str(home / '.config')}):
                index = skills.from_request(None, str(ws))
                listed = skill_manage.list_skills(skills.default_directories(str(ws)), workspace=str(ws))
            self.assertEqual(index.skills['shared'].description, 'Relay project')
            self.assertIn('codex-only', index.skills)
            copies = [item for item in listed if item['name'] == 'shared']
            self.assertEqual([item['source'] for item in copies[:3]], ['project-relay', 'relay-refined', 'warp'])
            self.assertEqual(copies[1]['shadowed_by'], copies[0]['path'])

    def test_default_search_covers_warp_tree_claude_dirs_and_excludes(self):
        from unittest import mock
        with tempfile.TemporaryDirectory() as temp:
            home, ws = Path(temp) / 'home', Path(temp) / 'ws'
            skill = '---\ndescription: {}\n---\nbody\n'
            write(home / '.warp/skills/shared/SKILL.md', skill.format('user copy'))
            bundled = home / '.warp/remote-server/bundled_resources/bundled/skills'
            write(bundled / 'shared/SKILL.md', skill.format('bundled copy'))
            write(bundled / 'bundled-only/SKILL.md', skill.format('bundled'))
            write(bundled / 'bundled-only/nested/SKILL.md', skill.format('not a separate skill'))
            write(bundled / 'warpctrl/SKILL.md', skill.format('warp app only'))
            write(home / '.warp/a/b/c/d/e/f/too-deep/SKILL.md', skill.format('too deep'))
            write(home / '.claude/skills/claude-user/SKILL.md', skill.format('claude user'))
            write(ws / '.claude/skills/claude-project/SKILL.md', skill.format('claude project'))
            with mock.patch.dict(os.environ, {'HOME': str(home)}):
                index = skills.from_request(None, str(ws))
                # Relay's own bundled skills are in the default search too, after everything of the
                # user's; this test is about the user's tree, so they are taken out again here.
                found = sorted(set(index.skills) - set(skills.SkillIndex.load([skills.bundled_dir()]).skills))
                self.assertEqual(found, ['bundled-only', 'claude-project', 'claude-user', 'shared'])
                self.assertEqual(index.skills['shared'].description, 'user copy')
                self.assertTrue(any(r.startswith('shared: duplicate') for r in index.skipped))
                self.assertTrue(any(r.startswith('warpctrl: excluded') for r in index.skipped))
                self.assertIn('warpctrl', skills.from_request({'exclude': []}, str(ws)).skills)
                with self.assertRaises(ValueError):
                    skills.from_request({'exclude': 'warpctrl'}, str(ws))


class DefaultDirectoryTests(unittest.TestCase):
    def test_nested_claude_skills_are_found(self):
        # Owner report 2026-09-18: nothing under ~/.claude/skills was indexed. Claude Code's synced
        # skills sit at skills/synced/<id>/<name>/SKILL.md, so the folder the search order names
        # holds folders of skills rather than skills, and every one of them was skipped as
        # "no SKILL.md". The same walk that finds Warp's bundled skills now runs over ~/.claude.
        with tempfile.TemporaryDirectory() as home:
            root = Path(home)
            write(root / '.claude' / 'skills' / 'synced' / 'abc123' / 'nested'/ 'SKILL.md',
                  '---\nname: nested\ndescription: A synced skill one level deeper\n---\n')
            write(root / '.claude' / 'skills' / 'plain' / 'SKILL.md',
                  '---\nname: plain\ndescription: A skill in the folder itself\n---\n')
            with unittest.mock.patch.object(Path, 'home', staticmethod(lambda: root)):
                index = SkillIndex.load(skills.default_directories(), defaults=True)
        self.assertIn('nested', index.skills)
        self.assertIn('plain', index.skills)


class BundledSkillTests(unittest.TestCase):
    """The skills Relay ships itself: relay_core/skills_bundled/<name>/SKILL.md."""

    def test_guest_account_setup_is_a_loadable_interview_skill(self):
        # The Add account button submits /skill guest-account-setup. If the bundled skill is
        # missing, that looks like a normal chat turn and the guided setup never starts.
        index = skills.SkillIndex.load([skills.bundled_dir()])
        self.assertIn('guest-account-setup', [row['name'] for row in index.commands()])
        content = index.load_skill('guest-account-setup')['content']
        self.assertIn('app_option_list', content)
        self.assertIn('Ask one question per turn', content)
        self.assertIn('Never ask for a password', content)

    def test_bundled_skills_are_indexed_and_readable(self):
        index = skills.SkillIndex.load([skills.bundled_dir()])
        self.assertEqual(index.skipped, [])
        self.assertIn('local-model-setup', index.skills)
        loaded = index.load_skill('local-model-setup')
        self.assertFalse(loaded['truncated'])
        self.assertIn('recipes/models.json', loaded['files'])
        for path in loaded['files']:
            self.assertTrue(index.read_file('local-model-setup', path)['content'])
        # Every recipe the SKILL.md names by path has to exist, or the agent loads a dead reference.
        for path in loaded['files']:
            self.assertIn(path, loaded['content'], path)
        self.assertLessEqual(len(index.skills['local-model-setup'].description), skills.MAX_DESCRIPTION)

    def test_the_deliver_skill_is_the_switchboard_procedure_and_is_offered_as_a_command(self):
        # The policy in every pane agent's prompt says "load the `deliver` skill" (#R9G7), and
        # `/deliver <request>` runs it by hand: both need the bundled skill to be indexed, named
        # `deliver`, and in `commands()` — that list is what the composer offers as `/name`.
        index = skills.SkillIndex.load([skills.bundled_dir()])
        self.assertEqual(index.skipped, [])
        self.assertIn('deliver', index.skills)
        self.assertLessEqual(len(index.skills['deliver'].description), skills.MAX_DESCRIPTION)
        self.assertIn('/deliver', index.skills['deliver'].description)
        self.assertIn('deliver', [c['name'] for c in index.commands()])
        loaded = index.load_skill('deliver')
        self.assertFalse(loaded['truncated'])
        self.assertEqual(loaded['files'], [])          # one file: the procedure itself
        for phrase in ('board_claim', 'board_claimed_elsewhere', 'needs-verification',
                       'links.commits', 'links.related'):
            self.assertIn(phrase, loaded['content'], phrase)

    def test_the_model_catalog_parses_and_dates_itself(self):
        index = skills.SkillIndex.load([skills.bundled_dir()])
        catalog = json.loads(index.read_file('local-model-setup', 'recipes/models.json')['content'])
        self.assertEqual(sorted(catalog['tiers']), ['128', '16', '24', '48', '8'])
        for tier, entries in catalog['tiers'].items():
            for entry in entries:
                self.assertIn(entry['status'], ('verified-here', 'unverified-listing'), entry['name'])
                self.assertTrue(entry['verified'], entry['name'])
                self.assertTrue(entry['sources'], entry['name'])

    def test_they_are_named_as_relays_own_in_the_skills_list(self):
        from relay_core import skill_manage
        rows = {item['name']: item for item in skill_manage.list_skills([skills.bundled_dir()])}
        self.assertEqual(rows['local-model-setup']['source'], 'relay-bundled')

    def test_the_default_search_includes_them_last(self):
        # Last, so a skill of the user's own with the same name wins (first directory wins).
        directories = skills.default_directories(None)
        self.assertEqual(directories[-1], skills.bundled_dir())

    def test_a_users_own_copy_wins(self):
        with tempfile.TemporaryDirectory() as temp:
            mine = Path(temp) / 'skills'
            write(mine / 'local-model-setup' / 'SKILL.md', '---\ndescription: my own version\n---\nMine.\n')
            index = SkillIndex.load([mine, skills.bundled_dir()])
            self.assertEqual(index.skills['local-model-setup'].description, 'my own version')
            self.assertTrue(any('duplicate' in reason for reason in index.skipped))


class ProfileTests(unittest.TestCase):
    """#MSJ0: a SKILL.md may declare a task profile in a `profile: |` frontmatter block."""

    PROFILED = ('---\nname: buy\ndescription: Buy a domain\nprofile: |\n  artifact: system\n  primary: probe\n'
                '  also: script, ai-text\n  human: required\n  criteria: "name, price and renewal match"\n'
                '  sign_off: money\n  effort: low\n  stakes: money\n  blast: case\n  regularity: routine\n'
                '  executable: yes\n  rot: medium\n  rot_reason: registrar API changes\n  confidential: no\n'
                '  money: yes\n---\n# Buy\nStep one.\n')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name) / 'skills'
        write(self.base / 'buy' / 'SKILL.md', self.PROFILED)
        write(self.base / 'plain' / 'SKILL.md', '---\nname: plain\ndescription: No profile\n---\nBody\n')
        write(self.base / 'typo' / 'SKILL.md', '---\nname: typo\ndescription: Typo in profile\nprofile: |\n'
              '  primary: probe\n  stake: money\n  human: sometimes\n  also: script, nonsense\n  just words\n---\nBody\n')

    def tearDown(self):
        self.temp.cleanup()

    def test_parse_profile_reads_vocabularies_lists_and_free_text(self):
        warnings = []
        profile = parse_profile(parse_frontmatter(self.PROFILED)['profile'], warnings)
        self.assertEqual(warnings, [])
        self.assertEqual(profile['primary'], 'probe')
        self.assertEqual(profile['also'], ['script', 'ai-text'])
        self.assertEqual(profile['human'], 'required')
        self.assertEqual(profile['criteria'], 'name, price and renewal match')
        self.assertEqual(profile['rot_reason'], 'registrar API changes')
        self.assertEqual((profile['sign_off'], profile['stakes'], profile['money']), ('money', 'money', 'yes'))
        # Vocabulary values are case-insensitive; an empty block is no profile at all.
        self.assertEqual(parse_profile('Primary: Script\nHUMAN: None')['human'], 'none')
        self.assertEqual(parse_profile(''), {})

    def test_unknown_key_is_a_warning_not_an_error(self):
        warnings = []
        profile = parse_profile('primary: script\ncolour: blue', warnings)
        self.assertEqual(profile, {'primary': 'script'})
        self.assertEqual(len(warnings), 1)
        self.assertIn("unknown key 'colour'", warnings[0])
        self.assertIn('known: artifact, primary', warnings[0])

    def test_bad_values_are_warnings_and_dropped(self):
        warnings = []
        profile = parse_profile('human: sometimes\nalso: script, nonsense\nstakes: MONEY\nrot: high\nrot: low\nnot a line',
                                warnings)
        self.assertEqual(profile, {'also': ['script'], 'stakes': 'money', 'rot': 'high'})
        text = '\n'.join(warnings)
        self.assertIn("human: 'sometimes' not in none, optional, required", text)
        self.assertIn('also: nonsense not in', text)
        self.assertIn("'rot' repeated", text)
        self.assertIn('line 6 is not key: value', text)

    def test_index_carries_the_profile_and_reports_warnings_beside_skipped(self):
        index = SkillIndex.load([self.base])
        self.assertEqual(sorted(index.skills), ['buy', 'plain', 'typo'])   # a bad profile never skips a skill
        self.assertEqual(index.skills['buy'].profile['primary'], 'probe')
        self.assertEqual(index.skills['plain'].profile, {})
        self.assertEqual(index.skills['typo'].profile, {'primary': 'probe', 'also': ['script']})
        self.assertEqual(len(index.skills['typo'].profile_warnings), 4)
        reported = [line for line in index.skipped if line.startswith('typo: ')]
        self.assertEqual(len(reported), 4)
        self.assertTrue(all(line.endswith('(skill still loads)') for line in reported))
        self.assertIn("typo: profile has unknown key 'stake'", '\n'.join(reported))
        self.assertFalse([line for line in index.skipped if line.startswith('buy')])

    def test_load_skill_result_carries_the_profile(self):
        index = SkillIndex.load([self.base])
        loaded = index.load_skill('buy')
        self.assertEqual(loaded['profile']['sign_off'], 'money')
        self.assertEqual(loaded['profile']['also'], ['script', 'ai-text'])
        self.assertEqual(index.load_skill('plain')['profile'], {})
        json.dumps(loaded)   # what the worker sends back must serialise

    def test_skills_list_items_carry_the_profile_and_its_warnings(self):
        rows = {item['name']: item for item in skill_manage.list_skills([self.base])}
        self.assertEqual(rows['buy']['profile']['primary'], 'probe')
        self.assertEqual(rows['buy']['profile']['human'], 'required')
        self.assertNotIn('profile_warnings', rows['buy'])
        self.assertNotIn('profile', rows['plain'])
        self.assertEqual(rows['typo']['profile'], {'primary': 'probe', 'also': ['script']})
        self.assertEqual(len(rows['typo']['profile_warnings']), 4)

    def test_the_catalogue_line_is_unchanged_by_a_profile(self):
        index = SkillIndex.load([self.base])
        section = index.prompt_section()
        self.assertIn('- buy: Buy a domain\n', section)
        self.assertNotIn('probe', section)
        self.assertNotIn('primary', section)
        self.assertNotIn('sign_off', section)

    def test_the_six_example_profiles_parse_clean(self):
        bundled = SkillIndex.load([skills.bundled_dir()])
        deliver = bundled.skills['deliver'].profile
        self.assertEqual((deliver['primary'], deliver['human'], deliver['effort']), ('script', 'none', 'medium'))
        self.assertEqual((deliver['stakes'], deliver['blast'], deliver['regularity']), ('rework', 'capability', 'routine'))
        self.assertEqual((deliver['executable'], deliver['rot'], deliver['confidential'], deliver['money']),
                         ('yes', 'low', 'no', 'no'))
        self.assertEqual(bundled.skills['deliver'].profile_warnings, [])
        examples = SkillIndex.load([ROOT / 'docs' / 'skills-examples'])
        self.assertEqual(sorted(examples.skills),
                         ['analysis-run', 'domain-purchase', 'referee-report', 'server-health-check', 'slide-deck'])
        self.assertEqual(examples.skipped, [])
        for skill in examples.skills.values():
            self.assertEqual(skill.profile_warnings, [], skill.id)
            for key in ('artifact', 'primary', 'human', 'effort', 'stakes', 'blast', 'regularity', 'executable',
                        'rot', 'confidential', 'money'):
                self.assertIn(key, skill.profile, f'{skill.id} lacks {key}')
            if skill.profile['human'] != 'none':
                self.assertIn('criteria', skill.profile, skill.id)
            if skill.profile['rot'] != 'low':
                self.assertIn('rot_reason', skill.profile, skill.id)
        by = examples.skills
        self.assertEqual((by['referee-report'].profile['primary'], by['referee-report'].profile['also']), ('level', ['ai-text']))
        self.assertEqual((by['referee-report'].profile['confidential'], by['referee-report'].profile['rot']), ('yes', 'high'))
        self.assertEqual((by['domain-purchase'].profile['primary'], by['domain-purchase'].profile['sign_off'],
                          by['domain-purchase'].profile['money']), ('probe', 'money', 'yes'))
        self.assertEqual((by['slide-deck'].profile['primary'], by['slide-deck'].profile['also']), ('script', ['ai-visual', 'pairwise']))
        self.assertIn('rehearsed', by['slide-deck'].profile['criteria'])
        self.assertEqual((by['analysis-run'].profile['primary'], by['analysis-run'].profile['blast']), ('metric', 'capability'))
        self.assertIn('ai-visual', by['analysis-run'].profile['also'])
        self.assertEqual((by['server-health-check'].profile['primary'], by['server-health-check'].profile['executable'],
                          by['server-health-check'].profile['location']), ('probe', 'yes', 'remote'))


class CaseStatisticsTests(unittest.TestCase):
    """#95VZ: a `skills_list` item with a profile carries its case statistics from the
    workspace's board ledger — cases, last_served, pass_rate_30, stale — and nothing else does."""

    def test_profiled_items_carry_the_four_statistics_from_the_boards_ledger(self):
        from relay_core import cases, skill_manage
        with tempfile.TemporaryDirectory() as temp:
            ws = Path(temp) / 'ws'
            (ws / 'issues').mkdir(parents=True)
            (ws / 'issues' / 'board.yaml').write_text('version: 1\ntabs: [{id: features, folder: features}]\n')
            base = ws / '.relay' / 'skills'
            write(base / 'referee' / 'SKILL.md', '---\nname: referee\ndescription: Referee\nprofile: |\n'
                  '  artifact: text\n  primary: ai-text\n  effort: high\n  rot: high\n---\nBody\n')
            write(base / 'plain' / 'SKILL.md', '---\nname: plain\ndescription: Plain\n---\nBody\n')
            cases.append(ws / 'issues', cases.new_record('referee', served_by='person', verdict='pass',
                                                         when='2026-01-01T10:00:00Z'))
            cases.append(ws / 'issues', cases.new_record('referee', served_by='openai/gpt-5-6', verdict='fail',
                                                         when='2026-01-02T10:00:00Z'))
            cases.append(ws / 'issues', cases.new_record('referee', served_by='openai/gpt-5-6',
                                                         when='2026-01-03T10:00:00Z'))
            rows = {item['name']: item for item in skill_manage.list_skills([base], workspace=str(ws))}
            referee = rows['referee']
            self.assertEqual((referee['cases'], referee['last_served']), (3, '2026-01-03T10:00:00Z'))
            self.assertEqual(referee['pass_rate_30'], 0.5)          # pending rows are not decided
            self.assertTrue(referee['stale'])                       # rot high: 7 days since the last pass
            for key in ('cases', 'last_served', 'pass_rate_30', 'stale'):
                self.assertNotIn(key, rows['plain'])
            # No board: a profiled skill still lists, uncounted and not stale.
            nowhere = {item['name']: item for item in skill_manage.list_skills([base], workspace=temp)}
            self.assertEqual((nowhere['referee']['cases'], nowhere['referee']['stale']), (0, False))
            self.assertIsNone(nowhere['referee']['pass_rate_30'])


class RealSkillsTest(unittest.TestCase):
    def test_home_skills_index_without_errors(self):
        home = Path.home() / '.warp' / 'skills'
        if not home.is_dir():
            self.skipTest('no ~/.warp/skills')
        index = SkillIndex.load([home])
        self.assertTrue(index.skills)
        for reason in index.skipped:
            self.assertIn('no SKILL.md', reason)
        self.assertLessEqual(len(index.prompt_section().encode('utf-8')), skills.MAX_PROMPT_BYTES)


if __name__ == '__main__':
    unittest.main()
