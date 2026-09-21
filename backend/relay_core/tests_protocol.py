# SPDX-License-Identifier: AGPL-3.0-or-later
"""Worker protocol handlers for the Test suites pane and the card's Check
(docs/AGENT-SESSIONS-PROTOCOL.md section 31, card #7BM4 phase 3).

This is the seam between the three offline halves that landed in phase 1 — `test_probe`
(what tests exist), `test_history` (what happened to them) and `junit_runner` (per-test
results out of the stdlib suite) — and the GUI model that landed in phase 2
(`src/TestSuitesModel.h`).  It owns no data of its own: discovery is a subprocess, history
is one append-only JSONL file, and everything the pane shows is folded out of the two on
demand.

Five requests, answered with the four events of the wire contract:

    tests_list                              -> tests_list     the inventory and the header line
    tests_run {ids, repeat_until_fail}      -> tests_run       started / progress / finished
    tests_stop {run_id}                     -> tests_run       stopped
    tests_history {id, limit}               -> tests_history   one test's executions
    tests_check {card}                      -> tests_check     one card's staleness findings
    tests_suggest {card}                    -> tests_suggest    tests named after what it changed

`tests_check` also leaves a dated `### Check` block under the card's `## Tests`, and
`gate_move()` is the rule that stops a card leaving `needs-verification` while the tests it
names are gone, never run or failing (#7BM4 phase 4).

Three rules this module keeps, all of them learned from the research in
`docs/SWITCHBOARD-TOOLING-RESEARCH.md` and from the ways a test runner behind a button goes
wrong:

* **Never a full suite implicitly.**  A run names its tests.  There is no "run everything"
  path here at all: `scripts/test.sh` and `ctest` are still how the whole suite is run, and a
  button that quietly starts 4,000 tests because a filter was empty is the accident this
  refuses to have.
* **One run at a time, and it can be stopped.**  Every command goes through `jobs.JobTable`,
  which is a process group with a stop — the same table the agent's own `run_command` uses —
  so `tests_stop` really ends the compile or the test binary, not just the wait.
* **The environment is `scripts/test.sh`'s.**  A test run from a pane must touch nothing of
  the owner's: a temporary `XDG_DATA_HOME` (and every other XDG dir, and `TMPDIR`, under one
  short `/tmp` path because of the 108-byte socket limit), `RELAY_KEYRING=off` so no run can
  reach the desktop keyring, `RELAY_LOCAL_MODELS` pointed at a throwaway file, and
  `QT_QPA_PLATFORM=offscreen` so a widget test opens no window over the user's work.

Results are ingested into the store exactly once, keyed on the run: a local run by its own
`run_id`, a folder fetched from another machine by `scripts/relay-remote-tests` by the
`run_id` in its `meta.json` **and** by an `ingested` marker written beside it.  Either guard
alone would do; both are here because the folders that existed before the marker did must not
be counted twice.
"""
from __future__ import annotations

import datetime
import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Callable, Sequence

from . import board as B
from . import jobs as J
from . import test_history as H
from . import test_probe as P

#: The signal requests (protocol 32, card #AQ6X).  They live here rather than in a module of
#: their own because the fold they read is driven from this one: every execution a signal is made
#: of passes through `ingest_incoming` and `_execute`, and nothing else ever writes one.
SIGNALS_TYPES = frozenset({"signals_list", "signals_claim", "signals_release",
                           "signals_dismiss", "signals_promote", "signals_config"})

#: The requests this class answers.  `board_protocol.TYPES` includes them and delegates here.
TYPES = frozenset({"tests_list", "tests_run", "tests_stop", "tests_history", "tests_check",
                   "tests_suggest"}) | SIGNALS_TYPES

#: The card section that lists what proves a card, and the statuses at which not having one is
#: worth reporting: a card being worked, or waiting for a verifier, with no tests named is the
#: verification backlog the pane's board-level report exists to show.
TESTS_HEADING = "Tests"
UNTESTED_STATUSES = ("executing", "in-progress", "needs-verification",
                     "needs-qa-llm", "needs-qa-human")

#: The dated block Check leaves under `## Tests` (#7BM4 phase 4).  A durable artefact rather
#: than a toast — the Trunk shape — so the card itself says when it was last checked and what
#: was wrong.  `### Check YYYY-MM-DD HH:MM`, local time, to the minute.
CHECK_HEADING = "Check"
CHECK_BLOCK_RE = re.compile(
    r"^###[ \t]+Check[ \t]+(?P<date>\d{4}-\d{2}-\d{2})[ \t]+(?P<time>\d{2}:\d{2})[ \t]*$", re.M)
#: At most one block per card per hour: a Check pressed twice in a minute is one answer, not two
#: entries in the card's history.  Inside the hour the findings are still sent to the GUI.
CHECK_MIN_GAP_SECONDS = 3600.0

#: The gate (#7BM4, owner 2026-09-20: Check "is a gate on leaving `needs-verification`, with a
#: recorded override").  Moving *out of* `needs-verification` towards one of these is a landing:
#: from here on a verifier, or nobody, reads the card again.
GATE_FROM_STATUS = "needs-verification"
GATE_TO_STATUSES = ("needs-qa", "needs-qa-llm", "needs-qa-human", "needs-review", "done",
                    "verified")
#: The verdicts that stop a landing.  `skipped-forever`, `edited`, `flaky` and `slow` are worth
#: reading and are not worth blocking on: they say a test is weak, not that the card is unproven.
GATE_VERDICTS = ("gone", "never-run")

#: Ceilings.  A run is the owner's or the agent's deliberate act, so these are about what can
#: be typed by mistake, not about what is allowed to take time.
MAX_IDS = 200                   # ids in one `tests_run` without `all: true`
MAX_AGENT_IDS = 50              # ids in one agent-facing `tests_run` tool call
MAX_REPEAT = 100                # `--repeat until-fail:N`
RUN_TIMEOUT = 3600.0            # seconds one run may take before it is stopped
CTEST_TEST_TIMEOUT = 900        # `ctest --timeout`, per test
MAX_HISTORY_LIMIT = 500         # executions one `tests_history` may carry
MAX_COMMITS = 20                # commits of a card read for their changed files
MAX_CHANGED_FILES = 400
MAX_SUGGESTED = 20           # lines one `tests_suggest` appends to a card
#: Decision 3's re-run is a **separate** run in the store, so a fail-fail really is two
#: consecutive failing executions and a fail-pass really is one tree disagreeing with itself.
RERUN_SUFFIX = "-rerun"
GIT_TIMEOUT = 20.0

#: The runners a run can actually start.  `manual` is evidence a person records, so a row for
#: one is skipped rather than refused — see `_partition`.
RUNNABLE = (P.RUNNER_CTEST, P.RUNNER_UNITTEST)


class TestsError(ValueError):
    """A refusal the caller should read: one sentence, no traceback."""


# --------------------------------------------------------------------------- ctest's own output

#: `1/2 Test #1: board .......................   Passed    0.90 sec`
_CTEST_DONE = re.compile(
    r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(?P<name>\S+)\s+\.*\s*"
    r"(?P<verdict>Passed|\*\*\*Failed|\*\*\*Timeout|\*\*\*Skipped|\*\*\*Not Run|"
    r"\*\*\*Exception[^ ]*)\s+(?P<seconds>[0-9.]+)\s+sec")
#: `    Start 1: board`
_CTEST_START = re.compile(r"^\s*Start\s+\d+:\s+(?P<name>\S+)\s*$")

_CTEST_VERDICT = {"Passed": "pass", "***Failed": "fail", "***Timeout": "timeout",
                  "***Skipped": "skip", "***Not Run": "skip"}


def parse_ctest_line(line: str) -> dict | None:
    """One line of `ctest`'s live output as a progress event, or None.

    Two shapes are recognised, and only these two: the `Start n: <name>` that says a test has
    begun (`{"id"}` alone, which is what puts the row in Running), and the
    `n/m Test #n: <name> … Passed 0.90 sec` that says how it went (`{"id", "result",
    "duration"}`).  The JUnit file written at the end is the authority for what is stored;
    this only makes the pane move while the run is going.
    """
    done = _CTEST_DONE.match(line)
    if done:
        verdict = done.group("verdict")
        result = _CTEST_VERDICT.get(verdict, "error")
        try:
            duration = float(done.group("seconds"))
        except ValueError:                                   # pragma: no cover - regex-guarded
            duration = 0.0
        return {"id": f"{P.RUNNER_CTEST}:{done.group('name')}", "result": result,
                "duration": duration}
    start = _CTEST_START.match(line)
    if start:
        return {"id": f"{P.RUNNER_CTEST}:{start.group('name')}"}
    return None


# --------------------------------------------------------------------------- one run

class _Run:
    """The run in flight: what was asked for, what has come back, and how to stop it."""

    def __init__(self, run_id: str, ids: Sequence[str], skipped: Sequence[str]):
        self.run_id = run_id
        self.ids = list(ids)
        self.skipped = list(skipped)
        self.total = len(self.ids)
        self.done = 0
        self.state = "started"
        self.message = ""
        self.results: dict[str, dict] = {}
        self.reported: set[str] = set()
        self.cancel = threading.Event()
        self.finished = threading.Event()
        self.jobs: list = []
        self.stopped = False
        #: The keys the re-run of decision 3 took, and the signal keys the fold that followed
        #: this run put on the board — what `tests_run`'s result carries as `opened`, so the pane
        #: that ran the tests learns in the same turn what its run broke.
        self.rerun: list[str] = []
        self.opened: list[str] = []


