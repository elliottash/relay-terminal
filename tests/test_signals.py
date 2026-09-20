# SPDX-License-Identifier: AGPL-3.0-or-later
"""`relay_core.signals`: the fold, every threshold, and the two files it reads (card #AQ6X).

The whole state machine is one pure function — `fold(executions, events, now)` — so this module
is mostly one table: a sequence of executions and actions in, one expected state out.  That is
deliberate.  Each rule on card #AQ6X's Decisions list is a row here, named after the decision,
and the four thresholds the research marks as guesses are exercised through the constants rather
than through literals, so tuning them changes one number in one place and this suite still holds.

Nothing here runs a test, a model, a subprocess or the clock: `now` is an argument.
"""
import json
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))

from relay_core import signals as S                 # noqa: E402
from relay_core import test_history as H            # noqa: E402

NOW = "2026-09-20T12:00:00Z"


def day(d: int, hour: int = 10, minute: int = 0) -> str:
    return f"2026-09-{d:02d}T{hour:02d}:{minute:02d}:00Z"


def ex(ts, key, result, *, run="r", commit="c1", tree="", message="", excerpt="",
       duration=0.1) -> H.Execution:
    """One execution, with the two fields the fold reads that the wire contract does not name."""
    return H.Execution(ts=ts, id=key, result=result, run_id=run, commit=commit,
                       runner=key.split(":", 1)[0], message=message, excerpt=excerpt,
                       duration=duration, tree_digest=tree)


def fold(executions, events=(), now=NOW, **kw):
    return S.fold(executions, events, now, **kw)


# --------------------------------------------------------------------------- the fingerprint

class FingerprintTests(unittest.TestCase):
    """R2: the message with numbers, hex, absolute paths and timestamps replaced by `%`."""

    def test_the_four_replacements_happen_and_punctuation_between_them_collapses(self):
        self.assertEqual(
            S.fingerprint("AssertionError: 3 != 17 at /home/x/y/z.py:44 on "
                          "2026-09-20T11:03:12Z (7b80f963)"),
            "AssertionError: % != % at % on % (%)")

    def test_two_failures_that_differ_only_in_their_numbers_fingerprint_alike(self):
        self.assertEqual(S.fingerprint("expected 3, got 17"), S.fingerprint("expected 1, got 2"))

    def test_a_word_made_of_hex_letters_is_still_a_word(self):
        # `decade` is six hex characters and no digits; a commit-looking run needs a digit.
        self.assertIn("decade", S.fingerprint("the decade of deadbeef1 failures"))
        self.assertNotIn("deadbeef1", S.fingerprint("the decade of deadbeef1 failures"))

    def test_an_empty_message_has_no_fingerprint_and_a_long_one_is_bounded(self):
        self.assertEqual(S.fingerprint(""), "")
        self.assertEqual(S.fingerprint(None), "")
        self.assertLessEqual(len(S.fingerprint("word " * 500)), S.MAX_FINGERPRINT)

    def test_the_group_key_is_stable_and_names_the_source(self):
        self.assertEqual(S.group_key("a"), S.group_key("a"))
        self.assertNotEqual(S.group_key("a"), S.group_key("b"))
        self.assertEqual(S.source_of(S.group_key("a")), "group")
        self.assertEqual(S.source_of("unittest:tests.test_board.T.test_x"), "unittest")


# --------------------------------------------------------------------------- opening and closing

