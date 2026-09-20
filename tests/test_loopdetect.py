# SPDX-License-Identifier: AGPL-3.0-or-later
"""Deterministic loop detection (card #2CZP): every pattern fires at its threshold and not one
below, the shapes the module's docstring promises to forgive stay silent over long runs, and the
nudge, stop and double-check texts say what that docstring says they say.

Pure: the one provider call (``run_check``) is stubbed here and never made."""
import unittest
from unittest import mock

from relay_core import loopdetect
from relay_core.loopdetect import Detector, Pattern, make_call

OK = {"stdout": "ok", "exit_code": 0}
FAIL = {"error": "cc: no such file: main.c", "exit_code": 1}


def call(tool="run_command", args=None, result=None):
    return make_call(tool, {"command": "make"} if args is None else args,
                     OK if result is None else result)


def fired(patterns):
    """Just the patterns that fired, so a twenty-observation run reads as a short list."""
    return [p for p in patterns if p is not None]


def kinds(patterns):
    return [p.kind for p in fired(patterns)]


def feed(det, calls):
    return [det.observe_call(c) for c in calls]


class RepeatTests(unittest.TestCase):
    def test_three_identical_calls_are_not_a_loop_and_the_fourth_is(self):
        det = Detector()
        self.assertEqual(feed(det, [call()] * 3), [None, None, None])
        pattern = det.observe_call(call())
        self.assertEqual((pattern.kind, pattern.count, pattern.tool),
                         ("repeat", loopdetect.SAME_RESULT_REPEATS, "run_command"))
        self.assertEqual(loopdetect.SAME_RESULT_REPEATS, 4)

    def test_a_differing_result_breaks_the_run(self):
        # Same command, one line of new output: the agent learned something, so the count restarts.
        det = Detector()
        feed(det, [call()] * 3)
        self.assertIsNone(det.observe_call(call(result={"stdout": "ok, one more", "exit_code": 0})))
        self.assertIsNone(det.observe_call(call()))

    def test_a_different_tool_with_the_same_args_is_not_the_same_call(self):
        det = Detector()
        feed(det, [call()] * 3)
        self.assertIsNone(det.observe_call(call(tool="run_shell")))


class ErrorTests(unittest.TestCase):
    def test_two_failures_are_not_a_loop_and_the_third_is(self):
        det = Detector()
        self.assertEqual(feed(det, [call(result=FAIL)] * 2), [None, None])
        pattern = det.observe_call(call(result=FAIL))
        self.assertEqual((pattern.kind, pattern.count, pattern.tool),
                         ("error", loopdetect.SAME_ERROR_REPEATS, "run_command"))
        self.assertEqual(loopdetect.SAME_ERROR_REPEATS, 3)

    def test_it_fires_when_the_results_differ_but_each_one_failed(self):
        # A failure usually carries a varying detail (a pid, a temp path), so only (tool, args) match.
        det = Detector()
        results = [{"error": f"connection refused (attempt {n})", "exit_code": 1} for n in range(3)]
        got = feed(det, [call(result=r) for r in results])
        self.assertEqual(kinds(got), ["error"])
        self.assertIsNotNone(got[2].detail)
        self.assertIn("connection refused", got[2].detail)

    def test_a_non_zero_exit_code_is_a_failure_and_zero_is_not(self):
        det = Detector()
        self.assertEqual(kinds(feed(det, [call(result={"stdout": "boom", "exit_code": 2})] * 3)),
                         ["error"])
        quiet = Detector()
        # exit_code 0 three times running is not an error run; it only becomes a "repeat" at four.
        self.assertEqual(feed(quiet, [call(result={"stdout": "boom", "exit_code": 0})] * 3),
                         [None, None, None])
        self.assertEqual(quiet.observe_call(call(result={"stdout": "boom", "exit_code": 0})).kind,
                         "repeat")

    def test_differing_arguments_do_not_make_an_error_run(self):
        det = Detector()
        got = feed(det, [call(args={"command": f"make target{n}"}, result=FAIL) for n in range(3)])
        self.assertEqual(kinds(got), [])

    def test_the_detail_quotes_the_failure(self):
        det = Detector()
        pattern = fired(feed(det, [call(result=FAIL)] * 3))[0]
        self.assertIn("no such file", pattern.detail)


