# SPDX-License-Identifier: AGPL-3.0-or-later
"""Which tests a project *has*, discovered offline and without running one of them.

This is the `list` half of the nextest split (https://nexte.st/docs/machine-readable/): a card's
`## Tests` section, and the Test suites pane, can both be checked for staleness without a single
test executing.  Two sources, because this repository has two runners and so does almost every
C++/Python project:

* **ctest** — `ctest --show-only=json-v1 --test-dir <build>` (CMake's own documented listing,
  https://cmake.org/cmake/help/latest/manual/ctest.1.html) gives name, command and properties
  (`LABELS`, `DISABLED`, `TIMEOUT`) without building or running anything.  This is the **only**
  subprocess this module ever starts, it is run with a timeout, and an absent build directory or
  an absent `ctest` is *no ctest tests*, never an error.
* **stdlib unittest** — `tests/test_*.py` parsed with `ast`.  Nothing is imported and nothing is
  executed: a test file that would fail at import time is still discovered, which is exactly the
  case where discovery matters.

Bounded in the `project_probe.py` idiom: every walk has a depth and an entry ceiling, every file a
byte ceiling, a symlink that leaves the project is never followed, and `discover()` never raises
for a project it cannot read — an unreadable corner is an absent test.

Nothing here is specific to this repository: the roots, the glob and the build directory are
parameters, and their defaults (`tests/`, `test_*.py`, `build/`) are conventions, not assumptions.

The dicts this module produces are the discovery half of the *TestRecord* in the tests wire
contract (protocol §31): `id`, `name`, `runner`, `file`, `line`, `labels`, `source_hash`.  The
execution half (`runs`, percentiles, `flaky`, `stale`) is folded on top by `test_history.records()`.
"""
from __future__ import annotations

import ast
import hashlib
import json
import os
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

#: Schema version of `discover()`'s result, so a GUI can refuse a shape it does not know.
PROBE_VERSION = 1

# --------------------------------------------------------------------------- ceilings
#: Discovery runs while a pane waits, so every step is bounded rather than merely fast.
MAX_FILE_BYTES = 2 * 1024 * 1024    # any single test file we parse
MAX_JSON_BYTES = 16 * 1024 * 1024   # `ctest --show-only=json-v1` on a very large project
MAX_TESTS = 5000                    # tests past this are counted, not carried
MAX_DIR_ENTRIES = 4000              # entries read from any one directory
MAX_DEPTH = 4                       # how far below a test root we descend
CTEST_TIMEOUT = 20.0                # seconds; a listing that hangs is no listing

#: Where tests live, and what a Python test file is called, by convention.  Both are parameters
#: everywhere below; these are only the defaults.
DEFAULT_TEST_ROOTS = ("tests", "test")
DEFAULT_PYTHON_GLOB = "test_*.py"
DEFAULT_BUILD_DIR = "build"

#: Runner names, as they appear in a `TestRecord.id` prefix.
RUNNER_CTEST = "ctest"
RUNNER_UNITTEST = "unittest"
RUNNER_MANUAL = "manual"
RUNNERS = (RUNNER_CTEST, RUNNER_UNITTEST, RUNNER_MANUAL)


class ProbeError(Exception):
    """Discovery could not read something it was pointed at.  Never raised out of `discover()`."""


# --------------------------------------------------------------------------- the finding

@dataclass
class DiscoveredTest:
    """One test that exists in the project right now — before any history is folded in.

    `id` is the stable key the whole tests feature is built on: `"<runner>:<invocation>"`, e.g.
    `"ctest:panelayout"` or `"unittest:tests.test_board.CardTests.test_roundtrip"`.  It survives
    the file being edited around the test; it does not survive a rename, which is the point —
    a renamed test *is* a different test, and the old id going missing is the "gone" verdict.
    """
    id: str
    name: str
    runner: str
    file: str = ""                          # project-relative, POSIX separators, "" if unknown
    line: int = 0                           # 1-based, 0 when unknown
    labels: list[str] = field(default_factory=list)
    disabled: bool = False                  # ctest DISABLED, or @unittest.skip on the test
    skip_reason: str = ""
    #: What a human types to run just this one: `ctest -R <name>`, `tests/x.py::Cls::test_y`.
    invocation: str = ""
    #: `"sha256:<hex>"` of the test's source.  File granularity in v0 (asv hashes the benchmark
    #: body; a file is the cheap honest approximation), so editing any test in a file resets the
    #: history of every test in it — over-reporting "edited", never under-reporting it.
    source_hash: str = ""

    def to_dict(self) -> dict:
        out = {"id": self.id, "name": self.name, "runner": self.runner,
               "file": self.file, "labels": list(self.labels),
               "invocation": self.invocation or self.name}
        if self.line:
            out["line"] = self.line
        if self.disabled:
            out["disabled"] = True
            if self.skip_reason:
                out["skip_reason"] = self.skip_reason
        if self.source_hash:
            out["source_hash"] = self.source_hash
        return out