class StateMachineTests(unittest.TestCase):
    """Decisions 3 and 4, one row per rule.  `key` is always `ctest:a` unless it matters."""

    def one(self, executions, events=(), now=NOW, key="ctest:a", **kw):
        return fold(executions, events, now, **kw).get(key)

    # ---- decision 3: pending on the first failure, open on the second consecutive one
    def test_the_first_failing_execution_is_pending_and_nothing_else(self):
        signal = self.one([ex(day(1), "ctest:a", "fail", message="boom")])
        self.assertEqual(signal.state, "pending")
        self.assertEqual(signal.count, 1)
        self.assertEqual(signal.kind, "broken")
        self.assertEqual(signal.first_seen, day(1))
        self.assertEqual(signal.fingerprint, "boom")
        self.assertFalse(signal.is_open)
        self.assertFalse(signal.blocks_verification)

    def test_the_second_consecutive_failing_execution_opens_it(self):
        signal = self.one([ex(day(1), "ctest:a", "fail"), ex(day(2), "ctest:a", "fail", run="r2")])
        self.assertEqual(signal.state, "open")
        self.assertEqual(signal.count, 2)
        self.assertEqual(signal.opened_run, "r2")
        self.assertTrue(signal.blocks_verification)

    def test_a_pass_between_two_failures_deletes_the_pending_one_without_trace(self):
        signal = self.one([ex(day(1), "ctest:a", "fail"),
                           ex(day(2), "ctest:a", "pass", run="r2", commit="c2"),
                           ex(day(3), "ctest:a", "fail", run="r3", commit="c3")])
        self.assertEqual((signal.state, signal.count), ("pending", 1))
        self.assertEqual(signal.first_seen, day(3))

    def test_a_pass_alone_leaves_no_signal_at_all(self):
        self.assertEqual(fold([ex(day(1), "ctest:a", "pass")]), {})

    # ---- decision 4: a run that did not execute the key counts as nothing
    def test_a_run_that_did_not_execute_the_key_advances_nothing(self):
        signal = self.one([ex(day(1), "ctest:a", "fail"),
                           ex(day(2), "ctest:b", "pass", run="r2"),
                           ex(day(3), "ctest:a", "fail", run="r3", commit="c3")])
        self.assertEqual((signal.state, signal.count), ("open", 2))

    def test_skip_and_timeout_are_not_evaluated(self):
        for result in S.NOT_EVALUATED:
            with self.subTest(result=result):
                signal = self.one([ex(day(1), "ctest:a", "fail"),
                                   ex(day(2), "ctest:a", result, run="r2"),
                                   ex(day(3), "ctest:a", "fail", run="r3", commit="c3")])
                self.assertEqual((signal.state, signal.count), ("open", 2))
                # …and a skip alone never opens anything.
                self.assertEqual(fold([ex(day(1), "ctest:a", result)]), {})

    def test_an_error_result_counts_as_a_failure(self):
        signal = self.one([ex(day(1), "ctest:a", "error"),
                           ex(day(2), "ctest:a", "error", run="r2", commit="c2")])
        self.assertEqual(signal.state, "open")

    # ---- decision 4: resolve on 2 (broken) or 20 (flaky) consecutive passes
    def test_a_broken_signal_resolves_on_two_consecutive_passes_and_records_the_commit(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(3), "ctest:a", "pass", run="r3", commit="c3")]
        self.assertEqual(self.one(rows).state, "open")          # one pass is not enough
        rows.append(ex(day(4), "ctest:a", "pass", run="r4", commit="c4"))
        signal = self.one(rows)
        self.assertEqual(signal.state, "resolved")
        self.assertEqual(signal.fixed_in, "c4")
        self.assertEqual(signal.resolved_at, day(4))
        self.assertEqual(S.RESOLVE_PASSES["broken"], 2)

    def test_a_flaky_signal_needs_twenty_passes_where_a_broken_one_needed_two(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="cc", tree="t1"),
                ex(day(1), "ctest:a", "pass", run="r1b", commit="cc", tree="t1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(2), "ctest:a", "fail", run="r2b", commit="c2")]
        signal = self.one(rows)
        self.assertEqual((signal.kind, signal.state), ("flaky", "open"))
        for index in range(S.RESOLVE_PASSES["flaky"] - 1):
            rows.append(ex(day(3, minute=index), "ctest:a", "pass",
                           run=f"p{index}", commit=f"g{index}"))
        self.assertEqual(self.one(rows).state, "open")
        rows.append(ex(day(4), "ctest:a", "pass", run="last", commit="glast"))
        self.assertEqual(self.one(rows).state, "resolved")
        self.assertEqual(S.RESOLVE_PASSES["flaky"], 20)

    # ---- the flaky mark itself
    def test_a_pass_and_a_fail_on_one_tree_is_flaky_and_stays_pending(self):
        # Datadog's rule, read over (commit, tree_digest): a fail-pass in Relay's own re-run.
        signal = self.one([ex(day(1), "ctest:a", "fail", commit="cc", tree="t1"),
                           ex(day(1), "ctest:a", "pass", run="r1b", commit="cc", tree="t1")])
        self.assertEqual((signal.kind, signal.state), ("flaky", "pending"))

    def test_a_pass_and_a_fail_at_one_commit_on_two_trees_is_not_flaky(self):
        # Why `tree_digest` exists (#AQ6X step 1): this shared checkout's commit is not its tree.
        signal = self.one([ex(day(1), "ctest:a", "fail", commit="cc", tree="broken"),
                           ex(day(2), "ctest:a", "pass", run="r2", commit="cc", tree="")])
        self.assertIsNone(signal)

    def test_a_test_that_only_ever_fails_is_broken_not_flaky(self):
        rows = [ex(day(d), "ctest:a", "fail", run=f"r{d}", commit=f"c{d}") for d in range(1, 8)]
        self.assertEqual(self.one(rows).kind, "broken")

    def test_a_test_that_flips_enough_times_opens_as_flaky_on_the_score(self):
        rows = [ex(day(d), "ctest:a", "fail" if d % 2 else "pass", run=f"r{d}", commit=f"c{d}")
                for d in range(1, 15)]
        signal = self.one(rows)
        self.assertEqual((signal.kind, signal.state), ("flaky", "open"))

    # ---- regression (R8)
    def test_a_regression_inside_thirty_days_reopens_the_same_signal_skipping_pending(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(3), "ctest:a", "pass", run="r3", commit="c3"),
                ex(day(4), "ctest:a", "pass", run="r4", commit="c4"),
                ex(day(10), "ctest:a", "fail", run="r5", commit="c5")]
        signal = self.one(rows)
        self.assertEqual(signal.state, "open")                 # one failure, no pending tier
        self.assertTrue(signal.regressed)
        self.assertEqual(signal.count, 3)                      # it keeps its count
        self.assertIsNone(signal.previous)
        self.assertEqual(signal.first_seen, day(1))

    def test_a_regression_past_thirty_days_is_a_new_signal_that_names_the_old_one(self):
        rows = [ex("2026-07-01T10:00:00Z", "ctest:a", "fail", commit="c1"),
                ex("2026-07-02T10:00:00Z", "ctest:a", "fail", run="r2", commit="c2"),
                ex("2026-07-03T10:00:00Z", "ctest:a", "pass", run="r3", commit="c3"),
                ex("2026-07-04T10:00:00Z", "ctest:a", "pass", run="r4", commit="c4"),
                ex(day(10), "ctest:a", "fail", run="r5", commit="c5"),
                ex(day(11), "ctest:a", "fail", run="r6", commit="c6")]
        signal = self.one(rows)
        self.assertFalse(signal.regressed)
        self.assertEqual(signal.count, 2)                      # its own count, from zero
        self.assertEqual(signal.previous, {"key": "ctest:a",
                                           "first_seen": "2026-07-01T10:00:00Z"})
        self.assertEqual(signal.first_seen, day(10))
        self.assertEqual(S.REGRESS_DAYS, 30)

    # ---- stale, removed, retention
    def test_an_open_signal_unseen_for_seven_days_is_stale_and_still_open(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2")]
        self.assertFalse(self.one(rows, now=day(3)).stale)
        fresh = self.one(rows, now=day(8))
        self.assertFalse(fresh.stale)
        stale = self.one(rows, now=day(20))
        self.assertTrue(stale.stale)
        self.assertEqual(stale.state, "open")                  # never closed by a timer
        self.assertEqual(S.STALE_DAYS, 7)

    def test_a_key_that_left_discovery_is_removed_which_is_not_a_fix(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2")]
        self.assertEqual(self.one(rows, discovered=["ctest:a"]).state, "open")
        gone = self.one(rows, discovered=["ctest:b"])
        self.assertEqual(gone.state, "removed")
        self.assertFalse(gone.blocks_verification)
        # A build or check key is not in test discovery and must not be removed by its absence.
        build = fold([ex(day(1), "build:relay", "fail", commit="c1")],
                     discovered=["ctest:b"])["build:relay"]
        self.assertEqual(build.state, "open")

    def test_a_resolved_signal_is_kept_thirty_days_for_the_regression_rule_then_dropped(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(3), "ctest:a", "pass", run="r3", commit="c3"),
                ex(day(4), "ctest:a", "pass", run="r4", commit="c4")]
        self.assertEqual(self.one(rows, now=day(20)).state, "resolved")
        self.assertEqual(fold(rows, now="2026-10-20T10:00:00Z"), {})
        self.assertEqual(S.RETENTION_DAYS, 30)


# --------------------------------------------------------------------------- one cause, one item

class OneCauseTests(unittest.TestCase):
    """R7 in its order: inhibition, then the red run, then the fingerprint group, then the cap."""

    def test_a_build_failure_inhibits_every_test_in_that_run(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(3), "build:relay", "fail", run="r3", commit="c3",
                   message="error: no member named x"),
                ex(day(3), "ctest:a", "fail", run="r3", commit="c3"),
                ex(day(3), "ctest:b", "fail", run="r3", commit="c3")]
        signals = fold(rows)
        self.assertEqual(signals["build:relay"].state, "open")   # complete evidence, opens at once
        self.assertEqual(signals["build:relay"].kind, "build")
        self.assertEqual(signals["ctest:a"].count, 2)            # the third failure was not one
        self.assertEqual(signals["ctest:a"].inhibited_by, "build:relay")
        self.assertNotIn("ctest:b", signals)                     # never evaluated at all

    def test_a_red_run_opens_one_run_signal_and_itemises_nothing(self):
        rows = [ex(day(1), f"ctest:p{i}", "pass", run="r1") for i in range(4)]
        rows += [ex(day(1), f"ctest:f{i}", "fail", run="r1", message=f"unlike {i} others")
                 for i in range(6)]
        signals = fold(rows)
        self.assertEqual([s.key for s in signals.values() if s.kind == "run"], ["run:ctest"])
        run = signals["run:ctest"]
        self.assertEqual(run.state, "open")
        self.assertEqual(len(run.members), 6)
        for index in range(6):
            member = signals[f"ctest:f{index}"]
            self.assertEqual(member.state, "pending")            # members stay pending
            self.assertEqual(member.group, "run:ctest")
        self.assertEqual((S.RED_RUN_MIN_EXECUTED, S.RED_RUN_FRACTION), (10, 0.5))

    def test_a_run_below_the_red_threshold_itemises_its_failures(self):
        rows = [ex(day(1), f"ctest:p{i}", "pass", run="r1") for i in range(2)]
        rows += [ex(day(1), f"ctest:f{i}", "fail", run="r1", message=f"unlike {i} others")
                 for i in range(2)]
        signals = fold(rows)
        self.assertEqual([s.kind for s in signals.values()], ["broken", "broken"])
        self.assertFalse(any(s.group for s in signals.values()))

    def test_three_keys_with_one_fingerprint_become_one_group(self):
        rows = [ex(day(1), f"ctest:k{i}", "fail", run="r1",
                   message=f"undefined reference to `foo' at line {i}")
                for i in range(S.GROUP_MIN_KEYS)]
        signals = fold(rows)
        groups = [s for s in signals.values() if s.kind == "group"]
        self.assertEqual(len(groups), 1)
        self.assertEqual(groups[0].state, "open")
        self.assertEqual(sorted(groups[0].members), ["ctest:k0", "ctest:k1", "ctest:k2"])
        self.assertEqual(groups[0].fingerprint, "undefined reference to `foo' at line %")
        for index in range(S.GROUP_MIN_KEYS):
            self.assertEqual(signals[f"ctest:k{index}"].state, "pending")
        self.assertEqual(S.GROUP_MIN_KEYS, 3)

    def test_two_keys_with_one_fingerprint_are_not_a_group(self):
        rows = [ex(day(1), f"ctest:k{i}", "fail", run="r1", message="undefined reference")
                for i in range(S.GROUP_MIN_KEYS - 1)]
        signals = fold(rows)
        self.assertEqual([s.kind for s in signals.values()], ["broken", "broken"])

    def test_a_group_resolves_when_its_members_pass_twice(self):
        fail = [ex(day(1), f"ctest:k{i}", "fail", run="r1", message="undefined reference")
                for i in range(3)]
        passes = [ex(day(d), f"ctest:k{i}", "pass", run=f"r{d}", commit=f"c{d}")
                  for d in (2, 3) for i in range(3)]
        signals = fold(fail + passes)
        group = next(s for s in signals.values() if s.kind == "group")
        self.assertEqual(group.state, "resolved")

    def test_a_run_itemises_at_most_ten_signals_and_the_overflow_rolls_into_the_run(self):
        # Sixteen passes keep the run under the red-run rule, and each failure has a
        # fingerprint of its own so the group rule does not claim them either.
        rows = [ex(day(1), f"ctest:p{i:02d}", "pass", run="r1") for i in range(16)]
        rows += [ex(day(1), f"ctest:f{i:02d}", "fail", run="r1", message=f"kind{i} broke")
                 for i in range(S.MAX_SIGNALS_PER_RUN + 2)]
        signals = fold(rows)
        itemised = sorted(k for k, s in signals.items() if s.kind == "broken" and not s.group)
        self.assertEqual(len(itemised), S.MAX_SIGNALS_PER_RUN)
        overflow = signals["run:ctest"]
        self.assertEqual(sorted(overflow.members), ["ctest:f10", "ctest:f11"])
        self.assertEqual(S.MAX_SIGNALS_PER_RUN, 10)

    def test_separate_runs_are_separate_even_with_no_run_id(self):
        # A hand-typed `ctest` ingested from a JUnit file with no metadata is still one run: the
        # fold must not read a month of them as one mass failure.
        rows = [H.Execution(ts=day(d), id=f"ctest:k{d}", result="fail", runner="ctest",
                            message="undefined reference") for d in range(1, 6)]
        signals = fold(rows)
        self.assertFalse([s for s in signals.values() if s.kind in ("group", "run")])


