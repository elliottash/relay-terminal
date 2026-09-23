# SPDX-License-Identifier: AGPL-3.0-or-later
"""Offer the user's Claude Code and Codex memories to Relay as suggestions (#MEMS step 5).

Owner, 2026-09-22: "on startup, relay can get the memories from claude and codex and import
them." They arrive as *suggestions* through `memory_suggestions.suggest` — never straight into
user memory — so the person confirms each one in Globals, one review list for the whole batch,
and a rejected fact is declined there however often it is offered again.

What is read, and only this:

* Claude Code: `~/.claude/CLAUDE.md` (the user's own instructions, split into one fact per
  top-level bullet or short paragraph) and `~/.claude/projects/*/memory/*.md` whose front matter
  type is `user` or `feedback`. `project` and `reference` memories are about one repository and
  stay where they are; `MEMORY.md` is only the index.
* Codex: `~/.codex/AGENTS.md` (and `AGENTS.override.md`), split the same way, and the
  `## User Profile` / `## User preferences` sections of `~/.codex/memories/memory_summary.md` —
  the consolidated cross-task summary Codex's memory phase 2 writes (first line `v1`). Its
  `MEMORY.md` task blocks, `raw_memories.md` and `rollout_summaries/` are per-rollout notes and
  are not read, nor is the `memories_1.sqlite` stage-1 queue.

Lines that look like credentials are dropped before anything is split. `imported.json` in the
global memory folder remembers each source file's hash and the hash of every fact already offered
from it: an unchanged file is not even parsed on the next start, and a changed one offers only
facts it has not offered before. The whole run holds a lock, so the workers of several panes
starting at once import once between them.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
import threading
from datetime import date
from pathlib import Path

from . import aliases, board, logs
from .filelock import LOCK_EX, LOCK_UN, flock

LEDGER = "imported.json"
LOCK_NAME = ".import.lock"
FACT_CAP = 400             # longer than this is a document, not a fact to confirm
MIN_FACT = 12              # "Rules:" and friends
FILE_CAP = 256 * 1024
MAX_MEMORY_FILES = 2000
CLAUDE_TYPES = {"user", "feedback"}
CODEX_SECTIONS = {"user profile", "user preferences"}
ENV_SWITCH = "RELAY_MEMORY_IMPORT"

log = logs.get("memory_import")

# Anything that reads as a credential. `logs.scrub` has the provider-key shapes; these add the
# `name = value` forms it misses (`OPENAI_API_KEY=` has no word boundary before `API`) and the
# common token prefixes. A line that matches is dropped, never shortened.
_SECRET_LINE = (
    re.compile(r"(?i)(api[_-]?key|access[_-]?key|secret|token|passw(?:or)?d|passwd|credential|"
               r"private[_-]?key|auth(?:orization)?)[\w-]*[\"']?\s*[=:]\s*[\"']?[^\s\"'`]{6,}"),
    re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"),
    re.compile(r"\b(?:ghp|gho|ghs|ghu|github_pat)_[A-Za-z0-9_]{16,}"),
    re.compile(r"\bAKIA[0-9A-Z]{16}\b"),
    re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}"),
    re.compile(r"\bAIza[0-9A-Za-z_\-]{30,}"),
)
_BULLET = re.compile(r"^(?:[-*+]|\d{1,3}[.)])\s+")
_FENCE = re.compile(r"^\s*(```|~~~)")
_WIKILINK = re.compile(r"\[\[([^\]|]+)(?:\|([^\]]+))?\]\]")
_SEE_ALSO = re.compile(r"\s*(?:See|Related|Also see)[: ]+(?:\[\[[^\]]+\]\][,; and]*\s*)+\.?\s*$")
_TASK_CITE = re.compile(r"(?:\s*\[Task \d+\])+\s*$")


def enabled(env=None) -> bool:
    value = (env if env is not None else os.environ).get(ENV_SWITCH, "").strip().lower()
    return value not in ("off", "0", "false", "no")


def looks_secret(line: str) -> bool:
    return logs.scrub(line) != line or any(p.search(line) for p in _SECRET_LINE)


def fact_key(text: str) -> str:
    """The hash a fact is remembered by: case and whitespace do not make a new fact."""
    return hashlib.sha256(" ".join(text.split()).casefold().encode("utf-8")).hexdigest()[:32]


# ------------------------------------------------------------------------------- splitting

def _strip_front_matter(text: str) -> tuple[dict, str]:
    if not text.startswith("---"):
        return {}, text
    end = text.find("\n---", 3)
    if end < 0:
        return {}, text
    head, body = text[3:end], text[end + 4:].lstrip("\n")
    try:
        front = board.parse_yaml(head)
    except Exception:
        # Claude writes plain `key: value` lines; take what can be read that way.
        front = {}
        for line in head.splitlines():
            key, sep, value = line.strip().partition(":")
            if sep and value.strip():
                front.setdefault(key.strip(), value.strip().strip("\"'"))
    return (front if isinstance(front, dict) else {}), body


def _clean(text: str) -> str:
    text = _SEE_ALSO.sub("", text.strip())
    text = _WIKILINK.sub(lambda m: m.group(2) or m.group(1), text)
    text = _TASK_CITE.sub("", text)
    return text.strip()


def _drop_secrets(lines, counter):
    kept = []
    for line in lines:
        if looks_secret(line):
            counter[0] += 1
        else:
            kept.append(line)
    return kept


def split_facts(text: str, counter=None) -> list[str]:
    """One fact per top-level bullet (its nested lines folded in) or plain paragraph.

    Headings, fenced code, tables, HTML comments, `@path` imports and lead-in lines ending in a
    colon are not facts; anything over FACT_CAP is left out rather than cut.
    """
    counter = counter if counter is not None else [0]
    _, text = _strip_front_matter(text)
    lines = _drop_secrets(text.splitlines(), counter)
    facts, current = [], []
    in_fence = in_comment = False

    def flush():
        if current:
            fact = _clean("".join(current))
            if MIN_FACT <= len(fact) <= FACT_CAP and not fact.endswith(":"):
                facts.append(fact)
            current.clear()

    for raw in lines:
        line = raw.rstrip()
        stripped = line.strip()
        if _FENCE.match(line):
            flush()
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if in_comment or stripped.startswith("<!--"):
            in_comment = "-->" not in stripped
            flush()
            continue
        if not stripped:
            flush()
            continue
        if stripped.startswith(("#", "|", "@", ">")) or re.fullmatch(r"[-*_=]{3,}", stripped):
            flush()
            continue
        top_bullet = line[:1] not in (" ", "\t") and _BULLET.match(line)
        if top_bullet:
            flush()
            current.append(_BULLET.sub("", line, count=1))
        elif current and _BULLET.match(stripped):
            current.append("\n" + stripped)     # a nested bullet keeps its own line
        else:
            # A hard-wrapped line of a paragraph or bullet: Markdown reads the break as a space.
            current.append((" " if current else "") + stripped)
    flush()
    return facts


# -------------------------------------------------------------------------------- sources

def _paragraphs(body: str) -> list[str]:
    return [" ".join(p.split()) for p in re.split(r"\n\s*\n", body) if p.strip()]


def _first_sentence(text: str) -> str:
    match = re.match(r"(.+?[.!?])(\s|$)", text, re.S)
    return (match.group(1) if match else text).strip()


def claude_memory(text: str, counter=None) -> list[dict]:
    """A `type: user|feedback` memory file as at most one fact; anything else as none.

    The fact is the front matter's `description` — Claude writes it as the one-line summary it
    recalls the memory by, where the body usually opens with the dated story of how it was
    learned — and the first body paragraph when there is no usable description. The first
    sentence of `**Why:**` is folded in when it fits; `**How to apply:**` is left out.
    """
    counter = counter if counter is not None else [0]
    front, body = _strip_front_matter(text)
    meta = front.get("metadata") if isinstance(front.get("metadata"), dict) else {}
    kind = str(meta.get("type") or front.get("type") or "").strip().lower()
    if kind not in CLAUDE_TYPES:
        return []
    body = "\n".join(_drop_secrets(body.splitlines(), counter))
    rule = why = ""
    for para in _paragraphs(body):
        if para.startswith("#"):
            continue
        label = re.match(r"\*\*(Why|How to apply)[:*]*\*\*:?\s*", para)
        if label:
            if label.group(1) == "Why" and not why:
                why = _first_sentence(para[label.end():])
            continue
        if not rule:
            rule = _clean(para)
    description = _clean(" ".join(str(front.get("description") or "").split()))
    if looks_secret(description):
        counter[0] += 1
        description = ""
    fact = next((text for text in (description, rule) if MIN_FACT <= len(text) <= FACT_CAP), "")
    if not fact:
        return []
    if why:
        folded = f"{fact if fact[-1] in '.!?' else fact + '.'} Why: {_clean(why)}"
        fact = folded if len(folded) <= FACT_CAP else fact
    name = str(front.get("name") or "").strip() or None
    return [{"fact": fact, "name": name, "title": None}]


def codex_summary(text: str, counter=None) -> list[str]:
    """The user-level sections of Codex's `memory_summary.md`, one fact per bullet/paragraph."""
    counter = counter if counter is not None else [0]
    lines = text.splitlines()
    if lines and lines[0].strip() == "v1":
        lines = lines[1:]
    keep, section = [], None
    for line in lines:
        heading = re.match(r"^(#{1,3})\s+(.*?)\s*#*\s*$", line)
        if heading and len(heading.group(1)) <= 2:
            section = heading.group(2).strip().casefold()
            keep.append("")
            continue
        if section in CODEX_SECTIONS:
            keep.append(line)
    return split_facts("\n".join(keep), counter)


