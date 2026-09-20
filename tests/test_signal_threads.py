# SPDX-License-Identifier: AGPL-3.0-or-later
"""`relay_core.signal_threads`: unasked pickup (card #AQ6X, decision 9, plan step 7).

Two halves, both pure enough to test without a model, a worker or a clock.

*In-loop* (step 7a): the pane whose own run opened a signal is told in that run's result, and the
sentence is what tells it the signal is its own to fix before it reports.  `format_run` is the
only thing that has to be true for that, so it is the first case here.

*Orphans* (step 7b): a signal nobody claimed after a whole fold becomes a **signal thread** — its
own subagent, claiming under its own thread id, notified and listed.  `SignalThreads` is handed a
`spawn` and an `emit`, so every rule (one fold of grace, the cap of three, `auto_work`, autonomy
off, one thread per key, the 24-hour gave-up cool-off) is exercised against a fake spawn that
records what it was asked for.
"""
import sys
import tempfile
import threading
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))

from relay_core import board as B                   # noqa: E402
from relay_core import signal_threads as ST         # noqa: E402
from relay_core import signals as S                 # noqa: E402
from relay_core import test_history as H            # noqa: E402
from relay_core import tests_protocol as TP         # noqa: E402


def signal(key="ctest:panelayout", **kw) -> S.Signal:
    """One folded signal, open and orphaned unless a keyword says otherwise."""
    fields = {"source": key.split(":", 1)[0], "kind": "broken", "state": "open", "count": 2,
              "first_seen": "2026-09-20T10:00:00Z", "last_seen": "2026-09-20T11:00:00Z"}
    fields.update(kw)
    return S.Signal(key=key, **fields)


class FakeSpawn:
    """A `spawn` that records what it was asked for and hands back ids, never an agent."""

    def __init__(self, fail: bool = False):
        self.calls: list[tuple[str, str]] = []
        self.fail = fail
        self._next = 0

    def __call__(self, task, description):
        self.calls.append((task, description))
        if self.fail:
            return None
        self._next += 1
        return f"th{self._next}", f"a{self._next}", "owner-session"


def threads(spawn=None, *, clock=None, **kw):
    """A `SignalThreads` over a fake spawn, collecting its events and its log writes."""
    events: list[dict] = []
    written: list[tuple] = []
    spawn = spawn if spawn is not None else FakeSpawn()
    at = [1_000_000.0]
    manager = ST.SignalThreads(
        spawn, events.append,
        claim=lambda key, token: written.append(("claim", key, token)),
        release=lambda key, token, reason: written.append(("release", key, token, reason)),
        clock=(clock or (lambda: at[0])), **kw)
    manager.events, manager.written, manager.spawn, manager.at = events, written, spawn, at
    return manager


class InLoopTests(unittest.TestCase):
    """Step 7a: the run's own result is where the pane learns what it broke."""

    def test_run_text_says_a_signal_this_run_opened_is_yours(self):
        text = TP.format_run({"run_id": "r1", "counts": {"pass": 0, "fail": 1, "skip": 0},
                              "ran": 1, "requested": 1,
                              "tests": [{"id": "ctest:panelayout", "result": "fail",
                                         "duration": 0.2}],
                              "opened": ["ctest:panelayout"]})
        self.assertIn("signals this run opened: ctest:panelayout", text)
        self.assertIn("is yours", text)
        self.assertIn("board_signals", text)
        self.assertIn("gave-up", text)

    def test_a_run_that_opened_nothing_says_nothing(self):
        text = TP.format_run({"run_id": "r1", "counts": {"pass": 1, "fail": 0, "skip": 0},
                              "ran": 1, "requested": 1,
                              "tests": [{"id": "ctest:panelayout", "result": "pass",
                                         "duration": 0.2}]})
        self.assertNotIn("is yours", text)
        self.assertNotIn("signals this run opened", text)