# --------------------------------------------------------------------------- the actions

class ActionTests(unittest.TestCase):
    """The event log: claim, release, dismiss, promote, note — and `run`, which is ours."""

    def rows(self):
        return [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2")]

    def test_a_claim_names_the_session_and_a_release_gives_it_back(self):
        events = [{"ts": day(3), "action": "claim", "key": "ctest:a", "session": "tok12345678"}]
        self.assertEqual(fold(self.rows(), events)["ctest:a"].session, "tok12345678")
        events.append({"ts": day(4), "action": "release", "key": "ctest:a", "reason": "done"})
        self.assertEqual(fold(self.rows(), events)["ctest:a"].session, "")

    def test_a_release_that_gave_up_is_recorded_and_promotes(self):
        events = [{"ts": day(3), "action": "claim", "key": "ctest:a", "session": "t"},
                  {"ts": day(4), "action": "release", "key": "ctest:a", "reason": S.GAVE_UP}]
        signal = fold(self.rows(), events)["ctest:a"]
        self.assertEqual(signal.gave_up, S.GAVE_UP)
        self.assertEqual(signal.promote, S.GAVE_UP)

    def test_a_dismissal_holds_until_its_expiry_and_then_the_signal_is_back(self):
        events = [{"ts": day(3), "action": "dismiss", "key": "ctest:a",
                   "reason": "environmental", "comment": "the socket path", "until": day(9)}]
        held = fold(self.rows(), events, now=day(5))["ctest:a"]
        self.assertEqual(held.state, "dismissed")
        self.assertFalse(held.blocks_verification)               # never blocks verification
        self.assertEqual(held.dismissed["reason"], "environmental")
        expired = fold(self.rows(), events, now=day(10))["ctest:a"]
        self.assertEqual(expired.state, "open")
        self.assertTrue(expired.dismissed["expired"])

    def test_occurrences_keep_counting_under_a_dismissed_signal(self):
        rows = self.rows() + [ex(day(5), "ctest:a", "fail", run="r5", commit="c5")]
        events = [{"ts": day(3), "action": "dismiss", "key": "ctest:a", "reason": "wont-fix",
                   "comment": "known", "until": day(30)}]
        signal = fold(rows, events, now=day(6))["ctest:a"]
        self.assertEqual((signal.state, signal.count), ("dismissed", 3))

    def test_a_dismissed_signal_still_resolves_by_passing(self):
        rows = self.rows() + [ex(day(5), "ctest:a", "pass", run="r5", commit="c5"),
                              ex(day(6), "ctest:a", "pass", run="r6", commit="c6")]
        events = [{"ts": day(3), "action": "dismiss", "key": "ctest:a", "reason": "wont-fix",
                   "comment": "known", "until": day(30)}]
        signal = fold(rows, events, now=day(7))["ctest:a"]
        self.assertEqual(signal.state, "resolved")
        self.assertIsNone(signal.dismissed)

    def test_a_dismissal_over_a_pending_signal_falls_back_to_pending(self):
        events = [{"ts": day(2), "action": "dismiss", "key": "ctest:a", "reason": "environmental",
                   "comment": "c", "until": day(3)}]
        rows = [ex(day(1), "ctest:a", "fail", commit="c1")]
        self.assertEqual(fold(rows, events, now=day(2, 12))["ctest:a"].state, "dismissed")
        self.assertEqual(fold(rows, events, now=day(4))["ctest:a"].state, "pending")

    def test_a_promotion_records_the_card_and_stops_promoting(self):
        events = [{"ts": day(3), "action": "release", "key": "ctest:a", "reason": S.GAVE_UP},
                  {"ts": day(4), "action": "promote", "key": "ctest:a", "card": "ab12"}]
        signal = fold(self.rows(), events)["ctest:a"]
        self.assertEqual(signal.card, "AB12")
        self.assertEqual(signal.promote, "")
        payload = S.summary(fold(self.rows(), events).values())
        self.assertEqual([s["key"] for s in payload["promoted"]], ["ctest:a"])
        self.assertEqual([s["card"] for s in payload["promoted"]], ["AB12"])

    def test_an_action_for_a_key_with_no_signal_invents_none(self):
        events = [{"ts": day(3), "action": "claim", "key": "ctest:nothing", "session": "t"}]
        self.assertEqual(fold([], events), {})

    def test_a_run_line_is_how_the_fold_learns_whose_run_failed_it(self):
        events = [{"ts": day(1), "action": "run", "run_id": "r", "session": "pane-1"},
                  {"ts": day(2), "action": "run", "run_id": "r2", "session": "pane-2"}]
        signal = fold(self.rows(), events)["ctest:a"]
        self.assertEqual(signal.first_session, "pane-1")
        self.assertEqual(signal.sessions, ["pane-1", "pane-2"])


# --------------------------------------------------------------------------- promotion

class PromotionTests(unittest.TestCase):
    """R9 / decision 5: give-up, persistence or flakiness — never age alone; capped at five."""

    def test_three_failing_runs_over_a_day_with_no_claim_promotes(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(3), "ctest:a", "fail", run="r3", commit="c3")]
        self.assertEqual(fold(rows)["ctest:a"].promote, "persistent")
        self.assertEqual((S.PROMOTE_MIN_RUNS, S.PROMOTE_MIN_HOURS), (3, 24))

    def test_three_failing_runs_inside_a_day_does_not_promote(self):
        rows = [ex(day(1, 9), "ctest:a", "fail", commit="c1"),
                ex(day(1, 10), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(1, 11), "ctest:a", "fail", run="r3", commit="c3")]
        self.assertEqual(fold(rows)["ctest:a"].promote, "")

    def test_two_failing_runs_over_a_week_does_not_promote_because_age_alone_never_does(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(9), "ctest:a", "fail", run="r2", commit="c2")]
        self.assertEqual(fold(rows, now=day(19))["ctest:a"].promote, "")

    def test_a_held_signal_is_not_promoted_out_from_under_the_session_holding_it(self):
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(3), "ctest:a", "fail", run="r3", commit="c3")]
        events = [{"ts": day(3, 12), "action": "claim", "key": "ctest:a", "session": "t"}]
        self.assertEqual(fold(rows, events)["ctest:a"].promote, "")

    def test_a_confirmed_flaky_signal_promotes_because_deflaking_is_a_decision(self):
        rows = [ex(day(d), "ctest:a", "fail" if d % 2 else "pass", run=f"r{d}", commit=f"c{d}")
                for d in range(1, 15)]
        signal = fold(rows)["ctest:a"]
        self.assertEqual((signal.kind, signal.promote), ("flaky", "flaky"))

    def test_a_pending_or_resolved_signal_never_promotes(self):
        self.assertEqual(fold([ex(day(1), "ctest:a", "fail")])["ctest:a"].promote, "")
        rows = [ex(day(1), "ctest:a", "fail", commit="c1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2"),
                ex(day(3), "ctest:a", "pass", run="r3", commit="c3"),
                ex(day(4), "ctest:a", "pass", run="r4", commit="c4")]
        self.assertEqual(fold(rows)["ctest:a"].promote, "")

    def test_the_cap_counts_the_promoted_cards_that_are_still_open(self):
        rows, events = [], []
        for index in range(7):
            key = f"ctest:k{index}"
            rows += [ex(day(1), key, "fail", run="r1", message=f"unlike {index} others"),
                     ex(day(2), key, "fail", run="r2", commit="c2",
                        message=f"unlike {index} others")]
            events.append({"ts": day(3), "action": "promote", "key": key,
                           "card": f"a{index}b2"})
        signals = fold(rows, events)
        self.assertEqual(len(S.promoted_open(signals.values())), 7)
        self.assertEqual(S.MAX_PROMOTED_OPEN, 5)


