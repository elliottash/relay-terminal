# SPDX-License-Identifier: AGPL-3.0-or-later
"""Signals: one keyed, machine-opened, machine-closed item per fault (card `#AQ6X`).

A *signal* is what a failing check becomes so that nobody has to remember it.  It is **not** a
card and **not** in git (decision 1): it is a fold over two local files under the board's private
root — `test_history`'s `tests/history.jsonl`, which holds every execution, and this module's
`signals/events.jsonl`, which holds the human and agent *actions* (claim, release, dismiss,
promote, note) plus one `run` line per run so the fold knows which pane's run saw what.  Nothing
here stores an occurrence a second time (research R1, R13).

The whole state is therefore a pure function, and it is the only thing worth testing:

    fold(executions, events, now, discovered=...) -> {key: Signal}

`docs/SIGNALS-RESEARCH.md` is the reasoning and every threshold below cites it.  The decisions on
card `#AQ6X` are the spec; where the two differ the card wins.

## The key, and the fingerprint

The key is the source plus the check's own identity, and nothing else — no commit, no branch,
never the failure text (R2):

    ctest:<name>            unittest:<module.Class.test>        build:<target>
    check:<code>:<path>     crash:<signal>:<frame>              ci:<workflow>:<job>

The **fingerprint** is the failure message with timestamps, paths, hex and numbers replaced by
`%`.  It groups keys and it says "this now fails differently"; it is never part of the key.

## The states (R3, decision 2)

`pending` seen failing once — the pane that ran it is told, nobody else, and a pass deletes it
without trace.  `open` counted on the board and claimable.  `resolved` machine only: the way to
resolve a signal is to run the check.  `dismissed` a person, or an agent within limits, with a
reason, a comment and a **required** expiry; occurrences keep counting underneath and it blocks
nothing.  `removed` the key left discovery — not a fix, and not counted as one.

## What advances a signal (decisions 3 and 4)

*Consecutive executions of that key*, not runs: most runs here are `ctest -R` subsets, so a run
that did not execute the key advances nothing at all.  Neither do `skip`, `timeout` and a key
inhibited by a build failure: those are **not evaluated** (R5, R7a).

* `pending` on the first failing execution; `open` on the second **consecutive** one.
* `resolved` on `RESOLVE_PASSES[kind]` consecutive passing executions — 2 for `broken`, 20 for
  `flaky`, because 2 green runs mean nothing for a test that fails one time in ten (R5).
* `kind` is `flaky` when `test_history`'s own rule says so: a pass *and* a fail on one tree
  (Datadog), or `flake_score >= FLAKE_FLAKY` over the last `FLAKE_WINDOW` executions.  The mark
  is sticky — once a test has proven it flips, two passes are not a repair.  A flaky signal
  *opens* only on the score, so one fail-pass marks it without putting it on the board (R4).
* One tree, not one commit: several sessions edit this checkout at once, so the Datadog rule is
  read over `(commit, tree_digest)` — which is what `test_history.Execution.tree_digest` (#AQ6X
  step 1) exists for.
* Reopen within `REGRESS_DAYS` as `regressed`, keeping the count and skipping `pending`; later, a
  new signal whose `previous` names the old key and its `first_seen` (R8).
* `stale` after `STALE_DAYS` unseen — re-run first, never closed by a timer (R6).

## One cause, one item (R7, decision 3's "at most ten")

In this order, per run: a failing `build:<target>` inhibits every test in that run; else a **red
run** (more than `RED_RUN_FRACTION` of at least `RED_RUN_MIN_EXECUTED` executed keys failing)
opens one `run:<runner>` signal and nothing else; else `GROUP_MIN_KEYS` keys sharing one
fingerprint become one `group:` signal; and at most `MAX_SIGNALS_PER_RUN` keys are itemised, the
overflow rolling into that run's `run:<runner>` signal.  A collapsed key stays `pending`: it is
counted and listed under its container, and it cannot reach `open` on its own.

Those four numbers are guesses, marked as such in the research, to tune on the first month of
history — which is why they are named constants and not literals.

## Promotion (R9, decision 5)

Never age alone.  A signal becomes a bug card when the agent holding it gives up, when it has
failed in `PROMOTE_MIN_RUNS` runs over `PROMOTE_MIN_HOURS` with nobody holding it, or when it is
confirmed flaky — and at most `MAX_PROMOTED_OPEN` promoted cards are open at a time.  The card
is written by `board_tools`, which owns every board write; this module owns the rules and the
text of the machine-owned `## Signal` section (`signal_section`).
"""
from __future__ import annotations

import hashlib
import json
import os
import re
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Iterable, Sequence

from . import board as B
from . import test_history as H

#: Schema version of an event line and of `Signal.to_dict()`.
SIGNAL_VERSION = 1

# --------------------------------------------------------------------------- knobs
#
# Every number here is cited.  `docs/SIGNALS-RESEARCH.md` R-numbers are the argument; the four
# marked GUESS are the ones §11 R7 says to tune against the first month of `history.jsonl`.