# --------------------------------------------------------------------------- the handlers

class TestsCommands:
    """The `tests_*` protocol messages for one worker (or for one agent's tools).

    Built the way `board_protocol.BoardCommands` is: it is handed its collaborators rather than
    finding them — the project root, the board root, the `emit` that puts an event on the wire,
    and a `JobTable` (or a factory for one, so a worker can keep the tests' jobs out of the
    agent's own table).  Nothing here reads a settings file, and the only paths it knows are the
    ones it was given.
    """

    def __init__(self, project, board_root=None, emit: Callable[[dict], None] | None = None,
                 jobs=None, *, build_dir=None, python: str | None = None,
                 clock: Callable[[], float] = time.time, pane_token: str | None = None):
        self.project = Path(os.path.abspath(Path(project).expanduser()))
        root = Path(board_root) if board_root is not None else B.board_folder(self.project)
        self.board_root = Path(root) if root is not None else None
        self.emit = emit or (lambda event: None)
        self._jobs = jobs
        self._build_dir = Path(build_dir) if build_dir else None
        self.python = python or sys.executable or "python3"
        self.clock = clock
        self._lock = threading.Lock()
        self._run: _Run | None = None
        #: This pane's session token (19.19), when there is one.  A signal claim writes it
        #: exactly as a card claim does, and the `run` line every run leaves in the signal log
        #: carries it — which is how the verification gate of decision 8 knows *whose* run first
        #: failed a key.  The Switchboard's own worker and a test have none.
        self.pane_token = pane_token or None
        self._signals_sent: dict | None = None
        self._tools = None
        #: How a signal thread is started (#AQ6X step 7b).  Set by whoever built this instance and
        #: has a `SubagentManager` — `board_protocol._tests()` in the worker, a fake in a test, and
        #: nothing at all for the agent's own tool instance, which must not start background work.
        #: `(task, description) -> (thread_id, agent_id, session_id, done_event) | None`.
        self.spawn_agent: Callable[[str, str], tuple | None] | None = None
        self._threads = None

    # ---- collaborators ---------------------------------------------------------
    @property
    def jobs(self) -> J.JobTable:
        """The job table, made on first use when a factory (or nothing) was passed."""
        table = self._jobs
        if table is None or callable(table):
            table = table() if callable(table) else J.JobTable()
            self._jobs = table
        return table

    def board(self) -> B.Board | None:
        """This project's board, or None when it has no Switchboard at all."""
        if self.board_root is None:
            root = B.board_folder(self.project)
            if root is None:
                return None
            self.board_root = Path(root)
        return B.Board(self.board_root, self.project)

    def build_dir(self, asked=None) -> Path:
        """Where ctest is run: the request's `build_dir`, this instance's, or `<project>/build`."""
        if asked:
            path = Path(str(asked)).expanduser()
            return path if path.is_absolute() else self.project / path
        if self._build_dir is not None:
            return self._build_dir
        return self.project / P.DEFAULT_BUILD_DIR

    def store_path(self) -> Path:
        return H.default_path(self.project, self.board_root)

    def incoming_dir(self) -> Path:
        return self.store_path().parent / "incoming"

    # ---- dispatch --------------------------------------------------------------
    @staticmethod
    def handles(kind) -> bool:
        return kind in TYPES

    def dispatch(self, request: dict) -> bool:
        """Answer one request.  False when it is not ours; a `TestsError` is a refusal event."""
        kind = request.get("type")
        if kind not in TYPES:
            return False
        # `id` is the **test** on a tests_history request and on every tests_run event (the
        # contract, and `src/TestSuitesModel.cpp` which reads it that way), so only the two
        # requests that leave the key free carry a request id.
        if kind == "tests_list":
            self.emit_list(request.get("id"), build_dir=request.get("build_dir"))
        elif kind == "tests_run":
            try:
                self.start_run(request.get("ids"),
                               repeat_until_fail=request.get("repeat_until_fail") or 0,
                               build_dir=request.get("build_dir"),
                               allow_all=bool(request.get("all")))
            except TestsError as exc:
                self._emit_run({"state": "error", "message": str(exc), "done": 0, "total": 0},
                               run_id="")
        elif kind == "tests_stop":
            try:
                self.stop_run(request.get("run_id"))
            except TestsError as exc:
                self._emit_run({"state": "error", "message": str(exc), "done": 0, "total": 0},
                               run_id=str(request.get("run_id") or ""))
        elif kind == "tests_history":
            self.emit_history(request.get("id") or request.get("test"), request)
        elif kind == "tests_check":
            self.emit_check(request.get("card"), request.get("id"))
        elif kind == "tests_suggest":
            self.emit_suggest(request.get("card"), request.get("id"),
                              apply=request.get("apply", True))
        elif kind in SIGNALS_TYPES:
            self.dispatch_signal(kind, request)
        return True

    def shutdown(self) -> None:
        """Stop whatever is running; the worker is going."""
        threads = self._threads
        if threads is not None:
            threads.stop_all()      # each signal thread says `stopped` once, and frees its key
        run = self._run
        if run is not None and not run.finished.is_set():
            run.cancel.set()
            for job in list(run.jobs):
                try:
                    self.jobs.stop(job)
                except Exception:                            # pragma: no cover - already gone
                    pass

    # ---- tests_list ------------------------------------------------------------
    def inventory(self, *, build_dir=None) -> dict:
        """The whole `tests_list` payload, without emitting it.

        Order matters: any results another machine left in `incoming/` are folded into the store
        **first**, so a pane that opens right after `scripts/relay-remote-tests` shows that run
        rather than showing it one refresh later.
        """
        self.ingest_incoming()
        discovered = P.discover(self.project, build_dir=self.build_dir(build_dir))
        executions = H.read(self.store_path())
        index, without = self.card_index()
        records = H.records(discovered, executions, index)
        # The fold runs after every ingest (#AQ6X step 4), and this is the only place that knows
        # what the project still *collects* — so it is the only place `removed` can be decided.
        try:
            self.fold_signals(discovered=discovered, executions=executions)
        except Exception:                                    # pragma: no cover - defensive
            pass
        return {"project": str(self.project), "tests": records,
                "summary": H.summary(records), "cards_without_tests": without}

    def emit_list(self, rid=None, *, build_dir=None) -> dict:
        payload = self.inventory(build_dir=build_dir)
        self.emit({"event": "tests_list", **_rid(rid), **payload})
        return payload

    def card_index(self) -> tuple[dict[str, list[str]], list[dict]]:
        """`({card id: its ## Tests lines}, [cards in flight with no ## Tests section])`.

        One pass over the board's cards, reading each one's `## Tests` section with the board's
        own section reader and `test_history.parse_test_line`.  A line that names no invocation
        (a sentence, a blank) is dropped here rather than travelling as a test nobody can run.
        """
        board = self.board()
        index: dict[str, list[str]] = {}
        without: list[dict] = []
        if board is None or not board.config_path.is_file():
            return index, without
        try:
            cards = board.cards()
        except (B.BoardError, OSError):                      # pragma: no cover - unreadable board
            return index, without
        for card in cards:
            card_id = card.id
            if not card_id or card.type != "work":
                continue
            lines = [line for line in section_lines(card.body,
                                                    B.section_span(card.body, TESTS_HEADING))
                     if H.parse_test_line(line)]
            if lines:
                index[card_id] = lines
            elif card.status in UNTESTED_STATUSES:
                without.append({"id": card_id, "title": card.title or card_id,
                                "status": card.status})
        without.sort(key=lambda row: (UNTESTED_STATUSES.index(row["status"])
                                      if row["status"] in UNTESTED_STATUSES else 99, row["id"]))
        return index, without

    # ---- results another machine produced --------------------------------------
    def ingest_incoming(self) -> dict:
        """Fold `<board>/.private/tests/incoming/*/` into the store, each folder exactly once.

        `scripts/relay-remote-tests` leaves one folder per run — `meta.json`, `ctest.xml`,
        `unittest.xml` and the logs — and this is the only thing that reads them.  A folder is
        skipped when it carries an `ingested` marker, **or** when the store already holds its
        `run_id`: the marker is the fast path, the store check is what keeps the folders that
        were fetched before markers existed from being counted a second time.
        """
        incoming = self.incoming_dir()
        out = {"folders": [], "executions": 0}
        try:
            entries = sorted(p for p in incoming.iterdir() if p.is_dir())
        except OSError:
            return out
        known: set[str] | None = None
        for folder in entries:
            marker = folder / "ingested"
            if marker.exists():
                continue
            meta = self._meta(folder)
            run_id = str(meta.get("run_id") or folder.name)
            if known is None:
                known = {row.run_id for row in H.read(self.store_path()) if row.run_id}
            if run_id in known:
                self._mark_ingested(marker, run_id, 0, "already in the store")
                continue
            commit = str(meta.get("commit") or "")
            host = str(meta.get("host") or folder.name.split("-", 1)[0])
            stamp = str(meta.get("finished") or "") or H.now_iso()
            rows: list[H.Execution] = []
            for name, runner in (("ctest.xml", P.RUNNER_CTEST),
                                 ("unittest.xml", P.RUNNER_UNITTEST)):
                path = folder / name
                if path.is_file():
                    rows.extend(H.ingest_junit(path, runner=runner, commit=commit,
                                               run_id=run_id, host=host, ts=stamp))
            if rows:
                H.append(rows, self.store_path())
                known.add(run_id)
            self._mark_ingested(marker, run_id, len(rows), "")
            out["folders"].append({"folder": folder.name, "run_id": run_id,
                                   "executions": len(rows)})
            out["executions"] += len(rows)
        return out

    @staticmethod
    def _meta(folder: Path) -> dict:
        try:
            data = json.loads((folder / "meta.json").read_text(encoding="utf-8", errors="replace"))
        except (OSError, ValueError):
            return {}
        return data if isinstance(data, dict) else {}

    @staticmethod
    def _mark_ingested(marker: Path, run_id: str, count: int, note: str) -> None:
        try:
            marker.write_text(json.dumps({"at": H.now_iso(), "run_id": run_id,
                                          "executions": count, "note": note}) + "\n",
                              encoding="utf-8")
        except OSError:                                      # pragma: no cover - read-only board
            pass

    # ---- tests_history ---------------------------------------------------------
    def emit_history(self, test_id, request: dict | None = None) -> list[dict]:
        if not isinstance(test_id, str) or not test_id.strip():
            raise ValueError("tests_history needs `id`: the test's `<runner>:<invocation>` key.")
        limit = (request or {}).get("limit")
        try:
            count = int(limit) if limit is not None else H.RETENTION_PER_TEST
        except (TypeError, ValueError):
            raise ValueError("tests_history limit must be a number.") from None
        count = max(1, min(MAX_HISTORY_LIMIT, count))
        rows = H.read(self.store_path(), ids=[test_id.strip()], limit=count)
        executions = [row.to_dict() for row in reversed(rows)]     # newest first
        self.emit({"event": "tests_history", "id": test_id.strip(), "executions": executions})
        return executions

    # ---- tests_check -----------------------------------------------------------
    def check_card(self, card_id, *, build_dir=None) -> dict:
        """One card's `## Tests` section against discovery, history and its own commits.

        A card with **no** `## Tests` section is not run through `check_card`: it would answer
        with the generic orphaned line, and the honest answer is the specific one — the section
        is missing, and that is the single thing to fix.
        """
        ident = str(card_id or "").strip().lstrip("#").upper()
        if not ident:
            raise ValueError("tests_check needs `card`: the card id, e.g. 7BM4.")
        board = self.board()
        card = board.card_by_id(ident) if board is not None else None
        if card is None:
            raise ValueError(f"No card #{ident} on this board.")
        span = B.section_span(card.body, TESTS_HEADING)
        if span is None:
            return {"card": ident, "findings": [{
                "test": "", "verdict": "no-tests",
                "message": (f"#{ident} has no `## Tests` section, so nothing says which tests "
                            f"prove it: add one line per test, e.g. `ctest -R board` or "
                            f"`tests/test_board.py::CardTests::test_roundtrip`."),
                "severity": "warning"}],
                "actions": ["Add the tests this card's commits touched"],
                "ids": [], "files": {}, "failing": [],
                **self.signal_block(ident, card)}
        lines = section_lines(card.body, span)
        discovered = P.discover(self.project, build_dir=self.build_dir(build_dir))
        executions = H.read(self.store_path())
        index, _ = self.card_index()
        records = H.records(discovered, executions, index)
        result = H.check_card(lines, self.card_files(ident, card), records)
        extra = resolved_tests(lines, records)
        # A test whose last stored result was not a pass is a finding here, not only an action.
        # `test_history.check_card` answers "is this list stale?", and a failing test is not
        # stale — but the landing gate refuses on exactly that, so a Check that stayed silent
        # about it would call a card clean and then be contradicted by the refusal. One finding
        # per failing test, added where the two halves meet, so neither side has to guess.
        result["findings"] = list(result.get("findings") or []) + _failing_findings(
                extra["failing"], {f.get("test") for f in result.get("findings") or []}, records)
        return {"card": ident, **result, **extra,
                **self.signal_block(ident, card, discovered=discovered, executions=executions)}

    def signal_block(self, card_id: str, card, *, discovered=None, executions=None) -> dict:
        """`{"blocks": […], "open_before": […]}` for one card (decision 8, #AQ6X step 5).

        `blocks` are the signals this card is answerable for — the one it was promoted from, and
        the ones first seen in a run by the pane that holds it — and they are what refuses the
        move out of `needs-verification` (`board_tools._signal_gate`).  `open_before` is every
        other open signal, listed so the person reading Check knows the tree was already red and
        that it is not this card's doing.  A board with no signal store answers with two empty
        lists, which reads as "nothing known", not as "nothing wrong".
        """
        try:
            from . import signals as S
            signals = self.signal_state(discovered=discovered, executions=executions)
            blocks, before = S.blocking(signals.values(), card=card_id,
                                        session=str((card.front.get("session") if card is not None
                                                     else "") or ""))
        except Exception:                                    # pragma: no cover - unreadable store
            return {"blocks": [], "open_before": []}
        return {"blocks": [s.to_dict() for s in blocks],
                "open_before": [s.to_dict() for s in before]}

    def emit_check(self, card_id, rid=None) -> dict:
        """Answer one Check, and leave the dated block on the card that says it happened.

        The **worker** writes the block, not the GUI: an agent's `tests_check`, the card's
        button and a check run from another window all leave the same artefact, and the card
        file stays the record even when no window is open.  A card with no `## Tests` section
        gets no block at all — there is nowhere to put it, and the one finding already says the
        section is missing.
        """
        result = self.check_card(card_id)
        written = self.write_check_block(result)
        self.emit({"event": "tests_check", **_rid(rid), **result,
                   **({"block": written} if written else {})})
        return result

    # ---- the dated `### Check` block -------------------------------------------
    def write_check_block(self, result: dict) -> str:
        """Append (or replace) `### Check <date>` under the card's `## Tests`.  "" when it did not.

        Two rules, both about not turning a card into a log: **at most one block an hour** — a
        button pressed twice in a minute is one answer — and, when an hour has passed but the
        newest block is from **today**, that block is *replaced* rather than stacked on, so a day
        of checking leaves one current line instead of twelve historical ones.
        """
        ident = str(result.get("card") or "")
        if not ident or any(f.get("verdict") == "no-tests" for f in result.get("findings") or []):
            return ""                       # no section: nothing to write the block under
        board = self.board()
        card = board.card_by_id(ident) if board is not None else None
        if card is None:
            return ""
        span = B.section_span(card.body, TESTS_HEADING)
        if span is None:
            return ""
        section = card.body[span[0]:span[1]]
        now = datetime.datetime.now()
        newest = _newest_check(section)
        if newest is not None and (now - newest[1]).total_seconds() < CHECK_MIN_GAP_SECONDS:
            return ""                       # inside the hour: the GUI still has the findings
        stamp = now.strftime("%Y-%m-%d %H:%M")
        block = f"### {CHECK_HEADING} {stamp}\n" + _check_lines(result)
        if newest is not None and newest[1].date() == now.date():
            start, end = newest[0]
            body = card.body[:span[0] + start] + block + card.body[span[0] + end:]
        else:
            body = B.append_body_section(card.body, TESTS_HEADING, block)
        card.body = body
        try:
            board.save(card)
            board.append_thread(ident, f"Check · {_check_sentence(result)}",
                                author="agent", kind="evidence")
        except (B.BoardError, OSError):                   # pragma: no cover - defensive
            return ""
        return stamp

    # ---- tests_suggest ---------------------------------------------------------
    def suggest_tests(self, card_id, *, build_dir=None) -> dict:
        """The tests this card's commits touched, by the convention `check_card` calls "orphaned".

        There is no coverage data here, so the honest mapping is the naming one `test_history`
        already uses for the orphaned verdict (`_covers`: same directory, or matching normalised
        stems).  A test the section already lists is not suggested again, and nothing is invented:
        every line comes from a test discovery actually found.
        """
        ident = str(card_id or "").strip().lstrip("#").upper()
        if not ident:
            raise ValueError("tests_suggest needs `card`: the card id, e.g. 7BM4.")
        board = self.board()
        card = board.card_by_id(ident) if board is not None else None
        if card is None:
            raise ValueError(f"No card #{ident} on this board.")
        changed = self.card_files(ident, card)
        span = B.section_span(card.body, TESTS_HEADING)
        listed = section_lines(card.body, span) if span is not None else []
        known = set()
        for line in listed:
            parsed = H.parse_test_line(line)
            if parsed:
                known.add(parsed["id"])
        discovered = P.discover(self.project, build_dir=self.build_dir(build_dir))
        lines, ids = [], []
        for test in H._discovered_dicts(discovered):
            test_id = str(test.get("id") or "")
            source = str(test.get("file") or "")
            if not test_id or test_id in known or test_id in ids or not source:
                continue
            if not any(H._covers(source, name) for name in changed):
                continue
            invocation = str(test.get("invocation") or test.get("name") or "")
            lines.append(f"- `{invocation}` — {source}" if invocation else f"- {source}")
            ids.append(test_id)
            if len(ids) >= MAX_SUGGESTED:
                break
        return {"card": ident, "changed": changed, "lines": lines, "ids": ids}

    def emit_suggest(self, card_id, rid=None, *, apply: bool = True) -> dict:
        """`tests_suggest {card}` — suggest, and (by default) append the lines to `## Tests`.

        The write goes down the board's own path with a thread entry, exactly as the button's
        other two actions go down the worker's: the GUI never writes a card file.
        """
        result = self.suggest_tests(card_id)
        ident = result["card"]
        added = False
        if apply and result["lines"]:
            board = self.board()
            card = board.card_by_id(ident)
            if card is not None:
                card.body = B.append_body_section(card.body, TESTS_HEADING,
                                                  "\n".join(result["lines"]))
                board.save(card)
                board.append_thread(
                    ident,
                    "Added the tests this card's commits touched to `## Tests`:\n\n"
                    + "\n".join(result["lines"]),
                    author="agent", kind="evidence")
                added = True
        result["added"] = added
        result["message"] = _suggest_sentence(result)
        self.emit({"event": "tests_suggest", **_rid(rid), **result})
        return result

    # ---- signals (protocol 32, #AQ6X) ------------------------------------------
    def signals_path(self) -> Path:
        from . import signals as S
        return S.default_path(self.project, self.board_root)

    def signal_state(self, *, discovered=None, executions=None, now=None) -> dict:
        """The fold, over this board's two private files.  Runs nothing and writes nothing.

        `discovered` is taken in any of the three shapes `test_probe` and `test_history` pass
        around — the whole `discover()` dict, its `tests` list, or plain ids — because a caller
        that has just discovered is exactly the caller that has the dict, and a `removed` verdict
        computed from the dict's *keys* would mark every signal removed.
        """
        from . import signals as S
        rows = H.read(self.store_path()) if executions is None else executions
        known = None
        if discovered is not None:
            if all(isinstance(item, str) for item in discovered) and not isinstance(
                    discovered, dict):
                known = [str(item) for item in discovered]
            else:
                known = [str(row.get("id") or "") for row in H._discovered_dicts(discovered)]
        return S.fold(rows, S.read_events(self.signals_path()), now, discovered=known)

    def fold_signals(self, *, discovered=None, executions=None, run: "_Run | None" = None,
                     emit: bool = True) -> dict:
        """Refold, promote what is due, refresh the promoted cards, and push `signals_changed`.

        This is the one place the state is recomputed, and it is driven from the two places every
        execution passes through — the ingest in `inventory()` and the end of a run in
        `_execute` — so a signal cannot be a run behind what the store says.

        Three things happen after the fold, in this order.  **Promotion** of the signals the rules
        say are due (R9: persistent, or confirmed flaky; `gave-up` is promoted by the tool that
        released it), which writes cards, so the state is folded again.  The machine-owned
        `## Signal` **section** of every promoted card is rewritten in place, so a card never
        disagrees with its signal.  And the payload goes out — but only when it *changed*: a
        `tests_list` on a quiet board would otherwise re-send the same rows every refresh.
        """
        from . import signals as S
        signals = self.signal_state(discovered=discovered, executions=executions)
        if self._promote_due(signals):
            signals = self.signal_state(discovered=discovered, executions=executions)
        board = self.board()
        if board is not None:
            for signal in signals.values():
                if signal.card:
                    try:
                        S.rewrite_section(board, signal)
                    except (B.BoardError, OSError):        # pragma: no cover - unwritable board
                        pass
        if run is not None:
            run.opened = [key for key, signal in signals.items()
                          if signal.state == "open" and signal.opened_run in
                          (run.run_id, f"{run.run_id}{RERUN_SUFFIX}")]
        # Unasked pickup (#AQ6X step 7b, decision 9), last of all and before the payload: a thread
        # claims its key, so refolding after one has started is what makes the board's chip name
        # the thread in the same event rather than a refresh later.
        if self.pick_up_signals(signals):
            signals = self.signal_state(discovered=discovered, executions=executions)
        if emit:
            self.emit_signals(signals=signals)
        return signals

    def _promote_due(self, signals: dict) -> list[str]:
        """Write the bug card for every signal R9 says is due, up to the cap.  The keys written.

        `board_tools` owns every card write, so this asks it rather than writing one itself; a
        board that cannot be reached, or a refusal (the cap), leaves the signal in the fold, which
        is exactly what the cap is for.
        """
        from . import signals as S
        due = [s for s in S.sort_signals(signals.values())
               if s.promote in ("persistent", "flaky") and not s.card]
        if not due:
            return []
        room = S.MAX_PROMOTED_OPEN - len(S.promoted_open(signals.values()))
        if room <= 0:
            return []
        tools = self._board_tools()
        if tools is None:
            return []
        written: list[str] = []
        for signal in due[:room]:
            try:
                result = tools.promote_signal(signal.key, signal.promote)
            except Exception:                              # pragma: no cover - defensive
                continue
            if result.get("promoted"):
                written.append(signal.key)
        return written

    def _board_tools(self):
        """A `BoardTools` for the machine's own writes: no limits, no duplicate check, no model.

        Made on first use and kept, because `_promote_due` may be reached from every run.  It is
        deliberately not the pane's instance: this write is the fold's, it counts against no
        turn's budget, and the card it makes is signed by nobody.
        """
        if self._tools is None:
            board = self.board()
            if board is None:
                return None
            from . import board_tools as BT
            self._tools = BT.BoardTools(
                board, autonomy="auto", enforce_limits=False, duplicate_check=False,
                context=BT.ToolContext(actor="agent"), pane_token=self.pane_token,
                state_path=self.project / ".relay" / "signals-rate.json")
        return self._tools

    def emit_signals(self, rid=None, *, signals: dict | None = None) -> dict:
        """Push `signals_changed`.  With `rid` it is the answer to a `signals_list` request."""
        from . import signals as S
        if signals is None:
            signals = self.signal_state()
        payload = S.summary(signals.values(), session=self.pane_token or "")
        # #AQ6X step 7b: the setting the Options row writes, and the threads running right now —
        # the GUI reads the second one so a chip on a thread's claim can say `live` after a
        # restart, when it has seen no `signal_thread started` event of its own.
        payload["auto_work"] = self.signals_auto_work()[0]
        threads = self.signal_threads()
        payload["threads"] = [{"key": t.key, "thread_id": t.thread_id,
                               "session_id": t.session_id}
                              for t in (threads.running() if threads is not None else [])]
        if rid is None and payload == self._signals_sent:
            return payload
        self._signals_sent = payload
        self.emit({"event": "signals_changed", **_rid(rid), **payload})
        return payload

    def dispatch_signal(self, kind: str, request: dict) -> dict:
        """One `signals_*` request.  A refusal is the same event with `error` and `code`.

        The GUI's path, not the agent's: a dismissal from here is the **owner's**, so all four
        reasons and any expiry are allowed (decision 7) — the agent's two-reason, seven-day limit
        lives in `board_tools.board_signals`, which is the model-facing door.
        """
        from . import signals as S
        if kind == "signals_list":
            return self.emit_signals(request.get("id"))
        if kind == "signals_config":
            return self.write_signals_config(request)
        action = kind.split("_", 1)[1]
        key = str(request.get("key") or "").strip()
        signals = self.signal_state()
        signal = signals.get(key)
        def refuse(message: str, code: str) -> dict:
            out = {"kind": action, "key": key, "error": message, "code": code}
            self.emit({"event": "signals_written", **_rid(request.get("id")), **out})
            return out
        if not key:
            return refuse(f"signals_{action} needs `key`: the signal's key.", "signal_refused")
        if signal is None or signal.state in ("resolved", "removed"):
            return refuse(f"There is no open signal {key!r}.", "signal_not_found")
        path = self.signals_path()
        written = {"kind": action, "key": key}
        if action == "claim":
            token = str(request.get("pane_token") or self.pane_token or "")
            if signal.session and signal.session != token and not request.get("force"):
                return refuse(f"{key} is held by another session ({signal.session[:8]}).",
                              "board_claimed_elsewhere")
            S.append_event({"action": "claim", "key": key, "session": token}, path)
            written["session"] = token
        elif action == "release":
            S.append_event({"action": "release", "key": key,
                            "reason": str(request.get("reason") or ""),
                            "session": signal.session}, path)
        elif action == "dismiss":
            try:
                dismissal = S.check_dismissal(request.get("reason"), request.get("comment"),
                                              request.get("until"), by_agent=False)
            except S.SignalError as exc:
                return refuse(str(exc), exc.code)
            S.append_event({"action": "dismiss", "key": key, "by": "owner", **dismissal}, path)
            written.update(dismissal)
        else:                                              # promote
            tools = self._board_tools()
            if tools is None:
                return refuse("This project has no Switchboard to file a card on.",
                              "signal_refused")
            result = tools.promote_signal(key, "by hand")
            if result.get("error"):
                return refuse(str(result["error"]), str(result.get("code") or "signal_refused"))
            if result.get("card"):
                written["card"] = result["card"]
        self.emit({"event": "signals_written", **_rid(request.get("id")), **written})
        self.fold_signals()
        return written

    # ---- signal threads (#AQ6X step 7b, decision 9) ----------------------------
    def signals_auto_work(self) -> tuple[bool, str]:
        """`(auto_work, autonomy)` from this board's `board.yaml` — the two gates on a pickup.

        A board that cannot be read at all answers the defaults rather than refusing to work:
        `signals.auto_work` absent means true (the owner's "yes by default"), and the autonomy of
        a board with no config is the one `DEFAULT_CONFIG` gives it.  A project with no
        Switchboard has nothing to fold and nothing to work, and says so with `autonomy: off`.
        """
        from . import signal_threads as ST
        board = self.board()
        if board is None:
            return False, "off"
        try:
            config = board.config()
        except (B.BoardError, OSError, ValueError):         # pragma: no cover - unreadable yaml
            return ST.AUTO_WORK_DEFAULT, "auto"
        agent = config.get("agent") if isinstance(config.get("agent"), dict) else {}
        return ST.auto_work(config), str(agent.get("autonomy") or "auto")

    def signal_threads(self):
        """This project's `SignalThreads`, made on first use; None when nothing can spawn one.

        The agent's own `TestsCommands` instance (`board_tools._tests`) has no `spawn_agent`, and
        that is the point: a tool call must not quietly start three background agents.  Only the
        worker's instance, which `board_protocol._tests()` wires, picks anything up.
        """
        if self.spawn_agent is None:
            return None
        if self._threads is None:
            from . import signal_threads as ST
            self._threads = ST.SignalThreads(
                self._spawn_signal_thread, self.emit,
                claim=self._signal_thread_claim, release=self._signal_thread_release)
        return self._threads

    def pick_up_signals(self, signals: dict) -> list:
        """Start a thread on every orphan this fold is allowed to, and say which.

        Never raises into the fold: a pickup that cannot start is a signal left where it was.
        """
        threads = self.signal_threads()
        if threads is None:
            return []
        from . import signal_threads as ST
        auto, autonomy = self.signals_auto_work()
        board = self.board()
        folder = board.root.name if board is not None else ""
        try:
            return threads.start_due(
                signals.values(),
                lambda signal: ST.task_text(signal, project=str(self.project),
                                            board_folder=folder),
                auto_work_on=auto, autonomy=autonomy)
        except Exception:                                    # pragma: no cover - defensive
            return []

    def _signal_thread_claim(self, key: str, token: str) -> None:
        from . import signals as S
        S.append_event({"action": "claim", "key": key, "session": token}, self.signals_path())

    def _signal_thread_release(self, key: str, token: str, reason: str) -> None:
        from . import signals as S
        S.append_event({"action": "release", "key": key, "session": token, "reason": reason},
                       self.signals_path())

    def _spawn_signal_thread(self, task: str, description: str):
        """Start the subagent and arrange to hear that it ended.  `(thread, agent, session)`.

        The watcher is one daemon thread per pickup, waiting on the subagent's own `done` event —
        the same event `agent_wait` waits on.  Nothing in the worker polls, and a subagent that
        never finishes simply never posts its second notification, which is what a thread that is
        still working should look like.
        """
        spawned = self.spawn_agent(task, description)
        if not spawned:
            return None
        thread_id, agent_id, session_id, done = (list(spawned) + [None] * 4)[:4]
        if done is not None:
            threading.Thread(target=self._await_signal_thread, args=(str(thread_id or ""), done),
                             name=f"relay-signal-thread-{agent_id}", daemon=True).start()
        return str(thread_id or ""), str(agent_id or ""), str(session_id or "")

    def _await_signal_thread(self, thread_id: str, done) -> None:
        try:
            done.wait()
            self.signal_thread_ended(thread_id)
        except Exception:                                    # pragma: no cover - defensive
            pass

    def signal_thread_ended(self, thread_id: str, *, verify: bool = True) -> dict | None:
        """One signal thread's agent has stopped: say how it went, and free the key.

        The **check's** verdict decides, not the agent's report (`SignalThreads.outcome_for`).  A
        thread that ran to the end with the signal still open and still unpromoted has given up
        whether it said so or not, so the card is written here — that is promotion trigger (a) of
        R9, and leaving it out would let an agent close a fault by going quiet.
        """
        from . import signal_threads as ST
        from . import signals as S
        threads = self.signal_threads()
        if threads is None:
            return None
        key = threads.key_of(thread_id)
        if not key:
            return None
        signals = self.signal_state()
        signal = signals.get(key)
        # The check decides — so the check is **run**.  A signal thread is a subagent with
        # `run_command`: whatever `ctest` it ran was a subprocess in its own shell and landed in no
        # store, so the fold has seen nothing since the failure that opened the signal.  Without
        # this, a thread that really fixed its test still read as `open` and was promoted as a
        # give-up (found by the live run in docs/qa_evidence/2026-09-20-signal-threads).
        if signal is not None and signal.state == "open" and verify:
            self.verify_signal(signal)
            signals = self.signal_state()
            signal = signals.get(key)
        outcome = ST.SignalThreads.outcome_for(signal)
        card = signal.card if signal is not None else ""
        if outcome == "stopped" and signal is not None and signal.state == "open":
            card = self._promote_gave_up(key) or card
            if card:
                outcome = "gave-up"
        finished = threads.finish(thread_id, outcome=outcome, card=card,
                                  reason=S.GAVE_UP if outcome == "gave-up" else outcome)
        self.fold_signals()
        return finished.event("finished") if finished is not None else None

    def verify_signal(self, signal, *, timeout: float = 300.0) -> int:
        """Run one signal's key until it has passed enough times to resolve.  The passes seen.

        `RESOLVE_PASSES[kind]` consecutive passing executions resolve a signal and **nothing else
        does** (decision 4), so a thread that says it fixed something is answered by that many runs
        of that one key — recorded in the history like any other run, which is what makes the fold
        see them.  A failure ends it early: the fix did not work, and running it again proves
        nothing.  A key this project cannot run from here (not collected, no build directory) is
        left exactly as it was, because a verdict from a check that did not run is not a verdict.
        """
        from . import signals as S
        need = max(1, S.RESOLVE_PASSES.get(signal.kind, 2) - max(0, signal.green_streak))
        passes = 0
        for _ in range(need):
            try:
                result = self.run_and_wait([signal.key], timeout=timeout)
            except TestsError:
                return passes
            rows = result.get("tests") or []
            if not rows or any(row.get("result") != "pass" for row in rows):
                return passes
            passes += 1
        return passes

    def _promote_gave_up(self, key: str) -> str:
        """The bug card for a signal its thread could not fix, or "" (the cap, or no board)."""
        from . import signals as S
        tools = self._board_tools()
        if tools is None:
            return ""
        try:
            result = tools.promote_signal(key, S.GAVE_UP)
        except Exception:                                    # pragma: no cover - the cap refuses
            return ""
        return str(result.get("card") or "")

    def write_signals_config(self, request: dict) -> dict:
        """`signals_config {auto_work}` (§32.2): the Options row, written into `board.yaml`.

        One flag, through `board.write_config` like every other board setting, and the answer is a
        `signals_written {kind: "config"}` followed by the state event — so a second Switchboard
        pane on the same project learns the new value without asking.
        """
        from . import signal_threads as ST
        rid = request.get("id")
        on = request.get("auto_work")
        out = {"kind": "config"}
        def refuse(message: str, code: str = "signal_refused") -> dict:
            self.emit({"event": "signals_written", **_rid(rid), **out,
                       "error": message, "code": code})
            return {**out, "error": message, "code": code}
        if not isinstance(on, bool):
            return refuse("signals_config needs `auto_work`: true or false.")
        board = self.board()
        if board is None:
            return refuse("This project has no Switchboard to configure.")
        try:
            B.write_config(board, ST.with_auto_work(board.config(), on))
        except (B.BoardError, OSError, ValueError) as exc:
            return refuse(f"board.yaml could not be written ({exc}).")
        out["auto_work"] = on
        self.emit({"event": "signals_written", **_rid(rid), **out})
        self.fold_signals()
        return out

    # ---- the needs-verification gate -------------------------------------------
    def gate_move(self, card_id, status) -> dict | None:
        """Why this card may not leave `needs-verification` yet, or None when it may.

        The owner's rule (#7BM4): a card does not land while the tests it *names* are gone,
        have never run, or last failed.  A card that names **no** tests is not gated — the
        `no-tests` finding is advisory, because gating on it would stop every card that predates
        the section from ever closing.
        """
        result = self.check_card(card_id)
        if any(f.get("verdict") == "no-tests" for f in result.get("findings") or []):
            return None
        offending: list[str] = []
        reasons: list[str] = []
        for finding in result.get("findings") or []:
            if finding.get("verdict") not in GATE_VERDICTS:
                continue
            name = str(finding.get("test") or "")
            if name and name not in offending:
                offending.append(name)
            reasons.append(str(finding.get("message") or ""))
        for test_id in result.get("failing") or []:
            if test_id not in offending:
                offending.append(test_id)
                reasons.append(f"{test_id} last failed here")
        if not offending:
            return None
        # Short names in the sentence (the notice is one narrow box, and a unittest id is a
        # whole dotted path); `tests` keeps the full ids for whoever needs them.
        short = [name.split(":", 1)[-1].rsplit(".", 1)[-1] for name in offending]
        shown = ", ".join(short[:3]) + ("…" if len(short) > 3 else "")
        return {"card": result["card"], "status": str(status or ""),
                "tests": offending, "findings": result.get("findings") or [],
                "message": (f"#{result['card']} still has {len(offending)} test(s) that do not "
                            f"prove it ({shown}): run or fix them, or move it with an override "
                            f"that says why."),
                "reasons": reasons}

    def card_files(self, card_id: str, card=None) -> list[str]:
        """The repo-relative files this card's commits touched, newest commit first.

        The commits are found the way the `qa` block finds them (`qa_verifiers.card_commits`:
        `links.commits` first, then `git log --grep '#ID'`), and each one's files come from
        `git show --name-only`.  No git, no repository or an unknown hash is an empty list —
        "this card changed nothing we can see", which reads as no orphaned finding at all.
        """
        from . import qa_verifiers as QA
        links = (card.front.get("links") if card is not None else None) or {}
        try:
            rows = QA.card_commits(self.project, card_id, links if isinstance(links, dict) else {})
        except Exception:                                    # pragma: no cover - defensive
            rows = []
        out: list[str] = []
        seen: set[str] = set()
        for row in rows[:MAX_COMMITS]:
            for name in self._commit_files(str(row.get("hash") or "")):
                if name not in seen:
                    seen.add(name)
                    out.append(name)
                if len(out) >= MAX_CHANGED_FILES:
                    return out
        return out

    def _commit_files(self, sha: str) -> list[str]:
        if not sha:
            return []
        try:
            done = subprocess.run(["git", "-C", str(self.project), "show", "--name-only",
                                   "--pretty=format:", sha],
                                  capture_output=True, text=True, timeout=GIT_TIMEOUT)
        except (OSError, subprocess.SubprocessError):
            return []
        if done.returncode != 0:
            return []
        return [line.strip() for line in done.stdout.splitlines() if line.strip()]

    def head_commit(self) -> str:
        try:
            done = subprocess.run(["git", "-C", str(self.project), "rev-parse", "HEAD"],
                                  capture_output=True, text=True, timeout=GIT_TIMEOUT)
        except (OSError, subprocess.SubprocessError):
            return ""
        return done.stdout.strip() if done.returncode == 0 else ""

    # ---- tests_run -------------------------------------------------------------
    def running(self) -> bool:
        run = self._run
        return run is not None and not run.finished.is_set()

    @staticmethod
    def _partition(ids: Sequence[str]) -> tuple[list[str], list[str], list[str]]:
        """`(ctest ids, unittest ids, skipped ids)`, deduplicated and in the order given."""
        ctest, unittest_, skipped = [], [], []
        for value in dict.fromkeys(str(v).strip() for v in ids if str(v).strip()):
            runner = value.split(":", 1)[0] if ":" in value else ""
            if runner not in RUNNABLE:
                skipped.append(value)
            elif runner == P.RUNNER_CTEST:
                ctest.append(value)
            else:
                unittest_.append(value)
        return ctest, unittest_, skipped

    def start_run(self, ids, *, repeat_until_fail=0, build_dir=None,
                  allow_all: bool = False, max_ids: int = MAX_IDS) -> _Run:
        """Validate and start one run; the work happens on a thread and emits as it goes.

        Every refusal is one sentence and raises `TestsError` — the caller turns it into the
        contract's `{"state": "error", "message"}` (the wire) or into a tool error (the agent).
        """
        if not isinstance(ids, (list, tuple)) or not ids:
            raise TestsError("tests_run needs `ids`: name the tests to run, because nothing "
                             "here starts the whole suite implicitly.")
        if not all(isinstance(value, str) for value in ids):
            raise TestsError("tests_run ids must be strings, each a test's "
                             "`<runner>:<invocation>` key.")
        if len(ids) > max_ids and not allow_all:
            raise TestsError(f"tests_run was given {len(ids)} ids; it runs at most {max_ids} "
                             "in one go unless the request says `all: true`.")
        try:
            repeat = max(0, min(MAX_REPEAT, int(repeat_until_fail or 0)))
        except (TypeError, ValueError):
            raise TestsError("tests_run repeat_until_fail must be a number of attempts.") from None
        ctest, unit, skipped = self._partition(ids)
        if not ctest and not unit:
            raise TestsError("None of those ids names a test this can run: a `manual:` entry is "
                             "evidence recorded by hand, not a command.")
        build = self.build_dir(build_dir)
        if ctest and not (build / "CTestTestfile.cmake").is_file():
            raise TestsError(f"There is no configured build directory at {build}, so the ctest "
                             "tests cannot run; build the project first.")
        with self._lock:
            if self.running():
                raise TestsError(f"A test run is already going ({self._run.run_id}); stop it "
                                 "before starting another.")
            run = _Run(self._new_run_id(), ctest + unit, skipped)
            self._run = run
        thread = threading.Thread(target=self._execute, name="relay-tests-run",
                                  args=(run, ctest, unit, repeat, build), daemon=True)
        thread.start()
        return run

    def stop_run(self, run_id=None) -> bool:
        run = self._run
        wanted = str(run_id or "").strip()
        if run is None or run.finished.is_set() or (wanted and wanted != run.run_id):
            raise TestsError(f"There is no test run {wanted or '(none named)'} to stop.")
        run.stopped = True
        run.cancel.set()
        for job in list(run.jobs):
            try:
                self.jobs.stop(job)
            except Exception:                                # pragma: no cover - already gone
                pass
        return True

    def _new_run_id(self) -> str:
        stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime(self.clock()))
        return f"{stamp}-{os.urandom(2).hex()}"

    # ---- the run itself --------------------------------------------------------
    def _execute(self, run: _Run, ctest: list[str], unit: list[str], repeat: int,
                 build: Path) -> None:
        """One run, start to finish, on its own thread.  Never raises out of the thread."""
        tmp = Path(tempfile.mkdtemp(prefix="rt-", dir="/tmp"))
        try:
            started = {"state": "started", "done": 0, "total": run.total,
                       "message": self._started_message(ctest, unit, repeat, run.skipped)}
            self._emit_run(started, run_id=run.run_id)
            executions: list[H.Execution] = []
            commit = self.head_commit()
            # Which *tree* this ran on, once per run (#AQ6X step 1): several sessions edit this
            # checkout, so `commit` alone does not name the code under test.
            tree = H.tree_digest_of(self.project)
            env = self._environment(tmp)
            if ctest and not run.cancel.is_set():
                executions += self._run_ctest(run, ctest, repeat, build, tmp, env, commit,
                                              tree_digest=tree)
            if unit and not run.cancel.is_set():
                executions += self._run_unittest(run, unit, repeat, tmp, env, commit,
                                                 tree_digest=tree)
            # The JUnit files are the authority: anything the live output did not already report
            # is emitted here, so a pane that missed a line still ends with every verdict.
            for row in executions:
                if row.id in run.reported:
                    continue
                run.reported.add(row.id)
                if row.id in run.ids:
                    run.done += 1
                self._emit_progress(run, row.id, row.result, row.duration)
            self._store(executions)
            self._note_run(run.run_id)
            # Decision 3: a short failed set is re-run **once**, right now, so "open on the
            # second consecutive failure" takes seconds instead of waiting for the next run.
            executions += self._rerun_failures(run, executions, build, tmp, env, commit, tree,
                                               repeat)
            run.state = "stopped" if run.stopped else "finished"
            self._emit_run({"state": run.state, "done": run.done, "total": run.total,
                            "message": self._finished_message(run, executions)},
                           run_id=run.run_id)
            try:
                self.fold_signals(run=run)
            except Exception:                                # pragma: no cover - defensive
                pass
        except Exception as exc:                             # pragma: no cover - belt and braces
            run.state = "error"
            run.message = f"The test run failed: {type(exc).__name__}: {str(exc)[:300]}"
            self._emit_run({"state": "error", "done": run.done, "total": run.total,
                            "message": run.message}, run_id=run.run_id)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
            # The fresh list goes out *before* the run is marked finished: a caller that waits
            # for the run (the agent's tool, a test) is then waiting for the whole thing, and
            # nothing of this run is still reading the project after it returns.
            try:
                self.emit_list()
            except Exception:                                # pragma: no cover - defensive
                pass
            run.finished.set()

    def _note_run(self, run_id: str) -> None:
        """Leave one `run` line in the signal log: this run, and the pane whose it was.

        An execution carries no pane token — the store is shared with every other producer — and
        the verification gate of decision 8 needs to know whose run first failed a key.  One line
        per run answers it, and a board with no signal store simply has none.
        """
        if not run_id:
            return
        try:
            from . import signals as S
            S.append_event({"action": "run", "run_id": run_id,
                            "session": self.pane_token or ""}, self.signals_path())
        except Exception:                                    # pragma: no cover - unwritable board
            pass

    def _rerun_failures(self, run: _Run, executions: Sequence[H.Execution], build: Path,
                        tmp: Path, env: dict, commit: str, tree: str,
                        repeat: int = 0) -> list[H.Execution]:
        """Decision 3's one re-run of a short failed set, as its own run.  The rows it stored.

        `signals.rerun_keys` decides: ten or fewer failures whose recorded p50 durations sum to a
        minute.  Anything bigger stays `pending` and is answered by the next natural run, because
        a button that quietly re-runs six minutes of tests is not a button anybody wants.

        It is a **separate** `run_id` (`RERUN_SUFFIX`) so the fold reads it as a second execution
        of each key: fail-fail is then two consecutive failures and opens the signal at once, and
        fail-pass is one tree disagreeing with itself, which is the flaky mark (R4).

        A run that asked for `repeat_until_fail` is left alone: it has already run each test
        several times, and a re-run after it would say nothing new.
        """
        from . import signals as S
        if run.stopped or run.cancel.is_set() or run.rerun or repeat:
            return []                      # `repeat_until_fail` already ran them more than once
        failed = [row.id for row in executions if row.result in S.FAILING]
        if not failed:
            return []
        known = H.read(self.store_path(), ids=failed)
        keys = S.rerun_keys(failed, list(known) + list(executions))
        if not keys:
            return []
        run.rerun = keys
        ctest, unit, _ = self._partition(keys)
        run_id = f"{run.run_id}{RERUN_SUFFIX}"
        folder = tmp / "rerun"
        folder.mkdir(parents=True, exist_ok=True)
        rows: list[H.Execution] = []
        if ctest and not run.cancel.is_set():
            rows += self._run_ctest(run, ctest, 0, build, folder, env, commit,
                                    run_id=run_id, tree_digest=tree)
        if unit and not run.cancel.is_set():
            rows += self._run_unittest(run, unit, 0, folder, env, commit,
                                       run_id=run_id, tree_digest=tree)
        self._store(rows)
        self._note_run(run_id)
        return rows

    def _environment(self, tmp: Path) -> dict:
        """`scripts/test.sh`'s isolation, plus the offscreen platform and this project's backend.

        Every XDG directory and `TMPDIR` go under one short `/tmp` path: a Qt or D-Bus socket
        made under a long one hits the 108-byte `sun_path` limit and the test fails for a reason
        that has nothing to do with the test.
        """
        env = dict(os.environ)
        for name in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME",
                     "XDG_RUNTIME_DIR", "TMPDIR"):
            path = tmp / name.lower()
            path.mkdir(parents=True, exist_ok=True)
            path.chmod(0o700)
            env[name] = str(path)
        env["RELAY_KEYRING"] = "off"
        env["RELAY_LOCAL_MODELS"] = str(tmp / "local-models.json")
        env["QT_QPA_PLATFORM"] = "offscreen"
        backend = self.project / "backend"
        if backend.is_dir():
            existing = env.get("PYTHONPATH")
            env["PYTHONPATH"] = f"{backend}:{existing}" if existing else str(backend)
        return env

    def _run_ctest(self, run: _Run, ids: list[str], repeat: int, build: Path, tmp: Path,
                   env: dict, commit: str, *, run_id: str = "",
                   tree_digest: str = "") -> list[H.Execution]:
        """Every ctest id in **one** `ctest` invocation, with its JUnit file read at the end."""
        names = [value.split(":", 1)[1] for value in ids]
        pattern = "^(" + "|".join(re.escape(name) for name in names) + ")$"
        junit = tmp / "ctest.xml"
        parts = ["ctest", "--test-dir", str(build), "-R", pattern,
                 "--output-junit", str(junit), "--timeout", str(CTEST_TEST_TIMEOUT)]
        if repeat:
            parts += ["--repeat", f"until-fail:{repeat}"]
        self._run_command(run, " ".join(shlex.quote(p) for p in parts), env, live=True)
        return H.ingest_junit(junit, runner=P.RUNNER_CTEST, commit=commit,
                              run_id=run_id or run.run_id, tree_digest=tree_digest)

    def _run_unittest(self, run: _Run, ids: list[str], repeat: int, tmp: Path, env: dict,
                      commit: str, *, run_id: str = "",
                      tree_digest: str = "") -> list[H.Execution]:
        """Every unittest id in **one** `relay_core.junit_runner` invocation.

        `--repeat until-fail:N` has no equivalent in `unittest`, so the shell loops instead and
        stops at the first non-zero exit: the JUnit file left behind is then the failing attempt,
        which is the one worth keeping.
        """
        names = [value.split(":", 1)[1] for value in ids]
        junit = tmp / "unittest.xml"
        parts = [self.python, "-m", "relay_core.junit_runner", "--junit", str(junit),
                 "--root", str(self.project), *names]
        command = " ".join(shlex.quote(p) for p in parts)
        if repeat:
            command = f"for _ in $(seq 1 {repeat}); do {command} || exit 1; done"
        self._run_command(run, command, env, live=False)
        return H.ingest_junit(junit, runner=P.RUNNER_UNITTEST, commit=commit,
                              run_id=run_id or run.run_id, tree_digest=tree_digest)

    def _run_command(self, run: _Run, command: str, env: dict, *, live: bool) -> None:
        """Start one command as a job and wait for it, streaming ctest's progress lines."""
        job = self.jobs.start(command, str(self.project), env)
        run.jobs.append(job)
        buffer = [""]

        def on_text(text: str) -> None:
            buffer[0] += text
            *lines, buffer[0] = buffer[0].split("\n")
            for line in lines:
                event = parse_ctest_line(line)
                if event is None:
                    continue
                if "result" in event:
                    if event["id"] in run.reported:
                        continue
                    run.reported.add(event["id"])
                    if event["id"] in run.ids:
                        run.done += 1
                self._emit_progress(run, event["id"], event.get("result"),
                                    event.get("duration"))

        self.jobs.wait(job, RUN_TIMEOUT, run.cancel, live=on_text if live else None)
        if job.running:
            # Cancelled, or past the ceiling: either way nothing else may start after it.
            run.cancel.set()
            self.jobs.stop(job)
        if job.stopped:
            run.stopped = True

    def _store(self, executions: Sequence[H.Execution]) -> int:
        if not executions:
            return 0
        host = _hostname()
        for row in executions:
            if not row.host:
                row.host = host
        return H.append(executions, self.store_path())

    # ---- what a run says -------------------------------------------------------
    @staticmethod
    def _started_message(ctest: list[str], unit: list[str], repeat: int,
                         skipped: Sequence[str]) -> str:
        parts = []
        if ctest:
            parts.append(f"{len(ctest)} ctest")
        if unit:
            parts.append(f"{len(unit)} unittest")
        text = "running " + " and ".join(parts)
        if repeat:
            text += f", until one fails (at most {repeat} times)"
        if skipped:
            text += f"; {len(skipped)} not runnable from here"
        return text

    @staticmethod
    def _finished_message(run: _Run, executions: Sequence[H.Execution]) -> str:
        if run.stopped:
            return f"stopped after {run.done} of {run.total}"
        # One line per *test*, not per execution: a key the re-run of decision 3 tried twice
        # counts once, with its newest verdict, or a run of two tests would say "2 failed"
        # because one of them failed twice.
        newest: dict[str, H.Execution] = {}
        for row in executions:
            newest[row.id] = row
        counts = {"pass": 0, "fail": 0, "skip": 0}
        for row in newest.values():
            counts["pass" if row.result == "pass" else
                   "skip" if row.result == "skip" else "fail"] += 1
        seconds = sum(row.duration for row in newest.values())
        parts = [f"{counts['pass']} passed"]
        if counts["fail"]:
            parts.append(f"{counts['fail']} failed")
        if counts["skip"]:
            parts.append(f"{counts['skip']} skipped")
        if not executions:
            return "the run produced no results"
        return ", ".join(parts) + f" in {format_seconds(seconds)}"

    # ---- events ----------------------------------------------------------------
    def _emit_run(self, fields: dict, *, run_id: str) -> None:
        self.emit({"event": "tests_run", "run_id": run_id, **fields})

    def _emit_progress(self, run: _Run, test_id: str, result, duration) -> None:
        event = {"event": "tests_run", "run_id": run.run_id, "state": "progress",
                 "done": run.done, "total": run.total}
        if test_id:
            event["id"] = test_id
        if result:
            event["result"] = result
            if duration is not None:
                event["duration"] = round(float(duration), 6)
        self.emit(event)

    # ---- the blocking path the agent tool uses ---------------------------------
    def run_and_wait(self, ids, *, timeout: float = 300.0, repeat_until_fail: int = 0,
                     max_ids: int = MAX_AGENT_IDS) -> dict:
        """Start a run and wait for it: what an agent's `tests_run` tool answers with.

        The same code the pane drives, with the events going nowhere: a model cannot read a
        stream, so it gets the table at the end — every test that ran, its verdict, its duration
        and, for a failure, the message from the JUnit file.
        """
        run = self.start_run(ids, repeat_until_fail=repeat_until_fail, max_ids=max_ids)
        completed = run.finished.wait(max(1.0, float(timeout)))
        if not completed:
            try:
                self.stop_run(run.run_id)
            except TestsError:                               # pragma: no cover - it just ended
                pass
            run.finished.wait(30)
        rows = H.read(self.store_path(), ids=list(run.ids))
        # The re-run is its own `run_id` in the store (`RERUN_SUFFIX`) but the same answer here:
        # the verdict the model needs is the *second* one, which is why the re-run happened.
        ours = (run.run_id, f"{run.run_id}{RERUN_SUFFIX}")
        mine = [row for row in rows if row.run_id in ours]
        newest: dict[str, H.Execution] = {}
        for row in mine:
            newest[row.id] = row                             # the store is oldest first
        table = [{"id": row.id, "result": row.result, "duration": round(row.duration, 3),
                  **({"message": row.message[:500]} if row.message else {})}
                 for row in sorted(newest.values(), key=lambda r: r.id)]
        counts = {"pass": 0, "fail": 0, "skip": 0}
        for row in newest.values():
            counts["pass" if row.result == "pass" else
                   "skip" if row.result == "skip" else "fail"] += 1
        return {"run_id": run.run_id, "state": "timed-out" if not completed else run.state,
                "requested": len(run.ids), "ran": len(table), "counts": counts,
                "tests": table, "skipped": list(run.skipped),
                # What this run put on the board, so the pane that ran it learns in the same turn
                # what it broke (#AQ6X step 7a) — and `rerun` says which keys were tried twice.
                "opened": list(run.opened), "rerun": list(run.rerun),
                "message": ("The run passed its timeout and was stopped."
                            if not completed else self._finished_message(run, []) or "")}