# --------------------------------------------------------------------------- safe IO

def _inside(project: Path, path: Path) -> bool:
    """True when `path` really lives inside `project` — symlinks resolved."""
    try:
        real = path.resolve()
        root = project.resolve()
    except OSError:                                      # pragma: no cover - unreadable path
        return False
    return real == root or root in real.parents


def _read_bytes(project: Path, path: Path, limit: int = MAX_FILE_BYTES) -> bytes | None:
    """The file's bytes, or None when it is absent, too big, not a file, or outside `project`."""
    if not _inside(project, path):
        return None
    try:
        if not path.is_file():
            return None
        if path.stat().st_size > limit:
            return None
        return path.read_bytes()
    except OSError:
        return None


def _rel(project: Path, path: Path) -> str:
    try:
        return Path(path).resolve().relative_to(project.resolve()).as_posix()
    except (ValueError, OSError):
        return Path(path).as_posix()


def _walk(project: Path, directory: Path, suffix: str = ".py",
          depth: int = MAX_DEPTH) -> list[Path]:
    """Files under `directory`, breadth-first, bounded by depth, entry count and symlinks."""
    out: list[Path] = []
    frontier = [(directory, 0)]
    while frontier and len(out) < MAX_DIR_ENTRIES:
        here, level = frontier.pop(0)
        if not _inside(project, here):
            continue
        try:
            entries = sorted(here.iterdir())[:MAX_DIR_ENTRIES]
        except OSError:
            continue
        for entry in entries:
            if not _inside(project, entry):
                continue
            try:
                is_dir = entry.is_dir()
            except OSError:                              # pragma: no cover - vanished mid-walk
                continue
            if is_dir:
                if level < depth and not entry.is_symlink():
                    frontier.append((entry, level + 1))
                continue
            if suffix and not entry.name.endswith(suffix):
                continue
            out.append(entry)
    return sorted(out)


def source_hash(data: bytes | str) -> str:
    """`"sha256:<hex>"` of a test source, the asv-style version stamp (research §3.4)."""
    if isinstance(data, str):
        data = data.encode("utf-8", "replace")
    return "sha256:" + hashlib.sha256(data).hexdigest()


def _matches_glob(name: str, glob: str) -> bool:
    from fnmatch import fnmatch
    return fnmatch(name, glob)


# --------------------------------------------------------------------------- ctest

def ctest_listing(build_dir: str | os.PathLike, *, timeout: float = CTEST_TIMEOUT,
                  ctest: str = "ctest") -> dict | None:
    """`ctest --show-only=json-v1`'s object for `build_dir`, or None.

    None — never an exception — for: no such directory, no `CTestTestfile.cmake` in it (so we do
    not start a process for a directory that is plainly not a build tree), no `ctest` on PATH, a
    non-zero exit, output that is not JSON, output past `MAX_JSON_BYTES`, or a timeout.  This is
    the only subprocess in this module.
    """
    build = Path(build_dir)
    try:
        if not build.is_dir() or not (build / "CTestTestfile.cmake").is_file():
            return None
    except OSError:
        return None
    try:
        done = subprocess.run([ctest, "--show-only=json-v1", "--test-dir", str(build)],
                              capture_output=True, timeout=timeout, check=False)
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode != 0 or len(done.stdout) > MAX_JSON_BYTES:
        return None
    try:
        data = json.loads(done.stdout.decode("utf-8", "replace"))
    except (ValueError, UnicodeError):
        return None
    return data if isinstance(data, dict) else None


