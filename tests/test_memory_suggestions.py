"""#MEMS: learned user facts are suggestions the user confirms; rejections decline repeats."""
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from relay_core import board, memories, memory_suggestions as ms

ACTIVE = ('---\ntype: memory\nstatus: active\nname: explanation-style\nscope: user\n'
          'pinned: true\npaths: []\n---\n# Explanation style\n\nExplain decisions concisely.\n')


class MemorySuggestionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        env = patch.dict(os.environ, HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / 'config'),
                         RELAY_GLOBAL_SWITCHBOARD=str(self.home / 'hq'), RELAY_KEYRING='off')
        env.start()
        self.addCleanup(env.stop)
        self.hq = self.home / 'hq'
        self.workspace = str(self.home / 'project')

    def files(self, folder):
        return sorted((self.hq / 'memory' / folder).glob('*.md'))

    def test_a_suggestion_is_a_pending_card_that_never_reaches_context(self):
        result = ms.suggest('The user prefers tabs over spaces in Python.', origin='pane 3')
        self.assertEqual(result['status'], 'pending')
        self.assertIsNone(result['matched'])
        self.assertEqual(result['name'], 'prefers-tabs-over-spaces-python')
        path = self.hq / 'memory' / 'suggestions' / f"{result['id']}.md"
        card = board.Card.load(path)
        self.assertEqual((card.type, card.status, card.front['scope'], card.front['source'],
                          card.front['origin']), ('memory', 'suggested', 'user', 'agent', 'pane 3'))
        self.assertIn('suggested', card.front)
        self.assertEqual(card.title, 'The user prefers tabs over spaces in Python')
        self.assertEqual(memories.cards(self.hq), [])
        self.assertEqual(memories.prompt_section(self.workspace), '')
        [row] = ms.pending()
        self.assertEqual((row['id'], row['fact'], row['origin']),
                         (result['id'], 'The user prefers tabs over spaces in Python.', 'pane 3'))

    def test_rejected_cards_never_reach_context_even_when_active_ones_do(self):
        ms.reject(ms.suggest('Likes long explanations.')['id'])
        ms.accept(ms.suggest('Works mostly over SSH.', name='remote-work')['id'])
        section = memories.prompt_section(self.workspace)
        self.assertIn('Works mostly over SSH.', section)
        self.assertNotIn('Likes long explanations', section)
        self.assertEqual([c.front['name'] for c in memories.cards(self.hq)], ['remote-work'])

    def test_accept_writes_an_active_memory_and_removes_the_suggestion(self):
        sid = ms.suggest('Works mostly over SSH on a DGX box.', source='claude')['id']
        record = ms.accept(sid)
        self.assertEqual(self.files('suggestions'), [])
        self.assertEqual((record['kind'], record['status'], record['memory_scope'], record['pinned']),
                         ('memory', 'active', 'user', True))
        card = board.Card.load(Path(record['path']))
        self.assertEqual(card.front['paths'], [])
        self.assertEqual(card.front['source'], 'claude')
        self.assertIn('reviewed', card.front)
        self.assertIn('Works mostly over SSH on a DGX box.', memories.prompt_section(self.workspace))
        thread = board.Board(self.hq).thread(record['key'])
        self.assertTrue(any('Kept from a suggestion by claude' in e.text for e in thread))
        with self.assertRaises(ms.SuggestionError):
            ms.accept(sid)

    def test_accept_takes_the_users_edit(self):
        sid = ms.suggest('Prefers terse answers.')['id']
        record = ms.accept(sid, fact='Prefers terse answers with one example.', title='Answer style')
        self.assertEqual(record['title'], 'Answer style')
        self.assertIn('with one example', Path(record['path']).read_text())

    def test_accept_does_not_fail_on_an_incidental_name_clash(self):
        ms.accept(ms.suggest('Uses Emacs keybindings everywhere.', name='editor')['id'])
        # Written straight to the folder, as an older suggestion that predates the active card.
        other = ms.suggest('Runs long training jobs on a Slurm cluster.')
        path = self.hq / 'memory' / 'suggestions' / f"{other['id']}.md"
        path.write_text(path.read_text().replace(f"name: {other['name']}", 'name: editor'))
        record = ms.accept(other['id'])
        self.assertEqual(board.Card.load(Path(record['path'])).front['name'], 'editor-2')

    def test_reject_remembers_and_declines_the_same_near_same_and_same_named_fact(self):
        first = ms.suggest('The user prefers dark themes in every editor.', name='theme')
        rejected = ms.reject(first['id'], reason='not true')
        self.assertEqual(self.files('suggestions'), [])
        card = board.Card.load(self.hq / 'memory' / 'rejected' / f"{first['id']}.md")
        self.assertEqual((card.status, card.front['reason']), ('rejected', 'not true'))
        self.assertIn('rejected', card.front)
        self.assertEqual(rejected['id'], first['id'])
        for fact, name in (('The user prefers dark themes in every editor.', None),   # same
                           ('user prefers DARK themes, in every editor!', None),      # normalised
                           ('Prefers dark themes in every single editor.', None),     # Jaccard
                           ('Something else entirely.', 'Theme')):                    # same name
            result = ms.suggest(fact, name=name, source='codex')
            self.assertEqual(result['status'], 'declined', fact)
            self.assertIsNone(result['id'])
            self.assertEqual((result['matched']['id'], result['matched']['kind'],
                              result['matched']['reason']), (first['id'], 'rejected', 'not true'))
            self.assertTrue(result['matched']['date'])
        self.assertEqual(self.files('suggestions'), [])
        self.assertEqual(len(self.files('rejected')), 1)
        self.assertEqual(ms.suggest('Prefers light themes for slides.')['status'], 'pending')

    def test_duplicates_of_active_memory_and_pending_suggestions_write_nothing(self):
        (self.hq / 'memory').mkdir(parents=True)
        (self.hq / 'memory' / 'ABCD.md').write_text(ACTIVE.replace('---\ntype', '---\nid: ABCD\ntype'))
        result = ms.suggest('The user: explain decisions concisely')
        self.assertEqual((result['status'], result['matched']['kind'], result['matched']['id']),
                         ('duplicate', 'active', 'ABCD'))
        self.assertEqual(ms.suggest('x', name='explanation-style')['matched']['kind'], 'active')
        pending = ms.suggest('Keeps commits small and frequent.')
        again = ms.suggest('keeps commits small, and frequent')
        self.assertEqual((again['status'], again['matched']['kind'], again['matched']['id']),
                         ('duplicate', 'pending', pending['id']))
        self.assertEqual(len(self.files('suggestions')), 1)

    def test_a_derived_name_is_not_matched_on_its_own(self):
        ms.reject(ms.suggest('Prefers Python for data analysis scripts and notebooks.')['id'])
        self.assertEqual(ms.suggest('Prefers Python for web servers.')['status'], 'pending')

    def test_digest_lists_rejected_facts_newest_first_and_is_bounded(self):
        self.assertEqual(ms.rejection_digest(), '')
        ms.reject(ms.suggest('Likes emoji in commit messages.')['id'], reason='never')
        ms.reject(ms.suggest('Works night shifts.')['id'])
        digest = ms.rejection_digest()
        self.assertEqual(len(digest.splitlines()), 2)
        self.assertIn('- Likes emoji in commit messages. [rejected ', digest)
        self.assertIn('(never)', digest)
        self.assertEqual(len(ms.rejection_digest(limit=1).splitlines()), 1)

    def test_bad_input_is_refused(self):
        for fact in ('', '   ', None, 'x' * (ms.MAX_FACT + 1)):
            with self.assertRaises(ms.SuggestionError):
                ms.suggest(fact)
        for sid in ('', 'nope!', 'ZZZZ', None):
            with self.assertRaises(ms.SuggestionError):
                ms.reject(sid)
        self.assertFalse((self.hq / 'memory' / 'rejected').exists())


if __name__ == '__main__':
    unittest.main()
