# SPDX-License-Identifier: AGPL-3.0-or-later
"""Try it: stage the situation, do the mechanical pass, hand over one question (protocol 31.10).

Card #JNYN.  No model and no network: the Try it turn is faked exactly the way
`tests/test_board_protocol.py` fakes a cleanup turn — a stub supervisor records what was
submitted, and the test then plays the turn's events back through `observe`, doing by hand
whatever the model would have done to the card in between.
"""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import board as B
from relay_core import board_protocol as P
from relay_core import board_tools as T
from relay_core import tryit_protocol as TI
from relay_core.provider import ProviderConfig

CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""


class StubTurns:
    """`TurnSupervisor` as Try it needs it: a submission record and an agent to check for."""

    def __init__(self):
        self.submitted = []
        self.resets = 0
        self.agent = None
        self.busy = False
        self.cancels = 0

    def reset(self):
        self.resets += 1

    def submit(self, prompt, when="now", request_id=None, context=None, attachments=None, **kw):
        self.submitted.append({"prompt": prompt, "when": when, "id": request_id})
        return "q1"

    def cancel(self):
        self.cancels += 1

    def now_or_later(self, now, later):
        return later() if self.busy else now()


class StubBoardAgent:
    """Stands in for the Board worker's Agent: it only has to carry the board tools."""

    def __init__(self, tools):
        self.board = tools
        self.config = ProviderConfig(api_key="", base_url="https://example.invalid", model="stub/one")
        self.session_id = "s-1"


class TryItTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name).resolve()
        self.root = self.repo / "issues"
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.board = B.Board(self.root, self.repo)
        self.events = []
        self.turns = StubTurns()
        self.commands = P.BoardCommands(self.turns, self.events.append)
        self.commands.configure(str(self.repo), {})
        self.agent_tools = self.commands.agent_tools(str(self.repo), {})
        self.turns.agent = StubBoardAgent(self.agent_tools)
        self.card = self.make_card()

    # ---- helpers ---------------------------------------------------------------
    def send(self, **request):
        self.events.clear()
        self.commands.dispatch(request)
        return self.events

    def of(self, name):
        return [e for e in self.events if e.get("event") == name]

    def tryit_events(self, events=None):
        return [e for e in (self.events if events is None else events)
                if e.get("event") == "tryit"]

    def make_card(self, title="Order totals are wrong above 100 items",
                  text="totals go negative past a hundred lines"):
        events = self.send(type="board_create", id="r1", tab="features", status="inbox",
                           title=title, text=text)
        return [e for e in events if e["event"] == "board_written"][0]["card_id"]

    def start(self, **kw):
        return self.send(type="try_run", id="k1", card=self.card, **kw)

    def finish(self, *, outcome="done", text="Staged it; one question on the card."):
        """Play the turn's end back through `observe`, as the worker does."""
        self.events.clear()
        if text:
            self.commands.observe({"event": "delta", "text": text})
        self.commands.observe({"event": outcome, "turn_id": "t-9"})
        return self.events

    def write_section(self, body, heading="Try it"):
        card = self.board.card_by_id(self.card)
        self.agent_tools.run("board_update_card",
                             {"id": self.card, "base_hash": B.file_hash(card.path),
                              "replace_section": {"heading": heading, "text": body}})

    def evidence(self):
        return TI.evidence_dir_for(self.repo, self.card)

    def seal(self, text="The gate refuses the move and names three tests."):
        out = self.evidence()
        out.mkdir(parents=True, exist_ok=True)
        (out / TI.EXPECTED_FILE).write_text(text + "\n", encoding="utf-8")
        return str((out / TI.EXPECTED_FILE).relative_to(self.repo))

    def staged_section(self):
        """A `## Try it` of the shape the brief asks for, with its sealed file."""
        expected = self.seal()
        self.write_section(
            "1. Open it: `bash docs/qa_evidence/x/stage.sh`\n\n"
            "2. The task: open the card the fixture put in Needs verification and try to move it "
            "to Done.\n\n"
            "3. The question: did it stop you, and did you know why?\n\n"
            f"Expected: {expected} (sealed until you answer)\n")
        return expected


