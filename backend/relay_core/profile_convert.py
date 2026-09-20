# SPDX-License-Identifier: AGPL-3.0-or-later
"""Profiles in, a table out — the arithmetic behind `scripts/relay-profile` (card #7BM4).

Every profiling product shows a table before the flame graph (`docs/SWITCHBOARD-TOOLING-RESEARCH.md`
section 4.3: Sentry's Slowest Functions, Pyroscope's Top table, speedscope's own Sandwich), and
Relay has no QtWebEngine, so the table is the view that stays in the app and the flame graph is
the one that leaves it.  This module is the part of that which is arithmetic rather than shell:

    pstats   a `cProfile` file -> speedscope JSON, for the machine with no py-spy
    rows     a speedscope JSON, a folded-stack file or a `.ninja_log` -> the table's rows

Three input shapes, one row shape, because the pane draws one table:

    {"name", "file"?, "line"?, "self", "total", "self_pct", "total_pct"}

For a build there is no call stack, so a target's `self` and `total` are both its compile time and
`self_pct` is its share of the summed step time — the column the build table is read by.

Nothing here imports anything outside the standard library, and nothing here runs a profiler: it
is fed files that a profiler left behind, so it is testable with a fixture and no tools installed.
"""
from __future__ import annotations

import argparse
import json
import marshal
import os
import sys
from pathlib import Path

#: The schema speedscope writes into its own exports, and reads back.
SCHEMA = "https://www.speedscope.app/file-format-schema.json"

#: How many rows a summary table carries by default.  25 is what the card asks for and what fits
#: in a pane without scrolling; the wire caps it the same way.
DEFAULT_LIMIT = 25

#: speedscope's `unit` values, as seconds.  A profile with no unit is treated as seconds, which
#: is what py-spy, pyinstrument and this module's own exporter all write.
_UNIT_SECONDS = {"seconds": 1.0, "milliseconds": 1e-3, "microseconds": 1e-6,
                 "nanoseconds": 1e-9}


class ProfileError(ValueError):
    """A file this cannot read: one sentence, no traceback."""


# --------------------------------------------------------------------------- cProfile -> speedscope

def _pstats_entries(path) -> dict:
    """`{func: (cc, nc, tt, ct, callers)}` out of a `cProfile.dump_stats` file.

    Read with `marshal` directly rather than through `pstats.Stats`, because `Stats` prints to
    stdout on a bad file and this has to raise instead.
    """
    try:
        with open(path, "rb") as handle:
            data = marshal.load(handle)
    except Exception as exc:                                  # noqa: BLE001 - any bad file
        raise ProfileError(f"{path} is not a cProfile file ({type(exc).__name__}).") from None
    if not isinstance(data, dict) or not data:
        raise ProfileError(f"{path} holds no profile data.")
    return data


def _func_name(func) -> tuple[str, str, int]:
    """`(name, file, line)` for pstats' `(file, line, name)` key, including its built-in shape."""
    if not isinstance(func, tuple) or len(func) != 3:
        return (str(func), "", 0)
    filename, line, name = func
    if filename == "~" or not filename:
        return (str(name), "", 0)
    return (f"{name} ({os.path.basename(str(filename))})", str(filename), int(line or 0))