def format_seconds(seconds: float) -> str:
    """`"35 ms"`, `"1.4 s"`, `"2.1 m"` — a run's wall time, never `"0.0 s"`.

    A whole suite is minutes and one test is often under a millisecond, and a line that says a
    run took `0.0 s` reads as "it did not run" rather than as "it was fast".
    """
    value = max(0.0, float(seconds))
    if value < 1.0:
        return f"{value * 1000:.0f} ms"
    if value < 120.0:
        return f"{value:.1f} s"
    return f"{value / 60.0:.1f} m"


def _rid(rid) -> dict:
    """`{"id": rid}` when there is a request to answer, and nothing at all when there is not.

    `tests_list`, `tests_check` and `tests_suggest` are the events with the `id` key free (on
    the other two it is the *test*), so a null there would be a key the contract does not have.
    """
    return {"id": rid} if rid is not None else {}


def _hostname() -> str:
    try:
        return socket.gethostname()
    except OSError:                                          # pragma: no cover - nameless host
        return ""


def format_findings(result: dict) -> str:
    """A `tests_check` answer as the text an agent reads: one line per finding, then the actions."""
    findings = result.get("findings") or []
    card = result.get("card") or ""
    signals = _signal_lines(result)
    if not findings:
        head = (f"#{card}: every test its `## Tests` section names is collected, has run, and is "
                "neither flaky nor slow. Nothing to fix.")
        return head + ("\n" + "\n".join(signals) if signals else "")
    lines = [f"#{card}: {len(findings)} finding{'' if len(findings) == 1 else 's'}."]
    for item in findings:
        test = item.get("test") or ""
        lines.append(f"- [{item.get('severity', 'notice')}] {item.get('verdict', '')}"
                     f"{f' · {test}' if test else ''}: {item.get('message', '')}")
    lines += signals
    actions = result.get("actions") or []
    if actions:
        lines.append("Offered: " + "; ".join(str(a) for a in actions) + ".")
    return "\n".join(lines)