# ------------------------------------------------------------------ try_run: the turn

class RunTests(TryItTest):
    def test_successful_retry_does_not_report_previous_failure(self):
        self.start()
        self.finish(outcome="error", text="")
        self.start()
        self.staged_section()
        event = self.tryit_events(self.finish(outcome="done", text="Ready now."))[-1]
        self.assertTrue(event["section_written"])
        self.assertFalse(event["staging_failed"])
        self.assertEqual(event["message"], "Ready now.")


    def test_the_types_match_the_protocol_modules(self):
        # The same drift guard `tests/test_profile_protocol.py` puts on PROFILE_TYPES: the list in
        # `board_protocol` exists so a worker need not import this module at start-up.
        self.assertEqual(set(P.TRYIT_TYPES), set(TI.TYPES))

    def test_it_starts_one_turn_with_the_brief_the_card_and_the_evidence_directory(self):
        events = self.start()
        started = self.tryit_events(events)[0]
        self.assertEqual(started["state"], "started")
        self.assertEqual(started["id"], "k1")
        self.assertEqual(started["card_id"], self.card)
        self.assertTrue(started["run_id"].startswith("t-"))
        self.assertEqual(started["out"], f"docs/qa_evidence/{_today()}-tryit-{self.card}")
        self.assertTrue((self.repo / started["out"]).is_dir())
        prompt = self.turns.submitted[-1]["prompt"]
        self.assertIn("[Board Try it]", prompt)
        self.assertIn(f"#{self.card}", prompt)
        self.assertIn("one task and one question", prompt)        # the brief
        self.assertIn("totals go negative past a hundred lines", prompt)   # the card
        self.assertIn(started["out"], prompt)
        self.assertEqual(self.turns.resets, 1)                    # its own conversation

    def test_the_brief_never_puts_the_expected_result_on_the_card(self):
        # The one mistake the worked example made (codex review section B, scenario 2).
        self.start()
        prompt = self.turns.submitted[-1]["prompt"]
        self.assertIn("expected.md", prompt)
        self.assertIn("sealed", prompt.lower())
        self.assertIn("and nowhere on the card", prompt)

    def test_progress_lines_stream_while_it_runs(self):
        self.start()
        self.events.clear()
        tagged = self.commands.observe({"event": "status", "text": "reading the card",
                                        "turn_id": "t-9"})
        self.assertTrue(tagged["tryit"])
        self.assertEqual(tagged["card_id"], self.card)
        lines = [e["line"] for e in self.tryit_events() if e["state"] == "progress"]
        self.assertEqual(lines, ["reading the card"])
        self.events.clear()
        self.commands.observe({"event": "tool_started", "tool": "board_read"})
        self.assertEqual([e["line"] for e in self.tryit_events()], ["running board_read"])

    def test_it_finishes_saying_whether_the_section_was_written(self):
        self.start()
        self.staged_section()
        done = self.tryit_events(self.finish())[-1]
        self.assertEqual(done["state"], "finished")
        self.assertTrue(done["section_written"])
        self.assertFalse(done["staging_failed"])
        self.assertIn("one question", done["message"])
        self.assertEqual(done["card_id"], self.card)

    def test_a_staging_failure_writes_a_note_and_no_section(self):
        self.start()
        self.agent_tools.run("board_comment", {
            "id": self.card, "kind": "note",
            "text": "Try it could not be staged: `land.py try --commit` could not build the card's "
                    "commit, and build/relay is not built on this machine."})
        done = self.tryit_events(self.finish(text="I could not stage it."))[-1]
        self.assertEqual(done["state"], "finished")
        self.assertFalse(done["section_written"])
        self.assertTrue(done["staging_failed"])
        self.assertIn("build/relay is not built", done["message"])
        card = self.board.card_by_id(self.card)
        self.assertNotIn("## Try it", card.body)

    def test_a_turn_that_ended_badly_leaves_the_note_the_agent_could_not(self):
        # Live, 2026-09-21: a 30-minute turn on kimi-k3 stalled at the provider at step 53 and
        # ended `error`. The brief asks the *agent* for the "could not be staged" note, and an
        # agent whose provider stalled never reaches it — so the card carried no trace of the
        # attempt at all. The worker writes it now.
        self.start()
        done = self.tryit_events(self.finish(outcome="error", text=""))[-1]
        self.assertEqual(done["state"], "error")
        self.assertFalse(done["section_written"])
        self.assertTrue(done["staging_failed"])
        self.assertIn(TI.FAILED_NOTE, done["message"])
        self.assertIn("ended with an error", done["message"])
        notes = [e for e in self.board.thread(self.card) if e.kind == "note"]
        self.assertTrue(notes[-1].text.startswith(TI.FAILED_NOTE))
        self.assertIn("press Try it again", notes[-1].text)
        self.assertNotIn("## Try it", self.board.card_by_id(self.card).body)

    def test_a_stop_leaves_the_same_note_in_its_own_words(self):
        self.start()
        self.send(type="try_stop", id="k9")
        ended = self.tryit_events(self.finish(outcome="cancelled", text=""))[-1]
        self.assertEqual(ended["state"], "stopped")
        notes = [e for e in self.board.thread(self.card) if e.kind == "note"]
        self.assertIn("it was stopped", notes[-1].text)

    def test_the_agents_own_note_is_not_written_over(self):
        self.start()
        self.agent_tools.run("board_comment", {
            "id": self.card, "kind": "note",
            "text": "Try it could not be staged: `land.py try --commit` could not build the card's "
                    "commit, and build/relay is not built on this machine."})
        self.tryit_events(self.finish(outcome="error", text=""))
        notes = [e for e in self.board.thread(self.card) if e.kind == "note"]
        self.assertEqual(len(notes), 1)
        self.assertIn("build/relay is not built", notes[-1].text)

    def test_a_finished_turn_that_wrote_the_section_leaves_no_note(self):
        self.start()
        self.staged_section()
        self.tryit_events(self.finish())
        self.assertEqual([e for e in self.board.thread(self.card) if e.kind == "note"], [])

    def test_a_second_run_while_one_is_in_flight_says_already_running(self):
        self.start()
        refused = self.tryit_events(self.send(type="try_run", id="k2", card=self.card))[0]
        self.assertEqual(refused["state"], "error")
        self.assertIn("already running", refused["message"])
        self.assertEqual(len(self.turns.submitted), 1)

    def test_stop_ends_the_run(self):
        self.start()
        self.send(type="try_stop", id="k3")
        self.assertEqual(self.turns.cancels, 1)
        ended = self.tryit_events(self.finish(outcome="cancelled", text=""))[-1]
        self.assertEqual(ended["state"], "stopped")
        self.assertFalse(self.commands._tryit().running())
        # …and there is nothing left to stop.
        again = self.tryit_events(self.send(type="try_stop", id="k4"))[0]
        self.assertEqual(again["state"], "error")

    def test_an_unknown_card_and_a_missing_agent_are_refused_with_a_sentence(self):
        bad = self.tryit_events(self.send(type="try_run", id="k5", card="ZZZZ"))[0]
        self.assertEqual(bad["state"], "error")
        self.assertIn("no card #ZZZZ", bad["message"])
        self.turns.agent = None
        none = self.tryit_events(self.send(type="try_run", id="k6", card=self.card))[0]
        self.assertIn("Configure a provider", none["message"])

    def test_try_run_is_refused_while_a_cleanup_runs(self):
        self.send(type="board_cleanup", id="c1", dry_run=True)
        refused = self.send(type="try_run", id="k7", card=self.card)[0]
        self.assertEqual(refused["event"], "error")
        self.assertEqual(refused["code"], "board_busy")
        self.assertTrue(refused["cleanup_running"])


