# SPDX-License-Identifier: AGPL-3.0-or-later
"""Two-way sync between Switchboard cards and GitHub issues (`#GDQN`).

Every test works in a temporary board against `tests/fake_github.py` on 127.0.0.1: no model call,
no real network, no keyring.  The engine is driven through the real `forge_github.GitHubProvider`,
so the mapping, the three-way merge and the HTTP shape are all exercised together.
"""
import json
import tempfile
import unittest
from pathlib import Path

import fake_github as FG
from relay_core import board as B
from relay_core import forge_github as GH
from relay_core import forge_sync as F

CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: planning, folder: planning}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto}
"""


class SyncCase(unittest.TestCase):
    """A board, a fake GitHub and the engine that joins them."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name).resolve()
        # `.switchboard/`, the folder Relay creates since 2026-09-19: the sync's state file, its
        # login map and every path it reports are derived from the board root, so a hidden board
        # exercises all of them.
        self.root = self.repo / B.DEFAULT_BOARD_FOLDER
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        (self.root / ".gitignore").write_text(B.GITIGNORE_TEXT, encoding="utf-8")
        self.board = B.Board(self.root, self.repo)
        self.gh = FG.FakeGitHub()
        self.addCleanup(self.gh.close)
        self.slept = []

    # ---- board helpers
    def card(self, title="Voice transcription mode", *, status="ready", tab="features",
             request="add voice transcribe mode", card_type="work", private=False,
             labels=("feature",), tasks=(), **fields):
        card = B.new_card(card_type, title, status, request=request,
                          labels=list(labels) or None, private=private, **fields)
        if tasks:
            body = "\n".join(f"- [{'x' if done else ' '}] {text} <!-- t:{tid} -->"
                             for tid, done, text in tasks)
            card.body = card.body.rstrip("\n") + f"\n\n## Tasks\n{body}\n"
        folder = {"work": tab and self.tab_folder(tab), "memory": B.MEMORY_FOLDER,
                  "alias": B.ALIAS_FOLDER}[card_type]
        directory = self.board.base_for(private) / card.expected_folder(folder)
        directory.mkdir(parents=True, exist_ok=True)
        card.path = directory / B.card_filename(title)
        B.atomic_write(card.path, card.to_text())
        return self.board.card_by_id(card.id)

    def tab_folder(self, tab):
        return F.tab_folders(self.board).get(tab, tab)

    def reload(self, card_id):
        card = self.board.card_by_id(card_id)
        self.assertIsNotNone(card, f"#{card_id} is gone")
        return card

    def edit_card(self, card_id, *, body=None, **fields):
        card = self.reload(card_id)
        if body is not None:
            card.body = body
        for key, value in fields.items():
            card.set(key, value)
        B.atomic_write(card.path, card.to_text())
        return self.reload(card_id)

    # ---- engine helpers
    def provider(self, repo="relay/terminal", token=FG.TOKEN, **kwargs):
        kwargs.setdefault("base_url", self.gh.base_url)
        kwargs.setdefault("sleep", self.slept.append)
        return GH.GitHubProvider(repo, token, **kwargs)

    def engine(self, provider=None, **kwargs):
        return F.ForgeSync(self.board, provider or self.provider(), **kwargs)

    def sync(self, **kwargs):
        result = self.engine(**kwargs).run(confirm_bulk=kwargs.pop("confirm_bulk", True))
        self.assertEqual(result.errors, [], result.errors)
        return result

    def issue(self, number=1):
        return self.gh.main.issues[number]

    def labels_of(self, number=1):
        return sorted(l["name"] for l in self.issue(number)["labels"])

    def assertCheckClean(self):
        problems = [str(p) for p in self.board.check() if p.severity == "error"]
        self.assertEqual(problems, [])


# ------------------------------------------------------------------ first sync

class FirstSyncTests(SyncCase):
    def test_a_card_becomes_an_issue_with_every_mapped_field(self):
        card = self.card(labels=["feature", "voice"], tasks=[("a3", False, "Record audio"),
                                                             ("b7", True, "Add the call")])
        result = self.sync()
        self.assertEqual((result.creates, result.pushed), (1, 1))
        issue = self.issue()
        self.assertEqual(issue["title"], card.title)
        self.assertIn("## Issue", issue["body"])
        self.assertIn("- [ ] Record audio", issue["body"])
        self.assertIn("- [x] Add the call", issue["body"])
        self.assertEqual(self.labels_of(), ["feature", "status:ready", "tab:features", "voice"])
        self.assertEqual(issue["state"], "open")

    def test_the_card_id_is_a_hidden_marker_and_the_link_lands_in_front_matter(self):
        card = self.card()
        self.sync()
        self.assertIn(f"<!-- relay-id: {card.id} -->", self.issue()["body"])
        self.assertEqual(self.reload(card.id).front["links"]["github"], "relay/terminal#1")

    def test_a_done_card_opens_its_issue_closed(self):
        self.card(status="done")
        self.sync()
        self.assertEqual((self.issue()["state"], self.issue()["state_reason"]),
                         ("closed", "completed"))

    def test_an_untriaged_issue_lands_in_the_inbox(self):
        self.gh.make_issue("Nobody has labelled this", "## Issue\nit broke\n")
        self.sync()
        card = next(c for c in self.board.cards() if c.title == "Nobody has labelled this")
        self.assertEqual(card.status, F.IMPORT_STATUS)
        self.assertCheckClean()

    def test_a_closed_issue_imports_as_a_done_card(self):
        self.gh.make_issue("Already finished there", "## Issue\ndone\n")
        self.gh.web_edit(1, state="closed", state_reason="completed")
        self.sync()
        card = next(c for c in self.board.cards() if c.title == "Already finished there")
        self.assertEqual(card.status, "done")
        self.assertEqual(card.path.parent.name, "done")

    def test_an_issue_nobody_here_knows_becomes_a_card(self):
        self.gh.make_issue("Something filed on GitHub", "## Issue\nit broke\n",
                           labels=["bug", "tab:bugs", "status:inbox"])
        result = self.sync()
        self.assertEqual(result.imported, 1)
        card = next(c for c in self.board.cards() if c.title == "Something filed on GitHub")
        self.assertEqual(card.status, "inbox")
        self.assertEqual(self.board.category_of(card.path), "changes")
        self.assertEqual(list(card.front.get("labels") or []), ["bug"])
        self.assertEqual(card.front["links"]["github"], "relay/terminal#1")
        self.assertIn("it broke", card.body)
        self.assertIn(f"<!-- relay-id: {card.id} -->", self.issue()["body"])

    def test_both_directions_at_once(self):
        self.card(title="Mine")
        self.gh.make_issue("Theirs", "## Issue\nfrom github\n")
        result = self.sync()
        self.assertEqual((result.creates, result.imported), (1, 1))
        self.assertEqual(sorted(i["title"] for i in self.gh.main.issues.values()),
                         ["Mine", "Theirs"])
        self.assertEqual(sorted(c.title for c in self.board.cards()), ["Mine", "Theirs"])

    def test_a_second_run_changes_nothing(self):
        self.card()
        self.sync()
        before = json.dumps(self.gh.main.issues, sort_keys=True)
        writes = len(self.gh.writes)
        result = self.sync()
        self.assertEqual(json.dumps(self.gh.main.issues, sort_keys=True), before)
        self.assertEqual(len(self.gh.writes), writes)
        self.assertEqual((result.pushed, result.pulled, result.creates), (0, 0, 0))

    def test_the_board_still_checks_clean(self):
        self.card(tasks=[("a3", False, "Record audio")])
        self.gh.make_issue("Theirs", "## Issue\nfrom github\n- [ ] do a thing\n")
        self.sync()
        self.assertCheckClean()


