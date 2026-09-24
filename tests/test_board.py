# SPDX-License-Identifier: AGPL-3.0-or-later
"""Board format tests: cards, ids, ranks, task markers, threads, check, migrate."""
import os
import shutil
import subprocess
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from relay_core import board as B

FIXTURES = Path(__file__).resolve().parent / "fixtures" / "issues_legacy"

CARD = """\
---
id: K7Q2
type: work
status: in-progress
labels: [voice, mvp]
component: [gui, worker]
milestone: desktop-alpha
assignee: agent
rank: 0i
created: '2026-09-17'
acceptance: holding Right Alt records speech
source: 'issues/feature_intake.txt: "add voice transcribe mode"'
links: {plans: [], commits: [], evidence: [], related: [M3XJ], github: null}
---
# Voice transcription mode

## Request
add voice transcribe mode (microphone icon)

## Tasks
- [x] Add OpenRouter transcription call <!-- t:b7 -->
- [ ] Record audio with QAudioSource <!-- t:a3 s=in-progress -->
  - [ ] Handle device permission errors <!-- t:c1 blocked_by=a3 -->
- [ ] #M3XJ Right-Alt push-to-talk <!-- t:d9 card=M3XJ -->
- [x] ~~Local whisper.cpp fallback~~ <!-- t:e2 s=dropped -->

## Decisions
- 2026-09-17, owner: "cloud-based, using the existing OpenRouter key".
"""


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


class TempBoardTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        self.root = self.repo / "issues"
        self.root.mkdir()
        self.board = B.Board(self.root, self.repo)

    def tearDown(self):
        self.tmp.cleanup()

    def card(self, rel: str = "features/2026-09-17-voice.md", text: str = CARD) -> B.Card:
        return B.Card.load(write(self.root / rel, text))


# --------------------------------------------------------------------------- ids

class IdTests(unittest.TestCase):
    def test_new_id_shape(self):
        for _ in range(200):
            value = B.new_id()
            self.assertEqual(len(value), 4)
            self.assertTrue(B.valid_id(value))
            self.assertTrue(any(c.isalpha() for c in value), value)
            self.assertFalse(set(value) & set("ILOU"))

    def test_all_digit_and_lowercase_ids_are_invalid(self):
        self.assertFalse(B.valid_id("1234"))
        self.assertFalse(B.valid_id("k7q2"))
        self.assertFalse(B.valid_id("K7Q"))
        self.assertFalse(B.valid_id("K7QI"))  # I is not in Crockford base32
        self.assertTrue(B.valid_id("K7Q2"))

    def test_new_id_avoids_taken(self):
        taken = {B.new_id() for _ in range(50)}
        self.assertNotIn(B.new_id(taken), taken)

    def test_derived_id_is_deterministic_and_avoids_collisions(self):
        first = B.derived_id("features/a.md")
        self.assertEqual(first, B.derived_id("features/a.md"))
        self.assertTrue(B.valid_id(first))
        self.assertNotEqual(first, B.derived_id("features/a.md", {first}))

    def test_item_ids(self):
        ids = {B.new_item_id() for _ in range(100)}
        for value in ids:
            self.assertEqual(len(value), 2)
            self.assertTrue(B.valid_item_id(value))
        self.assertFalse(B.valid_item_id("12"))
        self.assertFalse(B.valid_item_id("A3"))


# ------------------------------------------------------------------------- ranks

class RankTests(unittest.TestCase):
    def test_between_is_strictly_ordered(self):
        low, high = "0m", "18"
        mid = B.rank_between(low, high)
        self.assertLess(low, mid)
        self.assertLess(mid, high)

    def test_append_and_prepend(self):
        first = B.rank_between(None, None)
        after = B.rank_between(first, None)
        before = B.rank_between(None, first)
        self.assertLess(first, after)
        self.assertLess(before, first)
        self.assertTrue(B.valid_rank(after) and B.valid_rank(before))

    def test_repeated_inserts_stay_between(self):
        low, high = "a", "b"
        for _ in range(30):
            mid = B.rank_between(low, high)
            self.assertLess(low, mid)
            self.assertLess(mid, high)
            high = mid
        self.assertTrue(B.valid_rank(high))

    def test_ranks_never_end_in_zero(self):
        value = B.rank_between(None, "2")
        self.assertFalse(value.endswith("0"))
        self.assertLess(value, "2")
        self.assertLess(B.rank_between(None, value), value)

    def test_out_of_order_and_impossible_bounds(self):
        with self.assertRaises(B.BoardError):
            B.rank_between("b", "a")
        with self.assertRaises(B.BoardError):
            B.rank_between(None, "0")

    def test_initial_ranks_are_sorted_and_unique(self):
        for count in (1, 2, 5, 57, 200):
            ranks = B.initial_ranks(count)
            self.assertEqual(len(ranks), count)
            self.assertEqual(ranks, sorted(ranks))
            self.assertEqual(len(set(ranks)), count)
            self.assertTrue(all(B.valid_rank(r) for r in ranks))


# ------------------------------------------------------------- cards / round trip

class PriorityTests(TempBoardTest):
    """The row's priority flag (card #VKFV): −1…+3, clamped on every write, absent at 0."""

    def test_the_flag_clamps_into_range(self):
        self.assertEqual(B.clamp_priority(0), 0)
        self.assertEqual(B.clamp_priority(2), 2)
        self.assertEqual(B.clamp_priority(-1), -1)
        self.assertEqual(B.clamp_priority(9), 3)
        self.assertEqual(B.clamp_priority(-9), -1)
        self.assertEqual(B.clamp_priority("+2"), 2)

    def test_a_bad_flag_is_refused(self):
        for value in ("high", None, True, ""):
            with self.assertRaises(B.BoardError):
                B.clamp_priority(value)

    def test_the_card_reads_its_flag_and_survives_a_broken_one(self):
        card = self.card()
        card.set("priority", 2)
        self.assertEqual(B.Card.parse(card.to_text()).priority, 2)
        broken = B.Card.parse(card.to_text().replace("priority: 2", 'priority: "soon"'))
        self.assertEqual(broken.priority, 0)


class CardTests(TempBoardTest):
    def test_round_trip_is_byte_identical(self):
        card = B.Card.parse(CARD)
        self.assertEqual(card.to_text(), CARD)

    def test_round_trip_keeps_unusual_front_matter_bytes(self):
        text = CARD.replace("status: in-progress", 'status: "in-progress"   # kept verbatim')
        card = B.Card.parse(text)
        self.assertEqual(card.status, "in-progress")
        self.assertEqual(card.to_text(), text)

    def test_editing_a_field_emits_canonical_front_matter(self):
        card = B.Card.parse(CARD)
        card.set("status", "needs-qa-llm")
        self.assertIn("status: needs-qa-llm\n", card.to_text())
        self.assertEqual(B.Card.parse(card.to_text()).front, card.front)
        self.assertTrue(card.to_text().endswith(card.body))

    def test_fields_and_title(self):
        card = B.Card.parse(CARD)
        self.assertEqual(card.id, "K7Q2")
        self.assertEqual(card.type, "work")
        self.assertEqual(card.title, "Voice transcription mode")
        self.assertEqual(card.front["labels"], ["voice", "mvp"])
        self.assertEqual(card.front["links"]["related"], ["M3XJ"])
        self.assertEqual(card.front["links"]["github"], None)
        self.assertEqual(card.front["created"], "2026-09-17")

    def test_yaml_scalars_that_would_otherwise_change_type(self):
        for value in ("2026-09-17", "12:30", "true", "null", "0x1f", "1234", "- dash",
                      "colon: here", "", "#hash"):
            text = B.dump_front_matter({"source": value})
            self.assertEqual(B.parse_yaml(text)["source"], value, text)

    def test_flow_values_may_wrap_across_lines(self):
        parsed = B.parse_yaml("links: {plans: [a,\n  b], github: null}\nlabels: [x]\n")
        self.assertEqual(parsed["links"], {"plans": ["a", "b"], "github": None})
        self.assertEqual(parsed["labels"], ["x"])

    def test_block_sequences_are_read(self):
        parsed = B.parse_yaml("labels:\n  - voice\n  - mvp\nid: K7Q2\n")
        self.assertEqual(parsed["labels"], ["voice", "mvp"])
        self.assertEqual(parsed["id"], "K7Q2")

    def test_atomic_hash_checked_save(self):
        card = self.card()
        digest = B.file_hash(card.path)
        card.set("status", "ready")
        self.board.save(card, base_hash=digest)
        self.assertIn("status: ready", card.path.read_text())
        card.set("status", "done")
        with self.assertRaises(B.BoardConflict):
            self.board.save(card, base_hash=digest)

    def test_save_leaves_no_temporary_files(self):
        card = self.card()
        card.set("labels", ["voice"])
        self.board.save(card)
        self.assertEqual([p.name for p in card.path.parent.iterdir()], [card.path.name])


# -------------------------------------------------------------------- task items

