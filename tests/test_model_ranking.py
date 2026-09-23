# SPDX-License-Identifier: AGPL-3.0-or-later
"""`backend/relay_core/model-ranking.md` and the defaults it produces (card #MDL1, design 5.4).

The owner asked for "a structured ranking MD or YAML in the repo i can review and edit" and chose
Markdown, so the two tables in that file are now the only place a model's score or a provider's
rank is written down. Three things are tested here:

* the parser — extra whitespace, a missing notes cell, a blank score, and a malformed row that
  raises with its line number rather than dropping a model quietly;
* the shipped file — it parses, `check()` finds nothing, and every model the worker's own catalog
  and the two guest CLIs name has a row;
* `presets.tier_list_defaults` — the 0 / 1 / 2 / 3-provider rules of design section 5.4, which is
  what the file is *for*. The wire shape of the two fillings is tested in tests/test_tier_lists.py.

Offline throughout: no key, no network, no guest process — the guest rows are the dicts the
`presets` event carries.
"""
import tempfile
import textwrap
import unittest
from pathlib import Path

from relay_core import model_ranking as MR
from relay_core import presets as P


def pairs(entries):
    return [(e['preset'], e['model'], e.get('effort')) for e in entries]


def names(entries):
    return [e['model'] for e in entries]


def write(text: str) -> str:
    handle = tempfile.NamedTemporaryFile('w', suffix='.md', delete=False, encoding='utf-8')
    handle.write(textwrap.dedent(text))
    handle.close()
    return handle.name


MINIMAL = """\
    # A ranking

    ## Providers

    | provider | kind | order |
    |---|---|---|
    | glm-coding | plan | 11 |
    |   openai   |  api  |  32  |

    ## Models

    | name | classes | score | notes |
    |---|---|---|---|
    | gpt-6-astra | high, main | 53 | the top one |
    | glm-5.3 |  high  main  | 45 |
    | glm-5.3-flash | flash | | no score yet
    | k3-256k | - | - | a default for nothing |
"""

# The two per-class tables added on 2026-09-21, appended to MINIMAL where a test needs them.
PER_CLASS = """\

    ## Provider picks

    | provider | high | main | flash | lite |
    |---|---|---|---|---|
    | openrouter | glm-5.3 |  | glm-5.3-flash | glm-5.3-flash |

    ## Levels

    | name | high | main | flash | lite | notes |
    |---|---|---|---|---|---|
    | gpt-6-astra | xhigh | medium | low |  | codex's own words |
    | glm-5.3 | max | high | low | - |  |
    | glm-5.3-flash |  |  |  |  | the levels are the provider's |
"""