def pstats_to_speedscope(path, *, name=None) -> dict:
    """A `cProfile` file as a sampled speedscope profile.

    cProfile counts calls; speedscope draws samples.  The bridge every converter uses is to emit
    **one sample per function**, weighted by that function's *own* time (`tottime`), on the stack
    that reaches it — so self time is exact, and a frame's total is the sum of what is below it,
    which is what a flame graph shows.  The stack is walked up through each function's heaviest
    caller (cycles cut), because pstats records callers without recording the whole path.
    """
    entries = _pstats_entries(path)
    frames: list[dict] = []
    index: dict[object, int] = {}

    def frame_of(func) -> int:
        if func in index:
            return index[func]
        label, filename, line = _func_name(func)
        frame = {"name": label}
        if filename:
            frame["file"] = filename
        if line:
            frame["line"] = line
        index[func] = len(frames)
        frames.append(frame)
        return index[func]

    def heaviest_caller(func):
        callers = entries.get(func, (0, 0, 0.0, 0.0, {}))[4] or {}
        best, best_time = None, -1.0
        for caller, stats in callers.items():
            if caller not in entries:
                continue
            weight = stats[3] if isinstance(stats, tuple) and len(stats) >= 4 else 0.0
            if weight > best_time:
                best, best_time = caller, weight
        return best

    def stack_of(func) -> list[int]:
        chain, seen, current = [], set(), func
        while current is not None and current not in seen and len(chain) < 128:
            seen.add(current)
            chain.append(frame_of(current))
            current = heaviest_caller(current)
        chain.reverse()
        return chain

    samples: list[list[int]] = []
    weights: list[float] = []
    for func, stats in entries.items():
        own = float(stats[2]) if len(stats) > 2 else 0.0
        if own <= 0:
            continue
        samples.append(stack_of(func))
        weights.append(own)
    total = sum(weights)
    profile = {"type": "sampled", "name": name or Path(path).name, "unit": "seconds",
               "startValue": 0, "endValue": total, "samples": samples, "weights": weights}
    return {"$schema": SCHEMA, "exporter": "relay-profile (cProfile)",
            "name": name or Path(path).name, "activeProfileIndex": 0,
            "shared": {"frames": frames}, "profiles": [profile]}


# --------------------------------------------------------------------------- folding

def _accumulate(stacks) -> tuple[dict, dict, float]:
    """`(self, total, wall)` keyed by frame, out of `(stack, weight)` pairs.

    `self` is the leaf's weight; `total` is every *distinct* frame on the stack, so a recursive
    function is counted once per sample and its total cannot exceed the wall.
    """
    own: dict[object, float] = {}
    whole: dict[object, float] = {}
    wall = 0.0
    for stack, weight in stacks:
        if not stack:
            continue
        wall += weight
        own[stack[-1]] = own.get(stack[-1], 0.0) + weight
        for key in dict.fromkeys(stack):
            whole[key] = whole.get(key, 0.0) + weight
    return own, whole, wall


def _rows_from(own, whole, wall, label) -> list[dict]:
    rows = []
    for key in whole:
        self_time = own.get(key, 0.0)
        name, filename, line = label(key)
        row = {"name": name, "self": round(self_time, 6), "total": round(whole[key], 6),
               "self_pct": round(100.0 * self_time / wall, 2) if wall else 0.0,
               "total_pct": round(100.0 * whole[key] / wall, 2) if wall else 0.0}
        if filename:
            row["file"] = filename
        if line:
            row["line"] = line
        rows.append(row)
    rows.sort(key=lambda r: (-r["self"], -r["total"], r["name"]))
    return rows


def fold_speedscope(document: dict) -> dict:
    """`{"kind": "profile", "rows": [...], "wall": seconds, "samples": n}` from speedscope JSON."""
    frames = (document.get("shared") or {}).get("frames") or []
    profiles = document.get("profiles") or []
    if not profiles:
        raise ProfileError("That speedscope file carries no profile.")
    stacks: list[tuple[tuple, float]] = []
    for profile in profiles:
        scale = _UNIT_SECONDS.get(str(profile.get("unit") or "seconds"), 1.0)
        if profile.get("type") == "evented":
            stacks.extend(_evented_stacks(profile, scale))
            continue
        samples = profile.get("samples") or []
        weights = profile.get("weights") or []
        for position, stack in enumerate(samples):
            weight = float(weights[position]) if position < len(weights) else 1.0
            stacks.append((tuple(int(i) for i in stack), weight * scale))
    own, whole, wall = _accumulate(stacks)

    def label(key):
        frame = frames[key] if isinstance(key, int) and 0 <= key < len(frames) else {}
        return (str(frame.get("name") or f"frame {key}"), str(frame.get("file") or ""),
                int(frame.get("line") or 0))

    return {"kind": "profile", "wall": round(wall, 6), "samples": len(stacks),
            "rows": _rows_from(own, whole, wall, label)}