class TaskTests(TempBoardTest):
    def test_parse_markers(self):
        items = B.Card.parse(CARD).tasks()
        self.assertEqual([i.item_id for i in items], ["b7", "a3", "c1", "d9", "e2"])
        self.assertEqual([i.status for i in items],
                         ["done", "in-progress", "open", "open", "dropped"])
        self.assertEqual([i.depth for i in items], [0, 0, 1, 0, 0])
        self.assertEqual(items[2].blocked_by, ["a3"])
        self.assertEqual(items[3].card, "M3XJ")
        self.assertEqual(items[4].text, "Local whisper.cpp fallback")

    def test_rewriting_tasks_keeps_every_other_byte(self):
        card = B.Card.parse(CARD)
        items = card.tasks()
        card.write_tasks(items)
        self.assertEqual(card.to_text(), CARD)

    def test_status_change_rewrites_one_line_only(self):
        card = B.Card.parse(CARD)
        items = card.tasks()
        items[1].status = "done"
        card.write_tasks(items)
        before, after = CARD.splitlines(), card.to_text().splitlines()
        self.assertEqual(len(before), len(after))
        self.assertEqual(sum(1 for a, b in zip(before, after) if a != b), 1)
        self.assertIn("- [x] Record audio with QAudioSource <!-- t:a3 -->", card.body)

    def test_github_toggled_box_without_a_marker(self):
        body = "# T\n\n## Tasks\n- [x] Ticked in the GitHub web UI\n- [ ] Added by hand\n"
        card = B.Card(front={"id": "K7Q2", "type": "work", "status": "ready"}, body=body, dirty=True)
        items = card.tasks()
        self.assertEqual([i.status for i in items], ["done", "open"])
        self.assertTrue(all(i.missing_marker for i in items))
        self.assertEqual(card.normalize_tasks(), 2)
        again = card.tasks()
        self.assertTrue(all(B.valid_item_id(i.item_id) for i in again))
        self.assertFalse(any(i.missing_marker for i in again))
        self.assertEqual([i.status for i in again], ["done", "open"])
        self.assertIn("- [x] Ticked in the GitHub web UI <!-- t:", card.body)
        # the marker is now stable: a second normalize changes nothing
        text = card.body
        self.assertEqual(card.normalize_tasks(), 0)
        self.assertEqual(card.body, text)

    def test_checkbox_beats_a_stale_marker(self):
        body = "# T\n\n## Tasks\n- [x] Done in GitHub <!-- t:a3 s=in-progress -->\n"
        card = B.Card(front={"id": "K7Q2"}, body=body, dirty=True)
        item = card.tasks()[0]
        self.assertEqual(item.status, "done")
        self.assertTrue(item.box_wins)
        card.normalize_tasks()
        self.assertIn("- [x] Done in GitHub <!-- t:a3 -->", card.body)

    def test_unticked_box_reopens_a_done_marker(self):
        body = "# T\n\n## Tasks\n- [ ] Reopened in GitHub <!-- t:a3 s=done -->\n"
        card = B.Card(front={"id": "K7Q2"}, body=body, dirty=True)
        self.assertEqual(card.tasks()[0].status, "open")

    def test_add_task_appends_after_the_last_item(self):
        card = B.Card.parse(CARD)
        item = card.add_task("Write the QA checklist")
        self.assertEqual([i.item_id for i in card.tasks()][-1], item.item_id)
        self.assertIn("## Decisions", card.body)
        self.assertTrue(card.body.index("Write the QA checklist") < card.body.index("## Decisions"))

    def test_tasks_section_is_created_when_missing(self):
        card = B.Card(front={"id": "K7Q2"}, body="# T\n\n## Request\nhi\n", dirty=True)
        card.add_task("First step")
        self.assertIn("## Tasks", card.body)
        self.assertEqual(len(card.tasks()), 1)

    def test_unknown_marker_status_is_rejected(self):
        card = B.Card(front={"id": "K7Q2"},
                      body="# T\n\n## Tasks\n- [ ] x <!-- t:a3 s=sideways -->\n", dirty=True)
        with self.assertRaises(B.BoardError):
            card.tasks()


# ------------------------------------------------------------------------ threads

