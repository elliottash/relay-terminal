# SPDX-License-Identifier: AGPL-3.0-or-later
"""The per-test execution store, and every number folded out of it.

One append-only JSONL file under the board's private root holds one object per **test
execution** — `{ts, run_id, id, runner, result, duration, commit, host}` — and nothing else is
stored: `runs`, `reliability`, `duration_p50`, `duration_p95`, `flake_score`, `slow`, `flaky` and
the staleness verdicts are all a fold over that log, computed on demand.  That is the shape
Buildkite, Codecov and Bencher converge on (research §3.5), and it is the shape that lets Check
answer a card without running a single test.

The store is a *store*, not source: it lives at `<board>/.private/tests/history.jsonl`, which
`board.GITIGNORE_TEXT` already keeps out of git, and the numbers that matter are committed into
cards by Check.  Its path is a parameter everywhere; `default_path()` is only the default.

## What is defined here, and where each definition comes from

* **reliability** — `pass / (pass + fail) * 100`, Buildkite's own formula
  (https://buildkite.com/docs/test-engine/test-suites), omitted when nothing ran.
* **p50 / p95** — *nearest-rank*: the value at 1-based index `ceil(p/100 × n)` of the durations
  sorted ascending, clamped to the ends.  No interpolation, so a percentile is always a duration
  that really happened.  Both are kept because a bimodal test (fast normally, 30 s when it retries
  a socket) is invisible in a mean.
* **flake_score** — TestGrid's recency-weighted count of pass↔fail **transitions**: with `n`
  pass/fail outcomes oldest-first, the transition between outcome `k` and `k+1` weighs
  `0.5 ** ((n - 2 - k) / FLAKE_HALF_LIFE)`, so the newest transition counts 1.0 and one
  `FLAKE_HALF_LIFE` (5) executions older counts 0.5.  A steady test scores 0.
* **flaky** — Datadog's definition first (a pass *and* a fail at the same commit), or
  `flake_score >= FLAKE_FLAKY` (2.0).  A test that only ever fails is *broken*, not flaky.
* **slow** — history-relative, never an absolute threshold alone: `duration_p95` in the suite's
  top decile **and** at least `SLOW_SECONDS` (1.0 s), or a p95 over the most recent runs more than
  `SLOW_REGRESSION` (50%) above the trailing median of the ones before them.
* **source_hash** — asv's per-benchmark version stamp.  When a test's source changes, the
  executions recorded under the old hash describe a *different* test, so they are dropped from
  every statistic; the verdict `edited` says so until the test runs again.
* **tree_digest** — what `commit` alone cannot say (signals research R2, card #AQ6X step 1).
  Several sessions share this checkout, so an execution's commit names the last thing that
  *landed*, not the code that ran: two failures "at the same commit" may be two different trees.
  `tree_digest` is empty when the working tree equals `commit`, and otherwise
  `TREE_DIGEST_CHARS` (12) hex of sha256 over `git diff HEAD` — paths and hunks, so the same
  uncommitted edit digests the same on every run.  Nothing here reads it; `signals.py` does, to
  tell Datadog's same-commit flake (a pass and a fail on *one* tree) from two trees disagreeing.

## Retention

`prune()` keeps an execution when **either** it is newer than `RETENTION_DAYS` (120, Buildkite's
window) **or** it is among the newest `RETENTION_PER_TEST` (200) executions of its own test — the
union, so whichever of the two sets is larger is the one kept.  A test that runs constantly keeps
120 days of it; a test that runs twice a year keeps its last 200 runs however old they are.
"""
from __future__ import annotations

import json
import math
import os
import re
import socket
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Iterable, Sequence

from . import board as B
from . import test_probe as P

#: Schema version of the store and of `records()`'s dicts.
HISTORY_VERSION = 1

# --------------------------------------------------------------------------- knobs
RETENTION_DAYS = 120            # Buildkite's retention window
RETENTION_PER_TEST = 200        # …or this many executions per test, whichever set is larger
MAX_HISTORY_CELLS = 20          # executions carried inside one TestRecord (wire contract)
MAX_EXCERPT = 2000              # bytes of a failure message carried into the store
FLAKE_HALF_LIFE = 5.0           # executions over which a transition's weight halves
FLAKE_FLAKY = 2.0               # flake_score at which a test is called flaky
SLOW_SECONDS = 1.0              # a test under this is never called slow on the decile rule
SLOW_DECILE = 90.0              # "top decile" = p95 at or above the suite's 90th percentile
SLOW_REGRESSION = 0.5           # …or a recent p95 more than +50% over the trailing median
SLOW_RECENT = 5                 # how many executions count as "recent" for that comparison
MAX_STORE_BYTES = 256 * 1024 * 1024   # a store past this is read up to the cap, never wholly
TREE_DIGEST_CHARS = 12          # hex characters of sha256(`git diff HEAD`) kept on an execution
TREE_DIGEST_TIMEOUT = 20.0      # seconds `git diff HEAD` may take before the digest is left empty

#: Results, as they travel on the wire.  Anything else a parser sees becomes `"error"`.
RESULTS = ("pass", "fail", "skip", "error", "timeout")
BAD_RESULTS = ("fail", "error", "timeout")

#: The four answers Check gives per **listed test** (card #PR4Q, Codex's review §C: "make the
#: status distinguish passed, failed, missing evidence and not applicable").  A status says
#: whether this card is proven; the older verdicts (`gone`, `never-run`, `flaky`, `slow`, …) say
#: whether a *test* is weak, and travel beside a status as advisory findings.
STATUS_PASSED = "passed"
STATUS_FAILED = "failed"
STATUS_MISSING = "missing-evidence"
STATUS_NA = "not-applicable"
STATUSES = (STATUS_PASSED, STATUS_FAILED, STATUS_MISSING, STATUS_NA)
#: The two that stop a landing.  `not-applicable` never does — a retired test is a thing to
#: replace, not evidence that the work is unfinished — and `passed` is the point.
BLOCKING_STATUSES = (STATUS_FAILED, STATUS_MISSING)

#: Two commit spellings are one commit when either is a prefix of the other and both are at
#: least this long: a card's `links.commits` carries 12 characters, a run's store row whatever
#: `git rev-parse` gave it, and `3f2a9c1e` is the same commit as `3f2a9c1e4d5b`.
COMMIT_PREFIX_MIN = 7
#: Executions carried per test on a `tests_check` event: enough to say who ran it and when.
MAX_EVIDENCE = 3

#: Where the store lives inside a board.
STORE_DIR = "tests"
STORE_NAME = "history.jsonl"