# ----- the parser -------------------------------------------------------------------------------
class ParseTests(unittest.TestCase):
    def test_it_reads_both_tables_through_whatever_whitespace(self):
        rank = MR.parse(textwrap.dedent(MINIMAL))
        self.assertEqual(sorted(rank.providers), ['glm-coding', 'openai'])
        self.assertEqual((rank.provider_kind('openai'), rank.provider_order('openai')), ('api', 32))
        self.assertEqual(rank.provider_order('glm-coding'), 11)
        self.assertEqual(sorted(rank.models),
                         ['glm-5.3', 'glm-5.3-flash', 'gpt-6-astra', 'k3-256k'])

    def test_classes_split_on_commas_or_spaces_and_come_back_in_class_order(self):
        rank = MR.parse(textwrap.dedent(MINIMAL))
        self.assertEqual(rank.classes('gpt-6-astra'), ('high', 'main'))
        self.assertEqual(rank.classes('glm-5.3'), ('high', 'main'))      # separated by spaces
        self.assertEqual(rank.classes('glm-5.3-flash'), ('flash',))
        self.assertEqual(rank.classes('k3-256k'), ())                    # "-" is a default for nothing

    def test_a_missing_notes_cell_and_a_blank_score_are_fine(self):
        rank = MR.parse(textwrap.dedent(MINIMAL))
        self.assertIsNone(rank.score('glm-5.3-flash'))                   # blank
        self.assertIsNone(rank.score('k3-256k'))                         # "-"
        self.assertEqual(rank.models['glm-5.3'].notes, '')               # the cell was left off
        self.assertEqual(rank.models['gpt-6-astra'].notes, 'the top one')
        self.assertEqual(rank.score('gpt-6-astra'), 53)

    def test_a_name_nothing_knows_is_unscored_and_classless_rather_than_an_error(self):
        rank = MR.parse(textwrap.dedent(MINIMAL))
        self.assertIsNone(rank.score('a-model-nobody-listed'))
        self.assertEqual(rank.classes('a-model-nobody-listed'), ())
        self.assertEqual(rank.provider_order('custom:acme'), MR.UNKNOWN_PROVIDER_ORDER)
        self.assertEqual(rank.provider_kind('custom:acme'), '')
        self.assertIsNone(rank.score(None))                              # not even a string

    def test_a_malformed_row_raises_and_names_its_line(self):
        broken = MINIMAL.replace('| k3-256k | - | - | a default for nothing |',
                                 '| k3-256k | - |')
        with self.assertRaises(ValueError) as caught:
            MR.parse(textwrap.dedent(broken), 'ranking.md')
        self.assertIn('ranking.md:17', str(caught.exception))             # the row's own line
        self.assertIn('2 cell(s)', str(caught.exception))

        bad_score = MINIMAL.replace('| 53 | the top one |', '| fifty-three | the top one |')
        with self.assertRaises(ValueError) as caught:
            MR.parse(textwrap.dedent(bad_score), 'ranking.md')
        self.assertIn('ranking.md:14', str(caught.exception))
        self.assertIn("score must be a whole number, not 'fifty-three'", str(caught.exception))

        bad_kind = MINIMAL.replace('|   openai   |  api  |  32  |', '| openai | freemium | 32 |')
        with self.assertRaises(ValueError) as caught:
            MR.parse(textwrap.dedent(bad_kind), 'ranking.md')
        self.assertIn('ranking.md:8', str(caught.exception))
        self.assertIn("'freemium' is not one of", str(caught.exception))

    def test_a_missing_or_misnamed_table_raises(self):
        with self.assertRaises(ValueError) as caught:
            MR.parse('# nothing here\n', 'ranking.md')
        self.assertIn("no '## Providers' heading", str(caught.exception))
        renamed = MINIMAL.replace('| name | classes | score | notes |', '| model | classes | score |')
        with self.assertRaises(ValueError) as caught:
            MR.parse(textwrap.dedent(renamed), 'ranking.md')
        self.assertIn('expected name | classes | score | notes', str(caught.exception))

    def test_check_reports_what_parsing_let_through(self):
        text = MINIMAL.replace('| glm-5.3-flash | flash | | no score yet',
                               '| glm-5.3-flash | flashy | | a typo\n'
                               '| gpt-6-astra | main | 53 | said twice |')
        problems = MR.parse(textwrap.dedent(text)).check()
        self.assertTrue(any("unknown class 'flashy'" in line for line in problems), problems)
        self.assertTrue(any("'gpt-6-astra' has more than one row" in line for line in problems), problems)
        # ... and the two structural checks, on a file that only names two providers.
        self.assertTrue(any("'kimi' has no row in the Providers table" in line for line in problems), problems)
        self.assertTrue(any("'kimi-k3'" in line and 'Models table' in line for line in problems), problems)

    def test_the_two_per_class_tables_are_optional(self):
        """A file with neither still parses, and every rule they carry stays where it was.

        That is what lets the owner add one table at a time — and what keeps a worker shipped
        with an older copy of the file running instead of refusing to start.
        """
        rank = MR.parse(textwrap.dedent(MINIMAL))
        self.assertEqual(rank.picks, {})
        self.assertEqual(rank.levels, {})
        self.assertIsNone(rank.level('glm-5.3', 'high'))
        self.assertIsNone(rank.pick('openrouter', 'lite'))
        self.assertEqual(rank.level_columns, ())
        self.assertEqual([line for line in rank.check() if 'Levels' in line or 'picks' in line], [])

    def test_it_reads_the_provider_picks_and_levels_tables(self):
        rank = MR.parse(textwrap.dedent(MINIMAL + PER_CLASS))
        self.assertEqual(rank.pick('openrouter', 'high'), 'glm-5.3')
        self.assertEqual(rank.pick('openrouter', 'lite'), 'glm-5.3-flash')
        self.assertIsNone(rank.pick('openrouter', 'main'))          # a blank cell says nothing
        self.assertIsNone(rank.pick('openai', 'high'))              # no row at all says nothing
        self.assertEqual(rank.level('gpt-6-astra', 'high'), 'xhigh')
        self.assertEqual(rank.level('gpt-6-astra', 'main'), 'medium')
        self.assertIsNone(rank.level('gpt-6-astra', 'lite'))
        self.assertIsNone(rank.level('glm-5.3-flash', 'high'))      # a row of nothing but notes
        self.assertIsNone(rank.level('k3-256k', 'high'))
        # A cell is a word, not a sentence: "-" and "none" mean the same as leaving it blank.
        self.assertIsNone(rank.level('glm-5.3', 'lite'))

    def test_a_class_column_may_be_left_out_or_re_ordered(self):
        """The header says which class a cell is in, so the file can write the four in any order
        and leave out a class no model starts at a level in."""
        text = MINIMAL + """\
            ## Levels

            | name | flash | high |
            |---|---|---|
            | glm-5.3 | low | max |
        """
        rank = MR.parse(textwrap.dedent(text))
        self.assertEqual(rank.level_columns, ('flash', 'high'))
        self.assertEqual(rank.level('glm-5.3', 'high'), 'max')
        self.assertEqual(rank.level('glm-5.3', 'flash'), 'low')
        self.assertIsNone(rank.level('glm-5.3', 'main'))
        self.assertEqual([line for line in rank.check() if 'Levels' in line], [])

    def test_a_per_class_table_that_is_not_the_table_it_says_it_is_raises(self):
        text = MINIMAL + """\
            ## Levels

            | model | high |
            |---|---|
            | glm-5.3 | max |
        """
        with self.assertRaises(ValueError) as caught:
            MR.parse(textwrap.dedent(text), 'ranking.md')
        self.assertIn("the Levels table's first column is 'model'", str(caught.exception))
        self.assertIn("expected 'name'", str(caught.exception))

    def test_check_reports_an_unknown_column_and_a_key_neither_old_table_names(self):
        text = MINIMAL + """\
            ## Provider picks

            | provider | high |
            |---|---|
            | openrouter | glm-5.3 |
            | glm-coding | kimi-k3 |

            ## Levels

            | name | high | fast | notes |
            |---|---|---|---|
            | glm-5.3 | max | low | |
            | gpt-6-astra-mini | high | | renamed |
        """
        problems = MR.parse(textwrap.dedent(text)).check()
        self.assertTrue(any("column 'fast'" in line and 'Levels' in line for line in problems), problems)
        self.assertTrue(any("row for 'gpt-6-astra-mini'" in line for line in problems), problems)
        # `openrouter` is not one of this file's two providers, and kimi-k3 is not one of its models.
        self.assertTrue(any("row for 'openrouter'" in line for line in problems), problems)
        self.assertTrue(any("glm-coding's high pick is 'kimi-k3'" in line for line in problems), problems)

    def test_check_reports_two_providers_sharing_an_order(self):
        """`order` is the tie-break, so it decides nothing when two rows share a number.

        It is the slip a re-numbering makes — a band is renumbered and one row keeps its old
        value, or two bands are given the same first number — and it is well-formed Markdown, so
        parsing cannot catch it. It is a finding rather than a raise: the owner edits this file by
        hand and a worker that refuses to start over a repeated number would be worse than one
        that sorts two providers by name for an afternoon.
        """
        text = MINIMAL.replace('|   openai   |  api  |  32  |', '| openai | api | 11 |')
        ranking = MR.parse(textwrap.dedent(text), 'ranking.md')
        self.assertEqual(ranking.provider_order('openai'), 11)      # parsed, not refused
        problems = ranking.check()
        shared = [line for line in problems if 'order 11' in line]
        self.assertEqual(len(shared), 1, problems)
        self.assertIn('glm-coding', shared[0])
        self.assertIn('openai', shared[0])
        self.assertIn('tie-break', shared[0])
        # Distinct numbers say nothing at all.
        clean = MR.parse(textwrap.dedent(MINIMAL), 'ranking.md').check()
        self.assertEqual([line for line in clean if 'tie-break' in line], [])

    def test_a_missing_file_says_so_rather_than_ranking_nothing(self):
        # With no file there is no score, no class and no provider order, so every default list
        # would come out blank and read like a ranking decision. It ships with the worker.
        with self.assertRaises(FileNotFoundError) as caught:
            MR.load('/nonexistent/model-ranking.md')
        self.assertIn('the model ranking file the defaults are built from is missing',
                      str(caught.exception))

    def test_load_parses_once_per_path_and_reload_reads_it_again(self):
        path = write(MINIMAL)
        try:
            first = MR.load(path)
            self.assertIs(MR.load(path), first)                 # cached per process
            Path(path).write_text(textwrap.dedent(MINIMAL).replace('| 53 |', '| 99 |'),
                                  encoding='utf-8')
            self.assertIs(MR.load(path), first)                 # still the cached one
            self.assertEqual(MR.reload(path).score('gpt-6-astra'), 99)
        finally:
            MR.reload(path=None)                                # leave the shipped file cached alone
            Path(path).unlink()


