import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from relay_core import aliases, board
from relay_core.globals_protocol import GlobalsCommands

MEMORY = '---\ntype: memory\nstatus: active\nname: rule\nscope: user\npinned: true\n---\n# Rule\n\nUse tests.\n'
ALIAS = '---\ntype: alias\nstatus: active\nname: where\nkind: command\n---\n# Where\n\n## Run\n```sh\npwd\n```\n'


class GlobalsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        env = patch.dict(os.environ, HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / 'config'),
                         RELAY_GLOBAL_SWITCHBOARD=str(self.home / 'config/relay/switchboard'))
        env.start()
        self.addCleanup(env.stop)
        self.events = []
        self.commands = GlobalsCommands(self.events.append)

    def ask(self, action, **kw):
        self.commands.dispatch(dict(type='globals_' + action, id='test', **kw))
        return self.events[-1]

    def create(self, kind='memory', text=MEMORY):
        result = self.ask('save', kind=kind, text=text, base_hash='')
        self.assertEqual(result['event'], 'globals_saved', result)
        return result['record']

    def test_read_is_side_effect_free_and_create_bootstraps(self):
        self.assertEqual(self.ask('list')['event'], 'globals_state')
        self.assertFalse(aliases.global_root().exists())
        row = self.create()
        self.assertTrue((aliases.global_root() / board.BOARD_CONFIG).exists())
        self.assertEqual(self.ask('get', kind='memory', key=row['key'])['record']['text'], row['text'])
        self.assertTrue(board.Board(aliases.global_root()).thread(row['key']))

    def test_conflict_and_retire_preserve_card(self):
        row = self.create()
        path = Path(row['path'])
        path.write_text(row['text'] + '\nChanged\n')
        result = self.ask('save', kind='memory', key=row['key'], text=row['text'], base_hash=row['hash'])
        self.assertEqual(result['event'], 'globals_error')
        self.assertIn('Changed', path.read_text())
        current = self.ask('get', kind='memory', key=row['key'])['record']
        result = self.ask('retire', kind='memory', key=row['key'], base_hash=current['hash'])
        self.assertEqual(result['record']['status'], 'retired')
        self.assertFalse(path.exists())
        self.assertTrue(Path(result['record']['path']).exists())

    def test_retired_alias_no_longer_resolves_and_overrides_visible(self):
        row = self.create('alias', ALIAS)
        workspace = self.home / 'project'
        aliases.save(aliases.Alias(name='where', kind='command', text='ls'), str(workspace))
        listing = self.ask('list', workspace=str(workspace))['records']
        self.assertTrue(next(r for r in listing if r['key'] == row['key'])['shadowed'])
        self.ask('retire', kind='alias', key=row['key'], base_hash=row['hash'])
        with self.assertRaises(aliases.AliasError):
            aliases.resolve('where', scope='global')

    def test_instruction_allowlist_and_optimistic_edit_in_place(self):
        path = self.home / '.claude/CLAUDE.md'
        row = self.ask('get', kind='instruction', key=str(path))['record']
        result = self.ask('save', kind='instruction', key=str(path), text='Be concise.\n', base_hash=row['hash'])
        self.assertEqual(result['event'], 'globals_saved')
        self.assertEqual(path.read_text(), 'Be concise.\n')
        self.assertFalse(aliases.global_root().exists())
        result = self.ask('save', kind='instruction', key=str(self.home / 'secret'), text='oops', base_hash='')
        self.assertEqual(result['event'], 'globals_error')
        self.assertFalse((self.home / 'secret').exists())

    def test_duplicate_alias_rejected(self):
        self.create('alias', ALIAS)
        self.assertEqual(self.ask('save', kind='alias', text=ALIAS, base_hash='')['event'], 'globals_error')

    def test_invalid_memory_metadata_rejected(self):
        for text in [MEMORY.replace('pinned: true', 'pinned: maybe'), MEMORY.replace('scope: user', 'scope: planet')]:
            self.assertEqual(self.ask('save', kind='memory', text=text, base_hash='')['event'], 'globals_error')
