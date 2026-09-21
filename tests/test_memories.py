import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from relay_core import aliases, board, memories


class MemoriesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        env = patch.dict(os.environ, HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / 'config'),
                         RELAY_GLOBAL_SWITCHBOARD=str(self.home / 'global'))
        env.start()
        self.addCleanup(env.stop)
        self.workspace = self.home / 'repo'
        self.workspace.mkdir()
        (self.workspace / '.git').mkdir()
        self.local = self.workspace / '.relay'

    def put(self, name, text, root=None, **fields):
        card = board.new_card('memory', name, fields.pop('status', 'active'), name=name, **fields)
        card.body += '\n' + text + '\n'
        path = (root or self.local) / 'memory' / (name + '.md')
        board.atomic_write(path, card.to_text())
        return card

    def test_pinned_matched_and_inactive(self):
        self.put('pin', 'PINNED', pinned=True)
        self.put('match', 'MATCHED', paths=['src/**'])
        self.put('global-match', 'GLOBAL MATCH', root=aliases.global_root(), paths=['src/**'])
        self.put('other', 'EXCLUDED', paths=['docs/**'])
        self.put('team', 'TEAM', pinned=True, scope='team')
        self.put('retired', 'RETIRED', pinned=True, status='retired')
        text = memories.prompt_section(self.workspace / 'src/lib')
        self.assertIn('PINNED', text)
        self.assertIn('MATCHED', text)
        self.assertIn('GLOBAL MATCH', text)
        for word in ('EXCLUDED', 'TEAM', 'RETIRED'):
            self.assertNotIn(word, text)

    def test_project_shadows_global_and_superseded_excluded(self):
        self.put('same', 'GLOBAL', root=aliases.global_root(), pinned=True)
        self.put('same', 'LOCAL', pinned=True)
        old = self.put('old', 'OBSOLETE', pinned=True)
        self.put('new', 'CURRENT', pinned=True, supersedes=[old.id])
        text = memories.prompt_section(self.workspace)
        self.assertIn('LOCAL', text)
        self.assertIn('CURRENT', text)
        self.assertNotIn('GLOBAL', text)
        self.assertNotIn('OBSOLETE', text)

    def test_byte_cap_and_reload(self):
        self.put('big', '界' * 1000, pinned=True)
        self.assertLessEqual(len(memories.prompt_section(self.workspace, cap=300).encode()), 300)
        self.put('big', 'NEW TEXT', pinned=True)
        self.assertIn('NEW TEXT', memories.prompt_section(self.workspace))

    def test_malformed_skipped(self):
        self.put('good', 'GOOD', pinned=True)
        (self.local / 'memory/bad.md').write_bytes(b'\xff')
        self.assertIn('GOOD', memories.prompt_section(self.workspace))

    def test_agent_profiles_load_edits_at_prompt_refresh(self):
        from relay_core.agent import Agent
        from relay_core.provider import ProviderConfig
        self.put('runtime', 'FIRST MEMORY', root=aliases.global_root(), pinned=True)
        for profile in ('full', 'short'):
            agent = Agent(ProviderConfig('http://127.0.0.1:12345/v1', 'mock', ''),
                          str(self.workspace), lambda event: None, provider=object(),
                          prompt_profile=profile)
            self.assertIn('FIRST MEMORY', agent.system_prompt())
        self.put('runtime', 'SECOND MEMORY', root=aliases.global_root(), pinned=True)
        self.assertIn('SECOND MEMORY', agent.system_prompt())
        self.assertNotIn('FIRST MEMORY', agent.system_prompt())

    def test_scoped_replacement_only_supersedes_where_it_applies(self):
        old = self.put('old-scoped', 'OLDER GUIDANCE', pinned=True)
        self.put('replacement', 'NEWER GUIDANCE', paths=['docs/**'], supersedes=[old.id])
        source = memories.prompt_section(self.workspace / 'src')
        self.assertIn('OLDER GUIDANCE', source)
        self.assertNotIn('NEWER GUIDANCE', source)
        docs = memories.prompt_section(self.workspace / 'docs')
        self.assertNotIn('OLDER GUIDANCE', docs)
        self.assertIn('NEWER GUIDANCE', docs)
