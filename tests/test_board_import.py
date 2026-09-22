# SPDX-License-Identifier: AGPL-3.0-or-later
"""`relay_core.board_import`: proposing cards from a tracker, and writing the accepted ones.

Every test builds a throwaway project from a fixture tree under `tests/fixtures/trackers/`
and a throwaway Board beside it.  Two rules are checked over and over because they are
the ones that would hurt: **the files the import read are never touched**, and **the same
item is never imported twice**.  Nothing here calls a model or the network.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from relay_core import board as B
from relay_core import board_import as I
from relay_core import board_tools as T
from relay_core import project_probe as P

FIXTURES = Path(__file__).resolve().parent / "fixtures" / "trackers"
REPO = Path(__file__).resolve().parents[1]

CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: planning, folder: planning},
  {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""


class ImportCase(unittest.TestCase):
    """A project copied from fixtures, with an initialized Board inside it."""

    fixtures: tuple = ()
    config = CONFIG

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.project = Path(self.tmp.name).resolve() / "project"
        self.project.mkdir()
        for name in self.fixtures:
            shutil.copytree(FIXTURES / name, self.project, dirs_exist_ok=True)
        # The hidden folder Relay creates since 2026-09-19; `import-state.json` and the card paths
        # in every proposal are derived from it.
        self.root = self.project / B.DEFAULT_BOARD_FOLDER
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(self.config, encoding="utf-8")
        self.board = B.Board(self.root, self.project)
        self.before = self.snapshot()

    def tearDown(self):
        self.tmp.cleanup()

    # ---- helpers
    def snapshot(self) -> dict:
        """sha256 of every file in the project outside the board directory."""
        out = {}
        for path in sorted(self.project.rglob("*")):
            if not path.is_file() or self.root in path.parents or path == self.root:
                continue
            out[str(path.relative_to(self.project))] = hashlib.sha256(path.read_bytes()).hexdigest()
        return out

    def assert_sources_untouched(self):
        self.assertEqual(self.snapshot(), self.before,
                         "the import changed a file it was only supposed to read")

    def propose(self, kinds=None):
        return I.propose(self.project, kinds, board=self.board)

    def apply(self, proposals=None, **kwargs):
        return I.apply(self.board, self.propose() if proposals is None else proposals, **kwargs)

    def card(self, card_id: str) -> B.Card:
        found = self.board.card_by_id(card_id)
        self.assertIsNotNone(found, f"no card {card_id}")
        return found

    def by_key(self) -> dict:
        return {I.source_key_of(c): c for c in self.board.cards() if I.source_key_of(c)}

    def thread(self, card_id: str) -> str:
        path = self.board.thread_path(card_id)
        return path.read_text(encoding="utf-8") if path.exists() else ""

    def check(self) -> str:
        """`scripts/relay-board.py check` on the board this test built."""
        result = subprocess.run(
            [sys.executable, str(REPO / "scripts" / "relay-board.py"),
             "--issues", str(self.root), "check", "--strict"],
            capture_output=True, text=True, timeout=120)
        return f"rc={result.returncode}\n{result.stdout}{result.stderr}"

    def assert_check_clean(self):
        report = self.check()
        self.assertTrue(report.startswith("rc=0"), report)


# ----------------------------------------------------------------------------- proposing

class ProposeTest(ImportCase):
    fixtures = ("checklist", "taskmaster")

    def test_a_proposal_per_item_with_a_stable_key_and_a_source(self):
        proposals = self.propose()
        keys = [p.source_key for p in proposals]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual(keys, [p.source_key for p in self.propose()])
        one = next(p for p in proposals if p.kind == "checklist")
        self.assertTrue(one.source_line.startswith(I.SOURCE_PREFIX))
        self.assertIn(one.source_key, one.source_line)

    def test_finding_kinds_limits_the_run(self):
        self.assertEqual({p.kind for p in self.propose(["taskmaster"])}, {"taskmaster"})

    def test_statuses_map_onto_the_boards_own_columns(self):
        by_key = {p.source_key: p for p in self.propose()}
        prefix = "taskmaster:.taskmaster/tasks/tasks.json#master/"
        self.assertEqual(by_key[prefix + "1"].status, "done")
        self.assertEqual(by_key[prefix + "2"].status, "in-progress")
        self.assertEqual(by_key[prefix + "3"].status, "inbox")      # "pending" is untriaged
        self.assertEqual(by_key[prefix + "4"].status, "dropped")
        for proposal in self.propose():
            self.assertIn(proposal.status, B.WORK_STATUS_FOLDER)

    def test_every_card_is_labelled_with_imported_and_its_tracker(self):
        for proposal in self.propose():
            self.assertEqual(proposal.labels[:2], [I.IMPORT_LABEL, proposal.kind])

    def test_priority_decides_the_order_cards_are_created_in(self):
        order = [(I._priority_rank(p.priority)) for p in self.propose()]
        self.assertEqual(order, sorted(order))

    def test_a_dependency_becomes_another_proposals_key_and_a_dangling_one_is_dropped(self):
        by_key = {p.source_key: p for p in self.propose()}
        prefix = "taskmaster:.taskmaster/tasks/tasks.json#master/"
        self.assertEqual(by_key[prefix + "2"].depends_on, [prefix + "1"])
        # `master/1` and `feature-auth/1` share a number but not a tag, and must not link.
        auth = "taskmaster:.taskmaster/tasks/tasks.json#feature-auth/1"
        self.assertEqual(by_key[auth].depends_on, [])

    def test_a_dependency_on_something_outside_the_run_is_dropped_not_invented(self):
        only_two = [p for p in self.propose()
                    if p.source_key.endswith("master/2") or p.source_key.endswith("master/3")]
        self.assertTrue(only_two)
        ids = I.apply(self.board, only_two)
        cards = [self.card(i) for i in ids]
        blocked = [c for c in cards if c.front.get("blocked_by")]
        for card in blocked:
            for blocker in card.front["blocked_by"]:
                self.assertIsNotNone(self.board.card_by_id(blocker))

    def test_an_empty_project_proposes_nothing(self):
        empty = Path(self.tmp.name) / "empty"
        empty.mkdir()
        self.assertEqual(I.propose(empty), [])


# ------------------------------------------------------------------------------ applying

class ApplyTest(ImportCase):
    fixtures = ("checklist", "taskmaster", "beads", "backlog-md")

    def test_cards_are_written_the_originals_are_not_and_check_is_clean(self):
        ids = self.apply()
        self.assertTrue(ids)
        self.assertEqual(len(ids), len(set(ids)))
        self.assert_sources_untouched()
        self.assert_check_clean()

    def test_each_card_records_where_it_came_from_in_its_front_matter(self):
        self.apply()
        for card in self.board.cards():
            self.assertTrue(str(card.front["source"]).startswith(I.SOURCE_PREFIX), card.front)
            self.assertIsNotNone(I.source_key_of(card))
            self.assertEqual(card.front["labels"][0], I.IMPORT_LABEL)

    def test_each_card_gets_one_thread_entry_saying_where_it_came_from(self):
        ids = self.apply()
        for card_id in ids:
            entries = self.board.thread(card_id)
            notes = [e for e in entries if e.kind == "note" and "imported from" in e.text]
            self.assertEqual(len(notes), 1, self.thread(card_id))
            self.assertIn("The original file is unchanged", notes[0].text)

    def test_a_beads_comment_becomes_a_thread_entry_under_its_own_author(self):
        self.apply()
        card = self.by_key()["beads:.beads/issues.jsonl#bd-a3f2dd"]
        entries = self.board.thread(card.id)
        mine = [e for e in entries if e.author == "alice"]
        self.assertEqual([e.text for e in mine], ["Start with the CLI, it is the worst."])

    def test_subtasks_become_a_tasks_checklist_with_their_own_state(self):
        self.apply()
        card = self.by_key()["taskmaster:.taskmaster/tasks/tasks.json#master/2"]
        self.assertEqual([(i.text, i.status) for i in card.tasks()],
                         [("Pick a backend", "done"), ("Write the adapter", "in-progress"),
                          ("Expire old sessions", "open")])
        for item in card.tasks():
            self.assertIsNotNone(item.item_id)

    def test_a_subtask_dependency_becomes_a_blocked_by_marker_on_the_item(self):
        # The gap `docs/PROJECT-INIT-AND-IMPORT.md` §9 listed until 2026-09-18: Task Master
        # numbers dependencies between subtasks, and a `## Tasks` item can carry them.
        self.apply()
        card = self.by_key()["taskmaster:.taskmaster/tasks/tasks.json#master/2"]
        items = card.tasks()
        self.assertEqual([i.blocked_by for i in items],
                         [[], [items[0].item_id], [items[1].item_id]])
        self.assert_check_clean()
        self.assert_sources_untouched()

    def test_a_qualified_subtask_dependency_names_the_sibling_it_meant(self):
        # `"2.2"` inside task 2 is that task's own second subtask, not a card.
        self.apply()
        card = self.by_key()["taskmaster:.taskmaster/tasks/tasks.json#master/2"]
        third = card.tasks()[2]
        self.assertEqual(third.text, "Expire old sessions")
        self.assertEqual(third.blocked_by, [card.tasks()[1].item_id])

    def test_a_proposal_carries_item_dependencies_as_positions(self):
        by_key = {p.source_key: p for p in self.propose(["taskmaster"])}
        tasks = by_key["taskmaster:.taskmaster/tasks/tasks.json#master/2"].tasks
        self.assertEqual([t.get("blocked_by") for t in tasks], [None, [1], [2]])

    def test_a_dependency_becomes_blocked_by_naming_a_card_that_exists(self):
        self.apply()
        cards = self.by_key()
        blocked = cards["taskmaster:.taskmaster/tasks/tasks.json#master/2"]
        blocker = cards["taskmaster:.taskmaster/tasks/tasks.json#master/1"]
        self.assertEqual(blocked.front["blocked_by"], [blocker.id])

    def test_a_subtask_that_depends_on_another_task_is_blocked_by_that_card(self):
        (self.project / ".taskmaster" / "tasks" / "tasks.json").write_text(json.dumps({"tasks": [
            {"id": 1, "title": "The blocker task", "status": "pending"},
            {"id": 2, "title": "The blocked task", "status": "pending", "subtasks": [
                {"id": 1, "title": "Waits for the other card", "dependencies": ["1.1"]}]}]}),
            encoding="utf-8")
        self.before = self.snapshot()
        self.apply(self.propose(["taskmaster"]))
        cards = self.by_key()
        blocker = cards["taskmaster:.taskmaster/tasks/tasks.json#master/1"]
        blocked = cards["taskmaster:.taskmaster/tasks/tasks.json#master/2"]
        self.assertEqual(blocked.tasks()[0].blocked_by, [f"#{blocker.id}"])
        self.assert_check_clean()

    def test_an_item_dependency_on_a_card_outside_the_run_is_dropped(self):
        (self.project / ".taskmaster" / "tasks" / "tasks.json").write_text(json.dumps({"tasks": [
            {"id": 1, "title": "The blocker task", "status": "pending"},
            {"id": 2, "title": "The blocked task", "status": "pending", "subtasks": [
                {"id": 1, "title": "Waits for the other card", "dependencies": ["1.1"]}]}]}),
            encoding="utf-8")
        self.before = self.snapshot()
        blocked_only = [p for p in self.propose(["taskmaster"])
                        if p.source_key.endswith("#master/2")]
        self.apply(blocked_only)
        card = self.by_key()["taskmaster:.taskmaster/tasks/tasks.json#master/2"]
        self.assertEqual(card.tasks()[0].blocked_by, [])
        self.assert_check_clean()

    def test_a_beads_parent_child_edge_becomes_the_parent_field(self):
        self.apply()
        cards = self.by_key()
        child = cards["beads:.beads/issues.jsonl#bd-c99ff0.1"]
        self.assertEqual(child.front["parent"], cards["beads:.beads/issues.jsonl#bd-c99ff0"].id)

    def test_assignee_and_milestone_come_across_and_no_unknown_field_is_invented(self):
        self.apply()
        card = next(c for c in self.board.cards() if c.front.get("milestone"))
        self.assertEqual(card.front["milestone"], "beta-2")
        self.assertEqual(card.front["assignee"], "@test-hygiene")
        for card in self.board.cards():
            self.assertFalse(set(card.front) - B.ALLOWED_FIELDS["work"], card.front)

    def test_cards_land_in_the_folder_their_status_belongs_to(self):
        self.apply()
        for card in self.board.cards():
            want = card.expected_folder(self.board.category_of(card.path))
            self.assertEqual(str(card.path.parent.relative_to(self.root)), want)

    def test_the_column_ends_up_in_the_trackers_own_priority_order(self):
        self.apply()
        inbox = [c for c in self.board.cards() if c.status == "inbox"]
        inbox.sort(key=lambda c: c.rank)
        ranks = [c.rank for c in inbox]
        self.assertEqual(ranks, sorted(ranks))
        priorities = []
        for card in inbox:
            key = I.source_key_of(card)
            item = next((i for i in P.items_for(self.project) if i.source_key == key), None)
            priorities.append(I._priority_rank(item.priority if item else None))
        self.assertEqual(priorities, sorted(priorities))

    def test_the_tab_can_be_chosen(self):
        for card_id in self.apply(tab="bugs"):
            self.assertEqual(self.board.category_of(self.card(card_id).path), "changes")

    def test_an_unknown_tab_is_refused_before_anything_is_written(self):
        with self.assertRaises(I.ImportError_) as caught:
            I.apply(self.board, self.propose(), tab="nope")
        self.assertIn("features", str(caught.exception))
        self.assertEqual(self.board.cards(), [])

    def test_a_filter_tab_is_not_somewhere_a_card_can_live(self):
        with self.assertRaises(I.ImportError_):
            I.apply(self.board, self.propose(), tab="done")

    def test_the_default_tab_is_features_when_the_board_has_one(self):
        self.assertEqual(I.default_tab(self.board), "features")

    def test_applying_to_a_board_that_does_not_exist_yet_is_refused(self):
        other = B.Board(self.project / "nothing-here", self.project)
        with self.assertRaises(I.ImportError_) as caught:
            I.apply(other, self.propose())
        self.assertIn("board.yaml", str(caught.exception))

    def test_applying_nothing_writes_nothing(self):
        self.assertEqual(I.apply(self.board, []), [])
        self.assertEqual(self.board.cards(), [])
        self.assertFalse(I.state_path(self.board).exists())


# --------------------------------------------------------------------------- never twice

class IdempotenceTest(ImportCase):
    fixtures = ("checklist",)

    def test_a_second_run_proposes_nothing_and_writes_nothing(self):
        first = self.apply()
        self.assertTrue(first)
        self.assertEqual(self.propose(), [])
        self.assertEqual(self.apply(), [])
        self.assertEqual(len(self.board.cards()), len(first))
        self.assert_sources_untouched()
        self.assert_check_clean()

    def test_skipped_keys_names_what_propose_left_out(self):
        keys = {p.source_key for p in self.propose()}
        self.assertEqual(I.skipped_keys(self.project, board=self.board), [])
        self.apply()
        self.assertEqual(set(I.skipped_keys(self.project, board=self.board)), keys)

    def test_the_state_file_is_sorted_json_naming_every_card(self):
        ids = self.apply()
        text = I.state_path(self.board).read_text(encoding="utf-8")
        data = json.loads(text)
        self.assertEqual(data["version"], I.STATE_VERSION)
        self.assertEqual(sorted(data["imported"]), list(data["imported"]))
        self.assertEqual(sorted(v["card"] for v in data["imported"].values()), sorted(ids))
        self.assertTrue(text.endswith("\n"))

    def test_losing_the_state_file_does_not_duplicate_a_card(self):
        self.apply()
        I.state_path(self.board).unlink()
        self.assertEqual(self.propose(), [])             # the `source` field still says so

    def test_deleting_a_card_does_not_silently_re_import_it(self):
        ids = self.apply()
        self.card(ids[0]).path.unlink()
        self.assertEqual(self.propose(), [])             # the state file still says so

    def test_removing_the_key_from_both_is_how_you_import_it_again(self):
        ids = self.apply()
        card = self.card(ids[0])
        key = I.source_key_of(card)
        card.path.unlink()
        state = I.read_state(self.board)
        del state[key]
        I.write_state(self.board, state)
        self.assertEqual([p.source_key for p in self.propose()], [key])

    def test_editing_the_file_around_an_item_does_not_re_propose_it(self):
        self.apply()
        todo = self.project / "TODO.md"
        todo.write_text("# A new first line\n\n" + todo.read_text(), encoding="utf-8")
        self.before = self.snapshot()
        self.assertEqual(self.propose(), [])

    def test_a_genuinely_new_item_is_the_only_thing_proposed_next_time(self):
        self.apply()
        todo = self.project / "TODO.md"
        todo.write_text(todo.read_text() + "\n## Later\n\n- [ ] Something nobody wrote before\n",
                        encoding="utf-8")
        self.before = self.snapshot()
        proposals = self.propose()
        self.assertEqual([p.title for p in proposals], ["Something nobody wrote before"])
        self.apply(proposals)
        self.assert_sources_untouched()
        self.assert_check_clean()

    def test_a_key_holding_spaces_round_trips_through_the_source_field(self):
        """A Backlog.md task file is called `task-1 - Its Title.md`, so the key has spaces in
        it; reading it back as far as the first space would re-import it on every run."""
        shutil.copytree(FIXTURES / "backlog-md", self.project, dirs_exist_ok=True)
        self.before = self.snapshot()
        proposals = [p for p in self.propose() if p.kind == "backlog-md"]
        keys = {p.source_key for p in proposals}
        self.assertTrue(any(" " in k for k in keys), keys)
        I.apply(self.board, proposals)
        self.assertEqual({I.source_key_of(c) for c in self.board.cards()}, keys)
        self.assertEqual(self.propose(["backlog-md"]), [])
        I.state_path(self.board).unlink()
        self.assertEqual(self.propose(["backlog-md"]), [])

    def test_a_card_whose_source_was_typed_by_hand_is_not_mistaken_for_an_import(self):
        tools = T.BoardTools(self.board, enforce_limits=False, duplicate_check=False)
        tools.run("board_create_card", {"tab": "features", "status": "inbox", "title": "By hand",
                                        "request": "typed", "source": "the owner said so"})
        self.assertEqual(I.source_key_of(self.board.cards()[0]), None)
        self.assertTrue(self.propose())


# --------------------------------------------------------------- one case per tracker kind

class EveryTrackerTest(ImportCase):
    """Each fixture tree on its own: it imports, `check` passes, the source is untouched."""

    def _run(self, name: str, kind: str):
        shutil.copytree(FIXTURES / name, self.project, dirs_exist_ok=True)
        self.before = self.snapshot()
        proposals = self.propose([kind])
        self.assertTrue(proposals, f"{kind} proposed nothing")
        ids = I.apply(self.board, proposals)
        self.assertEqual(len(ids), len(proposals))
        self.assert_sources_untouched()
        self.assert_check_clean()
        self.assertEqual(self.propose([kind]), [])
        return ids

    def test_checklist(self):
        self._run("checklist", "checklist")

    def test_backlog_md(self):
        self._run("backlog-md", "backlog-md")

    def test_beads(self):
        self._run("beads", "beads")

    def test_taskmaster(self):
        self._run("taskmaster", "taskmaster")

    def test_spec_kit(self):
        ids = self._run("spec-kit", "spec-kit")
        card = self.card(ids[0])
        self.assertEqual(card.title, "User accounts")
        self.assertIn("T002 Initialize Python project with FastAPI dependencies",
                      [i.text for i in card.tasks()])

    def test_kiro(self):
        self._run("kiro", "kiro")
        card = self.by_key()["kiro:.kiro/specs/session-logout-fix/tasks.md#session-logout-fix"]
        statuses = {i.text: i.status for i in card.tasks()}
        self.assertEqual(statuses["3 Add authentication debugging and error logging"], "in-progress")

    def test_openspec(self):
        self._run("openspec", "openspec")
        card = self.by_key()["openspec:openspec/changes/add-dark-mode/tasks.md#add-dark-mode"]
        self.assertEqual(card.status, "in-progress")


# ------------------------------------------------------------------- text that breaks things

class AwkwardTextTest(ImportCase):
    def test_unicode_and_crlf_survive_into_the_card_and_check_stays_clean(self):
        shutil.copytree(FIXTURES / "checklist", self.project, dirs_exist_ok=True)
        self.before = self.snapshot()
        self.apply()
        titles = [c.title for c in self.board.cards()]
        self.assertIn("Ünïcøde in a task — and an em dash — plus emoji 🎛", titles)
        self.assertIn("A task written on Windows", titles)
        for card in self.board.cards():
            self.assertNotIn("\r", card.path.read_text(encoding="utf-8"))
        self.assert_sources_untouched()
        self.assert_check_clean()

    def test_a_title_with_yaml_punctuation_in_it_is_written_safely(self):
        (self.project / "TODO.md").write_text(
            "## Awkward\n"
            "- [ ] fix: the thing — it said \"no: really\" and {a: b} and [x, y]\n"
            "- [ ] 2026-09-18 a line that starts like a date\n", encoding="utf-8")
        self.before = self.snapshot()
        ids = self.apply()
        self.assertEqual(len(ids), 2)
        self.assert_check_clean()
        for card in self.board.cards():
            reread = B.Card.load(card.path)
            self.assertEqual(reread.front["source"], card.front["source"])

    def test_an_item_with_no_text_of_its_own_is_not_a_card(self):
        (self.project / "TODO.md").write_text("## Empty\n- [ ] \n- [ ] real one\n", encoding="utf-8")
        self.before = self.snapshot()
        self.assertEqual([p.title for p in self.propose()], ["real one"])

    def test_a_very_long_title_is_cut_to_what_the_board_accepts(self):
        (self.project / "TODO.md").write_text("## Long\n- [ ] " + "word " * 300 + "\n",
                                              encoding="utf-8")
        self.before = self.snapshot()
        proposal = self.propose()[0]
        self.assertLessEqual(len(proposal.title), 200)
        self.apply()
        self.assert_check_clean()


if __name__ == "__main__":                               # pragma: no cover
    unittest.main()