class CycleTests(unittest.TestCase):
    A = ("run_command", {"command": "make"})
    B = ("read_file", {"path": "src/main.c"})
    C = ("write_file", {"path": "src/main.c", "content": "int main(){}"})

    def run_of(self, pairs):
        return [call(tool=t, args=a) for t, a in pairs]

    def test_five_calls_of_a_two_call_cycle_are_not_a_loop_and_the_sixth_is(self):
        det = Detector()
        seq = [self.A, self.B] * 3
        self.assertEqual(feed(det, self.run_of(seq[:5])), [None] * 5)
        pattern = det.observe_call(self.run_of(seq[5:6])[0])
        self.assertEqual((pattern.kind, pattern.count), ("cycle", loopdetect.CYCLE_CALLS))
        self.assertEqual(loopdetect.CYCLE_CALLS, 6)
        self.assertIn("2-call cycle", pattern.detail)
        self.assertIn("run_command", pattern.detail)
        self.assertIn("read_file", pattern.detail)

    def test_a_three_call_cycle_fires_at_the_sixth_call(self):
        det = Detector()
        seq = [self.A, self.B, self.C] * 2
        self.assertEqual(feed(det, self.run_of(seq[:5])), [None] * 5)
        pattern = det.observe_call(self.run_of(seq[5:6])[0])
        self.assertEqual(pattern.kind, "cycle")
        self.assertIn("3-call cycle", pattern.detail)

    def test_six_identical_calls_are_a_repeat_not_a_cycle(self):
        # The module promises this precedence explicitly: repeat is checked first and wins.
        det = Detector()
        self.assertEqual(kinds(feed(det, [call()] * 6)), ["repeat"])

    def test_a_broken_cycle_does_not_fire(self):
        det = Detector()
        seq = [self.A, self.B, self.A, self.C, self.A, self.B]
        self.assertEqual(kinds(feed(det, self.run_of(seq))), [])


class MonologueTests(unittest.TestCase):
    def test_two_identical_messages_are_not_a_loop_and_the_third_is(self):
        det = Detector()
        self.assertIsNone(det.observe_message("Let me try that again.", False))
        self.assertIsNone(det.observe_message("Let me try that again.", False))
        pattern = det.observe_message("Let me try that again.", False)
        self.assertEqual((pattern.kind, pattern.count, pattern.tool),
                         ("monologue", loopdetect.MONOLOGUE_REPEATS, ""))
        self.assertEqual(loopdetect.MONOLOGUE_REPEATS, 3)
        self.assertIn("Let me try that again.", pattern.detail)

    def test_a_message_that_calls_tools_ends_the_run(self):
        det = Detector()
        det.observe_message("Thinking.", False)
        det.observe_message("Thinking.", False)
        self.assertIsNone(det.observe_message("Thinking.", True))
        self.assertIsNone(det.observe_message("Thinking.", False))
        self.assertIsNone(det.observe_message("Thinking.", False))
        self.assertEqual(det.observe_message("Thinking.", False).kind, "monologue")

    def test_a_different_message_restarts_the_run(self):
        det = Detector()
        det.observe_message("Thinking.", False)
        det.observe_message("Thinking.", False)
        self.assertIsNone(det.observe_message("Something else.", False))
        self.assertIsNone(det.observe_message("Thinking.", False))
        self.assertIsNone(det.observe_message("Thinking.", False))
        self.assertEqual(det.observe_message("Thinking.", False).kind, "monologue")

    def test_a_tool_call_between_messages_resets_the_run(self):
        # The model is acting again, so what it said before is no longer an unbroken run of talk.
        det = Detector()
        det.observe_message("Thinking.", False)
        det.observe_message("Thinking.", False)
        det.observe_call(call())
        self.assertIsNone(det.observe_message("Thinking.", False))

    def test_messages_differing_only_in_whitespace_are_the_same_message(self):
        det = Detector()
        self.assertIsNone(det.observe_message("Let me try\nthat again.", False))
        self.assertIsNone(det.observe_message("Let me try that again.  ", False))
        self.assertEqual(det.observe_message("  Let me try  that   again.", False).kind, "monologue")

    def test_a_run_of_messages_does_not_disturb_the_call_history(self):
        # A model that narrates between two identical commands is still running identical commands.
        det = Detector()
        for _ in range(3):
            det.observe_call(call())
            det.observe_message("Still building.", False)
        self.assertEqual(det.observe_call(call()).kind, "repeat")

    def test_an_empty_message_says_nothing_either_way(self):
        det = Detector()
        det.observe_message("Thinking.", False)
        det.observe_message("Thinking.", False)
        self.assertIsNone(det.observe_message("   ", False))
        self.assertIsNone(det.observe_message("Thinking.", False))


