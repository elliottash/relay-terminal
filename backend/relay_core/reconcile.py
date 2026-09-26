# SPDX-License-Identifier: AGPL-3.0-or-later
"""Automatic AI reconciliation of landing conflicts (card #P9ZA, A4 of docs/TREES-AND-LANDING.md).

The landing queue (A2, `relay_core.landq`) prepares a *candidate*: the submitted commit merged onto
the current target in a disposable worktree. When that merge conflicts, the queue hands this
module a context and gets back either a resolved candidate or the conflict returned to the
**author agent** — never a question for the owner (owner, 2026-09-26: "no, thats going to be AI,
i cant confirm everything"). The queue owns commit creation and publication; whatever this module
writes goes into `candidate_path` and nowhere else, and any resolved candidate still goes through
the project's required gate.

What it does, in order, for one context:

1. **Policy.** `[reconcile]` of the project config (`enabled`, `max_attempts`, `tokens_per_case`,
   `tokens_per_day`), clamped to the contract's ceilings (two attempts, ever).
2. **Draw a model** from the configured *high* list, through the routing that already exists:
   `roles.RoleResolver.choose_role("high")`, which is `roles.ordered_candidates(choose=True)` —
   ranks first, then a draw weighted by each subscription's remaining quota, with a guest family
   row standing for each of its signed-in accounts. The list comes from the context's policy when
   the queue passes one, else the GUI's stored `high` list, else `presets.tier_list_defaults`
   from what can take a turn on this machine. No vendor model id is written here. Every entry
   runs at its own level or the model's top one.
3. **Reserve tokens** durably in `state_root/integration/reconcile.sqlite3` before the call: the
   estimated prompt plus the completion cap, charged against the case and against the UTC day. A
   reservation the process never settled (a crash) keeps counting at its reserved size.
4. **Call** the model: an API entry through `provider.make_provider` with no tools; a guest entry
   (Claude Code, Codex, an account of either) through `guest_harness_provider.start_provider` in
   read-only mode with the candidate as its cwd. The reply is JSON: full contents for each
   conflicted file, or a `give_up`.
5. **Review** conservatively (`review_patch`): only the allowed paths, no conflict markers,
   every assertion-bearing line of either side kept verbatim, no test function lost, no skip or
   early return added to a test (Python bodies are compared as syntax trees), Python, JSON and
   TOML files still parse, neither side's added lines and no line both sides kept dropped beyond
   a small tolerance, nothing that reads like an elided file. This is a screen, not a proof of
   semantics: the project's required gate still runs on every resolved candidate. A refusal is
   cheap and routes to the author agent with the diagnostics and the rejected patch.
6. **Apply** the accepted files into the candidate, stage them, and return the unified patch, the
   `Reconciled-By:` trailer (the queue writes `Reconciled-From: <target> <submitted>` itself;
   `reconciled_from` carries that line for an integrator that does not) and the card notes.

Two shapes of context arrive from `landq` (A2). A **merge conflict** names `conflicts` and a
candidate holding git's markers. A **gate failure** (`diagnostics.kind == "gate_failure"`) names
no conflicts: the candidate is the merged commit the gate refused, the diagnostics carry the
gate's reason and log, and the files the model may touch are the ones the submission changed
(`base..submitted`) plus any `repair_paths` the queue names. In that mode the submission's own
lines may change — repairing them is the point — while the target's lines, every test and every
assertion line are held as before.

Bounds on one attempt: the prompt is capped (`context_tokens`), an API completion is capped by
`max_tokens` (`completion_tokens`), and a guest turn — which has no request-body cap — is
**cancelled** when its harness reports usage past the attempt's reservation, when its streamed
answer runs past the completion cap, or when `guest_timeout_seconds` pass. Both guest adapters
poll the cancel event every 0.2 s. That is a bound at report granularity, not a hard cap: Claude
Code reports usage per result message, Codex per token-usage update, so a turn can overshoot by
what it consumed since its last report. A cancelled turn is charged at the larger of its
reservation and the last usage it reported. Attempts persist per case in the ledger: a restart
that re-runs the same job does not get two more.

Tests inject `model_call`, `tiers`, `key_lookup` and `guest_check`; nothing here starts a real
guest or reads a real keyring under test (`tests/test_reconcile.py`).
"""
from __future__ import annotations

import ast
import asyncio
import configparser
import dataclasses
import datetime as _dt
import difflib
import hashlib
import math
import os
import re
import socket
import sqlite3
import tomllib
import subprocess
import threading
import time
from pathlib import Path

from . import logs, presets
from . import roles as model_roles
from .provider import (DEFAULT_STALL_TIMEOUT, MIN_OUTPUT_TOKENS, Cancelled, ProviderConfig,
                       ProviderError, cache_counts)

_log = logs.get("reconcile")

# ----- policy ---------------------------------------------------------------------------------

# The contract's ceilings (docs/TREES-AND-LANDING.md, A4): at most two attempts, whatever a
# project's config asks for, and the defaults its `[reconcile]` table shows.
HARD_MAX_ATTEMPTS = 2
DEFAULT_POLICY = {"enabled": True, "max_attempts": 2, "tokens_per_case": 200_000,
                  "tokens_per_day": 10_000_000}
# Prompt and completion caps, in tokens. `context_tokens` bounds what one call may be handed;
# `completion_tokens` what it may answer with (API calls; a guest decides its own reply length and
# is settled after the fact). Both are further clamped so one attempt fits the case budget.
DEFAULT_CONTEXT_TOKENS = 100_000
DEFAULT_COMPLETION_TOKENS = 32_768
CHARS_PER_TOKEN = 4
# A reservation left open longer than this by a process that is gone is *uncertain*: it stays
# charged at its reserved size (conservative), and is labelled so `history()` can say why.
STALE_RESERVATION_S = 6 * 3600
# The largest single file this will ask a model to rewrite whole, in bytes.
MAX_FILE_BYTES = 400_000
DEFAULT_GUEST_TIMEOUT_S = 900
MAX_REPAIR_PATHS = 24
GATE_LOG_CHARS = 16_000
SCHEMA_VERSION = 2
TRAILER = "Reconciled-From"
BY_TRAILER = "Reconciled-By"

_MARKER = re.compile(r"^(<{7}|={7}|>{7}|\|{7})( |$)", re.M)
_ELISION = re.compile(r"^\s*(?:#|//|/\*|<!--)?\s*(?:\.\.\.|…)\s*(?:rest|remaining|unchanged|other|existing|"
                      r"more|omitted|same as|snip)\b|^\s*(?:#|//)\s*\.\.\.\s*$|"
                      r"\b(?:rest of (?:the )?file|remains? unchanged|unchanged (?:code|lines|below|above))\b",
                      re.I | re.M)
_TEST_PATH = re.compile(r"(^|/)(tests?|spec|__tests__)(/|$)|(^|/)test_[^/]*\.py$|_test\.[a-z]+$|"
                        r"Tests?\.(cpp|cc|mm|m|py|js|ts|tsx|rs|go|java|kt|swift)$|\.(spec|test)\.[a-z]+$")
_TEST_NAMES = [
    re.compile(r"^\s*(?:async\s+)?def\s+(test\w*)\s*\(", re.M),
    re.compile(r"^\s*TEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)", re.M),
    re.compile(r"^\s*(?:void\s+)?(test\w+)\s*\(\s*\)\s*(?:const\s*)?\{?\s*$", re.M),
    re.compile(r"^\s*(?:it|test)\s*\(\s*['\"`]([^'\"`]+)['\"`]", re.M),
    re.compile(r"^\s*#\[test\]\s*\n\s*fn\s+(\w+)", re.M),
    re.compile(r"^\s*func\s+(Test\w+)\s*\(", re.M),
]
_ASSERTIONS = re.compile(r"(?:^|[^\w.])assert\w*\b|self\.assert\w*|\bASSERT_\w+|\bEXPECT_\w+|\bQVERIFY\w*|"
                         r"\bQCOMPARE\w*|\bQTRY_\w+|\bexpect\s*\(|\bassertThat\b|\brequire\.|\bt\.Errorf|"
                         r"\bt\.Fatal\w*|\bREQUIRE(?:_\w+)?\s*\(|\bCHECK(?:_\w+)?\s*\(")
_PY_CHECK_NAMES = re.compile(r"^(?:assert\w*|raises|warns|fail\w*|expect\w*)$")
_SKIPS = re.compile(r"(?:unittest\.skip|pytest\.mark\.skip|pytest\.mark\.xfail|pytest\.skip\s*\(|"
                    r"unittest\.expectedFailure|\bDISABLED_\w+|\bQSKIP\s*\(|GTEST_SKIP|\bxit\s*\(|"
                    r"\bxdescribe\s*\(|\bit\.skip\s*\(|\btest\.skip\s*\(|\bdescribe\.skip\s*\(|"
                    r"#\[ignore\]|t\.Skip\w*\s*\()")
# Bypass shapes a line rule can still see in any language's test file: a guard opened above an
# assertion (`if (false) {`, `#if 0`), an early `return`, a block comment opened around it.
_GUARD_OPENERS = re.compile(r"^\s*(?:\}?\s*else\s+)?(?:if|elif|else if|while|for|switch|unless|match|case)\b|^\s*#\s*if", re.M)
_RETURNS = re.compile(r"^\s*return\b", re.M)
_BLOCK_COMMENTS = re.compile(r"/\*|^\s*(?:\"\"\"|\'\'\')", re.M)
_TRIVIAL_LINES = frozenset({"", "{", "}", "};", ")", ");", "]", "];", "end", "else", "else:", "pass",
                            "return", "return;", "break", "break;", "continue", "continue;", "fi",
                            "done", "esac", "#", "//", "/*", "*/", "*", "-", "---", "..."})


class ReconcileError(RuntimeError):
    """A refusal with a sentence in it; the queue records `str(exc)` as the job's reason."""


class BudgetExceeded(ReconcileError):
    """A case or day budget would be exceeded by the next call. Never leaves `reconcile()`: it
    becomes the `author_required` reason."""


