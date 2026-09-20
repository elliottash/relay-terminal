#!/usr/bin/env python3
"""Byte table for one slice of the worker->GUI channel (#PF4K phone area).

    python3 seg.py <file> <start-offset> [<end-offset>]

Same counting as the toolout reducer (`reduce.py`): one line is one message, bytes include the
newline. Printed per `event`/`type` so a turn's `tool_output` / `tool_result` can be read off.
"""
import collections
import json
import sys

path = sys.argv[1]
start = int(sys.argv[2])
end = int(sys.argv[3]) if len(sys.argv) > 3 else None
with open(path, "rb") as fh:
    fh.seek(start)
    blob = fh.read() if end is None else fh.read(end - start)

n, b = collections.Counter(), collections.Counter()
total = 0
for raw in blob.split(b"\n"):
    if not raw.strip():
        continue
    total += len(raw) + 1
    try:
        obj = json.loads(raw.decode("utf-8", "replace"))
    except ValueError:
        n["<unparsed>"] += 1
        b["<unparsed>"] += len(raw) + 1
        continue
    kind = obj.get("event") or obj.get("type") or "?"
    if kind == "tool_output" and obj.get("counted"):
        kind = "tool_output(counted)"
    if kind == "tool_result" and obj.get("counted"):
        kind = "tool_result(counted)"
    n[kind] += 1
    b[kind] += len(raw) + 1
print(f"--- {path} [{start}:{end if end is not None else 'eof'}] bytes={total}")
print(f"{'message':<26}{'n':>5}{'bytes':>10}{'avg':>9}{'share':>8}")
for kind, count in sorted(b.items(), key=lambda kv: -kv[1]):
    print(f"{kind:<26}{n[kind]:>5}{count:>10}{count // n[kind]:>9}{100 * count / total:>7.1f}%")