class ThreadTests(TempBoardTest):
    def test_append_and_read(self):
        when = datetime(2026, 9, 17, 14, 12, 3, tzinfo=timezone.utc)
        first = self.board.append_thread("K7Q2", "where should transcription run?",
                                         author="owner", when=when)
        second = self.board.append_thread("K7Q2", "1. Local whisper.cpp, or cloud?",
                                          author="agent", kind="question", model="kimi-k3",
                                          when=datetime(2026, 9, 17, 14, 12, 40, tzinfo=timezone.utc))
        self.assertTrue(first.entry_id.startswith("20260917T141203Z-"))
        self.assertLess(first.entry_id, second.entry_id)
        entries = self.board.thread("K7Q2")
        self.assertEqual([e.author for e in entries], ["owner", "agent"])
        self.assertEqual(entries[1].attrs["model"], "kimi-k3")
        self.assertEqual(entries[0].text, "where should transcription run?")
        self.assertTrue(self.board.thread_path("K7Q2").read_text().startswith("<!-- relay:entry "))

    def test_entries_are_separated_by_a_blank_line(self):
        for i in range(3):
            self.board.append_thread("K7Q2", f"entry {i}")
        text = self.board.thread_path("K7Q2").read_text()
        self.assertNotIn("\n\n\n", text)
        self.assertEqual(len(B.parse_thread(text)), 3)

    def test_multiline_and_quoted_attributes(self):
        self.board.append_thread("K7Q2", "line one\nline two", author="Elliott Ash", kind="decision")
        entry = self.board.thread("K7Q2")[0]
        self.assertEqual(entry.text, "line one\nline two")
        self.assertEqual(entry.author, "Elliott Ash")

    def test_unknown_kind_is_rejected(self):
        with self.assertRaises(B.BoardError):
            self.board.append_thread("K7Q2", "x", kind="gossip")

    def test_an_append_replaces_the_file_so_a_directory_watch_sees_it(self):
        # #N5JJ: the Board pane holds a QFileSystemWatcher on the board's *directories*,
        # and a directory watch fires when an entry is created, renamed or removed — not when an
        # existing file grows. The append was `O_APPEND` until 2026-09-20, so about two of every
        # three thread writes never reached the pane. It writes a temporary file and renames it
        # in now, which is a directory change, and the inode is what proves it.
        self.board.append_thread("K7Q2", "first")
        path = self.board.thread_path("K7Q2")
        before = path.stat().st_ino
        self.board.append_thread("K7Q2", "second")
        self.assertNotEqual(path.stat().st_ino, before)
        self.assertEqual([e.text for e in self.board.thread("K7Q2")], ["first", "second"])

    def test_concurrent_appends_keep_every_entry(self):
        # The lock moved from the file to the threads directory with that change: `os.replace`
        # gives the path a new inode, so a lock on the old one would stop excluding anybody.
        import threading as T
        errors: list[BaseException] = []

        def write(n: int) -> None:
            try:
                for i in range(6):
                    self.board.append_thread("K7Q2", f"writer {n} entry {i}")
            except BaseException as exc:                     # pragma: no cover - a real failure
                errors.append(exc)

        threads = [T.Thread(target=write, args=(n,)) for n in range(4)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(errors, [])
        entries = self.board.thread("K7Q2")
        self.assertEqual(len(entries), 24)
        self.assertEqual(len({e.entry_id for e in entries}), 24)

    def test_private_threads_live_under_the_private_root(self):
        self.board.append_thread("K7Q2", "secret", private=True)
        self.assertTrue((self.root / ".private" / "threads" / "K7Q2.md").exists())
        self.assertFalse((self.root / "threads" / "K7Q2.md").exists())

    @unittest.skipUnless(shutil.which("git"), "git is required")
    def test_union_merge_keeps_both_sides_entries(self):
        base = self.board.thread_path("K7Q2")
        self.board.append_thread("K7Q2", "shared entry",
                                 when=datetime(2026, 9, 17, 10, 0, 0, tzinfo=timezone.utc))
        work = self.root / "merge"
        work.mkdir()
        shutil.copy(base, work / "base.md")
        shutil.copy(base, work / "ours.md")
        shutil.copy(base, work / "theirs.md")
        for name, text, when in (("ours.md", "mine", datetime(2026, 9, 17, 11, 0, 0, tzinfo=timezone.utc)),
                                 ("theirs.md", "yours", datetime(2026, 9, 17, 12, 0, 0, tzinfo=timezone.utc))):
            side = B.Board(work.parent, self.repo)
            entry = B.ThreadEntry(B.new_entry_id(when), {"author": "owner", "kind": "comment"}, text)
            with open(work / name, "a", encoding="utf-8") as out:
                out.write("\n" + entry.render())
            del side
        merged = subprocess.run(["git", "merge-file", "--union", "-p",
                                 str(work / "ours.md"), str(work / "base.md"), str(work / "theirs.md")],
                                capture_output=True, text=True)
        self.assertEqual(merged.returncode, 0, merged.stderr)
        entries = B.parse_thread(merged.stdout)
        self.assertEqual([e.text for e in entries], ["shared entry", "mine", "yours"])
        self.assertNotIn("<<<<<<<", merged.stdout)
        ids = [e.entry_id for e in entries]
        self.assertEqual(ids, sorted(ids))


# --------------------------------------------------------- memory, aliases, types

class IssueSectionTests(TempBoardTest):
    """The section holding the user's own words (owner, 2026-09-18: call it Issue, not Request)."""

    def test_a_new_card_says_issue(self):
        card = B.new_card("work", "Voice mode", "inbox", card_id="K7Q2",
                          request="add voice transcribe mode")
        self.assertIn("## Issue\nadd voice transcribe mode", card.body)
        self.assertNotIn("## Request", card.body)

    def test_an_older_card_keeps_its_request_heading_and_is_still_read(self):
        # Nothing rewrites the cards that are already filed; they are read as they are.
        body = "# Voice mode\n\n## Request\nadd voice transcribe mode\n"
        card = B.Card(front={"id": "K7Q2", "type": "work", "status": "inbox"}, body=body)
        self.assertEqual(card.body, body)
        self.assertEqual(B.ISSUE_HEADING, "Issue")
        self.assertIn("request", B.ISSUE_HEADINGS)


class CardTypeTests(TempBoardTest):
    def test_there_is_no_plan_card_type(self):
        # Card #X7NB, owner 2026-09-20: a plan is the `## Plan` section of the work card it
        # plans (and plan mode's own files under `.relay/plans`), never a card of its own.
        self.assertNotIn("plan", B.CARD_TYPES)
        write(self.root / "planning" / "2026-09-17-voice-mode.md",
              "---\nid: M3XJ\ntype: plan\nstatus: draft\n---\n# Voice mode plan\n")
        problems = self.board.check()
        self.assertEqual([p.code for p in problems], ["unknown_type"])
        self.assertIn("type 'plan' is not one of", problems[0].message)

    def test_the_planning_folder_is_a_tab_of_work_cards(self):
        # Only the plan *type*'s claim on `planning/` went with #X7NB: the folder is one of
        # the board's ordinary category tabs, and `planning` is also a work card's stage.
        card = B.new_card("work", "Voice mode", "planning", card_id="M3XK")
        write(self.root / "planning" / "2026-09-17-voice-mode.md", card.to_text())
        self.assertEqual(self.board.check(), [])
        loaded = self.board.card_by_id("M3XK")
        self.assertEqual(loaded.type, "work")
        self.assertEqual(loaded.expected_folder("planning"), "planning")
        self.assertEqual(B.tab_of(self.board, loaded), "planning")

    def test_memory_card_one_fact_per_file(self):
        card = B.new_card("memory", "Run GUI checks under Xvfb", "active", card_id="P4QT",
                          name="gui-tests-need-xvfb", kind="convention", topic="environment",
                          scope="project",
                          description="GUI checks must run under Xvfb with an isolated XDG_CONFIG_HOME",
                          paths=["src/**"], pinned=False)
        write(self.root / "memory" / "gui-tests-need-xvfb.md", card.to_text())
        self.assertEqual(self.board.check(), [])
        loaded = self.board.card_by_id("P4QT")
        self.assertEqual(loaded.type, "memory")
        self.assertEqual(loaded.front["kind"], "convention")
        self.assertEqual(loaded.expected_folder(), "memory")

    def test_retired_memory_moves_to_archive(self):
        card = B.new_card("memory", "Old fact", "retired", card_id="P4QV", name="old-fact")
        write(self.root / "memory" / "old-fact.md", card.to_text())
        self.assertIn("folder_status_mismatch", {p.code for p in self.board.check()})
        (self.root / "memory" / "old-fact.md").unlink()
        write(self.root / "memory" / "archive" / "old-fact.md", card.to_text())
        self.assertEqual(self.board.check(), [])

    def test_memory_fields_are_not_allowed_on_work_cards(self):
        card = B.new_card("work", "A feature", "ready", card_id="P4QW", topic="environment")
        write(self.root / "features" / "2026-09-17-a.md", card.to_text())
        problem = next(p for p in self.board.check() if p.code == "unknown_field")
        self.assertIn("topic", problem.message)

    def test_unknown_card_type(self):
        write(self.root / "features" / "2026-09-17-a.md",
              "---\nid: P4QX\ntype: sticker\nstatus: ready\n---\n# A\n")
        self.assertIn("unknown_type", {p.code for p in self.board.check()})


class VerifyBlockTests(TempBoardTest):
    """The QA ladder's `verify` block (#WFRA): vocabulary, refusals, round trip, check, index."""

    GOOD = {"artifact": "code", "primary": "script", "also": ["ai-text", "script"],
            "human": "required", "criteria": "the refusal reads as one sentence", "effort": "medium"}

    def test_a_valid_block_is_normalized(self):
        block = B.validate_verify(dict(self.GOOD, primary=" Script ", stakes="rework"))
        self.assertEqual(block, {"artifact": "code", "primary": "script", "also": ["ai-text"],
                                 "human": "required", "criteria": "the refusal reads as one sentence",
                                 "sign_off": "none", "effort": "medium", "stakes": "rework"})
        self.assertEqual(list(block), [k for k in B.VERIFY_KEYS if k in block])
        # `also` may be written as one word; `human` and `sign_off` default to none.
        one = B.validate_verify({"artifact": "text", "primary": "ai-text", "also": "pairwise", "effort": "low"})
        self.assertEqual(one["also"], ["pairwise"])
        self.assertEqual((one["human"], one["sign_off"]), ("none", "none"))
        self.assertNotIn("deferred", one)

    def test_refusals_name_the_bad_key_or_value(self):
        cases = [
            (dict(self.GOOD, primary="vibes"), "verify.primary 'vibes' is not one of script|probe"),
            (dict(self.GOOD, also=["ai-text", "guess"]), "verify.also 'guess' is not one of"),
            (dict(self.GOOD, colour="red"), "verify has no key 'colour'; the keys are artifact, primary"),
            ({"artifact": "code", "primary": "script"}, "verify is missing 'effort'"),
            ({"primary": "script", "effort": "low"}, "verify is missing 'artifact'"),
            (dict(self.GOOD, human="required", criteria=None), "verify.criteria is required when verify.human is 'required'"),
            (dict(self.GOOD, effort=["low"]), "verify.effort must be one word, not ['low']"),
            (dict(self.GOOD, criteria=["a", "b"]), "verify.criteria must be one line of text"),
            (dict(self.GOOD, sign_off="boss"), "verify.sign_off 'boss' is not one of none|money|publish"),
            (dict(self.GOOD, stakes="high"), "verify.stakes 'high' is not one of nuisance|rework"),
            (dict(self.GOOD, blast="everything"), "verify.blast 'everything' is not one of case|capability"),
            ("script", "verify must be a mapping of artifact, primary"),
        ]
        for value, message in cases:
            with self.assertRaises(B.BoardError, msg=str(value)) as caught:
                B.validate_verify(value)
            self.assertIn(message, str(caught.exception))

    def test_the_block_round_trips_through_the_front_matter(self):
        card = B.Card.parse("---\nid: K7Q2\ntype: work\nstatus: executing\nrank: 0i\n"
                            "verify:\n  artifact: visual\n  primary: probe\n  also: [ai-visual, pairwise]\n"
                            "  human: required\n  criteria: the strip reads in one line\n  effort: medium\n"
                            "---\n# Strip\n")
        block = B.verify_block(card)
        self.assertEqual(block["also"], ["ai-visual", "pairwise"])
        card.set("verify", block)
        again = B.Card.parse(card.to_text())
        self.assertEqual(B.verify_block(again), block)
        self.assertIn("verify: {artifact: visual, primary: probe, also: [ai-visual, pairwise], "
                      "human: required, criteria: the strip reads in one line, sign_off: none, "
                      "effort: medium}", card.to_text())
        self.assertIn("verify", B.ALLOWED_FIELDS["work"])
        self.assertNotIn("verify", B.ALLOWED_FIELDS["memory"])
        self.assertIsNone(B.verify_block(B.new_card("work", "None yet", "ready", card_id="M3XJ")))

    def test_check_errors_on_an_invalid_block_and_warns_when_a_working_card_has_none(self):
        bad = B.new_card("work", "Bad block", "executing", card_id="K7Q2", rank="0m",
                         verify={"artifact": "code", "primary": "vibes", "effort": "low"})
        write(self.root / "features" / "2026-09-23-a.md", bad.to_text())
        problems = {p.code: p for p in self.board.check()}
        self.assertEqual(problems["bad_verify"].severity, "error")
        self.assertIn("verify.primary 'vibes'", problems["bad_verify"].message)
        (self.root / "features" / "2026-09-23-a.md").unlink()
        for status, expected in (("executing", True), ("needs-verification", True),
                                 ("needs-qa-human", True), ("planned", False), ("done", False)):
            card = B.new_card("work", "No block", status, card_id="M3XJ", rank="0n")
            folder = self.root / "features" / B.WORK_STATUS_FOLDER[status]
            path = write(folder / "2026-09-23-b.md", card.to_text())
            codes = {p.code for p in self.board.check()}
            self.assertEqual("missing_verify" in codes, expected, status)
            path.unlink()
        good = B.new_card("work", "Has block", "executing", card_id="P4QT", rank="0o",
                          verify=self.GOOD)
        write(self.root / "features" / "2026-09-23-c.md", good.to_text())
        self.assertEqual([p.code for p in self.board.check() if p.code.endswith("verify")], [])

    def test_the_index_shows_the_primary_mode_and_the_person_flag(self):
        write(self.root / "features" / "2026-09-23-a.md",
              B.new_card("work", "Person looks", "executing", card_id="K7Q2", rank="0m",
                         verify=dict(self.GOOD, primary="probe")).to_text())
        write(self.root / "features" / "2026-09-23-b.md",
              B.new_card("work", "Optional look", "executing", card_id="M3XJ", rank="0n",
                         verify=dict(self.GOOD, primary="ai-text", human="optional")).to_text())
        write(self.root / "features" / "2026-09-23-c.md",
              B.new_card("work", "Deferred", "executing", card_id="P4QT", rank="0o",
                         verify={"artifact": "system", "primary": "world", "effort": "high",
                                 "deferred": "until the pilot runs (owner: Sam)"}).to_text())
        write(self.root / "features" / "2026-09-23-d.md",
              B.new_card("work", "No block", "ready", card_id="Z9QT", rank="0p").to_text())
        text = self.board.index_markdown()
        self.assertIn("| Card | Title | Status | Verify | Assignee | Tasks | Thread |", text)
        self.assertIn("| executing | probe · person |", text)
        self.assertIn("| executing | ai-text · person? |", text)
        self.assertIn("| executing | unverified until the pilot runs (owner: Sam) |", text)
        self.assertIn("| ready |  |", text)
        self.assertEqual(B.verify_summary(None), "")
        self.assertEqual(B.deferred_text({"deferred": "Until Monday"}), "Monday")


class VerifiedTests(TempBoardTest):
    """`verified(card)` and its parts (#1AA6): evidence, the person's answer, the receipt, deferred."""

    PASSING = ("## Tests\n- `ctest -R x`\n\n### Check 2026-09-23 10:00\n"
               "- passed · ctest:x — passed for this revision\n"
               "- not-applicable · manual — not this card's\nhistory: thread\n")
    FAILING = ("## Tests\n- `ctest -R x`\n\n### Check 2026-09-23 10:00\n"
               "- passed · ctest:x — passed\n- missing-evidence · unittest:y — no run\nhistory: thread\n")

    def card(self, body="", **verify):
        card = B.new_card("work", "Thing", "needs-verification", card_id="K7Q2", rank="0m",
                          **({"verify": {"artifact": "code", "primary": "script", "effort": "low", **verify}}
                             if verify else {}))
        card.body += "\n" + body
        return card

    def test_human_qa_questions_and_their_answers(self):
        body = ("## Human QA\n1. Is the threshold right?\n    Answer: yes, leave it.\n"
                "2. Does the wording read right?\n")
        self.assertEqual(B.human_qa_questions(body),
                         [("1. Is the threshold right?", True), ("2. Does the wording read right?", False)])
        self.assertEqual(B.human_qa_questions("## Human QA\nprose only\n"), [])

    def test_a_check_block_passes_only_when_every_listed_test_passed(self):
        self.assertTrue(B.check_passing(self.PASSING))
        self.assertFalse(B.check_passing(self.FAILING))
        self.assertFalse(B.check_passing("## Tests\n- `ctest -R x`\n"))
        self.assertFalse(B.check_passing("## Tests\n### Check 2026-09-23 10:00\n- no findings\nhistory: thread\n"))
        # The current block is the last one; an older failing block above it is history.
        self.assertTrue(B.check_passing(self.FAILING + "\n### Check 2026-09-23 11:00\n- passed · ctest:x — ok\n"))

    def test_a_receipt_lives_in_the_verdict_or_the_execution_summary(self):
        self.assertTrue(B.has_receipt("## Verdict\npass\nReceipt: owner clicked Publish, 2026-09-23 10:12\n"))
        self.assertTrue(B.has_receipt("## Execution Summary\n- Receipt: transfer #4411 confirmed by Sam\n"))
        self.assertFalse(B.has_receipt("## Issue\nReceipt: not here\n"))

    def test_verified_needs_the_primary_evidence(self):
        self.assertEqual(B.unverified_reasons(self.card()),
                         ["no primary evidence: no `## Verdict` and no passing `### Check` under `## Tests`"])
        self.assertTrue(B.verified(self.card("## Verdict\npass\n")))
        self.assertTrue(B.verified(self.card(self.PASSING)))
        self.assertFalse(B.verified(self.card(self.FAILING)))
        self.assertTrue(B.verified(self.card("## Verdict\npass\n", human="none")))

    def test_verified_needs_the_persons_answer_when_required(self):
        base = "## Verdict\npass\n"
        card = self.card(base, human="required", criteria="reads right")
        reasons = B.unverified_reasons(card)
        self.assertEqual(len(reasons), 1)
        self.assertIn("the person's answer is missing", reasons[0])
        self.assertIn("reads right", reasons[0])
        card = self.card(base + "## Human QA\n1. Reads right?\n", human="required", criteria="reads right")
        self.assertIn("has no `Answer:` under '1. Reads right?'", B.unverified_reasons(card)[0])
        card = self.card(base + "## Human QA\n1. Reads right?\n    Answer: yes\n", human="required", criteria="reads right")
        self.assertTrue(B.verified(card))
        self.assertTrue(B.verified(self.card(base, human="optional", criteria="reads right")))

    def test_verified_needs_the_receipt_when_a_sign_off_is_required(self):
        card = self.card("## Verdict\npass\n", sign_off="publish")
        self.assertEqual(B.unverified_reasons(card),
                         ["no `Receipt:` line in `## Verdict` or `## Execution Summary` (verify.sign_off: publish)"])
        self.assertTrue(B.verified(self.card("## Verdict\npass\nReceipt: published by Sam\n", sign_off="publish")))

    def test_a_deferred_card_is_not_verified_and_says_until_when(self):
        card = self.card("## Verdict\npass\n", deferred="until the pilot runs (owner: Sam)")
        self.assertEqual(B.unverified_reasons(card),
                         ["unverified until the pilot runs (owner: Sam) (verify.deferred)"])
        card.set("verify", {"artifact": "code", "primary": "vibes", "effort": "low"})
        self.assertIn("the verify block is invalid", B.unverified_reasons(card)[0])

    def test_check_names_a_done_card_that_is_not_verified(self):
        card = self.card("## Verdict\npass\n", human="required", criteria="reads right")
        card.set("status", "done")
        write(self.root / "features" / "done" / "2026-09-23-a.md", card.to_text())
        problem = next(p for p in self.board.check() if p.code == "not_verified")
        self.assertEqual(problem.severity, "warning")
        self.assertIn("done but not verified: the person's answer is missing", problem.message)
        card.body += "## Human QA\n1. Reads right?\n    Answer: yes\n"
        write(self.root / "features" / "done" / "2026-09-23-a.md", card.to_text())
        self.assertEqual([p.code for p in self.board.check() if p.code == "not_verified"], [])


class PrivateTests(TempBoardTest):
    def test_private_card_under_the_private_root(self):
        card = B.new_card("work", "Private thing", "ready", card_id="Z9QT", private=True)
        write(self.root / ".private" / "features" / "2026-09-17-private.md", card.to_text())
        self.assertEqual(self.board.check(), [])
        self.assertEqual([c.id for c in self.board.cards()], ["Z9QT"])
        self.assertEqual([c.id for c in self.board.cards(include_private=False)], [])

    def test_private_flag_must_match_the_path(self):
        card = B.new_card("work", "Not really private", "ready", card_id="Z9QV", private=True)
        write(self.root / "features" / "2026-09-17-a.md", card.to_text())
        self.assertIn("private_flag_mismatch", {p.code for p in self.board.check()})

    def test_index_excludes_private_cards_by_default(self):
        write(self.root / ".private" / "features" / "2026-09-17-p.md",
              B.new_card("work", "Secret", "ready", card_id="Z9QW", private=True).to_text())
        write(self.root / "features" / "2026-09-17-a.md",
              B.new_card("work", "Public", "ready", card_id="Z9QX").to_text())
        self.assertNotIn("Secret", self.board.index_markdown())
        self.assertIn("Public", self.board.index_markdown())
        self.assertIn("Secret", self.board.index_markdown(include_private=True))

    @unittest.skipUnless(shutil.which("git"), "git is required")
    def test_check_flags_private_files_tracked_by_git(self):
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True, capture_output=True)
        write(self.root / ".private" / "features" / "2026-09-17-p.md",
              B.new_card("work", "Secret", "ready", card_id="Z9QY", private=True).to_text())
        self.assertNotIn("private_tracked", {p.code for p in self.board.check()})
        subprocess.run(["git", "-C", str(self.repo), "add", "-f", "issues/.private"],
                       check=True, capture_output=True)
        problem = next(p for p in self.board.check() if p.code == "private_tracked")
        self.assertIn(".private", problem.message)


