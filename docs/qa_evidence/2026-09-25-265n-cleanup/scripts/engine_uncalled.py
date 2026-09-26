#!/usr/bin/env python3
"""#265N audit (d): uncalled functions in engine/core/GhosttyCore.cpp,
engine/session/TerminalSession.cpp, engine/view/TerminalView.cpp.

Lists every function defined in the three files (by name), then greps the
name across engine/, src/ and tests/. Names whose only occurrences are the
definition itself (and its own recursive calls) are printed. Report-only.
"""
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/265n-head")
FILES = [
    "engine/core/GhosttyCore.cpp",
    "engine/session/TerminalSession.cpp",
    "engine/view/TerminalView.cpp",
]
DEF = re.compile(
    r"^\s*(?:[A-Za-z_][\w:<>,\*\&\s]*?\s+)?(?:\w+::)*(?P<name>[a-z][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\s*$",
    re.M,
)

corpus = [
    p for p in list((ROOT / "engine").rglob("*")) + list((ROOT / "src").rglob("*")) + list((ROOT / "tests").rglob("*"))
    if p.suffix in (".cpp", ".h")
]

for rel in FILES:
    p = ROOT / rel
    text = p.read_text()
    print(f"\n=== {rel} ===")
    names = []
    for m in DEF.finditer(text):
        line = text[text.rfind("\n", 0, m.start()) + 1 : text.find("\n", m.start())].strip()
        if line.startswith(("//", "*", "/*", "#")):
            continue
        n = m.group("name")
        if n in ("if", "for", "while", "switch", "return", "catch", "sizeof", "else", "do"):
            continue
        if n not in names:
            names.append(n)
    dead = []
    for n in names:
        pat = re.compile(r"\b" + re.escape(n) + r"\b")
        uses = []
        for q in corpus:
            try:
                body = q.read_text()
            except UnicodeDecodeError:
                continue
            c = len(pat.findall(body))
            if c:
                uses.append((str(q.relative_to(ROOT)), c))
        # own file's definition counts one; calls inside the same file count as uses
        own = sum(c for f, c in uses if f == rel)
        outside = sum(c for f, c in uses if f != rel)
        if own <= 1 and outside == 0:
            dead.append((n, line[:90]))
    print(f"definitions parsed: {len(names)}; zero-use names: {len(dead)}")
    for n, l in dead:
        print(f"  {n}  <- {l}")