def _signal_lines(result: dict) -> list[str]:
    """The two signal lines Check adds (decision 8): what blocks this card, and what does not."""
    blocks = result.get("blocks") or []
    before = result.get("open_before") or []
    lines = []
    if blocks:
        names = ", ".join(str(s.get("key") or "") for s in blocks[:5])
        lines.append(f"- [error] signal: {len(blocks)} open signal(s) this pane's own runs "
                     f"opened stop this card leaving needs-verification ({names}). A signal "
                     "resolves by its check passing; board_signals lists and claims them.")
    if before:
        names = ", ".join(str(s.get("key") or "") for s in before[:5])
        lines.append(f"- [notice] signal: {len(before)} signal(s) were open before this card "
                     f"({names}); they do not block it.")
    return lines


def format_run(result: dict) -> str:
    """A finished run as the table an agent reads."""
    counts = result.get("counts") or {}
    head = (f"run {result.get('run_id', '')}: {counts.get('pass', 0)} passed, "
            f"{counts.get('fail', 0)} failed, {counts.get('skip', 0)} skipped "
            f"({result.get('ran', 0)} of {result.get('requested', 0)} asked for)")
    lines = [head]
    for row in result.get("tests") or []:
        line = f"  {row.get('result', ''):<5} {row.get('duration', 0):>7.2f}s  {row.get('id', '')}"
        if row.get("message"):
            line += f"\n        {row['message']}"
        lines.append(line)
    for value in result.get("skipped") or []:
        lines.append(f"  skipped (not runnable from here): {value}")
    if result.get("state") == "timed-out":
        lines.append("  the run passed its timeout and was stopped")
    lines += _opened_lines(result)
    return "\n".join(lines)