# -------------------------------------------------------------------- check rules

class CheckTests(TempBoardTest):
    def good(self, card_id="K7Q2", status="ready", rel=None, **fields):
        card = B.new_card("work", f"Card {card_id}", status, card_id=card_id, **fields)
        rel = rel or f"features/{B.WORK_STATUS_FOLDER[status] + '/' if B.WORK_STATUS_FOLDER[status] else ''}2026-09-17-{card_id.lower()}.md"
        return write(self.root / rel, card.to_text())

    def test_clean_board_has_no_problems(self):
        self.good("K7Q2")
        self.good("M3XJ", "needs-qa-llm")
        self.assertEqual([p for p in self.board.check() if p.code != "missing_verify"], [])

    def test_folder_and_status_must_agree(self):
        self.good("K7Q2", "needs-qa-llm", rel="features/2026-09-17-k7q2.md")
        problem = next(p for p in self.board.check() if p.code == "folder_status_mismatch")
        self.assertIn("needs_qa_llm", problem.message)

    def test_duplicate_ids(self):
        self.good("K7Q2")
        self.good("K7Q2", rel="features/2026-09-17-other.md")
        self.assertIn("duplicate_id", {p.code for p in self.board.check()})

    def test_unknown_field_and_bad_status(self):
        # `priority` was this test's unknown field until it became a real one (card #VKFV).
        self.good("K7Q2", urgency="high")
        write(self.root / "features" / "2026-09-17-b.md",
              "---\nid: M3XJ\ntype: work\nstatus: whenever\nrank: 0i\n---\n# B\n")
        codes = {p.code for p in self.board.check()}
        self.assertIn("unknown_field", codes)
        self.assertIn("bad_status", codes)

    def test_missing_and_malformed_ids(self):
        write(self.root / "features" / "2026-09-17-a.md",
              "---\ntype: work\nstatus: ready\nrank: 0i\n---\n# A\n")
        write(self.root / "features" / "2026-09-17-b.md",
              "---\nid: 1234\ntype: work\nstatus: ready\nrank: 0i\n---\n# B\n")
        codes = {p.code for p in self.board.check()}
        self.assertIn("missing_id", codes)
        self.assertIn("bad_id", codes)

    def test_no_front_matter_and_missing_title(self):
        write(self.root / "features" / "2026-09-17-a.md", "# Just a note\n")
        write(self.root / "features" / "2026-09-17-b.md",
              "---\nid: M3XJ\ntype: work\nstatus: ready\nrank: 0i\n---\nno heading\n")
        codes = {p.code for p in self.board.check()}
        self.assertIn("no_front_matter", codes)
        self.assertIn("missing_title", codes)

    def test_merge_markers(self):
        card = B.new_card("work", "A", "ready", card_id="K7Q2")
        write(self.root / "features" / "2026-09-17-a.md",
              card.to_text() + "\n<<<<<<< HEAD\nours\n=======\ntheirs\n>>>>>>> branch\n")
        self.assertIn("merge_markers", {p.code for p in self.board.check()})

    def test_task_marker_rules(self):
        card = B.new_card("work", "A", "ready", card_id="K7Q2")
        card.body += ("\n## Tasks\n- [ ] one <!-- t:a3 -->\n- [ ] two <!-- t:a3 -->\n"
                      "- [ ] three <!-- t:b7 blocked_by=zz -->\n"
                      "      - [ ] too deep <!-- t:c1 -->\n")
        write(self.root / "features" / "2026-09-17-a.md", card.to_text())
        codes = {p.code for p in self.board.check()}
        self.assertIn("duplicate_task_id", codes)
        self.assertIn("unknown_blocker", codes)
        self.assertIn("task_too_deep", codes)

    def test_task_cycle(self):
        card = B.new_card("work", "A", "ready", card_id="K7Q2")
        card.body += ("\n## Tasks\n- [ ] one <!-- t:a3 blocked_by=b7 -->\n"
                      "- [ ] two <!-- t:b7 blocked_by=a3 -->\n")
        write(self.root / "features" / "2026-09-17-a.md", card.to_text())
        self.assertIn("task_cycle", {p.code for p in self.board.check()})

    def test_missing_markers_are_a_fixable_warning(self):
        card = B.new_card("work", "A", "ready", card_id="K7Q2")
        card.body += "\n## Tasks\n- [ ] typed by hand\n- [x] ticked in GitHub\n"
        path = write(self.root / "features" / "2026-09-17-a.md", card.to_text())
        problem = next(p for p in self.board.check() if p.code == "task_missing_marker")
        self.assertEqual(problem.severity, "warning")
        self.assertTrue(problem.fixable)
        self.assertEqual([p for p in self.board.check(fix=True) if p.code == "task_missing_marker"], [])
        self.assertIn("<!-- t:", path.read_text())
        self.assertEqual(self.board.check(), [])

    def test_a_heading_outside_the_schema_is_a_warning(self):
        # #Z4HR: one section per stage. The board predates the set by hundreds of cards, so an
        # invented heading warns (and is not fixable) rather than erroring.
        card = B.new_card("work", "A", "ready", card_id="K7Q2")
        card.body += "\n## Implementer check (not a QA verdict)\nlooked at it\n"
        write(self.root / "features" / "2026-09-17-a.md", card.to_text())
        problems = [p for p in self.board.check() if p.code == "unknown_section"]
        self.assertEqual(len(problems), 1)
        self.assertEqual(problems[0].severity, "warning")
        self.assertFalse(problems[0].fixable)
        self.assertIn("Implementer check", problems[0].message)

    def test_the_schema_sections_do_not_warn(self):
        card = B.new_card("work", "A", "ready", card_id="K7Q2")
        card.body += ("\n## Discussion points\n\n## Planning notes\n\n## Plan\n\n## Execution Summary\n\n"
                      "## Tests\n\n## QA checklist\n\n## Verdict\n\n## Resolution\n\n"
                      "## Merged in\n\n## Split\n\n"
                      # A parenthesized suffix still names its section.
                      "## Decisions (owner, 2026-09-20)\n")
        write(self.root / "features" / "2026-09-17-a.md", card.to_text())
        self.assertEqual(self.board.check(), [])

    def test_memory_and_alias_cards_are_not_checked_against_the_schema(self):
        memory = B.new_card("memory", "A fact", "active", card_id="P4QT", name="a-fact")
        memory.body += "\n## Context\nsome fact\n"
        write(self.root / "memory" / "a-fact.md", memory.to_text())
        alias = B.new_card("alias", "A command", "active", card_id="A1BC", name="a-command",
                           kind="command")
        alias.body += "\n## Run\n```bash\nmake\n```\n\n## Parameters\n- `target` = `all`\n"
        write(self.root / "aliases" / "a-command.md", alias.to_text())
        self.assertEqual([p for p in self.board.check() if p.code == "unknown_section"], [])

    def test_thread_entry_rules(self):
        self.good("K7Q2")
        thread = self.board.thread_path("K7Q2")
        thread.parent.mkdir(parents=True, exist_ok=True)
        thread.write_text("<!-- relay:entry 20260917T141240Z-b7 author=owner kind=comment -->\nsecond\n\n"
                          "<!-- relay:entry 20260917T141203Z-a1 author=owner kind=comment -->\nfirst\n\n"
                          "<!-- relay:entry 20260917T141203Z-a1 author=owner kind=comment -->\nfirst again\n")
        codes = {p.code for p in self.board.check()}
        self.assertIn("thread_unsorted", codes)
        self.assertIn("duplicate_thread_entry", codes)

    def test_thread_sorting_is_fixable(self):
        self.good("K7Q2")
        thread = self.board.thread_path("K7Q2")
        thread.parent.mkdir(parents=True, exist_ok=True)
        thread.write_text("<!-- relay:entry 20260917T141240Z-b7 author=owner kind=comment -->\nsecond\n\n"
                          "<!-- relay:entry 20260917T141203Z-a1 author=owner kind=comment -->\nfirst\n")
        self.board.check(fix=True)
        self.assertEqual([e.text for e in self.board.thread("K7Q2")], ["first", "second"])
        self.assertEqual(self.board.check(), [])

    def test_orphan_and_misnamed_threads(self):
        self.good("K7Q2")
        self.board.append_thread("M3XJ", "no such card")
        (self.root / "threads" / "notes.md").write_text(
            "<!-- relay:entry 20260917T141203Z-a1 author=owner kind=comment -->\nhi\n")
        codes = {p.code for p in self.board.check()}
        self.assertIn("orphan_thread", codes)
        self.assertIn("bad_thread_name", codes)


