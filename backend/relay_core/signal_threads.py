# SPDX-License-Identifier: AGPL-3.0-or-later
"""Signal threads: the unasked pickup of a fault nobody is on (card `#AQ6X`, decision 9, step 7b).

Decision 9 is the owner's, in two sentences on the card.  *"6 -- i think yes by default, but its
optional"*: agents work unclaimed signals without being asked, and the board can be told not to.
And: *"you get a notification that you can click on to open the agent thread, and those go into
the sessions manger"* — so a pickup is **visible**, it is **its own thread**, and it is listed
where every other thread is.  It is deliberately not a turn in whatever pane the user happens to
be typing in: a background fix that stole the pane would be the opposite of visible.

So, after every fold (`tests_protocol.fold_signals`), a signal that nobody is on becomes a
subagent thread of the board worker.  This module holds the decision and the bookkeeping; it
never spawns anything itself — it is handed a `spawn` and an `emit`, which is why every rule below
is a unit test rather than a live run.

## Which signals (and what "orphaned" means here)

`open`, unclaimed (`session` empty), not dismissed, not promoted — and **unclaimed for longer
than one fold**.  That last clause is the honest form of "no live pane owns it".  A signal's log
says which *run* opened it and which pane token that run belonged to (`signals.Signal.first_session`),
but a pane token belongs to another worker process: this worker cannot ask whether that pane is
still on screen, and a claim it cannot see is a claim it must not steal.  What it can see is that
the key went through a whole fold with nobody claiming it — and a fold happens at the end of
every run, which is exactly when the pane that broke it was told it was its own (step 7a,
`tests_protocol._opened_lines`).  One fold of grace, then it is fair game.  `first_session` is
still used, to put a signal whose own pane never claimed it **after** one that arrived orphaned.

A **promoted** signal is a person's card and not a thread's to race — with one exception, which is
why `task_text` can name a card: a signal that was promoted, resolved and then came back
(`regressed`) is a regression nobody is on, and the card is the context the thread needs.

## The limits

`MAX_SIGNAL_THREADS` (3) running per project, `board.yaml`'s `signals: {auto_work: …}` (default
true when the block or the key is absent), and never at all when `agent.autonomy` is `off` — the
setting that already means "this board writes nothing by itself".  One thread per key at a time,
and a key a thread gave up on is not retried for `GAVE_UP_HOURS` (24) unless it regresses: a
second agent re-reading the same failure the same hour is how a machine loops.

## The claim

The worker claims the key **for** the thread, under the thread's own id, before the thread's first
step: the board's chip then names the thread rather than showing nothing for the minute the agent
takes to get going, and two folds in a row cannot start two threads on one key.  The thread
releases it itself when it gives up (`relay-board.py signals release <key> --reason gave-up`,
which promotes), and the worker releases it as a safety net when the thread ends still holding it.
"""
from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Callable, Iterable

from . import signals as S

#: Signal threads running at once, per project (decision 9: "at most three at a time per
#: project").  A ceiling on machine load, not a policy: the fourth orphan waits for a free slot.
MAX_SIGNAL_THREADS = 3

#: A key a thread released with `gave-up` is not picked up again for this long.  It has a bug card
#: by then (promotion trigger (a) of R9), so the retry would be a second agent re-reading the same
#: failure — and a regression inside the window is a different failure and is not held back.
GAVE_UP_HOURS = 24

#: The subagent definition a signal thread runs on (`agents_defs.BUILTINS`).  Its own rather than
#: `general`, so the Sessions manager can tell a pickup from one of the user's own subagents by
#: `agent_type` and list it without being asked to.  Its tools are `agents_defs.SUBAGENT_TOOLS` —
#: files, commands and skills — which is what a fix needs; the `board_*` tools are the pane's and a
#: subagent never has them, so the thread reaches the board through `scripts/relay-board.py`,
#: exactly as a guest session does (`<board>/POLICY.md`).
THREAD_AGENT_TYPE = "signal"

#: `state` of the `signal_thread` event (protocol §32.4).
STATES = ("started", "finished")
#: `outcome` on the `finished` event, in the order the fold is read for it.
OUTCOMES = ("fixed", "gave-up", "dismissed", "stopped")

#: `signals: {auto_work: …}` in `board.yaml` — and what its absence means.  Default **true** is
#: the owner's "yes by default": a board written before this existed works its signals.
AUTO_WORK_DEFAULT = True