OPEN_AFTER_FAILURES = 2         # R4: open on the second *consecutive* failing execution
#: R4 and R10: the debounce is for sources whose evaluation is *partial* and whose tree is shared
#: — a first failure there is as likely to be another session's half-saved edit as a fault.  A
#: build failure, a `check` problem, a fingerprint group of `GROUP_MIN_KEYS` keys and a red run
#: are each complete evidence the moment they happen, so they open at once.
DEBOUNCED_SOURCES = ("ctest", "unittest", "ci")
RESOLVE_PASSES = {              # R5: consecutive passing executions that resolve a signal
    "broken": 2,                #   TestGrid's two green runs, right for the deterministic case
    "flaky": 20,                #   Datadog's number; cheap here because Relay can just re-run
    "group": 2,                 #   a group is resolved by its members passing
    "run": 2,
    "build": 2,
}
FLAKE_WINDOW = 21               # R4: the Nagios-style window the flakiness statistic reads
STALE_DAYS = 7                  # R6: unseen this long is `stale`, re-run first, never closed
REGRESS_DAYS = 30               # R8: reopen as `regressed` inside this, a new signal after it
RETENTION_DAYS = 30             # R13: a resolved or removed signal stays folded this long
GROUP_MIN_KEYS = 3              # R7b GUESS: keys sharing one fingerprint that make a group
RED_RUN_MIN_EXECUTED = 10       # R7c GUESS: a run smaller than this is never called red
RED_RUN_FRACTION = 0.5          # R7c GUESS: more than this share failing is one `run:` signal
MAX_SIGNALS_PER_RUN = 10        # R7 GUESS: individual signals one run may itemise
PROMOTE_MIN_RUNS = 3            # R9b: failing runs with no claim before promotion
PROMOTE_MIN_HOURS = 24          # R9b: …spread over at least this long (Mozilla's 3-in-7, floored)
MAX_PROMOTED_OPEN = 5           # R9: promoted cards open at once; the overflow stays in the fold
RERUN_MAX_FAILURES = 10         # decision 3: failures Relay re-runs once before folding
RERUN_MAX_SECONDS = 60.0        # decision 3: …when their recorded p50 durations sum to this
AGENT_DISMISS_MAX_DAYS = 7      # decision 7: an agent's dismissal expires within a week
MAX_EXCERPT = H.MAX_EXCERPT     # one store's excerpt rule, not two
MAX_FINGERPRINT = 200
MAX_COMMENT = 2000
MAX_MEMBERS = 200               # members listed on one group or run signal

#: R3.  `removed` is the key leaving discovery, which is not a fix.
STATES = ("pending", "open", "resolved", "dismissed", "removed")
OPEN_STATES = ("pending", "open", "dismissed")
KINDS = ("broken", "flaky", "group", "run", "build")

#: R3 / decision 7.  Every dismissal expires; an agent may reach only the first two, with a
#: comment and at most `AGENT_DISMISS_MAX_DAYS`.  `wont-fix` and `expected` are the owner's.
DISMISS_REASONS = ("environmental", "flaky-known", "wont-fix", "expected")
AGENT_DISMISS_REASONS = ("environmental", "flaky-known")

#: What an event line may say.  `run` is this module's own addition to the card's list: the
#: executions do not carry a pane token, and the verification gate of decision 8 needs to know
#: *whose* run first failed a key, so `tests_protocol` writes one `run` line per run.
ACTIONS = ("claim", "release", "dismiss", "promote", "note", "run")

#: The reason a `release` gives when the agent could not fix it — promotion trigger (a) of R9.
GAVE_UP = "gave-up"

#: Where the events live inside a board, beside `tests/history.jsonl`.
STORE_DIR = "signals"
STORE_NAME = "events.jsonl"

#: The machine-owned section on a promoted card.  Rewritten in place on every state change of the
#: signal, never as a comment, and it never touches the person's text (R9).
SIGNAL_HEADING = "Signal"

#: The label a promoted card carries, so the bugs tab can tell machine-written cards apart.
SIGNAL_LABEL = "signal"

#: Results that advance nothing (R5, decision 4).  A `skip` and a harness `timeout` are not a
#: verdict about the code, and neither is a test that was never built.
NOT_EVALUATED = ("skip", "timeout")
FAILING = ("fail", "error")


class SignalError(ValueError):
    """A refusal, with the one-sentence message the caller shows and a machine code."""

    def __init__(self, message: str, code: str = "signal_refused", **fields):
        super().__init__(message)
        self.code = code
        self.fields = fields

    def to_result(self) -> dict:
        return {"error": str(self), "code": self.code, **self.fields}


# --------------------------------------------------------------------------- time

def now_iso() -> str:
    return H.now_iso()


def _ts(value) -> datetime | None:
    if isinstance(value, datetime):
        return value if value.tzinfo else value.replace(tzinfo=timezone.utc)
    return H._parse_ts(str(value or ""))


def _at(value, default: datetime | None = None) -> datetime:
    parsed = _ts(value)
    if parsed is not None:
        return parsed
    return default if default is not None else datetime(1970, 1, 1, tzinfo=timezone.utc)


def _iso(when: datetime) -> str:
    return when.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# --------------------------------------------------------------------------- the fingerprint

_TIMESTAMP_RE = re.compile(
    r"\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})?"
    r"|\d{2}:\d{2}:\d{2}(?:\.\d+)?")
_PATH_RE = re.compile(r"(?:[A-Za-z]:)?(?:/[\w.+@%-]+){2,}/?")
_HEX_RE = re.compile(r"\b(?:0[xX])?[0-9a-fA-F]{6,}\b")
_NUMBER_RE = re.compile(r"\b\d+(?:[.,]\d+)?\b")
_SQUASH_RE = re.compile(r"%(?:[\s:,]*%)+")


def _hexish(match: re.Match) -> str:
    """A hex run is only a hex run when it has a digit in it — `decade` is a word."""
    text = match.group(0)
    return "%" if any(c.isdigit() for c in text) else text