class WhitelistTests(unittest.TestCase):
    """The shapes the module exists to forgive. Each run is long enough to be convincing."""

    def test_batch_operations_across_many_files(self):
        # `write_file` over 24 paths: identical result every time, but each call names a new path.
        det = Detector()
        got = feed(det, [call(tool="write_file", args={"path": f"src/mod{n}.py", "content": "x"},
                              result={"ok": True}) for n in range(24)])
        self.assertEqual((len(got), kinds(got)), (24, []))

    def test_incremental_edits_to_one_file(self):
        # Same tool, same path, 22 times — the content differs, which is the whole of the work.
        det = Detector()
        got = feed(det, [call(tool="edit_file", args={"path": "src/main.c", "content": f"line {n}"},
                              result={"ok": True}) for n in range(22)])
        self.assertEqual((len(got), kinds(got)), (22, []))

    def test_retry_with_variation(self):
        # 20 failing retries: every one fails, but every one varies, so it is not the error run.
        det = Detector()
        got = feed(det, [call(args={"command": f"curl --retry-delay {n} https://host/x"},
                              result=FAIL) for n in range(20)])
        self.assertEqual((len(got), kinds(got)), (20, []))

    def test_re_running_a_build_after_an_edit(self):
        # run -> edit -> run -> edit ..., the build failing identically every time. It is the
        # interleaving that saves this one, so the next test drops the edits and expects a fire.
        det = Detector()
        got = feed(det, self.build_edit_run(12))
        self.assertEqual((len(got), kinds(got)), (24, []))

    def test_the_same_build_runs_without_the_edits_between_them_do_fire(self):
        # Proof that the previous test is testing what it claims: the runs alone are a loop.
        det = Detector()
        runs = [c for c in self.build_edit_run(12) if c.tool == "run_command"]
        self.assertEqual(len(runs), 12)
        self.assertEqual(kinds(feed(det, runs)), ["error"] * 4)

    @staticmethod
    def build_edit_run(rounds):
        calls = []
        for n in range(rounds):
            calls.append(call(args={"command": "make"}, result=FAIL))
            calls.append(call(tool="edit_file", args={"path": "src/main.c", "content": f"try {n}"},
                              result={"ok": True}))
        return calls

    def test_polling_a_job_whose_result_changes(self):
        # 24 polls of the same job id: identical call, but the answer moves every time.
        det = Detector()
        got = feed(det, [call(tool="job_status", args={"id": "job-7"},
                              result={"status": "running", "lines_done": n, "tail": f"step {n}"})
                         for n in range(24)])
        self.assertEqual((len(got), kinds(got)), (24, []))

    def test_a_poll_whose_answer_never_changes_is_caught(self):
        # The docstring's own carve-out: a byte-identical poll four times running is the loop.
        det = Detector()
        got = feed(det, [call(tool="job_status", args={"id": "job-7"},
                              result={"status": "running", "tail": "step 3"}) for n in range(4)])
        self.assertEqual(kinds(got), ["repeat"])