def auto_work(config: dict | None) -> bool:
    """Whether this board works its unclaimed signals unasked, from its `board.yaml` dict."""
    block = (config or {}).get("signals") if isinstance(config, dict) else None
    value = block.get("auto_work") if isinstance(block, dict) else None
    return AUTO_WORK_DEFAULT if not isinstance(value, bool) else value


def with_auto_work(config: dict | None, on: bool) -> dict:
    """`config` with `signals.auto_work` set — the whole dict, for `board.write_config`.

    Every other key of the block and of the file is carried through untouched: the Options row
    writes one flag, and a board that has other `signals:` settings one day keeps them.
    """
    out = dict(config or {})
    block = out.get("signals")
    block = dict(block) if isinstance(block, dict) else {}
    block["auto_work"] = bool(on)
    out["signals"] = block
    return out


def description(signal: "S.Signal") -> str:
    """The subagent's one-line description — and the thread's **title** in the Sessions manager.

    The key alone, because that is what the row has to say: "signal ctest:panelayout" would read as
    "signal signal ctest:panelayout" beside the row's own `signal` mark.
    """
    return signal.key[:200]


def task_text(signal: "S.Signal", *, project: str, board_folder: str = "",
              thread_id: str = "", script: str = "scripts/relay-board.py") -> str:
    """The whole task a signal thread is started with: the fault, and the rules.

    It says the key, the failure in the check's own words, the card when there is one, and how to
    reach the board from a shell — a subagent has `run_command`, not `board_signals`.  The rules
    are the card's: fix it, re-run until the signal resolves, give up out loud rather than
    silently, land through `scripts/land.py`, and never touch a file another session holds.
    """
    # `--board` is a *global* flag of `relay-board.py`, so it goes before the subcommand.
    board = f"--board {board_folder} " if board_folder else ""
    lines = [
        f"A test in {project} is failing and nobody is working on it. Fix it.",
        "",
        f"The signal is `{signal.key}` ({signal.kind}); it has failed {signal.count} "
        f"execution(s), last at {signal.last_seen or 'an unrecorded time'}.",
    ]
    if signal.regressed:
        lines.append("It was fixed once and has come back, so look at what changed since.")
    if signal.card:
        lines.append(f"It is already card #{signal.card} on the Switchboard: read that card "
                     "before you start, and put what you find on it.")
    if signal.message:
        lines += ["", "The failure, in the check's own words:", "```", signal.message[:600], "```"]
    elif signal.excerpt:
        lines += ["", "The failure, in the check's own words:", "```", signal.excerpt[:600], "```"]
    lines += [
        "",
        "Relay has already claimed this signal for you"
        + (f" under the session token `{thread_id}`" if thread_id else "")
        + f": `python3 {script} {board}signals` lists the board's signals and shows the claim. "
        "Nobody else will take it while you hold it.",
        "",
        "How this ends, in the order you should try:",
        f"1. Fix the cause. Relay runs the check itself when you stop, "
        f"{S.RESOLVE_PASSES.get(signal.kind, 2)} time(s) — that is what resolves the signal and "
        "nothing else does — so run it yourself to see whether your fix works, and stop when it "
        "passes. Your report does not close anything; the check does.",
        f"2. If the failure is not the code's fault and you can *show* that it is not: "
        f"`python3 {script} {board}signals dismiss {signal.key} --reason environmental|"
        "flaky-known --comment '<what you checked>' --until <YYYY-MM-DD, at most a week out>`.",
        f"3. If you cannot fix it, say so out loud rather than leaving it: "
        f"`python3 {script} {board}signals release {signal.key} --reason gave-up"
        + (f" --as {thread_id}" if thread_id else "")
        + "`, which files it as a bug card for a person. Then report what you tried and what you "
        "believe is wrong.",
        "",
        "House rules for this checkout (they are the owner's, in CLAUDE.md and WARP.md — read "
        "both before you write anything):",
        "- Commit only through `python3 scripts/land.py begin <name> <paths>` and "
        "`python3 scripts/land.py commit <name> -m \"…\"`. Never `git add`, `git commit`, "
        "`git stash`, `git checkout` or `git reset`: several sessions share this working tree.",
        "- Claim the paths immediately before you edit them and dry-run every commit "
        "(`commit --dry-run`): a stale snapshot lands somebody else's work.",
        "- `python3 scripts/land.py who` says which paths other live sessions hold. If your fix "
        "needs one of them, do not edit it — release the signal with reason `gave-up`, naming the "
        "file and the session, and let a person decide.",
        "- Build through `scripts/relay-build`, never `cmake --build`. Run the targeted tests for "
        "what you changed, never a whole suite.",
        "",
        "You were started by Relay, not by a person, and nobody is watching this run: do not ask "
        "questions, and do not do anything the fault does not need.",
    ]
    return "\n".join(lines)


