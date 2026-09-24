# SPDX-License-Identifier: AGPL-3.0-or-later
"""The case ledger (card #95VZ): every served case, one JSON line in `<board>/cases.jsonl`.

A card is built; a case is served (design #1QKM §1).  A program serves a case algorithmically,
a skill serves it intelligently, and a person serves it by hand — and all three go in the same
record, because the record is what says what to build next: the third ad-hoc case of one shape
is the signal to file a card that builds a server for it.  The ledger is also the evaluation
data: per-skill pass rates, the `stale` flag, and the counts the QA policy floor
(`relay_core.qa_policy`, `ai_may_gate_after` / `sample_after`) compares against.

Owner steer, 2026-09-23: "ideally, most of this is just in the agent's work and the user doesn't
see it directly."  So nothing here draws anything.  Rows are written by the worker at the seams
where a case is known to have been served (`board_tools.BoardTools`, `tryit_protocol`, the end
of an agent turn that loaded a profiled skill) and by the agent's `board_case` tool for a case a
person served; they are read back through `board_list {cases: true}` and as four statistics on
every `skills_list` item that has a profile.

**One row** (the keys of `FIELDS`, in that order)::

    {"id": "c-8f3a2b1c", "when": "2026-09-23T14:02:11Z", "server": "referee-report",
     "server_version": "sha256:…", "served_by": "person", "card": "K7Q2",
     "input": "Referee-Work/2026/EJ-1234.pdf", "cost": {"seconds": 10800},
     "signal": {"mode": "person", "result": "pass"}, "escalated": {"yes": false},
     "verdict": {"result": "pass", "who": "person", "revision": 1}, "confidential": false}

`server` is a skill id, a program path, or the word `person`; `server_version` is the sha256 of
the skill's SKILL.md, a program's path plus the repository HEAD, or `person`.  `served_by` is
`person` or a model signature (`qa_verifiers.signature`).  `input` is a path or a one-line
reference, never content, and a row written under a `confidential: yes` profile carries no
`input` at all — only ids.  Rows are append-only under the board directory's own lock, the same
discipline `board.append_to_thread` uses, so two panes writing at once both land.
"""
from __future__ import annotations

import datetime as _dt
import hashlib
import json
import os
import re
import secrets
import subprocess
from pathlib import Path

from . import filelock as fcntl

CASES_FILE = "cases.jsonl"
FIELDS = ("id", "when", "server", "server_version", "served_by", "card", "input", "cost",
          "signal", "escalated", "verdict", "confidential")
VERDICTS = ("pass", "fail", "pending")
PERSON = "person"
#: How long a passing case keeps a skill fresh, by the profile's `rot` (#1QKM §7): a
#: capability that depends on an external UI or a model version goes stale in a week, one
#: that depends on a document format in a season.
ROT_DAYS = {"low": 90, "medium": 30, "high": 7}
DEFAULT_ROT = "low"
#: The pass rate is over the last this-many decided cases (pass or fail; pending is not a decision).
PASS_RATE_WINDOW = 30
#: The third person-served case of one server inside this many days is the signal (owner,
#: 2026-09-23): one optional line in the agent's reply, never a card.
THIRD_CASE_DAYS = 90
THIRD_CASE_COUNT = 3
MAX_ROWS = 20000
MAX_LINE = 4096
MAX_REF = 200
MAX_LIMIT = 200
_ID_RE = re.compile(r"^c-[0-9a-f]{8}$")
_COST_RE = re.compile(r"^\s*(?:\$\s*)?(\d+(?:\.\d+)?)\s*([a-zA-Z$€£]*)\s*$")


class CaseError(ValueError):
    """A record that is not one the ledger takes; the message says which field."""


# ----- times ----------------------------------------------------------------------------------

def now_iso(now: float | None = None) -> str:
    """UTC, seconds, `Z`: sortable as text and the same on every platform."""
    stamp = _dt.datetime.now(_dt.timezone.utc) if now is None \
        else _dt.datetime.fromtimestamp(now, _dt.timezone.utc)
    return stamp.strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_when(text) -> _dt.datetime | None:
    """A row's `when` as an aware datetime, or None for anything that is not one."""
    if not isinstance(text, str) or not text:
        return None
    try:
        parsed = _dt.datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        return None
    return parsed if parsed.tzinfo is not None else parsed.replace(tzinfo=_dt.timezone.utc)


def _now(now) -> _dt.datetime:
    if isinstance(now, _dt.datetime):
        return now if now.tzinfo is not None else now.replace(tzinfo=_dt.timezone.utc)
    if isinstance(now, (int, float)):
        return _dt.datetime.fromtimestamp(now, _dt.timezone.utc)
    return _dt.datetime.now(_dt.timezone.utc)