# ------------------------------------------------------- one side at a time

class OneSidedEditTests(SyncCase):
    def setUp(self):
        super().setUp()
        self.card_id = self.card(tasks=[("a3", False, "Record audio")]).id
        self.sync()

    # ---- pushes
    def test_a_retitled_card_retitles_the_issue(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("# Voice transcription mode",
                                                            "# Voice mode"))
        result = self.sync()
        self.assertEqual(self.issue()["title"], "Voice mode")
        self.assertEqual(result.pushed, 1)

    def test_an_edited_body_reaches_the_issue(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("add voice transcribe mode",
                                                            "hold Right Alt to dictate"))
        self.sync()
        self.assertIn("hold Right Alt to dictate", self.issue()["body"])

    def test_a_ticked_task_reaches_the_issue(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("- [ ] Record audio",
                                                            "- [x] Record audio"))
        self.sync()
        self.assertIn("- [x] Record audio", self.issue()["body"])

    def test_a_new_label_reaches_the_issue(self):
        self.edit_card(self.card_id, labels=["feature", "voice"])
        self.sync()
        self.assertIn("voice", self.labels_of())

    def test_a_removed_label_is_removed_there_too(self):
        self.edit_card(self.card_id, labels=[])
        self.sync()
        self.assertEqual(self.labels_of(), ["status:ready", "tab:features"])

    def test_a_status_change_moves_the_status_label(self):
        card = self.reload(self.card_id)
        card.set("status", "in-progress")
        target = self.board.root / card.expected_folder("features") / card.path.name
        B.atomic_write(card.path, card.to_text())
        self.sync()
        self.assertIn("status:in-progress", self.labels_of())
        self.assertNotIn("status:ready", self.labels_of())

    def test_moving_the_card_to_another_tab_moves_the_label(self):
        card = self.reload(self.card_id)
        target = self.root / "changes" / card.path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        card.path.rename(target)
        self.sync()
        self.assertIn("tab:bugs", self.labels_of())

    def test_an_assignee_is_pushed_when_a_login_is_mapped(self):
        self.edit_card(self.card_id, assignee="elliott")
        self.sync(login_map={"elliott": "eash"})
        self.assertEqual([a["login"] for a in self.issue()["assignees"]], ["eash"])

    def test_an_unmapped_assignee_is_left_alone(self):
        self.edit_card(self.card_id, assignee="agent")
        self.sync(login_map={"elliott": "eash"})
        self.assertEqual(self.issue()["assignees"], [])

    # ---- pulls
    def test_a_retitled_issue_retitles_the_card(self):
        self.gh.web_edit(1, title="Dictation mode")
        result = self.sync()
        self.assertEqual(self.reload(self.card_id).title, "Dictation mode")
        self.assertEqual(result.pulled, 1)

    def test_an_edited_issue_body_reaches_the_card(self):
        body = self.issue()["body"].replace("add voice transcribe mode", "typed in the browser")
        self.gh.web_edit(1, body=body)
        self.sync()
        self.assertIn("typed in the browser", self.reload(self.card_id).body)

    def test_a_box_ticked_in_the_web_ui_ticks_the_card(self):
        self.gh.web_edit(1, body=self.issue()["body"].replace("- [ ] Record audio",
                                                              "- [x] Record audio"))
        self.sync()
        tasks = self.reload(self.card_id).tasks()
        self.assertEqual([(t.item_id, t.done) for t in tasks], [("a3", True)])

    def test_a_checkbox_typed_in_the_web_ui_becomes_a_card_task_with_a_marker(self):
        self.gh.web_edit(1, body=self.issue()["body"].replace(
            "- [ ] Record audio", "- [ ] Record audio <!-- t:a3 -->\n- [ ] Ship it"))
        self.sync()
        texts = [(t.text, bool(t.item_id)) for t in self.reload(self.card_id).tasks()]
        self.assertIn(("Ship it", True), texts)
        self.assertCheckClean()

    def test_a_label_added_on_github_reaches_the_card(self):
        self.gh.web_edit(1, labels=self.labels_of() + ["needs-design"])
        self.sync()
        self.assertIn("needs-design", self.reload(self.card_id).front["labels"])

    def test_a_status_label_changed_on_github_moves_the_card_and_its_file(self):
        labels = [l for l in self.labels_of() if not l.startswith("status:")]
        self.gh.web_edit(1, labels=labels + ["status:needs-qa-llm"])
        self.sync()
        card = self.reload(self.card_id)
        self.assertEqual(card.status, "needs-qa-llm")
        self.assertEqual(card.path.parent.name, "needs_qa_llm")
        self.assertCheckClean()

    def test_an_assignee_set_on_github_reaches_the_card(self):
        self.gh.main.issues[1]["assignees"] = [{"login": "eash"}]
        self.gh.web_edit(1)
        self.sync(login_map={"elliott": "eash"})
        self.assertEqual(self.reload(self.card_id).front.get("assignee"), "elliott")


