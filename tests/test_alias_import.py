# SPDX-License-Identifier: GPL-3.0-or-later
"""Importing Warp workflows and shell aliases (issue G8DK, protocol section 19).

Offline, and nothing imported is ever executed: the tests read fixtures out of temporary
directories and assert on the text that *would* be stored.  One test additionally reads this
machine's real Warp database when there is one, because the importer was written against it --
it is skipped, not failed, where Warp is not installed.
"""
import json
import os
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import alias_import, aliases

WARP_ROWS = [
    {"name": "Squash the last N commits together",
     "command": "git reset --soft HEAD~{{num_commits}} && git commit",
     "tags": ["git"], "description": "Squashes the last n commits together.",
     "arguments": [{"name": "num_commits", "arg_type": "Text",
                    "description": "How many commits", "default_value": None}],
     "shells": []},
    {"name": "Example workflow", "command": "echo hello, {{your_name}}", "tags": [],
     "description": "A workflow is a templated command.",
     "arguments": [{"name": "your_name", "arg_type": "Text", "description": "Who",
                    "default_value": "world"}], "shells": ["bash"]},
    {"type": "agent_mode", "name": "update system",
     "query": "sudo apt update && sudo apt upgrade -y", "description": "update system",
     "arguments": []},
    {"name": "No command at all", "tags": [], "arguments": []},
    {"command": "echo nameless"},
]

WARP_YAML = """\
---
name: Deploy a service
command: |
  kubectl -n {{namespace}} rollout restart deploy/{{service}}
  kubectl -n {{namespace}} rollout status deploy/{{service}}
tags: [k8s, deploy]
description: Restart a deployment and wait for it.
arguments:
  - name: namespace
    description: The namespace
    default_value: "default"
  - name: service
    description: The deployment
shells: [bash, zsh]
"""

BASHRC = """\
# a comment
alias ll='ls -alF'
alias gs='git status --short --branch'
alias gco="git checkout"
alias alert='notify-send -i "$([ $? = 0 ] && echo terminal || echo error)" "$(history|tail -n1|sed -e '\\''s/^ *//'\\'')"'
alias broken='unterminated
alias continued='one \\
alias
export NOT_AN_ALIAS=1
"""


def warp_db(directory: Path, rows) -> Path:
    path = directory / "warp.sqlite"
    connection = sqlite3.connect(path)
    connection.execute("create table workflows (id integer not null primary key, data text not null)")
    for index, row in enumerate(rows, 1):
        payload = row if isinstance(row, str) else json.dumps(row)
        connection.execute("insert into workflows (id, data) values (?, ?)", (index, payload))
    connection.commit()
    connection.close()
    return path