# ----- versions -------------------------------------------------------------------------------

def skill_version(manifest: str | os.PathLike) -> str:
    """`sha256:<hex>` of a skill's SKILL.md — the version of the intelligence that served the
    case.  "" when the file cannot be read: a row is still worth writing without it."""
    try:
        data = Path(manifest).read_bytes()
    except OSError:
        return ""
    return "sha256:" + hashlib.sha256(data).hexdigest()


def program_version(path: str | os.PathLike, repo: str | os.PathLike | None = None) -> str:
    """`<path>@<HEAD>` for a program in a repository, or the path alone when git says nothing."""
    text = str(path)
    if repo is None:
        return text
    try:
        done = subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"], capture_output=True,
                              text=True, timeout=10, check=False)
    except (OSError, ValueError, subprocess.SubprocessError):
        return text
    head = done.stdout.strip() if done.returncode == 0 else ""
    return f"{text}@{head[:12]}" if head else text


# ----- the record ------------------------------------------------------------------------------

def _ref(value, what: str) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise CaseError(f"{what} must be a one-line string.")
    text = " ".join(value.split())
    if not text:
        return None
    if len(text) > MAX_REF:
        raise CaseError(f"{what} is a reference of at most {MAX_REF} characters, never content.")
    return text


def parse_cost(value) -> dict:
    """`{tokens, seconds, money}` (each optional, non-negative numbers) from an object or from a
    short phrase an agent would type: `"3 h"`, `"45 min"`, `"20s"`, `"$12"`, `"1200 tokens"`."""
    if value is None:
        return {}
    if isinstance(value, str):
        match = _COST_RE.match(value)
        if not match:
            raise CaseError("cost must be {tokens, seconds, money} or a phrase like '3 h', "
                            "'45 min', '$12' or '1200 tokens'.")
        number, unit = float(match.group(1)), match.group(2).lower()
        if value.strip().startswith("$") or unit in ("$", "usd", "€", "£", "eur", "gbp", "chf"):
            return {"money": number}
        if unit in ("h", "hr", "hrs", "hour", "hours"):
            return {"seconds": number * 3600}
        if unit in ("m", "min", "mins", "minute", "minutes"):
            return {"seconds": number * 60}
        if unit in ("", "s", "sec", "secs", "second", "seconds"):
            return {"seconds": number}
        if unit in ("tok", "tokens", "token", "t"):
            return {"tokens": int(number)}
        if unit in ("d", "day", "days"):
            return {"seconds": number * 86400}
        raise CaseError(f"cost unit {match.group(2)!r} is not one of h, min, s, d, tokens or $.")
    if not isinstance(value, dict):
        raise CaseError("cost must be {tokens, seconds, money} or a short phrase.")
    out: dict = {}
    for key in ("tokens", "seconds", "money"):
        if key not in value or value[key] is None:
            continue
        item = value[key]
        if isinstance(item, bool) or not isinstance(item, (int, float)) or item < 0:
            raise CaseError(f"cost.{key} must be a non-negative number.")
        out[key] = int(item) if key == "tokens" else float(item)
    unknown = [k for k in value if k not in ("tokens", "seconds", "money")]
    if unknown:
        raise CaseError(f"cost has no key {unknown[0]!r}; the keys are tokens, seconds, money.")
    return out


def _verdict(value, default_who: str) -> dict:
    """`{result, who, revision}`; a bare word is the result with the writer as `who`."""
    if value is None:
        return {"result": "pending", "who": default_who, "revision": 1}
    if isinstance(value, str):
        value = {"result": value}
    if not isinstance(value, dict):
        raise CaseError("verdict must be pass, fail or pending, or {result, who, revision}.")
    result = str(value.get("result") or "pending").strip().lower()
    if result not in VERDICTS:
        raise CaseError(f"verdict.result must be one of {', '.join(VERDICTS)}.")
    who = _ref(value.get("who"), "verdict.who") or default_who
    revision = value.get("revision", 1)
    if isinstance(revision, bool) or not isinstance(revision, int) or revision < 1:
        raise CaseError("verdict.revision must be a whole number from 1.")
    return {"result": result, "who": who, "revision": revision}


def _signal(value) -> dict:
    """`{mode, result}`: the verify primary mode that produced the signal and what it said."""
    if value is None:
        return {}
    if isinstance(value, str):
        value = {"mode": value}
    if not isinstance(value, dict):
        raise CaseError("signal must be {mode, result}.")
    out = {}
    mode = _ref(value.get("mode"), "signal.mode")
    if mode:
        out["mode"] = mode.lower()
    result = _ref(value.get("result"), "signal.result")
    if result:
        out["result"] = result.lower()
    return out