# ---------------------------------------------------------------- close and reopen

class ClosingTests(SyncCase):
    def setUp(self):
        super().setUp()
        self.card_id = self.card().id
        self.sync()

    def move(self, status):
        card = self.reload(self.card_id)
        card.set("status", status)
        target = self.board.root / card.expected_folder("features") / card.path.name
        B.atomic_write(card.path, card.to_text())
        target.parent.mkdir(parents=True, exist_ok=True)
        if target != card.path:
            card.path.rename(target)

    def test_done_closes_the_issue_as_completed(self):
        self.move("done")
        self.sync()
        self.assertEqual((self.issue()["state"], self.issue()["state_reason"]),
                         ("closed", "completed"))

    def test_dropped_closes_the_issue_as_not_planned(self):
        self.move("dropped")
        self.sync()
        self.assertEqual((self.issue()["state"], self.issue()["state_reason"]),
                         ("closed", "not_planned"))

    def test_an_issue_closed_on_github_marks_the_card_done(self):
        self.gh.web_edit(1, state="closed", state_reason="completed")
        self.sync()
        card = self.reload(self.card_id)
        self.assertEqual(card.status, "done")
        self.assertEqual(card.path.parent.name, "done")

    def test_an_issue_closed_as_not_planned_drops_the_card(self):
        self.gh.web_edit(1, state="closed", state_reason="not_planned")
        self.sync()
        self.assertEqual(self.reload(self.card_id).status, "dropped")

    def test_a_reopened_issue_brings_the_card_back(self):
        self.move("done")
        self.sync()
        self.gh.web_edit(1, state="open", state_reason=None)
        self.sync()
        card = self.reload(self.card_id)
        self.assertEqual(card.status, F.REOPEN_STATUS)
        self.assertEqual(card.path.parent.name, "features")
        self.assertCheckClean()

    def test_reopening_a_card_reopens_the_issue(self):
        self.move("done")
        self.sync()
        self.move("in-progress")
        self.sync()
        self.assertEqual(self.issue()["state"], "open")
        self.assertIn("status:in-progress", self.labels_of())


# -------------------------------------------------------------- both sides at once

class MergeTests(SyncCase):
    def setUp(self):
        super().setUp()
        self.card_id = self.card(tasks=[("a3", False, "Record audio")]).id
        self.sync()

    def test_independent_fields_both_land(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("# Voice transcription mode",
                                                            "# Voice mode"))
        self.gh.web_edit(1, labels=self.labels_of() + ["needs-design"])
        result = self.sync()
        self.assertEqual(result.conflicts, [])
        self.assertEqual(self.issue()["title"], "Voice mode")
        self.assertIn("needs-design", self.reload(self.card_id).front["labels"])

    def test_prose_here_and_a_ticked_box_there_merge(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("add voice transcribe mode",
                                                            "rewritten locally"))
        self.gh.web_edit(1, body=self.issue()["body"].replace("- [ ] Record audio",
                                                              "- [x] Record audio"))
        result = self.sync()
        self.assertEqual(result.conflicts, [])
        merged = self.reload(self.card_id)
        self.assertIn("rewritten locally", merged.body)
        self.assertEqual([(t.item_id, t.done) for t in merged.tasks()], [("a3", True)])
        self.assertIn("rewritten locally", self.issue()["body"])
        self.assertIn("- [x] Record audio", self.issue()["body"])

    def test_the_same_field_changed_on_both_sides_is_a_conflict_and_nothing_is_overwritten(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("# Voice transcription mode",
                                                            "# Local title"))
        self.gh.web_edit(1, title="Remote title")
        result = self.sync()
        self.assertEqual([c["field"] for c in result.conflicts], ["title"])
        self.assertEqual(self.reload(self.card_id).title, "Local title")
        self.assertEqual(self.issue()["title"], "Remote title")

    def test_a_conflict_is_surfaced_on_the_card_thread_with_both_versions(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("# Voice transcription mode",
                                                            "# Local title"))
        self.gh.web_edit(1, title="Remote title")
        self.sync()
        text = self.board.thread_path(self.card_id).read_text(encoding="utf-8")
        self.assertIn("sync conflict", text)
        self.assertIn("Local title", text)
        self.assertIn("Remote title", text)
        self.assertCheckClean()

    def test_an_unresolved_conflict_is_not_repeated_on_every_sync(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("# Voice transcription mode",
                                                            "# Local title"))
        self.gh.web_edit(1, title="Remote title")
        self.sync()
        entries = len(self.board.thread(self.card_id))
        self.sync()
        self.assertEqual(len(self.board.thread(self.card_id)), entries)

    def test_resolving_the_conflict_on_one_side_lets_it_through(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("# Voice transcription mode",
                                                            "# Local title"))
        self.gh.web_edit(1, title="Remote title")
        self.sync()
        self.gh.web_edit(1, title="Local title")
        result = self.sync()
        self.assertEqual(result.conflicts, [])
        self.assertEqual(self.reload(self.card_id).title, "Local title")

    def test_the_same_edit_on_both_sides_is_not_a_conflict(self):
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("# Voice transcription mode",
                                                            "# Same title"))
        self.gh.web_edit(1, title="Same title")
        result = self.sync()
        self.assertEqual(result.conflicts, [])

    def test_a_conflict_survives_a_quiet_sync_instead_of_pushing_over_it(self):
        """The listing trap: an unresolved conflict, then nothing changes anywhere.

        The conflicted card's baseline is deliberately left behind, so "absent from the
        changed-issues listing" must not read as "the issue equals the baseline" — an unchanged
        listing is not an unchanged issue *relative to that card*. Guessing that way pushes the
        local version over the remote edit the conflict was about, which is the one write the
        whole merge exists to prevent.
        """
        card = self.reload(self.card_id)
        self.edit_card(self.card_id, body=card.body.replace("add voice transcribe mode",
                                                            "local words"))
        self.gh.web_edit(1, body=self.issue()["body"].replace("add voice transcribe mode",
                                                              "remote words"))
        first = self.sync()
        self.assertEqual([c["field"] for c in first.conflicts], ["prose"])
        second = self.sync()
        self.assertEqual([c["field"] for c in second.conflicts], ["prose"])
        self.assertIn("remote words", self.issue()["body"])
        self.assertIn("local words", self.reload(self.card_id).body)
        self.assertCheckClean()