# ----- the file this repo ships -----------------------------------------------------------------
class ShippedFileTests(unittest.TestCase):
    def setUp(self):
        self.rank = MR.load()

    def test_it_parses_and_check_finds_nothing(self):
        self.assertEqual(self.rank.check(), [])

    def test_every_provider_the_worker_has_is_a_row_and_nothing_else_is(self):
        """What must hold whatever the owner ranks — and nothing more.

        This test used to assert that the bands ascend in `MR.KINDS` order, which made the
        preference order a thing the code owned and the file merely repeated. On 2026-09-21 the
        owner put the harnesses ahead of the plans and the test failed on a file that was doing
        exactly what it exists for. So the order is not asserted here at all: `Ranking.kinds()`
        derives it from the table (tested below), and what is left is the structure — every
        provider has a row, nothing that is not a provider does, every kind is a word the parser
        allows, and the numbers can actually break a tie.
        """
        self.assertEqual(sorted(self.rank.providers),
                         sorted(list(P.PRESETS) + ['guest:claude', 'guest:codex']))
        for preset_id, row in self.rank.providers.items():
            with self.subTest(preset_id):
                self.assertIn(row.kind, MR.KINDS)
                # A row has to sort before a provider that has no row at all.
                self.assertLess(row.order, MR.UNKNOWN_PROVIDER_ORDER)
        # `order` is the tie-break, so it decides nothing unless it is unique.
        orders = [row.order for row in self.rank.providers.values()]
        self.assertEqual(sorted(orders), sorted(set(orders)))
        # The two rows nothing else can settle: a guest CLI is a harness and Relay Free is free.
        self.assertEqual(self.rank.provider_kind('guest:codex'), 'harness')
        self.assertEqual(self.rank.provider_kind('relay-free'), 'free')
        # Relay's own allowance is spent last, whatever else the owner moves.
        self.assertEqual(self.rank.kinds()[-1], 'free')

    def test_the_kind_order_is_read_off_the_file_and_not_out_of_KINDS(self):
        """`MR.KINDS` is the set of words a `kind` cell may hold. It is not the order.

        The order is each band by its lowest `order`, because that is the row that wins a tie
        against another band, and it changes when the owner re-numbers the table — which is the
        whole point of the table.
        """
        text = MINIMAL.replace('| glm-coding | plan | 11 |\n', '')
        text = text.replace('|   openai   |  api  |  32  |',
                            '| openai | api | 10 |\n| guest:codex | harness | 20 |\n'
                            '| glm-coding | plan | 30 |\n| relay-free | free | 90 |')
        rank = MR.parse(textwrap.dedent(text), 'ranking.md')
        self.assertEqual(rank.kinds(), ('api', 'harness', 'plan', 'free'))
        # Re-number it and the answer follows, with nothing in the code touched.
        flipped = textwrap.dedent(text).replace('| openai | api | 10 |', '| openai | api | 40 |')
        self.assertEqual(MR.parse(flipped, 'ranking.md').kinds(),
                         ('harness', 'plan', 'api', 'free'))
        # A band is its *lowest* row, not its first or its last.
        two = textwrap.dedent(text).replace('| glm-coding | plan | 30 |',
                                            '| glm-coding | plan | 30 |\n| kimi-code | plan | 5 |')
        self.assertEqual(MR.parse(two, 'ranking.md').kinds()[0], 'plan')

    def test_check_reports_an_order_that_reaches_the_no_row_placeholder(self):
        # A provider with no row sorts at UNKNOWN_PROVIDER_ORDER, which only means "last" while
        # every row in the file is below it.
        text = MINIMAL.replace('|   openai   |  api  |  32  |',
                               f'| openai | api | {MR.UNKNOWN_PROVIDER_ORDER} |')
        problems = MR.parse(textwrap.dedent(text), 'ranking.md').check()
        hit = [line for line in problems if 'at or past' in line]
        self.assertEqual(len(hit), 1, problems)
        self.assertIn("'openai'", hit[0])
        self.assertEqual([l for l in MR.parse(textwrap.dedent(MINIMAL)).check()
                          if 'at or past' in l], [])

    def test_the_paid_for_plan_is_spent_before_the_metered_api(self):
        # Design rule 2.2's one claim that is not about band order: two presets of one company,
        # and the credit already paid for goes first. The owner can re-band the table without
        # touching this, and if he ever does mean to change it, this is the line that says so.
        self.assertLess(self.rank.provider_order('glm-coding'), self.rank.provider_order('glm'))
        self.assertLess(self.rank.provider_order('kimi-code'), self.rank.provider_order('kimi'))
        self.assertLess(self.rank.provider_order('kimi-code'), self.rank.provider_order('kimi'))

    def test_every_catalog_model_and_every_guest_model_has_a_row(self):
        for preset_id, rows in P.MODEL_CATALOG.items():
            for row in rows:
                with self.subTest(preset=preset_id, model=row['id']):
                    self.assertIn(P.model_name(preset_id, row['id']), self.rank.models)
        for guest, models in MR._GUEST_MODEL_IDS.items():
            for model in models:
                with self.subTest(guest=guest, model=model):
                    self.assertIn(P.model_name('guest:' + guest, model), self.rank.models)
        # The guest lists this file checks against are the ones the adapters really name.
        from relay_core.guest_harness_claude import MODEL_ALIASES
        from relay_core.guest_harness_provider import _CODEX_FALLBACK_MODELS
        self.assertEqual(MR._GUEST_MODEL_IDS['claude'], tuple(MODEL_ALIASES))
        self.assertEqual(MR._GUEST_MODEL_IDS['codex'],
                         tuple(row['id'] for row in _CODEX_FALLBACK_MODELS))

    def test_the_rows_are_sorted_by_score_then_name(self):
        order = [(-(row.score if row.score is not None else -1), row.name)
                 for row in self.rank.models.values()]
        self.assertEqual(order, sorted(order), 'model-ranking.md is no longer sorted')

    def test_intelligence_is_a_view_over_the_file(self):
        # Nothing that reads presets.INTELLIGENCE changed: it still answers .get, [] and `in`.
        self.assertEqual(P.INTELLIGENCE['kimi-k3'], self.rank.score('kimi-k3'))
        self.assertEqual(dict(P.INTELLIGENCE),
                         {name: row.score for name, row in self.rank.models.items()})
        self.assertIsNone(P.INTELLIGENCE.get('a-model-nobody-listed'))
        self.assertNotIn('a-model-nobody-listed', P.INTELLIGENCE)

    def test_relay_free_owns_the_three_role_names(self):
        self.assertEqual(self.rank.classes('relay-main'), ('high', 'main'))
        self.assertEqual(self.rank.classes('relay-flash'), ('flash',))
        self.assertEqual(self.rank.classes('relay-lite'), ('lite',))

    def test_a_serving_variant_has_its_own_row(self):
        for name in ('k3-256k', 'kimi-k2.7-code-highspeed', 'minimax-m2.7-highspeed'):
            self.assertIn(name, self.rank.models, name)
        self.assertEqual(self.rank.classes('k3-256k'), ())          # a variant no tier names


