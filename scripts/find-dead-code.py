#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""List names that are defined but never referenced: unused Python imports, and Python and C++
names that occur exactly once in the whole code base (their own definition).

Why this exists: no compiler flag finds an unused *member* function (-Wunused only covers locals,
parameters and static free functions), and the Python side has no linter installed. On 2026-09-18
this scan found a toolbar that was never built but still "synced" from three places, and sixteen
other names nothing called.

It is a word count, not a parser, so it reports candidates, not verdicts. Read each one before
deleting it:
  * handlers looked up by name (`getattr(self, "_" + kind)`, `_on_<message>`) are skipped by the
    DISPATCH_PREFIXES below, but a new dispatch convention will show up here as a false positive;
  * a name that only tests use is reported as "tests-only", which is sometimes the point of it;
  * Qt overrides and framework hooks (formatTime, redirect_request, paintEvent) are called by
    their base class, never by name.

Usage: scripts/find-dead-code.py [--python] [--cpp]        (default: both)
Exit status is 0 whatever it finds: this is a report, not a gate.
"""
from __future__ import annotations

import argparse
import ast
import collections
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
PY_DIRS = ("backend", "remote", "rendezvous", "scripts", "shell")
CPP_DIRS = ("src", "engine", "engine/core", "engine/view", "engine/backend", "engine/session", "engine/pty")
CPP_REFERENCE_DIRS = CPP_DIRS + ("tests", "engine/tests", "engine/tools", "engine/bench")
# Methods found by name at run time, never referenced as identifiers.
DISPATCH_PREFIXES = ("_on_", "test_", "visit_", "do_")
DISPATCH_FILES = {"session_protocol.py", "observe_protocol.py", "board_protocol.py", "alias_protocol.py"}
# Called by a base class or by the interpreter.
FRAMEWORK_HOOKS = {"formatTime", "redirect_request", "main", "setUp", "tearDown", "setUpClass", "tearDownClass",
                   "paint", "sizeHint", "eventFilter", "event"}
WORD = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


def python_report() -> int:
    sources = {p: p.read_text() for d in PY_DIRS for p in (ROOT / d).rglob("*.py") if "__pycache__" not in p.parts}
    tests = {p: p.read_text() for p in (ROOT / "tests").rglob("*.py")}
    source_words = collections.Counter(w for text in sources.values() for w in WORD.findall(text))
    test_words = collections.Counter(w for text in tests.values() for w in WORD.findall(text))
    found = 0
    for path, text in sorted(sources.items()):
        tree = ast.parse(text)
        exported: set[str] = set()
        imported: dict[str, int] = {}
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    imported[(alias.asname or alias.name).split(".")[0]] = node.lineno
            elif isinstance(node, ast.ImportFrom):
                for alias in node.names:
                    imported[alias.asname or alias.name] = node.lineno
            elif isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "__all__" for t in node.targets):
                exported = {e.value for e in getattr(node.value, "elts", []) if isinstance(e, ast.Constant)}
        used = {n.id for n in ast.walk(tree) if isinstance(n, ast.Name)}
        if path.name != "__init__.py":
            for name, line in sorted(imported.items(), key=lambda item: item[1]):
                if name not in used and name not in exported and name != "annotations":
                    print(f"{path.relative_to(ROOT)}:{line}: unused import {name}")
                    found += 1
        definitions: list[tuple[str, int]] = []
        for node in tree.body:
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                definitions.append((node.name, node.lineno))
            if isinstance(node, ast.ClassDef):
                definitions += [(sub.name, sub.lineno) for sub in node.body
                                if isinstance(sub, (ast.FunctionDef, ast.AsyncFunctionDef)) and not sub.name.startswith("__")]
            if isinstance(node, ast.Assign):
                definitions += [(t.id, node.lineno) for t in node.targets if isinstance(t, ast.Name) and t.id.isupper()]
        for name, line in definitions:
            if name in FRAMEWORK_HOOKS or name.startswith(DISPATCH_PREFIXES):
                continue
            if path.name in DISPATCH_FILES and name.startswith("_"):
                continue
            if source_words[name] <= 1:
                where = "tests-only" if test_words[name] else "unreferenced"
                print(f"{path.relative_to(ROOT)}:{line}: {where} {name}")
                found += 1
    return found


def cpp_report() -> int:
    def files(dirs):
        return [p for d in dirs for p in (ROOT / d).glob("*") if p.suffix in (".cpp", ".h")]
    words = collections.Counter(w for p in set(files(CPP_REFERENCE_DIRS)) for w in WORD.findall(p.read_text(errors="replace")))
    # At most four spaces of indentation: a free function or a class member in this code base's
    # style. Deeper lines are statements, where the same shape is a call (vterm_set_utf8(...);)
    # or a local (QSignalBlocker block(box);). An `override` is called by its base class.
    definition = re.compile(r"^ {0,4}(?! )(?:static |inline |virtual |constexpr |explicit )*[A-Za-z_:<>\*&, ]+?[ \*&]"
                            r"([a-z][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:const)?\s*(?!override)\s*[{;]", re.M)
    member = re.compile(r"\b(m_[A-Za-z0-9_]+)\b")
    found = 0
    for path in sorted(set(files(CPP_DIRS))):
        if "third_party" in path.parts:
            continue
        text = path.read_text(errors="replace")
        names = {m.group(1) for m in definition.finditer(text) if "override" not in m.group(0)}
        names |= set(member.findall(text))
        for name in sorted(names):
            # One occurrence anywhere is the definition itself. Qt setters called once
            # (setCursorWidth, setModal) match the pattern too; they are Qt's, so skip "set…".
            # vterm_*: libvterm's own symbols, declared here to be resolved from the library.
            if words[name] == 1 and name not in FRAMEWORK_HOOKS and not re.match(r"set[A-Z]|vterm_", name):
                line = text[:text.index(name)].count("\n") + 1
                print(f"{path.relative_to(ROOT)}:{line}: unreferenced {name}")
                found += 1
    return found


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--python", action="store_true")
    parser.add_argument("--cpp", action="store_true")
    args = parser.parse_args()
    both = not (args.python or args.cpp)
    total = 0
    if args.python or both:
        total += python_report()
    if args.cpp or both:
        total += cpp_report()
    print(f"{total} candidate(s). Each is a name with no reference but its own definition; read before deleting.")


if __name__ == "__main__":
    main()