# -------------------------------------------------------------------- comments

class CommentTests(SyncCase):
    def setUp(self):
        super().setUp()
        self.card_id = self.card().id
        self.sync()

    def test_a_thread_entry_becomes_a_comment_carrying_its_hidden_marker(self):
        entry = self.board.append_thread(self.card_id, "where should this run?", author="owner",
                                         kind="question")
        result = self.sync()
        self.assertEqual(result.comments_out, 1)
        body = self.gh.comments_of(1)[0]["body"]
        self.assertIn("where should this run?", body)
        self.assertIn(f"<!-- relay-entry: {entry.entry_id}", body)

    def test_a_relay_comment_is_never_imported_back(self):
        self.board.append_thread(self.card_id, "ours", author="owner", kind="comment")
        self.sync()
        before = len(self.board.thread(self.card_id))
        result = self.sync()
        self.assertEqual(result.comments_in, 0)
        self.assertEqual(len(self.board.thread(self.card_id)), before)
        self.assertEqual(len(self.gh.comments_of(1)), 1)

    def test_a_github_comment_becomes_a_thread_entry_under_its_author(self):
        self.gh.web_comment(1, "have you tried turning it off", author="octocat")
        result = self.sync()
        self.assertEqual(result.comments_in, 1)
        entry = self.board.thread(self.card_id)[-1]
        self.assertEqual(entry.author, "octocat")
        self.assertEqual(entry.attrs.get("via"), "github")
        self.assertIn("turning it off", entry.text)

    def test_an_imported_comment_is_not_imported_twice(self):
        self.gh.web_comment(1, "once", author="octocat")
        self.sync()
        count = len(self.board.thread(self.card_id))
        self.sync()
        self.assertEqual(len(self.board.thread(self.card_id)), count)

    def test_an_imported_comment_is_not_pushed_back_as_a_new_comment(self):
        self.gh.web_comment(1, "from github", author="octocat")
        self.sync()
        self.assertEqual(len(self.gh.comments_of(1)), 1)
        self.sync()
        self.assertEqual(len(self.gh.comments_of(1)), 1)

    def test_events_stay_local(self):
        self.board.append_thread(self.card_id, "- ✦ agent moved this card", author="agent",
                                 kind="event")
        result = self.sync()
        self.assertEqual(result.comments_out, 0)
        self.assertEqual(self.gh.comments_of(1), [])

    def test_a_marker_a_human_deleted_is_put_back_rather_than_re_imported(self):
        self.board.append_thread(self.card_id, "ours", author="owner", kind="comment")
        self.sync()
        comment = self.gh.comments_of(1)[0]
        self.gh.main.comments[comment["id"]]["body"] = "ours, edited by hand"
        self.gh.web_edit(1)                     # the issue is touched, so the sync looks again
        result = self.sync()
        self.assertEqual(result.comments_in, 0)
        self.assertIn("relay-entry:", self.gh.comments_of(1)[0]["body"])
        self.assertCheckClean()

    def test_comments_round_trip_in_both_directions_without_echo(self):
        self.board.append_thread(self.card_id, "ours", author="owner", kind="comment")
        self.gh.web_comment(1, "theirs", author="octocat")
        self.sync()
        self.sync()
        self.assertEqual(len(self.gh.comments_of(1)), 2)
        texts = [e.text for e in self.board.thread(self.card_id)]
        self.assertEqual(sum(1 for t in texts if "theirs" in t), 1)
        self.assertEqual(sum(1 for t in texts if t.strip() == "ours"), 1)


# ------------------------------------------------------- the owner's decisions (2026-09-18)

class ConfigTests(SyncCase):
    """What lives in the committed `board.yaml` and what is per user (`board_config`)."""

    def write_config(self, extra: str):
        (self.root / B.BOARD_CONFIG).write_text(CONFIG + extra, encoding="utf-8")

    def test_the_committed_block_carries_the_repository_and_the_caps(self):
        self.write_config("github: {repo: relay/terminal, base_url: 'https://ghe.example.com', "
                          "create_cap: 5, default_tab: bugs}\n")
        config = F.board_config(self.board)
        self.assertEqual(config["repo"], "relay/terminal")
        self.assertEqual(config["base_url"], "https://ghe.example.com")
        self.assertEqual(config["create_cap"], 5)
        self.assertEqual(config["default_tab"], "bugs")

    def test_a_board_with_no_github_block_has_defaults_and_no_repository(self):
        config = F.board_config(self.board)
        self.assertEqual(config["repo"], "")
        self.assertEqual(config["create_cap"], F.DEFAULT_CREATE_CAP)
        self.assertEqual(config["login_map"], {})
        self.assertEqual(config["comment_kinds"], list(F.DEFAULT_COMMENT_KINDS))

    def test_the_login_map_is_per_user_and_read_from_the_private_root(self):
        self.write_config("github: {repo: relay/terminal}\n")
        (self.root / ".private").mkdir(exist_ok=True)
        (self.root / F.LOGINS_FILES[0]).write_text("Elliott: elliott-ash\nAna: anab\n",
                                                   encoding="utf-8")
        self.assertEqual(F.board_config(self.board)["login_map"],
                         {"Elliott": "elliott-ash", "Ana": "anab"})

    def test_the_login_map_may_be_json_or_nested_under_logins(self):
        (self.root / ".private").mkdir(exist_ok=True)
        (self.root / F.LOGINS_FILES[2]).write_text(
            json.dumps({"logins": {"Elliott": "elliott-ash"}}), encoding="utf-8")
        self.assertEqual(F.read_logins(self.board), {"Elliott": "elliott-ash"})

    def test_a_login_map_committed_to_board_yaml_is_not_read(self):
        # The decision: logins are somebody's account names, not a property of the project.
        self.write_config("github: {repo: relay/terminal, login_map: {Elliott: elliott-ash}}\n")
        self.assertEqual(F.board_config(self.board)["login_map"], {})

    def test_an_unreadable_login_file_is_no_map_rather_than_a_failed_sync(self):
        (self.root / ".private").mkdir(exist_ok=True)
        (self.root / F.LOGINS_FILES[2]).write_text("{ not json", encoding="utf-8")
        self.assertEqual(F.read_logins(self.board), {})

    def test_the_login_file_leaves_relay_board_check_clean(self):
        (self.root / ".private").mkdir(exist_ok=True)
        (self.root / F.LOGINS_FILES[0]).write_text("Elliott: elliott-ash\n", encoding="utf-8")
        self.card()
        self.assertCheckClean()

    def test_comment_kinds_can_be_widened_and_an_unknown_kind_is_refused(self):
        self.write_config("github: {repo: relay/terminal, comment_kinds: [comment, progress]}\n")
        self.assertEqual(F.board_config(self.board)["comment_kinds"], ["comment", "progress"])
        self.write_config("github: {repo: relay/terminal, comment_kinds: [gossip]}\n")
        with self.assertRaises(F.ForgeError) as bad:
            F.board_config(self.board)
        self.assertIn("gossip", str(bad.exception))