def _ctest_properties(entry: dict) -> dict:
    out: dict = {}
    for prop in entry.get("properties") or []:
        if isinstance(prop, dict) and isinstance(prop.get("name"), str):
            out[prop["name"]] = prop.get("value")
    return out


def _target_sources(project: Path, roots: Sequence[Path]) -> dict[str, list[str]]:
    """`add_executable(<target> <sources…>)` seen in the project's CMake files, target → sources.

    A deliberately small scanner: it reads `CMakeLists.txt` at the project root and one level of
    subdirectories (which covers `engine/CMakeLists.txt` here), takes the text between
    `add_executable(` and its closing parenthesis, and keeps the entries that look like source
    paths.  Variables and generator expressions are left alone — an unresolvable source is simply
    not a mapping, and the command-path convention below still applies.
    """
    out: dict[str, list[str]] = {}
    files: list[Path] = []
    for root in roots:
        candidate = root / "CMakeLists.txt"
        if candidate.is_file():
            files.append(candidate)
        try:
            children = sorted(root.iterdir())[:MAX_DIR_ENTRIES]
        except OSError:
            children = []
        for child in children:
            try:
                if child.is_dir() and not child.is_symlink():
                    sub = child / "CMakeLists.txt"
                    if sub.is_file():
                        files.append(sub)
            except OSError:                              # pragma: no cover - vanished mid-walk
                continue
    for path in files[:64]:
        data = _read_bytes(project, path)
        if data is None:
            continue
        text = data.decode("utf-8", "replace")
        base = path.parent
        start = 0
        while True:
            at = text.find("add_executable(", start)
            if at < 0:
                break
            close = text.find(")", at)
            if close < 0:
                break
            body = text[at + len("add_executable("):close]
            start = close + 1
            words = body.split()
            if not words:
                continue
            target, rest = words[0], words[1:]
            sources = []
            for word in rest:
                if word.upper() in ("WIN32", "MACOSX_BUNDLE", "EXCLUDE_FROM_ALL"):
                    continue
                if "$" in word or word.startswith("#"):
                    continue
                if not any(word.endswith(ext) for ext in
                           (".cpp", ".cc", ".cxx", ".c", ".m", ".mm")):
                    continue
                sources.append(_rel(project, base / word))
            if sources:
                out.setdefault(target, sources)
    return out


def _ctest_source(project: Path, name: str, command: Sequence[str],
                  targets: dict[str, list[str]], roots: Sequence[str]) -> str:
    """The source file a ctest test most likely lives in, or `""`.

    Three attempts, in order of confidence: the CMake target the command's basename names, taking
    its first source under a test root (for a many-source target such as `relay-engine-tests` that
    is the target's own `tests/main.cpp`, which is honest — the binary is one ctest entry and the
    file is where it is defined); then the `<root>/<name>_test.cpp` convention on the test's own
    name; then the same convention on the command basename with a `relay-`-style prefix and a
    `-tests` suffix stripped.
    """
    exe = Path(command[0]).name if command else ""
    sources = targets.get(exe, [])
    for source in sources:
        if any(source == r or source.startswith(r + "/") for r in roots):
            return source
    for source in sources:                               # a test root one directory down
        if any(f"/{r}/" in f"/{source}" for r in roots):
            return source
    stems = [name]
    if exe:
        stem = exe
        for suffix in ("-tests", "_tests", "-test", "_test"):
            if stem.endswith(suffix):
                stem = stem[: -len(suffix)]
                break
        if "-" in stem:
            stem = stem.rsplit("-", 1)[-1]
        stems.append(stem)
    for stem in stems:
        for root in roots:
            for pattern in (f"{stem}_test.cpp", f"{stem}_tests.cpp", f"test_{stem}.cpp",
                            f"{stem}Test.cpp", f"{stem}.cpp"):
                candidate = project / root / pattern
                if candidate.is_file():
                    return _rel(project, candidate)
    return ""