# ------------------------------------------ the environment the verifying session staged (#WC3E)

class ReuseTests(TryItTest):
    """Owner, 2026-09-21: Try it is step 3, so it reuses what step 2 staged rather than restaging."""

    def stage_verify_dir(self, *, script=True, notes=True, day=None):
        out = self.repo / "docs" / "qa_evidence" / f"{day or _today()}-verify-{self.card}"
        out.mkdir(parents=True, exist_ok=True)
        if script:
            (out / TI.STAGE_SCRIPT).write_text("#!/bin/bash\necho staged\n", encoding="utf-8")
        if notes:
            (out / TI.STAGING_NOTES).write_text("The seeded orders are invented.\n", encoding="utf-8")
        return out

    def test_the_prompt_says_to_reuse_the_verify_staging(self):
        out = self.stage_verify_dir()
        started = self.tryit_events(self.start())[0]
        self.assertTrue(started["reusing"])
        self.assertEqual(started["staged"], f"{out.relative_to(self.repo)}/{TI.STAGE_SCRIPT}")
        prompt = self.turns.submitted[-1]["prompt"]
        self.assertIn("ALREADY STAGED by the verifying session", prompt)
        self.assertIn(str(out.relative_to(self.repo)), prompt)
        self.assertIn("do not replay the mechanical steps", prompt)
        self.assertIn(TI.STAGING_NOTES, prompt)

    def test_with_no_staging_the_prompt_says_to_stage_it_itself(self):
        started = self.tryit_events(self.start())[0]
        self.assertFalse(started["reusing"])
        self.assertEqual(started["staged"], "")
        self.assertIn("No staged environment from the verifying session",
                      self.turns.submitted[-1]["prompt"])

    def test_a_staged_line_on_the_card_wins_over_the_glob(self):
        named = self.repo / "docs" / "qa_evidence" / "2026-09-01-verify-elsewhere"
        named.mkdir(parents=True)
        (named / TI.STAGE_SCRIPT).write_text("#!/bin/bash\n", encoding="utf-8")
        self.stage_verify_dir()
        self.write_section(f"staged: {named.relative_to(self.repo)}/\n"
                           "simulation: drove the gate with xdotool\n", heading="QA checklist")
        found = TI.verify_staging(self.repo, self.card,
                                  self.board.card_by_id(self.card).body)
        self.assertEqual(found["dir"], str(named.relative_to(self.repo)))
        self.assertIn("drove the gate", found["simulation"])
        self.assertIn("drove the gate", self.turns.submitted[-1]["prompt"]
                      if self.turns.submitted else TI.tryit_prompt(
                          self.commands.tools, self.card, self.evidence()))

    def test_a_verify_directory_without_a_stage_script_falls_through(self):
        self.stage_verify_dir(script=False)
        started = self.tryit_events(self.start())[0]
        self.assertFalse(started["reusing"])
        self.assertIn(f"no {TI.STAGE_SCRIPT} in it", self.turns.submitted[-1]["prompt"])

    def test_with_a_landed_commit_the_binary_comes_from_land_py_try(self):
        # Card #76QW: a Try-it turn never serves a stale shared binary. When the card has
        # landed commits, the binary is the newest one built by `land.py try --commit` in its
        # own verify slot; `build/relay` is only the fallback the turn names.
        slot = self.repo / "land" / "verify-slots" / "tryit" / "bin" / "relay"
        slot.parent.mkdir(parents=True)
        slot.write_text("#!/bin/sh\n", encoding="utf-8")
        (self.repo / "scripts").mkdir()
        (self.repo / "scripts" / "land.py").write_text("# stub for the guard\n",
                                                       encoding="utf-8")
        sha = "0123456789ab"
        card = self.board.card_by_id(self.card)
        card.set("links", {**(card.front.get("links") or {}), "commits": [sha]})
        self.board.save(card)
        ran = []

        def fake_land_try(argv, **kw):
            ran.append((argv, kw))
            return mock.Mock(returncode=0, stdout=f"{slot}\n", stderr="")

        with mock.patch("relay_core.tryit_protocol.subprocess.run", fake_land_try):
            self.assertEqual(TI._app_binary(self.repo, sha), slot)
            prompt = TI.tryit_prompt(self.commands.tools, self.card, self.evidence())
        self.assertEqual(ran[0][0],
                         [sys.executable, "scripts/land.py", "try", "tryit",
                          "--commit", sha, "--print-binary"])
        self.assertEqual(ran[0][1]["cwd"], self.repo)
        self.assertGreaterEqual(ran[0][1]["timeout"], 1800)  # a cold slot builds the tree
        self.assertIn(f"`land.py try --commit {sha} --print-binary`", prompt)
        self.assertIn(str(slot), prompt)
        # Card #J6MF: the slot binary is not rebuilt here, but it is still proven to hold the change.
        self.assertIn(f"--check-only --check", prompt)
        self.assertIn(f"--check-binary {slot}", prompt)
        self.assertNotIn("scripts/relay-build --check \"", prompt)

    def test_the_prompt_requires_proving_the_binary_holds_the_change(self):
        # Card #J6MF: session 3f4a20ad handed a person a build/relay linked before the change was
        # compiled. The turn builds build/relay only through scripts/relay-build, checks it for a
        # literal the change adds, and stops with the named failure when it is stale.
        with mock.patch("relay_core.tryit_protocol.subprocess.run",
                        side_effect=AssertionError("no commits, nothing builds")):
            prompt = TI.tryit_prompt(self.commands.tools, self.card, self.evidence())
        shared = self.repo / "build" / "relay"
        self.assertIn("build it only through `scripts/relay-build --check", prompt)
        self.assertIn(f"--check-only --check", prompt)
        self.assertIn(f"--check-binary {shared}", prompt)
        self.assertIn("binary predates the change", prompt)
        self.assertIn("Try it could not be staged: binary predates the change", prompt)
        brief = T.card_brief("tryit")
        self.assertIn("binary predates the change", brief)
        self.assertIn("scripts/relay-build --check-only", brief)

    def test_without_a_landed_commit_nothing_shells_out(self):
        self.start()
        with mock.patch("relay_core.tryit_protocol.subprocess.run",
                        side_effect=AssertionError(
                            "nothing builds when the card has no commits")):
            self.assertEqual(TI._app_binary(self.repo), self.repo / "build" / "relay")
            TI.tryit_prompt(self.commands.tools, self.card, self.evidence())