def fingerprint(message: str) -> str:
    """The failure message with timestamps, absolute paths, hex and numbers replaced by `%`.

    Order matters and is the reason this is one function rather than four `sub`s at the call
    site: a timestamp is made of numbers and a path may hold both, so the widest pattern goes
    first.  Runs of `%` separated by nothing but punctuation collapse into one, so
    `expected 3, got 17` and `expected 1, got 2` fingerprint alike, which is what makes
    `GROUP_MIN_KEYS` keys "the same failure" (R2, R7b).
    """
    text = " ".join(str(message or "").split())
    if not text:
        return ""
    text = _TIMESTAMP_RE.sub("%", text)
    text = _PATH_RE.sub("%", text)
    text = _HEX_RE.sub(_hexish, text)
    text = _NUMBER_RE.sub("%", text)
    text = _SQUASH_RE.sub("%", text)
    return text.strip()[:MAX_FINGERPRINT]


def group_key(print_: str) -> str:
    """`group:<12 hex>` for a fingerprint — stable across runs, so a group can be counted."""
    digest = hashlib.sha256((print_ or "").encode("utf-8")).hexdigest()[:12]
    return f"group:{digest}"


def source_of(key: str) -> str:
    """`ctest`, `unittest`, `build`, `check`, `group`, `run`, … — the key's first segment."""
    return str(key or "").split(":", 1)[0]


# --------------------------------------------------------------------------- the record

@dataclass
class Signal:
    """One fault, folded.  `to_dict()` is the wire shape the GUI and the tools both read."""

    key: str
    source: str = ""
    kind: str = "broken"
    state: str = "pending"
    first_seen: str = ""
    last_seen: str = ""
    count: int = 0                      # failing executions
    green_streak: int = 0               # passing executions since the last failure
    runs: int = 0                       # distinct runs that failed it
    fingerprint: str = ""
    excerpt: str = ""
    message: str = ""
    regressed: bool = False
    stale: bool = False
    session: str = ""                   # the claim, as on a card
    card: str = ""                      # set by promotion
    dismissed: dict | None = None       # {reason, comment, until, by, expired}
    fixed_in: str = ""                  # the commit of the run that resolved it
    resolved_at: str = ""
    inhibited_by: str = ""              # e.g. "build:relay"
    group: str = ""                     # the container this key was collapsed into
    members: list[str] = field(default_factory=list)      # a container's members
    previous: dict | None = None         # {key, first_seen} of the signal this one succeeds
    first_session: str = ""             # pane token of the run that first failed it (decision 8)
    first_run: str = ""
    opened_run: str = ""
    sessions: list[str] = field(default_factory=list)
    promote: str = ""                   # why it is eligible: gave-up | persistent | flaky
    gave_up: str = ""                   # the reason the claiming agent released it with

    def to_dict(self) -> dict:
        out = {
            "key": self.key, "source": self.source, "kind": self.kind, "state": self.state,
            "first_seen": self.first_seen, "last_seen": self.last_seen, "count": self.count,
            "green_streak": self.green_streak, "runs": self.runs,
            "fingerprint": self.fingerprint, "regressed": self.regressed, "stale": self.stale,
            "version": SIGNAL_VERSION,
        }
        for name in ("excerpt", "message", "session", "card", "fixed_in", "resolved_at",
                     "inhibited_by", "group", "promote", "gave_up", "first_session",
                     "first_run", "opened_run"):
            value = getattr(self, name)
            if value:
                out[name] = value
        if self.dismissed:
            out["dismissed"] = dict(self.dismissed)
        if self.members:
            out["members"] = list(self.members[:MAX_MEMBERS])
        if self.previous:
            out["previous"] = dict(self.previous)
        return out

    # ---- the two questions every surface asks --------------------------------
    @property
    def is_open(self) -> bool:
        """On the board and countable: `open`, or `dismissed` while the dismissal holds."""
        return self.state in ("open", "dismissed")

    @property
    def blocks_verification(self) -> bool:
        """A dismissed signal never blocks (decision 7); a resolved one has nothing to say."""
        return self.state == "open"


# --------------------------------------------------------------------------- the store

def default_path(project: str | os.PathLike, board_root: str | os.PathLike | None = None) -> Path:
    """`<board>/.private/signals/events.jsonl` — beside the history it is folded with."""
    project = Path(project).expanduser()
    root = Path(board_root) if board_root is not None else B.board_folder(project)
    if root is None:
        root = project / B.DEFAULT_BOARD_FOLDER
    return B.Board(root, project).private_root() / STORE_DIR / STORE_NAME


def append_event(event: dict, path: str | os.PathLike) -> dict:
    """Append one action; returns the line as it was written (with `ts` filled in).

    One `O_APPEND` `os.write` of one line, exactly as `test_history.append` does it, so the
    several worker processes that write here interleave lines but never interleave within one.
    """
    row = dict(event or {})
    action = str(row.get("action") or "")
    if action not in ACTIONS:
        raise SignalError(f"unknown signal action {action!r}; use one of {', '.join(ACTIONS)}.")
    row.setdefault("ts", now_iso())
    row.setdefault("v", SIGNAL_VERSION)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    blob = (json.dumps(row, separators=(",", ":"), sort_keys=True) + "\n").encode("utf-8")
    fd = os.open(str(path), os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o644)
    try:
        written = 0
        while written < len(blob):
            written += os.write(fd, blob[written:])
    finally:
        os.close(fd)
    return row