def now_iso() -> str:
    """ISO-8601 UTC to the second, `Z`-suffixed — the store's one timestamp spelling."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _parse_ts(text: str) -> datetime | None:
    if not text:
        return None
    raw = text.strip().replace("Z", "+00:00")
    try:
        parsed = datetime.fromisoformat(raw)
    except ValueError:
        return None
    return parsed if parsed.tzinfo else parsed.replace(tzinfo=timezone.utc)


# --------------------------------------------------------------------------- one execution

@dataclass
class Execution:
    """One test, one run: the only thing this module ever writes.

    `message`/`excerpt` are carried for a failure so a card can show *what* broke without the
    log, `source_hash` so a later fold can tell which executions describe today's test, and
    `tree_digest` which *tree* it ran on (`tree_digest_of()` below).  All four are optional extras
    beside the wire contract's eight fields, which ignore unknown keys.
    """
    ts: str
    id: str
    result: str = "pass"
    duration: float = 0.0
    runner: str = ""
    run_id: str = ""
    commit: str = ""
    host: str = ""
    message: str = ""
    excerpt: str = ""
    source_hash: str = ""
    tree_digest: str = ""

    def to_dict(self) -> dict:
        out = {"ts": self.ts, "run_id": self.run_id, "id": self.id, "runner": self.runner,
               "result": self.result, "duration": round(float(self.duration), 6),
               "commit": self.commit, "host": self.host}
        for key in ("message", "excerpt", "source_hash", "tree_digest"):
            value = getattr(self, key)
            if value:
                out[key] = value
        return out

    @classmethod
    def from_dict(cls, data: dict) -> "Execution | None":
        if not isinstance(data, dict) or not isinstance(data.get("id"), str) or not data["id"]:
            return None
        result = str(data.get("result") or "pass")
        try:
            duration = float(data.get("duration") or 0.0)
        except (TypeError, ValueError):
            duration = 0.0
        return cls(ts=str(data.get("ts") or ""), id=data["id"],
                   result=result if result in RESULTS else "error",
                   duration=duration, runner=str(data.get("runner") or ""),
                   run_id=str(data.get("run_id") or ""), commit=str(data.get("commit") or ""),
                   host=str(data.get("host") or ""), message=str(data.get("message") or ""),
                   excerpt=str(data.get("excerpt") or ""),
                   source_hash=str(data.get("source_hash") or ""),
                   tree_digest=str(data.get("tree_digest") or ""))


def _as_executions(rows: Iterable) -> list[Execution]:
    out: list[Execution] = []
    for row in rows or []:
        if isinstance(row, Execution):
            out.append(row)
        else:
            one = Execution.from_dict(row)
            if one is not None:
                out.append(one)
    return out


# --------------------------------------------------------------------------- the store

def default_path(project: str | os.PathLike, board_root: str | os.PathLike | None = None) -> Path:
    """`<board>/.private/tests/history.jsonl` for `project`.

    The board is found the one way the backend ever finds one — `board.board_folder()`, which
    walks `board`, `.switchboard`, `switchboard`, `issues` in that order — and its `.private/` root is
    `Board.private_root()`, already covered by the `.gitignore` `board.GITIGNORE_TEXT` writes.
    A project with no board yet answers with the path it *would* have under the default folder,
    so a caller can create it.
    """
    project = Path(project).expanduser()
    root = Path(board_root) if board_root is not None else B.board_folder(project)
    if root is None:
        root = project / B.DEFAULT_BOARD_FOLDER
    return B.Board(root, project).private_root() / STORE_DIR / STORE_NAME


def append(executions: Sequence[Execution | dict], path: str | os.PathLike) -> int:
    """Append a batch to the store; returns how many lines were written.

    One `O_APPEND` file, one `os.write` for the whole batch: `O_APPEND` makes the offset and the
    write a single operation, so two sessions appending at once interleave batches but never
    interleave *within* a line.  Nothing is ever rewritten in place, which is why a reader only
    ever has to tolerate a torn **last** line.
    """
    rows = _as_executions(executions)
    if not rows:
        return 0
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    blob = "".join(json.dumps(r.to_dict(), separators=(",", ":"), sort_keys=True) + "\n"
                   for r in rows).encode("utf-8")
    fd = os.open(str(path), os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o644)
    try:
        written = 0
        while written < len(blob):
            written += os.write(fd, blob[written:])
    finally:
        os.close(fd)
    return len(rows)


def read(path: str | os.PathLike, *, limit: int | None = None,
         ids: Sequence[str] | None = None) -> list[Execution]:
    """Every execution in the store, oldest first, tolerantly.

    A line that is not JSON is skipped rather than raising — the last line of an append-only file
    can be torn by a crash mid-write, and a store that cannot be read is worse than one that has
    lost its newest record.  An absent store is an empty list.  `limit` keeps the **newest** n.

    "Oldest first" is by `ts`, **stably**, so rows sharing a timestamp keep the order they were
    appended in.  The store keeps whole seconds and two runs of one test finish inside one second
    all the time; the file is append-only, so its order is the real chronology, and anything else
    as a tiebreak — the `run_id`, which is `<stamp>-<random hex>` — puts an earlier run second as
    often as not.  `signals.fold` reads consecutive failures and passes out of this order, so it
    is load-bearing there (card #AQ6X).
    """
    path = Path(path)
    try:
        if not path.is_file():
            return []
        with open(path, "rb") as handle:
            data = handle.read(MAX_STORE_BYTES)
    except OSError:
        return []
    wanted = set(ids) if ids else None
    out: list[Execution] = []
    for line in data.decode("utf-8", "replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
        except ValueError:
            continue                                     # a torn or half-written line
        one = Execution.from_dict(row)
        if one is None:
            continue
        if wanted is not None and one.id not in wanted:
            continue
        out.append(one)
    out.sort(key=lambda e: e.ts)                         # stable: see the docstring
    return out[-limit:] if limit else out


def prune(path: str | os.PathLike, *, days: int = RETENTION_DAYS,
          per_test: int = RETENTION_PER_TEST, now: datetime | None = None) -> int:
    """Rewrite the store keeping the union of the two retention sets; returns lines dropped.

    Kept: every execution newer than `days`, **plus** the newest `per_test` executions of each
    test however old they are.  The union means neither rule can silently throw away the only
    history a rarely-run test has, and a busy test keeps its whole window.

    The rewrite is atomic (write a sibling, `os.replace`), so a reader never sees a half-file; a
    batch appended between the read and the replace would be lost, which is why pruning is a
    maintenance call and not something a run does.
    """
    path = Path(path)
    rows = read(path)
    if not rows:
        return 0
    cutoff = (now or datetime.now(timezone.utc)) - timedelta(days=days)
    keep: set[int] = set()
    per_id: dict[str, list[int]] = {}
    for index, row in enumerate(rows):
        stamp = _parse_ts(row.ts)
        if stamp is None or stamp >= cutoff:
            keep.add(index)
        per_id.setdefault(row.id, []).append(index)
    for indexes in per_id.values():
        keep.update(indexes[-per_test:])
    if len(keep) == len(rows):
        return 0
    kept = [rows[i] for i in sorted(keep)]
    tmp = path.with_name(path.name + ".prune")
    blob = "".join(json.dumps(r.to_dict(), separators=(",", ":"), sort_keys=True) + "\n"
                   for r in kept)
    tmp.write_text(blob, encoding="utf-8")
    os.replace(str(tmp), str(path))
    return len(rows) - len(kept)


# --------------------------------------------------------------------------- ingest

def _junit_result(case: ET.Element) -> tuple[str, str, str]:
    """`(result, message, excerpt)` for one `<testcase>`, from its children then its `status`."""
    for tag, result in (("failure", "fail"), ("error", "error"), ("skipped", "skip")):
        child = case.find(tag)
        if child is not None:
            message = (child.get("message") or child.get("type") or "").strip()
            excerpt = (child.text or "").strip()[:MAX_EXCERPT]
            return result, message[:500], excerpt
    status = (case.get("status") or "").strip().lower()
    if status in ("fail", "failed", "failure"):
        return "fail", "", ""
    if status in ("error",):
        return "error", "", ""
    if status in ("skip", "skipped", "notrun", "disabled"):
        return "skip", "", ""
    if status in ("timeout",):
        return "timeout", "", ""
    return "pass", "", ""


COLLECTION_PREFIX = "collection:unittest:"
SCOPE_EXECUTION = "scope execution"
LEGACY_COLLECTION_PREFIX = "unittest:unittest.loader._FailedTest."


def collection_scope(key: str) -> str:
    """Runnable scope, including the old unittest loader wrapper's stored identity."""
    if key.startswith(COLLECTION_PREFIX):
        return key[len(COLLECTION_PREFIX):]
    if key.startswith(LEGACY_COLLECTION_PREFIX):
        scope = key[len(LEGACY_COLLECTION_PREFIX):]
        return "tests." + scope if "." not in scope else scope
    return ""


def deduplicate_collection(rows: Sequence[Execution]) -> list[Execution]:
    """One synthetic observation per scope/run; leave real repeated tests intact."""
    out: list[Execution] = []
    seen: dict[tuple[str, str], int] = {}
    for row in rows:
        key = (row.run_id, row.id)
        if collection_scope(row.id) and key in seen:
            if row.result in BAD_RESULTS:
                out[seen[key]] = row
            continue
        if collection_scope(row.id):
            seen[key] = len(out)
        out.append(row)
    return out


def _junit_id(runner: str, case: ET.Element) -> str:
    """The wire id for a `<testcase>`.

    CTest writes `classname` equal to the test name, so a ctest case is `ctest:<name>`; every
    other producer (including `relay_core.junit_runner`) writes the dotted module and class, so
    the id is `<runner>:<classname>.<name>`.
    """
    if runner == P.RUNNER_UNITTEST and case.get("collection_scope"):
        return COLLECTION_PREFIX + case.get("collection_scope")
    name = (case.get("name") or "").strip()
    classname = (case.get("classname") or "").strip()
    if runner == P.RUNNER_CTEST or not classname or classname == name:
        return f"{runner}:{name}"
    return f"{runner}:{classname}.{name}"


def ingest_junit(path_or_bytes, runner: str = P.RUNNER_UNITTEST, commit: str = "",
                 run_id: str = "", host: str | None = None, *,
                 ts: str | None = None,
                 source_hashes: dict[str, str] | None = None,
                 tree_digest: str = "") -> list[Execution]:
    """JUnit XML → executions, for both `ctest --output-junit` and `relay_core.junit_runner`.

    Takes a path, bytes or a string of XML.  Both document shapes are accepted: a `<testsuites>`
    root and CTest's bare `<testsuite>` root.  Malformed XML is an empty list, never an
    exception — an unreadable report must not take a pane down.  `source_hashes` maps a test id to
    its source hash at the moment of the run, which is what lets a later fold reset a test's
    history when it is edited; pass `test_probe`'s discovery for it.  `tree_digest` names the
    working tree the run happened on when it was not `commit`'s (`tree_digest_of()`); every
    execution of one report carries the same one, because one report is one run.
    """
    if isinstance(path_or_bytes, (bytes, bytearray)):
        data = bytes(path_or_bytes)
    elif isinstance(path_or_bytes, str) and path_or_bytes.lstrip()[:1] == "<":
        data = path_or_bytes.encode("utf-8")
    else:
        try:
            data = Path(path_or_bytes).read_bytes()
        except OSError:
            return []
    try:
        root = ET.fromstring(data.decode("utf-8", "replace"))
    except ET.ParseError:
        return []
    suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
    stamp = ts or now_iso()
    machine = host if host is not None else _hostname()
    hashes = source_hashes or {}
    out: list[Execution] = []
    for suite in suites:
        for case in suite.findall("testcase"):
            name = (case.get("name") or "").strip()
            if not name:
                continue
            result, message, excerpt = _junit_result(case)
            try:
                duration = float(case.get("time") or 0.0)
            except (TypeError, ValueError):
                duration = 0.0
            test_id = _junit_id(runner, case)
            out.append(Execution(ts=stamp, id=test_id, result=result, duration=duration,
                                 runner=runner, run_id=run_id, commit=commit, host=machine,
                                 message=message, excerpt=excerpt,
                                 source_hash=hashes.get(test_id, ""),
                                 tree_digest=str(tree_digest or "")))
        if runner == P.RUNNER_UNITTEST:
            for collection in suite.findall("collection"):
                scope = collection.get("scope", "")
                if scope and collection.get("result") in ("pass", "fail", "skip"):
                    out.append(Execution(ts=stamp, id=COLLECTION_PREFIX + scope,
                                         runner=runner, run_id=run_id, commit=commit,
                                         result=collection.get("result"), message=SCOPE_EXECUTION,
                                         host=machine, tree_digest=tree_digest))
    return deduplicate_collection(out)


def ingest_ctest_cost(path: str | os.PathLike) -> dict[str, dict]:
    """`build/Testing/Temporary/CTestCostData.txt` → `{id: {"runs": n, "mean": seconds}}`.

    **A weak hint only, never an execution.**  The file is `<name> <runs> <mean-seconds>` per
    line up to a `---` separator: it has no timestamps, no outcomes and no commits, it is wiped by
    a clean build, and it counts runs from the beginning of that build directory's life.  It is
    worth reading for exactly one thing — a test with no entry here has never run in this build
    tree — and the returned means are only shown when the store has nothing better.
    """
    try:
        text = Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    out: dict[str, dict] = {}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("---"):
            break                                        # the failed-test list follows
        if not line:
            continue
        parts = line.split()
        if len(parts) != 3:
            continue
        name, runs, mean = parts
        try:
            out[f"{P.RUNNER_CTEST}:{name}"] = {"runs": int(runs), "mean": float(mean)}
        except ValueError:
            continue
    return out


def _hostname() -> str:
    try:
        return socket.gethostname()
    except OSError:                                      # pragma: no cover - nameless host
        return ""


def tree_digest_of(repo: str | os.PathLike) -> str:
    """`""` when the working tree is `HEAD`, else a short digest of `git diff HEAD` (#AQ6X step 1).

    Several sessions edit this one checkout, so an execution's `commit` names what last landed
    rather than the code that ran: "the same commit" is not the same tree, and the flake rule
    that Datadog states over commits (`_same_commit_flake`) needs the tree to be honest about a
    pass and a fail.  The digest is over `git diff HEAD` — the unstaged *and* staged text, so an
    edit that is merely `git add`ed still counts — which makes it stable for as long as nobody
    types: two runs of one broken edit digest alike, and the next keystroke gives a new digest.

    Only `--no-ext-diff --no-color` and the repo's own path go on the command line, and every
    failure (no git, not a repository, a timeout, a binary blob it cannot spell) is the empty
    string: an execution with no digest says "not known", which is what the store held before
    this field existed and what every remote producer will keep saying.
    """
    import subprocess
    try:
        done = subprocess.run(["git", "-C", str(repo), "diff", "HEAD",
                               "--no-ext-diff", "--no-color"],
                              capture_output=True, timeout=TREE_DIGEST_TIMEOUT)
    except (OSError, subprocess.SubprocessError):
        return ""
    if done.returncode != 0 or not done.stdout.strip():
        return ""
    import hashlib
    return hashlib.sha256(done.stdout).hexdigest()[:TREE_DIGEST_CHARS]


# --------------------------------------------------------------------------- statistics

def percentile(values: Sequence[float], pct: float) -> float:
    """Nearest-rank percentile: the value at 1-based index `ceil(pct/100 × n)`, clamped.

    No interpolation on purpose — every number this returns is a duration that really happened,
    which is what makes "p95 is 30 s" answerable by pointing at a run.
    """
    ordered = sorted(float(v) for v in values)
    if not ordered:
        return 0.0
    rank = max(1, min(len(ordered), math.ceil(pct / 100.0 * len(ordered))))
    return ordered[rank - 1]


def flake_score(results: Sequence[str], half_life: float = FLAKE_HALF_LIFE) -> float:
    """TestGrid's recency-weighted count of pass↔fail transitions.

    `results` is oldest first; skips carry no signal and are dropped first.  With `n` remaining
    outcomes, the transition between `k` and `k+1` weighs `0.5 ** ((n - 2 - k) / half_life)`, so
    the newest transition counts 1.0, one five executions older counts 0.5, and a test that
    flipped once a year ago barely registers.  A steady test — all pass, or all fail — scores 0,
    which is the distinction between a *flaky* test and a *broken* one.
    """
    outcomes = [("fail" if r in BAD_RESULTS else "pass")
                for r in results if r in RESULTS and r != "skip"]
    n = len(outcomes)
    if n < 2:
        return 0.0
    total = 0.0
    for k in range(n - 1):
        if outcomes[k] != outcomes[k + 1]:
            total += 0.5 ** ((n - 2 - k) / max(0.5, half_life))
    return round(total, 3)


def _same_commit_flake(rows: Sequence[Execution]) -> bool:
    """Datadog: a pass **and** a fail for the same commit is a flake, whatever the score says."""
    seen: dict[str, set[str]] = {}
    for row in rows:
        if not row.commit or row.result == "skip":
            continue
        seen.setdefault(row.commit, set()).add(
            "fail" if row.result in BAD_RESULTS else "pass")
    return any({"pass", "fail"} <= kinds for kinds in seen.values())


def _slow_regression(durations: Sequence[float]) -> bool:
    """True when the recent p95 is more than `SLOW_REGRESSION` above the trailing median."""
    if len(durations) < SLOW_RECENT * 2:
        return False
    recent, before = durations[-SLOW_RECENT:], durations[:-SLOW_RECENT]
    trailing = percentile(before, 50.0)
    if trailing <= 0.0:
        return False
    return percentile(recent, 95.0) > trailing * (1.0 + SLOW_REGRESSION)


# --------------------------------------------------------------------------- the fold

def _discovered_dicts(discovered) -> list[dict]:
    """Accept `discover()`'s dict, its `tests` list, or `DiscoveredTest` objects."""
    if isinstance(discovered, dict):
        discovered = discovered.get("tests") or []
    out = []
    for item in discovered or []:
        if isinstance(item, P.DiscoveredTest):
            out.append(item.to_dict())
        elif isinstance(item, dict) and isinstance(item.get("id"), str):
            out.append(item)
    return out


def _cards_for(cards_index) -> dict[str, list[str]]:
    """`{card id: [test ids or `## Tests` lines]}` → `{test id: [card ids]}`."""
    out: dict[str, list[str]] = {}
    for card, lines in (cards_index or {}).items():
        for line in lines or []:
            text = str(line).strip()
            if any(text.startswith(r + ":") for r in P.RUNNERS) and "`" not in text:
                test_id = text                            # already an id
            else:
                test_id = (parse_test_line(text) or {}).get("id") or ""
            if not test_id:
                continue
            cards = out.setdefault(test_id, [])
            if card not in cards:
                cards.append(str(card))
    return out


def records(discovered, executions, cards_index: dict | None = None) -> list[dict]:
    """The full TestRecord list: what exists, folded together with what happened.

    `discovered` is `test_probe.discover()`'s result (or its `tests` list, or `DiscoveredTest`s);
    `executions` is what `read()` returned; `cards_index` maps a card id to the tests its
    `## Tests` section names, as ids or as raw lines, and becomes each record's `cards`.

    A record is produced for every discovered test **and** for every test id the store knows that
    discovery no longer finds — the latter carrying `stale: ["gone"]`, which is the whole point of
    keeping history for tests that have disappeared.

    History resets at a `source_hash` change: executions recorded under a hash other than the
    test's current one are excluded from every count, percentile and score, and while none has
    been recorded at the current hash the record is `stale: ["edited"]` — said once, until the
    test runs again.
    """
    tests = _discovered_dicts(discovered)
    rows = _as_executions(executions)
    by_id: dict[str, list[Execution]] = {}
    for row in rows:
        by_id.setdefault(row.id, []).append(row)
    for group in by_id.values():
        group.sort(key=lambda e: (e.ts, e.run_id))
    cards = _cards_for(cards_index)

    out: list[dict] = []
    for test in tests:
        out.append(_record(test, by_id.get(test["id"], []), cards.get(test["id"], [])))
    known = {t["id"] for t in tests}
    for test_id, group in sorted(by_id.items()):
        if test_id in known:
            continue
        runner = group[-1].runner or test_id.split(":", 1)[0]
        gone = {"id": test_id, "name": test_id.split(":", 1)[-1], "runner": runner,
                "file": "", "labels": [], "invocation": test_id.split(":", 1)[-1]}
        record = _record(gone, group, cards.get(test_id, []))
        if "gone" not in record["stale"]:
            record["stale"].insert(0, "gone")
        out.append(record)

    _mark_slow(out)
    return out


def _record(test: dict, group: Sequence[Execution], cards: Sequence[str]) -> dict:
    current_hash = str(test.get("source_hash") or "")
    counted = list(group)
    edited = False
    if current_hash:
        mismatched = [i for i, row in enumerate(group)
                      if row.source_hash and row.source_hash != current_hash]
        if mismatched:
            counted = list(group[mismatched[-1] + 1:])
            edited = not any(row.source_hash == current_hash for row in group)

    results = [row.result for row in counted]
    durations = [row.duration for row in counted
                 if row.result != "skip" and row.duration > 0.0]
    passes = sum(1 for r in results if r == "pass")
    fails = sum(1 for r in results if r in BAD_RESULTS)
    skips = sum(1 for r in results if r == "skip")

    record: dict = {
        "id": test["id"], "name": test.get("name") or test["id"].split(":", 1)[-1],
        "runner": test.get("runner") or test["id"].split(":", 1)[0],
        "file": test.get("file") or "", "labels": list(test.get("labels") or []),
        "runs": len(counted), "pass": passes, "fail": fails, "skip": skips,
        "flake_score": flake_score(results), "slow": False, "flaky": False,
        "stale": [], "cards": list(cards),
        "history": [row.to_dict() for row in reversed(counted[-MAX_HISTORY_CELLS:])],
        "last_result": "",
    }
    if test.get("line"):
        record["line"] = int(test["line"])
    if test.get("invocation"):
        record["invocation"] = test["invocation"]
    if current_hash:
        record["source_hash"] = current_hash
    if group:
        record["first_seen"] = group[0].ts
    if counted:
        record["last_run"] = counted[-1].ts
        record["last_result"] = counted[-1].result
        record["duration_last"] = round(counted[-1].duration, 6)
    if durations:
        record["duration_p50"] = round(percentile(durations, 50.0), 6)
        record["duration_p95"] = round(percentile(durations, 95.0), 6)
    if passes + fails:
        record["reliability"] = round(passes / (passes + fails) * 100.0, 2)

    last_failure = next((row for row in reversed(counted) if row.result in BAD_RESULTS), None)
    if last_failure is not None:
        record["last_failure"] = {"at": last_failure.ts, "commit": last_failure.commit,
                                  "message": last_failure.message,
                                  "excerpt": last_failure.excerpt}

    record["flaky"] = bool(record["flake_score"] >= FLAKE_FLAKY or _same_commit_flake(counted))
    record["_regressed"] = _slow_regression([row.duration for row in counted
                                             if row.result != "skip"])

    stale = record["stale"]
    if edited:
        stale.append("edited")
    if not counted:
        stale.append("never-run")
    if test.get("disabled") or (counted and skips == len(counted)):
        stale.append("skipped-forever")
    return record


def _mark_slow(out: Sequence[dict]) -> None:
    """`slow` needs the whole suite: the top decile is relative to the other tests' p95."""
    p95s = [r["duration_p95"] for r in out if r.get("duration_p95")]
    threshold = percentile(p95s, SLOW_DECILE) if p95s else 0.0
    for record in out:
        p95 = record.get("duration_p95") or 0.0
        decile = bool(p95 and p95 >= threshold and p95 >= SLOW_SECONDS)
        record["slow"] = bool(decile or record.pop("_regressed", False))
        record.pop("_regressed", None)


def format_seconds(seconds: float) -> str:
    """`"4 ms"`, `"1.4 s"`, `"2.1 m"` — never `"0.0 s"`, which reads as "did not run" rather
    than "was fast"; a whole suite is minutes and one test is often under a millisecond."""
    value = max(0.0, float(seconds))
    if value < 1.0:
        return f"{value * 1000:.0f} ms"
    if value < 120.0:
        return f"{value:.1f} s"
    return f"{value / 60.0:.1f} m"


def summary(records_list: Sequence[dict]) -> dict:
    """The suite header: counts plus nextest's ready-made `line`.

    `passed`/`failed`/`skipped` count a test by its **last** result, not by its executions, because
    the header answers "what is red right now"; `never_run` is the verification backlog.
    """
    total = len(records_list)
    passed = sum(1 for r in records_list if r.get("last_result") == "pass")
    failed = sum(1 for r in records_list if r.get("last_result") in BAD_RESULTS)
    skipped = sum(1 for r in records_list if r.get("last_result") == "skip")
    never = sum(1 for r in records_list if not r.get("last_run"))
    slow = sum(1 for r in records_list if r.get("slow"))
    flaky = sum(1 for r in records_list if r.get("flaky"))
    duration = round(sum(float(r.get("duration_last") or 0.0) for r in records_list), 3)

    marks = [f"{slow} slow"] if slow else []
    if flaky:
        marks.append(f"{flaky} flaky")
    parts = [f"{total} test{'' if total == 1 else 's'}",
             f"{passed} passed" + (f" ({', '.join(marks)})" if marks else "")]
    if failed:
        parts.append(f"{failed} failed")
    if skipped:
        parts.append(f"{skipped} skipped")
    if never:
        parts.append(f"{never} never run")
    parts.append(format_seconds(duration))
    return {"total": total, "passed": passed, "failed": failed, "skipped": skipped,
            "never_run": never, "slow": slow, "flaky": flaky, "duration": duration,
            "line": " · ".join(parts)}


# --------------------------------------------------------------------------- a card's ## Tests

#: `` `ctest -R name` `` — the C++ half's invocation, with or without regex anchors.
_CTEST_RE = re.compile(r"ctest\s+(?:-R|--tests-regex)\s+(?P<name>\S+)")
#: `` `tests/test_x.py::Class::test_y` ``, or just the file, or the file and a class.
_PY_RE = re.compile(r"(?P<file>[\w./\-]+\.py)(?:::(?P<cls>\w+))?(?:::(?P<method>\w+))?")
#: `manual: docs/qa_evidence/…`
_MANUAL_RE = re.compile(r"manual\s*:\s*(?P<what>.+?)\s*$", re.IGNORECASE)


def parse_test_line(line: str) -> dict | None:
    """One `## Tests` line → `{raw, runner, id, invocation, file}`, or None if it names no test.

    The three spellings the card format defines, in Markdown list form, with the optional
    ` — path` tail:

        - `ctest -R panelayout` — tests/panelayout_test.cpp
        - `tests/test_board_chat.py::BoardChatTests::test_steer`
        - manual: docs/qa_evidence/2026-09-20-thing/

    Deliberately forgiving about the bullet, the backticks and the dash (`—`, `--`, `-`): the
    section is hand-edited, and a line a human clearly meant as a test must not vanish silently.
    A prose line that names no invocation returns None.
    """
    raw = (line or "").strip()
    body = re.sub(r"^[-*+]\s+", "", raw)
    body = re.sub(r"^\d+[.)]\s+", "", body)
    if not body:
        return None
    file_tail = ""
    split = re.split(r"\s+(?:—|–|--|-)\s+", body, maxsplit=1)
    head = split[0].strip()
    if len(split) > 1:
        file_tail = split[1].strip().strip("`").strip()
    head = head.strip().strip("`").strip()

    manual = _MANUAL_RE.match(head)
    if manual:
        what = manual.group("what").strip().strip("`")
        return {"raw": raw, "runner": P.RUNNER_MANUAL, "id": f"{P.RUNNER_MANUAL}:{what}",
                "invocation": what, "file": file_tail or what}

    ctest = _CTEST_RE.search(head)
    if ctest:
        name = ctest.group("name").strip("`'\"").strip("^$")
        return {"raw": raw, "runner": P.RUNNER_CTEST, "id": f"{P.RUNNER_CTEST}:{name}",
                "invocation": f"ctest -R {name}", "file": file_tail}

    python = _PY_RE.fullmatch(head)
    if python:
        path = python.group("file")
        module = path[:-3].replace("/", ".")
        parts = [p for p in (python.group("cls"), python.group("method")) if p]
        test_id = f"{P.RUNNER_UNITTEST}:{'.'.join([module] + parts)}"
        return {"raw": raw, "runner": P.RUNNER_UNITTEST, "id": test_id,
                "invocation": head, "file": path}
    return None


def _norm_stem(path: str) -> str:
    """`tests/test_board_chat.py` → `boardchat`; `src/BoardPane.cpp` → `boardpane`.

    The convention key: drop the directory and the extension, drop a `test_` prefix or a `_test`
    suffix, then drop the punctuation and the case.  Two files whose keys match are *about* the
    same thing by this project's own naming, which is as far as a no-coverage tool can honestly go.
    """
    stem = Path(path).stem
    for prefix in ("test_", "tests_"):
        if stem.startswith(prefix):
            stem = stem[len(prefix):]
    for suffix in ("_test", "_tests", "Test", "Tests"):
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
    return re.sub(r"[^a-z0-9]", "", stem.lower())


def _covers(test_file: str, changed: str) -> bool:
    """Convention-based, never coverage: same directory, or matching normalised stems."""
    if not test_file or not changed:
        return False
    if Path(test_file).parent == Path(changed).parent:
        return True
    a, b = _norm_stem(test_file), _norm_stem(changed)
    if not a or not b:
        return False
    return a == b or (len(a) >= 4 and len(b) >= 4 and (a.startswith(b) or b.startswith(a)))


_GROUP_PHRASES = {
    "retired": "are not in the project any more",
    "never-run": "never ran here",
    "skipped-forever": "are skipped for good",
    "edited": "were edited since their history began, so it starts over",
    "flaky": "are flaky",
    "slow": "are slow",
    "failing": "failed the last time they ran",
}


def _grouped_findings(entry: dict, found: Sequence[dict]) -> list[dict]:
    """One finding per verdict for a line that names many tests, not one per test.

    `tests/test_board.py` is 92 cases; a card that lists the file would otherwise get 92 lines of
    "has never run here", which no one reads.  So a line that resolves to more than one test
    reports each verdict once — "tests/test_board.py: 92 of 92 never ran here" — naming the first
    few tests, with the worst severity any of them carried.  A line that names one test keeps
    the per-test wording.
    """
    by_verdict: dict[str, list[dict]] = {}
    severity: dict[str, str] = {}
    rank = {"failure": 2, "warning": 1, "notice": 0}
    for record in found:
        for finding in _test_findings(entry, record):
            by_verdict.setdefault(finding["verdict"], []).append(finding)
            if rank.get(finding["severity"], 0) >= rank.get(severity.get(finding["verdict"], "notice"), 0):
                severity[finding["verdict"]] = finding["severity"]
    out: list[dict] = []
    for verdict, group in by_verdict.items():
        names = [f["test"].split(":", 1)[-1].rsplit(".", 1)[-1] for f in group[:3]]
        shown = ", ".join(names) + ("…" if len(group) > 3 else "")
        phrase = _GROUP_PHRASES.get(verdict, verdict)
        out.append(_finding(entry["id"], verdict,
                            f"{entry['invocation']}: {len(group)} of {len(found)} {phrase} ({shown})",
                            severity.get(verdict, "warning")))
    return out


# --------------------------------------------------------------------------- the four statuses

def same_commit(one: str, other: str) -> bool:
    """Two commit spellings naming one commit: either is a prefix of the other.

    A card's `links.commits` holds 12 characters, an execution holds whatever `git rev-parse`
    printed, and a staged fixture holds 8.  Both must be at least `COMMIT_PREFIX_MIN` long, so
    an empty commit never matches anything.
    """
    a = (one or "").strip().lower()
    b = (other or "").strip().lower()
    if len(a) < COMMIT_PREFIX_MIN or len(b) < COMMIT_PREFIX_MIN:
        return False
    return a.startswith(b) or b.startswith(a)


def _field(row, key: str, default=""):
    """One field of an execution, whether it arrived as an `Execution` or as its dict."""
    if isinstance(row, Execution):
        return getattr(row, key, default)
    if isinstance(row, dict):
        value = row.get(key, default)
        return default if value is None else value
    return default


def applicable(row, *, commits: Sequence[str] = (), since: str = "", source_hash: str = "",
               accepted: Sequence[str] = ()) -> bool:
    """Does this execution say anything about **this card's revision**?

    The rule, in one sentence (card #PR4Q; Codex's review §C: "'passed last time' is not proof
    about this change", and "'never run here' is not 'never run'"):

        an execution is evidence for a card when it ran at one of the card's own commits, or
        when it is newer than the card's newest commit and was recorded under the test's
        current `source_hash` — plus any run the card has explicitly accepted.

    So a green run from another *host* counts (the store is host-agnostic: a run fetched from
    sphinxpad by `scripts/relay-remote-tests` is an execution like any other), while a green run
    from before the card's work does not.  A card with no commits at all has no revision to
    compare against: then every execution recorded under the test's current source hash counts,
    which is the answer this module gave before statuses existed.

    `accepted` is the set of run ids the card's `### Check` status records as accepted for this
    revision ("Use this existing result"), and those are evidence whatever their commit.
    """
    run_id = str(_field(row, "run_id") or "")
    if run_id and run_id in set(accepted or ()):
        return True
    row_hash = str(_field(row, "source_hash") or "")
    if source_hash and row_hash and row_hash != source_hash:
        return False                                  # a run of a different version of the test
    commit = str(_field(row, "commit") or "")
    if any(same_commit(commit, str(one or "")) for one in commits or ()):
        return True
    if not commits and not since:
        return True                                   # no revision to compare against
    base = _parse_ts(since)
    stamp = _parse_ts(str(_field(row, "ts") or ""))
    return bool(base is not None and stamp is not None and stamp >= base)


def _evidence(row, ok: bool) -> dict:
    """One execution as the `tests_check` event carries it, with the fold's own verdict on it."""
    return {"run_id": str(_field(row, "run_id") or ""), "host": str(_field(row, "host") or ""),
            "commit": str(_field(row, "commit") or ""), "ts": str(_field(row, "ts") or ""),
            "result": str(_field(row, "result") or ""), "applicable": bool(ok)}


def _record_status(record: dict, *, commits, since, accepted) -> tuple[str, list[dict]]:
    """One discovered test's status, and the executions that decided it (newest first)."""
    if "gone" in (record.get("stale") or []):
        return STATUS_NA, []
    source_hash = str(record.get("source_hash") or "")
    rows = list(record.get("history") or [])           # newest first, already source-hash-clean
    fit = [r for r in rows if applicable(r, commits=commits, since=since,
                                         source_hash=source_hash, accepted=accepted)]
    decided = [r for r in fit if str(_field(r, "result") or "") != "skip"]
    evidence = [_evidence(r, True) for r in fit[:MAX_EVIDENCE]]
    if not evidence and rows:
        evidence = [_evidence(rows[0], False)]         # what there is, marked as not evidence
    if not decided:
        return STATUS_MISSING, evidence
    result = str(_field(decided[0], "result") or "")
    return (STATUS_FAILED if result in BAD_RESULTS else STATUS_PASSED), evidence


def line_status(entry: dict, found: Sequence[dict], *, commits: Sequence[str] = (),
                since: str = "", accepted: Sequence[str] = (), host: str = "") -> dict:
    """One `## Tests` line's status: `{test, invocation, status, retired, evidence, …}`.

    A line may name several tests (`tests/test_board.py` is every case in the file), and the line
    is only as proven as its worst test: one applicable failure makes the line `failed`, one test
    with no applicable evidence makes it `missing-evidence`, and otherwise it `passed`.

    `use_existing` is the "Use this existing result" affordance (#PR4Q): true when the newest
    execution for this line is a pass from **another** host — a CI or a second runner whose
    results `tests_list` folded in from `.private/tests/incoming/` — that this card has not
    accepted yet.  Accepting it is a decision, so it is offered rather than taken.
    """
    test_id = str(entry.get("id") or "")
    invocation = str(entry.get("invocation") or test_id.split(":", 1)[-1])
    out = {"test": test_id, "invocation": invocation, "status": STATUS_NA, "retired": False,
           "evidence": [], "use_existing": False, "accepted": False, "message": ""}
    if entry.get("runner") == P.RUNNER_MANUAL:
        out["message"] = (f"manual evidence, recorded by hand: "
                          f"{entry.get('file') or invocation}")
        return out
    if not found or all("gone" in (r.get("stale") or []) for r in found):
        out["retired"] = True
        out["message"] = f"{invocation} is not in the project any more"
        return out

    statuses: list[str] = []
    evidence: list[dict] = []
    for record in found:
        status, rows = _record_status(record, commits=commits, since=since, accepted=accepted)
        statuses.append(status)
        evidence.extend(rows)
    evidence.sort(key=lambda row: (row["applicable"], row["ts"]), reverse=True)
    evidence = evidence[:MAX_EVIDENCE]
    if STATUS_FAILED in statuses:
        out["status"] = STATUS_FAILED
    elif STATUS_MISSING in statuses:
        out["status"] = STATUS_MISSING
    elif STATUS_PASSED in statuses:
        out["status"] = STATUS_PASSED
    else:
        out["retired"] = True
        out["message"] = f"{invocation} is not in the project any more"
        return out

    newest = evidence[0] if evidence else None
    out["evidence"] = evidence
    out["accepted"] = bool(newest and newest["run_id"] in set(accepted or ()))
    if out["status"] == STATUS_PASSED:
        where = f" on {newest['host']}" if newest and newest["host"] else ""
        when = f", {newest['ts']}" if newest and newest["ts"] else ""
        out["message"] = f"{invocation} passed for this revision{where}{when}"
    elif out["status"] == STATUS_FAILED:
        where = f" on {newest['host']}" if newest and newest["host"] else ""
        out["message"] = f"{invocation} failed for this revision{where}"
    else:
        out["message"] = (f"no run of {invocation} for this revision, from any host, and no "
                          f"attached result")
    if (newest is not None and newest["result"] == "pass" and newest["host"]
            and newest["host"] != host and not out["accepted"]
            and out["status"] != STATUS_FAILED):
        out["use_existing"] = True
    return out


def card_statuses(test_lines: Sequence[str], records_list: Sequence[dict], *,
                  commits: Sequence[str] = (), since: str = "", accepted: Sequence[str] = (),
                  host: str = "") -> list[dict]:
    """`line_status()` for every line of a card's `## Tests`, in the order the card lists them."""
    out = []
    for line in test_lines or []:
        entry = parse_test_line(line)
        if entry:
            out.append(line_status(entry, resolve(entry, records_list), commits=commits,
                                   since=since, accepted=accepted, host=host))
    return out


def check_card(test_lines: Sequence[str], card_commit_files: Sequence[str],
               records_list: Sequence[dict], *, commits: Sequence[str] = (), since: str = "",
               accepted: Sequence[str] = (), host: str = "") -> dict:
    """Check one card's `## Tests` section against discovery and history.

    `test_lines` are the section's raw lines, `card_commit_files` the repo-relative files this
    card's commits touched, `records_list` what `records()` returned; `commits`, `since`,
    `accepted` and `host` are the card's revision, as `applicable()` above defines it.  The
    result is

        {"statuses": [{test, invocation, status, evidence, retired, use_existing, …}],
         "findings": [{"test", "verdict", "message", "severity"}], "actions": [str]}

    **`statuses` is the answer** (card #PR4Q): one of `passed`, `failed`, `missing-evidence` or
    `not-applicable` per listed test, decided by the executions that apply to this revision from
    *any* host.  `findings` are what is additionally worth reading — the older verdicts, now
    advisory notices — and stay **empty** when nothing moved, because a report nobody must read
    is what every CI product ends up ignoring.  Severities are GitHub's Checks vocabulary
    (`failure` / `warning` / `notice`) and there are at most three actions, which is that API's
    own cap.

    The verdicts, in the order they are emitted per test: **retired** (listed, absent from
    discovery), **never-run** (no execution in the store at all), **skipped-forever** (disabled,
    or skipped in every stored run), **edited** (source changed, history reset), **flaky**,
    **slow**, and **failing** (added by `tests_protocol`).  One card-level **orphaned** finding
    is added when the card changed files and *no* listed test's source shares a directory or a
    naming convention with any of them — convention-based by design: there is no coverage data
    here, and a guess dressed as coverage would be worse than an honest heuristic.

    A line need not name exactly one test: `resolve()` below takes `tests/test_board.py` as every
    case in that file and `ctest -R board` as the regex ctest would, so a section written the way
    a person runs things is checked the way a person runs it.
    """
    findings: list[dict] = []
    listed: list[dict] = []
    for line in test_lines or []:
        parsed = parse_test_line(line)
        if parsed:
            listed.append(parsed)

    matched: dict[str, list[dict]] = {}
    for entry in listed:
        matched[entry["id"]] = resolve(entry, records_list)
    statuses = [line_status(entry, matched.get(entry["id"]) or [], commits=commits, since=since,
                            accepted=accepted, host=host) for entry in listed]

    for entry in listed:
        test_id = entry["id"]
        if entry["runner"] == P.RUNNER_MANUAL:
            listed_file = entry.get("file") or ""
            if listed_file and not Path(listed_file).exists():
                findings.append(_finding(test_id, "gone",
                                         f"manual evidence {listed_file} is not there",
                                         "warning"))
            continue
        found = matched.get(test_id) or []
        if not found:
            findings.append(_finding(test_id, "retired",
                                     f"{entry['invocation']} is not in the project any more",
                                     "notice"))
            continue
        if len(found) == 1:
            findings.extend(_test_findings(entry, found[0]))
        else:
            findings.extend(_grouped_findings(entry, found))

    changed = [f for f in (card_commit_files or []) if f]
    if changed and listed:
        files = []
        for entry in listed:
            files.append(entry.get("file") or "")
            files.extend(r.get("file") or "" for r in matched.get(entry["id"]) or [])
        uncovered = [c for c in changed if not any(_covers(f, c) for f in files)]
        if len(uncovered) == len(changed):
            shown = ", ".join(uncovered[:3]) + ("…" if len(uncovered) > 3 else "")
            findings.append(_finding("", "orphaned",
                                     f"none of the listed tests is named after anything this "
                                     f"card changed ({shown})", "warning"))
    elif changed and not listed:
        findings.append(_finding("", "orphaned",
                                 f"this card changed {len(changed)} file(s) and lists no tests",
                                 "warning"))

    verdicts = {f["verdict"] for f in findings}
    kinds = {row["status"] for row in statuses}
    actions: list[str] = []
    # The three Codex's review names — "Run", "Use this existing result", "Replace retired
    # check" — minus the middle one, which needs a run id and so rides on the status row itself
    # rather than on a button about the whole card.
    if kinds & {STATUS_FAILED, STATUS_MISSING} or "edited" in verdicts:
        actions.append("Run these")
    if any(row["retired"] for row in statuses):
        actions.append("Replace retired check")
    if STATUS_FAILED in kinds or any(r.get("last_result") in BAD_RESULTS
                                     for group in matched.values() for r in group):
        actions.append("Open the failing one")
    if "orphaned" in verdicts:
        actions.append("Add the tests this card's commits touched")
    return {"statuses": statuses, "findings": findings, "actions": actions[:3]}


def resolve(entry: dict, records_list: Sequence[dict]) -> list[dict]:
    """Every record one `## Tests` line names: exactly one, or several.

    An exact id wins outright.  Otherwise the line is an *invocation*, and an invocation can name
    more than one test: `tests/test_board.py` (or `…::CardTests`) is every case under that prefix,
    and `ctest -R board` is a **regex**, which is what ctest itself does with it — so the same
    line selects the same tests here as at the terminal.  An unparsable regex matches nothing,
    which reads as `gone` and is the honest answer for a line nobody can run.
    """
    test_id = entry.get("id") or ""
    exact = [r for r in records_list if r.get("id") == test_id]
    if exact:
        return exact
    runner = entry.get("runner") or test_id.split(":", 1)[0]
    if runner == P.RUNNER_UNITTEST:
        prefix = test_id + "."
        return [r for r in records_list if str(r.get("id", "")).startswith(prefix)]
    if runner == P.RUNNER_CTEST:
        pattern = test_id.split(":", 1)[-1]
        try:
            regex = re.compile(pattern)
        except re.error:
            return []
        return [r for r in records_list
                if r.get("runner") == P.RUNNER_CTEST and regex.search(str(r.get("name", "")))]
    return []


def _test_findings(entry: dict, record: dict) -> list[dict]:
    """The verdicts for one listed test against one record, in report order."""
    test_id = str(record.get("id") or entry.get("id") or "")
    shown = record.get("invocation") or entry.get("invocation") or record.get("name") or test_id
    stale = record.get("stale") or []
    out: list[dict] = []
    if "gone" in stale:
        return [_finding(test_id, "retired",
                         f"{shown} has history but is no longer collected", "notice")]
    if not record.get("last_run"):
        out.append(_finding(test_id, "never-run", f"{shown} has never run here", "notice"))
    if "skipped-forever" in stale:
        out.append(_finding(test_id, "skipped-forever",
                            f"{shown} is skipped in every run", "notice"))
    if "edited" in stale:
        out.append(_finding(test_id, "edited",
                            f"{shown} was edited; its history was reset", "notice"))
    if record.get("flaky"):
        out.append(_finding(test_id, "flaky",
                            f"{shown} flips between pass and fail "
                            f"(flake score {record.get('flake_score', 0)}, "
                            f"reliability {record.get('reliability', 0)}%)", "notice"))
    if record.get("slow"):
        out.append(_finding(test_id, "slow",
                            f"{shown} is slow: p95 {record.get('duration_p95', 0):.2f} s, "
                            f"p50 {record.get('duration_p50', 0):.2f} s", "notice"))
    return out


def _finding(test: str, verdict: str, message: str, severity: str) -> dict:
    return {"test": test, "verdict": verdict, "message": message, "severity": severity}
