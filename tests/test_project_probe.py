# SPDX-License-Identifier: GPL-3.0-or-later
"""`relay_core.project_probe`: what Relay can see in a project before it may do anything.

Every test works on a fixture tree under `tests/fixtures/trackers/` or on a temporary
directory it builds itself.  Nothing here writes into a fixture, reads the developer's own
`~/.config/gh`, runs git, or touches the network — `OfflineTest` proves the last two.
"""
import json
import os
import shutil
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path

from relay_core import board as B
from relay_core import board_import as I
from relay_core import project_probe as P

FIXTURES = Path(__file__).resolve().parent / "fixtures" / "trackers"
#: A `gh` config that does not exist, so a probe in a test never reads the real one.
NO_GH = "/nonexistent/gh/hosts.yml"


def fixture(kind: str) -> Path:
    return FIXTURES / kind


class ProbeCase(unittest.TestCase):
    """A temporary project, plus helpers for building git and tracker trees in it."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.project = Path(self.tmp.name).resolve() / "project"
        self.project.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def copy(self, kind: str) -> Path:
        """A writable copy of one fixture tree, merged into the temporary project."""
        shutil.copytree(fixture(kind), self.project, dirs_exist_ok=True)
        return self.project

    def write(self, rel: str, text: str, *, newline: str = "\n") -> Path:
        path = self.project / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.replace("\n", newline).encode("utf-8"))
        return path

    def git(self, config: str, head: str = "ref: refs/heads/main\n") -> Path:
        git_dir = self.project / ".git"
        git_dir.mkdir(exist_ok=True)
        (git_dir / "config").write_text(config, encoding="utf-8")
        (git_dir / "HEAD").write_text(head, encoding="utf-8")
        return git_dir

    def probe(self, **kwargs) -> dict:
        kwargs.setdefault("gh_hosts_path", NO_GH)
        return P.probe(self.project, **kwargs)

    def finding(self, result: dict, kind: str) -> dict:
        matches = [f for f in result["trackers"] if f["kind"] == kind]
        self.assertEqual(len(matches), 1, f"expected one {kind} finding in {result['trackers']}")
        return matches[0]

    def items(self, kind: str) -> list:
        return P.items_for(self.project, [kind])


# --------------------------------------------------------------------------------- shape

class ResultShapeTest(ProbeCase):
    def test_empty_project_is_a_small_stable_json_document(self):
        result = self.probe()
        self.assertEqual(json.loads(json.dumps(result)), result)
        self.assertEqual(result["version"], P.PROBE_VERSION)
        self.assertEqual(result["git"]["is_repo"], False)
        self.assertEqual(result["trackers"], [])
        self.assertEqual(result["board"]["kind"], "none")
        self.assertEqual(result["counts"], {"trackers": 0, "items": 0})
        self.assertTrue(Path(result["project"]).is_absolute())
        self.assertLess(len(json.dumps(result)), 2000)

    def test_findings_and_remotes_come_out_sorted(self):
        self.copy("checklist")
        self.copy("beads")
        self.git('[remote "zulu"]\n\turl = git@github.com:a/z.git\n'
                 '[remote "alpha"]\n\turl = git@github.com:a/a.git\n')
        result = self.probe()
        kinds = [(f["kind"], f["path"]) for f in result["trackers"]]
        self.assertEqual(kinds, sorted(kinds))
        self.assertEqual([r["name"] for r in result["git"]["remotes"]], ["alpha", "zulu"])

    def test_two_runs_of_the_probe_agree_byte_for_byte(self):
        self.copy("taskmaster")
        first = json.dumps(self.probe(), sort_keys=True)
        second = json.dumps(self.probe(), sort_keys=True)
        self.assertEqual(first, second)

    def test_an_unknown_tracker_kind_is_refused_by_name(self):
        with self.assertRaises(P.ProbeError) as caught:
            P.findings(self.project, ["beeds"])
        self.assertIn("beeds", str(caught.exception))

    def test_a_project_that_is_not_a_directory_is_no_findings_not_a_crash(self):
        self.assertEqual(P.findings(self.project / "nope"), [])


# ----------------------------------------------------------------------------- offline

class OfflineTest(ProbeCase):
    """No socket and no subprocess, for the whole of `probe` and `board_import.propose`.

    The project has a no-telemetry rule and this code runs *before* the user has agreed to
    anything, so the guarantee is tested rather than asserted in a comment.  A subprocess
    would be just as bad as a socket: git in an unvetted directory runs that repository's
    hooks and credential helpers.
    """

    def setUp(self):
        super().setUp()
        self.saved = {
            (socket, "socket"): socket.socket,
            (socket, "create_connection"): socket.create_connection,
            (socket, "getaddrinfo"): socket.getaddrinfo,
            (subprocess, "Popen"): subprocess.Popen,
            (subprocess, "run"): subprocess.run,
            (subprocess, "check_output"): subprocess.check_output,
            (os, "popen"): os.popen,
            (os, "system"): os.system,
        }
        for (module, name) in self.saved:
            setattr(module, name, self._forbidden(f"{module.__name__}.{name}"))

    def tearDown(self):
        for (module, name), original in self.saved.items():
            setattr(module, name, original)
        super().tearDown()

    @staticmethod
    def _forbidden(what):
        def deny(*args, **kwargs):
            raise AssertionError(f"{what} was called during an offline probe")
        return deny

    def test_probe_of_every_fixture_touches_neither(self):
        for kind in P.TRACKER_KINDS:
            with self.subTest(kind=kind):
                folder = "checklist" if kind == "checklist" else kind
                if not fixture(folder).is_dir():          # pragma: no cover - fixtures are present
                    continue
                shutil.rmtree(self.project)
                shutil.copytree(fixture(folder), self.project)
                self.git('[remote "origin"]\n\turl = https://github.com/a/b.git\n')
                result = self.probe()
                self.assertTrue(result["trackers"], f"{kind} found nothing")

    def test_propose_touches_neither(self):
        self.copy("backlog-md")
        proposals = I.propose(self.project)
        self.assertTrue(proposals)

    def test_a_pre_board_tree_is_detected_without_running_git(self):
        self.write("issues/features/2026-01-01-a-thing.md",
                   "# A thing\n\n- **Status**: open\n- **Component**: gui\n\n## Request\nDo it.\n")
        board = self.probe()["board"]
        self.assertEqual(board["kind"], "pre-board")
        self.assertEqual(board["convertible"], 1)


# --------------------------------------------------------------------------------- git

class GitConfigTest(unittest.TestCase):
    def test_sections_subsections_comments_and_continuations(self):
        entries = P.parse_git_config(
            '# a comment\n'
            '[core]\n\trepositoryformatversion = 0\n'
            '[remote "origin"] url = https://example.com/a.git ; trailing comment\n'
            '[remote "up stream"]\n\turl = "https://example.com/b.git"\n'
            '[branch.main]\n\tremote = origin\n'
            '[alias]\n\tlong = one \\\n two\n'
            '[core]\n\tbare\n')
        table = {(s, sub, k): v for s, sub, k, v in entries}
        self.assertEqual(table[("remote", "origin", "url")], "https://example.com/a.git")
        self.assertEqual(table[("remote", "up stream", "url")], "https://example.com/b.git")
        self.assertEqual(table[("branch", "main", "remote")], "origin")
        self.assertEqual(table[("alias", "", "long")], "one  two")
        self.assertEqual(table[("core", "", "bare")], "true")

    def test_url_shapes(self):
        cases = {
            "https://github.com/a/b.git": ("github.com", "a", "b"),
            "git@github.com:a/b.git": ("github.com", "a", "b"),
            "ssh://git@github.com:22/a/b.git": ("github.com", "a", "b"),
            "git://git.example.org/a/b": ("git.example.org", "a", "b"),
            "https://gitlab.com/group/sub/proj.git": ("gitlab.com", "group/sub", "proj"),
            "https://dev.azure.com/org/project/_git/repo": ("dev.azure.com", "org/project", "repo"),
            "/srv/git/bare.git": (None, None, None),
            "file:///srv/git/bare.git": (None, None, None),
            "../sibling": (None, None, None),
        }
        for url, expected in cases.items():
            with self.subTest(url=url):
                parts = P.split_remote_url(url)
                self.assertEqual((parts["host"], parts["owner"], parts["repo"]), expected)

    def test_forges(self):
        self.assertEqual(P.forge_for("github.com"), "github")
        self.assertEqual(P.forge_for("gitlab.com"), "gitlab")
        self.assertEqual(P.forge_for("gitlab.example.org"), "gitlab")
        self.assertEqual(P.forge_for("codeberg.org"), "gitea-like")
        self.assertEqual(P.forge_for("bitbucket.org"), "bitbucket")
        self.assertEqual(P.forge_for("dev.azure.com"), "azure-devops")
        self.assertEqual(P.forge_for("contoso.visualstudio.com"), "azure-devops")
        self.assertEqual(P.forge_for("git.example.org"), "unknown")
        self.assertEqual(P.forge_for(None), "unknown")
        self.assertEqual(P.forge_for("code.acme.example", ["code.acme.example"]),
                         "github-enterprise")


class GitProbeTest(ProbeCase):
    def test_remotes_and_the_primary_one(self):
        self.git('[remote "origin"]\n\turl = git@github.com:me/fork.git\n'
                 '[remote "upstream"]\n\turl = https://github.com/them/real.git\n'
                 '[remote "backup"]\n\turl = /srv/git/backup.git\n')
        git = self.probe()["git"]
        self.assertTrue(git["is_repo"])
        self.assertEqual(git["primary"], "upstream")
        self.assertIn("fork", git["primary_reason"])
        rows = {r["name"]: r for r in git["remotes"]}
        self.assertEqual(rows["upstream"]["owner"], "them")
        self.assertEqual(rows["origin"]["forge"], "github")
        self.assertEqual(rows["backup"]["host"], None)
        self.assertEqual(rows["backup"]["forge"], "unknown")

    def test_origin_wins_when_there_is_no_upstream(self):
        self.git('[remote "origin"]\n\turl = git@github.com:me/x.git\n'
                 '[remote "fork"]\n\turl = git@github.com:you/x.git\n')
        git = self.probe()["git"]
        self.assertEqual(git["primary"], "origin")
        self.assertIn("own remote", git["primary_reason"])

    def test_the_first_remote_by_name_wins_when_neither_is_conventional(self):
        self.git('[remote "zeta"]\n\turl = git@github.com:me/x.git\n'
                 '[remote "alpha"]\n\turl = git@github.com:you/x.git\n')
        git = self.probe()["git"]
        self.assertEqual(git["primary"], "alpha")
        self.assertIn("no upstream or origin", git["primary_reason"])

    def test_no_remotes_says_so(self):
        self.git("[core]\n\tbare = false\n")
        git = self.probe()["git"]
        self.assertEqual(git["primary"], None)
        self.assertIn("no remotes", git["primary_reason"])

    def test_insteadof_rewrites_the_url_longest_prefix_first(self):
        self.git('[url "git@github.com:"]\n\tinsteadOf = https://github.com/\n'
                 '[url "git@github-work:"]\n\tinsteadOf = https://github.com/acme/\n'
                 '[remote "origin"]\n\turl = https://github.com/acme/thing.git\n'
                 '[remote "other"]\n\turl = https://github.com/someone/else.git\n')
        rows = {r["name"]: r for r in self.probe()["git"]["remotes"]}
        self.assertEqual(rows["origin"]["url"], "git@github-work:thing.git")
        self.assertEqual(rows["other"]["url"], "git@github.com:someone/else.git")
        self.assertEqual(rows["other"]["owner"], "someone")

    def test_apply_insteadof_leaves_an_unmatched_url_alone(self):
        rules = [("https://github.com/", "git@github.com:")]
        self.assertEqual(P.apply_insteadof("ssh://x/y", rules), "ssh://x/y")

    def test_dot_git_as_a_file_pointing_at_a_submodule_gitdir(self):
        """A submodule's `.git` is a file naming a gitdir in the superproject — outside the
        project directory, which is git's own design and must still be read."""
        real = Path(self.tmp.name) / "super" / ".git" / "modules" / "sub"
        real.mkdir(parents=True)
        (real / "config").write_text('[remote "origin"]\n\turl = git@github.com:a/sub.git\n')
        (real / "HEAD").write_text("ref: refs/heads/PROJ-42-fix\n")
        (self.project / ".git").write_text(f"gitdir: {real}\n")
        git = self.probe()["git"]
        self.assertTrue(git["is_repo"])
        self.assertEqual(git["remotes"][0]["repo"], "sub")
        self.assertEqual(git["branch"], "PROJ-42-fix")

    def test_dot_git_as_a_file_in_a_worktree_follows_commondir(self):
        main = Path(self.tmp.name) / "main" / ".git"
        (main / "worktrees" / "wt").mkdir(parents=True)
        (main / "config").write_text('[remote "origin"]\n\turl = https://example.org/a/b.git\n')
        (main / "worktrees" / "wt" / "commondir").write_text("../..\n")
        (main / "worktrees" / "wt" / "HEAD").write_text("ref: refs/heads/feature\n")
        (self.project / ".git").write_text(f"gitdir: {main / 'worktrees' / 'wt'}\n")
        git = self.probe()["git"]
        self.assertEqual(git["remotes"][0]["host"], "example.org")
        self.assertEqual(git["branch"], "feature")

    def test_a_dot_git_file_that_is_not_a_gitdir_pointer_is_not_a_repository(self):
        (self.project / ".git").write_text("this is not a pointer\n")
        self.assertFalse(self.probe()["git"]["is_repo"])

    def test_a_detached_head_names_no_branch_and_no_ticket(self):
        self.git("[core]\n", head="a" * 40 + "\n")
        git = self.probe()["git"]
        self.assertIsNone(git["branch"])
        self.assertEqual(git["ticket_keys"], [])

    def test_a_ticket_key_in_the_branch_is_a_hint_and_nothing_is_contacted(self):
        self.git("[core]\n", head="ref: refs/heads/feature/ENG-1234-dark-mode\n")
        result = self.probe()
        self.assertEqual([k["key"] for k in result["git"]["ticket_keys"]], ["ENG-1234"])
        hints = [h for h in result["hints"] if h["kind"] == "ticket-key-in-branch"]
        self.assertEqual(len(hints), 1)
        self.assertIn("does not contact", hints[0]["detail"])

    def test_a_lower_case_branch_word_is_not_a_ticket_key(self):
        self.git("[core]\n", head="ref: refs/heads/fix-issue-12\n")
        self.assertEqual(self.probe()["git"]["ticket_keys"], [])