class CommentKindTests(SyncCase):
    """Which thread kinds are public (owner, 2026-09-18): the said-out-loud ones only."""

    def setUp(self):
        super().setUp()
        self.card_id = self.card().id
        self.sync()

    def add(self, kind, text="text"):
        return self.board.append_thread(self.card_id, text, author="agent", kind=kind)

    def test_progress_and_evidence_stay_local_by_default(self):
        for kind in ("progress", "evidence"):
            self.add(kind, f"a {kind} entry")
        result = self.sync()
        self.assertEqual(result.comments_out, 0)
        self.assertEqual(self.gh.comments_of(1), [])

    def test_the_four_said_out_loud_kinds_are_pushed(self):
        for kind in ("comment", "question", "decision", "note"):
            self.add(kind, f"a {kind} entry")
        result = self.sync()
        self.assertEqual(result.comments_out, 4)

    def test_board_yaml_can_widen_what_goes_out(self):
        self.add("progress", "ran the suite")
        engine = self.engine(comment_kinds=["comment", "progress"])
        result = engine.run(confirm_bulk=True)
        self.assertEqual(result.comments_out, 1)
        self.assertIn("ran the suite", self.gh.comments_of(1)[0]["body"])


class OneRepositoryTests(SyncCase):
    """One board, one repository: pointing a synced board elsewhere is refused (decision d)."""

    def test_a_board_already_synced_refuses_a_different_repository(self):
        self.card()
        self.sync()
        self.gh.add_repo("other/repo")
        with self.assertRaises(F.ForgeError) as refused:
            self.engine(self.provider(repo="other/repo"))
        message = str(refused.exception)
        self.assertIn("relay/terminal", message)
        self.assertIn("other/repo", message)
        self.assertIn("One board", message)

    def test_nothing_is_written_to_either_side_by_the_refusal(self):
        card_id = self.card().id
        self.sync()
        link = self.reload(card_id).front["links"]["github"]
        self.gh.add_repo("other/repo")
        writes = len(self.gh.writes)
        with self.assertRaises(F.ForgeError):
            self.engine(self.provider(repo="other/repo"))
        self.assertEqual(len(self.gh.writes), writes)
        self.assertEqual(self.reload(card_id).front["links"]["github"], link)

    def test_a_link_left_on_a_card_refuses_even_when_the_state_file_is_gone(self):
        self.card()
        self.sync()
        (self.root / F.STATE_FILE).unlink()
        self.gh.add_repo("other/repo")
        with self.assertRaises(F.ForgeError) as refused:
            self.engine(self.provider(repo="other/repo"))
        self.assertIn("relay/terminal", str(refused.exception))
        self.assertIn("1 card(s) linked", str(refused.exception))

    def test_the_same_repository_is_of_course_fine(self):
        self.card()
        self.sync()
        self.assertEqual(self.sync().errors, [])


class ImportedIssueTests(SyncCase):
    """Issues filed on GitHub do become cards (decision c), in the tab their label names."""

    def test_the_tab_label_picks_the_folder(self):
        self.gh.make_issue("Crash on startup", labels=["tab:bugs"])
        self.sync()
        card = next(c for c in self.board.cards() if c.title == "Crash on startup")
        self.assertEqual(F.tab_of(self.board, card), "bugs")
        self.assertEqual(card.status, F.IMPORT_STATUS)

    def test_default_tab_is_used_when_the_issue_names_none(self):
        self.gh.make_issue("No label at all")
        self.sync(default_tab="bugs")
        card = next(c for c in self.board.cards() if c.title == "No label at all")
        self.assertEqual(F.tab_of(self.board, card), "bugs")

    def test_the_first_tab_is_used_when_there_is_no_default(self):
        self.gh.make_issue("No label at all")
        self.sync()
        card = next(c for c in self.board.cards() if c.title == "No label at all")
        self.assertEqual(F.tab_of(self.board, card), "features")

    def test_an_imported_card_is_a_normal_card_check_accepts(self):
        self.gh.make_issue("Crash on startup", labels=["tab:bugs"])
        self.sync()
        self.assertCheckClean()


# ---------------------------------------------------------------------- privacy