# ------------------------------------------------------------------ try_answer: the person's turn

class AnswerTests(TryItTest):
    def test_answers_preserve_existing_human_judgements(self):
        self.staged_section()
        prior = "1. Keep the layout?\n   Answer: Keep the old layout.\n\n2. Is it readable?"
        self.write_section(prior, heading="Human QA")
        self.answer("First answer.")
        self.answer("Second answer.")
        human = T.section_text(self.board.card_by_id(self.card).body, "Human QA")
        self.assertIn(prior, human)
        self.assertIn("Second answer.", human)
        self.assertNotIn("First answer.", human)
        self.assertEqual(human.count("relay:tryit-human start"), 1)


    def answer(self, text="It stopped me, and the notice said which test."):
        return self.send(type="try_answer", id="a1", card=self.card, answer=text)

    def test_the_answer_is_a_verdict_the_seal_breaks_and_human_qa_is_generated(self):
        expected = self.staged_section()
        events = self.answer()
        answered = self.tryit_events(events)[-1]
        self.assertEqual(answered["state"], "answered")
        self.assertTrue(answered["expected_revealed"])
        self.assertTrue(answered["human_qa"])

        # 1. their words, on the thread, quoted (policy rule 4)
        entries = [e for e in self.board.thread(self.card) if e.kind == "decision"]
        self.assertTrue(entries)
        self.assertIn("Try it · verdict", entries[-1].text)
        self.assertIn("did it stop you", entries[-1].text.lower())
        self.assertIn('"It stopped me, and the notice said which test."', entries[-1].text)

        # 2. the seal, broken under `## Try it` and only there
        body = self.board.card_by_id(self.card).body
        section = T.section_text(body, "Try it")
        self.assertIn("Expected: The gate refuses the move", section)
        self.assertIn(expected, section)          # the path is still named

        # 3. `## Human QA`, built from the section and the answer
        human = T.section_text(body, "Human QA")
        self.assertIn("did it stop you", human.lower())
        self.assertIn("It stopped me", human)
        self.assertIn("The gate refuses the move", human)
        self.assertIn("docs/qa_evidence", human)
        self.assertIn("Generated from", human)

    def test_the_answer_leaves_a_person_served_row_in_the_case_ledger(self):
        # #95VZ: the person's Try it answer is their signal on the case.
        from relay_core import cases
        self.staged_section()
        self.assertEqual(cases.read(self.root), [])
        self.answer()
        (row,) = cases.read(self.root, card=self.card)
        self.assertEqual((row["served_by"], row["signal"]["mode"]), ("person", "person"))
        self.assertEqual(row["input"], f"card #{self.card} Try it answer")
        self.assertEqual(row["verdict"]["result"], "pending")    # "It stopped me…" decides nothing in a word
        self.assertEqual(row["server"], f"card:{self.card}")

    def test_a_second_answer_refreshes_human_qa_and_does_not_reveal_twice(self):
        self.staged_section()
        self.answer()
        self.answer("Second time: it was obvious.")
        body = self.board.card_by_id(self.card).body
        section = T.section_text(body, "Try it")
        self.assertEqual(section.count("Expected: The gate refuses"), 1)
        human = T.section_text(body, "Human QA")
        self.assertIn("Second time: it was obvious.", human)
        self.assertEqual(human.count("Answer:"), 1)

    def test_an_answer_without_a_try_it_section_is_refused(self):
        refused = self.tryit_events(self.answer())[0]
        self.assertEqual(refused["state"], "error")
        self.assertIn("press Try it first", refused["message"])

    def test_an_empty_answer_is_refused(self):
        self.staged_section()
        refused = self.tryit_events(self.send(type="try_answer", id="a2", card=self.card,
                                              answer="   "))[0]
        self.assertEqual(refused["state"], "error")
        self.assertIn("what the person saw", refused["message"])

    def test_a_section_with_no_sealed_file_still_records_the_verdict(self):
        self.write_section("Open it: `bash /tmp/x/stage.sh`\n\nTry the thing.\n\nWas it clear?\n")
        answered = self.tryit_events(self.answer("No."))[-1]
        self.assertEqual(answered["state"], "answered")
        self.assertFalse(answered["expected_revealed"])
        human = T.section_text(self.board.card_by_id(self.card).body, "Human QA")
        self.assertNotIn("Expected:", human)
        self.assertIn("Answer: No.", human)