# --------------------------------------------------------------------------- the gate

class BlockingTests(unittest.TestCase):
    """Decision 8: only a signal first seen in a run by the pane holding that card blocks."""

    def signals(self):
        rows = [ex(day(1), "ctest:mine", "fail", run="r1", message="one"),
                ex(day(2), "ctest:mine", "fail", run="r2", commit="c2", message="one"),
                ex(day(1), "ctest:theirs", "fail", run="r9", message="two"),
                ex(day(2), "ctest:theirs", "fail", run="r9b", commit="c2", message="two")]
        events = [{"ts": day(1), "action": "run", "run_id": "r1", "session": "pane-1"},
                  {"ts": day(1), "action": "run", "run_id": "r9", "session": "pane-9"}]
        return fold(rows, events)

    def test_a_signal_this_pane_first_saw_blocks_and_the_rest_are_open_before(self):
        blocks, before = S.blocking(self.signals().values(), session="pane-1")
        self.assertEqual([s.key for s in blocks], ["ctest:mine"])
        self.assertEqual([s.key for s in before], ["ctest:theirs"])

    def test_a_pane_with_no_token_blocks_on_nothing(self):
        blocks, before = S.blocking(self.signals().values(), session="")
        self.assertEqual(blocks, [])
        self.assertEqual(len(before), 2)

    def test_the_signal_a_card_was_promoted_from_blocks_that_card(self):
        rows = [ex(day(1), "ctest:a", "fail", run="r1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2")]
        events = [{"ts": day(3), "action": "promote", "key": "ctest:a", "card": "AB12"}]
        blocks, before = S.blocking(fold(rows, events).values(), card="ab12")
        self.assertEqual([s.key for s in blocks], ["ctest:a"])
        self.assertEqual(before, [])

    def test_a_dismissed_or_resolved_signal_blocks_nothing(self):
        rows = [ex(day(1), "ctest:a", "fail", run="r1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2")]
        events = [{"ts": day(1), "action": "run", "run_id": "r1", "session": "pane-1"},
                  {"ts": day(3), "action": "dismiss", "key": "ctest:a", "comment": "c",
                   "reason": "environmental", "until": day(9)}]
        blocks, before = S.blocking(fold(rows, events, now=day(4)).values(), session="pane-1")
        self.assertEqual((blocks, before), ([], []))