# ------------------------------------------------------------------------- index

class IndexTests(TempBoardTest):
    def test_index_lists_cards_by_tab_and_status(self):
        write(self.root / "features" / "2026-09-17-a.md",
              B.new_card("work", "A feature", "ready", card_id="K7Q2", rank="0m").to_text())
        write(self.root / "features" / "needs_qa_llm" / "2026-09-17-b.md",
              B.new_card("work", "Landed feature", "needs-qa-llm", card_id="M3XJ", rank="0n").to_text())
        write(self.root / "changes" / "2026-09-17-c.md",
              B.new_card("work", "A bug", "inbox", card_id="P4QT", rank="0o").to_text())
        self.board.append_thread("K7Q2", "hello")
        text = self.board.index_markdown()
        self.assertIn("## Features (2)", text)
        self.assertIn("## Bugs (1)", text)
        self.assertIn("`#K7Q2`", text)
        self.assertIn("(threads/K7Q2.md)", text)
        self.assertLess(text.index("A feature"), text.index("Landed feature"))

    def test_index_is_written_and_regenerated_identically(self):
        write(self.root / "features" / "2026-09-17-a.md",
              B.new_card("work", "A | pipe in the title", "ready", card_id="K7Q2").to_text())
        path = self.board.write_index()
        first = path.read_text()
        self.board.write_index()
        self.assertEqual(path.read_text(), first)
        self.assertIn(r"A \| pipe in the title", first)

    def test_index_shows_task_progress(self):
        card = B.new_card("work", "With tasks", "ready", card_id="K7Q2")
        card.body += "\n## Tasks\n- [x] one <!-- t:a3 -->\n- [ ] two <!-- t:b7 -->\n"
        write(self.root / "features" / "2026-09-17-a.md", card.to_text())
        self.assertIn("| 1/2 |", self.board.index_markdown())


# --------------------------------------------------------------------- migration

class LegacyParseTests(unittest.TestCase):
    def test_header_block_is_removed_and_the_body_kept(self):
        text = FIXTURES.joinpath("features/2026-09-17-voice-transcription.md").read_text()
        legacy = B.parse_legacy(text)
        self.assertEqual(legacy.title, "Voice transcription mode (microphone button, hold Right Alt)")
        self.assertEqual(legacy.headers["status"], "open")
        self.assertEqual(legacy.headers["component"], "gui, worker")
        self.assertTrue(legacy.body.startswith("# Voice transcription mode"))
        self.assertNotIn("- **Status**:", legacy.body)
        for section in text.split("\n## ")[1:]:
            self.assertIn("## " + section.split("\n")[0], legacy.body)

    def test_wrapped_header_values_are_joined(self):
        text = FIXTURES.joinpath("features/2026-09-17-agent-delegate-and-take-over.md").read_text()
        legacy = B.parse_legacy(text)
        self.assertIn("the user takes over mid-session with a keystroke",
                      legacy.headers["acceptance"])
        self.assertNotIn("\n", legacy.headers["acceptance"])

    def test_body_bullets_that_look_like_headers_stay_in_the_body(self):
        text = FIXTURES.joinpath("features/2026-09-17-website-and-beta-release.md").read_text()
        legacy = B.parse_legacy(text)
        self.assertIn("- **Deploy**", legacy.body)
        self.assertEqual(legacy.headers["status"], "open")

    def test_files_without_a_header_block_are_rejected(self):
        with self.assertRaises(B.BoardError):
            B.parse_legacy("# Title\n\nJust prose.\n")
        with self.assertRaises(B.BoardError):
            B.parse_legacy("- **Status**: open\n")


class MigrationTests(TempBoardTest):
    def setUp(self):
        super().setUp()
        shutil.rmtree(self.root)
        shutil.copytree(FIXTURES, self.root)

    def test_dry_run_writes_nothing(self):
        before = {p: p.read_bytes() for p in self.root.rglob("*.md")}
        report = B.migrate(self.root, apply=False, repo=self.repo)
        self.assertEqual(len(report.converted), 6)
        self.assertEqual(report.skipped, [])
        self.assertEqual({p: p.read_bytes() for p in self.root.rglob("*.md")}, before)
        self.assertFalse((self.root / "board.yaml").exists())
        self.assertIn("issues/board.yaml", report.created)

    def test_migration_maps_statuses_and_keeps_bodies(self):
        originals = {p.relative_to(self.root): B.parse_legacy(p.read_text())
                     for p in sorted(self.root.rglob("*.md")) if p.name != "README.md"}
        report = B.migrate(self.root, apply=True, repo=self.repo)
        statuses = {m.path: m.status for m in report.converted}
        self.assertEqual(statuses["features/2026-09-17-voice-transcription.md"], "ready")
        self.assertEqual(statuses["features/needs_qa_llm/2026-09-17-shortcut-hints.md"], "needs-qa-llm")
        self.assertEqual(statuses["features/done/2026-09-17-terminal-first-agent-fallback.md"], "done")
        for rel, legacy in originals.items():
            card = B.Card.load(self.root / rel)
            self.assertEqual(card.body, legacy.body, rel)
            self.assertTrue(B.valid_id(card.id), rel)
            self.assertTrue(B.valid_rank(card.rank), rel)
            self.assertEqual(card.type, "work")
            self.assertEqual(card.front["created"], "2026-09-17")

    def test_migration_creates_the_scaffolding(self):
        B.migrate(self.root, apply=True, repo=self.repo)
        self.assertIn("version: 1", (self.root / "board.yaml").read_text())
        self.assertIn(".private/", (self.root / ".gitignore").read_text())
        self.assertIn("issues/threads/*.md merge=union", (self.repo / ".gitattributes").read_text())
        self.assertTrue((self.root / "threads").is_dir())
        self.assertIn("# Board", (self.root / "BOARD.md").read_text())

    def test_migration_appends_to_an_existing_gitattributes(self):
        (self.repo / ".gitattributes").write_text("*.png binary\n")
        B.migrate(self.root, apply=True, repo=self.repo)
        text = (self.repo / ".gitattributes").read_text()
        self.assertIn("*.png binary", text)
        self.assertIn("issues/threads/*.md merge=union", text)

    def test_migration_is_deterministic_and_idempotent(self):
        first = B.migrate(self.root, apply=True, repo=self.repo)
        bytes_after = {p.relative_to(self.root): p.read_bytes() for p in sorted(self.root.rglob("*.md"))}
        second = B.migrate(self.root, apply=True, repo=self.repo)
        self.assertEqual({p.relative_to(self.root): p.read_bytes()
                          for p in sorted(self.root.rglob("*.md"))}, bytes_after)
        self.assertEqual(second.converted, [])
        self.assertEqual(len(second.migrations), len(first.migrations))
        ids = [m.card_id for m in first.converted]
        self.assertEqual(len(set(ids)), len(ids))

    def test_migrated_tree_passes_check(self):
        B.migrate(self.root, apply=True, repo=self.repo)
        problems = B.Board(self.root, self.repo).check()
        # Migration keeps legacy bodies byte-for-byte, so their invented headings now draw the
        # section-schema warning (#Z4HR: warn, never error, and no rewriting on migration).
        # What the test asserts is that there are no *errors*.
        self.assertTrue(all(p.code in ("unknown_section", "missing_verify") and p.severity == "warning"
                            for p in problems), problems)

    def test_unparsable_files_are_reported_not_written(self):
        write(self.root / "features" / "2026-09-17-freeform.md", "Some note without a title.\n")
        report = B.migrate(self.root, apply=True, repo=self.repo)
        self.assertEqual([m.path for m in report.skipped], ["features/2026-09-17-freeform.md"])
        self.assertEqual((self.root / "features" / "2026-09-17-freeform.md").read_text(),
                         "Some note without a title.\n")

    def test_header_status_losing_to_the_folder_is_noted(self):
        path = self.root / "features" / "needs_qa_llm" / "2026-09-17-shortcut-hints.md"
        path.write_text(path.read_text().replace("- **Status**: needs-qa-llm", "- **Status**: open"))
        report = B.migrate(self.root, apply=True, repo=self.repo)
        item = next(m for m in report.converted if m.path.endswith("shortcut-hints.md"))
        self.assertEqual(item.status, "needs-qa-llm")
        self.assertIn("folder wins", item.note)


