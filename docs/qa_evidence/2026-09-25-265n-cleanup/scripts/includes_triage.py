#!/usr/bin/env python3
"""#265N audit (b): unused #include triage for src/Pane.h, src/BoardPane.cpp, src/RelayWindow.h.

Stage 1 (this script): for every #include of the three files, collect the
identifiers the included header exports (class/struct/enum/using/function/
macro names at file scope) and check whether the audited file mentions any of
them anywhere outside the include line. Includes with zero hits are printed
as compile-verify candidates; the compile happens outside this script.
"""
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/265n-head")
FILES = ["src/Pane.h", "src/BoardPane.cpp", "src/RelayWindow.h"]
QT_DIRS = [
    Path("/usr/include/x86_64-linux-gnu/qt5"),
    Path("/usr/include/x86_64-linux-gnu/qt5/QtCore"),
    Path("/usr/include/x86_64-linux-gnu/qt5/QtGui"),
    Path("/usr/include/x86_64-linux-gnu/qt5/QtWidgets"),
    Path("/usr/include/x86_64-linux-gnu/qt5/QtNetwork"),
    Path("/usr/include/x86_64-linux-gnu/qt5/QtTest"),
    Path("/usr/include"),
    Path("/usr/include/c++/12"),
    Path("/usr/include/x86_64-linux-gnu/c++/12"),
]

def resolve(angle: str | None, quote: str | None) -> Path | None:
    name = angle or quote
    if not name:
        return None
    cand = [ROOT / "src" / name, ROOT / "engine" / name, ROOT / name]
    for c in cand:
        if c.is_file():
            return c
    for d in QT_DIRS:
        p = d / name
        if p.is_file():
            return p
    return None

EXPORT = re.compile(
    r"^\s*(?:template\s*<[^>]*>\s*)?"
    r"(?:class|struct|enum(?:\s+class)?|union)\s+(?P<t>[A-Za-z_]\w*)"
    r"|^\s*using\s+(?P<u>[A-Za-z_]\w*)\s*="
    r"|^\s*typedef\s+[^;]*\s(?P<td>[A-Za-z_]\w*);"
    r"|^\s*(?:inline\s+|static\s+|constexpr\s+|const\s+|virtual\s+)*"
    r"(?:[A-Za-z_][\w:<>,\*\&\s]*?\s+)?(?P<f>[A-Za-z_]\w*)\s*\([^;]*$"
    r"|^\s*#define\s+(?P<m>[A-Za-z_]\w*)"
    r"|^\s*(?:Q_OBJECT|Q_GADGET|Q_NAMESPACE)"
)

SKIP = {"if", "for", "while", "switch", "return", "else", "do", "sizeof", "operator"}

def exports_of(path: Path) -> set[str]:
    out: set[str] = set()
    try:
        body = path.read_text(errors="replace")
    except OSError:
        return out
    for line in body.splitlines():
        if line.lstrip().startswith(("//", "*", "/*", "#include", "#import")):
            continue
        for m in EXPORT.finditer(line):
            for k in ("t", "u", "td", "f", "m"):
                v = m.group(k)
                if v and v not in SKIP and not v.startswith("Q_"):
                    out.add(v)
    return out

for rel in FILES:
    f = ROOT / rel
    text = f.read_text()
    print(f"\n=== {rel} ===")
    total = 0
    for i, line in enumerate(text.splitlines(), 1):
        m = re.match(r'\s*#include\s+(?:<([^>]+)>|"([^"]+)")', line)
        if not m:
            continue
        total += 1
        hp = resolve(m.group(1), m.group(2))
        if hp is None:
            print(f"  {i:5d} {line.strip():55s} [unresolved header]")
            continue
        names = exports_of(hp)
        if not names:
            print(f"  {i:5d} {line.strip():55s} [no exported names parsed]")
            continue
        body_wo = "\n".join(l for j, l in enumerate(text.splitlines(), 1) if j != i)
        hits = [n for n in names if re.search(r"\b" + re.escape(n) + r"\b", body_wo)]
        if not hits:
            print(f"  {i:5d} {line.strip():55s} CANDIDATE (no {len(names)} exported names used)")
    print(f"  ({total} includes parsed)")
