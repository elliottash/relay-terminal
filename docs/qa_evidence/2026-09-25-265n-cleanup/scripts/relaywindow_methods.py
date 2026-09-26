#!/usr/bin/env python3
"""#265N audit (a): uncalled RelayWindow members.

Every method name declared in src/RelayWindow.h (the class plus its nested
helpers — the header carries inline bodies and lambdas, so calls live in the
header too). For each name: occurrences across src/ + tests/ outside the
header, then the header's own lines mentioning it, so a name whose every
appearance is a declaration/definition is visible by hand. Report-only.
"""
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/265n-head")
HEADER = ROOT / "src/RelayWindow.h"

text = HEADER.read_text()
decl = re.compile(
    r"^\s*(?:virtual\s+|static\s+|explicit\s+|inline\s+)*"
    r"(?:[A-Za-z_][\w:<>,\*\&\s]*?\s+)?"
    r"(?P<name>~?[a-z][A-Za-z0-9_]*)\s*\(",
    re.M,
)
names = []
for m in decl.finditer(text):
    line = text[text.rfind("\n", 0, m.start()) + 1 : text.find("\n", m.start())].strip()
    if (
        line.startswith(("//", "#", "*", "/*"))
        or line.startswith("Q_PROPERTY")
        or "operator" in line.split("(")[0]
    ):
        continue
    name = m.group("name")
    if name in ("if", "for", "while", "switch", "return", "catch", "sizeof"):
        continue
    if not any(c.isupper() for c in name) and not name.startswith("~"):
        continue
    if name not in names:
        names.append(name)

sources = [
    p
    for p in list((ROOT / "src").rglob("*")) + list((ROOT / "tests").rglob("*"))
    if p.suffix in (".cpp", ".h") and p != HEADER
]

header_lines = text.splitlines()

print(f"header: src/RelayWindow.h ({len(header_lines)} lines)")
print(f"method-name candidates parsed: {len(names)}")
print("\n== names with zero occurrences outside src/RelayWindow.h ==")
outside_zero = []
for name in names:
    pat = re.compile(re.escape(name))
    uses = 0
    for p in sources:
        try:
            body = p.read_text()
        except UnicodeDecodeError:
            continue
        n = len(pat.findall(body))
        if n:
            uses += n
    if uses == 0:
        outside_zero.append(name)

for name in outside_zero:
    hits = [i + 1 for i, l in enumerate(header_lines) if name in l]
    print(f"\n{name}: header lines {hits}")
    for i in hits:
        print(f"  {i}: {header_lines[i - 1].strip()[:150]}")