# ------------------------------------------------------------------ reading the section

class SectionTests(unittest.TestCase):
    """`parse_section`: shape, not grammar — the GUI's button and `## Human QA` read it."""

    def test_it_finds_the_command_the_task_the_question_and_the_sealed_path(self):
        parsed = TI.parse_section(
            "1. **Open it:** `bash docs/qa_evidence/2026-09-21-tryit-7BM4/stage.sh`\n"
            "2. Move the card in Needs verification to Done.\n"
            "3. Did it stop you, and did you know why?\n"
            "Expected: docs/qa_evidence/2026-09-21-tryit-7BM4/expected.md (sealed)\n")
        self.assertEqual(parsed["open"],
                         "bash docs/qa_evidence/2026-09-21-tryit-7BM4/stage.sh")
        self.assertEqual(parsed["open_kind"], "command")
        self.assertIn("Move the card", parsed["task"])
        self.assertEqual(parsed["question"], "Did it stop you, and did you know why?")
        self.assertEqual(parsed["expected_path"],
                         "docs/qa_evidence/2026-09-21-tryit-7BM4/expected.md")

    def test_a_relay_link_and_a_bare_path_are_both_openable(self):
        self.assertEqual(TI.parse_section("Open: `relay://card/7BM4`\nDo it.\nWell?")["open_kind"],
                         "link")
        self.assertEqual(TI.parse_section("Open: `docs/x/stage.sh`\nDo it.\nWell?")["open_kind"],
                         "path")

    def test_a_revealed_section_reads_back_its_expected_text(self):
        parsed = TI.parse_section("Open: `bash x.sh`\nDo it.\nWell?\n\nExpected: it refuses.\n")
        self.assertEqual(parsed["revealed"], "it refuses.")

    def test_prose_with_no_shape_is_still_the_task(self):
        parsed = TI.parse_section("Have a look at the new notice and tell me what you think.")
        self.assertEqual(parsed["open"], "")
        self.assertIn("Have a look", parsed["task"])