# ----- what the file is for: the defaults of design 5.4 -----------------------------------------
CLAUDE = {'id': 'guest:claude', 'harness': True, 'logged_in': True, 'guest': 'claude',
          'efforts': ['low', 'medium', 'high', 'xhigh', 'max'],
          'models': [{'id': 'fable'}, {'id': 'opus'}, {'id': 'sonnet'}, {'id': 'haiku'}]}
SIGNED_OUT = dict(CLAUDE, id='guest:codex', guest='codex', logged_in=False, models=[])


class DefaultRulesTests(unittest.TestCase):
    """Owner, 2026-09-21: "default without installing any providers -- you get the 3 relay models.
    default with 1 provider -- you get 1 each high / main / flash models. dont pick 2 options from
    the same provider. default with 2+ providers -- 2 each"."""

    def defaults(self, usable, **kwargs):
        kwargs.setdefault('listing', [])          # never touch the OpenRouter cache from a test
        return P.tier_list_defaults(usable, **kwargs)['plain']

    def test_no_providers_gives_relay_frees_three(self):
        lists = self.defaults(['relay-free'])
        self.assertEqual(pairs(lists['main']), [('relay-free', 'relay-main', 'medium')])
        self.assertEqual(pairs(lists['high']), [('relay-free', 'relay-main', 'medium')])
        self.assertEqual(pairs(lists['flash']), [('relay-free', 'relay-flash', 'low')])
        self.assertEqual(pairs(lists['lite']), [('relay-free', 'relay-lite', 'low')])

    def test_no_providers_and_no_relay_free_gives_nothing_but_the_local_list(self):
        lists = self.defaults([], local=[('local:bonsai', 'bonsai-2-27b')])
        self.assertEqual([tier for tier, entries in lists.items() if entries], ['local'])

    def test_relay_free_disappears_the_moment_anything_else_can_take_a_turn_except_in_lite(self):
        """Relay Free is what "no providers" means — with one exception since 2026-09-21.

        The owner: "for lite, i am thinking to simplify that and just everybody is on relay free
        by default, or openrouter if they want privacy". A Lite call is a title, a label or a
        duplicate check, and spending a subscription or a metered key on one is what the
        allowance exists to avoid. High, Main and Flash are unchanged.
        """
        for usable, guests in ((['relay-free', 'glm-coding'], ()),
                               (['relay-free'], [CLAUDE])):
            lists = self.defaults(usable, guests=guests)
            for tier in ('main', 'high', 'flash'):
                self.assertNotIn('relay-free', [e['preset'] for e in lists[tier]], (usable, tier))
            self.assertEqual(pairs(lists['lite']), [('relay-free', 'relay-lite', 'low')], usable)
        # Without Relay Free, lite is ranked out of the file like every other class.
        lists = self.defaults(['gemini'])
        self.assertEqual(pairs(lists['lite']), [('gemini', 'gemini-3.5-flash-lite', 'low')])

    def test_one_provider_gives_one_model_per_class_the_highest_scoring_one(self):
        lists = self.defaults(['openai'])
        # The levels are the file's Levels cells, in the endpoint's own words: the owner wrote
        # `gpt-6-astra | high = xhigh, main = medium` ("the API's default is high; codex's own is
        # medium"), and `gpt-6-luna | flash = low`. He also moved openai's flash model from
        # terra to luna and left terra a default for nothing.
        self.assertEqual(pairs(lists['main']), [('openai', 'gpt-6-astra', 'medium')])
        self.assertEqual(pairs(lists['high']), [('openai', 'gpt-6-astra', 'xhigh')])
        self.assertEqual(pairs(lists['flash']), [('openai', 'gpt-6-luna', 'low')])
        # Lite is Relay Free's, and Relay Free cannot run here, so there is nothing in it.
        self.assertEqual(pairs(lists['lite']), [])
        # gpt-6-sol scores 47 — above terra and luna — and is in no class, so it is a default for
        # nothing. That is the file's decision to make, and it is written down in it.
        self.assertEqual(MR.load().score('gpt-6-sol'), 47)
        self.assertNotIn('gpt-6-sol', [e['model'] for entries in lists.values() for e in entries])

    def test_one_provider_means_one_company_not_one_key(self):
        # Owner: "dont pick 2 options from the same provider". `glm` and `glm-coding` are both
        # z.ai serving glm-5.3, so two keys are still one provider — and the coding plan wins the
        # tie, so the credit already paid for is spent first.
        lists = self.defaults(['glm', 'glm-coding'])
        self.assertEqual(pairs(lists['main']), [('glm-coding', 'glm-5.3', 'high')])
        self.assertEqual(pairs(lists['flash']), [('glm-coding', 'glm-5.3-flash', 'high')])

    def test_two_providers_give_two_per_class_by_score(self):
        lists = self.defaults(['openai', 'glm-coding'])
        self.assertEqual(pairs(lists['main']),
                         [('openai', 'gpt-6-astra', 'medium'), ('glm-coding', 'glm-5.3', 'high')])
        self.assertEqual(pairs(lists['high']),
                         [('openai', 'gpt-6-astra', 'xhigh'), ('glm-coding', 'glm-5.3', 'max')])
        self.assertEqual([MR.load().score(n) for n in ('gpt-6-astra', 'glm-5.3')], [53, 45])

    def test_three_providers_still_give_two_per_class_and_never_two_from_one(self):
        lists = self.defaults(['openai', 'glm-coding', 'kimi'])
        for tier in ('main', 'high', 'flash', 'lite'):
            entries = lists[tier]
            self.assertLessEqual(len(entries), 2, tier)
            self.assertEqual(len({e['preset'] for e in entries}), len(entries), tier)
            self.assertEqual(len({e['model'] for e in entries}), len(entries), tier)
        self.assertEqual(names(lists['main']), ['gpt-6-astra', 'glm-5.3'])     # 53, 45; kimi-k3 44

    def test_a_blank_score_sorts_last_and_ties_break_by_provider_order_then_name(self):
        # No flash model is scored, so the whole flash class is a tie and the providers' `order`
        # decides: the coding plan (11), then minimax (12), ahead of openai (32) and kimi (30).
        lists = self.defaults(['openai', 'glm-coding', 'kimi', 'minimax'])
        self.assertEqual(pairs(lists['flash']),
                         [('glm-coding', 'glm-5.3-flash', 'high'),
                          ('minimax', 'MiniMax-M2.7-highspeed', None)])
        rank = MR.load()
        self.assertEqual([rank.score(n) for n in ('glm-5.3-flash', 'minimax-m2.7-highspeed')],
                         [None, None])
        self.assertLess(rank.provider_order('glm-coding'), rank.provider_order('minimax'))
        # minimax-m3 is unscored, so it loses main to the three that are scored.
        self.assertEqual(names(lists['main']), ['gpt-6-astra', 'glm-5.3'])

    def test_a_harness_counts_as_a_provider_and_is_ranked_by_name(self):
        # With Claude Code alone it is the one provider, so one model per class — and its `opus` is
        # claude-opus-5-5, which the file scores 51, above `sonnet` and `haiku` and above `fable`,
        # which no class names.
        alone = self.defaults([], guests=[CLAUDE])
        # `opus` is claude-opus-5-5, which the file classes for main and scores 51; `fable` is
        # claude-fable-5.1, which the owner classed for high on 2026-09-21 and scores 53. The
        # levels are his Levels cells, in Claude Code's own words — the vocabulary the guest
        # reports, not Relay's four.
        self.assertEqual(pairs(alone['main']), [('guest:claude', 'opus', 'high')])
        self.assertEqual(pairs(alone['high']), [('guest:claude', 'fable', 'high')])
        # A harness is offered for high, main and flash (owner, 2026-09-21: "the worker should
        # allow the harness for flash"), never for lite: lite is nothing but background jobs, and
        # roles.py will not hand one to an agent of its own. `sonnet` is claude-sonnet-5, the
        # file's flash model.
        self.assertEqual(P.GUEST_CLASSES, ('high', 'main', 'flash'))
        self.assertEqual(pairs(alone['flash']), [('guest:claude', 'sonnet', 'low')])
        self.assertEqual(alone['lite'], [])
        # Beside one API provider it is two providers, so two each, and claude-opus-5-5 (51) beats
        # glm-5.3 (45).
        with_glm = self.defaults(['glm-coding'], guests=[CLAUDE])
        self.assertEqual(pairs(with_glm['main']),
                         [('guest:claude', 'opus', 'high'), ('glm-coding', 'glm-5.3', 'high')])
        # Two providers, two per class — and the harness takes a flash slot now, beside z.ai's.
        self.assertEqual(pairs(with_glm['flash']),
                         [('guest:claude', 'sonnet', 'low'), ('glm-coding', 'glm-5.3-flash', 'high')])

    def test_a_signed_out_or_unavailable_guest_is_not_a_provider(self):
        lists = self.defaults(['glm-coding'], guests=[SIGNED_OUT])
        self.assertEqual(pairs(lists['main']), [('glm-coding', 'glm-5.3', 'high')])   # one provider
        self.assertNotIn('guest:codex', [e['preset'] for entries in lists.values() for e in entries])

    def test_the_same_model_from_two_providers_is_listed_once(self):
        # The OpenAI API and codex both serve gpt-6-astra. One row, from the provider the file
        # ranks first — and the second slot goes to somebody else's model.
        codex = {'id': 'guest:codex', 'harness': True, 'logged_in': True, 'guest': 'codex',
                 'efforts': ['low', 'medium', 'high', 'xhigh', 'max', 'ultra'],
                 'models': [{'id': 'gpt-6-astra', 'default_effort': 'medium'},
                            {'id': 'gpt-5.6-terra', 'default_effort': 'medium'}]}
        lists = self.defaults(['openai', 'glm-coding'], guests=[codex])
        self.assertEqual(names(lists['main']), ['gpt-6-astra', 'glm-5.3'])
        self.assertEqual(lists['main'][0]['preset'], 'guest:codex')    # harness (21) before api (32)

    def test_a_keyed_custom_provider_counts_and_is_ranked_by_name_where_the_file_knows_it(self):
        lists = self.defaults([], custom=[('custom:acme', 'acme-1')])
        self.assertEqual(pairs(lists['main']), [('custom:acme', 'acme-1', None)])
        self.assertEqual(pairs(lists['high']), [('custom:acme', 'acme-1', None)])
        self.assertEqual((lists['flash'], lists['lite']), ([], []))
        # A custom endpoint serving a model the file *does* know is scored and classed like it.
        lists = self.defaults(['openai'], custom=[('custom:acme', 'claude-opus-5-5')])
        self.assertEqual(names(lists['main']), ['gpt-6-astra', 'claude-opus-5-5'])

    def test_a_local_endpoint_is_not_a_provider_and_fills_local_as_before(self):
        lists = self.defaults(['relay-free'], local=[('local:bonsai', 'bonsai-2-27b'),
                                                     ('local:two', 'other')])
        self.assertEqual(pairs(lists['local']),
                         [('local:bonsai', 'bonsai-2-27b', None), ('local:two', 'other', None)])
        self.assertEqual(pairs(lists['main']), [('relay-free', 'relay-main', 'medium')])

    def test_lite_is_relay_free_by_default_and_openrouter_in_the_other_variant(self):
        """Owner, 2026-09-21: "for lite ... everybody is on relay free by default, or openrouter
        if they want privacy". The two buttons are where that choice is made."""
        both = P.tier_list_defaults(['relay-free', 'openai', 'openrouter'], listing=[])
        self.assertEqual(pairs(both['plain']['lite']), [('relay-free', 'relay-lite', 'low')])
        # The openrouter variant leads with OpenRouter's own lite pick — the Provider picks row
        # in the file, not a constant in the code — and keeps relay-lite behind it.
        self.assertEqual(pairs(both['openrouter']['lite'])[0],
                         ('openrouter', 'google/gemini-3.5-flash-lite', 'low'))
        self.assertIn(('relay-free', 'relay-lite', 'low'), pairs(both['openrouter']['lite']))
        self.assertEqual(MR.load().pick('openrouter', 'lite'), 'gemini-3.5-flash-lite')

    def test_a_provider_picks_row_replaces_that_providers_candidate_for_a_class(self):
        """`## Provider picks` is "a provider whose defaults differ from the shared rows", and the
        cell wins outright: OpenRouter's Main is glm-5.3-flash however the Models table classes
        it. Only `openrouter` has a row today; the owner asked for the table so that "we could
        add that for cerebras for example later on"."""
        rank = MR.load()
        self.assertEqual(rank.pick('openrouter', 'main'), 'glm-5.3-flash')
        self.assertEqual(P.provider_model_id('openrouter', 'glm-5.3-flash'), 'z-ai/glm-5.3-flash')
        lists = P.tier_list_defaults(['openrouter'], listing=[])['plain']
        self.assertEqual(names(lists['main']), ['z-ai/glm-5.3-flash'])
        self.assertEqual(names(lists['high']), ['z-ai/glm-5.3'])
        self.assertEqual(names(lists['flash']), ['deepseek/deepseek-v4.1-flash'])
        # Neither glm slug is in OpenRouter's own three catalog rows: the pick names a model and
        # `provider_model_id` finds the slug that serves it through the twin map.
        self.assertNotIn('z-ai/glm-5.3', [row['id'] for row in P.MODEL_CATALOG['openrouter']])
        # A provider with no row follows the Models table, as everyone but OpenRouter does.
        self.assertIsNone(rank.pick('openai', 'main'))


if __name__ == '__main__':
    unittest.main()