def read_events(path: str | os.PathLike) -> list[dict]:
    """Every action in the log, oldest first, tolerantly: a torn last line is skipped."""
    out: list[dict] = []
    try:
        text = Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return out
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
        except ValueError:
            continue
        if isinstance(row, dict) and str(row.get("action") or "") in ACTIONS:
            out.append(row)
    out.sort(key=lambda r: (str(r.get("ts") or ""),))
    return out


# --------------------------------------------------------------------------- the fold

@dataclass
class _Step:
    """One thing that happened to one key, in order: an execution or an action."""
    ts: str
    when: datetime
    kind: str                      # "exec" | "event"
    row: object = None
    outcome: str = ""              # "pass" | "fail" | ""  (empty = not evaluated)
    run_id: str = ""
    session: str = ""
    collapsed_into: str = ""
    inhibited_by: str = ""


def _run_order(executions: Sequence[H.Execution]) -> list[tuple[str, list[H.Execution]]]:
    """`[(run key, its executions)]`, runs in the order their first execution happened.

    An execution with no `run_id` is its own run — a single hand-typed `ctest` ingested from a
    JUnit file with no metadata is still one run, and folding them all together would invent a
    mass failure out of a month of separate ones.
    """
    runs: dict[str, list[H.Execution]] = {}
    for index, row in enumerate(executions):
        key = row.run_id or f"@{row.ts}#{index}"
        runs.setdefault(key, []).append(row)
    return sorted(runs.items(), key=lambda item: (str(item[1][0].ts), item[0]))


def _outcome(result: str) -> str:
    if result in NOT_EVALUATED:
        return ""
    if result in FAILING:
        return "fail"
    return "pass"


@dataclass
class _Container:
    """A `group:` or `run:` signal accumulated across runs."""
    key: str
    kind: str
    fingerprint: str = ""
    members: list[str] = field(default_factory=list)
    message: str = ""
    excerpt: str = ""

    def add(self, member: str) -> None:
        if member not in self.members:
            self.members.append(member)


def _collapse(rows: Sequence[H.Execution], containers: dict[str, _Container]) -> dict:
    """R7 for one run: what is itemised, what is collapsed, and into what.

    Returns `{"inhibited_by": key or "", "collapsed": {key: container key},
    "containers": [container keys touched], "passed": {container key: bool}}`.  The order of the
    three rules is the whole point — inhibition first, then the red run, then the fingerprint
    group — because each later rule would otherwise itemise what the earlier one explained.
    """
    builds = [r for r in rows if source_of(r.id) == "build" and _outcome(r.result) == "fail"]
    tests = [r for r in rows if source_of(r.id) != "build"]
    out = {"inhibited_by": builds[0].id if builds else "", "collapsed": {}, "touched": []}
    if builds:
        return out                                   # every test in this run is not evaluated
    evaluated = [r for r in tests if _outcome(r.result)]
    failed = [r for r in evaluated if _outcome(r.result) == "fail"]
    if not failed:
        return out
    runner = failed[0].runner or source_of(failed[0].id) or "tests"
    if (len(evaluated) >= RED_RUN_MIN_EXECUTED
            and len(failed) > len(evaluated) * RED_RUN_FRACTION):
        container = containers.setdefault(f"run:{runner}", _Container(f"run:{runner}", "run"))
        container.message = (f"{len(failed)} of {len(evaluated)} executed tests failed in one "
                             f"run: one cause, not {len(failed)}.")
        for row in failed:
            container.add(row.id)
            out["collapsed"][row.id] = container.key
        out["touched"].append(container.key)
        return out
    by_print: dict[str, list[H.Execution]] = {}
    for row in failed:
        by_print.setdefault(fingerprint(row.message or row.excerpt), []).append(row)
    left: list[H.Execution] = []
    for print_, group in sorted(by_print.items()):
        if print_ and len(group) >= GROUP_MIN_KEYS:
            key = group_key(print_)
            container = containers.setdefault(key, _Container(key, "group", fingerprint=print_))
            container.message = group[0].message or ""
            container.excerpt = group[0].excerpt or ""
            for row in group:
                container.add(row.id)
                out["collapsed"][row.id] = key
            if key not in out["touched"]:
                out["touched"].append(key)
        else:
            left.extend(group)
    if len(left) > MAX_SIGNALS_PER_RUN:
        overflow = sorted(left, key=lambda r: r.id)[MAX_SIGNALS_PER_RUN:]
        container = containers.setdefault(f"run:{runner}", _Container(f"run:{runner}", "run"))
        container.message = (f"more than {MAX_SIGNALS_PER_RUN} tests failed in one run; "
                             f"{len(overflow)} of them are listed here rather than itemised.")
        for row in overflow:
            container.add(row.id)
            out["collapsed"][row.id] = container.key
        if container.key not in out["touched"]:
            out["touched"].append(container.key)
    return out


def _flaky(rows: Sequence[H.Execution]) -> tuple[bool, float]:
    """`(flaky, score)` over the last `FLAKE_WINDOW` executions of one key.

    `test_history`'s own two rules, with one change the research asks for (R2): Datadog's
    "a pass and a fail at the same commit" is read over `(commit, tree_digest)`, because in this
    shared checkout one commit covers any number of working trees and a pass on a fixed tree
    beside a fail on a broken one is not a flake.
    """
    window = list(rows)[-FLAKE_WINDOW:]
    score = H.flake_score([r.result for r in window])
    seen: dict[tuple[str, str], set[str]] = {}
    for row in window:
        if not row.commit or row.result == "skip":
            continue
        seen.setdefault((row.commit, row.tree_digest), set()).add(
            "fail" if row.result in H.BAD_RESULTS else "pass")
    same_tree = any({"pass", "fail"} <= kinds for kinds in seen.values())
    return (same_tree or score >= H.FLAKE_FLAKY), score