def _escalated(value) -> dict:
    if value is None or value is False:
        return {"yes": False}
    if value is True:
        return {"yes": True}
    if isinstance(value, str):
        to = _ref(value, "escalated")
        return {"yes": True, "to": to} if to else {"yes": False}
    if not isinstance(value, dict):
        raise CaseError("escalated must be true, false, a name, or {yes, to}.")
    yes = bool(value.get("yes"))
    to = _ref(value.get("to"), "escalated.to")
    return {"yes": yes, **({"to": to} if yes and to else {})}


def new_record(server, *, served_by, server_version="", card=None, input=None, cost=None,
               signal=None, escalated=None, verdict=None, confidential=False,
               when: str | None = None, id: str | None = None) -> dict:
    """One validated row.  Raises `CaseError` naming the field that is wrong.

    `confidential` drops `input` (the one field that could name a patient, a client or a
    manuscript), so the row carries only ids and numbers.
    """
    server_text = _ref(server, "server")
    if not server_text:
        raise CaseError("server is required: a skill id, a program path, or 'person'.")
    who = _ref(served_by, "served_by") or PERSON
    row = {"id": id if isinstance(id, str) and _ID_RE.match(id) else f"c-{secrets.token_hex(4)}",
           "when": when if isinstance(when, str) and parse_when(when) else now_iso(),
           "server": server_text,
           "server_version": (_ref(server_version, "server_version") or
                              (PERSON if server_text == PERSON else "")),
           "served_by": who,
           "card": _card_id(card),
           "input": None if confidential else _ref(input, "input"),
           "cost": parse_cost(cost),
           "signal": _signal(signal),
           "escalated": _escalated(escalated),
           "verdict": _verdict(verdict, who),
           "confidential": bool(confidential)}
    return row


def _card_id(value) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lstrip("#").upper()
    if not text:
        return None
    if not re.match(r"^[A-Z0-9]{2,8}$", text):
        raise CaseError("card must be a card id like K7Q2.")
    return text


def row_text(record: dict) -> str:
    """The row as one line of JSON, keys in `FIELDS` order, `input` absent from a
    confidential row rather than null."""
    ordered = {key: record.get(key) for key in FIELDS}
    if ordered["confidential"]:
        ordered.pop("input", None)
    elif ordered.get("input") is None:
        ordered.pop("input", None)
    if ordered.get("card") is None:
        ordered.pop("card", None)
    line = json.dumps(ordered, ensure_ascii=False, separators=(",", ":"))
    if len(line.encode("utf-8")) > MAX_LINE:
        raise CaseError(f"a case row is at most {MAX_LINE} bytes.")
    return line


# ----- the file ---------------------------------------------------------------------------------

def ledger_path(board_dir: str | os.PathLike) -> Path:
    return Path(board_dir) / CASES_FILE


def append(board_dir: str | os.PathLike, record: dict) -> dict:
    """Append one validated row under the board directory's lock and return it.

    The lock is the same one `board.append_to_thread` takes for a thread — the directory's
    own inode, which no writer replaces — so a thread entry and a case row from two panes
    never interleave.  The write is one `O_APPEND` line: nobody watches this file for
    changes the way the pane watches `threads/`, and an append-only file is what a
    `merge=union` git merge keeps whole.
    """
    board = Path(board_dir)
    board.mkdir(parents=True, exist_ok=True)
    line = row_text(record)
    lock = fcntl.open_directory_lock(board)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = ledger_path(board)
        prefix = ""
        if path.exists() and path.stat().st_size:
            with path.open("rb") as handle:
                handle.seek(-1, os.SEEK_END)
                prefix = "" if handle.read(1) == b"\n" else "\n"
        with path.open("ab") as handle:
            handle.write((prefix + line + "\n").encode("utf-8"))
            handle.flush()
            os.fsync(handle.fileno())
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        os.close(lock)
    return record