class HashingTests(unittest.TestCase):
    def test_key_order_cannot_change_the_args_hash(self):
        self.assertEqual(make_call("t", {"a": 1, "b": 2}, OK).args_hash,
                         make_call("t", {"b": 2, "a": 1}, OK).args_hash)

    def test_a_result_differing_only_in_how_long_it_took_is_the_same_result(self):
        base = {"stdout": "built", "exit_code": 0, "ms": 120, "pid": 4111,
                "stats": {"elapsed": 1.5, "files": 3}}
        slower = {"stdout": "built", "exit_code": 0, "ms": 4310, "pid": 9921,
                  "stats": {"elapsed": 88.25, "files": 3}, "duration_ms": 4310}
        self.assertEqual(make_call("t", {}, base).result_hash, make_call("t", {}, slower).result_hash)

    def test_a_result_differing_in_real_output_is_a_different_result(self):
        base = {"stdout": "built", "exit_code": 0, "ms": 120}
        other = {"stdout": "built 1 target", "exit_code": 0, "ms": 120}
        self.assertNotEqual(make_call("t", {}, base).result_hash, make_call("t", {}, other).result_hash)

    def test_a_volatile_key_is_dropped_at_any_depth(self):
        deep = {"a": [{"b": {"timestamp": "2026-09-20T10:00:00Z", "out": "x"}}]}
        later = {"a": [{"b": {"timestamp": "2026-09-20T18:32:11Z", "out": "x"}}]}
        self.assertEqual(make_call("t", {}, deep).result_hash, make_call("t", {}, later).result_hash)

    def test_trailing_whitespace_on_a_result_string_does_not_change_the_hash(self):
        # A shell that sometimes adds a final newline would otherwise hide a perfect loop.
        self.assertEqual(make_call("t", {}, {"stdout": "done"}).result_hash,
                         make_call("t", {}, {"stdout": "done  \n"}).result_hash)
        self.assertEqual(make_call("t", {}, "done").result_hash,
                         make_call("t", {}, "done\n").result_hash)

    def test_non_dict_arguments_hash_stably(self):
        for args in ("a string", ["a", "list"], None, 17):
            with self.subTest(args=args):
                self.assertEqual(make_call("t", args, OK).args_hash, make_call("t", args, OK).args_hash)
        distinct = {make_call("t", a, OK).args_hash for a in ("a string", ["a", "list"], None, 17)}
        self.assertEqual(len(distinct), 4)

    def test_something_unserialisable_does_not_raise(self):
        class Opaque:
            def __repr__(self):
                return "<Opaque fixed>"

        circular = {"self": None}
        circular["self"] = circular
        deep = current = {}
        for _ in range(3000):                      # past the interpreter's own recursion limit
            current["n"] = {}
            current = current["n"]
        # A result is whatever object the tool returned, not necessarily JSON. Bookkeeping on it
        # must not be able to raise inside a turn: SCRUB_DEPTH stops the walk and the value hashes
        # as its repr instead.
        for args, result in ((Opaque(), OK), ({"obj": Opaque()}, OK), ({}, {"obj": Opaque()}),
                             ({"fn": len}, {"set": {1, 2}}), (circular, OK),
                             ({}, circular), ({}, deep), (deep, OK)):
            with self.subTest(args=repr(args)[:20]):
                made = make_call("t", args, result)
                self.assertTrue(made.args_hash and made.result_hash)
        # And it is still a hash, not a constant: what is visible before the cap still separates
        # two deep results. ("a" sorts first, so it lands inside the head the cap keeps.)
        self.assertNotEqual(make_call("t", {}, {"a": 1, "n": deep}).result_hash,
                            make_call("t", {}, {"a": 2, "n": deep}).result_hash)

    def test_the_head_of_a_very_large_argument_is_what_is_kept(self):
        head_a = {"path": "big.txt", "content": "A" + "z" * 100_000}
        head_b = {"path": "big.txt", "content": "B" + "z" * 100_000}
        self.assertNotEqual(make_call("write_file", head_a, OK).args_hash,
                            make_call("write_file", head_b, OK).args_hash)
        # ... and only the head: two writes differing past the cap are one call to the detector.
        tail_a = {"path": "big.txt", "content": "z" * 100_000 + "A"}
        tail_b = {"path": "big.txt", "content": "z" * 100_000 + "B"}
        self.assertEqual(make_call("write_file", tail_a, OK).args_hash,
                         make_call("write_file", tail_b, OK).args_hash)

    def test_the_note_is_never_part_of_a_call_identity(self):
        det = Detector()
        # Same tool, same args, same scrubbed result; only the quoted error line differs.
        got = feed(det, [call(result={"error": "boom", "exit_code": 1}),
                         call(result={"error": "boom", "exit_code": 1}),
                         call(result={"error": "boom", "exit_code": 1})])
        self.assertEqual(kinds(got), ["error"])