class ScriptTests(TempBoardTest):
    """The CLI wrapper collaborators and CI use."""

    def setUp(self):
        super().setUp()
        shutil.rmtree(self.root)
        shutil.copytree(FIXTURES, self.root)
        self.script = Path(__file__).resolve().parents[1] / "scripts" / "relay-board.py"

    def run_script(self, *args):
        env = dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parents[1] / "backend"))
        return subprocess.run(["python3", str(self.script), "--issues", str(self.root), *args],
                              capture_output=True, text=True, env=env)

    def test_migrate_check_and_index(self):
        dry = self.run_script("migrate")
        self.assertEqual(dry.returncode, 0, dry.stderr)
        self.assertIn("would migrate: 6 card(s)", dry.stdout)
        self.assertIn("dry run", dry.stdout)
        applied = self.run_script("migrate", "--apply")
        self.assertEqual(applied.returncode, 0, applied.stderr)
        check = self.run_script("check")
        self.assertEqual(check.returncode, 0, check.stdout + check.stderr)
        self.assertIn("0 error(s)", check.stdout)
        index = self.run_script("index", "--stdout")
        self.assertIn("# Board", index.stdout)

    def test_check_fails_on_an_error(self):
        self.run_script("migrate", "--apply")
        path = next((self.root / "features").glob("*.md"))
        path.write_text(path.read_text().replace("status: ready", "status: needs-qa-llm"))
        check = self.run_script("check")
        self.assertEqual(check.returncode, 1)
        self.assertIn("folder_status_mismatch", check.stdout)
        as_json = self.run_script("check", "--json")
        self.assertIn('"code": "folder_status_mismatch"', as_json.stdout)


# ---------------------------------------------- merge, split and the board's sections

class MergeTests(TempBoardTest):
    """`merge_cards`: nothing is deleted and nothing stops resolving (protocol 19.9)."""

    def setUp(self):
        super().setUp()
        (self.root / B.BOARD_CONFIG).write_text(B.CONFIG_TEXT, encoding="utf-8")
        self.into = self.card("features/2026-09-17-voice.md", CARD)
        self.other = self.card("features/2026-09-18-dictation.md", CARD.replace(
            "id: K7Q2", "id: M3XJ").replace("rank: 0i", "rank: 0j").replace(
            "status: in-progress", "status: inbox").replace(
            "# Voice transcription mode", "# Dictation\n\n## Issue\nlet me dictate"))
        self.board.append_thread("M3XJ", "can we use whisper?", author="owner", kind="comment")

    def merge(self, reason="the same request twice"):
        return B.merge_cards(self.board, self.into, [self.other], reason=reason)

    def test_the_survivor_keeps_the_merged_text_and_records_where_it_came_from(self):
        result = self.merge()
        survivor = B.Card.load(self.into.path)
        self.assertIn("## Merged in", survivor.body)
        self.assertIn("#M3XJ", survivor.body)
        self.assertIn("let me dictate", survivor.body)            # the user's own words, kept
        self.assertEqual(survivor.front["links"]["merged_from"], ["M3XJ"])
        self.assertEqual(result["merged"][0]["id"], "M3XJ")

    def test_the_merged_card_is_closed_in_place_and_still_resolves(self):
        self.merge()
        merged = self.board.card_by_id("M3XJ")
        self.assertIsNotNone(merged)                              # #M3XJ still resolves
        self.assertEqual(merged.status, "dropped")
        self.assertEqual(B.merged_into(merged), "K7Q2")
        self.assertIn("features/done/", str(merged.path))
        self.assertIn("## Resolution", merged.body)
        self.assertIn("let me dictate", merged.body)              # its own text is untouched

    def test_the_thread_is_carried_over_in_order_and_says_where_it_came_from(self):
        self.merge()
        entries = self.board.thread("K7Q2")
        self.assertEqual([e.text for e in entries], ["can we use whisper?"])
        self.assertEqual(entries[0].attrs["from"], "M3XJ")
        self.assertIn("orig", entries[0].attrs)
        self.assertEqual([e.entry_id for e in entries], sorted(e.entry_id for e in entries))
        self.assertEqual([p for p in self.board.check() if p.code != "missing_verify"], [])

    def test_a_quoted_body_cannot_end_the_section_it_was_quoted_into(self):
        self.merge()
        body = B.Card.load(self.into.path).body
        headings = [line for line in body.splitlines() if line.startswith("## ")]
        self.assertEqual(headings[-1], "## Merged in")

    def test_merging_a_card_into_itself_or_into_a_merged_card_is_refused(self):
        with self.assertRaises(B.BoardError):
            B.merge_cards(self.board, self.into, [self.into], reason="no")
        self.merge()
        with self.assertRaises(B.BoardError):
            B.merge_cards(self.board, self.board.card_by_id("M3XJ"),
                          [self.board.card_by_id("K7Q2")], reason="no")


class SplitTests(TempBoardTest):
    """`split_card`: one card per piece, and the original stays as the record."""

    def setUp(self):
        super().setUp()
        (self.root / B.BOARD_CONFIG).write_text(B.CONFIG_TEXT, encoding="utf-8")
        self.card_file = self.card("features/2026-09-17-voice.md", CARD)
        self.parts = [{"title": "Voice mode", "request": "add voice transcribe mode"},
                      {"title": "A clock", "request": "and put a clock in the tab bar"}]

    def test_two_cards_come_out_and_the_original_names_them(self):
        result = B.split_card(self.board, self.card_file, self.parts, reason="two asks in one card",
                              category="features")
        self.assertEqual(len(result["children"]), 2)
        original = B.Card.load(self.board.repo / result["path"])
        self.assertIn("## Split", original.body)
        self.assertEqual(original.front["links"]["split_into"],
                         [c["id"] for c in result["children"]])
        self.assertEqual(original.status, "in-progress")          # kept open by default
        child = B.Card.load(self.board.repo / result["children"][1]["path"])
        self.assertEqual(child.front["parent"], "K7Q2")
        self.assertEqual(child.front["links"]["split_from"], "K7Q2")
        self.assertIn("and put a clock in the tab bar", child.body)
        self.assertEqual([p for p in self.board.check() if p.code != "missing_verify"], [])

    def test_close_moves_the_original_to_done_with_a_resolution(self):
        result = B.split_card(self.board, self.card_file, self.parts, reason="split",
                              category="features", close=True)
        self.assertTrue(result["closed"])
        original = B.Card.load(self.board.repo / result["path"])
        self.assertEqual(original.status, "dropped")
        self.assertIn("features/done/", result["path"])
        self.assertIn("## Resolution", original.body)
        self.assertIn("add voice transcribe mode", original.body)  # its own text is still there
        self.assertEqual([p for p in self.board.check() if p.code != "missing_verify"], [])

    def test_a_split_needs_at_least_two_parts_and_a_request_each(self):
        with self.assertRaises(B.BoardError):
            B.split_card(self.board, self.card_file, self.parts[:1], reason="x", category="features")
        with self.assertRaises(B.BoardError):
            B.split_card(self.board, self.card_file, [{"title": "a"}, {"title": "b"}],
                         reason="x", category="features")


class ConfigWriteTests(TempBoardTest):
    """`board.yaml` is rewritten in a form it can read back (`board_sections`)."""

    def test_the_default_config_round_trips(self):
        (self.root / B.BOARD_CONFIG).write_text(B.CONFIG_TEXT, encoding="utf-8")
        before = self.board.config()
        B.write_config(self.board, before)
        self.assertEqual(self.board.config(), before)

    def test_a_scalar_with_flow_punctuation_stays_one_value(self):
        text = B.render_config({"version": 1, "tabs": [{"id": "done", "filter": "status:done,dropped"}]})
        self.assertIn("'status:done,dropped'", text)
        self.assertEqual(B.parse_yaml(text)["tabs"][0]["filter"], "status:done,dropped")