# --------------------------------------------------------------------------- the re-run rule

class RerunTests(unittest.TestCase):
    """Decision 3: ten or fewer failures, a minute of recorded p50, re-run once before folding."""

    def test_a_short_failed_set_is_re_run(self):
        rows = [ex(day(1), "ctest:a", "fail", duration=1.0),
                ex(day(1), "ctest:b", "fail", duration=2.0)]
        self.assertEqual(S.rerun_keys(["ctest:a", "ctest:b"], rows), ["ctest:a", "ctest:b"])

    def test_a_slow_failed_set_waits_for_the_next_natural_run(self):
        rows = [ex(day(1), "ctest:a", "fail", duration=61.0)]
        self.assertEqual(S.rerun_keys(["ctest:a"], rows), [])
        self.assertEqual(S.RERUN_MAX_SECONDS, 60.0)

    def test_more_than_ten_failures_is_never_re_run(self):
        keys = [f"ctest:k{i}" for i in range(S.RERUN_MAX_FAILURES + 1)]
        rows = [ex(day(1), key, "fail", duration=0.01) for key in keys]
        self.assertEqual(S.rerun_keys(keys, rows), [])
        self.assertEqual(S.RERUN_MAX_FAILURES, 10)

    def test_the_p50_is_the_median_of_what_is_recorded_not_the_last_run(self):
        rows = [ex(day(d), "ctest:a", "fail", run=f"r{d}", duration=duration)
                for d, duration in enumerate([1.0, 1.0, 1.0, 1.0, 300.0], start=1)]
        self.assertEqual(S.rerun_keys(["ctest:a"], rows), ["ctest:a"])

    def test_no_failures_is_no_re_run(self):
        self.assertEqual(S.rerun_keys([], []), [])
        self.assertEqual(S.rerun_keys(["", "  "], []), [])