class LifecycleTests(unittest.TestCase):
    def test_a_fired_pattern_forgets_the_run(self):
        # Eight identical calls fire at the 4th and the 8th — not on every call after the 4th.
        det = Detector()
        got = feed(det, [call()] * 8)
        self.assertEqual([i for i, p in enumerate(got) if p], [3, 7])
        self.assertEqual(kinds(got), ["repeat", "repeat"])

    def test_clear_forgets_everything(self):
        det = Detector()
        feed(det, [call()] * 3)
        det.clear()
        self.assertEqual(feed(det, [call()] * 3), [None, None, None])

    def test_clear_after_a_fire_leaves_nothing_behind(self):
        det = Detector()
        self.assertEqual(kinds(feed(det, [call()] * 4)), ["repeat"])
        det.clear()
        self.assertEqual(feed(det, [call()] * 3), [None, None, None])

    def test_clear_forgets_a_message_run_too(self):
        det = Detector()
        det.observe_message("Thinking.", False)
        det.observe_message("Thinking.", False)
        det.clear()
        self.assertIsNone(det.observe_message("Thinking.", False))

    def test_two_detectors_are_independent(self):
        one, two = Detector(), Detector()
        feed(one, [call()] * 3)
        self.assertEqual(feed(two, [call()] * 3), [None, None, None])
        self.assertEqual(one.observe_call(call()).kind, "repeat")


PATTERNS = {
    "repeat": Pattern(kind="repeat", count=4, tool="run_command",
                      detail="identical arguments, identical result"),
    "error": Pattern(kind="error", count=3, tool="run_command", detail="cc: no such file: main.c"),
    "cycle": Pattern(kind="cycle", count=6, tool="run_command",
                     detail="2-call cycle: run_command -> read_file"),
    "monologue": Pattern(kind="monologue", count=3, tool="",
                         detail="Let me try that again."),
}


class NudgeTests(unittest.TestCase):
    def test_every_kind_is_a_short_bracketed_relay_note(self):
        for kind, pattern in PATTERNS.items():
            with self.subTest(kind=kind):
                text = loopdetect.nudge_text(pattern, 1)
                self.assertTrue(text.startswith("[Relay note:"), text)
                self.assertTrue(text.endswith("]"), text)
                self.assertLess(len(text), 400, f"{kind}: {len(text)} chars")

    def test_a_nudge_stays_one_paragraph_even_at_the_longest_detail(self):
        # A detail arrives capped at NOTE_CAP, and every kind that quotes one quotes DETAIL_CAP of
        # it, so the widest nudge any pattern can produce is still one short paragraph.
        for kind in PATTERNS:
            with self.subTest(kind=kind):
                wide = Pattern(kind=kind, count=6, tool="run_command", detail="E" * loopdetect.NOTE_CAP)
                self.assertLess(len(loopdetect.nudge_text(wide, loopdetect.MAX_NUDGES)), 400)

    def test_a_tool_pattern_names_the_tool(self):
        for kind in ("repeat", "error", "cycle"):
            with self.subTest(kind=kind):
                self.assertIn("run_command", loopdetect.nudge_text(PATTERNS[kind], 1))

    def test_a_monologue_does_not_claim_a_tool(self):
        text = loopdetect.nudge_text(PATTERNS["monologue"], 1)
        self.assertNotIn("the same call", text)
        self.assertIn("no tool call", text)

    def test_the_last_nudge_says_the_turn_will_be_stopped_and_the_first_does_not(self):
        first = loopdetect.nudge_text(PATTERNS["repeat"], 1)
        last = loopdetect.nudge_text(PATTERNS["repeat"], loopdetect.MAX_NUDGES)
        self.assertNotIn("stopped", first)
        self.assertIn("stopped", last)
        self.assertIn("last reminder", last)
        self.assertEqual(loopdetect.MAX_NUDGES, 2)

    def test_every_nudge_asks_for_a_decision_rather_than_scolding(self):
        text = loopdetect.nudge_text(PATTERNS["error"], 1)
        self.assertIn("Change approach", text)
        self.assertIn("blocked", text)


class StopTests(unittest.TestCase):
    def test_it_names_the_repeated_call_and_says_the_request_is_not_finished(self):
        text = loopdetect.stop_text(PATTERNS["repeat"])
        self.assertTrue(text.startswith("Stopped:"), text)
        self.assertIn("run_command", text)
        self.assertIn("4 times in a row", text)
        self.assertIn("not finished", text)
        self.assertIn("continue", text)

    def test_it_counts_the_reminders_that_were_given(self):
        self.assertIn("two reminders", loopdetect.stop_text(PATTERNS["error"]))