class SettingTests(unittest.TestCase):
    """`signals: {auto_work: …}` in `board.yaml`, and the owner's default."""

    def test_absent_means_on(self):
        self.assertTrue(ST.auto_work(None))
        self.assertTrue(ST.auto_work({}))
        self.assertTrue(ST.auto_work({"signals": {}}))
        self.assertTrue(ST.auto_work({"signals": {"auto_work": "yes"}}))   # not a bool: default

    def test_the_flag_is_read_and_written(self):
        self.assertFalse(ST.auto_work({"signals": {"auto_work": False}}))
        out = ST.with_auto_work({"version": 1, "signals": {"other": 2}}, False)
        self.assertEqual(out["signals"], {"other": 2, "auto_work": False})
        self.assertEqual(out["version"], 1)

    def test_the_scaffolded_board_carries_it(self):
        config = B.parse_yaml(B.CONFIG_TEXT)
        self.assertEqual(config["signals"], {"auto_work": True})
        self.assertIn("signals", B.CONFIG_KEY_ORDER)
        # It survives a rewrite, which is what the Options row does to it.
        again = B.parse_yaml(B.render_config(ST.with_auto_work(config, False)))
        self.assertFalse(ST.auto_work(again))


class OrphanTests(unittest.TestCase):
    """Which signals a fold may pick up — decision 9's "signals no run of a live pane owns"."""

    def test_the_first_fold_only_starts_the_clock(self):
        manager = threads()
        one = signal()
        self.assertEqual(manager.candidates([one]), [])          # the pane's fold of grace
        self.assertEqual([s.key for s in manager.candidates([one])], [one.key])

    def test_a_claim_inside_that_fold_keeps_it(self):
        manager = threads()
        manager.candidates([signal()])
        self.assertEqual(manager.candidates([signal(session="pane-1")]), [])
        # And the grace is owed again when the claim goes: the pane had it and let it go.
        self.assertEqual(manager.candidates([signal()]), [])

    def test_pending_resolved_dismissed_and_collapsed_are_not_orphans(self):
        manager = threads()
        for state in ("pending", "resolved", "dismissed", "removed"):
            rows = [signal(state=state)]
            manager.candidates(rows)
            self.assertEqual(manager.candidates(rows), [], state)
        rows = [signal(group="run:ctest")]
        manager.candidates(rows)
        self.assertEqual(manager.candidates(rows), [], "a collapsed member is not the item")

    def test_a_promoted_signal_is_a_persons_card_unless_it_regressed(self):
        manager = threads()
        rows = [signal(card="K7Q2")]
        manager.candidates(rows)
        self.assertEqual(manager.candidates(rows), [])
        back = [signal(card="K7Q2", regressed=True)]
        manager.candidates(back)
        self.assertEqual([s.key for s in manager.candidates(back)], ["ctest:panelayout"])

    def test_an_orphan_with_no_pane_goes_before_one_whose_pane_never_claimed_it(self):
        manager = threads()
        rows = [signal("ctest:a", first_session="pane-1", count=9), signal("ctest:b")]
        manager.candidates(rows)
        self.assertEqual([s.key for s in manager.candidates(rows)], ["ctest:b", "ctest:a"])