class PrivacyTests(SyncCase):
    def test_a_private_card_is_never_an_issue(self):
        self.card(title="The secret plan for the launch", private=True)
        self.card(title="An ordinary card")
        self.sync()
        self.assertEqual([i["title"] for i in self.gh.main.issues.values()], ["An ordinary card"])

    def test_memory_and_alias_cards_are_never_issues(self):
        self.card(title="Remember the keyring rule", card_type="memory", status="active",
                  tab=None, labels=())
        self.card(title="Run the GUI checks", card_type="alias", status="active",
                  tab=None, labels=(), name="gui-checks", kind="command")
        self.card(title="An ordinary card")
        self.sync()
        self.assertEqual([i["title"] for i in self.gh.main.issues.values()], ["An ordinary card"])

    def test_no_outgoing_request_may_carry_a_private_card_title(self):
        private = self.card(title="The secret plan for the launch", private=True)
        card = self.card(title="An ordinary card")
        self.edit_card(card.id, body=card.body + f"\nSee also {private.title}\n")
        result = self.engine().run(confirm_bulk=True)
        self.assertEqual(self.gh.main.issues, {})
        self.assertTrue(any("never leaves this machine" in e for e in result.errors), result.errors)

    def test_an_honest_card_that_merely_repeats_a_line_is_not_refused(self):
        """The guard names identities, so two cards asking for the same thing still sync."""
        self.card(title="Private twin of the voice work", private=True,
                  request="add voice transcribe mode")
        self.card(title="An ordinary card", request="add voice transcribe mode")
        result = self.sync()
        self.assertEqual(result.creates, 1)
        self.assertEqual([i["title"] for i in self.gh.main.issues.values()], ["An ordinary card"])

    def test_no_outgoing_request_may_carry_a_private_card_id(self):
        private = self.card(title="Secret work item here", private=True)
        card = self.card(title="An ordinary card")
        self.edit_card(card.id, body=card.body + f"\nblocked by #{private.id}\n")
        result = self.engine().run(confirm_bulk=True)
        self.assertEqual(self.gh.main.issues, {})
        self.assertTrue(any("private card" in e for e in result.errors), result.errors)

    def test_no_comment_may_carry_a_memory_card_title(self):
        memory = self.card(title="Remember the keyring rule", card_type="memory",
                           status="active", tab=None, labels=())
        card = self.card(title="An ordinary card")
        self.sync()
        self.board.append_thread(card.id, f"following {memory.title}", author="owner", kind="note")
        result = self.engine().run(confirm_bulk=True)
        self.assertEqual(self.gh.comments_of(1), [])
        self.assertTrue(any("never leaves this machine" in e for e in result.errors), result.errors)

    def test_links_plans_is_inert_and_never_reaches_an_issue(self):
        # Card #X7NB dropped the plan card type. `links.plans` stays in the schema — 331 cards
        # carry `plans: []` — but nothing writes it and the sync no longer reads it.
        card = self.card()
        card.set("links", {**card.front["links"], "plans": ["M3XJ"]})
        B.atomic_write(card.path, card.to_text())
        self.sync()
        body = self.issue()["body"]
        self.assertNotIn("M3XJ", body)
        self.assertNotIn("plan cards", body)

    def test_the_guard_is_one_choke_point_every_write_passes(self):
        """Even a call the engine never makes today is refused by the same guard."""
        self.card(title="The secret plan for the launch", private=True)
        engine = self.engine()
        with self.assertRaises(F.ForgePrivacyError):
            engine.provider.create_issue("The secret plan for the launch", "x")
        with self.assertRaises(F.ForgePrivacyError):
            engine.provider.create_comment(1, "about the secret plan for the launch")
        with self.assertRaises(F.ForgePrivacyError):
            engine.provider.ensure_labels(["the secret plan for the launch"])
        self.assertEqual(self.gh.writes, [])


# ------------------------------------------------------------ safety and state

