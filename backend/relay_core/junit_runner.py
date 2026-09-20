# SPDX-License-Identifier: AGPL-3.0-or-later
"""`python3 -m unittest discover`, with JUnit XML out.

The C++ half of this project already emits per-test records: `ctest --output-junit <file>` writes
one `<testcase>` per test with its duration and outcome.  The Python half is one ctest entry —
`backend-and-bash` — so without this module the whole stdlib suite is a single pass/fail with a
single duration, and nothing downstream can say which Python test is slow, flaky or never run.

There is no pytest here (`relay_core` is deliberately stdlib-only), so the equivalent is a
`unittest.TestResult` that times `startTest`/`stopTest` and writes the same JUnit XML CTest does:

    python3 -m relay_core.junit_runner --junit out.xml [-s tests] [-p 'test_*.py'] [names…]

The exit code is unittest's own — 0 when the run succeeded, 1 otherwise — so this is a drop-in
for `python3 -m unittest discover -s tests` in a script or a CI step.

`classname` is the test's **project-relative dotted module** plus its class
(`tests.test_board.CardTests`), computed from the source file rather than from `__module__`, so it
matches `test_probe.discover_unittest()`'s ids exactly however discovery was rooted; `file` and
`line` point at the method, the two attributes that make a `<testcase>` a link to source.
"""
from __future__ import annotations

import argparse
import inspect
import os
import sys
import time
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

#: How much of a traceback or skip reason travels into the XML.  A JUnit file is read by a pane,
#: not by a person scrolling: the first lines are the message, the rest is in the log.
MAX_EXCERPT = 4000


def _relative_module(path: str | None, root: Path) -> str:
    """`/repo/tests/test_board.py` under `/repo` → `tests.test_board`; "" when not derivable."""
    if not path:
        return ""
    try:
        rel = Path(path).resolve().relative_to(root.resolve())
    except (ValueError, OSError):
        return ""
    return rel.as_posix()[:-3].replace("/", ".") if rel.suffix == ".py" else ""


def _location(test: unittest.TestCase, root: Path) -> tuple[str, str, int]:
    """`(classname, file, line)` for one test, all best-effort and never raising."""
    cls = type(test)
    try:
        source_file = inspect.getsourcefile(cls) or ""
    except (TypeError, OSError):                          # pragma: no cover - exotic loaders
        source_file = ""
    module = _relative_module(source_file, root) or getattr(cls, "__module__", "")
    classname = f"{module}.{cls.__qualname__}" if module else cls.__qualname__
    rel = ""
    if source_file:
        try:
            rel = Path(source_file).resolve().relative_to(root.resolve()).as_posix()
        except (ValueError, OSError):
            rel = source_file
    line = 0
    method = getattr(cls, getattr(test, "_testMethodName", ""), None)
    if method is not None:
        try:
            line = inspect.getsourcelines(method)[1]
        except (TypeError, OSError):                      # pragma: no cover - C or exec'd method
            line = 0
    return classname, rel, line