class BoardFolderTests(unittest.TestCase):
    """`board/` is where a new board goes; the older spellings are still read (19.12)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name).resolve()

    def make(self, name: str) -> Path:
        folder = self.dir / name
        folder.mkdir(parents=True)
        (folder / B.BOARD_CONFIG).write_text(B.CONFIG_TEXT, encoding="utf-8")
        return folder

    def test_a_new_board_is_visible_and_the_older_spellings_are_still_read(self):
        # Owner, 2026-09-21 (#1CXD): a new board is `<project>/board/`, plain and visible.
        # Reading is tolerant and ordered, and this tuple is the only definition of that order
        # in the backend.
        self.assertEqual(B.DEFAULT_BOARD_FOLDER, ".board")
        self.assertEqual(B.BOARD_FOLDERS, (".board", "board", ".switchboard", "switchboard", "issues"))
        self.assertEqual(B.new_board_folder(), ".board")
        self.assertIsNone(B.board_folder(self.dir))
        issues = self.make("issues")
        self.assertEqual(B.board_folder(self.dir), issues)

    def test_the_first_folder_that_exists_wins_in_that_order(self):
        self.make("issues")
        shown = self.make("switchboard")
        self.assertEqual(B.board_folder(self.dir), shown)
        hidden = self.make(".switchboard")
        self.assertEqual(B.board_folder(self.dir), hidden)
        old = self.make("board")
        self.assertEqual(B.board_folder(self.dir), old)
        current = self.make(".board")
        self.assertEqual(B.board_folder(self.dir), current)

    def test_a_new_board_scaffolds_into_board_and_names_it_in_gitattributes(self):
        board = B.Board(self.dir / B.DEFAULT_BOARD_FOLDER, self.dir)
        files = B.scaffold(board)
        self.assertEqual(files, [".board/board.yaml", ".board/.gitignore",
                                 ".board/threads/.gitkeep", ".gitattributes",
                                 ".board/POLICY.md", "AGENTS.md"])
        self.assertEqual(B.board_folder(self.dir), board.root)
        self.assertIn(".board/threads/*.md merge=union",
                      (self.dir / ".gitattributes").read_text())

    def test_a_hidden_board_is_found_and_names_itself_in_gitattributes(self):
        board = B.Board(self.dir / ".switchboard", self.dir)
        files = B.scaffold(board)
        self.assertEqual(files, [".switchboard/board.yaml", ".switchboard/.gitignore",
                                 ".switchboard/threads/.gitkeep", ".gitattributes",
                                 ".switchboard/POLICY.md", "AGENTS.md"])
        self.assertEqual(B.board_folder(self.dir), board.root)
        self.assertIn(".switchboard/threads/*.md merge=union",
                      (self.dir / ".gitattributes").read_text())

    def test_scaffold_writes_the_board_and_names_its_own_folder_in_gitattributes(self):
        board = B.Board(self.dir / "switchboard", self.dir)
        files = B.scaffold(board)
        self.assertEqual(files, ["switchboard/board.yaml", "switchboard/.gitignore",
                                 "switchboard/threads/.gitkeep", ".gitattributes",
                                 "switchboard/POLICY.md", "AGENTS.md"])
        self.assertIn("version: 1", (board.root / B.BOARD_CONFIG).read_text())
        self.assertIn(".private/", (board.root / ".gitignore").read_text())
        self.assertTrue((board.root / "threads").is_dir())
        self.assertIn("switchboard/threads/*.md merge=union",
                      (self.dir / ".gitattributes").read_text())
        # Called again it writes nothing: the board is already there.
        self.assertEqual(B.scaffold(board), [])

    def test_scaffold_on_the_older_spelling_keeps_naming_issues(self):
        board = B.Board(self.dir / "issues", self.dir)
        B.scaffold(board)
        self.assertIn("issues/threads/*.md merge=union", (self.dir / ".gitattributes").read_text())


class MoveTheFolderTests(unittest.TestCase):
    """`rename_board_folder`: the one explicit action that moves an existing board -- now "move
    this board to `board/`" (owner, 2026-09-21, #1CXD)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name).resolve()

    def env(self):
        # Never the caller's index: a test that runs git in a temp repo must not inherit
        # GIT_INDEX_FILE from the session that launched it (CLAUDE.md).
        env = dict(os.environ)
        for name in ("GIT_INDEX_FILE", "GIT_DIR", "GIT_WORK_TREE"):
            env.pop(name, None)
        return env

    def git(self, *args, check=True):
        return subprocess.run(["git", "-C", str(self.dir), *args], capture_output=True, text=True,
                              check=check, env=self.env(), timeout=30)

    def make(self, name: str, *, git: bool = False, commit: bool = False) -> B.Board:
        board = B.Board(self.dir / name, self.dir)
        B.scaffold(board)
        card = B.new_card("work", "A card", "inbox", card_id="K7Q2", rank="m")
        B.write_new_card(board, card, "features")
        if git:
            self.git("init", "-q")
            self.git("config", "user.email", "t@example.com")
            self.git("config", "user.name", "T")
            if commit:
                self.git("add", "-A")
                self.git("commit", "-qm", "board")
        return board

    def test_git_mv_moves_a_committed_board_to_board_and_carries_the_gitattributes_line(self):
        board = self.make(".switchboard", git=True, commit=True)
        move = B.rename_board_folder(board)
        self.assertEqual((move.old, move.new, move.method, move.hidden),
                         (".switchboard", ".board", "git mv", True))
        self.assertEqual(board.root, self.dir / ".board")
        self.assertFalse((self.dir / ".switchboard").exists())
        self.assertEqual(B.board_folder(self.dir), self.dir / ".board")
        self.assertIsNotNone(board.card_by_id("K7Q2"))
        self.assertIn(".board/threads/*.md merge=union",
                      (self.dir / ".gitattributes").read_text())
        self.assertIn(".gitattributes", move.files)
        self.assertIn(".board/POLICY.md", move.files)
        # git knows it as a rename, so the card's history is not broken.
        staged = self.git("diff", "--cached", "--name-status", "-M").stdout
        self.assertIn(".board/features/", staged)

    def test_every_older_spelling_moves_to_board(self):
        # The two Relay created itself; `issues/` has its own refusal below.  One project each,
        # so the moves cannot shadow one another.
        for index, name in enumerate(("board", ".switchboard", "switchboard")):
            with self.subTest(name):
                project = self.dir / f"p{index}"
                project.mkdir()
                board = B.Board(project / name, project)
                B.scaffold(board)
                B.write_new_card(board, B.new_card("work", "A card", "inbox", card_id="K7Q2",
                                                   rank="m"), "features")
                move = B.rename_board_folder(board)
                self.assertEqual((move.old, move.new), (name, ".board"))
                self.assertEqual(B.board_folder(project), project / ".board")
                self.assertIsNotNone(board.card_by_id("K7Q2"))

    def test_the_older_spellings_are_still_reachable_as_a_target(self):
        # The retired hide/show action (19.17's `{hidden}` shape, which `board_protocol` still
        # maps for one release) is `to=`, so nothing about it needed keeping in this module.
        board = self.make("switchboard")
        move = B.rename_board_folder(board, ".switchboard")
        self.assertEqual((move.old, move.new, move.hidden), ("switchboard", ".switchboard", True))
        back = B.rename_board_folder(board, "board")
        self.assertEqual((back.old, back.new, back.hidden), (".switchboard", "board", False))

    def test_a_board_outside_git_is_renamed_in_place(self):
        board = self.make("switchboard")
        move = B.rename_board_folder(board)
        self.assertEqual(move.method, "rename")
        self.assertEqual(B.board_folder(self.dir), self.dir / ".board")
        self.assertIn("is now .board/", move.summary())

    def test_a_board_git_has_never_seen_is_renamed_in_place(self):
        # Initialised but not committed: there is nothing for `git mv` to record, and it would
        # refuse every path as untracked.
        board = self.make("switchboard", git=True)
        move = B.rename_board_folder(board)
        self.assertEqual(move.method, "rename")
        self.assertEqual(B.board_folder(self.dir), self.dir / ".board")

    def test_uncommitted_changes_refuse_the_move_and_change_nothing(self):
        board = self.make("switchboard", git=True, commit=True)
        card = board.card_by_id("K7Q2")
        card.path.write_text(card.path.read_text(encoding="utf-8") + "\nedited\n", encoding="utf-8")
        with self.assertRaises(B.BoardError) as caught:
            B.rename_board_folder(board)
        self.assertIn("uncommitted", str(caught.exception))
        self.assertTrue((self.dir / "switchboard").is_dir())
        self.assertFalse((self.dir / ".board").exists())

    def test_an_existing_target_refuses_the_move(self):
        board = self.make("switchboard")
        (self.dir / ".board").mkdir()
        with self.assertRaises(B.BoardError) as caught:
            B.rename_board_folder(board)
        self.assertIn("already exists", str(caught.exception))
        self.assertTrue((self.dir / "switchboard" / B.BOARD_CONFIG).is_file())

    def test_an_issues_board_is_never_moved(self):
        # This repository's own board is `issues/`, and it is never converted.
        board = self.make("issues")
        with self.assertRaises(B.BoardError) as caught:
            B.rename_board_folder(board)
        self.assertIn("issues/", str(caught.exception))
        self.assertEqual(board.root, self.dir / "issues")
        self.assertTrue((self.dir / "issues" / B.BOARD_CONFIG).is_file())
        self.assertFalse((self.dir / ".board").exists())

    def test_issues_is_never_a_target_either(self):
        board = self.make("switchboard")
        with self.assertRaises(B.BoardError) as caught:
            B.rename_board_folder(board, "issues")
        self.assertIn("not a board folder Relay creates", str(caught.exception))
        self.assertEqual(board.root, self.dir / "switchboard")

    def test_a_board_that_is_already_there_is_refused(self):
        board = self.make(".board")
        with self.assertRaises(B.BoardError) as caught:
            B.rename_board_folder(board)
        self.assertIn("already", str(caught.exception))