def fold(executions: Iterable, events: Iterable = (), now=None, *,
         discovered: Iterable[str] | None = None) -> dict[str, Signal]:
    """Every signal, from the executions and the actions.  Pure: no clock, no disk, no board.

    `now` is the moment the answer is *for* (a string, a datetime, or None for the wall clock):
    staleness, a dismissal's expiry and retention all read it, so every one of them is testable
    by passing a different `now` rather than by waiting.  `discovered` is the set of keys the
    project still collects — a key that has left it is `removed`, which is not a fix (R3).
    """
    at = _at(now, datetime.now(timezone.utc))
    rows = H._as_executions(executions)
    rows.sort(key=lambda r: (str(r.ts), r.run_id, r.id))
    actions = [dict(e) for e in (events or []) if isinstance(e, dict)]
    actions.sort(key=lambda e: str(e.get("ts") or ""))
    run_session = {str(e.get("run_id") or ""): str(e.get("session") or "")
                   for e in actions if e.get("action") == "run" and e.get("run_id")}

    # ---- pass 1: the runs, and R7's one-cause-one-item decision for each of them
    containers: dict[str, _Container] = {}
    steps: dict[str, list[_Step]] = {}
    container_runs: list[tuple[str, dict, list[H.Execution]]] = []
    for run_key, run_rows in _run_order(rows):
        decision = _collapse(run_rows, containers)
        run_id = run_rows[0].run_id or run_key
        session = run_session.get(run_id, "")
        for row in run_rows:
            outcome = _outcome(row.result)
            inhibited = decision["inhibited_by"] if source_of(row.id) != "build" else ""
            steps.setdefault(row.id, []).append(_Step(
                ts=row.ts, when=_at(row.ts), kind="exec", row=row,
                outcome="" if inhibited else outcome, run_id=run_id, session=session,
                collapsed_into=decision["collapsed"].get(row.id, ""),
                inhibited_by=inhibited))
        container_runs.append((run_id, decision, list(run_rows)))
        # A container fails in the run that produced it, and passes in a later run where every
        # member it was asked about passed: that is how a group closes without a rule of its own.
        for key in decision["touched"]:
            first = next(r for r in run_rows if decision["collapsed"].get(r.id) == key)
            steps.setdefault(key, []).append(_Step(
                ts=first.ts, when=_at(first.ts), kind="exec",
                row=H.Execution(ts=first.ts, id=key, result="fail", runner=first.runner,
                                run_id=run_id, commit=first.commit,
                                tree_digest=first.tree_digest,
                                message=containers[key].message,
                                excerpt=containers[key].excerpt),
                outcome="fail", run_id=run_id, session=session))
    for run_id, decision, run_rows in container_runs:
        if decision["inhibited_by"]:
            continue
        seen = {r.id: _outcome(r.result) for r in run_rows}
        for key, container in containers.items():
            if key in decision["touched"] or not container.members:
                continue
            asked = [seen.get(m, "") for m in container.members if seen.get(m, "")]
            if asked and all(o == "pass" for o in asked):
                stamp = run_rows[-1].ts
                steps.setdefault(key, []).append(_Step(
                    ts=stamp, when=_at(stamp), kind="exec",
                    row=H.Execution(ts=stamp, id=key, result="pass", run_id=run_id,
                                    commit=run_rows[-1].commit),
                    outcome="pass", run_id=run_id))

    # ---- the actions, per key
    for action in actions:
        if action.get("action") == "run":
            continue
        key = str(action.get("key") or "")
        if not key:
            continue
        stamp = str(action.get("ts") or "")
        steps.setdefault(key, []).append(_Step(ts=stamp, when=_at(stamp), kind="event",
                                               row=action))

    # ---- pass 2: one timeline per key
    out: dict[str, Signal] = {}
    known = None if discovered is None else {str(k) for k in discovered}
    for key, timeline in steps.items():
        timeline.sort(key=lambda s: (s.ts, 0 if s.kind == "exec" else 1))
        signal = _walk(key, timeline, containers.get(key), at)
        if signal is not None:
            out[key] = signal
    for key, signal in list(out.items()):
        _finish(signal, out, known, at)
        if signal.state in ("resolved", "removed"):
            closed = _at(signal.resolved_at or signal.last_seen, at)
            if at - closed > timedelta(days=RETENTION_DAYS):
                del out[key]                       # R13: kept 30 days for R8, then dropped
    for signal in out.values():
        signal.promote = _promotion_reason(signal, at)
    return out


