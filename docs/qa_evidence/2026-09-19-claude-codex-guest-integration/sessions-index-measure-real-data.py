#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Read-only measurements of `guest_sessions` against the real claude/codex data.

Records how many sessions exist, how long parsing them takes, what a cold and a warm
`reconcile()` cost, and what tailing a live transcript costs — the numbers the sessions pane's
first-run budget is set from. Nothing here writes to the guests' directories; the only file it
creates is the throwaway index database under a temporary directory.

    python3 docs/qa_evidence/2026-09-19-claude-codex-guest-integration/sessions-index-measure-real-data.py

`--limit N` measures the N newest files per source instead of all of them (the long claude
history is minutes of parsing); `--json` prints the raw measurements.
"""
from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "backend"))

from relay_core import conv_index, guest, guest_sessions  # noqa: E402


def timed(function, *args, **kwargs):
    """`function`'s result and how long it took in milliseconds."""
    started = time.perf_counter()
    result = function(*args, **kwargs)
    return result, (time.perf_counter() - started) * 1000


def tree(root: Path, pattern: str) -> list[Path]:
    return [path for path in root.rglob(pattern) if not path.name.startswith(".")]


def sizes(paths: list[Path]) -> dict:
    total = 0
    lines = 0
    for path in paths:
        try:
            total += path.stat().st_size
            with open(path, "rb") as handle:
                lines += sum(1 for _ in handle)
        except OSError:
            continue
    return {"files": len(paths), "bytes": total, "mib": round(total / 1048576, 1), "lines": lines}


def describe(records: list[dict]) -> dict:
    """Record counts and the shapes a listing would show."""
    if not records:
        return {"records": 0}
    counts = sorted(record["message_count"] for record in records)
    titled = sum(1 for record in records if record["title"])
    kinds: dict[str, int] = {}
    for record in records:
        kinds[record["title_kind"]] = kinds.get(record["title_kind"], 0) + 1
    return {"records": len(records),
            "workspaces": len({record["workspace"] for record in records}),
            "with_title": titled,
            "title_kinds": kinds,
            "messages_total": sum(counts),
            "messages_median": statistics.median(counts),
            "messages_max": counts[-1],
            "empty_sessions": sum(1 for count in counts if count == 0),
            "entries_total": sum(len(record["entries"]) for record in records),
            "no_workspace": sum(1 for record in records if not record["workspace"]),
            "no_id": sum(1 for record in records if not record["id"])}


def measure_source(source: str, home: str, limit: int | None) -> dict:
    """Count the guest's files, then parse them (all of them, or the newest `limit`)."""
    if source == "claude":
        root = Path(guest.claude_projects_dir(home))
        # The session transcripts scan_claude reads, and (for context) the subagent transcripts
        # claude nests below them, which are not sessions of their own.
        files = [path for path in root.glob("*/*.jsonl") if not path.name.startswith(".")]
        folders = len([entry for entry in root.iterdir() if entry.is_dir()]) if root.is_dir() else 0
        nested = [path for path in tree(root, "*.jsonl") if path not in set(files)]
        extras = {"projects": folders, "subagent_transcripts": sizes(nested)}
    else:
        root = Path(guest.codex_sessions_dir(home))
        files = [path for path in tree(root, "*.jsonl")
                 if guest_sessions.CODEX_ROLLOUT.match(path.name)] if root.is_dir() else []
        db = Path(guest.codex_state_db(home) or "")
        meta, db_ms = timed(guest_sessions.codex_thread_meta, db)
        extras = {"threads_db": str(db), "threads_db_exists": db.is_file(),
                  "threads_db_rows": len(meta), "threads_db_ms": round(db_ms, 1)}
    on_disk = sizes(files)
    records, scan_ms = timed(guest_sessions.scan, source, home, limit=limit)
    parsed_files = on_disk["files"] if limit is None else min(limit, on_disk["files"])
    # Throughput of the parse alone: the files it actually read, at the size they are.
    read = sizes(guest_sessions._by_age(files)[:limit or None])["bytes"] if files else 0
    extras.update({"files_on_disk": on_disk, "scan_ms": round(scan_ms, 1),
                   "parsed_mib": round(read / 1048576, 1),
                   "parse_mib_per_s": round(read / 1048576 / (scan_ms / 1000), 2) if scan_ms else None,
                   "ms_per_file": round(scan_ms / parsed_files, 1) if parsed_files else None})
    return {"source": source, **extras, **describe(records)}