class PolicyFileTests(unittest.TestCase):
    """`POLICY.md` and the instruction-file pointer (#R9G7): the board's rules as a file, for
    an agent that has no `board_*` tools -- Claude Code or Codex in a Relay pane, which never sees
    the worker's system prompt (#4NXH)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name).resolve()

    def board(self, name: str = B.DEFAULT_BOARD_FOLDER) -> B.Board:
        return B.Board(self.dir / name, self.dir)

    def policy_source(self) -> str:
        return (Path(B.__file__).resolve().parent / B.POLICY_SOURCE).read_text(encoding="utf-8")

    # ---- the file itself
    def test_it_holds_the_policy_the_deliver_procedure_and_the_appendix(self):
        text = B.policy_text(self.board())
        # the policy body, the rules of it, and none of its provenance comment
        for line in self.policy_source().splitlines():
            if line.startswith(("1.", "5.", "9.", "12.")):
                self.assertIn(line.strip(), text)
        self.assertNotIn("Versioned here so evals can pin it", text)
        self.assertNotIn("Board agent policy v", text)
        # the bundled `deliver` skill: its body, demoted under a heading of ours, no frontmatter
        self.assertIn("Deliver a request through the Board", text)
        self.assertIn("### 1. Is it already done?", text)
        self.assertIn("### 3. Claim it", text)
        self.assertNotIn("description: Work a request through the Board", text)
        # the appendix: every tool the two of them name, and how to do it by hand
        self.assertIn("## Without the board tools", text)
        for tool in ("board_list", "board_read", "board_create_card", "board_claim",
                     "board_comment", "board_update_card", "board_move_card"):
            self.assertIn(tool, text)
        self.assertIn("needs_qa_llm/", text)               # the lanes that are folders
        self.assertIn("relay:entry", text)                 # the thread entry format
        self.assertIn("relay-board.py", text)              # how to check what you wrote
        # generated, and it says so
        self.assertTrue(text.startswith("<!-- Generated by relay_core.board.policy_text"))
        self.assertIn("Never hand-edited", text)

    def test_the_board_folder_is_substituted_throughout(self):
        current = B.policy_text(self.board())
        self.assertIn("# Board policy — `.board/`", current)
        self.assertIn(".board/threads/<ID>.md", current)
        self.assertIn("the repository's `.board/` tracker", current)
        self.assertNotIn("issues/", current)
        self.assertIn("rg --hidden", current)
        # `board/` is visible, so it is in an ordinary `rg`'s way: the file says how to
        # leave it out of a code search (owner, 2026-09-21, #1CXD).
        self.assertNotIn("rg -g '!", current)

        hidden = B.policy_text(self.board(".switchboard"))
        self.assertIn("# Board policy", hidden)
        self.assertIn(".switchboard/threads/<ID>.md", hidden)
        self.assertIn("the repository's `.switchboard/` tracker", hidden)
        self.assertNotIn("issues/", hidden)
        # A hidden folder has the opposite problem: an `rg` does not see it, so the file says so
        # and does not tell anybody to exclude what they cannot find.
        self.assertIn("rg --hidden", hidden)
        self.assertNotIn("rg -g '!", hidden)

        legacy = B.policy_text(self.board("issues"))
        self.assertIn("issues/threads/<ID>.md", legacy)
        self.assertNotIn("rg --hidden", legacy)
        self.assertIn("rg -g '!issues/'", legacy)

    def test_a_guest_is_told_it_cannot_write_the_session_field(self):
        # The pane token is Relay's (#R9G7): a guest claims with `assignee` and a thread entry.
        text = B.policy_text(self.board())
        claim = text[text.index("### Claim it"):text.index("### Comment")]
        self.assertIn("Do **not** write `session:`", claim)
        self.assertIn("assignee: <your name>", claim)
        self.assertIn("status: executing", claim)

    def test_scaffold_writes_it_and_rewrites_a_stale_one(self):
        board = self.board()
        files = B.scaffold(board)
        self.assertIn(".board/POLICY.md", files)
        path = board.root / B.POLICY_FILE
        self.assertEqual(path.read_text(encoding="utf-8"), B.policy_text(board))
        self.assertEqual(B.scaffold(board), [])            # generated, and already current
        path.write_text("an old version\n", encoding="utf-8")
        self.assertEqual(B.scaffold(board), [".board/POLICY.md"])
        self.assertEqual(path.read_text(encoding="utf-8"), B.policy_text(board))

    def test_write_index_refreshes_an_existing_policy_but_never_creates_one(self):
        board = self.board()
        B.scaffold(board)
        path = board.root / B.POLICY_FILE
        path.write_text("an old version\n", encoding="utf-8")
        board.write_index()
        self.assertEqual(path.read_text(encoding="utf-8"), B.policy_text(board))
        path.unlink()
        board.write_index()
        self.assertFalse(path.exists())

    def test_this_repositorys_own_policy_is_a_fresh_regeneration(self):
        """#WC3E: `issues/POLICY.md` is generated, and a hand-edit of it is the bug.

        The one test in this file that reads the repository it lives in, and deliberately: the
        file that guests actually load is the one that has to agree with `board_policy.md` and
        the `deliver` skill. It goes stale when somebody changes a source and forgets
        `relay-board.py policy`, and nothing else notices -- Relay rewrites a stale copy only
        when it next scaffolds or indexes that board.
        """
        repo = Path(B.__file__).resolve().parents[2]
        root = repo / B.LEGACY_BOARD_FOLDER
        policy = root / B.POLICY_FILE
        if not policy.is_file():                           # pragma: no cover - not this checkout
            self.skipTest(f"{policy} is not in this tree")
        want = B.policy_text(B.Board(root, repo))
        self.assertEqual(
            policy.read_text(encoding="utf-8"), want,
            f"{policy.relative_to(repo)} is not what it would be generated as: run "
            "`python3 scripts/relay-board.py policy` and commit it with the change that made it "
            "stale.")

    # ---- the pointer in the instruction files
    def test_the_block_is_appended_to_an_existing_claude_md(self):
        (self.dir / "CLAUDE.md").write_text("# Project\n\nThe project's own rules.\n")
        board = self.board()
        files = B.scaffold(board)
        self.assertIn("CLAUDE.md", files)
        text = (self.dir / "CLAUDE.md").read_text()
        self.assertTrue(text.startswith("# Project\n\nThe project's own rules.\n"))
        self.assertIn(B.POINTER_START, text)
        self.assertIn(B.POINTER_END, text)
        self.assertIn(".board/POLICY.md", text)
        self.assertIn("needs-verification", text)
        # The one sentence that tells an agent where the cards are and how to keep them out of
        # a code search (owner, 2026-09-21, #1CXD).
        self.assertIn("rg --hidden", text)

    def test_an_agents_md_is_created_with_an_import_when_only_claude_md_exists(self):
        # instructions.py loads the FIRST hit per directory in PROJECT_ORDER, so a new AGENTS.md
        # would shadow the project's CLAUDE.md. The `@CLAUDE.md` import is what stops that.
        (self.dir / "CLAUDE.md").write_text("# Project\n\nThe project's own rules.\n")
        files = B.scaffold(self.board())
        self.assertIn("AGENTS.md", files)
        text = (self.dir / "AGENTS.md").read_text()
        self.assertIn("@CLAUDE.md", text)
        self.assertLess(text.index("@CLAUDE.md"), text.index(B.POINTER_START))
        self.assertIn(B.POINTER_START, text)

    def test_an_agents_md_is_created_when_the_project_has_no_instruction_file(self):
        files = B.scaffold(self.board())
        self.assertIn("AGENTS.md", files)
        self.assertNotIn("CLAUDE.md", files)
        self.assertFalse((self.dir / "CLAUDE.md").exists())
        text = (self.dir / "AGENTS.md").read_text()
        self.assertNotIn("@CLAUDE.md", text)
        self.assertIn(B.POINTER_START, text)

    def test_both_files_get_the_block_and_neither_is_created(self):
        (self.dir / "CLAUDE.md").write_text("# Project\n")
        (self.dir / "AGENTS.md").write_text("# Agents\n\nBe careful.\n")
        files = B.scaffold(self.board())
        self.assertIn("CLAUDE.md", files)
        self.assertIn("AGENTS.md", files)
        agents = (self.dir / "AGENTS.md").read_text()
        self.assertTrue(agents.startswith("# Agents\n\nBe careful.\n"))
        self.assertNotIn("@CLAUDE.md", agents)             # not created, so nothing is imported
        for name in ("CLAUDE.md", "AGENTS.md"):
            self.assertIn(B.POINTER_START, (self.dir / name).read_text())

    def test_warp_md_is_never_touched(self):
        # WARP.md remains a compatible instruction source, but Board scaffolding writes its
        # pointer only to AGENTS.md and CLAUDE.md; Relay's agent has the policy in its prompt.
        (self.dir / "WARP.md").write_text("# Warp\n")
        (self.dir / "CLAUDE.md").write_text("# Project\n")
        files = B.scaffold(self.board())
        self.assertNotIn("WARP.md", files)
        self.assertEqual((self.dir / "WARP.md").read_text(), "# Warp\n")

    def test_a_second_scaffold_changes_nothing(self):
        (self.dir / "CLAUDE.md").write_text("# Project\n")
        board = self.board()
        B.scaffold(board)
        before = {p: p.read_bytes() for p in sorted(self.dir.rglob("*")) if p.is_file()}
        self.assertEqual(B.scaffold(board), [])
        self.assertEqual({p: p.read_bytes() for p in sorted(self.dir.rglob("*")) if p.is_file()},
                         before)

    def test_a_changed_block_replaces_the_old_one_between_the_markers(self):
        claude = self.dir / "CLAUDE.md"
        claude.write_text("# Project\n\nRules.\n")
        board = self.board()
        B.scaffold(board)
        text = claude.read_text()
        start, end = text.index(B.POINTER_START), text.index(B.POINTER_END)
        claude.write_text(text[:start] + B.POINTER_START + "\nold wording\n" + text[end:]
                          + "\nA later section.\n")
        self.assertEqual(B.scaffold(board), ["CLAUDE.md"])
        after = claude.read_text()
        self.assertNotIn("old wording", after)
        self.assertEqual(after.count(B.POINTER_START), 1)
        self.assertTrue(after.startswith("# Project\n\nRules.\n"))
        self.assertTrue(after.rstrip("\n").endswith("A later section."))

    def test_half_a_block_is_left_for_a_person(self):
        # One marker without the other is somebody's hand edit; guessing what it replaces is how
        # a generator eats a file.
        claude = self.dir / "CLAUDE.md"
        claude.write_text("# Project\n\n" + B.POINTER_START + "\nsomething of theirs\n")
        files = B.scaffold(self.board())
        self.assertNotIn("CLAUDE.md", files)
        self.assertIn("something of theirs", claude.read_text())

    def test_the_relay_board_command_is_named_only_when_the_script_is_there(self):
        board = self.board()
        self.assertIn("ships with Relay, not with this project", B.policy_text(board))
        (self.dir / "scripts").mkdir()
        (self.dir / "scripts" / "relay-board.py").write_text("#!/usr/bin/env python3\n")
        text = B.policy_text(board)
        self.assertIn("python3 scripts/relay-board.py --board .board check", text)
        self.assertIn("python3 scripts/relay-board.py --board .board index", text)

    def test_write_policy_regenerates_both_on_an_existing_board(self):
        board = self.board()
        B.scaffold(board)
        (board.root / B.POLICY_FILE).unlink()
        (self.dir / "AGENTS.md").unlink()
        self.assertEqual(sorted(B.write_policy(board)), [".board/POLICY.md", "AGENTS.md"])
        self.assertTrue((board.root / B.POLICY_FILE).is_file())
        self.assertTrue((self.dir / "AGENTS.md").is_file())
        self.assertEqual(B.write_policy(board), [])


class PolicyReachesTheRelayPromptTests(unittest.TestCase):
    """The shadowing case, checked against the loader itself: a created `AGENTS.md` must not cost
    Relay's own agent the project's `CLAUDE.md` (`instructions.PROJECT_ORDER` takes the first hit
    per directory)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name).resolve()

    def test_the_created_agents_md_imports_the_claude_md_it_would_shadow(self):
        from relay_core import instructions
        (self.dir / ".git").mkdir()
        (self.dir / "CLAUDE.md").write_text("# Project\n\nCLAUDE-ONLY-MARKER: build with ninja.\n")
        board = B.Board(self.dir / ".switchboard", self.dir)
        B.scaffold(board)
        # Without the import this is exactly what would break: AGENTS.md wins the first-hit pick.
        self.assertEqual([p.name for p in instructions.auto_project_files(self.dir)], ["AGENTS.md"])
        loaded = instructions.load({"files": [], "project_auto": True}, self.dir)
        self.assertIn("CLAUDE-ONLY-MARKER: build with ninja.", loaded.section)
        self.assertIn("POLICY.md", loaded.section)
        self.assertIn(str(self.dir / "CLAUDE.md"), loaded.loaded)

    def test_a_claude_local_md_is_imported_too(self):
        from relay_core import instructions
        (self.dir / ".git").mkdir()
        (self.dir / "CLAUDE.md").write_text("# Project\n\nCLAUDE-ONLY-MARKER\n")
        (self.dir / "CLAUDE.local.md").write_text("LOCAL-ONLY-MARKER\n")
        B.scaffold(B.Board(self.dir / ".switchboard", self.dir))
        loaded = instructions.load({"files": [], "project_auto": True}, self.dir)
        self.assertIn("CLAUDE-ONLY-MARKER", loaded.section)
        self.assertIn("LOCAL-ONLY-MARKER", loaded.section)

if __name__ == "__main__":
    unittest.main()