class TempHome(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / "home"
        self.home.mkdir()
        self.workspace = self.root / "project"
        self.workspace.mkdir()
        patcher = mock.patch.dict(
            os.environ, {"RELAY_GLOBAL_SWITCHBOARD": str(self.root / "global")})
        patcher.start()
        self.addCleanup(patcher.stop)


class WarpImportTests(TempHome):
    def test_workflows_come_out_of_the_database_with_their_parameters(self):
        path = warp_db(self.root, WARP_ROWS)
        result = alias_import.preview(("warp",), workspace=self.workspace,
                                      sqlite_path=path, yaml_dirs=[])
        by_name = {item["name"]: item for item in result["items"]}
        self.assertEqual(sorted(by_name), ["example-workflow", "squash-the-last-n-commits",
                                           "update-system"])
        squash = by_name["squash-the-last-n-commits"]
        self.assertEqual(squash["kind"], "command")
        self.assertEqual(squash["text"], "git reset --soft HEAD~{{num_commits}} && git commit")
        self.assertEqual(squash["params"], [{"name": "num_commits", "default": None,
                                             "description": "How many commits"}])
        self.assertEqual(squash["labels"], ["git"])
        self.assertEqual(by_name["example-workflow"]["params"][0]["default"], "world")

    def test_an_agent_mode_workflow_imports_as_a_prompt(self):
        path = warp_db(self.root, WARP_ROWS)
        result = alias_import.preview(("warp",), workspace=self.workspace,
                                      sqlite_path=path, yaml_dirs=[])
        update = next(i for i in result["items"] if i["name"] == "update-system")
        self.assertEqual(update["kind"], "prompt")
        self.assertEqual(update["text"], "sudo apt update && sudo apt upgrade -y")

    def test_a_workflow_with_no_command_or_no_name_is_skipped_with_a_reason(self):
        path = warp_db(self.root, WARP_ROWS)
        result = alias_import.preview(("warp",), workspace=self.workspace,
                                      sqlite_path=path, yaml_dirs=[])
        reasons = " ".join(s["reason"] for s in result["skipped"])
        self.assertIn("has no command text", reasons)
        self.assertIn("has no name", reasons)

    def test_malformed_rows_are_skipped_not_fatal(self):
        path = warp_db(self.root, ["{not json at all", json.dumps(["a", "list"]),
                                   json.dumps(WARP_ROWS[0])])
        result = alias_import.preview(("warp",), workspace=self.workspace,
                                      sqlite_path=path, yaml_dirs=[])
        self.assertEqual([i["name"] for i in result["items"]], ["squash-the-last-n-commits"])
        self.assertEqual(len(result["skipped"]), 2)

    def test_a_database_that_is_not_one_is_reported_rather_than_raised(self):
        path = self.root / "warp.sqlite"
        path.write_bytes(b"this is not a database")
        result = alias_import.preview(("warp",), workspace=self.workspace,
                                      sqlite_path=path, yaml_dirs=[])
        self.assertEqual(result["items"], [])
        self.assertEqual(len(result["skipped"]), 1)

    def test_a_missing_database_is_simply_empty(self):
        result = alias_import.preview(("warp",), workspace=self.workspace,
                                      sqlite_path=self.root / "nope.sqlite", yaml_dirs=[])
        self.assertEqual(result["items"], [])
        self.assertEqual(result["skipped"], [])

    def test_the_live_database_is_never_written_to(self):
        path = warp_db(self.root, WARP_ROWS)
        before = path.read_bytes()
        alias_import.preview(("warp",), workspace=self.workspace, sqlite_path=path, yaml_dirs=[])
        self.assertEqual(path.read_bytes(), before)

    def test_a_workflow_yaml_file_is_read(self):
        directory = self.root / "workflows"
        directory.mkdir()
        (directory / "deploy.yaml").write_text(WARP_YAML)
        result = alias_import.preview(("warp",), workspace=self.workspace,
                                      sqlite_path=self.root / "none.sqlite", yaml_dirs=[directory])
        item = result["items"][0]
        self.assertEqual(item["name"], "deploy-a-service")
        self.assertEqual(item["source"], "warp-yaml")
        self.assertIn("rollout restart deploy/{{service}}", item["text"])
        self.assertIn("rollout status", item["text"])
        self.assertEqual([(p["name"], p["default"]) for p in item["params"]],
                         [("namespace", "default"), ("service", None)])
        self.assertEqual(item["labels"], ["k8s", "deploy"])
        self.assertEqual(item["shell"], "bash")

    def test_a_malformed_yaml_file_is_skipped_with_a_reason(self):
        directory = self.root / "workflows"
        directory.mkdir()
        (directory / "bad.yaml").write_text("\t- this: [is, not\n  a workflow at all\n")
        (directory / "good.yaml").write_text(WARP_YAML)
        result = alias_import.preview(("warp",), workspace=self.workspace,
                                      sqlite_path=self.root / "none.sqlite", yaml_dirs=[directory])
        self.assertEqual([i["name"] for i in result["items"]], ["deploy-a-service"])
        self.assertEqual(len(result["skipped"]), 1)
        self.assertIn("bad.yaml", result["skipped"][0]["origin"])


class ShellImportTests(TempHome):
    def test_aliases_are_read_out_of_a_startup_file(self):
        (self.home / ".bashrc").write_text(BASHRC)
        result = alias_import.preview(("shell",), workspace=self.workspace, home=self.home)
        by_name = {item["name"]: item for item in result["items"]}
        self.assertEqual(sorted(by_name), ["alert", "gco", "gs"])
        self.assertEqual(by_name["gs"]["text"], "git status --short --branch")
        self.assertEqual(by_name["gco"]["text"], "git checkout")
        self.assertEqual(by_name["gs"]["kind"], "command")

    def test_bashs_own_quote_escape_is_unquoted_the_way_bash_does(self):
        self.assertEqual(alias_import.unquote_word("'it'\\''s'"), "it's")
        self.assertEqual(alias_import.unquote_word('"a $b `c`"'), "a $b `c`")
        self.assertEqual(alias_import.unquote_word("bare --flag"), "bare --flag")
        with self.assertRaises(ValueError):
            alias_import.unquote_word("'never closed")

    def test_a_malformed_alias_line_is_reported_not_guessed_at(self):
        (self.home / ".bashrc").write_text(BASHRC)
        result = alias_import.preview(("shell",), workspace=self.workspace, home=self.home)
        reasons = " ".join(s["reason"] for s in result["skipped"])
        self.assertIn("unbalanced single quote", reasons)
        self.assertIn("continued over more than one line", reasons)
        self.assertIn("not `alias name=value`", reasons)

    def test_aliases_that_only_respell_a_standard_command_are_skipped(self):
        (self.home / ".bashrc").write_text(BASHRC)
        result = alias_import.preview(("shell",), workspace=self.workspace, home=self.home)
        self.assertNotIn("ll", [i["name"] for i in result["items"]])

    def test_nothing_is_executed_and_nothing_is_written_by_a_preview(self):
        marker = self.root / "RAN"
        (self.home / ".bashrc").write_text(
            f"alias boom='touch {marker}'\n$(touch {marker})\n`touch {marker}`\n")
        result = alias_import.preview(("shell",), workspace=self.workspace, home=self.home)
        self.assertFalse(marker.exists())
        self.assertEqual(result["items"][0]["text"], f"touch {marker}")
        self.assertFalse((self.root / "global").exists())

    def test_a_startup_file_that_is_a_symlink_is_refused(self):
        (self.root / "elsewhere").write_text("alias x='echo hi'\n")
        (self.home / ".bashrc").symlink_to(self.root / "elsewhere")
        result = alias_import.preview(("shell",), workspace=self.workspace, home=self.home)
        self.assertEqual(result["items"], [])
        self.assertIn("symbolic link", result["skipped"][0]["reason"])


class PreviewAndApplyTests(TempHome):
    def preview(self):
        (self.home / ".bashrc").write_text(BASHRC)
        return alias_import.preview(("warp", "shell"), workspace=self.workspace, home=self.home,
                                    sqlite_path=warp_db(self.root, WARP_ROWS), yaml_dirs=[])

    def test_only_the_names_chosen_are_written(self):
        result = self.preview()
        applied = alias_import.apply(result["items"], ["gs"], "global", self.workspace)
        self.assertEqual([w["name"] for w in applied["written"]], ["gs"])
        catalog, _ = aliases.catalog(self.workspace)
        self.assertEqual([a.name for a in catalog], ["gs"])
        self.assertEqual(catalog[0].scope, "global")
        self.assertEqual(catalog[0].text, "git status --short --branch")

    def test_a_name_that_was_not_previewed_is_refused(self):
        result = self.preview()
        with self.assertRaises(aliases.AliasError):
            alias_import.apply(result["items"], ["something-else"], "global", self.workspace)

    def test_an_imported_name_can_be_changed_but_not_its_text(self):
        result = self.preview()
        applied = alias_import.apply(result["items"], ["squash-the-last-n-commits"], "local",
                                     self.workspace, {"squash-the-last-n-commits": "squash"})
        self.assertEqual([w["name"] for w in applied["written"]], ["squash"])
        self.assertEqual(aliases.resolve("squash", self.workspace).text,
                         "git reset --soft HEAD~{{num_commits}} && git commit")

    def test_the_preview_says_what_would_be_replaced_and_what_to_look_at(self):
        aliases.save(aliases.Alias(name="gs", text="echo mine"), self.workspace, "local")
        result = self.preview()
        gs = next(i for i in result["items"] if i["name"] == "gs")
        self.assertEqual(gs["conflict"], "local")
        self.assertIn("replaces the local alias `gs`", gs["warnings"])
        update = next(i for i in result["items"] if i["name"] == "update-system")
        self.assertIn("contains `sudo` — runs as root", update["warnings"])

    def test_an_imported_alias_records_where_it_came_from(self):
        result = self.preview()
        alias_import.apply(result["items"], ["gs"], "global", self.workspace)
        self.assertIn(".bashrc", aliases.resolve("gs", self.workspace).source)

    def test_an_alias_that_shadows_a_program_on_the_path_is_flagged(self):
        (self.home / ".bashrc").write_text("alias env='env | sort'\n")
        result = alias_import.preview(("shell",), workspace=self.workspace, home=self.home)
        warnings = " ".join(result["items"][0]["warnings"])
        self.assertIn("also a program on your PATH", warnings)


class RealWarpTests(unittest.TestCase):
    """The importer was written against the Warp install on the developer's machine."""

    @unittest.skipUnless(alias_import.warp_sqlite_path().exists(), "Warp is not installed here")
    def test_this_machines_own_warp_workflows_import_cleanly(self):
        with tempfile.TemporaryDirectory() as temp:
            with mock.patch.dict(os.environ, {"RELAY_GLOBAL_SWITCHBOARD": temp}):
                result = alias_import.preview(("warp",), workspace=None, yaml_dirs=[])
        self.assertTrue(result["items"], "no workflows read from the real Warp database")
        for item in result["items"]:
            self.assertTrue(aliases.NAME_RE.match(item["name"]), item["name"])
            self.assertIn(item["kind"], aliases.KINDS)
            self.assertTrue(item["text"].strip())
            # Every placeholder in the text is a parameter we can fill.
            self.assertEqual(sorted(aliases.PLACEHOLDER_RE.findall(item["text"])) or [],
                             sorted({p["name"] for p in item["params"]}) or [])


if __name__ == "__main__":
    unittest.main()