class StartTests(unittest.TestCase):
    """Starting one: the claim under the thread's id, the cap, the gates, the event."""

    def due(self, manager, rows, **kw):
        """Two folds, so the grace is spent and `start_due` actually starts something."""
        manager.start_due(rows, lambda s: "task for " + s.key, **kw)
        return manager.start_due(rows, lambda s: "task for " + s.key, **kw)

    def test_a_thread_claims_under_its_own_id_and_says_so(self):
        manager = threads()
        started = self.due(manager, [signal()])
        self.assertEqual([t.key for t in started], ["ctest:panelayout"])
        self.assertEqual(manager.written, [("claim", "ctest:panelayout", "th1")])
        self.assertEqual(manager.events, [{"event": "signal_thread", "state": "started",
                                           "key": "ctest:panelayout", "thread_id": "th1",
                                           "session_id": "owner-session"}])
        self.assertEqual(manager.tokens(), ["th1"])
        self.assertEqual(manager.key_of("th1"), "ctest:panelayout")

    def test_three_at_a_time_per_project(self):
        manager = threads()
        rows = [signal(f"ctest:t{n}") for n in range(6)]
        started = self.due(manager, rows)
        self.assertEqual(len(started), ST.MAX_SIGNAL_THREADS)
        self.assertEqual(manager.count(), 3)
        self.assertEqual(manager.room(), 0)
        # The rest are not forgotten: they start as slots free up.
        manager.finish("th1", outcome="fixed")
        self.assertEqual(len(manager.start_due(rows, lambda s: "t")), 1)

    def test_one_thread_per_key(self):
        manager = threads()
        self.due(manager, [signal()])
        self.assertEqual(manager.start_due([signal()], lambda s: "t"), [])
        self.assertEqual(len(manager.spawn.calls), 1)

    def test_auto_work_off_and_autonomy_off_each_stop_everything(self):
        off = threads()
        self.assertEqual(self.due(off, [signal()], auto_work_on=False), [])
        self.assertEqual(off.spawn.calls, [])
        # And turning it off does not silently start the clock, so the fold after it is turned
        # back on still owes the pane its grace.
        self.assertEqual(off.start_due([signal()], lambda s: "t"), [])

        quiet = threads()
        self.assertEqual(self.due(quiet, [signal()], autonomy="off"), [])
        self.assertEqual(quiet.spawn.calls, [])

    def test_a_spawn_that_cannot_start_leaves_the_signal_alone(self):
        manager = threads(FakeSpawn(fail=True))
        self.assertEqual(self.due(manager, [signal()]), [])
        self.assertEqual(manager.written, [])
        self.assertEqual(manager.events, [])
        self.assertEqual(manager.count(), 0)

    def test_the_task_it_is_started_with_names_the_fault_and_the_rules(self):
        manager = threads()
        one = signal(message="Assertion failed: rows == 3", card="K7Q2", regressed=True)
        manager.candidates([one])
        manager.start_due([one], lambda s: ST.task_text(s, project="/srv/p",
                                                        board_folder=".switchboard",
                                                        thread_id="th1"))
        task, description = manager.spawn.calls[0]
        self.assertEqual(description, "ctest:panelayout")   # the row's title is the key
        self.assertIn("ctest:panelayout", task)
        self.assertIn("Assertion failed: rows == 3", task)
        self.assertIn("card #K7Q2", task)
        self.assertIn("come back", task)                      # it regressed
        self.assertIn("--board .switchboard signals", task)   # a global flag, before the verb
        self.assertIn("--reason gave-up", task)
        self.assertIn("scripts/land.py", task)
        self.assertIn("scripts/relay-build", task)
        self.assertIn("who", task)                            # which paths others hold


class FinishTests(unittest.TestCase):
    """How it ends: the check's verdict, the release, and the cool-off on giving up."""

    def test_the_outcome_is_the_checks_verdict_not_the_agents(self):
        for state, outcome in (("resolved", "fixed"), ("removed", "fixed"),
                               ("dismissed", "dismissed"), ("open", "stopped")):
            self.assertEqual(ST.SignalThreads.outcome_for(signal(state=state)), outcome, state)
        self.assertEqual(ST.SignalThreads.outcome_for(signal(card="K7Q2")), "gave-up")
        self.assertEqual(ST.SignalThreads.outcome_for(None), "stopped")

    def test_finishing_frees_the_key_and_amends_the_notice(self):
        manager = threads()
        manager.candidates([signal()])
        manager.start_due([signal()], lambda s: "t")
        manager.events.clear()
        thread = manager.finish("th1", outcome="gave-up", card="K7Q2")
        self.assertEqual(thread.outcome, "gave-up")
        self.assertEqual(manager.events, [{"event": "signal_thread", "state": "finished",
                                           "key": "ctest:panelayout", "thread_id": "th1",
                                           "session_id": "owner-session",
                                           "outcome": "gave-up", "card": "K7Q2"}])
        self.assertEqual(manager.written[-1], ("release", "ctest:panelayout", "th1", "gave-up"))
        self.assertEqual(manager.count(), 0)
        self.assertIsNone(manager.finish("th1", outcome="fixed"))   # only once

    def test_a_key_a_thread_gave_up_on_waits_a_day_unless_it_regresses(self):
        manager = threads()
        manager.candidates([signal()])
        manager.start_due([signal()], lambda s: "t")
        manager.finish("th1", outcome="gave-up", card="K7Q2")
        rows = [signal()]
        manager.candidates(rows)
        self.assertEqual(manager.candidates(rows), [], "same hour: not again")
        back = [signal(regressed=True)]
        manager.candidates(back)
        self.assertEqual(len(manager.candidates(back)), 1, "a regression is a new failure")
        manager.at[0] += ST.GAVE_UP_HOURS * 3600 + 1
        later = [signal()]
        manager.candidates(later)
        self.assertEqual(len(manager.candidates(later)), 1, "a day later: fair game again")

    def test_shutdown_stops_every_thread_once(self):
        manager = threads()
        rows = [signal("ctest:a"), signal("ctest:b")]
        manager.candidates(rows)
        manager.start_due(rows, lambda s: "t")
        manager.events.clear()
        stopped = manager.stop_all()
        self.assertEqual({t.key for t in stopped}, {"ctest:a", "ctest:b"})
        self.assertEqual([e["outcome"] for e in manager.events], ["stopped", "stopped"])
        self.assertEqual(manager.stop_all(), [])