def _opened_lines(result: dict) -> list[str]:
    """What this run put on the board, and whose it is (#AQ6X step 7a, decision 9).

    The pane that ran the tests is told in the *same* turn, because it is the one agent who knows
    what it just changed: a signal its own run opened is its own to fix before it reports, and
    nobody else picks one up until it has been unclaimed for longer than one fold
    (`relay_core.signal_threads`).  The sentence is here rather than in the board policy because
    it is only true of the run in front of it — a rule on every turn would be a rule about a
    situation that is not happening.
    """
    opened = [str(key) for key in (result.get("opened") or []) if key]
    if not opened:
        return []
    shown = ", ".join(opened[:10])
    return [f"  signals this run opened: {shown}",
            "  A signal your own run opened is yours: claim it with board_signals "
            "{action: claim}, fix it in this turn before you report, then run the test again so "
            "it resolves (two consecutive passes of that key, and nothing else, close it). If "
            "you cannot fix it, release it with reason gave-up, which files it as a bug card."]


# ------------------------------------------------------------------- the card's `## Tests` body

def section_lines(body: str, span) -> list[str]:
    """The `## Tests` section's lines, **without** the `### Check` blocks under it.

    A check block's own lines are prose about tests, not tests: reading them back as entries
    would make every check add phantom "tests" to the next one.  The split is the heading, so a
    hand-written `### ` sub-heading of any other name is left in place and simply parses as the
    prose it is.
    """
    if span is None:
        return []
    section = body[span[0]:span[1]]
    first = CHECK_BLOCK_RE.search(section)
    return section[:first.start() if first else len(section)].splitlines()


