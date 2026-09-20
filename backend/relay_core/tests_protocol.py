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

#: The requests this class answers.  `board_protocol.TYPES` includes them and delegates here.
TYPES = frozenset({"tests_list", "tests_run", "tests_stop", "tests_history", "tests_check"})

#: The card section that lists what proves a card, and the statuses at which not having one is
#: worth reporting: a card being worked, or waiting for a verifier, with no tests named is the
#: verification backlog the pane's board-level report exists to show.
TESTS_HEADING = "Tests"
UNTESTED_STATUSES = ("executing", "in-progress", "needs-verification",
                     "needs-qa-llm", "needs-qa-human")

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
                 clock: Callable[[], float] = time.time):
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
        return True

    def shutdown(self) -> None:
        """Stop whatever is running; the worker is going."""
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
        return {"project": str(self.project), "tests": records,
                "summary": H.summary(records), "cards_without_tests": without}

    def emit_list(self, rid=None, *, build_dir=None) -> dict:
        payload = self.inventory(build_dir=build_dir)
        self.emit({"event": "tests_list", "id": rid, **payload})
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
            text = B.section_text(card.body, TESTS_HEADING)
            lines = [line for line in text.splitlines() if H.parse_test_line(line)]
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
                "actions": ["Add the tests this card's commits touched"]}
        lines = card.body[span[0]:span[1]].splitlines()
        discovered = P.discover(self.project, build_dir=self.build_dir(build_dir))
        executions = H.read(self.store_path())
        index, _ = self.card_index()
        records = H.records(discovered, executions, index)
        result = H.check_card(lines, self.card_files(ident, card), records)
        return {"card": ident, **result}

    def emit_check(self, card_id, rid=None) -> dict:
        result = self.check_card(card_id)
        self.emit({"event": "tests_check", "id": rid, **result})
        return result

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
            env = self._environment(tmp)
            if ctest and not run.cancel.is_set():
                executions += self._run_ctest(run, ctest, repeat, build, tmp, env, commit)
            if unit and not run.cancel.is_set():
                executions += self._run_unittest(run, unit, repeat, tmp, env, commit)
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
            run.state = "stopped" if run.stopped else "finished"
            self._emit_run({"state": run.state, "done": run.done, "total": run.total,
                            "message": self._finished_message(run, executions)},
                           run_id=run.run_id)
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
                   env: dict, commit: str) -> list[H.Execution]:
        """Every ctest id in **one** `ctest` invocation, with its JUnit file read at the end."""
        names = [value.split(":", 1)[1] for value in ids]
        pattern = "^(" + "|".join(re.escape(name) for name in names) + ")$"
        junit = tmp / "ctest.xml"
        parts = ["ctest", "--test-dir", str(build), "-R", pattern,
                 "--output-junit", str(junit), "--timeout", str(CTEST_TEST_TIMEOUT)]
        if repeat:
            parts += ["--repeat", f"until-fail:{repeat}"]
        self._run_command(run, " ".join(shlex.quote(p) for p in parts), env, live=True)
        return H.ingest_junit(junit, runner=P.RUNNER_CTEST, commit=commit, run_id=run.run_id)

    def _run_unittest(self, run: _Run, ids: list[str], repeat: int, tmp: Path, env: dict,
                      commit: str) -> list[H.Execution]:
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
        return H.ingest_junit(junit, runner=P.RUNNER_UNITTEST, commit=commit, run_id=run.run_id)

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
        counts = {"pass": 0, "fail": 0, "skip": 0}
        for row in executions:
            counts["pass" if row.result == "pass" else
                   "skip" if row.result == "skip" else "fail"] += 1
        seconds = sum(row.duration for row in executions)
        parts = [f"{counts['pass']} passed"]
        if counts["fail"]:
            parts.append(f"{counts['fail']} failed")
        if counts["skip"]:
            parts.append(f"{counts['skip']} skipped")
        if not executions:
            return "the run produced no results"
        return ", ".join(parts) + f" in {seconds:.1f} s"

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
        mine = [row for row in rows if row.run_id == run.run_id]
        table = [{"id": row.id, "result": row.result, "duration": round(row.duration, 3),
                  **({"message": row.message[:500]} if row.message else {})}
                 for row in sorted(mine, key=lambda r: r.id)]
        counts = {"pass": 0, "fail": 0, "skip": 0}
        for row in mine:
            counts["pass" if row.result == "pass" else
                   "skip" if row.result == "skip" else "fail"] += 1
        return {"run_id": run.run_id, "state": "timed-out" if not completed else run.state,
                "requested": len(run.ids), "ran": len(table), "counts": counts,
                "tests": table, "skipped": list(run.skipped),
                "message": ("The run passed its timeout and was stopped."
                            if not completed else self._finished_message(run, []) or "")}


def _hostname() -> str:
    try:
        return socket.gethostname()
    except OSError:                                          # pragma: no cover - nameless host
        return ""


def format_findings(result: dict) -> str:
    """A `tests_check` answer as the text an agent reads: one line per finding, then the actions."""
    findings = result.get("findings") or []
    card = result.get("card") or ""
    if not findings:
        return (f"#{card}: every test its `## Tests` section names is collected, has run, and is "
                "neither flaky nor slow. Nothing to fix.")
    lines = [f"#{card}: {len(findings)} finding{'' if len(findings) == 1 else 's'}."]
    for item in findings:
        test = item.get("test") or ""
        lines.append(f"- [{item.get('severity', 'notice')}] {item.get('verdict', '')}"
                     f"{f' · {test}' if test else ''}: {item.get('message', '')}")
    actions = result.get("actions") or []
    if actions:
        lines.append("Offered: " + "; ".join(str(a) for a in actions) + ".")
    return "\n".join(lines)


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
    return "\n".join(lines)