class SafetyTests(SyncCase):
    def test_a_dry_run_touches_neither_side(self):
        self.card()
        self.gh.make_issue("Theirs", "## Issue\nfrom github\n")
        before = json.dumps(self.gh.main.issues, sort_keys=True)
        result = self.engine().plan()
        self.assertEqual(self.gh.writes, [])
        self.assertEqual(json.dumps(self.gh.main.issues, sort_keys=True), before)
        self.assertTrue(result.dry_run)
        self.assertEqual({p.action for p in result.cards}, {"create", "import"})
        self.assertFalse((self.root / F.STATE_FILE).exists())

    def test_progress_is_reported_per_card_and_never_by_a_dry_run(self):
        for n in range(3):
            self.card(title=f"Card number {n}")
        self.gh.make_issue("Theirs", "## Issue\nfrom github\n")
        seen = []
        self.engine(on_progress=seen.append).plan()
        self.assertEqual(seen, [])
        self.engine(on_progress=seen.append).run(confirm_bulk=True)
        self.assertEqual([p["done"] for p in seen], [1, 2, 3, 4])
        self.assertTrue(all(p["total"] == 4 for p in seen))
        self.assertEqual(sorted(p["action"] for p in seen),
                         ["create", "create", "create", "import"])
        self.assertTrue(all(p["card"] for p in seen))     # the import names the card it made

    def test_a_watcher_that_throws_does_not_break_the_sync(self):
        self.card()

        def boom(_):
            raise RuntimeError("the pane went away")

        result = self.engine(on_progress=boom).run(confirm_bulk=True)
        self.assertEqual(result.errors, [])
        self.assertEqual(len(self.gh.main.issues), 1)

    def test_a_dry_run_says_exactly_what_the_run_will_do(self):
        self.card(tasks=[("a3", False, "Record audio")])
        plan = self.engine().plan()
        self.assertEqual(plan.creates, 1)
        result = self.sync()
        self.assertEqual(result.creates, plan.creates)

    def test_a_bulk_first_sync_needs_confirmation(self):
        for n in range(5):
            self.card(title=f"Card number {n}")
        result = self.engine(create_cap=3).run()
        self.assertTrue(result.needs_confirm)
        self.assertEqual(result.creates, 5)
        self.assertEqual(self.gh.main.issues, {})
        self.assertEqual(self.gh.writes, [])

    def test_confirm_bulk_lets_it_through(self):
        for n in range(5):
            self.card(title=f"Card number {n}")
        result = self.engine(create_cap=3).run(confirm_bulk=True)
        self.assertFalse(result.needs_confirm)
        self.assertEqual(len(self.gh.main.issues), 5)

    def test_under_the_cap_needs_no_confirmation(self):
        for n in range(2):
            self.card(title=f"Card number {n}")
        result = self.engine(create_cap=3).run()
        self.assertFalse(result.needs_confirm)
        self.assertEqual(len(self.gh.main.issues), 2)

    def test_an_idle_sync_makes_no_request_beyond_the_conditional_ones(self):
        self.card()
        self.sync()
        self.sync()                          # the run that settles the ETag of the new issue
        before = len(self.gh.requests)
        writes = len(self.gh.writes)
        result = self.sync()
        self.assertTrue(result.idle, result.to_dict())
        self.assertEqual(len(self.gh.writes), writes)
        self.assertLessEqual(len(self.gh.requests) - before, 2)   # repo_info + a 304 listing

    def test_a_rate_limit_stops_cleanly_with_a_time_to_try_again(self):
        self.card()
        self.gh.refuse_next(403, {"Retry-After": "3600"}, {"message": "secondary rate limit"})
        result = self.engine(provider=self.provider(clock=lambda: 1_000_000.0)).run(confirm_bulk=True)
        self.assertEqual(result.retry_at, 1_000_000.0 + 3600)
        self.assertIn("retry_at_text", result.to_dict())
        self.assertEqual(self.gh.main.issues, {})
        self.assertEqual(self.slept, [])

    def test_a_rate_limit_half_way_keeps_what_was_already_done(self):
        for n in range(3):
            self.card(title=f"Card number {n}")
        engine = self.engine(provider=self.provider(clock=lambda: 1_000_000.0))
        original = engine.provider.inner.create_issue
        calls = []

        def create(*args, **kwargs):
            calls.append(args)
            if len(calls) == 2:
                raise F.ForgeRateLimited("GitHub rate limit reached; try again at 09:00",
                                         1_000_000.0 + 60)
            return original(*args, **kwargs)

        engine.provider.inner.create_issue = create
        result = engine.run(confirm_bulk=True)
        self.assertTrue(result.retry_at)
        self.assertEqual(len(self.gh.main.issues), 1)
        state = json.loads((self.root / F.STATE_FILE).read_text(encoding="utf-8"))
        self.assertEqual(len([c for c in state["cards"].values() if c.get("number")]), 1)
        # Trying again finishes the job without duplicating the first issue.
        result = self.sync()
        self.assertEqual(len(self.gh.main.issues), 3)
        self.assertEqual(sorted(i["title"] for i in self.gh.main.issues.values()),
                         ["Card number 0", "Card number 1", "Card number 2"])

    def test_the_state_file_holds_no_token_and_lives_in_the_private_root(self):
        self.card()
        self.sync()
        path = self.root / F.STATE_FILE
        self.assertTrue(path.exists())
        self.assertEqual(path.parent.name, B.PRIVATE_FOLDER)
        text = path.read_text(encoding="utf-8")
        self.assertNotIn(FG.TOKEN, text)
        self.assertNotIn("token", text.lower())
        self.assertCheckClean()

    def test_a_board_without_a_gitignore_gets_one_before_the_state_is_written(self):
        (self.root / ".gitignore").unlink()
        self.card()
        self.sync()
        self.assertIn(".private/", (self.root / ".gitignore").read_text(encoding="utf-8"))

    def test_an_auth_failure_is_a_result_not_a_traceback(self):
        self.card()
        result = self.engine(provider=self.provider(token=None, env={},
                                                    runner=lambda *a, **k: "")).run(confirm_bulk=True)
        self.assertTrue(result.errors)
        self.assertIn("GH_TOKEN", result.errors[0])
        self.assertEqual(self.gh.writes, [])

    def test_the_state_file_does_not_confuse_the_board_check_or_the_index(self):
        self.card()
        self.sync()
        self.assertCheckClean()
        self.assertNotIn("forge-sync", self.board.index_markdown())

    def test_losing_the_state_file_does_not_duplicate_anything(self):
        card = self.card()
        self.board.append_thread(card.id, "a question", author="owner", kind="question")
        self.sync()
        self.assertEqual(len(self.gh.comments_of(1)), 1)
        (self.root / F.STATE_FILE).unlink()
        result = self.sync()
        self.assertEqual(len(self.gh.main.issues), 1)
        self.assertEqual(result.creates, 0)
        self.assertEqual(len(self.gh.comments_of(1)), 1)   # the comment is recognised as ours
        self.assertEqual(len(self.board.thread(card.id)), 1)

    def test_a_repository_with_issues_disabled_is_refused_before_anything_is_written(self):
        self.gh.add_repo("me/fork", has_issues=False, parent="relay/terminal")
        self.card()
        result = self.engine(provider=self.provider("me/fork")).run(confirm_bulk=True)
        self.assertTrue(any("issues disabled" in e for e in result.errors))
        self.assertEqual(self.gh.writes, [])

    def test_nothing_is_ever_deleted(self):
        card = self.card()
        self.sync()
        card.path.unlink()                       # the card is gone from the board
        self.sync()
        self.assertEqual(len(self.gh.main.issues), 1)
        self.assertEqual(self.issue()["state"], "open")


# ----------------------------------------------------------- markers and humans

class MarkerTests(SyncCase):
    def test_the_id_marker_survives_a_human_rewriting_the_body(self):
        card = self.card()
        self.sync()
        body = self.issue()["body"]
        self.gh.web_edit(1, body="Rewritten entirely by a human.\n\n" + body.split("\n\n")[-2]
                         + "\n\n" + f"<!-- relay-sync -->\n<!-- relay-id: {card.id} -->\n")
        self.sync()
        self.assertIn("Rewritten entirely by a human.", self.reload(card.id).body)
        self.assertIn(f"<!-- relay-id: {card.id} -->", self.issue()["body"])

    def test_a_deleted_id_marker_is_written_back_from_the_front_matter_link(self):
        card = self.card()
        self.sync()
        self.gh.web_edit(1, body="No marker any more, and a new line.")
        self.sync()
        self.assertIn("No marker any more", self.reload(card.id).body)
        self.assertIn(f"<!-- relay-id: {card.id} -->", self.issue()["body"])   # written back
        result = self.sync()
        self.assertEqual(result.imported, 0)     # the issue is not mistaken for a new one
        self.assertEqual(len(self.board.cards()), 1)

    def test_the_hidden_markers_are_html_comments_so_github_hides_them(self):
        card = self.card(tasks=[("a3", False, "Record audio")])
        self.sync()
        body = self.issue()["body"]
        self.assertTrue(body.rstrip().endswith("-->"))
        for line in body.splitlines():
            if "relay-id" in line or "t:a3" in line:
                self.assertIn("<!--", line)
                self.assertIn("-->", line)

    def test_the_marker_is_matched_even_after_whitespace_is_mangled(self):
        self.assertTrue(F.ID_MARKER_RE.search("<!--relay-id:K7Q2-->"))
        self.assertTrue(F.ID_MARKER_RE.search("<!--   RELAY-ID:   K7Q2   -->"))


# ------------------------------------------------------------------- unit level