def _evented_stacks(profile: dict, scale: float):
    """An `evented` profile as `(stack, weight)` pairs: one per interval between events."""
    out, stack, last = [], [], None
    for event in profile.get("events") or []:
        at = float(event.get("at") or 0.0)
        if last is not None and stack and at > last:
            out.append((tuple(stack), (at - last) * scale))
        last = at
        if event.get("type") == "O":
            stack.append(int(event.get("frame") or 0))
        elif stack:
            stack.pop()
    return out


def fold_folded(text: str) -> dict:
    """`frame;frame count` lines (Brendan Gregg's folded stacks) as the same row shape.

    The weight of a folded line is a sample count, not seconds, so `wall` here is the sample
    count and the percentages — which is what the table is read by — are exact either way.
    """
    stacks = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        head, _, count = line.rpartition(" ")
        try:
            weight = float(count)
        except ValueError:
            continue
        if not head:
            continue
        stacks.append((tuple(head.split(";")), weight))
    if not stacks:
        raise ProfileError("That folded-stack file has no `frame;frame count` lines.")
    own, whole, wall = _accumulate(stacks)
    return {"kind": "profile", "wall": round(wall, 6), "samples": len(stacks),
            "rows": _rows_from(own, whole, wall, lambda key: (str(key), "", 0))}


# --------------------------------------------------------------------------- ninja

def fold_ninja(text: str) -> dict:
    """A Ninja `.ninja_log` as per-output compile times.

    The format is one tab-separated `start_ms end_ms mtime output command_hash` per completed
    edge, appended — so an output built twice appears twice and the **last** line is the one that
    counts.  `wall` is the elapsed time of the build (last end minus first start), which is less
    than the summed step time on a parallel build; `self_pct` is the share of the *summed* step
    time, because that is the number that says where the compiler went.
    """
    rows: dict[str, dict] = {}
    first, last = None, None
    for line in text.splitlines():
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        try:
            start, end = int(parts[0]), int(parts[1])
        except ValueError:
            continue
        output = parts[3]
        seconds = max(0.0, (end - start) / 1000.0)
        rows[output] = {"name": output, "self": round(seconds, 3), "total": round(seconds, 3),
                        "start": start, "end": end}
        first = start if first is None else min(first, start)
        last = end if last is None else max(last, end)
    if not rows:
        raise ProfileError("That .ninja_log has no completed build steps in it.")
    summed = sum(row["self"] for row in rows.values())
    out = []
    for row in rows.values():
        row["self_pct"] = round(100.0 * row["self"] / summed, 2) if summed else 0.0
        row["total_pct"] = row["self_pct"]
        out.append(row)
    out.sort(key=lambda r: (-r["self"], r["name"]))
    return {"kind": "build", "wall": round(((last or 0) - (first or 0)) / 1000.0, 3),
            "steps": len(out), "sum": round(summed, 3), "rows": out}


def ninja_trace(folded: dict, *, name: str = "build") -> dict:
    """The same build as a Chrome trace, which is what the flame graph reads.

    speedscope imports Chrome traces, and ninjatracing (the tool this replaces one call to) emits
    exactly this: one complete (`X`) event per step, laid out in lanes so that two steps that
    overlapped in the real parallel build are drawn side by side rather than nested.
    """
    lanes: list[int] = []            # each lane's last end, in ms
    events = []
    rows = sorted(folded.get("rows") or [], key=lambda r: r.get("start", 0))
    base = min((row.get("start", 0) for row in rows), default=0)
    for row in rows:
        start, end = row.get("start", 0), row.get("end", 0)
        lane = next((i for i, last in enumerate(lanes) if last <= start), len(lanes))
        if lane == len(lanes):
            lanes.append(end)
        else:
            lanes[lane] = end
        events.append({"name": row["name"], "ph": "X", "pid": 1, "tid": lane,
                       "ts": (start - base) * 1000, "dur": max(0, end - start) * 1000,
                       "args": {"seconds": row.get("self", 0.0), "share": row.get("self_pct", 0.0)}})
    return {"traceEvents": events, "displayTimeUnit": "ms", "otherData": {"profile": name}}


# --------------------------------------------------------------------------- one entry point

