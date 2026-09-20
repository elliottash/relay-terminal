#!/usr/bin/env python3
"""Per-message byte table for one direction of the worker<->GUI channel (#PF4K reducer)."""
import json, sys, collections
for path in sys.argv[1:]:
    n = collections.Counter(); b = collections.Counter(); total = 0; msgs = 0
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
        except ValueError:
            continue
        kind = obj.get("event") or obj.get("type") or "?"
        n[kind] += 1; b[kind] += len(line) + 1; total += len(line) + 1; msgs += 1
    print(f"--- {path}  messages={msgs} bytes={total}")
    print(f"{'message':<28}{'n':>5}{'bytes':>10}{'avg':>9}{'share':>8}")
    for kind, count in sorted(b.items(), key=lambda kv: -kv[1]):
        print(f"{kind:<28}{n[kind]:>5}{count:>10}{count // n[kind]:>9}{100 * count / total:>7.1f}%")
    print()