class MergeUnitTests(unittest.TestCase):
    def test_scalar_three_way(self):
        self.assertEqual(F.merge_scalar("a", "a", "a"), ("same", "a"))
        self.assertEqual(F.merge_scalar("a", "b", "a"), ("push", "b"))
        self.assertEqual(F.merge_scalar("a", "a", "c"), ("pull", "c"))
        self.assertEqual(F.merge_scalar("a", "b", "c")[0], "conflict")
        self.assertEqual(F.merge_scalar(None, "b", "b"), ("same", "b"))
        self.assertEqual(F.merge_scalar(None, "b", "c")[0], "conflict")

    def test_labels_merge_as_sets(self):
        merged, card_differs, issue_differs = F.merge_labels(["a"], ["a", "b"], ["a", "c"])
        self.assertEqual(merged, {"a", "b", "c"})
        self.assertTrue(card_differs and issue_differs)

    def test_a_removed_label_stays_removed(self):
        merged, _, _ = F.merge_labels(["a", "b"], ["a"], ["a", "b"])
        self.assertEqual(merged, {"a"})

    def test_tasks_merge_per_item(self):
        base = (("a3", False, "one"), ("b7", False, "two"))
        card = (("a3", False, "one renamed"), ("b7", False, "two"))
        issue = (("a3", False, "one"), ("b7", True, "two"))
        state, merged = F.merge_tasks(base, card, issue)
        self.assertEqual(state, "merged")
        self.assertEqual(merged, (("a3", False, "one renamed"), ("b7", True, "two")))

    def test_the_same_task_changed_on_both_sides_conflicts(self):
        base = (("a3", False, "one"),)
        state, _ = F.merge_tasks(base, (("a3", False, "here"),), (("a3", False, "there"),))
        self.assertEqual(state, "conflict")

    def test_a_body_round_trips_through_the_split(self):
        body = "## Issue\ntext\n\n## Tasks\n- [ ] one <!-- t:a3 -->\n- [x] two <!-- t:b7 -->"
        prose, tasks = F.split_body(body)
        self.assertEqual(tasks, (("a3", False, "one"), ("b7", True, "two")))
        rendered = F.render_body("Title", prose, tasks)
        self.assertIn("- [ ] one <!-- t:a3 -->", rendered)
        self.assertIn("- [x] two <!-- t:b7 -->", rendered)
        self.assertTrue(rendered.startswith("# Title\n"))

    def test_the_footer_is_stripped_before_a_body_is_compared(self):
        body = "text\n\n<!-- relay-sync -->\nRelay plan cards (not synced): `#K7Q2`\n<!-- relay-id: M3XJ -->\n"
        self.assertEqual(F.strip_footer(body), "text")


# ------------------------------------------- a run that stops half way

class _StopsAfterOneCard(F.ForgeSync):
    """An engine whose second card hits a rate limit.

    Stopping half way is the ordinary case, not a contrived one: GitHub's secondary limit is what
    a first sync of a real board runs into, and `_sync` returns right there. What is faked is only
    *where* it stops, so the test can say which card was left untouched.
    """

    _applied = 0

    def _apply_card(self, card, issue, plan, result):
        self._applied += 1
        if self._applied > 1:
            raise F.ForgeRateLimited("GitHub rate limit reached; try again at 09:00", 1.0e9)
        return super()._apply_card(card, issue, plan, result)


class _EditsUnderneath(F.ForgeSync):
    """An engine with somebody else writing the card file while it works.

    A sync runs on its own thread (protocol 19.14) while the Switchboard pane and the agent write
    through `BoardTools`, so this is what actually happens when the user edits a card during a sync.
    """

    edit = staticmethod(lambda: None)

    def _apply_card(self, card, issue, plan, result):
        type(self).edit()
        return super()._apply_card(card, issue, plan, result)


class StoppedRunTests(SyncCase):
    def marks(self):
        data = json.loads((self.root / F.STATE_FILE).read_text(encoding="utf-8"))
        return data.get("issues_etag", ""), data.get("last_seen", "")

    def test_a_stopped_run_leaves_the_listing_marks_where_the_last_whole_run_put_them(self):
        first, second = self.card(title="Alpha card"), self.card(title="Beta card")
        self.sync()
        settled = self.marks()
        self.gh.web_edit(1, title="Alpha as GitHub has it")
        self.gh.web_edit(2, title="Beta as GitHub has it")

        stopped = _StopsAfterOneCard(self.board, self.provider()).run(confirm_bulk=True)
        self.assertTrue(stopped.retry_at)
        # Nothing about the listing moved: the next run must be allowed to see both issues again.
        self.assertEqual(self.marks(), settled)

        again = self.sync()
        self.assertEqual(again.errors, [])
        titles = sorted(self.reload(c.id).title for c in (first, second))
        self.assertEqual(titles, ["Alpha as GitHub has it", "Beta as GitHub has it"])

    def test_a_whole_run_does_move_them(self):
        self.card()
        self.gh.make_issue("Theirs", "## Issue\nfrom github\n")
        self.sync()
        etag, last_seen = self.marks()
        self.assertTrue(etag)
        self.assertTrue(last_seen)


class ConcurrentWriteTests(SyncCase):
    def test_a_card_edited_while_the_sync_runs_is_not_overwritten(self):
        card = self.card()
        self.sync()
        self.gh.web_edit(1, title="Their title")

        def edit():
            self.edit_card(card.id, body=self.reload(card.id).body + "\nThe pane wrote this.\n")

        _EditsUnderneath.edit = staticmethod(edit)
        self.addCleanup(setattr, _EditsUnderneath, "edit", staticmethod(lambda: None))
        result = _EditsUnderneath(self.board, self.provider()).run(confirm_bulk=True)

        self.assertTrue(result.errors, "the lost race should be reported")
        self.assertIn("changed since it was read", " ".join(result.errors))
        self.assertIn("The pane wrote this.", self.reload(card.id).body)

    def test_the_same_sync_may_write_a_card_twice(self):
        """A create links the card and a later pull rewrites it; the second write must still pass."""
        card = self.card()
        self.sync()
        self.assertIn("relay/terminal#1", json.dumps(self.reload(card.id).front))
        self.gh.web_edit(1, title="Their title")
        self.sync()
        self.assertEqual(self.reload(card.id).title, "Their title")


if __name__ == "__main__":                                        # pragma: no cover
    unittest.main()