def measure_index(home: str, limit: int | None) -> dict:
    """A cold reconcile (empty index) and a warm one (nothing changed), plus search."""
    directory = tempfile.mkdtemp(prefix="relay-measure-")
    index = conv_index.ConversationIndex(path=Path(directory) / "index.db")
    cold, cold_ms = timed(guest_sessions.reconcile, index, home, limit=limit)
    indexed = sum(cold[key] for key in ("added", "refreshed"))
    index_bytes = sum(path.stat().st_size for path in Path(directory).glob("index.db*"))
    warm, warm_ms = timed(guest_sessions.reconcile, index, home, limit=limit)
    listed, list_ms = timed(guest_sessions.list_sessions, index, limit=1000)
    found, search_ms = timed(guest_sessions.search_sessions, index, "the", limit=50)
    live: dict = {}
    for source in guest_sessions.GUEST_SOURCES:
        path = guest_sessions.live_transcript(source, None, home)
        if path is None:
            live[source] = None
            continue
        tail, tail_ms = timed(guest_sessions.LiveTail, path, home=home)
        record = tail.record() or {}
        again, again_ms = timed(tail.refresh)
        live[source] = {"path": str(path), "first_read_ms": round(tail_ms, 1),
                        "no_op_refresh_ms": round(again_ms, 2),
                        "message_count": record.get("message_count"),
                        "id": record.get("id"), "title": record.get("title")}
    return {"cold_reconcile": cold, "cold_ms": cold_ms, "warm_reconcile": warm, "warm_ms": warm_ms,
            "index_bytes": index_bytes, "index_mib": round(index_bytes / 1048576, 2),
            "index_bytes_per_record": int(index_bytes / indexed) if indexed else None,
            "list_ms": round(list_ms, 1), "listed": len(listed),
            "search_ms": round(search_ms, 1), "search_items": len(found["items"]),
            "live_tail": live}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--home", default=os.path.expanduser("~"), help="the home holding .claude/.codex")
    parser.add_argument("--limit", type=int, default=None, help="newest N files per source")
    parser.add_argument("--json", action="store_true", help="print the raw measurements")
    args = parser.parse_args()

    measurements = {"home": args.home, "limit": args.limit,
                    "claude": measure_source("claude", args.home, args.limit),
                    "codex": measure_source("codex", args.home, args.limit),
                    "index": measure_index(args.home, args.limit)}
    if args.json:
        print(json.dumps(measurements, indent=2, sort_keys=True))
        return 0

    for source in ("claude", "codex"):
        data = measurements[source]
        print(f"== {source}")
        print(f"   files on disk: {data['files_on_disk']['files']} "
              f"({data['files_on_disk']['mib']} MiB, {data['files_on_disk']['lines']} lines)")
        for key in ("projects", "subagent_transcripts", "threads_db", "threads_db_exists",
                    "threads_db_rows", "threads_db_ms"):
            if key in data:
                print(f"   {key}: {data[key]}")
        print(f"   records: {data['records']}  workspaces: {data['workspaces']}  "
              f"titled: {data['with_title']}  no id: {data['no_id']}  no workspace: {data['no_workspace']}")
        print(f"   messages: {data['messages_total']} total, median {data['messages_median']}, "
              f"max {data['messages_max']}, empty {data['empty_sessions']}  "
              f"entries: {data['entries_total']}")
        print(f"   title kinds: {data['title_kinds']}")
        print(f"   scan: {data['scan_ms']} ms over {data['parsed_mib']} MiB "
              f"({data['parse_mib_per_s']} MiB/s, {data['ms_per_file']} ms/file)")
    index = measurements["index"]
    print("== index")
    print(f"   cold reconcile: {index['cold_ms']:.0f} ms  {index['cold_reconcile']}")
    print(f"   warm reconcile: {index['warm_ms']:.0f} ms  {index['warm_reconcile']}")
    print(f"   index size: {index['index_mib']} MiB ({index['index_bytes_per_record']} bytes/record)")
    print(f"   list 1000: {index['list_ms']} ms -> {index['listed']} records")
    print(f"   search 'the': {index['search_ms']} ms -> {index['search_items']} items")
    for source, tail in index["live_tail"].items():
        print(f"   live tail {source}: {tail}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