def _newest_check(section: str):
    """`((start, end), when)` of the newest `### Check` block in a section, or None."""
    blocks = list(CHECK_BLOCK_RE.finditer(section))
    if not blocks:
        return None
    newest, when = None, None
    for index, match in enumerate(blocks):
        try:
            stamp = datetime.datetime.strptime(
                f"{match.group('date')} {match.group('time')}", "%Y-%m-%d %H:%M")
        except ValueError:                                   # pragma: no cover - regex-guarded
            continue
        end = blocks[index + 1].start() if index + 1 < len(blocks) else len(section)
        if when is None or stamp >= when:
            newest, when = (match.start(), end), stamp
    return None if when is None else (newest, when)


def _check_lines(result: dict) -> str:
    """The body of one dated block: one line per finding, or the one line that says there are none."""
    findings = result.get("findings") or []
    if not findings:
        return "- no findings\n"
    out = []
    for item in findings:
        test = str(item.get("test") or "").strip()
        out.append(f"- {item.get('severity', 'notice')} · {test or 'card'} — "
                   f"{str(item.get('message', '')).strip()}")
    return "\n".join(out) + "\n"


def _check_sentence(result: dict) -> str:
    count = len(result.get("findings") or [])
    if not count:
        return "no findings; every test this card names is collected, has run and passed."
    verdicts = ", ".join(dict.fromkeys(str(f.get("verdict") or "") for f in result["findings"]))
    return f"{count} finding{'' if count == 1 else 's'} ({verdicts}); the block is under `## Tests`."