def _claude_dir(home):
    if home is None and os.environ.get("CLAUDE_CONFIG_DIR"):
        return Path(os.environ["CLAUDE_CONFIG_DIR"]).expanduser()
    return Path(home or Path.home()) / ".claude"


def _codex_dir(home):
    if home is None and os.environ.get("CODEX_HOME"):
        return Path(os.environ["CODEX_HOME"]).expanduser()
    return Path(home or Path.home()) / ".codex"


def sources(home=None):
    """(source, path, reader) for every file this machine has, in a stable order."""
    claude, codex = _claude_dir(home), _codex_dir(home)
    found = [("claude", claude / "CLAUDE.md", "instructions")]
    projects = claude / "projects"
    if projects.is_dir():
        memory_files = sorted(p for p in projects.glob("*/memory/*.md") if p.name != "MEMORY.md")
        found += [("claude", p, "claude_memory") for p in memory_files[:MAX_MEMORY_FILES]]
    found += [("codex", codex / "AGENTS.md", "instructions"),
              ("codex", codex / "AGENTS.override.md", "instructions"),
              ("codex", codex / "memories" / "memory_summary.md", "codex_summary")]
    return [(source, path, reader) for source, path, reader in found if path.is_file()]


def read_candidates(reader: str, text: str, counter) -> list[dict]:
    if reader == "claude_memory":
        return claude_memory(text, counter)
    facts = codex_summary(text, counter) if reader == "codex_summary" else split_facts(text, counter)
    return [{"fact": fact, "name": None, "title": None} for fact in facts]