def _walk(key: str, timeline: Sequence[_Step], container: _Container | None,
          at: datetime) -> Signal | None:
    """The state machine for one key.  None when nothing is left to say about it."""
    signal = Signal(key=key, source=source_of(key),
                    kind=container.kind if container is not None else "broken")
    if container is not None:
        signal.members = list(container.members)
        signal.fingerprint = container.fingerprint
    fixed_kind = container is not None or signal.source == "build"
    if signal.source == "build":
        signal.kind = "build"
    alive = False                      # is there a signal at all right now?
    fail_streak = 0
    flaky_mark = False
    history: list[H.Execution] = []
    under = ""                         # the state a dismissal was laid over
    for step in timeline:
        if step.kind == "event":
            if alive:
                was = signal.state
                _apply_event(signal, step.row)
                if signal.state == "dismissed" and was != "dismissed":
                    under = was
            continue
        row = step.row
        if step.inhibited_by:
            signal.inhibited_by = step.inhibited_by
            continue
        if not step.outcome:
            continue                   # skip, timeout: not evaluated, advances nothing
        history.append(row)
        if not fixed_kind:
            flaky_now, score = _flaky(history)
            if flaky_now:
                flaky_mark = True
                signal.kind = "flaky"
        else:
            score = 0.0
        if step.outcome == "fail":
            signal.green_streak = 0
            if not alive:
                alive = True
                if signal.state == "resolved" and signal.resolved_at:
                    if _at(row.ts) - _at(signal.resolved_at) <= timedelta(days=REGRESS_DAYS):
                        signal.state = "open"       # R8: regressed, keeps its count
                        signal.regressed = True
                        signal.opened_run = step.run_id
                    else:
                        signal.previous = {"key": signal.key,
                                           "first_seen": signal.first_seen}
                        signal.count = 0
                        signal.runs = 0
                        signal.regressed = False
                        signal.fixed_in = ""
                        signal.first_seen = row.ts
                        signal.state = "pending"
                else:
                    signal.state = "pending"
                    if not signal.first_seen:
                        signal.first_seen = row.ts
                signal.resolved_at = ""
                fail_streak = 0
            elif signal.state == "removed":
                signal.state = "pending"
            fail_streak += 1
            signal.count += 1
            signal.last_seen = row.ts
            signal.runs += 1
            if not signal.first_run:
                signal.first_run = step.run_id
                signal.first_session = step.session
            if step.session and step.session not in signal.sessions:
                signal.sessions.append(step.session)
            if row.message or row.excerpt:
                signal.message = (row.message or "")[:500]
                signal.excerpt = (row.excerpt or row.message or "")[:MAX_EXCERPT]
                if not fixed_kind:
                    signal.fingerprint = fingerprint(row.message or row.excerpt)
            signal.group = step.collapsed_into
            if step.collapsed_into:
                continue               # R7: a collapsed key stays pending under its container
            need_fails = (OPEN_AFTER_FAILURES if signal.source in DEBOUNCED_SOURCES else 1)
            if signal.state == "pending" and (fail_streak >= need_fails
                                              or score >= H.FLAKE_FLAKY):
                signal.state = "open"
                signal.opened_run = step.run_id
            continue
        # a passing execution
        fail_streak = 0
        if not alive:
            continue
        signal.green_streak += 1
        if signal.state == "pending" and not flaky_mark:
            alive = False               # R3: a pending signal is deleted without trace
            signal.state = "pending"
            signal.count = 0
            signal.runs = 0
            signal.first_seen = ""
            signal.last_seen = ""
            signal.group = ""
            signal.message = signal.excerpt = ""
            continue
        need = RESOLVE_PASSES.get(signal.kind, RESOLVE_PASSES["broken"])
        if signal.state in ("open", "dismissed") and signal.green_streak >= need:
            signal.state = "resolved"
            signal.resolved_at = row.ts
            signal.fixed_in = row.commit
            signal.dismissed = None
            signal.session = ""
            alive = False
    if not alive and signal.state not in ("resolved", "removed"):
        return None
    if signal.state == "dismissed" and signal.dismissed:
        until = _ts(signal.dismissed.get("until"))
        if until is not None and until <= at:
            signal.state = under or "open"
            signal.dismissed = dict(signal.dismissed, expired=True)
    return signal


def _apply_event(signal: Signal, event: dict) -> None:
    """One action from the log, onto the signal it names."""
    action = str(event.get("action") or "")
    if action == "claim":
        signal.session = str(event.get("session") or "")
        signal.gave_up = ""
    elif action == "release":
        signal.session = ""
        reason = str(event.get("reason") or "")
        signal.gave_up = reason if reason == GAVE_UP else ""
    elif action == "dismiss":
        if signal.state in ("pending", "open", "dismissed"):
            signal.dismissed = {
                "reason": str(event.get("reason") or ""),
                "comment": str(event.get("comment") or "")[:MAX_COMMENT],
                "until": str(event.get("until") or ""),
                "by": str(event.get("by") or event.get("session") or ""),
            }
            signal.state = "dismissed"
    elif action == "promote":
        card = str(event.get("card") or "").strip().upper()
        if card:
            signal.card = card


def _finish(signal: Signal, out: dict[str, Signal], known: set[str] | None,
            at: datetime) -> None:
    """`removed` and `stale`: the two verdicts that need the world outside the timeline."""
    if (known is not None and signal.state in ("pending", "open")
            and signal.source in ("ctest", "unittest") and signal.key not in known):
        signal.state = "removed"
        signal.resolved_at = signal.resolved_at or _iso(at)
        return
    if signal.state == "open" and signal.last_seen:
        signal.stale = at - _at(signal.last_seen) > timedelta(days=STALE_DAYS)