class JUnitResult(unittest.TextTestResult):
    """A `TestResult` that remembers each test's wall time and outcome, then writes JUnit XML.

    Wall time is measured in `startTest`/`stopTest`, so it covers `setUp` and `tearDown` exactly
    as a runner's own reported duration does — the number a "slow test" verdict must be built on.
    The outcome is collected into a pending slot rather than written to the last recorded case:
    `unittest` calls `addFailure` *before* `stopTest`, so the case is only appended once both the
    duration and the verdict are known.
    """

    def __init__(self, *args, root: Path | None = None, **kwargs):
        super().__init__(*args, **kwargs)
        self.root = Path(root or os.getcwd())
        self.cases: list[dict] = []
        self._started = 0.0
        self._pending = {"result": "pass", "message": "", "excerpt": ""}

    def startTest(self, test):
        self._pending = {"result": "pass", "message": "", "excerpt": ""}
        self._started = time.monotonic()
        super().startTest(test)

    def stopTest(self, test):
        elapsed = max(0.0, time.monotonic() - self._started)
        classname, file, line = _location(test, self.root)
        case = {"classname": classname,
                "name": getattr(test, "_testMethodName", "") or str(test),
                "time": elapsed, "file": file, "line": line}
        case.update(self._pending)
        self.cases.append(case)
        super().stopTest(test)

    def _outcome(self, kind: str, message: str, excerpt: str = "") -> None:
        self._pending = {"result": kind, "message": message.strip()[:200],
                         "excerpt": excerpt[-MAX_EXCERPT:]}

    def addFailure(self, test, err):
        super().addFailure(test, err)
        text = self._exc_info_to_string(err, test)
        self._outcome("fail", text.strip().splitlines()[-1] if text.strip() else "failed", text)

    def addError(self, test, err):
        super().addError(test, err)
        text = self._exc_info_to_string(err, test)
        self._outcome("error", text.strip().splitlines()[-1] if text.strip() else "error", text)

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._outcome("skip", reason or "skipped")

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self._outcome("skip", "expected failure")

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self._outcome("fail", "unexpected success")

    def to_xml(self, name: str = "unittest") -> ET.ElementTree:
        """The whole run as a `<testsuite>` document, in CTest's `--output-junit` shape."""
        counts = {"fail": 0, "error": 0, "skip": 0}
        for case in self.cases:
            if case["result"] in counts:
                counts[case["result"]] += 1
        suite = ET.Element("testsuite", {
            "name": name, "tests": str(len(self.cases)),
            "failures": str(counts["fail"]), "errors": str(counts["error"]),
            "skipped": str(counts["skip"]), "disabled": "0",
            "hostname": _hostname(),
            "time": f"{sum(c['time'] for c in self.cases):.6f}",
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime()),
        })
        for case in self.cases:
            attrs = {"classname": case["classname"], "name": case["name"],
                     "time": f"{case['time']:.6f}",
                     "status": "run" if case["result"] == "pass" else case["result"]}
            if case["file"]:
                attrs["file"] = case["file"]
            if case["line"]:
                attrs["line"] = str(case["line"])
            node = ET.SubElement(suite, "testcase", attrs)
            tag = {"fail": "failure", "error": "error", "skip": "skipped"}.get(case["result"])
            if tag:
                child = ET.SubElement(node, tag, {"message": case["message"] or tag})
                if case["excerpt"]:
                    child.text = case["excerpt"]
        return ET.ElementTree(suite)

    def write_xml(self, path: str | os.PathLike, name: str = "unittest") -> Path:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        self.to_xml(name).write(str(path), encoding="utf-8", xml_declaration=True)
        return path


def _hostname() -> str:
    import socket
    try:
        return socket.gethostname()
    except OSError:                                       # pragma: no cover - nameless host
        return ""


def build_suite(start_dir: str, pattern: str, names: list[str],
                top_level_dir: str | None = None) -> unittest.TestSuite:
    """`names` if any were given (dotted, as `python3 -m unittest` takes them), else discovery."""
    loader = unittest.TestLoader()
    if names:
        return loader.loadTestsFromNames(names)
    return loader.discover(start_dir, pattern=pattern, top_level_dir=top_level_dir)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python3 -m relay_core.junit_runner",
        description="Run the stdlib unittest suite and write JUnit XML beside its usual output.")
    parser.add_argument("names", nargs="*", help="dotted test names; default is discovery")
    parser.add_argument("--junit", metavar="PATH", help="write JUnit XML here")
    parser.add_argument("-s", "--start-directory", default="tests", metavar="DIR")
    parser.add_argument("-t", "--top-level-directory", default=None, metavar="DIR")
    parser.add_argument("-p", "--pattern", default="test_*.py", metavar="GLOB")
    parser.add_argument("-v", "--verbose", action="count", default=1)
    parser.add_argument("-q", "--quiet", action="store_true")
    parser.add_argument("--suite-name", default="unittest",
                        help="the <testsuite name=…> attribute")
    parser.add_argument("--root", default=None,
                        help="project root that file= and classname= are relative to (cwd)")
    args = parser.parse_args(argv)

    root = Path(args.root or os.getcwd())
    suite = build_suite(args.start_directory, args.pattern, args.names,
                        args.top_level_directory)

    def factory(stream, descriptions, verbosity, **kwargs):
        return JUnitResult(stream, descriptions, verbosity, root=root, **kwargs)

    runner = unittest.TextTestRunner(verbosity=0 if args.quiet else args.verbose,
                                     resultclass=factory)
    result = runner.run(suite)
    if args.junit and isinstance(result, JUnitResult):
        result.write_xml(args.junit, args.suite_name)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":                                # pragma: no cover
    sys.exit(main())