@dataclass
class SignalThread:
    """One running (or just-finished) signal thread, as the worker and the event see it."""

    key: str
    thread_id: str = ""
    agent_id: str = ""
    session_id: str = ""        # the owner session the thread file is saved beside
    card: str = ""
    started: float = 0.0
    outcome: str = ""

    def event(self, state: str) -> dict:
        """The `signal_thread` event for this thread (protocol §32.4)."""
        out = {"event": "signal_thread", "state": state, "key": self.key,
               "thread_id": self.thread_id, "session_id": self.session_id}
        if state == "finished":
            out["outcome"] = self.outcome or "stopped"
        if self.card:
            out["card"] = self.card
        return out


class SignalThreads:
    """The signal threads of one project: which are running, which key is next, and the events.

    Collaborators, in the shape the rest of the backend uses (handed in, never found):

    * `spawn(task, description)` starts the subagent and answers
      `(thread_id, agent_id, session_id)`, or None when it could not start one.  It is also where
      the caller arranges to hear that the thread ended — this class owns no threads.
    * `emit(event)` puts the `signal_thread` event on the wire.
    * `claim(key, token)` and `release(key, token, reason)` write the two log lines.
    """

    def __init__(self, spawn: Callable[[str, str], tuple | None],
                 emit: Callable[[dict], None] | None = None, *,
                 claim: Callable[[str, str], None] | None = None,
                 release: Callable[[str, str, str], None] | None = None,
                 max_threads: int = MAX_SIGNAL_THREADS,
                 clock: Callable[[], float] = time.time):
        self._spawn = spawn
        self._emit = emit or (lambda event: None)
        self._claim = claim
        self._release = release
        self.max_threads = max(0, int(max_threads))
        self.clock = clock
        #: key -> SignalThread, for the threads that are running right now.
        self._running: dict[str, SignalThread] = {}
        #: thread id -> key, so a finish that knows only the thread can find the signal.
        self._by_thread: dict[str, str] = {}
        #: key -> the clock at which it was first seen orphaned, which is what "for longer than
        #: one fold" is measured against.  Dropped as soon as the key is claimed or closed.
        self._waiting: dict[str, float] = {}
        #: key -> the clock at which a thread gave up on it (`GAVE_UP_HOURS`).
        self._gave_up: dict[str, float] = {}

    # ---- what is running -------------------------------------------------------
    def running(self) -> list[SignalThread]:
        return list(self._running.values())

    def count(self) -> int:
        return len(self._running)

    def room(self) -> int:
        return max(0, self.max_threads - len(self._running))

    def holds(self, key: str) -> bool:
        return key in self._running

    def thread_of(self, key: str) -> SignalThread | None:
        return self._running.get(key)

    def key_of(self, thread_id: str) -> str:
        """The signal a running thread is working on, or "" — the worker's way in from an event."""
        return self._by_thread.get(str(thread_id or ""), "")

    def tokens(self) -> list[str]:
        """The thread ids the running threads claim under — the tokens a chip may read as live."""
        return [thread.thread_id for thread in self._running.values() if thread.thread_id]

    # ---- the decision ----------------------------------------------------------
    def candidates(self, signals: Iterable["S.Signal"], *, now: float | None = None,
                   ) -> list["S.Signal"]:
        """The orphans this fold may start a thread on, best first.

        Called once per fold, and it *is* the fold's memory: a key that is eligible now and was
        not eligible at the previous call is recorded and skipped, so the pane whose own run
        opened it has that fold to claim it (step 7a).  A key that has been eligible since an
        earlier fold is returned.
        """
        at = self.clock() if now is None else now
        cutoff = at - GAVE_UP_HOURS * 3600
        eligible: list[S.Signal] = []
        for signal in S.sort_signals(signals):
            if not self._eligible(signal, cutoff):
                continue
            eligible.append(signal)
        keys = {signal.key for signal in eligible}
        for key in list(self._waiting):
            if key not in keys:
                self._waiting.pop(key, None)        # claimed, closed, or gone: no grace owed
        due: list[S.Signal] = []
        for signal in eligible:
            if signal.key in self._waiting:
                due.append(signal)
            else:
                self._waiting[signal.key] = at      # its one fold of grace starts now
        # An orphan whose own pane never claimed it goes after one that arrived with no pane at
        # all: the first is somebody's to answer for, and they may still turn up.
        due.sort(key=lambda s: (1 if s.first_session else 0,))
        return due

    def _eligible(self, signal: "S.Signal", cutoff: float) -> bool:
        if signal.state != "open":
            return False                            # pending, resolved, dismissed, removed
        if signal.session:
            return False                            # somebody holds it, pane or thread
        if signal.key in self._running:
            return False                            # one thread per key
        if signal.card and not signal.regressed:
            return False                            # a person's card now, not a thread's race
        if signal.group:
            return False                            # collapsed under a container; that is the item
        gave_up = self._gave_up.get(signal.key)
        if gave_up is not None and gave_up > cutoff and not signal.regressed:
            return False
        return True

    # ---- starting and finishing -------------------------------------------------
    def start(self, signal: "S.Signal", task: str, *, now: float | None = None,
              ) -> SignalThread | None:
        """Spawn one thread, claim the key under its id, and emit `signal_thread started`.

        None when there is no room, when the key is already held, or when `spawn` could not start
        an agent — every one of which is a reason to leave the signal where it is, not an error.
        """
        if self.room() <= 0 or signal.key in self._running:
            return None
        spawned = self._spawn(task, description(signal))
        if not spawned:
            return None
        thread_id, agent_id, session_id = (list(spawned) + ["", "", ""])[:3]
        thread = SignalThread(key=signal.key, thread_id=str(thread_id or ""),
                              agent_id=str(agent_id or ""), session_id=str(session_id or ""),
                              card=signal.card, started=self.clock() if now is None else now)
        self._running[signal.key] = thread
        if thread.thread_id:
            self._by_thread[thread.thread_id] = signal.key
        self._waiting.pop(signal.key, None)
        self._gave_up.pop(signal.key, None)
        if self._claim is not None and thread.thread_id:
            self._claim(signal.key, thread.thread_id)
        self._emit(thread.event("started"))
        return thread

    def start_due(self, signals: Iterable["S.Signal"], task: Callable[["S.Signal"], str], *,
                  auto_work_on: bool = True, autonomy: str = "auto",
                  now: float | None = None) -> list[SignalThread]:
        """One fold's worth of pickups: the gates, then `candidates`, then `start` up to the cap.

        `auto_work_on` is `board.yaml`'s `signals.auto_work` and `autonomy` its
        `agent.autonomy` — off means this board does nothing by itself, signals included, and it
        is checked before the grace bookkeeping so turning autonomy off does not silently start
        the clock on every open signal.
        """
        if not auto_work_on or autonomy == "off" or self.max_threads <= 0:
            return []
        started: list[SignalThread] = []
        for signal in self.candidates(signals, now=now):
            if self.room() <= 0:
                break
            thread = self.start(signal, task(signal), now=now)
            if thread is not None:
                started.append(thread)
        return started

    @staticmethod
    def outcome_for(signal: "S.Signal | None") -> str:
        """What the finished event says happened, read off the signal the thread was working on.

        The **check's** verdict, never the agent's report: an agent that says it fixed a test and
        did not is the one failure mode this whole card exists to close, so `resolved` (or
        `removed`, the key having left discovery) is the only thing that counts as fixed.  A key
        that has gone from the fold entirely is `stopped`: nothing says it passed.
        """
        if signal is None:
            return "stopped"
        if signal.state in ("resolved", "removed"):
            return "fixed"
        if signal.state == "dismissed":
            return "dismissed"
        if signal.card:
            return "gave-up"
        return "stopped"

    def finish(self, thread_id: str, *, outcome: str, card: str = "",
               reason: str = "") -> SignalThread | None:
        """Take a thread off the running list, release what it still holds, and say how it went.

        `reason` is written on the release line when the worker has to make one — a thread that
        released the key itself is already free and nothing is written twice, because the release
        only happens for a key this thread is recorded against.
        """
        key = self._by_thread.pop(str(thread_id or ""), "")
        thread = self._running.pop(key, None) if key else None
        if thread is None:
            return None
        thread.outcome = outcome if outcome in OUTCOMES else "stopped"
        if card:
            thread.card = card
        if thread.outcome == "gave-up":
            self._gave_up[key] = self.clock()
        self._waiting.pop(key, None)
        if self._release is not None and thread.thread_id:
            self._release(key, thread.thread_id, reason or thread.outcome)
        self._emit(thread.event("finished"))
        return thread

    def stop_all(self) -> list[SignalThread]:
        """The worker is going: every thread is `stopped`, and each says so once."""
        out = []
        for thread_id in list(self._by_thread):
            thread = self.finish(thread_id, outcome="stopped")
            if thread is not None:
                out.append(thread)
        return out