# --------------------------------------------------------------------------- dismissal limits

class DismissalLimitTests(unittest.TestCase):
    """Decision 7: the agent's two reasons, a comment, and at most a week — always an expiry."""

    def test_an_agent_may_dismiss_environmental_and_flaky_known_only(self):
        for reason in S.AGENT_DISMISS_REASONS:
            self.assertEqual(S.check_dismissal(reason, "checked", day(3), by_agent=True,
                                               now=day(1))["reason"], reason)
        for reason in ("wont-fix", "expected"):
            with self.assertRaises(S.SignalError) as caught:
                S.check_dismissal(reason, "checked", day(3), by_agent=True, now=day(1))
            self.assertEqual(caught.exception.code, "signal_reason")
            # …and the owner's path takes all four.
            self.assertEqual(S.check_dismissal(reason, "checked", day(3), by_agent=False,
                                               now=day(1))["reason"], reason)

    def test_an_unknown_reason_is_refused_on_both_paths(self):
        for by_agent in (True, False):
            with self.assertRaises(S.SignalError):
                S.check_dismissal("because", "checked", day(3), by_agent=by_agent, now=day(1))

    def test_a_dismissal_needs_a_comment(self):
        with self.assertRaises(S.SignalError) as caught:
            S.check_dismissal("environmental", "  ", day(3), by_agent=True, now=day(1))
        self.assertEqual(caught.exception.code, "signal_comment")

    def test_every_dismissal_has_an_expiry_and_it_is_in_the_future(self):
        for until in (None, "", "not a date"):
            with self.assertRaises(S.SignalError) as caught:
                S.check_dismissal("environmental", "c", until, by_agent=False, now=day(2))
            self.assertEqual(caught.exception.code, "signal_until")
        with self.assertRaises(S.SignalError):
            S.check_dismissal("environmental", "c", day(1), by_agent=False, now=day(2))

    def test_an_agents_dismissal_lasts_at_most_a_week(self):
        self.assertTrue(S.check_dismissal("environmental", "c", day(8), by_agent=True,
                                          now=day(1))["until"])
        with self.assertRaises(S.SignalError) as caught:
            S.check_dismissal("environmental", "c", day(9), by_agent=True, now=day(1))
        self.assertEqual(caught.exception.code, "signal_until")
        self.assertEqual(S.AGENT_DISMISS_MAX_DAYS, 7)
        # The owner's may be longer.
        self.assertTrue(S.check_dismissal("wont-fix", "c", "2027-01-01", by_agent=False,
                                          now=day(1))["until"])

    def test_a_bare_date_is_accepted_and_normalised(self):
        self.assertEqual(S.check_dismissal("environmental", "c", "2026-09-25", by_agent=True,
                                           now=day(20))["until"], "2026-09-25T00:00:00Z")


# --------------------------------------------------------------------------- the store

