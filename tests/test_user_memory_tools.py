"""Interview saves share the Globals editor store and concurrency guards."""
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from relay_core import app_tools, memories
from relay_core.globals_protocol import GlobalsCommands

MEMORY = ('---\ntype: memory\nstatus: active\nname: explanation-style\n'
          'scope: user\npinned: true\npaths: []\n---\n# Explanation style\n\n'
          'Explain decisions concisely.\n')


class UserMemoryToolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        env = patch.dict(os.environ, HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / 'config'),
                         RELAY_GLOBAL_SWITCHBOARD=str(self.home / 'hq'))
        env.start()
        self.addCleanup(env.stop)
        self.catalog = app_tools.AppCatalog.from_request({'writes_enabled': True, 'options': [], 'actions': []})
        self.tools = app_tools.AppTools(self.catalog, app_tools.AppBridge(lambda e: None),
                                       workspace=str(self.home / 'project'))

    def call(self, action, **args):
        return self.tools.run('app_user_memory', dict(action=action, **args))

    def test_roundtrip_is_visible_to_editor_and_next_prompt(self):
        self.assertEqual(self.call('list')['records'], [])
        self.assertFalse((self.home / 'hq').exists())
        saved = self.call('save', text=MEMORY, base_hash='')['record']
        self.assertEqual(saved['memory_scope'], 'user')
        events = []
        GlobalsCommands(events.append).dispatch(dict(type='globals_get', kind='memory', key=saved['key']))
        self.assertEqual(events[-1]['record']['text'], saved['text'])
        self.assertIn('Explain decisions concisely.', memories.prompt_section(self.tools.workspace))
        changed = self.call('save', key=saved['key'], base_hash=saved['hash'],
                            text=saved['text'].replace('concisely', 'with examples'))['record']
        self.assertIn('with examples', memories.prompt_section(self.tools.workspace))
        self.assertNotIn('concisely', memories.prompt_section(self.tools.workspace))
        retired = self.call('retire', key=changed['key'], base_hash=changed['hash'])['record']
        self.assertTrue(Path(retired['path']).exists())
        self.assertNotIn('with examples', memories.prompt_section(self.tools.workspace))
        self.assertEqual(self.call('list')['records'][0]['status'], 'retired')

    def test_stale_interview_cannot_overwrite_owner_edit(self):
        saved = self.call('save', text=MEMORY, base_hash='')['record']
        path = Path(saved['path'])
        path.write_text(saved['text'] + '\nOwner correction.\n')
        result = self.call('save', key=saved['key'], base_hash=saved['hash'], text=saved['text'])
        self.assertIn('changed since', result['error'])
        self.assertIn('Owner correction.', path.read_text())

    def test_scope_duplicates_and_write_setting(self):
        self.assertIn('error', self.call('save', text=MEMORY.replace('scope: user', 'scope: project'), base_hash=''))
        self.call('save', text=MEMORY, base_hash='')
        self.assertIn('error', self.call('save', text=MEMORY, base_hash=''))
        self.tools.set_catalog(app_tools.AppCatalog.from_request({'writes_enabled': False}))
        self.assertIn('error', self.call('save', text=MEMORY, base_hash=''))
        self.assertEqual(len(self.call('list')['records']), 1)

    def test_non_user_memories_stay_out_of_tool(self):
        events = []
        GlobalsCommands(events.append).dispatch(dict(type='globals_save', kind='memory', base_hash='',
            text=MEMORY.replace('scope: user', 'scope: team')))
        key = events[-1]['record']['key']
        self.assertEqual(self.call('list')['records'], [])
        self.assertIn('error', self.call('get', key=key))
