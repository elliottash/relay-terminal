#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Implementer evidence: verify a migrated copy of issues/ against the originals.

Usage: verify-migration.py <original issues/> <migrated copy of issues/>

Checks, for every card: the body is byte-identical to the original minus its
`- **Field**: value` header block; every header value survived into the front
matter; a full YAML reader (PyYAML, if installed) agrees with board.py's parser;
and a second migration run produces identical bytes (determinism).
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "backend"))
from relay_core import board as B

SRC, DST = Path(sys.argv[1]), Path(sys.argv[2])
bad_body, bad_fields, bad_yaml, checked = [], [], [], 0

for path in sorted(DST.rglob("*.md")):
    rel = path.relative_to(DST)
    original = SRC / rel
    if rel.name in ("README.md", "BOARD.md") or not original.exists():
        continue
    card = B.Card.load(path)
    legacy = B.parse_legacy(original.read_text())
    checked += 1
    if card.body != legacy.body:
        bad_body.append(str(rel))
    for key, value in legacy.headers.items():
        if key == "status":
            continue
        if key == "assignee" and value.lower() in ("unassigned", "none", "-", ""):
            continue
        got = card.front.get(key)
        got = ", ".join(got) if isinstance(got, list) else got
        if got != value:
            bad_fields.append(f"{rel}: {key}: header={value!r} front={got!r}")
    try:
        import yaml
        front = yaml.safe_load(card.front_raw)
        for key, value in card.front.items():
            if front.get(key) != value:
                bad_yaml.append(f"{rel}: {key}: ours={value!r} pyyaml={front.get(key)!r}")
    except ImportError:
        bad_yaml.append("PyYAML not installed; cross-check skipped")
        break

print(f"cards checked: {checked}")
print(f"body byte-identical to the original minus its header block: {checked - len(bad_body)}")
print(f"body mismatches: {bad_body or 'none'}")
print(f"header values lost or changed: {bad_fields or 'none'}")
print(f"PyYAML cross-check mismatches: {bad_yaml or 'none'}")

tmp = Path(tempfile.mkdtemp())
shutil.copytree(SRC, tmp / "issues")
B.migrate(tmp / "issues", apply=True)
diff = subprocess.run(["diff", "-r", str(DST), str(tmp / "issues")], capture_output=True, text=True)
print("a second migration run produces identical bytes:",
      diff.returncode == 0 and not diff.stdout.strip())
if diff.stdout.strip():
    print(diff.stdout[:2000])
shutil.rmtree(tmp)