class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="sg-", dir="/tmp")
        self.path = Path(self.tmp.name) / "events.jsonl"

    def tearDown(self):
        self.tmp.cleanup()

    def test_an_event_is_one_line_with_a_timestamp_and_a_version(self):
        row = S.append_event({"action": "claim", "key": "ctest:a", "session": "t"}, self.path)
        self.assertTrue(row["ts"])
        self.assertEqual(row["v"], S.SIGNAL_VERSION)
        self.assertEqual(len(self.path.read_text(encoding="utf-8").splitlines()), 1)
        self.assertEqual(S.read_events(self.path), [row])

    def test_an_unknown_action_is_refused_before_anything_is_written(self):
        with self.assertRaises(S.SignalError):
            S.append_event({"action": "delete", "key": "ctest:a"}, self.path)
        self.assertFalse(self.path.exists())

    def test_a_torn_last_line_and_a_foreign_line_are_skipped_not_raised(self):
        S.append_event({"action": "claim", "key": "ctest:a"}, self.path)
        with self.path.open("a", encoding="utf-8") as handle:
            handle.write('{"action": "sabotage"}\n{"action": "claim", "key": "ctest:b"')
        self.assertEqual([r["key"] for r in S.read_events(self.path)], ["ctest:a"])

    def test_a_missing_store_reads_as_empty(self):
        self.assertEqual(S.read_events(self.path / "nope"), [])

    def test_the_events_live_beside_the_history_under_the_private_root(self):
        project = Path(self.tmp.name)
        (project / "issues").mkdir()
        (project / "issues" / "board.yaml").write_text("version: 1\n", encoding="utf-8")
        path = S.default_path(project)
        self.assertEqual(path.parent.name, S.STORE_DIR)
        self.assertEqual(path.name, S.STORE_NAME)
        self.assertEqual(path.parent.parent, H.default_path(project).parent.parent)
        self.assertIn(".private", str(path))

    def test_state_folds_the_two_files_off_the_disk(self):
        project = Path(self.tmp.name)
        (project / "issues").mkdir()
        (project / "issues" / "board.yaml").write_text("version: 1\n", encoding="utf-8")
        H.append([ex(day(1), "ctest:a", "fail", commit="c1"),
                  ex(day(2), "ctest:a", "fail", run="r2", commit="c2")],
                 H.default_path(project))
        S.append_event({"ts": day(3), "action": "claim", "key": "ctest:a", "session": "tok"},
                       S.default_path(project))
        signals = S.state(project, now=NOW)
        self.assertEqual(signals["ctest:a"].state, "open")
        self.assertEqual(signals["ctest:a"].session, "tok")


# --------------------------------------------------------------------------- what the surfaces read

class WireTests(unittest.TestCase):
    def signals(self):
        rows = [ex(day(1), "ctest:open", "fail", run="r1", message="one"),
                ex(day(2), "ctest:open", "fail", run="r2", commit="c2", message="one"),
                ex(day(2), "ctest:pending", "fail", run="r2", commit="c2", message="two")]
        events = [{"ts": day(1), "action": "run", "run_id": "r1", "session": "pane-1"}]
        return fold(rows, events)

    def test_the_summary_is_the_signals_changed_payload(self):
        payload = S.summary(self.signals().values())
        self.assertEqual([row["key"] for row in payload["open"]], ["ctest:open"])
        self.assertEqual(payload["pending_count"], 1)
        self.assertEqual(payload["dismissed_count"], 0)
        self.assertEqual(payload["dismissed"], [])
        self.assertEqual(payload["promoted"], [])
        self.assertEqual(set(payload), {"open", "dismissed", "pending_count", "dismissed_count",
                                        "promoted"})

    def test_a_dismissed_signal_is_sent_as_a_row_so_its_expiry_can_be_shown(self):
        rows = [ex(day(1), "ctest:a", "fail", run="r1"),
                ex(day(2), "ctest:a", "fail", run="r2", commit="c2")]
        events = [{"ts": day(3), "action": "dismiss", "key": "ctest:a", "reason": "wont-fix",
                   "comment": "known", "until": day(30)}]
        payload = S.summary(fold(rows, events, now=day(4)).values())
        self.assertEqual(payload["open"], [])
        self.assertEqual([row["key"] for row in payload["dismissed"]], ["ctest:a"])
        self.assertEqual(payload["dismissed"][0]["dismissed"]["until"], day(30))
        self.assertEqual(payload["dismissed_count"], 1)

    def test_a_signal_dict_is_json_and_carries_the_contract_keys(self):
        row = self.signals()["ctest:open"].to_dict()
        self.assertEqual(json.loads(json.dumps(row)), row)
        self.assertLessEqual({"key", "source", "kind", "state", "first_seen", "last_seen",
                              "count", "green_streak", "runs", "fingerprint", "regressed",
                              "stale", "version"}, set(row))
        self.assertNotIn("dismissed", row)             # absent, not null
        self.assertNotIn("card", row)

    def test_the_order_is_mine_then_regressed_then_broken_before_flaky_then_by_count(self):
        rows = [ex(day(d), "ctest:flaky", "fail" if d % 2 else "pass", run=f"f{d}",
                   commit=f"c{d}") for d in range(1, 15)]
        rows += [ex(day(1), "ctest:broken", "fail", run="b1", message="x"),
                 ex(day(2), "ctest:broken", "fail", run="b2", commit="c2", message="x"),
                 ex(day(3), "ctest:broken", "fail", run="b3", commit="c3", message="x")]
        rows += [ex(day(1), "ctest:mine", "fail", run="m1", message="y"),
                 ex(day(2), "ctest:mine", "fail", run="m2", commit="c2", message="y")]
        events = [{"ts": day(1), "action": "run", "run_id": "m1", "session": "pane-1"}]
        order = [s.key for s in S.sort_signals(fold(rows, events).values(), session="pane-1")]
        self.assertEqual(order, ["ctest:mine", "ctest:broken", "ctest:flaky"])

    def test_the_card_text_quotes_the_excerpt_verbatim_and_names_what_closes_it(self):
        signal = self.signals()["ctest:open"]
        signal.excerpt = "AssertionError: 3 != 17\n  at board_tools._claim"
        self.assertIn(signal.excerpt, S.card_request(signal))
        self.assertIn("ctest:open", S.card_title(signal))
        section = S.signal_section(signal)
        self.assertIn("`ctest:open`", section)
        self.assertIn("2 consecutive passing executions", section)
        self.assertIn("closing this card does not close the signal", section)
        self.assertIn("<!-- Written by Relay", section)

    def test_a_flaky_signals_section_says_twenty(self):
        signal = S.Signal(key="ctest:a", kind="flaky", state="open")
        self.assertIn("20 consecutive passing executions", S.signal_section(signal))