class GhHostsTest(ProbeCase):
    def test_top_level_keys_are_the_hosts_and_tokens_are_not_read(self):
        path = self.project / "hosts.yml"
        path.write_text(
            "github.com:\n"
            "    users:\n"
            "        monalisa:\n"
            "            oauth_token: ghp_SECRET\n"
            "    git_protocol: https\n"
            "code.acme.example:\n"
            "    user: monalisa\n"
            "    oauth_token: ghe_SECRET\n", encoding="utf-8")
        self.assertEqual(P.gh_hosts(path), ["code.acme.example", "github.com"])

    def test_a_ghe_host_is_recognised_from_the_gh_config(self):
        hosts = self.project / "hosts.yml"
        hosts.write_text("code.acme.example:\n    user: me\n", encoding="utf-8")
        self.git('[remote "origin"]\n\turl = git@code.acme.example:team/app.git\n')
        result = P.probe(self.project, gh_hosts_path=hosts)
        self.assertEqual(result["git"]["remotes"][0]["forge"], "github-enterprise")
        self.assertEqual(result["gh_hosts"], ["code.acme.example"])

    def test_the_same_host_is_unknown_without_the_gh_config(self):
        self.git('[remote "origin"]\n\turl = git@code.acme.example:team/app.git\n')
        self.assertEqual(self.probe()["git"]["remotes"][0]["forge"], "unknown")

    def test_gh_config_dir_honours_gh_config_dir_then_xdg_then_home(self):
        self.assertEqual(P.gh_config_dir({"GH_CONFIG_DIR": "/a/gh"}), Path("/a/gh"))
        self.assertEqual(P.gh_config_dir({"XDG_CONFIG_HOME": "/b"}), Path("/b/gh"))
        self.assertEqual(P.gh_config_dir({"HOME": "/home/x"}), Path("/home/x/.config/gh"))

    def test_a_missing_hosts_file_is_no_hosts(self):
        self.assertEqual(P.gh_hosts(NO_GH), [])