# ------------------------------------------------------------------ board_try: the agent's tool

class ToolTests(TryItTest):
    def test_board_try_hands_back_the_brief_and_records_a_progress_entry(self):
        result = self.agent_tools.run("board_try", {"card": self.card})
        self.assertNotIn("error", result)
        self.assertEqual(result["card"], self.card)
        self.assertIn("[Board Try it]", result["text"])
        self.assertIn("expected.md", result["text"])
        self.assertEqual(result["evidence_dir"],
                         f"docs/qa_evidence/{_today()}-tryit-{self.card}")
        self.assertFalse(result["reusing"])
        entries = [e for e in self.board.thread(self.card) if e.kind == "progress"]
        self.assertIn("preparing Try it", entries[-1].text)

    def test_board_try_says_when_the_verifying_session_already_staged_it(self):
        out = self.repo / "docs" / "qa_evidence" / f"{_today()}-verify-{self.card}"
        out.mkdir(parents=True)
        (out / TI.STAGE_SCRIPT).write_text("#!/bin/bash\n", encoding="utf-8")
        result = self.agent_tools.run("board_try", {"card": self.card})
        self.assertTrue(result["reusing"])
        self.assertIn(TI.STAGE_SCRIPT, result["staged"])
        self.assertIn("reusing the staging", self.board.thread(self.card)[-1].text)

    def test_board_try_is_refused_during_a_cleanup_exactly_as_board_claim_is(self):
        self.agent_tools.begin_cleanup("c-1")
        refused = self.agent_tools.run("board_try", {"card": self.card})
        self.assertEqual(refused["code"], "board_refused")
        self.assertIn("not available during a Board cleanup", refused["error"])
        claim = self.agent_tools.run("board_claim", {"id": self.card})
        self.assertEqual(claim["code"], "board_refused")

    def test_an_unknown_card_is_refused(self):
        self.assertEqual(self.agent_tools.run("board_try", {"card": "ZZZZ"})["code"],
                         "board_not_found")
        self.assertIn("board_try takes card",
                      self.agent_tools.run("board_try", {"card": self.card, "x": 1})["error"])

    def test_the_tool_is_in_the_set_every_turn_carries(self):
        self.assertIn("board_try", T.TOOL_NAMES)
        self.assertTrue(self.agent_tools.handles("board_try"))


def _today():
    import datetime
    return datetime.date.today().isoformat()


if __name__ == "__main__":       # pragma: no cover
    unittest.main()