def normalize_policy(raw) -> dict:
    """The `[reconcile]` table as this module reads it: the contract's defaults filled in, bad
    types refused with a sentence, and `max_attempts` never above HARD_MAX_ATTEMPTS."""
    if raw is None or isinstance(raw, str):
        # `landq` passes the job's accepted policy *hash* here (a string): the defaults apply and
        # the hash is kept for the record. B1 passes the config table to change the budgets.
        raw = {}
    if not isinstance(raw, dict):
        raise ReconcileError("policy must be the project config table, its [reconcile] section, or a policy hash.")
    source = raw.get("reconcile") if isinstance(raw.get("reconcile"), dict) else raw
    out = dict(DEFAULT_POLICY)
    enabled = source.get("enabled", True)
    if type(enabled) is not bool:
        raise ReconcileError("reconcile.enabled must be true or false.")
    out["enabled"] = enabled
    for key, low in (("max_attempts", 1), ("tokens_per_case", 1000), ("tokens_per_day", 1000)):
        value = source.get(key, DEFAULT_POLICY[key])
        if type(value) is not int or value < low:
            raise ReconcileError(f"reconcile.{key} must be an integer of at least {low}.")
        out[key] = value
    out["max_attempts"] = min(out["max_attempts"], HARD_MAX_ATTEMPTS)
    for key, default in (("context_tokens", DEFAULT_CONTEXT_TOKENS),
                         ("completion_tokens", DEFAULT_COMPLETION_TOKENS)):
        value = source.get(key, default)
        if type(value) is not int or value < MIN_OUTPUT_TOKENS:
            raise ReconcileError(f"reconcile.{key} must be an integer of at least {MIN_OUTPUT_TOKENS}.")
        out[key] = value
    # One attempt has to fit the case: the completion keeps at least a quarter of the case.
    per_case = out["tokens_per_case"]
    out["completion_tokens"] = max(MIN_OUTPUT_TOKENS, min(out["completion_tokens"], per_case // 4))
    out["context_tokens"] = max(MIN_OUTPUT_TOKENS, min(out["context_tokens"], per_case - out["completion_tokens"]))
    timeout = source.get("guest_timeout_seconds", DEFAULT_GUEST_TIMEOUT_S)
    if type(timeout) not in (int, float) or isinstance(timeout, bool) or timeout < 1:
        raise ReconcileError("reconcile.guest_timeout_seconds must be a positive number.")
    out["guest_timeout_seconds"] = float(timeout)
    return out


# ----- placement -------------------------------------------------------------------------------

def default_state_root() -> Path:
    """`$XDG_STATE_HOME/relay`, default `~/.local/state/relay` — the contract's `state_root`.
    `RELAY_STATE_HOME` wins when set, the way `scratch.state_root` reads it."""
    for name in ("RELAY_STATE_HOME", "XDG_STATE_HOME"):
        base = os.environ.get(name)
        if base:
            return Path(base).expanduser() / "relay"
    return Path.home() / ".local" / "state" / "relay"


def ledger_path(state_root=None) -> Path:
    root = Path(state_root).expanduser() if state_root is not None else default_state_root()
    return root / "integration" / "reconcile.sqlite3"


def _utc_day(now: float) -> str:
    return _dt.datetime.fromtimestamp(now, _dt.timezone.utc).strftime("%Y-%m-%d")


def _pid_alive(pid: int, host: str) -> bool:
    if host != socket.gethostname() or pid <= 0:
        return False                    # another machine's process: nothing here can ask it
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


class BudgetLedger:
    """Case and day token budgets in one SQLite file (WAL, busy timeout, short transactions).

    A *reservation* is written before a model call and charged at its reserved size; `settle`
    replaces that charge with what the call actually used; `release` drops a charge for a call
    that provably never went out. A row still `reserved` after its process is gone becomes
    `uncertain` at `sweep()`, and an uncertain row keeps its full reserved charge — the
    conservative reading the contract asks for after a crash.
    """

    def __init__(self, path, *, now=time.time):
        self.path = Path(path)
        self._now = now
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self._connect() as db:
            db.executescript(
                "CREATE TABLE IF NOT EXISTS schema(version INTEGER NOT NULL);"
                "CREATE TABLE IF NOT EXISTS reservations("
                " id INTEGER PRIMARY KEY, repo_id TEXT NOT NULL, job_id TEXT NOT NULL,"
                " case_key TEXT NOT NULL, attempt INTEGER NOT NULL, day TEXT NOT NULL,"
                " reserved INTEGER NOT NULL, input_tokens INTEGER NOT NULL DEFAULT 0,"
                " output_tokens INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL,"
                " preset TEXT NOT NULL DEFAULT '', model TEXT NOT NULL DEFAULT '',"
                " account TEXT NOT NULL DEFAULT '', effort TEXT NOT NULL DEFAULT '',"
                " outcome TEXT NOT NULL DEFAULT '', pid INTEGER NOT NULL DEFAULT 0,"
                " host TEXT NOT NULL DEFAULT '', created_at REAL NOT NULL, updated_at REAL NOT NULL);"
                "CREATE INDEX IF NOT EXISTS reservations_day ON reservations(day);"
                "CREATE INDEX IF NOT EXISTS reservations_case ON reservations(case_key);"
                "CREATE INDEX IF NOT EXISTS reservations_job ON reservations(job_id);")
            row = db.execute("SELECT version FROM schema").fetchone()
            if row is None:
                db.execute("INSERT INTO schema(version) VALUES (?)", (SCHEMA_VERSION,))
            elif row[0] > SCHEMA_VERSION:
                raise ReconcileError(f"{self.path} was written by a newer Relay (schema {row[0]}).")
            columns = {r[1] for r in db.execute("PRAGMA table_info(reservations)").fetchall()}
            if "cached_tokens" not in columns:            # schema 1 → 2: the prefix-cache counts
                db.execute("ALTER TABLE reservations ADD COLUMN cached_tokens INTEGER NOT NULL DEFAULT 0")
                db.execute("ALTER TABLE reservations ADD COLUMN cache_write_tokens INTEGER NOT NULL DEFAULT 0")
            db.execute("UPDATE schema SET version = ?", (SCHEMA_VERSION,))

    def _connect(self) -> sqlite3.Connection:
        db = sqlite3.connect(self.path, timeout=10.0, isolation_level=None)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA journal_mode=WAL")
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA busy_timeout=10000")
        return db

    # A row's charge: what it used once settled, what it reserved until then or for ever when the
    # process died holding it. Released rows charge nothing.
    _CHARGE = ("CASE status WHEN 'settled' THEN input_tokens + output_tokens"
               " WHEN 'released' THEN 0 ELSE reserved END")

    def charged(self, *, day: str | None = None, case_key: str | None = None,
                repo_id: str | None = None) -> int:
        clauses, args = [], []
        if day is not None:
            clauses.append("day = ?")
            args.append(day)
        if case_key is not None:
            clauses.append("case_key = ?")
            args.append(case_key)
        if repo_id is not None:
            clauses.append("repo_id = ?")
            args.append(repo_id)
        where = (" WHERE " + " AND ".join(clauses)) if clauses else ""
        with self._connect() as db:
            row = db.execute(f"SELECT COALESCE(SUM({self._CHARGE}), 0) FROM reservations{where}", args).fetchone()
        return int(row[0] or 0)

    def reserve(self, *, repo_id: str, job_id: str, case_key: str, attempt: int, tokens: int,
                per_case: int, per_day: int, preset: str = "", model: str = "", account: str = "",
                effort: str = "") -> int:
        """Charge `tokens` against the case and the day, or raise BudgetExceeded and charge
        nothing. One IMMEDIATE transaction, so two publishers cannot both fit under the line."""
        now = self._now()
        day = _utc_day(now)
        with self._connect() as db:
            db.execute("BEGIN IMMEDIATE")
            try:
                case_used = int(db.execute(f"SELECT COALESCE(SUM({self._CHARGE}), 0) FROM reservations"
                                           " WHERE case_key = ?", (case_key,)).fetchone()[0] or 0)
                day_used = int(db.execute(f"SELECT COALESCE(SUM({self._CHARGE}), 0) FROM reservations"
                                          " WHERE day = ?", (day,)).fetchone()[0] or 0)
                if case_used + tokens > per_case:
                    raise BudgetExceeded(f"case budget: {case_used:,} tokens charged and {tokens:,} more "
                                         f"needed exceed tokens_per_case {per_case:,}.")
                if day_used + tokens > per_day:
                    raise BudgetExceeded(f"day budget: {day_used:,} tokens charged today and {tokens:,} "
                                         f"more needed exceed tokens_per_day {per_day:,}.")
                cursor = db.execute(
                    "INSERT INTO reservations(repo_id, job_id, case_key, attempt, day, reserved, status,"
                    " preset, model, account, effort, pid, host, created_at, updated_at)"
                    " VALUES (?, ?, ?, ?, ?, ?, 'reserved', ?, ?, ?, ?, ?, ?, ?, ?)",
                    (repo_id, job_id, case_key, attempt, day, int(tokens), preset, model, account,
                     effort, os.getpid(), socket.gethostname(), now, now))
                db.execute("COMMIT")
            except BaseException:
                db.execute("ROLLBACK")
                raise
        return int(cursor.lastrowid)

    def settle(self, reservation_id: int, usage: dict | None, outcome: str, *, model: str = "") -> None:
        """Replace the reservation's charge with the call's usage. `usage` None or without any
        token count is a call whose consumption is unknown: the row becomes `uncertain` and keeps
        its reserved charge. `model`, when given, is what actually answered (a guest names its
        model only once started)."""
        counts = usage_counts(usage)
        cache = cache_counts(usage) if isinstance(usage, dict) else {}
        now = self._now()
        with self._connect() as db:
            if counts is None:
                db.execute("UPDATE reservations SET status = 'uncertain', outcome = ?, updated_at = ?"
                           " WHERE id = ?", (outcome, now, reservation_id))
            else:
                db.execute("UPDATE reservations SET status = 'settled', input_tokens = ?, output_tokens = ?,"
                           " cached_tokens = ?, cache_write_tokens = ?, outcome = ?, updated_at = ? WHERE id = ?",
                           (counts[0], counts[1], cache.get("cached_tokens", 0), cache.get("cache_write_tokens", 0),
                            outcome, now, reservation_id))
            if model:
                db.execute("UPDATE reservations SET model = ? WHERE id = ?", (model, reservation_id))

    def mark_uncertain(self, reservation_id: int, outcome: str, *, at_least: int = 0, model: str = "") -> None:
        """A call that was cut off (budget, timeout, error) with no final usage: the row stays
        charged at its reservation, raised to `at_least` when the last usage it reported was
        already past it — a cancelled turn may have consumed more than either."""
        with self._connect() as db:
            db.execute("UPDATE reservations SET status = 'uncertain', outcome = ?, updated_at = ?,"
                       " reserved = MAX(reserved, ?) WHERE id = ?",
                       (outcome, self._now(), int(at_least), reservation_id))
            if model:
                db.execute("UPDATE reservations SET model = ? WHERE id = ?", (model, reservation_id))

    def attempts_made(self, case_key: str) -> int:
        """How many attempts this case has on record, whatever became of them: a restart that
        runs the same job again continues the count instead of starting a fresh pair."""
        with self._connect() as db:
            return int(db.execute("SELECT COUNT(*) FROM reservations WHERE case_key = ?", (case_key,)).fetchone()[0])

    def release(self, reservation_id: int, outcome: str) -> None:
        """Drop the charge of a call that provably never reached a model (the guest would not
        start, the request never left)."""
        with self._connect() as db:
            db.execute("UPDATE reservations SET status = 'released', outcome = ?, updated_at = ?"
                       " WHERE id = ?", (outcome, self._now(), reservation_id))

    def sweep(self, *, max_age_s: float = STALE_RESERVATION_S) -> int:
        """Label reservations left `reserved` by a process that is gone, or older than
        `max_age_s`, as `uncertain`. Their charge does not change. Returns how many."""
        now = self._now()
        changed = 0
        with self._connect() as db:
            rows = db.execute("SELECT id, pid, host, created_at FROM reservations WHERE status = 'reserved'").fetchall()
            for row in rows:
                stale = now - float(row["created_at"]) > max_age_s
                if stale or not _pid_alive(int(row["pid"]), str(row["host"])):
                    db.execute("UPDATE reservations SET status = 'uncertain', outcome = ?, updated_at = ?"
                               " WHERE id = ? AND status = 'reserved'",
                               ("stale reservation" if stale else "process gone", now, row["id"]))
                    changed += 1
        return changed

    def rows(self, *, job_id: str | None = None, case_key: str | None = None) -> list[dict]:
        clauses, args = [], []
        if job_id is not None:
            clauses.append("job_id = ?")
            args.append(job_id)
        if case_key is not None:
            clauses.append("case_key = ?")
            args.append(case_key)
        where = (" WHERE " + " AND ".join(clauses)) if clauses else ""
        with self._connect() as db:
            rows = db.execute(f"SELECT * FROM reservations{where} ORDER BY id", args).fetchall()
        return [dict(row) for row in rows]

    def status(self, *, repo_id: str | None = None, now: float | None = None) -> dict:
        now = self._now() if now is None else now
        day = _utc_day(now)
        with self._connect() as db:
            open_rows = db.execute("SELECT COUNT(*) FROM reservations WHERE status IN ('reserved', 'uncertain')").fetchone()[0]
        return {"path": str(self.path), "day": day, "charged_today": self.charged(day=day, repo_id=repo_id),
                "open_reservations": int(open_rows)}


def usage_counts(usage) -> tuple[int, int] | None:
    """(input, output) from a provider's usage dict in either OpenAI's or Anthropic's names, or
    None when it carries no count at all."""
    if not isinstance(usage, dict):
        return None
    found = False
    counts = []
    for names in (("prompt_tokens", "input_tokens"), ("completion_tokens", "output_tokens")):
        value = 0
        for name in names:
            raw = usage.get(name)
            if isinstance(raw, int) and not isinstance(raw, bool) and raw >= 0:
                value, found = raw, True
                break
        counts.append(value)
    return (counts[0], counts[1]) if found else None


# ----- the high list ----------------------------------------------------------------------------

def _config_home() -> Path:
    return Path(os.environ.get("XDG_CONFIG_HOME") or (Path.home() / ".config")).expanduser()


def relay_settings_path(config_home=None) -> Path:
    """The GUI's QSettings ini (`QCoreApplication::setOrganizationName("RelayTerminal")`)."""
    base = Path(config_home).expanduser() if config_home is not None else _config_home()
    return base / "RelayTerminal" / "relay.conf"


def _ini_list(raw: str) -> list[str]:
    """A QSettings QStringList value: comma-separated items, quoted when they hold a comma or a
    space, with backslash escapes inside the quotes."""
    items, current, quoted, escape = [], [], False, False
    for char in raw:
        if escape:
            current.append(char)
            escape = False
        elif char == "\\" and quoted:
            escape = True
        elif char == '"':
            quoted = not quoted
        elif char == "," and not quoted:
            items.append("".join(current).strip())
            current = []
        else:
            current.append(char)
    items.append("".join(current).strip())
    return [item for item in items if item]


def _decode_tier_item(item: str, position: int) -> dict | None:
    """`preset|model|effort|rank=N` as `ModelCatalog.cpp decodeTierItem` reads it."""
    rank = position
    marker = item.rfind("|rank=")
    if marker > 0:
        try:
            saved = int(item[marker + 6:])
        except ValueError:
            saved = 0
        if 0 < saved <= 1000:
            rank = saved
            item = item[:marker]
    last = item.rfind("|")
    if last <= 0:
        return None
    key, effort = item[:last], item[last + 1:]
    preset, _, model = key.partition("|")
    if not preset:
        return None
    entry = {"preset": preset, "model": model, "rank": rank}
    if effort:
        entry["effort"] = effort
    return entry


def _available_key(key: str, available: list[str]) -> bool:
    """`ModelCatalog.cpp isAvailableKey`: the stored ticks of Options › Models. A list with no
    ticks stored lets everything through; a preset the ticks never name is available by default."""
    if not available or key in available:
        return True
    preset = key.partition("|")[0]
    return not any(item == preset or item.startswith(preset + "|") for item in available)


def stored_high_entries(config_home=None, tier: str = "high") -> list[dict] | None:
    """The `high` list the user keeps in Options › Models, as `tiers.high` entries, minus the
    models they have un-ticked; None when no list has been stored (a fresh install, or a GUI that
    has never opened that page), so the caller falls back to the defaults."""
    path = relay_settings_path(config_home)
    if not path.is_file():
        return None
    parser = configparser.RawConfigParser(strict=False, interpolation=None, delimiters=("=",))
    parser.optionxform = str
    try:
        parser.read(path, encoding="utf-8")
    except (configparser.Error, OSError, UnicodeDecodeError):
        return None
    if not parser.has_option("models", f"tier\\{tier}"):
        return None
    raw = parser.get("models", f"tier\\{tier}")
    available = _ini_list(parser.get("models", "available")) if parser.has_option("models", "available") else []
    out = []
    for position, item in enumerate(_ini_list(raw), 1):
        entry = _decode_tier_item(item, position)
        if entry is None:
            continue
        if not _available_key(f"{entry['preset']}|{entry['model']}", available):
            continue
        out.append(entry)
    return out


def default_high_entries() -> list[dict]:
    """`presets.tier_list_defaults`' plain `high` list from what can take a turn on this machine
    — the same call `worker.py` answers the GUI's `defaults` button with. Keys are read through
    the keystore (environment, then the desktop keyring unless `RELAY_KEYRING=off`)."""
    from . import customproviders, guest_harness_provider, hosted, keystore, localmodels, relay_pro
    try:
        sources = keystore.sources()
        usable = [p for p in presets.PRESETS
                  if (relay_pro.status()["available"] if p == "relay-pro" else
                      hosted.available() if p == "relay-free" else bool(sources.get(p)))]
        custom_rows = customproviders.rows()
        lists = presets.tier_list_defaults(
            usable,
            local=[(e.id, e.model) for e in localmodels.catalog().values()],
            custom=[(row["id"], row.get("model") or "") for row in custom_rows if row.get("has_stored_key")],
            guests=guest_harness_provider.preset_rows())
    except Exception as exc:                                  # a default is never a crash
        logs.event(_log, "reconcile_defaults_failed", level_name="warning", error=str(exc)[:200])
        return []
    return list((lists.get("plain") or {}).get("high") or [])


def high_entries(policy, *, stored=None, defaults=None) -> tuple[list[dict], str]:
    """(entries, source): the list a reconciliation draws from and where it came from —
    `policy` (the queue passed `tiers.high` or `high`), `stored` (Options › Models) or
    `defaults`. `stored`/`defaults` may be callables, so nothing is read that is not needed."""
    if isinstance(policy, dict):
        raw = None
        if isinstance(policy.get("tiers"), dict):
            raw = policy["tiers"].get("high")
        if raw is None and isinstance(policy.get("high"), list):
            raw = policy["high"]
        if raw is not None:
            listed = model_roles.validate_tiers({"high": raw}).get("high") or []
            if listed:
                return listed, "policy"
    found = stored() if callable(stored) else stored
    if found is None and stored is None:
        found = stored_high_entries()
    if found is not None:
        listed = model_roles.validate_tiers({"high": found}).get("high") or []
        if listed:
            return listed, "stored"
    found = defaults() if callable(defaults) else defaults
    if found is None and defaults is None:
        found = default_high_entries()
    listed = model_roles.validate_tiers({"high": found or []}).get("high") or []
    return listed, "defaults"


def with_high_effort(entry: dict) -> dict:
    """The entry at its own level, or — when it names none — the model's top level, in the
    provider's own word (`presets.tier_start_efforts`, the `high` rule): the reconciler runs at
    high effort (owner, 2026-09-26: "fable high or astra high")."""
    if entry.get("effort"):
        return entry
    preset_id = entry.get("preset") or ""
    model = entry.get("model") or ""
    try:
        if model_roles.is_guest_preset(preset_id):
            from . import guest_harness_provider
            family = model_roles.guest_id_of(preset_id).partition(":")[0]
            levels = guest_harness_provider.guest_efforts(family)
            top = presets.tier_start_efforts(levels, guest_id=family, name=model)["high"]
        else:
            levels = model_roles._levels_for(preset_id, model)
            top = presets.tier_start_efforts(levels, name=presets.model_name(preset_id, model))["high"]
    except Exception:
        top = None
    return {**entry, "effort": top} if top else entry


def _placeholder_main(completion_tokens: int) -> ProviderConfig:
    """The `main_config` a RoleResolver needs to exist. It is never called: a High list with
    nothing usable resolves back to it, which `draw_high` reads as "no model". Its
    `max_tokens` is what `_build` hands every resolved config, so the completion cap goes here."""
    return ProviderConfig("https://reconcile.invalid/v1", "none", "", {}, max(MIN_OUTPUT_TOKENS, completion_tokens))


def draw_high(entries: list[dict], *, key_lookup=None, guest_check=None, exclude=(),
              completion_tokens: int = DEFAULT_COMPLETION_TOKENS) -> model_roles.Resolved | None:
    """One weighted draw from the High list, through `RoleResolver.choose_role("high")`: ranks,
    then `ordered_candidates(choose=True)` with the subscription weights and every signed-in
    guest or plan account of a family row, then the first entry that can run here (a stored
    key, a harness that starts). None when nothing in the list can take the call."""
    skipped = set(exclude)
    listed = [with_high_effort(e) for e in entries
              if (e.get("preset") or e.get("base_url")) and e.get("preset") not in skipped
              and e.get("base_url") not in skipped]
    if not listed:
        return None
    resolver = model_roles.RoleResolver(_placeholder_main(completion_tokens), None, {},
                                        key_lookup=key_lookup, tiers={"high": listed},
                                        guest_check=guest_check)
    resolved = resolver.choose_role("high")
    return None if resolved.is_main else resolved


def account_of(preset_id) -> str:
    """The account a preset names: `work` for `guest:claude:work`, `ethz` for `glm-coding:ethz`,
    "" for a family or a plain preset."""
    if not isinstance(preset_id, str):
        return ""
    if model_roles.is_guest_preset(preset_id):
        return model_roles.guest_id_of(preset_id).partition(":")[2]
    from . import key_accounts
    return key_accounts.split(preset_id)[1]


# ----- model calls ------------------------------------------------------------------------------

SYSTEM_PROMPT = """You are Relay's landing reconciler. Two commits touched the same files: the target branch (ours, already on main) and a submitted commit (theirs, an agent's finished work). Git could not merge them. Resolve every conflicted file so that BOTH sides' intent is kept.

Rules:
- Keep every behavioural change from both sides. When they truly contradict, prefer the submitted side for the code it set out to change and keep the target side everywhere else, and say so in notes.
- Never remove or weaken a test, an assertion or a check that either side has. Never add a skip.
- Change only the files you are given. Return each one in full, exactly as it should be on disk, with no conflict markers and nothing elided.
- If a safe resolution is not possible from what you can see, answer {"give_up": "<why, one sentence>"} instead.

When the prompt describes a GATE FAILURE instead of a merge conflict, the two sides already merged cleanly but the project's required checks (build, tests, lint) failed on the result. Read the diagnostics, find the cause in the files you are allowed to change, and return corrected versions of only the files that need to change. Fix the code, never the check: do not delete, skip, loosen or rewrite a test or an assertion to make it pass.

Answer with one JSON object and nothing else:
{"files": [{"path": "<path as given>", "content": "<the whole file>"}], "notes": "<one or two sentences>"}
"""

GUEST_INSTRUCTIONS = ("You are running one read-only reconciliation turn for Relay's landing queue. Do not "
                      "edit, create or delete files, run builds or tests, or use the Board; read what you "
                      "need from the working directory and answer with the JSON object the prompt asks for.")


def _default_model_call(request: dict):
    """The production adapter: the drawn `Resolved` to a real completion. A guest preset runs one
    read-only harness turn with the candidate as its cwd, bounded by `budget_tokens`,
    `completion_tokens` and the request's `cancel`; anything else is a no-tools completion
    through `provider.make_provider` with the completion cap in `max_tokens`.

    Signature for an integrator's own adapter: `model_call(request) -> (text, usage)` or
    `{"text", "usage", "model"?, "session_id"?}`; `request` carries `system`, `user`,
    `resolved` (a `roles.Resolved`), `model`, `preset`, `account`, `effort`, `attempt`, `cwd`,
    `conflicts`, `completion_tokens`, `budget_tokens`, `cancel`, `job_id`, `kind`.
    """
    resolved = request["resolved"]
    cancel = request.get("cancel") or threading.Event()
    if model_roles.is_guest_preset(resolved.preset_id):
        return _call_guest(resolved, request["system"], request["user"], cancel, request["cwd"],
                           budget_tokens=int(request.get("budget_tokens") or 0),
                           completion_tokens=int(request.get("completion_tokens") or 0))
    return _call_api(resolved, request["system"], request["user"], cancel, request["completion_tokens"])


def _call_api(resolved, system: str, user: str, cancel: threading.Event,
              completion_tokens: int) -> tuple[str, dict]:
    from . import sidecall
    from .provider import make_provider
    config = dataclasses.replace(resolved.config, max_tokens=max(MIN_OUTPUT_TOKENS,
                                                                min(resolved.config.max_tokens, completion_tokens)))
    provider = make_provider(config, stall_timeout=max(DEFAULT_STALL_TIMEOUT, 300.0))
    # One reservation is one request: a zero retry budget makes the transport treat every
    # 429/5xx as final instead of re-sending the same prompt (and its input tokens) several
    # times behind one ledger row. The failure excludes the provider and the loop redraws.
    text, usage = sidecall.call(provider, system, user, cancel, retry_budget_s=0.0)
    return text, usage or {}


class OverBudget(RuntimeError):
    """A guest turn was cancelled by the reconciler's own watch: its harness reported usage past
    the attempt's reservation, or its streamed answer ran past the completion cap. Carries the
    last usage seen, so the charge can be raised to it."""

    def __init__(self, reason: str, usage: dict | None):
        super().__init__(reason)
        self.usage = usage or {}


def _call_guest(resolved, system: str, user: str, cancel: threading.Event, cwd: str, *,
                budget_tokens: int = 0, completion_tokens: int = 0):
    """One turn of Claude Code or Codex (or a registered account of either) through the existing
    harness provider: `start_provider` builds and starts the adapter — a failure to start is its
    ValueError — and `HarnessProvider.complete` runs the turn and reports `usage`. The system text
    rides in the prompt because a harness takes one prompt, not a message list.

    The turn is bounded from outside, because a harness takes no `max_tokens`: every `usage` the
    harness reports (cumulative for the turn) is checked against `budget_tokens`, the streamed
    answer's length against `completion_tokens`, and the first to run over sets `cancel`, which
    both adapters poll every 0.2 s. A turn that reports usage only at its end (Claude Code's
    result message) is bounded by the caller's wall-clock deadline and settled from that report.
    """
    from . import guest_harness_provider as guests
    options = {"model": resolved.config.model or "", "permissions": "deny", "memory": "relay"}
    if resolved.effort:
        options["effort"] = resolved.effort
    provider = guests.start_provider(resolved.preset_id, {"guest": options}, cwd, config=None,
                                     skill_index=None, instruction_suffix=GUEST_INSTRUCTIONS,
                                     delegation=False)
    usage: dict = {}
    streamed = [0]
    over: list[str] = []

    def quiet(event: dict) -> None:
        kind = event.get("event")
        if kind == "usage" and isinstance(event.get("usage"), dict):
            usage.update(event["usage"])
            counts = usage_counts(usage)
            if budget_tokens and counts is not None and counts[0] + counts[1] > budget_tokens and not over:
                over.append(f"the guest reported {counts[0] + counts[1]:,} tokens, past the attempt's "
                            f"reservation of {budget_tokens:,}")
                cancel.set()
        elif kind == "delta":
            streamed[0] += len(event.get("text") or "")
            if completion_tokens and streamed[0] > completion_tokens * CHARS_PER_TOKEN and not over:
                over.append(f"the guest's answer passed {completion_tokens:,} tokens "
                            f"({streamed[0]:,} characters streamed)")
                cancel.set()

    try:
        try:
            message = provider.complete([{"role": "user", "content": system + "\n\n" + user}], [], quiet, cancel)
        except Cancelled:
            if over:
                raise OverBudget(over[0], usage) from None
            raise
        if over:
            # The report that crossed the line was the turn's last one, and the turn ended before
            # the cancel was seen: the answer is complete, and still not accepted — the budget
            # was the bound, not the timing.
            raise OverBudget(over[0], usage)
    finally:
        try:
            provider.close()
        except Exception:
            pass
    # The model the guest said it is running ("claude-fable-5-1" for an entry that said "fable")
    # and its session id go back with the answer, so the record names what actually ran.
    return {"text": message.get("content") or "", "usage": usage, "model": provider.config.model,
            "session_id": getattr(provider, "session_id", "") or ""}


# ----- the candidate -----------------------------------------------------------------------------

def _git_env() -> dict:
    """The environment for a git call in the candidate: ambient GIT_* scrubbed, so a hook or a
    parent process's GIT_DIR/GIT_INDEX_FILE cannot point the call at the author's checkout."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env["GIT_TERMINAL_PROMPT"] = "0"
    return env


def _git(cwd, *args, check: bool = True) -> str:
    result = subprocess.run(["git", *args], cwd=str(cwd), env=_git_env(), capture_output=True,
                            text=True, errors="replace", timeout=120)
    if check and result.returncode != 0:
        raise ReconcileError(f"git {' '.join(args[:2])} failed in {cwd}: {result.stderr.strip()[:300]}")
    return result.stdout


def _git_bytes(cwd, *args) -> bytes | None:
    result = subprocess.run(["git", *args], cwd=str(cwd), env=_git_env(), capture_output=True, timeout=120)
    return result.stdout if result.returncode == 0 else None


def safe_relative_path(value) -> str | None:
    """A path as the model or the queue named it, or None when it is not a plain relative path
    inside the candidate (absolute, `..`, a backslash, a drive letter, a `.git` component)."""
    if not isinstance(value, str) or not value.strip():
        return None
    text = value.strip().replace("\\", "/")
    if text.startswith("/") or text.startswith("~") or re.match(r"^[A-Za-z]:", text):
        return None
    parts = [p for p in text.split("/") if p not in ("", ".")]
    if not parts or any(p == ".." or p == ".git" for p in parts):
        return None
    return "/".join(parts)


def _named_paths(items, what: str) -> list[str]:
    out: list[str] = []
    for item in items or ():
        raw = item.get("path") if isinstance(item, dict) else item
        path = safe_relative_path(raw)
        if path is None:
            raise ReconcileError(f"{what} {raw!r} is not a plain relative path in the candidate.")
        if path not in out:
            out.append(path)
    return out


def context_kind(context: dict) -> str:
    """`merge_conflict` (the default) or `gate_failure`, from `diagnostics.kind` as `landq`
    writes it, or an explicit `kind` on the context."""
    diagnostics = context.get("diagnostics")
    kind = diagnostics.get("kind") if isinstance(diagnostics, dict) else None
    kind = kind or context.get("kind") or "merge_conflict"
    return "gate_failure" if str(kind) in ("gate_failure", "repair", "gate") else "merge_conflict"


def repair_paths(candidate_path, repo, context: dict) -> list[str]:
    """The files a gate repair may touch: the queue's `repair_paths` and any `conflicts` it
    named, else what the submission changed against the base (`base..submitted`, additions
    and modifications; a path the submission deleted has nothing to repair). Capped at
    MAX_REPAIR_PATHS: a submission wider than that is the author's to fix."""
    out = _named_paths(context.get("repair_paths"), "repair path")
    out += [p for p in _named_paths(context.get("conflicts"), "conflict path") if p not in out]
    if out:
        return out
    base, submitted = context.get("base_sha"), context.get("submitted_sha")
    if not base or not submitted:
        return []
    listing = None
    for cwd in (candidate_path, repo):
        if cwd:
            listing = _git_bytes(cwd, "diff", "--name-only", "--diff-filter=AMR", "-z", str(base), str(submitted))
            if listing is not None:
                break
    for raw in (listing or b"").decode("utf-8", "replace").split("\0"):
        path = safe_relative_path(raw)
        if path and path not in out:
            out.append(path)
    if len(out) > MAX_REPAIR_PATHS:
        raise ReconcileError(f"the submission changed {len(out)} files; a repair is bounded to "
                             f"{MAX_REPAIR_PATHS}. Returned to the author.")
    return out


def conflicted_paths(candidate_path, conflicts) -> list[str]:
    """The paths to resolve: the context's `conflicts` (strings, or dicts with `path`) when the
    queue named them, else the unmerged entries of the candidate's index."""
    out = _named_paths(conflicts, "conflict path")
    if out:
        return out
    listing = _git(candidate_path, "ls-files", "-u", "-z", check=False)
    for record in listing.split("\0"):
        if not record:
            continue
        _meta, _tab, path = record.partition("\t")
        path = safe_relative_path(path)
        if path and path not in out:
            out.append(path)
    return out


def _is_binary(data: bytes | None) -> bool:
    return bool(data) and b"\0" in data[:8000]


@dataclasses.dataclass
class FileVersions:
    path: str
    base: str
    ours: str
    theirs: str
    merged: str
    exists: bool                # the candidate holds a working copy of it


def _text(data: bytes | None) -> str:
    return data.decode("utf-8", "replace") if data else ""


def file_versions(candidate_path, repo, path: str, base_sha, target_sha, submitted_sha) -> FileVersions:
    """Base, ours (target), theirs (submitted) and the merged working copy of one conflicted
    path. Objects are read from the candidate (a worktree shares them) and then from the repo."""
    versions = []
    for sha in (base_sha, target_sha, submitted_sha):
        data = None
        if sha:
            data = _git_bytes(candidate_path, "show", f"{sha}:{path}")
            if data is None and repo:
                data = _git_bytes(repo, "show", f"{sha}:{path}")
        if _is_binary(data):
            raise ReconcileError(f"{path} is binary in {str(sha)[:12]}; a binary conflict is the author's.")
        versions.append(_text(data))
    working = Path(candidate_path) / path
    merged, exists = "", False
    if working.is_file() and not working.is_symlink():
        raw = working.read_bytes()
        if _is_binary(raw):
            raise ReconcileError(f"{path} is binary in the candidate; a binary conflict is the author's.")
        if len(raw) > MAX_FILE_BYTES:
            raise ReconcileError(f"{path} is {len(raw):,} bytes; files over {MAX_FILE_BYTES:,} bytes are the author's.")
        merged, exists = _text(raw), True
    return FileVersions(path, versions[0], versions[1], versions[2], merged, exists)


def _diff(a: str, b: str, path: str, from_label: str, to_label: str, limit: int) -> str:
    lines = difflib.unified_diff(a.splitlines(keepends=True), b.splitlines(keepends=True),
                                 fromfile=f"{from_label}/{path}", tofile=f"{to_label}/{path}", n=3)
    text = "".join(lines)
    if len(text) > limit:
        text = text[:limit] + f"\n[… diff trimmed at {limit:,} characters …]\n"
    return text


def build_prompt(context: dict, files: list[FileVersions], *, context_tokens: int,
                 feedback: list[str] | None = None) -> tuple[str, str]:
    """(system, user) for one attempt, within `context_tokens` (at CHARS_PER_TOKEN). The merged
    working copies always go in whole; the two side diffs are trimmed first, then dropped. When
    even the working copies do not fit, the conflict is too large for a model — the caller
    refuses with that sentence."""
    budget = context_tokens * CHARS_PER_TOKEN
    repair = context_kind(context) == "gate_failure"
    head = [("GATE FAILURE to repair" if repair else "MERGE CONFLICT to resolve")
            + f" — repository: {context.get('repo_id') or ''}   job: {context.get('job_id') or ''}",
            f"base: {context.get('base_sha') or '?'}   target (ours): {context.get('target_sha') or '?'}   "
            f"submitted (theirs): {context.get('submitted_sha') or '?'}"]
    cards = context.get("cards") or []
    if cards:
        head.append("Cards: " + "; ".join(_card_line(c) for c in cards[:8]))
    intents = _lines_of(context.get("intents"), 12, 600)
    if intents:
        head.append("Intent of the submitted work:")
        head.extend("  - " + line for line in intents)
    head.extend(_diagnostic_lines(context.get("diagnostics")))
    if feedback:
        head.append("Your previous answer was rejected by the safety review; fix these and answer again:")
        head.extend("  - " + _clip(str(f), 500) for f in feedback[:20])
    if repair:
        head.append(f"Files you may change ({len(files)}), return only the ones that need to change: "
                    + ", ".join(f.path for f in files))
    else:
        head.append(f"Conflicted files ({len(files)}), return every one of them: " + ", ".join(f.path for f in files))
    header = "\n".join(head) + "\n"
    bodies = []
    for f in files:
        if repair:
            merged = f.merged if f.exists else "(no working copy in the candidate)"
            bodies.append(f"\n### {f.path}\n#### the candidate as the gate saw it\n```\n{merged}\n```\n")
        else:
            merged = f.merged if f.exists else "(no working copy in the candidate: the file was deleted on one side)"
            bodies.append(f"\n### {f.path}\n#### merged working copy (with git's conflict markers)\n```\n{merged}\n```\n")
    fixed = len(SYSTEM_PROMPT) + len(header) + sum(len(b) for b in bodies)
    if fixed > budget:
        raise ReconcileError(f"the conflicted files alone are {fixed:,} characters; the reconciler's context "
                             f"cap is {budget:,} ({context_tokens:,} tokens). Returned to the author.")
    remaining = budget - fixed
    per_diff = max(0, remaining // max(1, 2 * len(files)))
    parts = [header]
    for f, body in zip(files, bodies):
        parts.append(body)
        if per_diff >= 200:
            ours = _diff(f.base, f.ours, f.path, "base", "ours", per_diff)
            theirs = _diff(f.base, f.theirs, f.path, "base", "theirs", per_diff)
            parts.append(f"#### what the target changed (base → ours)\n```diff\n{ours or '(no change)'}\n```\n")
            parts.append(f"#### what the submission changed (base → theirs)\n```diff\n{theirs or '(no change)'}\n```\n")
    return SYSTEM_PROMPT, "".join(parts)


def _lines_of(value, limit: int, width: int) -> list[str]:
    """Intents or diagnostics as lines, whether the queue sent a list, a dict or a string."""
    if not value:
        return []
    if isinstance(value, dict):
        items = [f"{k}: {v}" for k, v in value.items()]
    elif isinstance(value, (list, tuple)):
        items = [str(v) for v in value]
    else:
        items = [str(value)]
    return [_clip(item, width) for item in items[:limit]]


def _diagnostic_lines(diagnostics) -> list[str]:
    """The queue's diagnostics: `landq` sends a dict — `kind`, and for a gate failure `reason`,
    `round` and the gate's `log`, whose tail is what a repair needs to read."""
    if not diagnostics:
        return []
    if not isinstance(diagnostics, dict):
        return ["Queue diagnostics:"] + ["  - " + line for line in _lines_of(diagnostics, 12, 400)]
    out = ["Queue diagnostics:"]
    for key in ("kind", "round", "reason", "candidate_sha", "policy_hash", "merge_output"):
        if diagnostics.get(key) not in (None, "", [], {}):
            out.append(f"  - {key}: {_clip(str(diagnostics[key]), 600)}")
    conflicts = diagnostics.get("conflicts")
    if isinstance(conflicts, (list, tuple)) and conflicts:
        out.append("  - conflicts: " + ", ".join(str(c) for c in conflicts[:30]))
    log = diagnostics.get("log")
    if isinstance(log, str) and log.strip():
        tail = log[-GATE_LOG_CHARS:]
        if len(log) > GATE_LOG_CHARS:
            tail = f"[… {len(log) - GATE_LOG_CHARS:,} earlier characters of the gate log omitted …]\n" + tail
        out.append("Gate log (tail):\n```\n" + tail.rstrip() + "\n```")
    for key, value in diagnostics.items():
        if key not in ("kind", "round", "reason", "candidate_sha", "policy_hash", "merge_output", "conflicts",
                       "log", "log_path"):
            out.append(f"  - {key}: {_clip(str(value), 300)}")
    return out


def _card_line(card) -> str:
    if isinstance(card, dict):
        ident = card.get("id") or ""
        title = card.get("title") or card.get("summary") or ""
        return _clip(f"#{ident} {title}".strip(), 200)
    return _clip(str(card), 200)


def _clip(text: str, limit: int) -> str:
    text = text.replace("\n", " ").strip()
    return text if len(text) <= limit else text[:limit - 1] + "…"


# ----- the reply ---------------------------------------------------------------------------------

def parse_reply(text: str) -> tuple[dict[str, str] | None, str, str]:
    """(files, notes, give_up) from the model's reply. `files` None with a `give_up` sentence when
    the model declined; None and a reason in `notes` when the reply is not what was asked for."""
    from .sidecall import parse_json_object
    data = parse_json_object(text or "")
    if data is None:
        return None, "the reply was not the JSON object asked for", ""
    if data.get("give_up"):
        return None, "", _clip(str(data["give_up"]), 400)
    rows = data.get("files")
    if not isinstance(rows, list) or not rows:
        return None, "the reply's `files` is not a non-empty list", ""
    files: dict[str, str] = {}
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("content"), str):
            return None, "a `files` row has no string `content`", ""
        path = safe_relative_path(row.get("path"))
        if path is None:
            return None, f"a `files` row names an unusable path {row.get('path')!r}", ""
        if path in files:
            return None, f"{path} is returned twice", ""
        files[path] = row["content"]
    notes = data.get("notes")
    return files, _clip(str(notes), 600) if isinstance(notes, str) else "", ""


def _stripped_lines(text: str) -> list[str]:
    return [line.strip() for line in text.splitlines()]


def _meaningful(lines) -> set[str]:
    return {line for line in lines if len(line) >= 4 and line not in _TRIVIAL_LINES}


def _test_names(text: str) -> set[str]:
    names: set[str] = set()
    for pattern in _TEST_NAMES:
        for match in pattern.finditer(text):
            names.add(".".join(g for g in match.groups() if g))
    return names


def is_test_path(path: str) -> bool:
    return bool(_TEST_PATH.search(path))


def _assertion_lines(text: str) -> set[str]:
    """The stripped lines that carry an assertion or check."""
    return {line for line in _stripped_lines(text) if line and _ASSERTIONS.search(line)}


def _required(base: set, sides: list[set]) -> set:
    """What a merge must keep, out of per-side sets: everything a side added against the base,
    and everything every side kept from the base. A base item one side changed is superseded by
    that side's version and is not required — the honest merge takes the change."""
    required: set = set()
    for side in sides:
        required |= side - base
    kept = set(base)
    for side in sides:
        kept &= side
    return required | kept


def _py_test_checks(text: str) -> dict[str, dict] | None:
    """Per test function (`test_*`, at module level or in a class): the multiset of its check
    statements as `ast.dump` strings — `assert`, calls whose name starts with assert/expect/fail,
    `with self.assertRaises(...)` / `pytest.raises(...)` items — plus how many top-level
    `return`/`raise` statements and how many statements its body has. None when the text does
    not parse (a side that never parsed is held to the line rules only)."""
    try:
        tree = ast.parse(text)
    except (SyntaxError, ValueError, RecursionError):
        return None
    out: dict[str, dict] = {}
    for node in ast.walk(tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) or not node.name.startswith("test"):
            continue
        checks: list[str] = []
        exits = [0]
        _walk_checks(node.body, (), checks, exits)
        first_exit = bool(node.body) and isinstance(node.body[0], (ast.Return, ast.Raise, ast.Pass))
        key = node.name if node.name not in out else f"{node.name}#{sum(k.split('#')[0] == node.name for k in out) + 1}"
        out[key] = {"checks": sorted(checks), "exits": exits[0], "first_exit": first_exit, "statements": len(node.body)}
    return out


def _is_check_call(value) -> bool:
    return isinstance(value, ast.Call) and bool(_PY_CHECK_NAMES.match(_call_name(value)))


def _walk_checks(stmts, path: tuple, checks: list[str], exits: list[int]) -> None:
    """Collect a test body's check statements, each fingerprinted with the control flow above
    it: `assert x` at the top of the body and the same `assert x` under `if False:`, inside a
    `for`, in an `except` arm or in a nested `def` are different fingerprints, so moving a check
    under a guard that never runs is a lost check, not a kept one. Every return/raise at any
    depth is counted: `if x: return` before the assertion is the bypass this exists to catch,
    and a `raise` inside a `with assertRaises` block is on both sides already, so the
    max-of-sides rule keeps it legal."""
    here = "/".join(path)
    for stmt in stmts:
        if isinstance(stmt, ast.Assert) or (isinstance(stmt, ast.Expr) and _is_check_call(stmt.value)):
            checks.append(f"{here}|{ast.dump(stmt)}")
        elif isinstance(stmt, (ast.Return, ast.Raise)):
            exits[0] += 1
        elif isinstance(stmt, (ast.With, ast.AsyncWith)):
            for item in stmt.items:
                if _is_check_call(item.context_expr):
                    checks.append(f"{here}|{ast.dump(item.context_expr)}")
            guard = "with:" + ",".join(ast.dump(item.context_expr) for item in stmt.items)
            _walk_checks(stmt.body, path + (guard,), checks, exits)
        elif isinstance(stmt, ast.If):
            _walk_checks(stmt.body, path + ("if:" + ast.dump(stmt.test),), checks, exits)
            _walk_checks(stmt.orelse, path + ("else:" + ast.dump(stmt.test),), checks, exits)
        elif isinstance(stmt, ast.While):
            _walk_checks(stmt.body, path + ("while:" + ast.dump(stmt.test),), checks, exits)
            _walk_checks(stmt.orelse, path + ("whileelse:" + ast.dump(stmt.test),), checks, exits)
        elif isinstance(stmt, (ast.For, ast.AsyncFor)):
            guard = "for:" + ast.dump(stmt.target) + "in" + ast.dump(stmt.iter)
            _walk_checks(stmt.body, path + (guard,), checks, exits)
            _walk_checks(stmt.orelse, path + ("forelse:" + guard,), checks, exits)
        elif isinstance(stmt, ast.Try) or (hasattr(ast, "TryStar") and isinstance(stmt, ast.TryStar)):
            _walk_checks(stmt.body, path + ("try",), checks, exits)
            for handler in stmt.handlers:
                kind = ast.dump(handler.type) if handler.type is not None else "*"
                _walk_checks(handler.body, path + ("except:" + kind,), checks, exits)
            _walk_checks(stmt.orelse, path + ("tryelse",), checks, exits)
            _walk_checks(stmt.finalbody, path + ("finally",), checks, exits)
        elif hasattr(ast, "Match") and isinstance(stmt, ast.Match):
            for case in stmt.cases:
                _walk_checks(case.body, path + ("case:" + ast.dump(case.pattern),), checks, exits)
        elif isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            _walk_checks(stmt.body, path + ("def:" + stmt.name,), checks, exits)
        else:
            # A statement that hides a body this walk does not know (a future node kind): count
            # what is inside conservatively as checks under an opaque guard.
            for child in ast.iter_child_nodes(stmt):
                if isinstance(child, ast.stmt):
                    _walk_checks([child], path + (type(stmt).__name__,), checks, exits)


def _call_name(call: ast.Call) -> str:
    func = call.func
    if isinstance(func, ast.Attribute):
        return func.attr
    if isinstance(func, ast.Name):
        return func.id
    return ""


def _parse_problem(path: str, content: str) -> str | None:
    """A file that must parse and does not: Python, JSON and TOML are cheap to check and a
    model's whole-file rewrite loses a bracket more often than anything else."""
    lower = path.lower()
    try:
        if lower.endswith((".py", ".pyi")):
            ast.parse(content)
        elif lower.endswith(".json") and content.strip():
            import json
            json.loads(content)
        elif lower.endswith(".toml"):
            tomllib.loads(content)
    except (SyntaxError, ValueError, RecursionError) as exc:
        return f"{path}: does not parse ({type(exc).__name__}: {_clip(str(exc), 120)})"
    return None


def _multiset_missing(needed: list[str], have: list[str]) -> list[str]:
    counts: dict[str, int] = {}
    for item in have:
        counts[item] = counts.get(item, 0) + 1
    missing = []
    for item in needed:
        if counts.get(item, 0) > 0:
            counts[item] -= 1
        else:
            missing.append(item)
    return missing


def _test_body_problems(path: str, base: str, sides: list[str], content: str) -> list[str]:
    """Python test functions compared as syntax trees: every check statement a side added, and
    every one both sides kept, must be in the result's function (a changed operand is a
    different statement); no test may gain an early `return`/`raise`, and none may open with
    one."""
    result = _py_test_checks(content)
    if result is None:
        return []                                  # `_parse_problem` already refused it
    before = [checks for checks in (_py_test_checks(side) for side in sides) if checks]
    if not before:
        return []
    base_checks = _py_test_checks(base) or {}
    problems = []
    names = sorted({name for checks in before for name in checks})
    for name in names:
        versions = [checks[name] for checks in before if name in checks]
        after = result.get(name)
        if after is None:
            continue                               # a lost test is `test(s) removed` already
        base_set = set(base_checks.get(name, {}).get("checks", []))
        union = sorted(_required(base_set, [set(v["checks"]) for v in versions]))
        missing = _multiset_missing(union, after["checks"])
        if missing:
            problems.append(f"{path}: {name} lost or changed {len(missing)} check(s), e.g. "
                            + _clip(missing[0].replace("Assert(", "assert(", 1), 100))
        if after["exits"] > max(v["exits"] for v in versions):
            problems.append(f"{path}: {name} gained an early return/raise")
        if after["first_exit"] and not any(v["first_exit"] for v in versions):
            problems.append(f"{path}: {name} now opens with return/raise/pass")
    return problems


def review_file(f: FileVersions, content: str, *, repair: bool = False) -> list[str]:
    """The conservative checks on one resolved file. Every string is a reason to refuse.

    `repair` is a gate-failure round: the candidate (`f.merged`) is the merged result the gate
    refused, so it counts as a side for the test and assertion rules, and the submission's own
    added lines may change (that is the repair) while the target's may not."""
    problems: list[str] = []
    path = f.path
    parse = _parse_problem(path, content)
    if parse:
        problems.append(parse)
    if _MARKER.search(content):
        problems.append(f"{path}: conflict markers remain")
    if _ELISION.search(content) and not _ELISION.search(f.ours) and not _ELISION.search(f.theirs):
        problems.append(f"{path}: the content reads as elided (a 'rest of file' placeholder)")
    ours_lines, theirs_lines, base_lines = _stripped_lines(f.ours), _stripped_lines(f.theirs), _stripped_lines(f.base)
    result_lines = _stripped_lines(content)
    result_set = set(result_lines)
    if not content.strip() and (f.ours.strip() or f.theirs.strip()):
        problems.append(f"{path}: emptied, though a side still has content")
        return problems
    shortest = min(len(f.ours), len(f.theirs)) if f.ours and f.theirs else max(len(f.ours), len(f.theirs))
    if shortest and len(content) < 0.5 * shortest:
        problems.append(f"{path}: {len(content):,} characters is under half of the shorter side ({shortest:,})")
    # Neither side's added lines may be dropped, beyond a small tolerance for a genuinely merged
    # line; nor a line both sides kept from the base. In a repair the submission's lines are what
    # is being fixed, so only the target's are held.
    base_set = set(base_lines)
    sides = [("target", ours_lines)] if repair else [("target", ours_lines), ("submission", theirs_lines)]
    for label, side_lines in sides:
        added = _meaningful(set(side_lines) - base_set)
        missing = sorted(added - result_set)
        # One merged line in ten may legitimately be rewritten (both sides changed it); under
        # ten added lines nothing may go. Conservative on purpose: a refusal costs the author a
        # look, an accepted deletion costs them their change.
        allowed = len(added) // 10
        if len(missing) > allowed:
            sample = "; ".join(_clip(m, 80) for m in missing[:4])
            problems.append(f"{path}: {len(missing)} of the {label}'s {len(added)} added lines are gone "
                            f"(allowed {allowed}), e.g. {sample}")
    kept = _meaningful(base_set & set(ours_lines) & set(theirs_lines))
    missing_kept = sorted(kept - result_set)
    allowed = len(kept) // 20
    if len(missing_kept) > allowed:
        sample = "; ".join(_clip(m, 80) for m in missing_kept[:4])
        problems.append(f"{path}: {len(missing_kept)} lines both sides kept are gone (allowed {allowed}), "
                        f"e.g. {sample}")
    # Assertion-bearing lines a side added, and those both sides kept, survive verbatim, in any
    # file: `assert value == 42` rewritten as `assert True` keeps the count and drops the check,
    # so counts are not the test. In a repair the candidate stands in for the submission — its
    # lines are what the repair may change, but not its assertions.
    texts = [f.ours, f.merged] if repair and f.merged else [f.ours, f.theirs]
    needed = _required(_assertion_lines(f.base), [_assertion_lines(t) for t in texts])
    gone = sorted(needed - result_set)
    if gone:
        problems.append(f"{path}: {len(gone)} assertion line(s) changed or removed, e.g. "
                        + "; ".join(_clip(g, 90) for g in gone[:3]))
    if is_test_path(path) or any(_test_names(t) for t in texts):
        names_before = set().union(*(_test_names(t) for t in texts))
        lost = sorted(names_before - _test_names(content))
        if lost:
            problems.append(f"{path}: test(s) removed: {', '.join(lost[:6])}")
        before = max(len(_ASSERTIONS.findall(t)) for t in texts)
        after = len(_ASSERTIONS.findall(content))
        if after < before:
            problems.append(f"{path}: assertions fell from {before} to {after}")
        skips_before = max(len(_SKIPS.findall(t)) for t in texts)
        skips_after = len(_SKIPS.findall(content))
        if skips_after > skips_before:
            problems.append(f"{path}: a skip or expected-failure marker was added ({skips_before} → {skips_after})")
        for label, pattern in (("guard", _GUARD_OPENERS), ("return", _RETURNS), ("block comment", _BLOCK_COMMENTS)):
            before = max(len(pattern.findall(t)) for t in texts)
            after = len(pattern.findall(content))
            if after > before:
                problems.append(f"{path}: a {label} was added to a test file ({before} → {after}); "
                                f"an assertion may have been moved out of the way")
        if path.lower().endswith(".py"):
            problems.extend(_test_body_problems(path, f.base, texts, content))
    return problems


def review_patch(files: dict[str, str], versions: dict[str, FileVersions], *, repair: bool = False) -> list[str]:
    """Every reason to refuse the model's files, across the whole set: a path outside the
    allowed set, a conflicted path left out (a repair may return a subset, but not nothing and
    not an unchanged file), then `review_file` on each. An empty list means the patch may be
    applied — which is not a proof of anything; the project gate still runs."""
    problems: list[str] = []
    what = "files this repair may change" if repair else "conflicted paths"
    for path in files:
        if path not in versions:
            problems.append(f"{path}: outside the {what} ({', '.join(sorted(versions))})")
    if repair:
        if not files:
            problems.append("no file was returned")
        for path, content in files.items():
            if path in versions and content == versions[path].merged:
                problems.append(f"{path}: returned unchanged")
    else:
        for path in versions:
            if path not in files:
                problems.append(f"{path}: not resolved (every conflicted file must be returned)")
    if problems:
        return problems
    for path, content in files.items():
        problems.extend(review_file(versions[path], content, repair=repair))
    return problems


def unified_patch(versions: dict[str, FileVersions], files: dict[str, str]) -> str:
    """The resolution as a unified diff against the target's version (what lands on top of the
    tip), one hunk set per path, `a/` `b/` prefixed."""
    out = []
    for path in sorted(files):
        before = versions[path].merged if versions[path].merged and not _MARKER.search(versions[path].merged) \
            else versions[path].ours
        out.append("".join(difflib.unified_diff(before.splitlines(keepends=True),
                                                files[path].splitlines(keepends=True),
                                                fromfile=f"a/{path}", tofile=f"b/{path}", n=3)))
    return "".join(out)


def apply_files(candidate_path, files: dict[str, str]) -> dict[str, str]:
    """Write the accepted files into the candidate and stage them, so the worktree's unmerged
    entries clear. Returns `{path: sha256}`. Only ever the candidate: the author's files are not
    named here and the paths were checked to stay inside it."""
    root = Path(candidate_path).resolve()
    hashes: dict[str, str] = {}
    for path, content in files.items():
        target = (root / path)
        if target.is_symlink():
            raise ReconcileError(f"{path} is a symlink in the candidate; refusing to write through it.")
        resolved = target.resolve()
        if root not in resolved.parents:
            raise ReconcileError(f"{path} resolves outside the candidate; refusing to write it.")
        resolved.parent.mkdir(parents=True, exist_ok=True)
        data = content.encode("utf-8")
        tmp = resolved.with_name(resolved.name + ".reconcile-tmp")
        tmp.write_bytes(data)
        os.replace(tmp, resolved)
        hashes[path] = hashlib.sha256(data).hexdigest()
    if (root / ".git").exists():
        _git(root, "add", "--", *files.keys(), check=False)
    return hashes


# ----- card notes -----------------------------------------------------------------------------------

def note_marker(job_id: str, status: str) -> str:
    return f"reconcile:{job_id}:{status}"


def card_notes(context: dict, result: dict) -> list[dict]:
    """One note per card the context names, keyed by job and outcome so `append_card_notes` can
    add each exactly once: what was reconciled, by which model, with the trailer; or what was
    handed back to the author agent and why."""
    ids = []
    for card in context.get("cards") or ():
        ident = card.get("id") if isinstance(card, dict) else card
        if isinstance(ident, str):
            ident = ident.strip().lstrip("#").upper()
            if ident and ident not in ids:
                ids.append(ident)
    if not ids:
        return []
    job_id = str(context.get("job_id") or "")
    marker = note_marker(job_id, result.get("status") or "")
    who = result.get("model") or "no model"
    if result.get("preset"):
        who += f" ({result['preset']}" + (f", account {result['account']}" if result.get("account") else "") + ")"
    paths = ", ".join(result.get("resolved_paths") or ()) or "no files"
    what = "repaired" if result.get("kind") == "gate_failure" else "resolved"
    if result.get("status") == "resolved":
        text = (f"Reconciled landing job {job_id} automatically: {paths} {what} by {who} at "
                f"{result.get('effort') or 'default'} effort, {_tokens_line(result.get('tokens'))}. "
                f"`{result.get('reconciled_from')}`. The project gate still decides whether it lands. "
                f"<!-- {marker} -->")
    else:
        text = (f"Landing job {job_id} could not be reconciled automatically: {result.get('reason') or 'no reason'}. "
                f"Returned to the author agent with the diagnostics"
                + (" and the rejected patch" if result.get("patch") else "") + f". <!-- {marker} -->")
    return [{"card": ident, "text": text, "marker": marker} for ident in ids]


def _tokens_line(tokens) -> str:
    if not isinstance(tokens, dict):
        return "no usage recorded"
    return f"{tokens.get('input', 0):,} in / {tokens.get('output', 0):,} out"


def append_card_notes(board_root, notes: list[dict], *, author: str = "reconcile") -> list[str]:
    """Append each note to its card's thread once (`board.Board.append_thread`, atomic under the
    board's own lock); a thread that already carries the note's marker is left alone. Returns
    the ids written. A card the board does not know is skipped, never an error."""
    from .board import Board, BoardError
    board = Board(board_root)
    written = []
    for note in notes:
        card_id = note["card"]
        try:
            if board.card_by_id(card_id) is None:
                continue
            if any(note["marker"] in (entry.text or "") for entry in board.thread(card_id)):
                continue
            board.append_thread(card_id, note["text"], author, kind="note")
            written.append(card_id)
        except (BoardError, OSError) as exc:
            logs.event(_log, "reconcile_note_failed", level_name="warning", card=card_id, error=str(exc)[:200])
    return written


# ----- the reconciler --------------------------------------------------------------------------------

def by_trailer(result: dict) -> str:
    """`Reconciled-By: <model> (<preset>[, account <a>]) effort=<e> attempts=<n> tokens=<in>/<out>`
    — the attribution line the queue appends under its own `Reconciled-From:`."""
    who = result.get("model") or "?"
    detail = result.get("preset") or ""
    if result.get("account"):
        detail += f", account {result['account']}"
    tokens = result.get("tokens") or {}
    return (f"{BY_TRAILER}: {who}" + (f" ({detail})" if detail else "")
            + f" effort={result.get('effort') or 'default'} attempts={len(result.get('attempts') or ())}"
            f" tokens={tokens.get('input', 0)}/{tokens.get('output', 0)}")


class _AttemptWatch:
    """The thread that turns the caller's cancel and a wall-clock deadline into one attempt's
    cancel event, which the adapters poll. `timed_out` says whether it was the deadline."""

    def __init__(self, outer: threading.Event, inner: threading.Event, deadline: float | None, now):
        self.outer, self.inner, self.deadline, self._now = outer, inner, deadline, now
        self.timed_out = False
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="reconcile-watch", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def _run(self) -> None:
        started = time.monotonic()
        while not self._stop.wait(0.1):
            if self.outer.is_set():
                self.inner.set()
                return
            if self.deadline is not None and time.monotonic() - started >= self.deadline:
                self.timed_out = True
                self.inner.set()
                return


class Reconciler:
    """`Reconciler(state_root=None).reconcile(context, model_call=None)` — the A4 contract.

    Keyword arguments beyond the contract are for tests and integrators: `tiers` (a `tiers`
    table or a bare High list, instead of the stored/default one), `key_lookup` and
    `guest_check` (what `RoleResolver` takes), `stored`/`defaults` (the list sources, values or
    callables), `model_call` (the default adapter for every call), `now` (the clock).
    """

    def __init__(self, *, state_root=None, tiers=None, key_lookup=None, guest_check=None,
                 stored=None, defaults=None, model_call=None, now=time.time):
        self.state_root = Path(state_root).expanduser() if state_root is not None else default_state_root()
        self.ledger = BudgetLedger(ledger_path(self.state_root), now=now)
        self.ledger.sweep()
        if isinstance(tiers, dict):
            tiers = tiers.get("high")
        self.tiers = list(tiers) if tiers is not None else None
        self.key_lookup = key_lookup
        self.guest_check = guest_check
        self.stored = stored
        self.defaults = defaults
        self.model_call = model_call
        self._now = now

    # ----- api ------------------------------------------------------------------------------
    async def reconcile(self, context: dict, *, model_call=None, board_root=None, cancel=None) -> dict:
        return await asyncio.to_thread(self.reconcile_sync, context, model_call=model_call,
                                       board_root=board_root, cancel=cancel)

    def history(self, job_id: str) -> list[dict]:
        """Every reservation this job made: model, account, tokens, outcome."""
        return self.ledger.rows(job_id=job_id)

    def budget_status(self, repo_id: str | None = None) -> dict:
        return self.ledger.status(repo_id=repo_id)

    def candidates(self, context: dict | None = None) -> dict:
        """What a draw would choose from right now, for `status` surfaces: the list and where it
        came from. No draw is made and nothing is charged."""
        policy = (context or {}).get("policy") if isinstance(context, dict) else None
        entries, source = self._entries(policy)
        return {"source": source, "entries": [with_high_effort(e) for e in entries]}

    # ----- pieces ---------------------------------------------------------------------------
    def _entries(self, policy) -> tuple[list[dict], str]:
        if self.tiers is not None:
            listed = model_roles.validate_tiers({"high": self.tiers}).get("high") or []
            return listed, "injected"
        return high_entries(policy, stored=self.stored, defaults=self.defaults)

    def _draw(self, entries: list[dict], exclude: set, completion_tokens: int):
        return draw_high(entries, key_lookup=self.key_lookup, guest_check=self.guest_check,
                         exclude=exclude, completion_tokens=completion_tokens)

    def reconcile_sync(self, context: dict, *, model_call=None, board_root=None, cancel=None) -> dict:
        if not isinstance(context, dict):
            raise ReconcileError("reconcile needs a context dict.")
        started = self._now()
        job_id = str(context.get("job_id") or "")
        repo_id = str(context.get("repo_id") or "")
        candidate = context.get("candidate_path")
        target_sha = str(context.get("target_sha") or "")
        submitted_sha = str(context.get("submitted_sha") or "")
        base_sha = str(context.get("base_sha") or "")
        kind = context_kind(context)
        repair = kind == "gate_failure"
        diagnostics = context.get("diagnostics")
        round_number = diagnostics.get("round") if isinstance(diagnostics, dict) else None
        result: dict = {"status": "author_required", "job_id": job_id, "repo_id": repo_id, "kind": kind,
                        "candidate_path": str(candidate or ""),
                        "reconciled_from": f"{TRAILER}: {target_sha} {submitted_sha}".rstrip(), "trailer": "",
                        "model": None, "preset": None, "account": "", "effort": None,
                        "tokens": {"input": 0, "output": 0, "cached": 0, "total": 0, "reserved": 0},
                        "attempts": [], "reason": "", "patch": "", "resolved_paths": [],
                        "files": {}, "notes": "", "diagnostics": [], "card_notes": [],
                        "policy_hash": context.get("policy") if isinstance(context.get("policy"), str) else None}
        try:
            policy = normalize_policy(context.get("policy"))
        except ReconcileError as exc:
            return self._finish(context, result, str(exc), board_root)
        result["policy"] = {k: policy[k] for k in ("enabled", "max_attempts", "tokens_per_case",
                                                  "tokens_per_day", "context_tokens", "completion_tokens",
                                                  "guest_timeout_seconds")}
        if not policy["enabled"]:
            return self._finish(context, result, "reconciliation is disabled by the project's policy.", board_root)
        if not candidate or not Path(candidate).is_dir():
            return self._finish(context, result, f"candidate_path {candidate!r} is not a directory.", board_root)
        if not target_sha or not submitted_sha:
            return self._finish(context, result, "target_sha and submitted_sha are required.", board_root)
        try:
            if repair:
                paths = repair_paths(candidate, context.get("repo"), context)
                if not paths:
                    return self._finish(context, result, "the submission changed no file this repair may touch.", board_root)
            else:
                paths = conflicted_paths(candidate, context.get("conflicts"))
                if not paths:
                    return self._finish(context, result, "no conflicted paths were found in the candidate.", board_root)
            versions = {p: file_versions(candidate, context.get("repo"), p, base_sha, target_sha, submitted_sha)
                        for p in paths}
            if repair:
                # A path the candidate does not hold (the submission deleted it, or it never
                # existed there) has nothing to repair.
                versions = {p: v for p, v in versions.items() if v.exists}
                if not versions:
                    return self._finish(context, result, "none of the submission's files exist in the candidate.", board_root)
                paths = list(versions)
        except ReconcileError as exc:
            return self._finish(context, result, str(exc), board_root)
        result["conflicts"] = list(paths)
        case_key = ":".join(str(part) for part in (repo_id, job_id or f"{target_sha}:{submitted_sha}", kind)
                            + ((round_number,) if repair and round_number is not None else ()))
        result["case_key"] = case_key
        entries, source = self._entries(context.get("policy"))
        result["list_source"] = source
        if not entries:
            return self._finish(context, result, "the high list is empty: no model is configured for reconciliation.", board_root)
        made = self.ledger.attempts_made(case_key)
        if made >= policy["max_attempts"]:
            return self._finish(context, result, f"attempt budget: {made} attempt(s) already on record for this "
                                                 f"case (max {policy['max_attempts']}); a restart does not earn more.", board_root)
        call = model_call or self.model_call or _default_model_call
        outer_cancel = cancel or threading.Event()
        exclude: set = set()
        feedback: list[str] | None = None
        last_patch = ""
        reason = ""
        for attempt in range(made + 1, policy["max_attempts"] + 1):
            if outer_cancel.is_set():
                raise Cancelled("Stopped.")
            resolved = self._draw(entries, exclude, policy["completion_tokens"])
            if resolved is None:
                reason = ("no model in the high list can take the call here (no stored key, or no guest that runs)"
                          if not exclude else f"no further usable model in the high list after {', '.join(sorted(exclude))}")
                break
            is_guest = model_roles.is_guest_preset(resolved.preset_id)
            record = {"attempt": attempt, "model": resolved.config.model, "preset": resolved.preset_id,
                      "account": account_of(resolved.preset_id), "effort": resolved.effort,
                      "tokens": {"input": 0, "output": 0, "cached": 0, "reserved": 0}, "outcome": "", "diagnostics": []}
            result["attempts"].append(record)
            try:
                system, user = build_prompt(context, list(versions.values()),
                                            context_tokens=policy["context_tokens"], feedback=feedback)
            except ReconcileError as exc:
                record["outcome"] = "too_large"
                reason = str(exc)
                break
            estimate = math.ceil((len(system) + len(user)) / CHARS_PER_TOKEN) + policy["completion_tokens"]
            record["tokens"]["reserved"] = estimate
            try:
                reservation = self.ledger.reserve(repo_id=repo_id, job_id=job_id, case_key=case_key,
                                                  attempt=attempt, tokens=estimate,
                                                  per_case=policy["tokens_per_case"], per_day=policy["tokens_per_day"],
                                                  preset=resolved.preset_id or "", model=resolved.config.model,
                                                  account=record["account"], effort=resolved.effort or "")
            except BudgetExceeded as exc:
                record["outcome"] = "budget"
                reason = str(exc)
                break
            result["tokens"]["reserved"] += estimate
            # One cancel per attempt: the caller's cancel, the guest's wall-clock deadline and the
            # adapter's own budget watch all set it; which one did is read back afterwards.
            attempt_cancel = threading.Event()
            deadline = policy["guest_timeout_seconds"] if is_guest else None
            watcher = _AttemptWatch(outer_cancel, attempt_cancel, deadline, self._now)
            request = {"system": system, "user": user, "resolved": resolved, "model": resolved.config.model,
                       "preset": resolved.preset_id, "account": record["account"], "effort": resolved.effort,
                       "attempt": attempt, "cwd": str(candidate), "conflicts": list(paths), "kind": kind,
                       "completion_tokens": policy["completion_tokens"], "budget_tokens": estimate,
                       "cancel": attempt_cancel, "job_id": job_id, "repair": repair}
            text, usage, extra = "", None, {}
            watcher.start()
            try:
                answer = call(request)
                text, usage, extra = _unpack_answer(answer)
            except OverBudget as exc:
                watcher.stop()
                seen = usage_counts(exc.usage)
                self.ledger.mark_uncertain(reservation, f"over budget: {_clip(str(exc), 160)}",
                                           at_least=(seen[0] + seen[1]) if seen else 0, model=record["model"])
                record["outcome"], record["diagnostics"] = "over_budget", [str(exc)]
                if seen:
                    record["tokens"]["input"], record["tokens"]["output"] = seen
                    result["tokens"]["input"] += seen[0]
                    result["tokens"]["output"] += seen[1]
                reason = f"attempt {attempt} on {resolved.config.model} was cut off: {exc}"
                exclude.add(resolved.preset_id or resolved.config.base_url)
                continue
            except Cancelled:
                watcher.stop()
                if outer_cancel.is_set() or not watcher.timed_out:
                    self.ledger.mark_uncertain(reservation, "cancelled", model=record["model"])
                    raise
                self.ledger.mark_uncertain(reservation, f"timeout after {deadline:g}s", model=record["model"])
                record["outcome"] = "timeout"
                record["diagnostics"] = [f"the guest turn passed {deadline:g}s and was interrupted"]
                reason = f"attempt {attempt} on {resolved.config.model} timed out after {deadline:g}s"
                exclude.add(resolved.preset_id or resolved.config.base_url)
                continue
            except ValueError as exc:
                watcher.stop()
                # `start_provider`'s "could not be started": nothing reached a model.
                self.ledger.release(reservation, f"start failed: {_clip(str(exc), 200)}")
                record["outcome"], record["diagnostics"] = "unavailable", [str(exc)[:300]]
                exclude.add(resolved.preset_id or resolved.config.base_url)
                reason = f"{resolved.preset_id}: {_clip(str(exc), 200)}"
                continue
            except Exception as exc:
                watcher.stop()
                # A transport or harness failure mid-call: what it consumed is unknown, so the
                # reservation stays charged (`uncertain`), and the next draw skips this provider.
                self.ledger.mark_uncertain(reservation, f"error: {_clip(str(exc), 200)}", model=record["model"])
                record["outcome"], record["diagnostics"] = "error", [f"{type(exc).__name__}: {str(exc)[:300]}"]
                exclude.add(resolved.preset_id or resolved.config.base_url)
                reason = f"{resolved.preset_id}: {type(exc).__name__}: {_clip(str(exc), 200)}"
                logs.event(_log, "reconcile_call_failed", level_name="warning", job=job_id,
                           preset=resolved.preset_id, error=str(exc)[:300])
                continue
            watcher.stop()
            if extra.get("model"):
                record["model"] = extra["model"]           # what the guest reported it ran
            if extra.get("session_id"):
                record["session_id"] = extra["session_id"]
            counts = usage_counts(usage)
            if counts is not None:
                record["tokens"]["input"], record["tokens"]["output"] = counts
                record["tokens"]["cached"] = cache_counts(usage).get("cached_tokens", 0)
                result["tokens"]["input"] += counts[0]
                result["tokens"]["output"] += counts[1]
                result["tokens"]["cached"] += record["tokens"]["cached"]
            files, notes, gave_up = parse_reply(text)
            if files is None:
                outcome = "gave_up" if gave_up else "unparseable"
                self.ledger.settle(reservation, usage, outcome, model=record["model"])
                record["outcome"] = outcome
                record["diagnostics"] = [gave_up or notes]
                reason = f"attempt {attempt} on {record['model']}: " + (
                    f"the model declined: {gave_up}" if gave_up else notes)
                feedback = None if gave_up else [notes]
                continue
            problems = review_patch(files, versions, repair=repair)
            if problems:
                self.ledger.settle(reservation, usage, "rejected", model=record["model"])
                record["outcome"], record["diagnostics"] = "rejected", problems
                last_patch = unified_patch(versions, {p: c for p, c in files.items() if p in versions})
                reason = f"attempt {attempt} on {record['model']} was rejected by the safety review: " + \
                         "; ".join(problems[:3]) + (" …" if len(problems) > 3 else "")
                feedback = problems
                continue
            self.ledger.settle(reservation, usage, "resolved", model=record["model"])
            record["outcome"] = "resolved"
            try:
                hashes = apply_files(candidate, files)
            except ReconcileError as exc:
                record["outcome"], record["diagnostics"] = "apply_failed", [str(exc)]
                reason = str(exc)
                break
            result["tokens"]["total"] = result["tokens"]["input"] + result["tokens"]["output"]
            result.update({"status": "resolved", "model": record["model"], "preset": resolved.preset_id,
                           "account": record["account"], "effort": resolved.effort,
                           "patch": unified_patch(versions, files), "resolved_paths": sorted(files),
                           "files": hashes, "notes": notes, "reason": "",
                           "review": "passed: only allowed paths, no markers, assertions and tests kept, "
                                     + ("the target's lines kept" if repair else "both sides' lines kept")})
            result["trailer"] = by_trailer(result)
            result["seconds"] = round(self._now() - started, 3)
            result["card_notes"] = card_notes(context, result)
            if board_root:
                result["notes_written"] = append_card_notes(board_root, result["card_notes"])
            logs.event(_log, "reconcile_resolved", job=job_id, kind=kind, model=result["model"],
                       preset=result["preset"], attempts=attempt, tokens=result["tokens"]["total"], paths=len(files))
            return result
        result["patch"] = last_patch
        result["diagnostics"] = [d for a in result["attempts"] for d in a["diagnostics"]]
        if result["attempts"]:
            last = result["attempts"][-1]
            result.update({"model": last["model"], "preset": last["preset"], "account": last["account"],
                           "effort": last["effort"]})
        return self._finish(context, result, reason or "no attempt could be made.", board_root)

    def _finish(self, context: dict, result: dict, reason: str, board_root) -> dict:
        result["status"] = "author_required"
        result["reason"] = reason
        result["tokens"]["total"] = result["tokens"]["input"] + result["tokens"]["output"]
        result["handoff"] = author_handoff(context, result)
        result["card_notes"] = card_notes(context, result)
        if board_root:
            result["notes_written"] = append_card_notes(board_root, result["card_notes"])
        logs.event(_log, "reconcile_author_required", job=result.get("job_id"), reason=reason[:300],
                   attempts=len(result.get("attempts") or ()))
        return result


def _unpack_answer(answer) -> tuple[str, dict | None, dict]:
    """A `model_call`'s return in any of the shapes a test or adapter finds natural: `(text,
    usage)`, a bare string, or `{"text": …, "usage": …, "model"?: …, "session_id"?: …}` — the
    dict form is how an adapter reports the model that actually answered."""
    if isinstance(answer, tuple) and len(answer) == 2:
        return str(answer[0] or ""), answer[1] if isinstance(answer[1], dict) else None, {}
    if isinstance(answer, dict):
        extra = {k: str(answer[k]) for k in ("model", "session_id") if isinstance(answer.get(k), str) and answer[k]}
        return str(answer.get("text") or answer.get("content") or ""), \
            answer.get("usage") if isinstance(answer.get("usage"), dict) else None, extra
    return str(answer or ""), None, {}


def author_handoff(context: dict, result: dict) -> str:
    """The message the queue gives the author agent with an `author_required` result: what
    conflicted, what was tried, why it was refused, and what to do — resolve in the workspace
    against the target and submit again. Plain text; the patch rides in `result["patch"]`."""
    lines = [f"Landing job {result.get('job_id') or ''} could not be reconciled automatically.",
             f"Target {str(context.get('target_sha') or '')[:12]} and your submission "
             f"{str(context.get('submitted_sha') or '')[:12]} conflict in: "
             + (", ".join(result.get("conflicts") or ()) or "(no paths found)") + ".",
             f"Reason: {result.get('reason') or ''}"]
    for attempt in result.get("attempts") or ():
        tokens = attempt.get("tokens") or {}
        lines.append(f"- attempt {attempt['attempt']}: {attempt.get('model') or '?'} ({attempt.get('preset') or '?'}"
                     + (f", account {attempt['account']}" if attempt.get("account") else "")
                     + f") → {attempt.get('outcome') or '?'}, {tokens.get('input', 0):,} in / {tokens.get('output', 0):,} out")
        for diagnostic in (attempt.get("diagnostics") or [])[:6]:
            lines.append(f"    · {diagnostic}")
    if result.get("patch"):
        lines.append("The last rejected patch is attached (`patch`): read it as a suggestion, not a resolution.")
    lines.append("Sync your workspace to the current target, resolve these files there, run the project's checks, "
                 "and submit the new commit. Nothing was changed in your workspace.")
    return "\n".join(lines)


__all__ = ["Reconciler", "ReconcileError", "BudgetExceeded", "OverBudget", "BudgetLedger", "normalize_policy",
           "context_kind", "repair_paths", "by_trailer", "BY_TRAILER",
           "high_entries", "stored_high_entries", "default_high_entries", "draw_high", "with_high_effort",
           "conflicted_paths", "file_versions", "build_prompt", "parse_reply", "review_patch", "review_file",
           "apply_files", "unified_patch", "card_notes", "append_card_notes", "author_handoff",
           "ledger_path", "default_state_root", "TRAILER"]