def discover_ctest(project: str | os.PathLike, build_dir: str | os.PathLike | None = None, *,
                   roots: Sequence[str] = DEFAULT_TEST_ROOTS,
                   timeout: float = CTEST_TIMEOUT,
                   ctest: str = "ctest") -> list[DiscoveredTest]:
    """Every ctest test in `build_dir`, mapped to its source where that is derivable.

    `build_dir` defaults to `<project>/build`.  No build directory, no ctest, or a ctest that
    fails means an empty list: a project without CMake simply has no ctest tests.
    """
    project = Path(os.path.abspath(Path(project).expanduser()))
    build = Path(build_dir) if build_dir is not None else project / DEFAULT_BUILD_DIR
    data = ctest_listing(build, timeout=timeout, ctest=ctest)
    if not data:
        return []
    entries = data.get("tests")
    if not isinstance(entries, list):
        return []
    targets = _target_sources(project, [project])
    out: list[DiscoveredTest] = []
    hashes: dict[str, str] = {}
    for entry in entries[:MAX_TESTS]:
        if not isinstance(entry, dict):
            continue
        name = entry.get("name")
        if not isinstance(name, str) or not name:
            continue
        command = [c for c in (entry.get("command") or []) if isinstance(c, str)]
        props = _ctest_properties(entry)
        labels = [str(v) for v in (props.get("LABELS") or [])
                  if isinstance(props.get("LABELS"), list)]
        disabled = str(props.get("DISABLED", "")).upper() in ("1", "ON", "TRUE", "YES")
        source = _ctest_source(project, name, command, targets, list(roots))
        if source and source not in hashes:
            data_bytes = _read_bytes(project, project / source)
            hashes[source] = source_hash(data_bytes) if data_bytes is not None else ""
        out.append(DiscoveredTest(
            id=f"{RUNNER_CTEST}:{name}", name=name, runner=RUNNER_CTEST, file=source,
            labels=labels, disabled=disabled,
            skip_reason="DISABLED" if disabled else "",
            invocation=f"ctest -R {name}",
            source_hash=hashes.get(source, "")))
    return out


# --------------------------------------------------------------------------- unittest, by ast

_SKIP_DECORATORS = ("skip", "skipIf", "skipUnless", "expectedFailure")


def _decorator_name(node: ast.expr) -> str:
    """The dotted tail of a decorator: `unittest.skip("why")` → `skip`, `@skipIf` → `skipIf`."""
    if isinstance(node, ast.Call):
        return _decorator_name(node.func)
    if isinstance(node, ast.Attribute):
        return node.attr
    if isinstance(node, ast.Name):
        return node.id
    return ""


def _decorator_reason(node: ast.expr) -> str:
    if isinstance(node, ast.Call):
        for arg in node.args:
            if isinstance(arg, ast.Constant) and isinstance(arg.value, str):
                return arg.value
    return ""


def _base_name(node: ast.expr) -> str:
    if isinstance(node, ast.Attribute):
        return node.attr
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Subscript):                   # pragma: no cover - Generic[T] bases
        return _base_name(node.value)
    return ""


def _is_test_case(node: ast.ClassDef, local_cases: set[str]) -> bool:
    """A class is a test case when a base is named `*TestCase` or is another case in this file.

    Name-based on purpose: nothing is imported, so `unittest.TestCase`, `TestCase`,
    `AsyncioTestCase` and a project's own `RelayTestCase` all qualify, and a one-file mixin
    hierarchy (`class Base(unittest.TestCase)` … `class Cards(Base)`) resolves locally.
    """
    for base in node.bases:
        name = _base_name(base)
        if name.endswith("TestCase") or name in local_cases:
            return True
    return False


def module_path(project: Path, path: Path) -> str:
    """The dotted module name for a test file, from the project root: `tests/test_x.py` →
    `tests.test_x`.  Package `__init__.py` files are irrelevant here — `unittest discover`
    imports by path and names the case `tests.test_x.Cls.test_y` either way."""
    rel = _rel(project, path)
    if rel.endswith(".py"):
        rel = rel[:-3]
    return rel.replace("/", ".")