def _suggest_sentence(result: dict) -> str:
    lines, changed = result.get("lines") or [], result.get("changed") or []
    if not changed:
        return (f"#{result.get('card', '')} has no commits on it yet, so there is nothing to "
                "map to tests.")
    if not lines:
        return (f"No discovered test is named after any of the {len(changed)} file(s) "
                f"#{result.get('card', '')}'s commits touched, so nothing was added.")
    verb = "Added" if result.get("added") else "Found"
    return (f"{verb} {len(lines)} test(s) named after what #{result.get('card', '')}'s commits "
            f"touched.")


def _failing_findings(failing: Sequence[str], already: set, records_list: Sequence[dict]) -> list:
    """One `failing` finding per test whose last stored result was not a pass."""
    by_id = {str(r.get("id") or ""): r for r in records_list}
    out = []
    for test_id in failing or []:
        if test_id in already:
            continue
        record = by_id.get(test_id) or {}
        shown = record.get("invocation") or record.get("name") or test_id
        when = str(record.get("last_run") or "")
        out.append({"test": test_id, "verdict": "failing", "severity": "failure",
                    "message": f"{shown} failed the last time it ran"
                               + (f", {when}" if when else "")})
    return out


def resolved_tests(lines: Sequence[str], records_list: Sequence[dict]) -> dict:
    """The three keys Check's event carries beside its findings (31.5).

    `ids` are the runnable test ids the section's lines resolve to — what *Run these* sends;
    `files` maps a test id to its source, so a finding's row can be clicked open; `failing` are
    the ids whose last stored result was not a pass, which is the third action's target and half
    of the landing gate's rule.  All three are derived from the same resolution `check_card`
    does, so no caller has to redo it and no two callers can disagree about what a line names.
    """
    ids: list[str] = []
    files: dict[str, str] = {}
    failing: list[str] = []
    for line in lines or []:
        entry = H.parse_test_line(line)
        if not entry:
            continue
        found = H.resolve(entry, records_list)
        if not found and entry.get("runner") in RUNNABLE:
            # Gone: keep the id anyway so *Run these* can still be pressed on a list that has
            # one bad line in it, and so the gate can name the line the card actually holds.
            if entry["id"] not in ids:
                ids.append(entry["id"])
            if entry.get("file"):
                files.setdefault(entry["id"], entry["file"])
            continue
        for record in found:
            test_id = str(record.get("id") or "")
            if not test_id:
                continue
            if record.get("runner") in RUNNABLE and test_id not in ids:
                ids.append(test_id)
            source = str(record.get("file") or "") or str(entry.get("file") or "")
            if source:
                files.setdefault(test_id, source)
            if record.get("last_result") in H.BAD_RESULTS and test_id not in failing:
                failing.append(test_id)
    return {"ids": ids[:MAX_IDS], "files": files, "failing": failing}