def fold_file(path) -> dict:
    """Whichever of the three shapes `path` is, folded.  Detection is by content, not by name."""
    path = Path(path)
    try:
        raw = path.read_text(errors="replace")
    except OSError as exc:
        raise ProfileError(f"{path}: {exc.strerror or exc}") from None
    head = raw.lstrip()[:1]
    if head == "{":
        try:
            document = json.loads(raw)
        except ValueError:
            raise ProfileError(f"{path} starts like JSON but does not parse.") from None
        if "profiles" in document:
            return fold_speedscope(document)
        raise ProfileError(f"{path} is JSON, but not a speedscope profile.")
    if raw.lstrip().startswith("# ninja log") or "\t" in raw.split("\n", 1)[0]:
        return fold_ninja(raw)
    return fold_folded(raw)


def markdown_table(folded: dict, *, limit: int = DEFAULT_LIMIT) -> str:
    """The summary table, in the two shapes the two kinds want."""
    rows = folded.get("rows") or []
    shown = rows[:max(1, int(limit))]
    lines: list[str] = []
    if folded.get("kind") == "build":
        lines.append(f"{folded.get('steps', len(rows))} steps · {_secs(folded.get('wall'))} wall "
                     f"· {_secs(folded.get('sum'))} of compile time")
        lines.append("")
        lines.append("| Output | Seconds | Share |")
        lines.append("|---|---:|---:|")
        for row in shown:
            lines.append(f"| `{row['name']}` | {row['self']:.1f} | {row['self_pct']:.1f}% |")
        return "\n".join(lines)
    lines.append(f"{len(rows)} functions · {folded.get('samples', 0)} samples · "
                 f"{_secs(folded.get('wall'))} total")
    lines.append("")
    lines.append("| Function | Self | Self % | Total | Total % |")
    lines.append("|---|---:|---:|---:|---:|")
    for row in shown:
        where = f"<br>`{row['file']}:{row['line']}`" if row.get("file") else ""
        lines.append(f"| {row['name']}{where} | {row['self']:.3f} | {row['self_pct']:.1f}% "
                     f"| {row['total']:.3f} | {row['total_pct']:.1f}% |")
    return "\n".join(lines)


def _secs(value) -> str:
    try:
        seconds = float(value or 0.0)
    except (TypeError, ValueError):
        return "—"
    if seconds >= 60:
        return f"{int(seconds // 60)}m {seconds % 60:.0f}s"
    return f"{seconds:.1f} s"


# --------------------------------------------------------------------------- CLI

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="relay_core.profile_convert", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    convert = sub.add_parser("pstats", help="a cProfile file -> speedscope JSON")
    convert.add_argument("input")
    convert.add_argument("output")
    trace = sub.add_parser("trace", help="a .ninja_log -> a Chrome trace speedscope can open")
    trace.add_argument("input")
    trace.add_argument("output")
    table = sub.add_parser("rows", help="a speedscope, folded or .ninja_log file -> a table")
    table.add_argument("input")
    table.add_argument("--limit", type=int, default=DEFAULT_LIMIT)
    table.add_argument("--json", dest="json_out", default=None,
                       help="write the table (the top `--limit` rows) here as JSON")
    table.add_argument("--full-json", dest="full_out", default=None,
                       help="write every row here as JSON, uncapped")
    args = parser.parse_args(argv)
    try:
        if args.command == "pstats":
            document = pstats_to_speedscope(args.input)
            Path(args.output).write_text(json.dumps(document))
            print(args.output)
            return 0
        if args.command == "trace":
            document = ninja_trace(fold_ninja(Path(args.input).read_text(errors="replace")))
            Path(args.output).write_text(json.dumps(document))
            print(args.output)
            return 0
        folded = fold_file(args.input)
        if args.full_out:
            Path(args.full_out).write_text(json.dumps(folded, indent=1))
        if args.json_out:
            limit = max(1, int(args.limit))
            top = dict(folded, rows=folded["rows"][:limit], total_rows=len(folded["rows"]))
            Path(args.json_out).write_text(json.dumps(top, indent=1))
        print(markdown_table(folded, limit=args.limit))
        return 0
    except ProfileError as exc:
        print(f"relay-profile: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":                                    # pragma: no cover - CLI
    sys.exit(main())
