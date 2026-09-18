# SPDX-License-Identifier: GPL-3.0-or-later
"""Aliases: the file format, parameter substitution, and global vs local resolution (issue G8DK,
docs/AGENT-SESSIONS-PROTOCOL.md section 20).

Everything here is offline and touches only temporary directories: no model, no network, and no
alias text is ever executed.  The GUI-side rules (matching a typed line, the composer's fields)
are tested in tests/aliases_test.cpp.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import aliases, board

SQUASH = aliases.Alias(
    name="squash", kind="command", title="Squash the last N commits together",
    description="Squashes the last n commits together.",
    text="git reset --soft HEAD~{{num_commits}} && git commit",
    params=[aliases.Param("num_commits", "2", "how many commits to squash")])

REVIEW = aliases.Alias(
    name="review", kind="prompt", title="Review the diff",
    text="Review the changes under {{path}} and list what is wrong.",
    params=[aliases.Param("path", ".", "what to review")])


class SubstitutionTests(unittest.TestCase):
    """The security-critical part: a value can only ever be data, never new shell syntax."""

    def test_defaults_are_used_when_a_value_is_not_given(self):
        self.assertEqual(aliases.fill(SQUASH), "git reset --soft HEAD~2 && git commit")

    def test_a_given_value_beats_the_default(self):
        self.assertEqual(aliases.fill(SQUASH, {"num_commits": "5"}),
                         "git reset --soft HEAD~5 && git commit")

    def test_a_placeholder_with_no_default_and_no_value_is_refused(self):
        alias = aliases.Alias(name="x", text="echo {{who}}")
        self.assertEqual(alias.required(), ["who"])
        with self.assertRaises(aliases.AliasError) as caught:
            aliases.fill(alias)
        self.assertIn("who", str(caught.exception))

    def test_a_value_with_shell_metacharacters_stays_one_word(self):
        out = aliases.fill(SQUASH, {"num_commits": "2; rm -rf /"})
        self.assertEqual(out, "git reset --soft HEAD~'2; rm -rf /' && git commit")
        self.assertNotIn("~2; rm", out)

    def test_a_quoted_placeholder_swallows_its_quotes(self):
        self.assertEqual(aliases.substitute('git commit -m "{{m}}"', {"m": "wip"}),
                         "git commit -m wip")
        self.assertEqual(aliases.substitute("echo '{{m}}'", {"m": "a b"}), "echo 'a b'")

    def test_a_value_inside_single_quotes_cannot_close_them(self):
        out = aliases.substitute("echo 'x {{m}} y'", {"m": "it's"})
        self.assertEqual(out, "echo 'x it'\\''s y'")

    def test_a_value_inside_double_quotes_cannot_expand(self):
        out = aliases.substitute('echo "x {{m}} y"', {"m": "$HOME `id`"})
        self.assertNotIn('"$HOME', out)
        self.assertIn("'$HOME `id`'", out)

    @unittest.skipUnless(shutil.which("bash"), "needs bash")
    def test_bash_itself_agrees_the_value_is_one_literal_word(self):
        """The rules are checked against a real interactive bash, not only against our reading."""
        nasty = "it's $HOME `id` \"q\" !bang; rm -rf / & $(whoami)"
        for template in ('echo {{v}}', 'echo "{{v}}"', "echo '{{v}}'",
                         'echo "pre {{v}} post"', "echo 'pre {{v}} post'", 'echo pre{{v}}post'):
            with self.subTest(template=template):
                filled = aliases.substitute(template, {"v": nasty})
                script = filled.replace("echo", "printf '[%s]'", 1)
                result = subprocess.run(["bash", "-ic", script], capture_output=True, text=True,
                                        timeout=20)
                # Exactly one word, holding the value verbatim (with whatever literal prefix
                # and suffix the template asked for).
                self.assertEqual(result.stdout.count("["), 1, result.stdout)
                self.assertIn(nasty, result.stdout)

    def test_a_prompt_is_substituted_as_plain_text(self):
        self.assertEqual(aliases.fill(REVIEW, {"path": "src/"}),
                         "Review the changes under src/ and list what is wrong.")
        self.assertEqual(aliases.fill(REVIEW), "Review the changes under . and list what is wrong.")

    def test_a_substituted_value_is_never_substituted_again(self):
        alias = aliases.Alias(name="x", text="echo {{a}} {{b}}")
        self.assertEqual(aliases.substitute(alias.text, {"a": "{{b}}", "b": "B"}),
                         "echo '{{b}}' B")

    def test_a_nul_byte_is_dropped_and_a_value_is_capped(self):
        self.assertEqual(aliases.clean_value("a\x00b"), "ab")
        self.assertEqual(len(aliases.clean_value("x" * (aliases.MAX_VALUE + 99))), aliases.MAX_VALUE)

    def test_positional_arguments_fill_placeholders_in_order(self):
        alias = aliases.Alias(name="deploy", text="deploy {{env}} {{tag}}")
        self.assertEqual(aliases.positional(alias, "prod v2"), {"env": "prod", "tag": "v2"})
        self.assertEqual(aliases.positional(alias, '"my app"'), {"env": "my app"})
        self.assertEqual(aliases.positional(alias, "a b c"), {"env": "a", "tag": "b"})
        self.assertEqual(aliases.positional(alias, 'un"balanced'), {"env": 'un"balanced'})


class FormatTests(unittest.TestCase):
    def test_a_card_round_trips(self):
        card = aliases.to_card(SQUASH)
        again = aliases.from_card(card, "local")
        self.assertEqual(again.name, "squash")
        self.assertEqual(again.kind, "command")
        self.assertEqual(again.title, "Squash the last N commits together")
        self.assertEqual(again.text, SQUASH.text)
        self.assertEqual(again.description, "Squashes the last n commits together.")
        self.assertEqual([(p.name, p.default) for p in again.params], [("num_commits", "2")])

    def test_a_prompt_card_round_trips_without_a_code_fence(self):
        again = aliases.from_card(aliases.to_card(REVIEW), "global")
        self.assertEqual(again.kind, "prompt")
        self.assertEqual(again.text, REVIEW.text)

    def test_the_card_is_a_switchboard_card_the_checker_accepts(self):
        card = aliases.to_card(SQUASH)
        self.assertEqual(card.front["type"], "alias")
        self.assertIn(card.front["status"], board.ALIAS_STATUS_FOLDER)
        self.assertEqual(card.expected_folder(), "aliases")
        self.assertTrue(board.valid_id(card.id))
        self.assertTrue(board.valid_rank(card.rank))
        for key in card.front:
            self.assertIn(key, board.ALLOWED_FIELDS["alias"], key)

    def test_a_default_holding_commas_braces_and_backticks_survives(self):
        alias = aliases.Alias(name="x", text="run {{opt}}",
                              params=[aliases.Param("opt", "a, b {c} `d` 'e'")])
        again = aliases.from_card(aliases.to_card(alias), "local")
        self.assertEqual(again.params[0].default, "a, b {c} `d` 'e'")

    def test_a_command_holding_a_code_fence_is_written_with_a_wider_fence(self):
        alias = aliases.Alias(name="x", text="echo '```'")
        again = aliases.from_card(aliases.to_card(alias), "local")
        self.assertEqual(again.text, "echo '```'")

    def test_a_card_with_an_empty_run_section_is_refused(self):
        card = board.new_card("alias", "Broken", "active", name="broken", kind="command")
        card.body = "# Broken\n\n## Run\n\n"
        with self.assertRaises(aliases.AliasError):
            aliases.from_card(card, "local")

    def test_a_bad_name_or_kind_is_refused(self):
        for name in ("", "Has Caps", "has/slash", "-leading", "x" * 40):
            with self.subTest(name=name), self.assertRaises(aliases.AliasError):
                aliases.validate(aliases.Alias(name=name, text="echo hi"))
        with self.assertRaises(aliases.AliasError):
            aliases.validate(aliases.Alias(name="x", kind="script", text="echo hi"))
        with self.assertRaises(aliases.AliasError):
            aliases.validate(aliases.Alias(name="x", text="   "))

    def test_parameters_are_parsed_from_the_markdown_list(self):
        params = aliases.parse_params(
            "- `a` = `1` — first\n"
            "- `b` — second, with no default\n"
            "- ``c`` = ``x`y`` — a backtick inside\n"
            "- not a parameter line\n"
            "- `Bad Name` = `1`\n")
        self.assertEqual([(p.name, p.default, p.description) for p in params],
                         [("a", "1", "first"), ("b", None, "second, with no default"),
                          ("c", "x`y", "a backtick inside")])

    def test_a_slug_is_made_from_a_title_and_never_collides(self):
        self.assertEqual(aliases.slug("Kill the process running on a port"),
                         "kill-the-process-running-on-a")
        self.assertEqual(aliases.slug("!!!"), "alias")
        self.assertEqual(aliases.slug("squash", {"squash"}), "squash-2")
        self.assertTrue(aliases.NAME_RE.match(aliases.slug("A" * 200)))


class StoreTests(unittest.TestCase):
    """Where the files land, and which one wins when both scopes have the name."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.workspace = root / "project"
        (self.workspace / "issues").mkdir(parents=True)
        (self.workspace / "issues" / "board.yaml").write_text(board.CONFIG_TEXT)
        self.global_root = root / "global-switchboard"
        patcher = mock.patch.dict(os.environ, {"RELAY_GLOBAL_SWITCHBOARD": str(self.global_root)})
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_a_local_alias_lands_in_the_repository_switchboard(self):
        saved = aliases.save(aliases.Alias(**vars(SQUASH)), self.workspace, "local")
        self.assertEqual(Path(saved.path),
                         self.workspace / "issues" / "aliases" / "squash.md")
        self.assertTrue(Path(saved.path).exists())

    def test_a_global_alias_lands_in_the_global_switchboard(self):
        saved = aliases.save(aliases.Alias(**vars(REVIEW)), self.workspace, "global")
        self.assertEqual(Path(saved.path), self.global_root / "aliases" / "review.md")

    def test_a_project_without_a_board_falls_back_to_dot_relay(self):
        plain = Path(self.temp.name) / "plain"
        plain.mkdir()
        saved = aliases.save(aliases.Alias(**vars(SQUASH)), plain, "local")
        self.assertEqual(Path(saved.path), plain / ".relay" / "aliases" / "squash.md")

    def test_a_local_alias_hides_the_global_one_of_the_same_name(self):
        aliases.save(aliases.Alias(name="deploy", text="echo GLOBAL", title="Global deploy"),
                     self.workspace, "global")
        aliases.save(aliases.Alias(name="deploy", text="echo LOCAL", title="Local deploy"),
                     self.workspace, "local")
        self.assertEqual(aliases.resolve("deploy", self.workspace).text, "echo LOCAL")
        self.assertEqual(aliases.resolve("deploy", self.workspace, "global").text, "echo GLOBAL")
        catalog, problems = aliases.catalog(self.workspace)
        self.assertEqual(problems, [])
        self.assertEqual([(a.scope, a.shadowed) for a in catalog],
                         [("local", False), ("global", True)])

    def test_a_global_alias_is_found_with_no_project_open(self):
        aliases.save(aliases.Alias(name="review", kind="prompt", text="Review {{x}}"), None, "global")
        self.assertEqual(aliases.resolve("review", None).scope, "global")

    def test_an_unknown_name_is_refused(self):
        with self.assertRaises(aliases.AliasError):
            aliases.resolve("nope", self.workspace)

    def test_saving_twice_keeps_the_card_id_and_the_creation_date(self):
        first = aliases.save(aliases.Alias(name="x", text="echo 1"), self.workspace, "local")
        second = aliases.save(aliases.Alias(name="x", text="echo 2"), self.workspace, "local")
        self.assertEqual(second.card_id, first.card_id)
        self.assertEqual(aliases.resolve("x", self.workspace).text, "echo 2")

    def test_deleting_removes_the_file(self):
        aliases.save(aliases.Alias(name="x", text="echo 1"), self.workspace, "local")
        path = aliases.delete("x", self.workspace, "local")
        self.assertFalse(Path(path).exists())
        with self.assertRaises(aliases.AliasError):
            aliases.resolve("x", self.workspace)

    def test_an_unreadable_card_becomes_a_problem_not_an_exception(self):
        directory = aliases.alias_dir(self.workspace / "issues")
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "broken.md").write_text("---\nthis is: not [balanced\n---\n# Broken\n")
        (directory / "notalias.md").write_text("---\nid: A1B2\ntype: work\nstatus: ready\n---\n# No\n")
        aliases.save(aliases.Alias(name="good", text="echo ok"), self.workspace, "local")
        catalog, problems = aliases.catalog(self.workspace)
        self.assertEqual([a.name for a in catalog], ["good"])
        self.assertEqual(len(problems), 2)

    def test_the_board_checker_accepts_a_saved_alias(self):
        aliases.save(aliases.Alias(**vars(SQUASH)), self.workspace, "local")
        errors = [p for p in board.Board(self.workspace / "issues").check() if p.severity == "error"]
        self.assertEqual(errors, [], [str(e) for e in errors])


class RepeatTests(unittest.TestCase):
    def test_commands_run_often_enough_are_ranked(self):
        history = (["pytest tests/test_agent.py -k slow"] * 4
                   + ["docker compose up -d --build"] * 3
                   + ["ls"] * 9 + ["git status"] * 2)
        found = aliases.repeats(history, min_count=3)
        self.assertEqual([r["command"] for r in found],
                         ["pytest tests/test_agent.py -k slow", "docker compose up -d --build"])
        self.assertEqual(found[0]["count"], 4)

    def test_whitespace_is_normalized_before_counting(self):
        self.assertEqual(aliases.repeats(["make  build", "make build", " make build "], 3)[0]["count"], 3)

    def test_nothing_repeated_enough_gives_nothing(self):
        self.assertEqual(aliases.repeats(["make build", "make test"], 3), [])
        self.assertEqual(aliases.repeats([], 3), [])
        self.assertEqual(aliases.repeats(None, 3), [])


if __name__ == "__main__":
    unittest.main()