def _promotion_reason(signal: Signal, at: datetime) -> str:
    """Why this signal is eligible for a bug card, or `""` (R9, decision 5).

    Never age alone: each of the three is impact or a decision a machine cannot make.
    """
    if signal.card or signal.state != "open":
        return ""
    if signal.gave_up:
        return GAVE_UP
    if signal.kind == "flaky":
        return "flaky"
    if signal.session:
        return ""                      # somebody is on it; give them time
    spread = _at(signal.last_seen, at) - _at(signal.first_seen, at)
    if signal.runs >= PROMOTE_MIN_RUNS and spread >= timedelta(hours=PROMOTE_MIN_HOURS):
        return "persistent"
    return ""


# --------------------------------------------------------------------------- reading it back

def state(project, board_root=None, *, now=None, discovered=None,
          history_path=None, events_path=None) -> dict[str, Signal]:
    """`fold()` over one board's two files.  The one place this module touches the disk."""
    history = Path(history_path) if history_path else H.default_path(project, board_root)
    events = Path(events_path) if events_path else default_path(project, board_root)
    return fold(H.read(history), read_events(events), now, discovered=discovered)


def sort_signals(signals: Iterable[Signal], *, session: str = "") -> list[Signal]:
    """R11's order: mine first, then regressed, then broken before flaky, then by count."""
    def rank(signal: Signal) -> tuple:
        return (0 if session and signal.first_session == session else 1,
                0 if signal.regressed else 1,
                {"broken": 0, "build": 0, "run": 1, "group": 1, "flaky": 2}.get(signal.kind, 3),
                -signal.count, signal.key)
    return sorted(signals, key=rank)


def summary(signals: Iterable[Signal], *, session: str = "") -> dict:
    """What `signals_changed` carries: the open rows, the dismissed ones, the counts, the cards.

    `open` is what a surface draws; `dismissed` is what its toggle reveals (R12: hidden behind a
    toggle **with their expiry shown**, which needs the rows and not just a number); `promoted` is
    every signal with a card that is still open, so a row can link to it.  `pending` signals are a
    count and nothing more, on purpose: a signal seen failing once is in-loop feedback for the pane
    that ran it, and a list of them on a human surface is the flood this design exists to avoid.
    """
    rows = list(signals)
    order = {"open": [], "dismissed": [], "promoted": []}
    for signal in sort_signals(rows, session=session):
        if signal.state == "open":
            order["open"].append(signal)
        elif signal.state == "dismissed":
            order["dismissed"].append(signal)
        if signal.card and signal.state in OPEN_STATES:
            order["promoted"].append(signal)
    return {
        "open": [s.to_dict() for s in order["open"]],
        "dismissed": [s.to_dict() for s in order["dismissed"]],
        "pending_count": sum(1 for s in rows if s.state == "pending"),
        "dismissed_count": len(order["dismissed"]),
        "promoted": [s.to_dict() for s in order["promoted"]],
    }


def promoted_open(signals: Iterable[Signal]) -> list[Signal]:
    """The promoted cards that are still open — what `MAX_PROMOTED_OPEN` caps."""
    return [s for s in signals if s.card and s.state in OPEN_STATES]


def blocking(signals: Iterable[Signal], *, card: str = "", session: str = "") -> tuple[list, list]:
    """Decision 8: `(blocks, open_before)` for one card.

    A signal blocks a card's verification only when it was **first seen in a run by the pane
    holding that card** — the run's pane token, from the log's `run` lines, against the card's
    `session` — or when it is the signal that card was promoted from.  Everything else open is
    listed as "open before this card" and refuses nothing: a session is answerable for what its
    own work broke, not for the state of the tree it found.
    """
    card = str(card or "").strip().upper()
    session = str(session or "").strip()
    blocks, before = [], []
    for signal in sort_signals(signals):
        if not signal.blocks_verification:
            continue
        mine = (card and signal.card == card) or (session and signal.first_session == session)
        (blocks if mine else before).append(signal)
    return blocks, before


def rerun_keys(failed: Sequence[str], executions: Iterable, *,
               max_failures: int = RERUN_MAX_FAILURES,
               max_seconds: float = RERUN_MAX_SECONDS) -> list[str]:
    """Decision 3: the failed keys worth re-running once, now, before the fold.

    Ten or fewer of them, and their recorded p50 durations summing to a minute — which is what
    makes "open on the second consecutive failure" take seconds rather than waiting for the next
    natural run.  Anything bigger stays `pending` and is answered by the next run, because a
    button that quietly re-runs six minutes of tests is the accident this refuses to have.
    """
    keys = [str(k) for k in dict.fromkeys(failed) if str(k).strip()]
    if not keys or len(keys) > max_failures:
        return []
    rows = H._as_executions(executions)
    by_key: dict[str, list[float]] = {}
    for row in rows:
        if row.id in set(keys) and row.result not in NOT_EVALUATED:
            by_key.setdefault(row.id, []).append(float(row.duration))
    total = sum(H.percentile(by_key.get(key, []), 50.0) for key in keys)
    return keys if total <= max_seconds else []


# --------------------------------------------------------------------------- the card's section