class WorkerTests(unittest.TestCase):
    """The fold's own hook, over a real board on disk: two folds, a thread, a promotion."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="rt-sigthreads-")
        self.project = Path(self.tmp.name) / "p"
        self.root = self.project / ".switchboard"
        (self.root / "changes").mkdir(parents=True)
        (self.root / "threads").mkdir(parents=True)
        (self.root / "features").mkdir(parents=True)
        self.root.joinpath("board.yaml").write_text(
            "version: 1\n"
            "tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\n"
            "columns: [inbox, executing, needs-verification, done]\n"
            "agent: {autonomy: auto}\n", encoding="utf-8")
        self.events: list[dict] = []
        self.commands = TP.TestsCommands(self.project, self.root, self.events.append)
        self.spawned: list[tuple] = []
        self.done = threading.Event()

        def spawn(task, description):
            self.spawned.append((task, description))
            return f"th{len(self.spawned)}", f"a{len(self.spawned)}", "owner-1", self.done

        self.commands.spawn_agent = spawn
        # Two failing executions of one key: the fold opens it (`signals.OPEN_AFTER_FAILURES`).
        H.append([H.Execution(ts="2026-09-20T10:00:00Z", id="ctest:beta", result="fail",
                              run_id="r1", runner="ctest", message="beta blew up", duration=0.2),
                  H.Execution(ts="2026-09-20T10:05:00Z", id="ctest:beta", result="fail",
                              run_id="r2", runner="ctest", message="beta blew up", duration=0.2)],
                 self.commands.store_path())

    def tearDown(self):
        self.tmp.cleanup()

    def folded(self):
        return self.commands.fold_signals()

    def test_two_folds_start_a_thread_that_claims_the_key(self):
        state = self.folded()
        self.assertEqual(state["ctest:beta"].state, "open")
        self.assertEqual(self.spawned, [], "the pane that ran it has this fold to claim it")
        self.folded()
        self.assertEqual(len(self.spawned), 1)
        self.assertIn("ctest:beta", self.spawned[0][0])
        # The claim is in the log under the thread's id, so the board's chip names the thread.
        state = self.commands.signal_state()
        self.assertEqual(state["ctest:beta"].session, "th1")
        started = [e for e in self.events if e.get("event") == "signal_thread"]
        self.assertEqual(started[0]["state"], "started")
        self.assertEqual(started[0]["thread_id"], "th1")
        self.assertEqual(started[0]["session_id"], "owner-1")

    def test_the_state_event_carries_the_setting_and_the_running_threads(self):
        self.folded()
        self.folded()
        changed = [e for e in self.events if e.get("event") == "signals_changed"][-1]
        self.assertTrue(changed["auto_work"])
        self.assertEqual(changed["threads"], [{"key": "ctest:beta", "thread_id": "th1",
                                               "session_id": "owner-1"}])

    def test_auto_work_false_in_board_yaml_starts_nothing(self):
        board = self.commands.board()
        B.write_config(board, ST.with_auto_work(board.config(), False))
        self.folded()
        self.folded()
        self.assertEqual(self.spawned, [])
        changed = [e for e in self.events if e.get("event") == "signals_changed"][-1]
        self.assertFalse(changed["auto_work"])

    def test_autonomy_off_starts_nothing(self):
        board = self.commands.board()
        config = board.config()
        config["agent"] = {"autonomy": "off"}
        B.write_config(board, config)
        self.folded()
        self.folded()
        self.assertEqual(self.spawned, [])

    def test_signals_config_writes_board_yaml_and_answers(self):
        out = self.commands.dispatch_signal("signals_config", {"id": "q1", "auto_work": False})
        self.assertEqual(out, {"kind": "config", "auto_work": False})
        written = [e for e in self.events if e.get("event") == "signals_written"][-1]
        self.assertEqual(written["id"], "q1")
        self.assertFalse(ST.auto_work(self.commands.board().config()))
        # And it round-trips: the row turns it back on.
        self.commands.dispatch_signal("signals_config", {"auto_work": True})
        self.assertTrue(ST.auto_work(self.commands.board().config()))
        bad = self.commands.dispatch_signal("signals_config", {"auto_work": "maybe"})
        self.assertEqual(bad["code"], "signal_refused")

    def test_a_thread_that_ends_with_the_check_still_red_promotes_and_says_gave_up(self):
        self.folded()
        self.folded()
        # `verify=False`: this project's key is not runnable from here (no build tree), so the
        # worker's own run of it would refuse and change nothing. `verify_signal` is exercised
        # against a real runnable key by docs/qa_evidence/2026-09-20-signal-threads/loop.py.
        event = self.commands.signal_thread_ended("th1", verify=False)
        self.assertEqual(event["state"], "finished")
        self.assertEqual(event["outcome"], "gave-up")
        card = event["card"]
        self.assertTrue(card)
        state = self.commands.signal_state()
        self.assertEqual(state["ctest:beta"].card, card)
        self.assertEqual(state["ctest:beta"].session, "", "the key is free again")
        # The card is a real bug card in the bugs tab, with the machine's own section.
        found = [c for c in B.Board(self.root, self.project).cards() if c.id == card]
        self.assertEqual(len(found), 1)
        self.assertIn(S.SIGNAL_LABEL, found[0].front.get("labels") or [])

    def test_a_thread_whose_check_went_green_says_fixed(self):
        self.folded()
        self.folded()
        H.append([H.Execution(ts="2026-09-20T11:00:00Z", id="ctest:beta", result="pass",
                              run_id="r3", runner="ctest", duration=0.2),
                  H.Execution(ts="2026-09-20T11:01:00Z", id="ctest:beta", result="pass",
                              run_id="r4", runner="ctest", duration=0.2)],
                 self.commands.store_path())
        event = self.commands.signal_thread_ended("th1", verify=False)
        self.assertEqual(event["outcome"], "fixed")
        self.assertNotIn("card", event)
        self.assertEqual(self.commands.signal_state()["ctest:beta"].state, "resolved")

    def test_the_worker_runs_the_check_itself_before_it_judges(self):
        """A thread's own `ctest` is a subprocess in its own shell and lands in no store, so the
        fold has seen nothing since the failure. Unless the worker runs the key, every thread that
        fixed its test still reads as open — which is how the live run of
        docs/qa_evidence/2026-09-20-signal-threads/loop.py first came out `gave-up`."""
        self.folded()
        self.folded()
        asked: list[list[str]] = []

        def fake_run(ids, **kw):
            asked.append(list(ids))
            H.append([H.Execution(ts=H.now_iso(), id=ids[0], result="pass",
                                  run_id=f"v{len(asked)}", runner="ctest", duration=0.1)],
                     self.commands.store_path())
            return {"tests": [{"id": ids[0], "result": "pass"}]}

        self.commands.run_and_wait = fake_run
        event = self.commands.signal_thread_ended("th1")
        # Two runs, because `RESOLVE_PASSES["broken"]` is 2 and nothing else resolves a signal.
        self.assertEqual(asked, [["ctest:beta"], ["ctest:beta"]])
        self.assertEqual(event["outcome"], "fixed")
        self.assertEqual(self.commands.signal_state()["ctest:beta"].state, "resolved")

    def test_a_check_that_fails_again_stops_after_one_run(self):
        self.folded()
        self.folded()
        asked: list[list[str]] = []
        self.commands.run_and_wait = lambda ids, **kw: (
            asked.append(list(ids)) or {"tests": [{"id": ids[0], "result": "fail"}]})
        event = self.commands.signal_thread_ended("th1")
        self.assertEqual(len(asked), 1, "a failure ends it: running it again proves nothing")
        self.assertEqual(event["outcome"], "gave-up")

    def test_the_agents_own_instance_never_picks_anything_up(self):
        plain = TP.TestsCommands(self.project, self.root, lambda event: None)
        self.assertIsNone(plain.signal_threads())
        plain.fold_signals()
        plain.fold_signals()
        self.assertEqual(self.spawned, [])


if __name__ == "__main__":
    unittest.main()