# --------------------------------------------------------------------------- the guest's path

class ScriptTests(unittest.TestCase):
    """`scripts/relay-board.py signals`: the same rules, for an agent with no tools (step 8).

    Run as a subprocess, because that is how a guest reaches it: a bare `python3` with no
    `PYTHONPATH`, from inside the checkout.
    """

    script = REPO / "scripts" / "relay-board.py"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="sb-", dir="/tmp")
        self.project = Path(self.tmp.name).resolve()
        self.root = self.project / "issues"
        (self.root / "changes").mkdir(parents=True)
        (self.root / "board.yaml").write_text(
            "version: 1\n"
            "tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\n"
            "columns: [inbox, executing, needs-verification, done]\n"
            "agent: {autonomy: auto}\n", encoding="utf-8")
        H.append([ex(day(1), "ctest:panelayout", "fail", message="AssertionError: 3 != 17"),
                  ex(day(2), "ctest:panelayout", "fail", run="r2", commit="c2",
                     message="AssertionError: 3 != 17")],
                 H.default_path(self.project))

    def tearDown(self):
        self.tmp.cleanup()

    def run_it(self, *args):
        import subprocess
        return subprocess.run([sys.executable, str(self.script), "--board", str(self.root),
                               "signals", *args], capture_output=True, text=True,
                              cwd=str(self.project), timeout=120)

    def test_the_list_names_the_open_signal_and_what_is_wrong(self):
        done = self.run_it()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("ctest:panelayout", done.stdout)
        self.assertIn("open", done.stdout)
        self.assertIn("AssertionError", done.stdout)

    def test_a_claim_needs_a_name_because_a_guest_has_no_pane_token(self):
        refused = self.run_it("claim", "ctest:panelayout")
        self.assertEqual(refused.returncode, 2)
        self.assertIn("--as", refused.stderr)
        done = self.run_it("claim", "ctest:panelayout", "--as", "claude-code")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("2 consecutive passing executions", done.stdout)
        signals = S.state(self.project)
        self.assertEqual(signals["ctest:panelayout"].session, "claude-code")

    def test_a_signal_somebody_else_holds_is_refused_until_force(self):
        self.run_it("claim", "ctest:panelayout", "--as", "codex")
        refused = self.run_it("claim", "ctest:panelayout", "--as", "claude-code")
        self.assertEqual(refused.returncode, 2)
        self.assertIn("held by codex", refused.stderr)
        taken = self.run_it("claim", "ctest:panelayout", "--as", "claude-code", "--force")
        self.assertEqual(taken.returncode, 0, taken.stderr)

    def test_the_guest_dismissal_is_the_agents_two_reasons_and_a_week(self):
        far = (datetime.now(timezone.utc) + timedelta(days=30)).strftime("%Y-%m-%d")
        soon = (datetime.now(timezone.utc) + timedelta(days=3)).strftime("%Y-%m-%d")
        for args in (("--reason", "wont-fix", "--comment", "c", "--until", soon),
                     ("--reason", "environmental", "--until", soon),
                     ("--reason", "environmental", "--comment", "c"),
                     ("--reason", "environmental", "--comment", "c", "--until", far)):
            with self.subTest(args=args):
                refused = self.run_it("dismiss", "ctest:panelayout", *args)
                self.assertEqual(refused.returncode, 2, refused.stdout)
        done = self.run_it("dismiss", "ctest:panelayout", "--reason", "flaky-known",
                           "--comment", "known socket flake", "--until", soon)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(S.state(self.project)["ctest:panelayout"].state, "dismissed")

    def test_a_release_that_gave_up_files_the_bug_card(self):
        self.run_it("claim", "ctest:panelayout", "--as", "claude-code")
        done = self.run_it("release", "ctest:panelayout", "--reason", S.GAVE_UP,
                           "--as", "claude-code")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("is now card #", done.stdout)
        signal = S.state(self.project)["ctest:panelayout"]
        self.assertTrue(signal.card)
        self.assertEqual(signal.session, "")

    def test_a_promotion_by_hand_writes_one_card_and_says_so_the_second_time(self):
        first = self.run_it("promote", "ctest:panelayout")
        self.assertEqual(first.returncode, 0, first.stderr)
        again = self.run_it("promote", "ctest:panelayout")
        self.assertIn("already has card", again.stdout)

    def test_an_unknown_key_and_a_missing_key_are_refused_by_name(self):
        self.assertEqual(self.run_it("claim").returncode, 2)
        missing = self.run_it("claim", "ctest:nothing", "--as", "x")
        self.assertEqual(missing.returncode, 2)
        self.assertIn("no open signal", missing.stderr)

    def test_the_policy_tells_a_guest_the_rules_it_cannot_read_from_a_tool(self):
        from relay_core import board as B
        text = B.policy_text(B.Board(self.root, self.project))
        for phrase in ("relay-board.py signals", "second consecutive failing execution",
                       "environmental", "flaky-known", "needs-verification",
                       "## Signal"):
            self.assertIn(phrase, text)



if __name__ == "__main__":                                       # pragma: no cover
    unittest.main()