def check_dismissal(reason, comment, until, *, by_agent: bool, now=None) -> dict:
    """Validate a dismissal; raises `SignalError` with the sentence the caller shows.

    Decision 7: an agent reaches `environmental` and `flaky-known` only, with a comment and at
    most `AGENT_DISMISS_MAX_DAYS`; `wont-fix`, `expected` and a longer expiry are the owner's.
    **Every** dismissal has an expiry, whoever makes it — a signal that is hidden for ever is a
    deleted signal, and nothing here deletes.
    """
    at = _at(now, datetime.now(timezone.utc))
    reason = str(reason or "").strip().lower()
    allowed = AGENT_DISMISS_REASONS if by_agent else DISMISS_REASONS
    if reason not in allowed:
        raise SignalError(
            f"A dismissal's reason is one of {', '.join(allowed)}"
            + (f" — {', '.join(r for r in DISMISS_REASONS if r not in allowed)} "
               "is the owner's call, so say why in a comment and leave it open." if by_agent
               else ".") + ("" if by_agent else ""),
            code="signal_reason")
    text = str(comment or "").strip()
    if not text:
        raise SignalError("A dismissal needs a comment: one line on what you checked and why "
                          "this failure is not the code's fault.", code="signal_comment")
    stamp = _ts(until)
    if stamp is None:
        raise SignalError(
            "A dismissal needs `until`: the date it expires, as YYYY-MM-DD or an ISO timestamp. "
            "Every dismissal expires — a signal hidden for ever is a deleted one.",
            code="signal_until")
    if stamp <= at:
        raise SignalError("A dismissal's `until` is in the future: it says when the signal comes "
                          "back, not when it went away.", code="signal_until")
    if by_agent and stamp - at > timedelta(days=AGENT_DISMISS_MAX_DAYS):
        raise SignalError(
            f"An agent's dismissal lasts at most {AGENT_DISMISS_MAX_DAYS} days. Dismiss it for a "
            "week and say in the comment what a longer one would need; a longer expiry is the "
            "owner's.", code="signal_until")
    return {"reason": reason, "comment": text[:MAX_COMMENT], "until": _iso(stamp)}


def signal_section(signal: Signal) -> str:
    """The body of the machine-owned `## Signal` section on a promoted card (R9).

    Rewritten in place whenever the signal's state changes, never appended to and never a
    comment: it is the machine's one paragraph on a card whose every other byte is a person's.
    It says what the signal is, what closes it, and that closing the card does not.
    """
    need = RESOLVE_PASSES.get(signal.kind, RESOLVE_PASSES["broken"])
    lines = [
        "<!-- Written by Relay (relay_core.signals.signal_section). Rewritten in place whenever",
        "     the signal changes; edit around it, not inside it. -->",
        f"- **key** `{signal.key}` · {signal.kind} · **{signal.state}**"
        + (" · regressed" if signal.regressed else "")
        + (" · stale" if signal.stale else ""),
        f"- failing executions: {signal.count} in {signal.runs} run(s); "
        f"first seen {signal.first_seen or '—'}, last {signal.last_seen or '—'}",
    ]
    if signal.fingerprint:
        lines.append(f"- fingerprint: `{signal.fingerprint}`")
    if signal.session:
        lines.append(f"- claimed by session `{signal.session[:8]}`")
    if signal.dismissed:
        lines.append(f"- dismissed as {signal.dismissed.get('reason')} until "
                     f"{signal.dismissed.get('until')}: {signal.dismissed.get('comment')}")
    if signal.members:
        lines.append("- members: " + ", ".join(f"`{m}`" for m in signal.members[:20])
                     + ("…" if len(signal.members) > 20 else ""))
    if signal.fixed_in:
        lines.append(f"- resolved at {signal.resolved_at} in `{signal.fixed_in[:12]}`")
    lines.append(f"- it resolves on {need} consecutive passing executions of that key, and on "
                 "nothing else: closing this card does not close the signal, and this card "
                 "cannot leave needs-verification while the signal is open.")
    if signal.excerpt:
        body = signal.excerpt.strip()[:1200]
        lines += ["", "```", body, "```"]
    return "\n".join(lines) + "\n"


def card_request(signal: Signal) -> str:
    """The promoted card's `## Issue`: the failure excerpt **verbatim**, and where it came from."""
    head = (f"`{signal.key}` has failed {signal.count} time(s) in {signal.runs} run(s) since "
            f"{signal.first_seen or 'it was first seen'}.")
    body = (signal.excerpt or signal.message or "").rstrip()
    if not body:
        return head
    return f"{head}\n\n```\n{body[:MAX_EXCERPT]}\n```"


def rewrite_section(board, signal: Signal) -> bool:
    """Put `signal_section(signal)` on the signal's card, in place.  True when it wrote.

    The `## Signal` section is the machine's, so it is *replaced* rather than appended to: a
    signal that opens, is claimed, regresses and resolves leaves one current paragraph, not five
    historical ones (R9, and the same rule as `tests_protocol`'s `### Check` block).  Every other
    byte of the card is a person's and is not touched, and a card that has gone is not an error —
    somebody deleted it, and the signal is still a signal.
    """
    if not signal.card:
        return False
    card = board.card_by_id(signal.card)
    if card is None:
        return False
    wanted = signal_section(signal)
    span = B.section_span(card.body, SIGNAL_HEADING)
    if span is not None:
        if card.body[span[0]:span[1]].strip("\n") == wanted.strip("\n"):
            return False
        body = card.body[:span[0]] + wanted + "\n" + card.body[span[1]:]
    else:
        body = B.append_body_section(card.body, SIGNAL_HEADING, wanted)
    card.body = body
    try:
        board.save(card)
    except (B.BoardError, OSError):                        # pragma: no cover - unwritable board
        return False
    return True


def card_title(signal: Signal) -> str:
    """One line for the bug card: the key, and what kind of failure it is."""
    what = {"flaky": "is flaky", "group": "and the tests that fail with it",
            "run": "failed as a whole run", "build": "does not build"}.get(
                signal.kind, "fails")
    return f"{signal.key} {what}"[:200]