def read(board_dir: str | os.PathLike, *, server: str | None = None, card: str | None = None,
         limit: int | None = None, include_confidential: bool = True) -> list[dict]:
    """The rows, oldest first, the last `limit` of those matching `server` / `card`.

    A line that is not a JSON object with an `id` and a `server` is skipped, never fatal: a
    hand-edited ledger still reads.  `include_confidential=False` leaves confidential rows out
    entirely, which is how a pane whose workspace is not this board sees the ledger.
    """
    path = ledger_path(board_dir)
    try:
        data = path.read_bytes()
    except OSError:
        return []
    want_card = _card_id(card) if card else None
    want_server = " ".join(str(server).split()) if server else None
    rows: list[dict] = []
    for raw in data.decode("utf-8", "replace").splitlines()[-MAX_ROWS:]:
        raw = raw.strip()
        if not raw:
            continue
        try:
            row = json.loads(raw)
        except ValueError:
            continue
        if not isinstance(row, dict) or not isinstance(row.get("id"), str) \
                or not isinstance(row.get("server"), str):
            continue
        if want_server and row["server"] != want_server:
            continue
        if want_card and str(row.get("card") or "").upper() != want_card:
            continue
        if not include_confidential and row.get("confidential"):
            continue
        rows.append(row)
    if limit is not None:
        rows = rows[-max(0, int(limit)):] if limit > 0 else []
    return rows


def count(board_dir: str | os.PathLike, *, server: str | None = None) -> int:
    return len(read(board_dir, server=server))


# ----- statistics --------------------------------------------------------------------------------

def _result(row: dict) -> str:
    verdict = row.get("verdict")
    if isinstance(verdict, dict):
        return str(verdict.get("result") or "").lower()
    return str(verdict or "").lower()


def verified_count(records, server: str | None = None) -> int:
    """How many rows carry a `pass` verdict — for `server`, or across all rows when None.  This
    is the number `qa_policy`'s `ai_may_gate_after` / `sample_after` compare against."""
    return sum(1 for row in records
               if _result(row) == "pass" and (server is None or row.get("server") == server))


def stats(records, skill: str, profile: dict | None = None, *, now=None) -> dict:
    """The four numbers a `skills_list` item carries for a profiled skill.

    `cases` is every row served by `skill`; `last_served` its newest `when`; `pass_rate_30`
    the share of passes among the last `PASS_RATE_WINDOW` decided rows (pass or fail; None
    when none has been decided); `stale` whether the newest passing row is older than the
    profile's `rot` allows (`ROT_DAYS`; `low` when the profile says nothing).  A skill with no
    row at all is not stale — there is nothing to have gone off — it is simply uncounted.
    """
    mine = [row for row in records if row.get("server") == skill]
    whens = sorted((row["when"] for row in mine if parse_when(row.get("when"))), key=str)
    decided = [_result(row) for row in mine if _result(row) in ("pass", "fail")][-PASS_RATE_WINDOW:]
    rate = round(sum(1 for r in decided if r == "pass") / len(decided), 3) if decided else None
    rot = str((profile or {}).get("rot") or DEFAULT_ROT).lower()
    days = ROT_DAYS.get(rot, ROT_DAYS[DEFAULT_ROT])
    passed = [parse_when(row.get("when")) for row in mine if _result(row) == "pass"]
    passed = [p for p in passed if p is not None]
    stale = False
    if mine:
        last_pass = max(passed) if passed else None
        stale = last_pass is None or (_now(now) - last_pass) > _dt.timedelta(days=days)
    return {"cases": len(mine), "last_served": whens[-1] if whens else None,
            "pass_rate_30": rate, "stale": stale}


def third_case_hint(records, server: str, *, days: int = THIRD_CASE_DAYS, now=None) -> bool:
    """True exactly when the person-served rows for `server` inside the last `days` number
    `THIRD_CASE_COUNT`: the one moment the agent may add its line ("three cases like this;
    build a server?").  Not at the fourth, not at the tenth: said once."""
    since = _now(now) - _dt.timedelta(days=days)
    seen = 0
    for row in records:
        if row.get("server") != server or row.get("served_by") != PERSON:
            continue
        when = parse_when(row.get("when"))
        if when is not None and when >= since:
            seen += 1
    return seen == THIRD_CASE_COUNT


def hint_line(server: str) -> str:
    """The optional sentence for the agent's reply (owner, 2026-09-23): the agent's choice from
    the tool result, never printed by the worker."""
    return f"three cases of {server} served by a person in {THIRD_CASE_DAYS} days; build a server? (/deliver)"


def verdict_from_text(text: str) -> str:
    """`pass` / `fail` / `pending` from the first decisive word of a `## Verdict` section —
    "Pass.", "**FAIL**: the gate …", "passed", "failed" — reading only its opening."""
    head = str(text or "")[:400].lower()
    match = re.search(r"\b(pass|passed|passes|fail|failed|fails)\b", head)
    if not match:
        return "pending"
    return "pass" if match.group(1).startswith("pass") else "fail"
