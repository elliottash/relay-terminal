import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
import unittest.mock
from pathlib import Path

from relay_core import skills
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from relay_core.skills import SkillError, SkillIndex, parse_frontmatter
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
        write(Path(self.temp.name) / '.warp' / 'skills' / 'proj' / 'SKILL.md', '---\ndescription: project skill\n---\n')
        self.assertNotIn('proj', skills.from_request({'dirs': [str(self.base)]}, self.temp.name).skills)
        self.assertIn('proj', skills.from_request({'dirs': [str(self.base)], 'project': True}, self.temp.name).skills)
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