# ------------------------------------------------------------------------- safety rails

class SafetyTest(ProbeCase):
    def test_a_symlinked_directory_that_leaves_the_project_is_not_followed(self):
        outside = Path(self.tmp.name) / "outside"
        (outside / "docs").mkdir(parents=True)
        (outside / "docs" / "TODO.md").write_text("- [ ] must not be read\n")
        os.symlink(outside / "docs", self.project / "docs")
        self.assertEqual(self.probe()["trackers"], [])

    def test_a_symlinked_file_that_leaves_the_project_is_not_read(self):
        outside = Path(self.tmp.name) / "outside"
        outside.mkdir()
        (outside / "TODO.md").write_text("- [ ] must not be read\n")
        os.symlink(outside / "TODO.md", self.project / "TODO.md")
        self.assertEqual(self.probe()["trackers"], [])

    def test_a_symlink_inside_the_project_is_fine(self):
        self.write("real/TODO.md", "- [ ] read me\n")
        os.symlink(self.project / "real" / "TODO.md", self.project / "TODO.md")
        self.assertEqual(self.finding(self.probe(), "checklist")["count"], 1)

    def test_a_file_over_the_byte_ceiling_is_skipped_rather_than_read(self):
        self.write("TODO.md", "- [ ] small\n")
        self.assertEqual(self.finding(self.probe(), "checklist")["count"], 1)
        (self.project / "TODO.md").write_text("- [ ] x\n" * (P.MAX_FILE_BYTES // 4))
        self.assertEqual(self.probe()["trackers"], [])

    def test_a_tasks_json_over_the_json_ceiling_is_skipped(self):
        path = self.write(".taskmaster/tasks/tasks.json",
                          json.dumps({"master": {"tasks": [{"id": 1, "title": "t"}]}}))
        self.assertEqual(self.finding(self.probe(), "taskmaster")["count"], 1)
        padding = "x" * (P.MAX_JSON_BYTES + 1)
        path.write_text(json.dumps({"master": {"tasks": [{"id": 1, "title": "t", "details": padding}]}}))
        self.assertEqual(self.probe()["trackers"], [])

    def test_the_item_ceiling_truncates_and_says_so(self):
        self.write("TODO.md", "".join(f"- [ ] item {n}\n" for n in range(P.MAX_ITEMS_PER_SOURCE + 50)))
        found = self.finding(self.probe(), "checklist")
        self.assertEqual(found["count"], P.MAX_ITEMS_PER_SOURCE)
        self.assertTrue(found["truncated"])

    def test_a_directory_named_like_a_checklist_file_is_not_read(self):
        (self.project / "TODO.md").mkdir()
        self.assertEqual(self.probe()["trackers"], [])

    def test_unreadable_json_is_no_finding_not_an_exception(self):
        self.write(".taskmaster/tasks/tasks.json", "{ this is not json")
        self.assertEqual(self.probe()["trackers"], [])


# ----------------------------------------------------------------------- the checklists

class ChecklistTest(ProbeCase):
    def setUp(self):
        super().setUp()
        self.copy("checklist")

    def test_only_unchecked_top_level_boxes_and_box_free_sections_become_items(self):
        titles = [i.title for i in self.items("checklist") if i.path == "TODO.md"]
        self.assertEqual(titles, ["Wrap long lines in the composer",
                                  "Copy on highlight in panes", "Ideas"])

    def test_the_boxes_nested_under_an_item_become_its_tasks_with_their_own_state(self):
        item = next(i for i in self.items("checklist") if i.title.startswith("Wrap long"))
        self.assertEqual(item.tasks, [("Handle a line that is one very long word", "open"),
                                      ("Keep the cursor visible while wrapping", "done")])

    def test_the_h1_title_and_its_preamble_are_not_a_card(self):
        self.assertNotIn("Things to do", [i.title for i in self.items("checklist")])

    def test_unicode_survives_and_an_in_progress_box_is_read(self):
        items = [i for i in self.items("checklist") if i.path == "docs/ROADMAP.md"]
        self.assertIn("Ünïcøde", items[0].title)
        self.assertIn("🎛", items[0].title)
        self.assertEqual(items[1].status, "in-progress")

    def test_a_crlf_file_parses_with_no_stray_carriage_returns(self):
        items = [i for i in self.items("checklist") if i.path == "docs/NOTES.md"]
        self.assertEqual([i.title for i in items], ["A task written on Windows"])
        self.assertNotIn("\r", json.dumps(self.probe()))

    def test_an_item_id_survives_an_edit_above_it(self):
        before = {i.title: i.source_key for i in self.items("checklist") if i.path == "TODO.md"}
        text = (self.project / "TODO.md").read_text()
        (self.project / "TODO.md").write_text("A new paragraph at the very top.\n\n" + text)
        after = {i.title: i.source_key for i in self.items("checklist") if i.path == "TODO.md"}
        self.assertEqual(before, after)

    def test_two_identical_lines_in_one_file_get_different_ids(self):
        self.write("IDEAS.md", "## A\n- [ ] the same line\n- [ ] the same line\n")
        keys = [i.source_key for i in self.items("checklist") if i.path == "IDEAS.md"]
        self.assertEqual(len(set(keys)), 2)

    def test_a_checklist_file_elsewhere_in_the_tree_is_not_read(self):
        self.write("src/vendor/TODO.md", "- [ ] not ours\n")
        paths = {f["path"] for f in self.probe()["trackers"]}
        self.assertNotIn("src/vendor/TODO.md", paths)


# ------------------------------------------------------------------------- the trackers

class BacklogMdTest(ProbeCase):
    def setUp(self):
        super().setUp()
        self.copy("backlog-md")
        self.by_id = {i.source_id: i for i in self.items("backlog-md")}

    def test_tasks_and_drafts_are_imported_and_completed_and_readme_are_not(self):
        self.assertEqual(sorted(self.by_id), ["TASK-1", "TASK-2", "TASK-3", "TASK-4"])
        self.assertIn("completed/ and archive/ are left alone",
                      self.finding(self.probe(), "backlog-md")["summary"])

    def test_front_matter_including_a_block_sequence_assignee(self):
        item = self.by_id["TASK-2"]
        self.assertEqual(item.title, "Harden test lifecycle, timeout, and cleanup handling")
        self.assertEqual(item.status, "In Progress")
        self.assertEqual(item.priority, "medium")
        self.assertEqual(item.fields, {"assignee": "@test-hygiene", "milestone": "beta-2"})
        self.assertEqual(item.depends_on, ["TASK-1"])
        self.assertEqual(item.parent, "TASK-1")
        self.assertIn("chore", item.labels)

    def test_numbered_and_unnumbered_acceptance_criteria_both_parse(self):
        self.assertEqual(self.by_id["TASK-1"].tasks[:2],
                         [("Board export and non-TTY board output show a task due date when one is set", "open"),
                          ("Surfaces without a due date are unchanged", "open")])
        self.assertEqual(self.by_id["TASK-3"].tasks,
                         [("The old spelling still parses", "done"), ("And an open one", "open")])

    def test_the_implementation_plan_becomes_tasks_too(self):
        self.assertIn(("Find every surface that renders a task row", "done"),
                      self.by_id["TASK-1"].tasks)

    def test_sections_beyond_the_description_are_kept_in_the_body(self):
        self.assertIn("### Implementation Notes", self.by_id["TASK-2"].body)
        self.assertIn("The leak is in the fixture", self.by_id["TASK-2"].body)

    def test_a_draft_is_labelled_and_treated_as_open(self):
        self.assertEqual(self.by_id["TASK-4"].status, "open")
        self.assertIn("draft", self.by_id["TASK-4"].labels)

    def test_a_dot_backlog_directory_is_found_too(self):
        shutil.rmtree(self.project)
        self.project.mkdir()
        self.write(".backlog/config.yml", 'statuses: ["To Do"]\n')
        self.write(".backlog/tasks/task-1 - A.md",
                   "---\nid: TASK-1\ntitle: A\nstatus: To Do\n---\n\n## Description\nx\n")
        self.assertEqual(self.finding(self.probe(), "backlog-md")["path"], ".backlog")

    def test_a_root_backlog_config_may_name_the_directory(self):
        shutil.rmtree(self.project)
        self.project.mkdir()
        self.write("backlog.config.yml", "backlog_directory: work-items\n")
        self.write("work-items/config.yml", 'statuses: ["To Do"]\n')
        self.write("work-items/tasks/task-1 - A.md",
                   "---\nid: TASK-1\ntitle: A\nstatus: To Do\n---\n\n## Description\nx\n")
        self.assertEqual(self.finding(self.probe(), "backlog-md")["path"], "work-items")


class BeadsTest(ProbeCase):
    def setUp(self):
        super().setUp()
        self.copy("beads")
        self.by_id = {i.source_id: i for i in self.items("beads")}

    def test_the_schema_header_memories_and_tombstones_are_skipped(self):
        self.assertEqual(sorted(self.by_id),
                         ["bd-a3f2dd", "bd-b17c01", "bd-c99ff0", "bd-c99ff0.1", "bd-e40100"])

    def test_statuses_and_integer_priorities(self):
        self.assertEqual(self.by_id["bd-a3f2dd"].status, "in-progress")
        self.assertEqual(self.by_id["bd-a3f2dd"].priority, "high")
        self.assertEqual(self.by_id["bd-c99ff0"].priority, "critical")   # priority 0 is not "unset"
        self.assertEqual(self.by_id["bd-e40100"].status, "done")
        self.assertEqual(self.by_id["bd-e40100"].priority, "lowest")

    def test_only_blocking_edges_become_dependencies_and_parent_child_becomes_parent(self):
        self.assertEqual(self.by_id["bd-c99ff0"].depends_on, ["bd-b17c01"])  # not the `related` one
        self.assertEqual(self.by_id["bd-c99ff0.1"].parent, "bd-c99ff0")
        self.assertEqual(self.by_id["bd-c99ff0.1"].depends_on, [])

    def test_the_prose_fields_are_gathered_into_the_body(self):
        body = self.by_id["bd-a3f2dd"].body
        self.assertIn("Quantify near-duplicate functions", body)
        self.assertIn("### Design", body)
        self.assertIn("### Acceptance criteria", body)

    def test_comments_are_carried_with_their_authors(self):
        self.assertEqual(self.by_id["bd-a3f2dd"].comments,
                         [("alice", "Start with the CLI, it is the worst.")])

    def test_the_legacy_type_spelling_is_read_as_issue_type(self):
        self.assertIn("task", self.by_id["bd-c99ff0.1"].labels)

    def test_the_summary_says_the_jsonl_is_an_export(self):
        self.assertIn("export of the Dolt database", self.finding(self.probe(), "beads")["summary"])


class TaskMasterTest(ProbeCase):
    def setUp(self):
        super().setUp()
        self.copy("taskmaster")
        self.by_id = {i.source_id: i for i in self.items("taskmaster")}

    def test_every_tag_is_read_and_the_tag_is_part_of_the_id(self):
        self.assertEqual(sorted(self.by_id),
                         ["feature-auth/1", "master/1", "master/2", "master/3", "master/4"])
        self.assertIn("feature-auth", self.by_id["feature-auth/1"].labels)

    def test_the_legacy_untagged_shape_is_read_as_master(self):
        self.write(".taskmaster/tasks/tasks.json",
                   json.dumps({"tasks": [{"id": 1, "title": "Only one", "status": "pending"}],
                               "metadata": {"created": "2026-01-01"}}))
        self.assertEqual([i.source_id for i in self.items("taskmaster")], ["master/1"])

    def test_statuses_map_and_a_string_id_is_accepted(self):
        self.assertEqual(self.by_id["master/1"].status, "done")
        self.assertEqual(self.by_id["master/2"].status, "in-progress")
        self.assertEqual(self.by_id["master/3"].status, "open")
        self.assertEqual(self.by_id["master/4"].status, "dropped")

    def test_dependencies_are_scoped_to_the_tag(self):
        self.assertEqual(self.by_id["master/2"].depends_on, ["master/1"])
        self.assertEqual(self.by_id["master/3"].depends_on, ["master/2"])

    def test_subtasks_become_tasks_with_their_own_state(self):
        self.assertEqual(self.by_id["master/2"].tasks,
                         [("Pick a backend", "done"), ("Write the adapter", "in-progress"),
                          ("Expire old sessions", "open")])

    def test_subtask_dependencies_are_kept_as_positions_in_the_checklist(self):
        # Sibling ids (`[1]`) and this task's own fully-qualified ones (`"2.2"`) both become a
        # position in `tasks`; `board_import` turns them into `blocked_by=` markers.
        self.assertEqual(self.by_id["master/1"].task_depends, {2: [1]})
        self.assertEqual(self.by_id["master/2"].task_depends, {2: [1], 3: [2]})
        self.assertEqual(self.by_id["master/2"].to_dict()["tasks"][2]["blocked_by"], [2])

    def test_a_subtask_that_depends_on_another_task_names_that_task(self):
        self.write(".taskmaster/tasks/tasks.json", json.dumps({"tasks": [
            {"id": 1, "title": "First", "status": "pending"},
            {"id": 2, "title": "Second", "status": "pending", "subtasks": [
                {"id": 1, "title": "Needs the other task", "dependencies": ["1.2"]},
                {"id": 2, "title": "Needs a whole task", "dependencies": ["1.1"]}]}]}))
        items = {i.source_id: i for i in self.items("taskmaster")}
        # A bare number inside a subtask is a *sibling* (Task Master's own rule), so both of
        # these have to be written fully qualified to mean the other task.
        self.assertEqual(items["master/2"].task_depends, {1: ["master/1"], 2: ["master/1"]})

    def test_a_subtask_dependency_on_nothing_is_dropped_rather_than_faked(self):
        self.write(".taskmaster/tasks/tasks.json", json.dumps({"tasks": [
            {"id": 1, "title": "Only", "status": "pending", "subtasks": [
                {"id": 1, "title": "One", "dependencies": [1, 9, "", None]}]}]}))
        items = {i.source_id: i for i in self.items("taskmaster")}
        self.assertEqual(items["master/1"].task_depends, {})

    def test_details_and_test_strategy_are_kept(self):
        body = self.by_id["master/1"].body
        self.assertIn("### Details", body)
        self.assertIn("### Test strategy", body)

    def test_a_legacy_tasks_json_path_is_found(self):
        shutil.rmtree(self.project / ".taskmaster")
        self.write("tasks/tasks.json", json.dumps({"tasks": [{"id": 1, "title": "Legacy"}]}))
        self.assertEqual(self.finding(self.probe(), "taskmaster")["path"], "tasks/tasks.json")


class SpecKitTest(ProbeCase):
    def setUp(self):
        super().setUp()
        self.copy("spec-kit")
        self.items_ = self.items("spec-kit")

    def test_one_card_per_numbered_feature_directory(self):
        self.assertEqual([i.source_id for i in self.items_], ["001-user-accounts"])
        self.assertEqual(self.finding(self.probe(), "spec-kit")["path"], "specs")

    def test_a_directory_that_is_not_numbered_is_not_a_spec_kit_feature(self):
        self.assertNotIn("notes-not-a-feature", [i.source_id for i in self.items_])

    def test_the_title_and_body_come_from_spec_md(self):
        self.assertEqual(self.items_[0].title, "User accounts")
        self.assertIn("sign in and keep their settings", self.items_[0].body)

    def test_the_task_line_keeps_its_id_and_loses_the_p_and_story_tags(self):
        texts = [t for t, _ in self.items_[0].tasks]
        self.assertIn("T003 Configure linting and formatting tools", texts)
        self.assertIn("T012 Create Account model in src/models/account.py", texts)

    def test_the_template_placeholder_task_is_not_imported(self):
        self.assertFalse([t for t, _ in self.items_[0].tasks if "TXXX" in t])

    def test_the_status_is_read_off_the_checklist(self):
        self.assertEqual(self.items_[0].status, "in-progress")     # T001 done, the rest not

    def test_the_older_phase_3_1_template_parses_too(self):
        self.write("specs/002-old/tasks.md",
                   "# Tasks\n\n## Phase 3.1: Setup\n- [ ] T001 Create project structure\n"
                   "- [ ] T004 [P] Contract test POST /api/users in tests/contract/t.py\n")
        texts = [t for t, _ in next(i for i in self.items("spec-kit")
                                    if i.source_id == "002-old").tasks]
        self.assertEqual(texts, ["T001 Create project structure",
                                 "T004 Contract test POST /api/users in tests/contract/t.py"])


class KiroTest(ProbeCase):
    def setUp(self):
        super().setUp()
        self.copy("kiro")
        self.by_id = {i.source_id: i for i in self.items("kiro")}

    def test_one_card_per_spec_directory_including_a_nested_one(self):
        self.assertEqual(sorted(self.by_id), ["nested-capability", "session-logout-fix"])

    def test_the_numbered_grammar_with_sub_items(self):
        texts = [t for t, _ in self.by_id["session-logout-fix"].tasks]
        self.assertIn("1 Set up project structure and dependencies", texts)
        self.assertIn("2.1 Create data models module (`data_models.ts`)", texts)

    def test_a_dash_box_is_kiros_in_progress_mark(self):
        tasks = dict(self.by_id["session-logout-fix"].tasks)
        self.assertEqual(tasks["3 Add authentication debugging and error logging"], "in-progress")
        self.assertEqual(self.by_id["session-logout-fix"].status, "in-progress")

    def test_an_optional_task_keeps_saying_so(self):
        texts = [t for t, _ in self.by_id["session-logout-fix"].tasks]
        self.assertIn("(optional) 2.2 Write unit tests for data models", texts)

    def test_detail_bullets_and_requirement_back_references_are_not_tasks(self):
        texts = " ".join(t for t, _ in self.by_id["session-logout-fix"].tasks)
        self.assertNotIn("_Requirements", texts)
        self.assertNotIn("Create `scripts/` directory", texts)

    def test_the_title_comes_from_requirements_md(self):
        self.assertEqual(self.by_id["session-logout-fix"].title, "Session logout fix")
        self.assertEqual(self.by_id["nested-capability"].title, "Nested capability")


class OpenSpecTest(ProbeCase):
    def setUp(self):
        super().setUp()
        self.copy("openspec")
        self.by_id = {i.source_id: i for i in self.items("openspec")}

    def test_one_card_per_change_and_the_archive_is_left_alone(self):
        self.assertEqual(sorted(self.by_id), ["add-dark-mode"])

    def test_only_an_x_box_is_done_here(self):
        tasks = dict(self.by_id["add-dark-mode"].tasks)
        self.assertEqual(tasks["1.1 Extract the palette into `:root` custom properties"], "done")
        self.assertEqual(tasks["1.2 Pin the published values in a unit test"], "open")
        tilde = next(v for k, v in tasks.items() if k.startswith("2.2"))
        empty = next(v for k, v in tasks.items() if k.startswith("2.3"))
        self.assertEqual((tilde, empty), ("open", "open"))

    def test_the_title_and_body_come_from_the_proposal(self):
        self.assertEqual(self.by_id["add-dark-mode"].title, "Add dark mode")
        self.assertIn("work at night", self.by_id["add-dark-mode"].body)


# -------------------------------------------------------------------- board and hints

class BoardAndHintsTest(ProbeCase):
    def test_an_existing_hidden_switchboard_is_reported_and_not_re_offered(self):
        # `.switchboard/` is what Relay creates since 2026-09-19. A probe that only knew the
        # visible names would offer to initialize a project that already has a board.
        root = self.project / B.DEFAULT_BOARD_FOLDER
        self.assertEqual(root.name, ".switchboard")
        (root / "features").mkdir(parents=True)
        (root / B.BOARD_CONFIG).write_text("version: 1\ntabs: [{id: features, folder: features}]\n")
        (root / "features" / "a.md").write_text(
            "---\nid: AB12\ntype: work\nstatus: inbox\nrank: g\ncreated: '2026-01-01'\n---\n# A\n")
        board = self.probe()["board"]
        self.assertEqual((board["present"], board["kind"], board["folder"]),
                         (True, "board", ".switchboard"))
        self.assertEqual(board["cards"], 1)
        self.assertEqual(board["path"], ".switchboard")

    def test_the_visible_switchboard_spelling_is_reported_as_the_board_too(self):
        root = self.project / "switchboard"
        root.mkdir()
        (root / B.BOARD_CONFIG).write_text("version: 1\ntabs: []\n")
        self.assertEqual(self.probe()["board"]["folder"], "switchboard")

    def test_the_older_issues_spelling_is_reported_as_the_board_too(self):
        root = self.project / "issues"
        root.mkdir()
        (root / B.BOARD_CONFIG).write_text("version: 1\ntabs: []\n")
        self.assertEqual(self.probe()["board"]["folder"], "issues")

    def test_github_issue_templates_are_a_hint_not_an_import(self):
        self.write(".github/ISSUE_TEMPLATE/bug.md", "---\nname: Bug\n---\n")
        self.write(".github/ISSUE_TEMPLATE/feature.yml", "name: Feature\n")
        hints = [h for h in self.probe()["hints"] if h["kind"] == "github-issue-templates"]
        self.assertEqual(hints[0]["count"], 2)
        self.assertEqual(self.probe()["trackers"], [])

    def test_a_single_issue_template_file_is_a_hint(self):
        self.write(".github/issue_template.md", "Describe the bug\n")
        self.assertEqual([h["kind"] for h in self.probe()["hints"]], ["github-issue-templates"])


if __name__ == "__main__":                               # pragma: no cover
    unittest.main()