# --------------------------------------------------------------------------------- ledger

def _memory_root() -> Path:
    return aliases.global_root() / "memory"


def _load_ledger(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) and isinstance(data.get("files"), dict) else {"files": {}}
    except (OSError, ValueError):
        return {"files": {}}


def _save_ledger(path: Path, ledger: dict):
    fd, tmp = tempfile.mkstemp(prefix=".imported-", suffix=".json", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(ledger, handle, indent=1, sort_keys=True)
            handle.write("\n")
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


# --------------------------------------------------------------------------------- import

def import_all(*, home=None, suggest=None) -> dict:
    """Offer every fact not offered before; returns the counts.

    `claude` / `codex` are the new pending suggestions from each; `declined` matched a rejected
    suggestion, `duplicate` an existing memory or pending suggestion; `skipped` were offered on an
    earlier start (or left out as too long); `unchanged` files were not re-read at all; `secrets`
    lines were dropped as credentials; `errors` facts failed to save and will be tried again.
    """
    if suggest is None:
        from .memory_suggestions import suggest            # part A of #MEMS
    summary = {"claude": 0, "codex": 0, "declined": 0, "duplicate": 0, "skipped": 0,
               "unchanged": 0, "secrets": 0, "errors": 0}
    found = sources(home)
    if not found:
        return summary
    root = _memory_root()
    root.mkdir(parents=True, exist_ok=True)
    lock_fd = os.open(root / LOCK_NAME, os.O_CREAT | os.O_RDWR, 0o600)
    try:
        flock(lock_fd, LOCK_EX)
        ledger_path = root / LEDGER
        ledger = _load_ledger(ledger_path)
        files = ledger["files"]
        for source, path, reader in found:
            key = str(path)
            try:
                if path.stat().st_size > FILE_CAP:
                    continue
                data = path.read_bytes()
            except OSError:
                continue
            digest = hashlib.sha256(data).hexdigest()
            entry = files.get(key) if isinstance(files.get(key), dict) else {}
            if entry.get("sha256") == digest:
                summary["unchanged"] += 1
                continue
            offered = set(entry.get("facts") or [])
            counter = [0]
            try:
                candidates = read_candidates(reader, data.decode("utf-8", "replace"), counter)
            except Exception:
                log.warning("memory_import: could not read %s", key, exc_info=True)
                continue
            summary["secrets"] += counter[0]
            failed = False
            for item in candidates:
                hashed = fact_key(item["fact"])
                if hashed in offered:
                    summary["skipped"] += 1
                    continue
                try:
                    result = suggest(item["fact"], name=item["name"], title=item["title"],
                                     source=source, origin=key)
                except Exception:
                    log.warning("memory_import: suggest failed for %s", key, exc_info=True)
                    summary["errors"] += 1
                    failed = True
                    continue
                offered.add(hashed)
                status = (result or {}).get("status")
                if status == "pending":
                    summary[source] += 1
                elif status in ("declined", "duplicate"):
                    summary[status] += 1
            # A failed save leaves the old hash, so the next start reads the file again; what
            # was offered is kept either way, so it is not offered twice.
            files[key] = {"sha256": entry.get("sha256", "") if failed else digest,
                          "imported": date.today().isoformat(), "source": source,
                          "facts": sorted(offered)}
            _save_ledger(ledger_path, ledger)
    finally:
        try:
            flock(lock_fd, LOCK_UN)
        finally:
            os.close(lock_fd)
    return summary


def start(emit=None, *, home=None, env=None):
    """Run `import_all` once, on a daemon thread. Never raises and never blocks.

    A failure is one log line. When anything new is waiting, `emit` gets
    `{"event": "memory_import", "claude": n, "codex": n}`. `RELAY_MEMORY_IMPORT=off` in the
    environment makes this a no-op that returns None.
    """
    if not enabled(env):
        return None

    def work():
        try:
            summary = import_all(home=home)
        except Exception:
            log.warning("memory_import failed", exc_info=True)
            return
        logs.event(log, "memory_import", **summary)
        if emit is not None and (summary["claude"] or summary["codex"]):
            try:
                emit({"event": "memory_import", "claude": summary["claude"],
                      "codex": summary["codex"]})
            except Exception:
                log.warning("memory_import: emit failed", exc_info=True)

    thread = threading.Thread(target=work, name="relay-memory-import", daemon=True)
    thread.start()
    return thread


class StartupImport:
    """The worker's side: one import per process, begun by its first `configure` (protocol 34).

    `configure.memory_import` is optional. `false` skips the import (the GUI's "import Claude and
    Codex memories" option turned off); `true` runs it and asks for the `memory_import` event;
    absent runs it without the event, so a GUI or test that has never heard of the event does not
    receive an unrequested line in the middle of the answers it is counting. The event waits for a
    `true` that arrives after the import finished, and is sent at most once.
    """

    def __init__(self, emit, *, home=None, env=None):
        self.emit, self.home, self.env = emit, home, env
        self.lock = threading.Lock()
        self.started = self.wanted = False
        self.event = None
        self.thread = None

    def configure(self, value):
        if value is not None and not isinstance(value, bool):
            raise ValueError("memory_import must be true or false.")
        with self.lock:
            self.wanted = self.wanted or value is True
            self._flush()
            if value is False or self.started:
                return
            self.started = True
        self.thread = start(self._done, home=self.home, env=self.env)

    def _done(self, event):
        with self.lock:
            self.event = event
            self._flush()

    def _flush(self):
        if self.wanted and self.event is not None:
            event, self.event = self.event, None
            self.emit(event)