class CheckInputTests(unittest.TestCase):
    def test_it_carries_the_pattern_kind_and_what_was_seen(self):
        text = loopdetect.check_input(PATTERNS["error"], ["ran make", "ran make"])
        self.assertIn("error", text)
        self.assertIn(loopdetect.describe(PATTERNS["error"]), text)
        self.assertIn("- ran make", text)

    def test_it_keeps_the_last_few_actions_of_a_longer_list(self):
        text = loopdetect.check_input(PATTERNS["repeat"], [f"action {n}" for n in range(30)])
        items = [line for line in text.splitlines() if line.startswith("- ")]
        self.assertEqual(len(items), loopdetect.CHECK_RECENT_MAX)
        self.assertEqual(items[-1], "- action 29")
        self.assertEqual(items[0], f"- action {30 - loopdetect.CHECK_RECENT_MAX}")
        self.assertNotIn("- action 17", text)

    def test_each_line_is_capped_and_flattened(self):
        text = loopdetect.check_input(PATTERNS["repeat"], ["x" * 900, "two\nlines\there"])
        items = [line for line in text.splitlines() if line.startswith("- ")]
        self.assertEqual(len(items[0]), loopdetect.CHECK_RECENT_CAP + 2)
        self.assertEqual(items[1], "- two lines here")

    def test_an_empty_list_says_so(self):
        for recent in ([], None):
            with self.subTest(recent=recent):
                text = loopdetect.check_input(PATTERNS["repeat"], recent)
                self.assertIn("(none recorded)", text)

    def test_non_string_items_do_not_break_it(self):
        text = loopdetect.check_input(PATTERNS["repeat"], [{"tool": "make"}, 7])
        self.assertIn("make", text)
        self.assertIn("- 7", text)


class ParseCheckTests(unittest.TestCase):
    def test_the_two_verdicts(self):
        self.assertIs(loopdetect.parse_check("loop"), True)
        self.assertIs(loopdetect.parse_check("productive"), False)
        self.assertIs(loopdetect.parse_check("LOOP\n"), True)

    def test_the_last_line_carrying_a_verdict_wins(self):
        # A model that reasons first puts its answer at the end.
        self.assertIs(loopdetect.parse_check("It keeps running make.\nloop"), True)
        self.assertIs(loopdetect.parse_check("Same file, new content each time.\nproductive"), False)
        self.assertIs(loopdetect.parse_check("loop\nproductive"), False)

    def test_a_line_naming_both_is_ambiguous_rather_than_a_guess(self):
        self.assertIsNone(loopdetect.parse_check("This is a loop, not productive repetition."))

    def test_junk_is_unusable(self):
        for reply in ("", "   ", None, "maybe", "42", "I could not tell."):
            with self.subTest(reply=reply):
                self.assertIsNone(loopdetect.parse_check(reply))

    def test_the_word_must_be_a_word(self):
        # "looping" is not "loop"; a verdict is one word on its own.
        self.assertIsNone(loopdetect.parse_check("the agent is looping-ish"))


class RunCheckTests(unittest.TestCase):
    """The one side call. Stubbed: this test never touches a provider or a network."""

    def test_it_sends_the_check_system_prompt_and_the_rendered_input(self):
        seen = {}

        def stub(provider, system, user, cancel=None):
            seen.update(provider=provider, system=system, user=user, cancel=cancel)
            return "productive", None

        provider, cancel = object(), object()
        recent = ["ran make", "ran make", "ran make"]
        with mock.patch.object(loopdetect.sidecall, "call", stub):
            verdict = loopdetect.run_check(provider, PATTERNS["error"], recent, cancel)
        self.assertIs(verdict, False)
        self.assertIs(seen["provider"], provider)
        self.assertIs(seen["cancel"], cancel)
        self.assertEqual(seen["system"], loopdetect.CHECK_SYSTEM)
        self.assertEqual(seen["user"], loopdetect.check_input(PATTERNS["error"], recent))

    def test_the_reply_goes_through_parse_check(self):
        for reply, expected in (("loop", True), ("productive", False), ("no idea", None),
                                ("It stalled.\nloop", True)):
            with self.subTest(reply=reply):
                with mock.patch.object(loopdetect.sidecall, "call",
                                       lambda *a, **k: (reply, None)):
                    self.assertIs(loopdetect.run_check(object(), PATTERNS["repeat"], []), expected)

    def test_the_system_prompt_names_the_whitelisted_shapes_and_distrusts_its_input(self):
        for phrase in ("batch operations", "incremental edits", "retrying with a variation",
                       "re-running a build", "polling a job", "untrusted data"):
            with self.subTest(phrase=phrase):
                self.assertIn(phrase, loopdetect.CHECK_SYSTEM)


if __name__ == "__main__":
    unittest.main()