def discover_unittest_file(project: Path, path: Path) -> list[DiscoveredTest]:
    """Every `test*` method of every `*TestCase` subclass in one file, parsed, never imported."""
    data = _read_bytes(project, path)
    if data is None:
        return []
    try:
        tree = ast.parse(data.decode("utf-8", "replace"), filename=str(path))
    except (SyntaxError, ValueError):
        return []
    rel = _rel(project, path)
    module = module_path(project, path)
    digest = source_hash(data)
    out: list[DiscoveredTest] = []
    local_cases: set[str] = set()
    for node in ast.walk(tree):
        if not isinstance(node, ast.ClassDef):
            continue
        if not _is_test_case(node, local_cases):
            continue
        local_cases.add(node.name)
        class_skips = [d for d in node.decorator_list
                       if _decorator_name(d) in _SKIP_DECORATORS]
        for item in node.body:
            if not isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef)):
                continue
            if not item.name.startswith("test"):
                continue
            skips = class_skips + [d for d in item.decorator_list
                                   if _decorator_name(d) in _SKIP_DECORATORS]
            reason = ""
            for dec in skips:
                reason = _decorator_reason(dec) or _decorator_name(dec)
                if reason:
                    break
            out.append(DiscoveredTest(
                id=f"{RUNNER_UNITTEST}:{module}.{node.name}.{item.name}",
                name=f"{node.name}.{item.name}", runner=RUNNER_UNITTEST,
                file=rel, line=item.lineno,
                disabled=bool(skips), skip_reason=reason,
                invocation=f"{rel}::{node.name}::{item.name}",
                source_hash=digest))
    return sorted(out, key=lambda t: (t.file, t.line))


def discover_unittest(project: str | os.PathLike, *,
                      roots: Sequence[str] = DEFAULT_TEST_ROOTS,
                      glob: str = DEFAULT_PYTHON_GLOB) -> list[DiscoveredTest]:
    """Every stdlib-unittest case under `roots` matching `glob`, by `ast` alone.

    Never imports and never executes: a test module that raises at import time is still listed,
    which is precisely when a card needs to know its tests exist.
    """
    project = Path(os.path.abspath(Path(project).expanduser()))
    out: list[DiscoveredTest] = []
    for root in roots:
        base = project / root
        try:
            if not base.is_dir():
                continue
        except OSError:                                  # pragma: no cover - unreadable root
            continue
        for path in _walk(project, base, ".py"):
            if not _matches_glob(path.name, glob):
                continue
            out.extend(discover_unittest_file(project, path))
            if len(out) >= MAX_TESTS:
                return out[:MAX_TESTS]
    return out


# --------------------------------------------------------------------------- the whole probe

def discover(project: str | os.PathLike, *, build_dir: str | os.PathLike | None = None,
             roots: Sequence[str] = DEFAULT_TEST_ROOTS,
             glob: str = DEFAULT_PYTHON_GLOB,
             runners: Sequence[str] = (RUNNER_CTEST, RUNNER_UNITTEST),
             timeout: float = CTEST_TIMEOUT,
             ctest: str = "ctest") -> dict:
    """Every test `project` has, from every runner asked for, as a JSON-serialisable dict.

    Small, sorted and total: it never raises for a project it cannot read, because this runs
    behind a button in a pane and a traceback there helps nobody.  The shape is

        {"version", "project", "build_dir", "tests": [DiscoveredTest.to_dict()],
         "counts": {"ctest": n, "unittest": n, "total": n}, "truncated": bool}

    and `tests` is the discovery half of the wire contract's TestRecord.
    """
    project = Path(os.path.abspath(Path(project).expanduser()))
    build = Path(build_dir) if build_dir is not None else project / DEFAULT_BUILD_DIR
    tests: list[DiscoveredTest] = []
    if RUNNER_CTEST in runners:
        try:
            tests.extend(discover_ctest(project, build, roots=roots, timeout=timeout,
                                        ctest=ctest))
        except Exception:                                # pragma: no cover - belt and braces
            pass
    if RUNNER_UNITTEST in runners:
        try:
            tests.extend(discover_unittest(project, roots=roots, glob=glob))
        except Exception:                                # pragma: no cover - belt and braces
            pass
    truncated = len(tests) > MAX_TESTS
    tests = tests[:MAX_TESTS]
    counts = {runner: sum(1 for t in tests if t.runner == runner) for runner in RUNNERS}
    counts["total"] = len(tests)
    return {
        "version": PROBE_VERSION,
        "project": str(project),
        "build_dir": str(build),
        "tests": [t.to_dict() for t in tests],
        "counts": counts,
        "truncated": truncated,
    }
