# SPDX-License-Identifier: AGPL-3.0-or-later
"""TeX-to-PDF document workspace: find the root, build it, read the log, sync (#WYGY).

The engine behind the first document plugin of #MEPR. It is Qt-free and has no
wire protocol of its own; the pane and tool layers call into it.

- `check_dependencies` reports latexmk, the TeX engines, synctex, bibtex and biber.
- `detect_root` finds the main file of a project (`% !TEX root`, `\\documentclass`,
  a latexmkrc's `@default_files`, or the file that includes the one opened), and
  `find_dependencies` lists what it pulls in (`\\input`, `\\include`, `\\subfile`,
  `\\import`, `\\bibliography`, `\\addbibresource`, `\\includegraphics`) so the
  editor group can open them together.
- `TexBuilder` runs latexmk (argv lists only, never a shell string) on a worker
  thread. Requests are debounced, a new one supersedes a running build (its process
  group is killed), and every build gets a generation id and the source revision it
  read. A good build is published as one generation -- PDF, SyncTeX map and log,
  cross-checked against each other -- by `GenerationStore`, which swaps
  `current.json` atomically and refuses any generation older than the one it holds,
  so the last good PDF survives failures and a late completion cannot replace a
  newer one.
- `parse_log` / `parse_blg` turn the LaTeX log (either 79-column wrapped or not) and
  the BibTeX/biber log into `Diagnostic`s with absolute source paths.
- `forward_search` / `inverse_search` wrap `synctex view` and `synctex edit`.
- `Transport` is where a build runs. `LocalTransport` is this machine; an SSH
  implementation plugs in with the same three methods, and `PathMap` maps the build
  host's root to the paths the editor uses.

Paths passed *into* a builder (root, main file, work dir) are build-host paths.
Everything it hands back for navigation (diagnostics, dependencies, SyncTeX results)
has gone through its `PathMap`, so it is in the editor's terms.
"""
from __future__ import annotations

import contextlib
import gzip
import hashlib
import json
import logging
import ntpath
import os
import posixpath
import re
import shutil
import signal
import subprocess
import threading
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Protocol, Sequence

from relay_core.filelock import LOCK_EX, LOCK_UN, flock

log = logging.getLogger(__name__)

TOOLS = ("latexmk", "pdflatex", "xelatex", "lualatex", "synctex", "bibtex", "biber")

# engine name -> (TeX program, latexmk flag)
ENGINES = {
    "pdf": ("pdflatex", "-pdf"),
    "xelatex": ("xelatex", "-xelatex"),
    "lualatex": ("lualatex", "-lualatex"),
}
_ENGINE_ALIASES = {
    "pdf": "pdf", "pdflatex": "pdf", "pdftex": "pdf", "latex": "pdf",
    "xe": "xelatex", "xelatex": "xelatex", "xetex": "xelatex",
    "lua": "lualatex", "lualatex": "lualatex", "luatex": "lualatex", "lualatexmk": "lualatex",
}
# latexmkrc `$pdf_mode`: 1 pdflatex, 4 lualatex, 5 xelatex
_PDF_MODES = {"1": "pdf", "4": "lualatex", "5": "xelatex"}

IDLE, BUILDING, LIVE, STALE, FAILED = "idle", "building", "live", "stale", "failed"
STATES = (IDLE, BUILDING, LIVE, STALE, FAILED)

# The log lines are not wrapped when TeX is given a large max_print_line; the
# parser still unwraps logs made without it (another tool's build, older runs).
BUILD_ENV = {"max_print_line": "10000", "error_line": "254", "half_error_line": "238"}
TEX_WRAP = 79


def normalize_engine(name: str | None) -> str | None:
    if not name:
        return None
    return _ENGINE_ALIASES.get(name.strip().lower())


class TexError(Exception):
    """A TeX workspace operation could not be done; the message says why."""


class TexToolMissing(TexError):
    pass


class GenerationRejected(TexError):
    """A generation was not published. `reasons` lists what was wrong with it."""

    def __init__(self, message: str, reasons: Sequence[str] = ()):
        super().__init__(message)
        self.reasons = list(reasons) or [message]


class GenerationSuperseded(GenerationRejected):
    """An equal or newer generation is already published."""


# --------------------------------------------------------------------------- transport


@dataclass
class RunResult:
    returncode: int
    output: str = ""
    cancelled: bool = False
    missing: bool = False  # the program itself was not found
    timed_out: bool = False


class Transport(Protocol):
    """Where a build runs. `remote` is True when paths are on another host.

    `run` must take an argv list and never hand it to a shell on this machine; an
    SSH implementation quotes each element for the remote shell. It returns when the
    process exits, when `cancel` is set (after killing it) or on `timeout`.
    `fetch` returns the bytes of each path, or None for a missing one.
    `makedirs` creates directories (and parents) that may already exist.
    """

    remote: bool

    def run(self, argv: Sequence[str], cwd: str, *, env: dict[str, str] | None = None,
            cancel: threading.Event | None = None,
            timeout: float | None = None) -> RunResult: ...

    def fetch(self, paths: Sequence[str]) -> dict[str, bytes | None]: ...

    def makedirs(self, paths: Sequence[str]) -> None: ...


def _kill_tree(proc: subprocess.Popen) -> None:
    """Stop latexmk and the engine it started (its own session / Windows job object)."""
    try:
        if os.name == "nt":
            proc._relay_tree.stop()
            return
        os.killpg(proc.pid, signal.SIGTERM)
        try:
            proc.wait(timeout=3)
            return
        except subprocess.TimeoutExpired:
            pass
        os.killpg(proc.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError, OSError, AttributeError):
        pass


class LocalTransport:
    remote = False

    def run(self, argv, cwd, *, env=None, cancel=None, timeout=None):
        full_env = dict(os.environ)
        full_env.update(env or {})
        try:
            # Windows: suspended and windowless until attached to a job object, as in jobs.py
            proc = subprocess.Popen(list(argv), cwd=cwd, env=full_env, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    start_new_session=os.name != "nt",
                                    creationflags=0x08000004 if os.name == "nt" else 0)
        except FileNotFoundError as exc:
            return RunResult(127, str(exc), missing=True)
        except OSError as exc:
            return RunResult(126, str(exc))
        if os.name == "nt":
            from relay_core.windows_jobs import attach
            attach(proc)
        deadline = None if timeout is None else time.monotonic() + timeout
        chunks: list[bytes] = []
        while True:
            try:
                out, _ = proc.communicate(timeout=0.05)
                chunks.append(out or b"")
                break
            except subprocess.TimeoutExpired:
                pass
            stop_cancel = cancel is not None and cancel.is_set()
            stop_timeout = deadline is not None and time.monotonic() > deadline
            if stop_cancel or stop_timeout:
                _kill_tree(proc)
                try:
                    out, _ = proc.communicate(timeout=5)
                    chunks.append(out or b"")
                except (subprocess.TimeoutExpired, ValueError):
                    pass
                text = b"".join(chunks).decode("utf-8", "replace")
                return RunResult(proc.returncode if proc.returncode is not None else -1, text,
                                 cancelled=stop_cancel, timed_out=stop_timeout and not stop_cancel)
        return RunResult(proc.returncode, b"".join(chunks).decode("utf-8", "replace"))

    def fetch(self, paths):
        out: dict[str, bytes | None] = {}
        for p in paths:
            try:
                out[p] = Path(p).read_bytes()
            except OSError:
                out[p] = None
        return out

    def makedirs(self, paths):
        for p in paths:
            os.makedirs(p, exist_ok=True)


@dataclass(frozen=True)
class PathMap:
    """Maps the build host's project root to the root the editor uses.

    Identity when both are the same (a local build). A path outside `build_root` is
    returned normalised but otherwise unchanged (system files, TeX Live paths)."""

    build_root: str
    local_root: str

    @staticmethod
    def _mod(path: str):
        return posixpath if path.startswith("/") else ntpath if re.match(r"^[A-Za-z]:[\\/]", path) \
            else os.path

    @staticmethod
    def _inside(mod, root: str, path: str) -> str | None:
        root = mod.normpath(root)
        path = mod.normpath(path)
        if path == root:
            return ""
        prefix = root if root.endswith(mod.sep) else root + mod.sep
        return path[len(prefix):] if path.startswith(prefix) else None

    def _convert(self, path: str, src: str, dst: str) -> str:
        smod, dmod = self._mod(src), self._mod(dst)
        rel = self._inside(smod, src, path)
        if rel is None:
            return smod.normpath(path)
        if rel == "":
            return dmod.normpath(dst)
        return dmod.normpath(dmod.join(dst, *rel.split(smod.sep)))

    def to_local(self, build_path: str) -> str:
        return self._convert(build_path, self.build_root, self.local_root)

    def to_build(self, local_path: str) -> str:
        return self._convert(local_path, self.local_root, self.build_root)

    def relative(self, local_path: str) -> str | None:
        """`local_path` relative to the local root, with `/` separators, or None."""
        mod = self._mod(self.local_root)
        rel = self._inside(mod, self.local_root, local_path)
        return None if rel is None else rel.replace(mod.sep, "/")


# --------------------------------------------------------------------------- dependencies


@dataclass
class ToolInfo:
    name: str
    available: bool
    path: str | None = None
    version: str | None = None


def _version_argv(tool: str) -> list[str]:
    if tool == "latexmk":
        return [tool, "-v"]
    if tool == "synctex":
        return [tool, "help"]
    return [tool, "--version"]


def _version_line(text: str) -> str | None:
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    for ln in lines:
        if re.search(r"\bversion\b", ln, re.I) or re.search(r"\d+\.\d+", ln):
            return ln
    return lines[0] if lines else None


def check_dependencies(*, versions: bool = False, transport: Transport | None = None,
                       tools: Sequence[str] = TOOLS, cwd: str | None = None,
                       timeout: float = 15) -> dict[str, ToolInfo]:
    """Which TeX tools exist here (or on `transport`'s host), with versions on request.

    Locally, presence is a PATH lookup and costs nothing; `versions=True` runs each
    tool once. Through a remote transport every tool is probed by running it."""
    result: dict[str, ToolInfo] = {}
    remote = transport is not None and transport.remote
    runner = transport or LocalTransport()
    for tool in tools:
        if not remote:
            path = shutil.which(tool)
            info = ToolInfo(tool, path is not None, path)
            if path and versions:
                r = runner.run(_version_argv(path), cwd or os.getcwd(), timeout=timeout)
                info.version = _version_line(r.output)
            result[tool] = info
            continue
        r = runner.run(_version_argv(tool), cwd or "/", timeout=timeout)
        ok = not r.missing and r.returncode != 127 and not r.timed_out
        result[tool] = ToolInfo(tool, ok, tool if ok else None,
                                _version_line(r.output) if ok and versions else None)
    return result


def missing_tools(tools: dict[str, ToolInfo], engine: str = "pdf") -> list[str]:
    """Tools a build with `engine` cannot do without (synctex and bib tools are optional)."""
    engine = normalize_engine(engine) or "pdf"
    need = ["latexmk", ENGINES[engine][0]]
    return [t for t in need if not (tools.get(t) and tools[t].available)]


class LocalFS:
    """What root detection and dependency discovery read. Swap for a remote one."""

    def read_text(self, path: str) -> str | None:
        try:
            return Path(path).read_bytes().decode("utf-8", "replace")
        except OSError:
            return None

    def exists(self, path: str) -> bool:
        return os.path.isfile(path)

    def is_dir(self, path: str) -> bool:
        return os.path.isdir(path)

    def listdir(self, path: str) -> list[str]:
        try:
            return sorted(os.listdir(path))
        except OSError:
            return []


LOCAL_FS = LocalFS()


class TransportFS:
    """A `LocalFS` over a transport, caching each file's bytes for one scan."""

    def __init__(self, transport: Transport):
        self.transport = transport
        self.cache: dict[str, bytes | None] = {}

    def read_bytes(self, path: str) -> bytes | None:
        if path not in self.cache:
            self.cache.update(self.transport.fetch([path]))
        return self.cache.get(path)

    def read_many(self, paths: Iterable[str]) -> dict[str, bytes | None]:
        todo = [p for p in paths if p not in self.cache]
        if todo:
            self.cache.update(self.transport.fetch(todo))
        return {p: self.cache.get(p) for p in paths}

    def read_text(self, path):
        data = self.read_bytes(path)
        return None if data is None else data.decode("utf-8", "replace")

    def exists(self, path):
        return self.read_bytes(path) is not None

    def is_dir(self, path):
        return False

    def listdir(self, path):
        return []


def strip_comments(text: str) -> str:
    """Drop `%` comments (not `\\%`), keeping every line so line numbers still hold."""
    out = []
    for line in text.split("\n"):
        i = 0
        while True:
            j = line.find("%", i)
            if j < 0:
                break
            k, slashes = j - 1, 0
            while k >= 0 and line[k] == "\\":
                slashes += 1
                k -= 1
            if slashes % 2 == 0:
                line = line[:j]
                break
            i = j + 1
        out.append(line)
    return "\n".join(out)


_MAGIC_ROOT = re.compile(r"^[ \t]*%[ \t]*!\s*tex\s+root\s*=\s*(.+?)\s*$", re.I | re.M)
_MAGIC_PROGRAM = re.compile(r"^[ \t]*%[ \t]*!\s*tex\s+(?:ts-)?program\s*=\s*(\S+)", re.I | re.M)
_DOCUMENTCLASS = re.compile(r"\\documentclass\s*(?:\[[^\]]*\])?\s*\{")
_BEGIN_DOCUMENT = re.compile(r"\\begin\s*\{document\}")
_RC_NAMES = ("latexmkrc", ".latexmkrc")
_RC_DEFAULT_FILES = re.compile(r"@default_files\s*=\s*\(\s*['\"]([^'\"]+)['\"]")
_RC_PDF_MODE = re.compile(r"\$pdf_mode\s*=\s*(\d)")
_PREFERRED_MAIN = ("main.tex", "paper.tex", "thesis.tex", "document.tex", "manuscript.tex",
                   "article.tex", "report.tex", "book.tex")


@dataclass
class RootInfo:
    main_file: str           # absolute path of the root .tex
    root_dir: str            # directory latexmk runs in: the main file's directory
    reason: str              # magic-comment | documentclass | latexmkrc | includer | candidate
    engine: str | None = None        # from `% !TEX program` or latexmkrc $pdf_mode
    project_rc: str | None = None    # a latexmkrc in the root (runs only when trusted)

    def to_dict(self):
        return asdict(self)


def _head(text: str, lines: int = 50) -> str:
    return "\n".join(text.split("\n")[:lines])


def _rc_info(directory: str, fs) -> tuple[str | None, str | None, str | None]:
    """(rc path, default main file, engine) of a latexmkrc in `directory`."""
    for name in _RC_NAMES:
        rc = os.path.join(directory, name)
        text = fs.read_text(rc)
        if text is None:
            continue
        code = "\n".join(ln.split("#", 1)[0] for ln in text.split("\n"))
        m = _RC_DEFAULT_FILES.search(code)
        default = os.path.normpath(os.path.join(directory, m.group(1))) if m else None
        if default and not default.endswith(".tex") and fs.exists(default + ".tex"):
            default += ".tex"
        pm = _RC_PDF_MODE.search(code)
        return rc, default, _PDF_MODES.get(pm.group(1)) if pm else None
    return None, None, None


def _root_info(main: str, reason: str, fs, text: str | None = None) -> RootInfo:
    text = fs.read_text(main) if text is None else text
    root_dir = os.path.dirname(main)
    rc, _, rc_engine = _rc_info(root_dir, fs)
    engine = None
    if text:
        m = _MAGIC_PROGRAM.search(_head(text))
        engine = normalize_engine(m.group(1)) if m else None
    return RootInfo(main, root_dir, reason, engine or rc_engine, rc)


def _is_root_text(text: str | None) -> bool:
    return bool(text) and bool(_DOCUMENTCLASS.search(strip_comments(text)))


def _candidates(directory: str, fs) -> list[str]:
    roots = []
    for name in fs.listdir(directory):
        if name.lower().endswith(".tex"):
            path = os.path.join(directory, name)
            if _is_root_text(fs.read_text(path)):
                roots.append(path)

    def rank(path):
        name = os.path.basename(path).lower()
        text = strip_comments(fs.read_text(path) or "")
        return (0 if _BEGIN_DOCUMENT.search(text) else 1,
                _PREFERRED_MAIN.index(name) if name in _PREFERRED_MAIN else len(_PREFERRED_MAIN),
                0 if name[:-4] == os.path.basename(directory).lower() else 1,
                name)
    return sorted(roots, key=rank)


def detect_root(path: str, *, fs=LOCAL_FS, search_parents: int = 2) -> RootInfo | None:
    """Find the main file for `path` (a .tex file or a project directory).

    Order: a `% !TEX root = ...` magic comment in the first 50 lines (followed up to
    four hops), the file itself when it has `\\documentclass`, a latexmkrc's
    `@default_files`, then a root file in the same or a parent directory that pulls
    `path` in through \\input/\\include. Returns None when nothing qualifies."""
    path = os.path.abspath(path)
    if fs.is_dir(path):
        rc, default, _ = _rc_info(path, fs)
        if default and fs.exists(default):
            return _root_info(default, "latexmkrc", fs)
        cands = _candidates(path, fs)
        return _root_info(cands[0], "candidate", fs) if cands else None

    seen = set()
    current = path
    for _ in range(5):
        if current in seen:
            break
        seen.add(current)
        text = fs.read_text(current)
        if text is None:
            return None
        m = _MAGIC_ROOT.search(_head(text))
        if m:
            target = m.group(1).strip().strip('"')
            target = os.path.normpath(os.path.join(os.path.dirname(current), target))
            if not fs.exists(target) and fs.exists(target + ".tex"):
                target += ".tex"
            if fs.exists(target) and target != current:
                current = target
                continue
        if _is_root_text(text):
            return _root_info(current, "magic-comment" if current != path else "documentclass",
                              fs, text)
        break

    directory = os.path.dirname(path)
    for _ in range(search_parents + 1):
        rc, default, _ = _rc_info(directory, fs)
        cands = ([default] if default and fs.exists(default) else []) + \
            [c for c in _candidates(directory, fs) if c != default]
        for cand in cands:
            deps = find_dependencies(cand, fs=fs, include_graphics=False)
            if any(d.path == path for d in deps):
                return _root_info(cand, "includer", fs)
        parent = os.path.dirname(directory)
        if parent == directory:
            break
        directory = parent
    return None


@dataclass(frozen=True)
class Dependency:
    kind: str                # main | input | include | subfile | import | bibliography | bibresource | graphic
    path: str                # absolute
    exists: bool
    source: str | None = None    # the file that references it
    line: int | None = None      # 1-based line in `source`

    @property
    def editable(self) -> bool:
        return self.kind != "graphic"

    def to_dict(self):
        d = asdict(self)
        d["editable"] = self.editable
        return d


_DEP_PATTERNS = [
    ("input", re.compile(r"\\(input|include|subfile|InputIfFileExists)\s*\{([^{}]*)\}")),
    ("input-tex", re.compile(r"\\input(?![A-Za-z@{])\s+([^\s{}\\%]+)")),
    ("import", re.compile(r"\\(sub)?(?:import|inputfrom|includefrom)\*?\s*\{([^{}]*)\}\s*\{([^{}]*)\}")),
    ("bibliography", re.compile(r"\\bibliography\s*\{([^{}]*)\}")),
    ("bibresource", re.compile(r"\\addbibresource\s*(?:\[[^\]]*\])?\s*\{([^{}]*)\}")),
    ("graphic", re.compile(r"\\includegraphics\s*\*?\s*(?:\[[^\]]*\])?\s*\{([^{}]*)\}")),
]
_GRAPHIC_EXTS = ("", ".pdf", ".png", ".jpg", ".jpeg", ".eps", ".svg")


def _resolve(base: str, name: str, exts: Sequence[str], fs) -> tuple[str, bool]:
    """The first of `name` + each extension that exists, else the name TeX would want."""
    name = name.strip().strip('"')
    base_path = os.path.normpath(os.path.join(base, os.path.expanduser(name)))
    for ext in exts:
        if fs.exists(base_path + ext):
            return base_path + ext, True
    if os.path.splitext(name)[1] or not any(exts):
        return base_path, False
    return base_path + next(e for e in exts if e), False


def find_dependencies(main_file: str, *, fs=LOCAL_FS, include_graphics: bool = True,
                      max_depth: int = 12) -> list[Dependency]:
    """Everything the root pulls in, in document order, main file first.

    Relative names resolve against the main file's directory (TeX's working
    directory), except `\\subimport`, which is relative to the including file.
    Missing files are listed with `exists=False` so the workspace can say so."""
    main_file = os.path.abspath(main_file)
    root = os.path.dirname(main_file)
    out: list[Dependency] = [Dependency("main", main_file, fs.exists(main_file))]
    seen = {main_file}

    def add(dep: Dependency) -> bool:
        if dep.path in seen:
            return False
        seen.add(dep.path)
        out.append(dep)
        return True

    def walk(tex: str, depth: int, import_dir: str):
        text = fs.read_text(tex)
        if text is None or depth > max_depth:
            return
        body = strip_comments(text)
        hits = []
        for kind, rx in _DEP_PATTERNS:
            if kind == "graphic" and not include_graphics:
                continue
            for m in rx.finditer(body):
                hits.append((m.start(), kind, m))
        hits.sort(key=lambda h: h[0])
        for pos, kind, m in hits:
            line = body.count("\n", 0, pos) + 1
            if kind == "input":
                cmd, name = m.group(1), m.group(2)
                if not name.strip() or "\\" in name or "#" in name:
                    continue
                exts = (".tex",) if cmd == "include" else (".tex", "")
                path, ok = _resolve(import_dir, name, exts, fs)
                dkind = {"include": "include", "subfile": "subfile"}.get(cmd, "input")
                if add(Dependency(dkind, path, ok, tex, line)) and ok:
                    walk(path, depth + 1, import_dir)
            elif kind == "input-tex":
                name = m.group(1)
                if "\\" in name or "#" in name:
                    continue
                path, ok = _resolve(import_dir, name, (".tex", ""), fs)
                if add(Dependency("input", path, ok, tex, line)) and ok:
                    walk(path, depth + 1, import_dir)
            elif kind == "import":
                sub, directory, name = m.group(1), m.group(2), m.group(3)
                if "\\" in directory + name:
                    continue
                base = os.path.dirname(tex) if sub else root
                new_dir = os.path.normpath(os.path.join(base, directory.strip()))
                path, ok = _resolve(new_dir, name, (".tex", ""), fs)
                if add(Dependency("import", path, ok, tex, line)) and ok:
                    walk(path, depth + 1, new_dir)
            elif kind in ("bibliography", "bibresource"):
                names = m.group(1).split(",") if kind == "bibliography" else [m.group(1)]
                for name in names:
                    name = name.strip()
                    if not name or "\\" in name:
                        continue
                    exts = (".bib",) if not name.endswith(".bib") else ("",)
                    path, ok = _resolve(root, name, exts, fs)
                    add(Dependency(kind, path, ok, tex, line))
            elif kind == "graphic":
                name = m.group(1)
                if not name.strip() or "\\" in name:
                    continue
                path, ok = _resolve(root, name, _GRAPHIC_EXTS, fs)
                if ok:
                    add(Dependency("graphic", path, ok, tex, line))

    walk(main_file, 0, root)
    return out


def source_revision(contents: dict[str, bytes | None], root: str) -> str:
    """A short hash of these files' saved contents, keyed by path relative to `root`."""
    h = hashlib.sha256()
    for path in sorted(contents):
        rel = os.path.relpath(path, root) if os.path.isabs(path) else path
        data = contents[path]
        h.update(rel.replace(os.sep, "/").encode("utf-8", "surrogateescape") + b"\0")
        if data is None:
            h.update(b"\1missing\0")
        else:
            h.update(hashlib.sha256(data).digest())
    return h.hexdigest()[:20]


def fls_inputs(text: str, *, root: str, exclude: Sequence[str] = (), pathmod=os.path) -> list[str]:
    """Project files latexmk's `.fls` says the engine read (under `root`, not under
    any `exclude` directory such as the build's own output directory). `pathmod` is
    `posixpath` for a remote build host."""
    pwd = root
    found: list[str] = []
    seen = set()
    for line in text.splitlines():
        if line.startswith("PWD "):
            pwd = line[4:].strip()
        elif line.startswith("INPUT "):
            p = pathmod.normpath(pathmod.join(pwd, line[6:].strip()))
            if p in seen:
                continue
            seen.add(p)
            inside = PathMap._inside(pathmod, root, p)
            if not inside:
                continue
            if any(PathMap._inside(pathmod, ex, p) is not None for ex in exclude):
                continue
            found.append(p)
    return found


# --------------------------------------------------------------------------- diagnostics


@dataclass
class Diagnostic:
    severity: str            # error | warning | info
    file: str | None         # absolute, in the editor's terms, when known
    line: int | None
    message: str
    kind: str = "latex"      # latex | reference | citation | box | package | font | bibtex | biber | build
    end_line: int | None = None

    def to_dict(self):
        return asdict(self)


_MSG_START = re.compile(
    rb"^(?:! |[^\s:()]+:\d+: |(?:LaTeX|Package|Class|pdfTeX)[ \w]* (?:Warning|Error)|Overfull |"
    rb"Underfull |l\.\d+ )")


def unwrap_log(data: str | bytes, width: int | None | str = "auto") -> list[str]:
    """The log's lines with TeX's hard wrapping at `width` columns undone.

    TeX breaks every line at max_print_line (79) characters -- bytes for pdfTeX,
    characters for XeTeX/LuaTeX -- mid-word and mid-path. `"auto"` unwraps at 79
    unless some line is longer, which means the log was written unwrapped. Works on
    bytes so a UTF-8 character split across the break is reassembled."""
    raw = data.encode("utf-8", "surrogateescape") if isinstance(data, str) else data
    lines = [ln[:-1] if ln.endswith(b"\r") else ln for ln in raw.split(b"\n")]
    if width == "auto":
        # The banner (line 1) and the memory statistics are never wrapped; any other
        # line well past 79 means the log was written with a large max_print_line.
        width = None if any(len(ln) > 100 for ln in lines[1:]) else TEX_WRAP
    if not width:
        return [ln.decode("utf-8", "replace") for ln in lines]
    out, buf = [], b""
    for i, ln in enumerate(lines):
        buf += ln
        wrapped = len(ln) == width or len(ln.decode("utf-8", "replace")) == width
        nxt = lines[i + 1] if i + 1 < len(lines) else None
        if wrapped and nxt is not None and nxt != b"" and not _MSG_START.match(nxt):
            continue
        out.append(buf.decode("utf-8", "replace"))
        buf = b""
    if buf:
        out.append(buf.decode("utf-8", "replace"))
    return out


_FILE_LINE_ERROR = re.compile(r"^((?:[A-Za-z]:)?[^:]*?[^\s:]):(\d+): (.*)$")
_BANG_ERROR = re.compile(r"^! (.*)$")
_CONTEXT_LINE = re.compile(r"^l\.(\d+)")
_WARNING = re.compile(r"^(LaTeX Font|LaTeX3?|Package (\S+)|Class (\S+))\s+Warning: (.*)$")
_PDFTEX_WARNING = re.compile(r"^pdfTeX warning(?: \([^)]*\))?: (.*)$")
_BOX = re.compile(r"^((?:Over|Under)full \\[hv]box \([^)]*\).*?)(?: at lines? (\d+)(?:--(\d+))?)?\s*(?:\[\])?\s*$")
_INPUT_LINE = re.compile(r"on input line (\d+)")
_PAREN_TOKEN = re.compile(r'\((?:"([^"]*)"|([^\s()"\[\]{}<>]*))|\)')
_FILE_EXT = re.compile(r"\.[A-Za-z][A-Za-z0-9]{0,7}$")


def _looks_like_file(name: str) -> bool:
    if not name or len(name) > 4096:
        return False
    if name.startswith(("/", "./", "../", "~/", ".\\", "..\\")) or re.match(r"^[A-Za-z]:[\\/]", name):
        return True
    m = _FILE_EXT.search(name)
    if not m:
        return False
    stem = name[:m.start()]
    return bool(re.search(r"[A-Za-z_]", stem))


class _LogReader:
    def __init__(self, build_cwd: str, path_map: PathMap | None):
        self.cwd = build_cwd
        self.map = path_map

    def path(self, raw: str | None) -> str | None:
        if not raw:
            return None
        raw = raw.strip().strip('"')
        mod = PathMap._mod(self.cwd)
        p = mod.normpath(raw if mod.isabs(raw) else mod.join(self.cwd, raw))
        return self.map.to_local(p) if self.map else p


def parse_log(data: str | bytes, *, build_cwd: str, path_map: PathMap | None = None,
              wrap_width: int | None | str = "auto") -> list[Diagnostic]:
    """Errors and warnings in a LaTeX log, each pointing at an absolute source file.

    `build_cwd` is the directory the engine ran in (relative names in the log are
    relative to it); `path_map` then maps build paths to the editor's. Handles
    `-file-line-error` errors, `! ` errors (file from the log's open-file stack,
    line from the `l.N` context), LaTeX/package/class warnings with their `on input
    line N`, and over/underfull boxes with their line ranges."""
    reader = _LogReader(build_cwd, path_map)
    lines = unwrap_log(data, wrap_width)
    stack: list[str | None] = []
    diags: list[Diagnostic] = []

    def current() -> str | None:
        for f in reversed(stack):
            if f is not None:
                return f
        return None

    def scan_parens(line: str):
        for m in _PAREN_TOKEN.finditer(line):
            if m.group(0) == ")":
                if stack:
                    stack.pop()
                continue
            name = m.group(1) if m.group(1) is not None else m.group(2)
            stack.append(name if _looks_like_file(name) else None)

    def skip_context(i: int) -> tuple[int, int | None]:
        """Index after an error's context/help block, and the `l.N` line if any."""
        ctx_line, seen_ctx, j = None, False, i + 1
        limit = min(len(lines), i + 40)
        while j < limit:
            ln = lines[j]
            m = _CONTEXT_LINE.match(ln)
            if m and ctx_line is None:
                ctx_line, seen_ctx = int(m.group(1)), True
                j += 2  # l.N and the context's second half (often only spaces)
                continue
            if ln == "":
                if ctx_line is None:
                    # \PackageError puts a blank line before its help pointer and l.N
                    k = j + 1
                    while k < min(len(lines), j + 8) and lines[k] != "" \
                            and not _CONTEXT_LINE.match(lines[k]) \
                            and not _MSG_START.match(lines[k].encode("utf-8", "replace")):
                        k += 1
                    if k < len(lines) and _CONTEXT_LINE.match(lines[k]):
                        j = k
                        continue
                return j + 1, ctx_line
            if _MSG_START.match(ln.encode("utf-8", "replace")) and not ln.startswith("l."):
                return j, ctx_line
            if seen_ctx and ln.startswith("("):
                opened = _PAREN_TOKEN.match(ln)
                if opened and _looks_like_file(opened.group(1) or opened.group(2) or ""):
                    return j, ctx_line  # no blank line after the help text: a file opens
            j += 1
        return j, ctx_line

    def continuation(i: int, pkg: str | None) -> tuple[str, int]:
        """Warning text continued on the next lines (`(pkg)   more`), and the next index."""
        parts, j = [], i + 1
        while j < len(lines) and j < i + 10:
            ln = lines[j]
            if ln == "" or _MSG_START.match(ln.encode("utf-8", "replace")):
                break
            if pkg and ln.startswith("(" + pkg + ")"):
                parts.append(ln[len(pkg) + 2:].strip())
            elif not pkg and ln.startswith(" ") and ln.strip():
                parts.append(ln.strip())
            else:
                break
            j += 1
        return " ".join(parts), j

    i = 0
    while i < len(lines):
        line = lines[i]
        m = _FILE_LINE_ERROR.match(line)
        if m and _looks_like_file(m.group(1)) and not line.startswith(("(", "[")):
            msg = m.group(3).strip()
            pm = re.match(r"Package (\S+) Error", msg)
            extra, _ = continuation(i, pm.group(1)) if pm else ("", i + 1)
            diags.append(Diagnostic("error", reader.path(m.group(1)), int(m.group(2)),
                                    (msg + " " + extra).strip(), "package" if pm else "latex"))
            i, _ = skip_context(i)
            continue
        m = _BANG_ERROR.match(line)
        if m:
            msg = m.group(1).strip()
            nxt, ctx_line = skip_context(i)
            diags.append(Diagnostic("error", reader.path(current()), ctx_line, msg, "latex"))
            i = nxt
            continue
        m = _WARNING.match(line)
        if m:
            who, pkg, cls, msg = m.group(1), m.group(2), m.group(3), m.group(4).strip()
            extra, nxt = continuation(i, pkg or cls)
            if extra:
                msg = f"{msg} {extra}"
            lm = _INPUT_LINE.search(msg)
            if who == "LaTeX Font":
                kind, sev = "font", "info"
            elif re.search(r"\bCitation\b.*\bundefined\b|undefined citations", msg):
                kind, sev = "citation", "warning"
            elif re.search(r"\bReference\b.*\bundefined\b|undefined references", msg):
                kind, sev = "reference", "warning"
            else:
                kind, sev = ("package" if pkg or cls else "latex"), "warning"
            if pkg:
                msg = f"{pkg}: {msg}"
            diags.append(Diagnostic(sev, reader.path(current()), int(lm.group(1)) if lm else None,
                                    msg, kind))
            i = nxt
            continue
        m = _PDFTEX_WARNING.match(line)
        if m:
            diags.append(Diagnostic("warning", reader.path(current()), None, m.group(1).strip(),
                                    "latex"))
            i += 1
            continue
        m = _BOX.match(line)
        if m:
            start = int(m.group(2)) if m.group(2) else None
            end = int(m.group(3)) if m.group(3) else None
            diags.append(Diagnostic("warning", reader.path(current()), start,
                                    line.strip().rstrip("[]").strip(), "box",
                                    end if end and end != start else None))
            j = i + 1
            while j < len(lines) and j < i + 30 and lines[j] != "":
                j += 1
            i = j + 1
            continue
        scan_parens(line)
        i += 1

    others = [d for d in diags if d.severity == "error"
              and not re.match(r"Emergency stop|==> Fatal error occurred", d.message)]
    if others:
        diags = [d for d in diags if not (d.severity == "error"
                 and re.match(r"Emergency stop|==> Fatal error occurred", d.message))]
    unique, seen = [], set()
    for d in diags:
        key = (d.severity, d.file, d.line, d.message)
        if key not in seen:
            seen.add(key)
            unique.append(d)
    return unique


_BLG_AT_LINE = re.compile(r"^(.*?)---line (\d+) of file (.+?)\s*$")
_BIBER_MSG = re.compile(r"^\[\d+\] [\w:.]+> (WARN|ERROR) - (.*)$")
_BIBER_BIBFILE = re.compile(r"([^\s,']+?\.bib)'?, line (\d+)")
_BIBER_TMPFILE = re.compile(r"BibTeX subsystem: \S+?\.utf8, line (\d+)")
_BIBER_SOURCE = re.compile(r"(?:Found BibTeX data source|Looking for bibtex file) '([^']+)'")


def parse_blg(data: str | bytes, *, build_cwd: str, path_map: PathMap | None = None) -> list[Diagnostic]:
    """Errors and warnings in a BibTeX or biber `.blg` log."""
    text = data.decode("utf-8", "replace") if isinstance(data, bytes) else data
    reader = _LogReader(build_cwd, path_map)
    diags: list[Diagnostic] = []
    source = None  # biber reports syntax errors against a temp copy of this .bib
    for line in text.splitlines():
        sm = _BIBER_SOURCE.search(line)
        if sm:
            source = sm.group(1)
        m = _BIBER_MSG.match(line)
        if m:
            sev = "error" if m.group(1) == "ERROR" else "warning"
            msg = m.group(2).strip()
            file, lno = None, None
            fm, tm = _BIBER_BIBFILE.search(msg), _BIBER_TMPFILE.search(msg)
            if tm and source:
                file, lno = reader.path(source), int(tm.group(1))
                msg = _BIBER_TMPFILE.sub(f"{os.path.basename(source)}, line {tm.group(1)}", msg)
            elif fm:
                file, lno = reader.path(fm.group(1)), int(fm.group(2))
            diags.append(Diagnostic(sev, file, lno, msg, "citation" if "database entry" in msg
                                    else "biber"))
            continue
        if line.startswith("Warning--"):
            msg = line[len("Warning--"):].strip()
            diags.append(Diagnostic("warning", None, None, msg,
                                    "citation" if "database entry" in msg else "bibtex"))
            continue
        if line.startswith("while executing"):
            continue
        m = _BLG_AT_LINE.match(line)
        if m and m.group(3).endswith(".bib"):
            diags.append(Diagnostic("error", reader.path(m.group(3)), int(m.group(2)),
                                    m.group(1).strip() or "BibTeX error", "bibtex"))
            continue
        m = re.match(r"^(I couldn't open (?:database|style|file name).*|I found no .*)$", line)
        if m:
            fm = re.search(r"database file (\S+)", line)
            diags.append(Diagnostic("error", reader.path(fm.group(1)) if fm else None, None,
                                    m.group(1).strip(), "bibtex"))
    return diags


# --------------------------------------------------------------------------- synctex


@dataclass
class PdfLocation:
    """A box in the PDF, in PDF points from the page's top-left corner (SyncTeX's frame).

    `x`,`y` is the point synctex reports (the baseline start); `h`,`v`,`width`,`height`
    the box around it."""
    page: int
    x: float
    y: float
    h: float = 0.0
    v: float = 0.0
    width: float = 0.0
    height: float = 0.0

    def to_dict(self):
        return asdict(self)


@dataclass
class SourceLocation:
    file: str                  # absolute, in the editor's terms
    line: int
    column: int | None = None  # 0-based; None when synctex does not know
    relative: str | None = None

    def to_dict(self):
        return asdict(self)


def parse_synctex_records(text: str) -> list[dict[str, str]]:
    """The key/value records of a `synctex view` or `synctex edit` reply.

    Each result starts with an `Output:` line; values may contain colons (Windows
    paths), so only the first colon separates key from value."""
    records: list[dict[str, str]] = []
    cur: dict[str, str] | None = None
    inside = False
    for line in text.splitlines():
        if line.startswith("SyncTeX result begin"):
            inside, cur = True, None
            continue
        if line.startswith("SyncTeX result end"):
            inside = False
            continue
        if not inside or ":" not in line:
            continue
        key, _, value = line.partition(":")
        if key == "Output" or cur is None:
            cur = {}
            records.append(cur)
        cur[key] = value
    return records


def _synctex_bin(synctex: str | None) -> str:
    exe = synctex or shutil.which("synctex")
    if not exe:
        raise TexToolMissing("synctex is not installed, so PDF and source cannot be linked")
    return exe


def _run_synctex(argv: list[str], cwd: str, timeout: float) -> str:
    try:
        proc = subprocess.run(argv, cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True,
                              timeout=timeout)
    except FileNotFoundError as exc:
        raise TexToolMissing(str(exc)) from exc
    except subprocess.TimeoutExpired as exc:
        raise TexError(f"synctex timed out after {timeout:g}s") from exc
    return (proc.stdout + proc.stderr).decode("utf-8", "replace")


def forward_search(pdf: str, source_file: str, line: int, column: int = 0, *,
                   path_map: PathMap | None = None, synctex: str | None = None,
                   timeout: float = 10) -> list[PdfLocation]:
    """Where source `line` (1-based) of `source_file` lands in `pdf`; best match first.

    `source_file` is in the editor's terms; `path_map` turns it into the build path
    that the `.synctex.gz` beside `pdf` recorded. Empty when synctex has no match."""
    build_path = path_map.to_build(source_file) if path_map else source_file
    argv = [_synctex_bin(synctex), "view", "-i", f"{int(line)}:{max(0, int(column))}:{build_path}",
            "-o", pdf]
    out: list[PdfLocation] = []
    for rec in parse_synctex_records(_run_synctex(argv, os.path.dirname(pdf) or ".", timeout)):
        try:
            out.append(PdfLocation(int(rec["Page"]), float(rec["x"]), float(rec["y"]),
                                   float(rec.get("h", 0)), float(rec.get("v", 0)),
                                   float(rec.get("W", 0)), float(rec.get("H", 0))))
        except (KeyError, ValueError):
            continue
    return out


def inverse_search(pdf: str, page: int, x: float, y: float, *, path_map: PathMap | None = None,
                   synctex: str | None = None, timeout: float = 10) -> SourceLocation | None:
    """The source file and line behind point (`x`, `y`) on 1-based `page` of `pdf`.

    Coordinates are PDF points from the top-left corner, the same frame
    `forward_search` returns. None when synctex has no match."""
    argv = [_synctex_bin(synctex), "edit", "-o", f"{int(page)}:{x:.3f}:{y:.3f}:{pdf}"]
    for rec in parse_synctex_records(_run_synctex(argv, os.path.dirname(pdf) or ".", timeout)):
        try:
            file, line = rec["Input"], int(rec["Line"])
        except (KeyError, ValueError):
            continue
        if not file or line <= 0:
            continue
        mod = PathMap._mod(file)
        file = mod.normpath(file)
        local = path_map.to_local(file) if path_map else file
        try:
            col = int(rec.get("Column", "-1"))
        except ValueError:
            col = -1
        rel = path_map.relative(local) if path_map else None
        return SourceLocation(local, line, col if col >= 0 else None, rel)
    return None


# --------------------------------------------------------------------------- generations


_OUTPUT_WRITTEN = re.compile(r"Output written on (.+?) \((\d+) pages?, (\d+) bytes\)\.")


def log_output_summary(log_data: str | bytes) -> tuple[str, int, int] | None:
    """(output file, pages, bytes) from the log's last `Output written on` line."""
    found = None
    for line in unwrap_log(log_data):
        m = _OUTPUT_WRITTEN.search(line)
        if m:
            found = (m.group(1), int(m.group(2)), int(m.group(3)))
    return found


def synctex_pages(synctex_gz: bytes) -> int:
    """Number of sheets (pages) recorded in a `.synctex.gz`."""
    text = gzip.decompress(synctex_gz)
    return sum(1 for ln in text.split(b"\n") if ln.startswith(b"{"))


def check_generation(pdf: bytes | None, synctex_gz: bytes | None, log_data: bytes | None) -> list[str]:
    """Why these three files are not one build's output, or [] when they are.

    All three must be present; the PDF must be complete; the log's `Output written
    on ... (P pages, N bytes)` must name the PDF's exact size (when the engine wrote
    the PDF itself) and the SyncTeX map must hold P pages."""
    problems = []
    if not pdf:
        problems.append("the PDF is missing")
    elif not pdf.startswith(b"%PDF-") or b"%%EOF" not in pdf[-2048:]:
        problems.append("the PDF is incomplete")
    if not synctex_gz:
        problems.append("the SyncTeX map is missing")
    if not log_data:
        problems.append("the log is missing")
    if problems:
        return problems
    summary = log_output_summary(log_data)
    if summary is None:
        return ["the log does not record a written PDF"]
    out_name, pages, size = summary
    if out_name.lower().endswith(".pdf") and size != len(pdf):
        problems.append(f"the log records a {size}-byte PDF but the PDF has {len(pdf)} bytes")
    try:
        sheets = synctex_pages(synctex_gz)
    except (OSError, EOFError, ValueError) as exc:
        problems.append(f"the SyncTeX map is unreadable ({exc})")
    else:
        if sheets != pages:
            problems.append(f"the SyncTeX map has {sheets} pages but the log records {pages}")
    return problems


@dataclass
class Generation:
    id: int
    revision: str
    dir: str
    pdf: str
    synctex: str
    log: str
    pages: int | None = None
    published_at: float = 0.0
    consistent: bool = True     # sources unchanged while it built
    sha256: dict = field(default_factory=dict)
    inputs: list = field(default_factory=list)  # files `revision` hashes, relative to the root

    def to_dict(self):
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict, base: str) -> Generation:
        gdir = os.path.join(base, d["dir"]) if not os.path.isabs(d["dir"]) else d["dir"]
        return cls(int(d["id"]), d["revision"], gdir, os.path.join(gdir, d["pdf"]),
                   os.path.join(gdir, d["synctex"]), os.path.join(gdir, d["log"]),
                   d.get("pages"), d.get("published_at", 0.0), d.get("consistent", True),
                   d.get("sha256", {}), list(d.get("inputs", [])))


def _write_atomic(path: str, data: bytes) -> None:
    tmp = f"{path}.tmp-{os.getpid()}-{threading.get_ident()}"
    with open(tmp, "wb") as fh:
        fh.write(data)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, path)


class GenerationStore:
    """Published builds under `out_dir`, one directory per generation.

        out_dir/current.json            which generation is live (swapped atomically)
        out_dir/generations/gen-000007/ main.pdf, main.synctex.gz, main.log, generation.json
        out_dir/failed/                 the log of the last failed build
        out_dir/counter                 the last generation id handed out

    Ids come from `allocate()` and only ever grow, across restarts and processes.
    `publish` refuses a generation that is not strictly newer than the current one,
    or whose files do not belong together, and leaves the current one in place."""

    def __init__(self, out_dir: str, keep: int = 3):
        self.out_dir = os.path.abspath(out_dir)
        self.gen_root = os.path.join(self.out_dir, "generations")
        self.keep = max(1, keep)
        os.makedirs(self.gen_root, exist_ok=True)
        self._tlock = threading.RLock()

    @contextlib.contextmanager
    def _locked(self):
        """This process's threads and other processes publishing to the same out_dir."""
        with self._tlock:
            fd = os.open(os.path.join(self.out_dir, ".lock"), os.O_CREAT | os.O_RDWR, 0o600)
            try:
                flock(fd, LOCK_EX)
                try:
                    yield
                finally:
                    flock(fd, LOCK_UN)
            finally:
                os.close(fd)

    def _gen_ids(self) -> list[int]:
        ids = []
        for name in os.listdir(self.gen_root):
            m = re.fullmatch(r"gen-(\d+)", name)
            if m:
                ids.append(int(m.group(1)))
        return sorted(ids)

    def allocate(self) -> int:
        with self._locked():
            path = os.path.join(self.out_dir, "counter")
            try:
                last = int(Path(path).read_text().strip() or 0)
            except (OSError, ValueError):
                last = 0
            cur = self._read_current()
            last = max([last, cur["id"] if cur else 0] + self._gen_ids())
            _write_atomic(path, f"{last + 1}\n".encode())
            return last + 1

    def _read_current(self) -> dict | None:
        try:
            return json.loads(Path(self.out_dir, "current.json").read_text())
        except (OSError, ValueError):
            return None

    def current(self, *, verify: bool = False) -> Generation | None:
        """The published generation, or None. With `verify`, also check that its
        directory's `generation.json` has the same id and its files their recorded
        hashes; a generation that fails is not returned."""
        with self._locked():
            d = self._read_current()
            if not d:
                return None
            try:
                gen = Generation.from_dict(d, self.out_dir)
            except (KeyError, ValueError, TypeError):
                return None
            if not all(os.path.isfile(p) for p in (gen.pdf, gen.synctex, gen.log)):
                return None
            if verify:
                try:
                    own = json.loads(Path(gen.dir, "generation.json").read_text())
                except (OSError, ValueError):
                    return None
                if int(own.get("id", -1)) != gen.id:
                    return None
                for key, p in (("pdf", gen.pdf), ("synctex", gen.synctex), ("log", gen.log)):
                    want = gen.sha256.get(key)
                    if want and hashlib.sha256(Path(p).read_bytes()).hexdigest() != want:
                        return None
            return gen

    def publish(self, gen_id: int, revision: str, *, job: str, pdf: bytes | None,
                synctex_gz: bytes | None, log_data: bytes | None, consistent: bool = True,
                inputs: Sequence[str] = ()) -> Generation:
        problems = check_generation(pdf, synctex_gz, log_data)
        if problems:
            raise GenerationRejected(f"generation {gen_id} is incomplete: " + "; ".join(problems),
                                     problems)
        names = {"pdf": f"{job}.pdf", "synctex": f"{job}.synctex.gz", "log": f"{job}.log"}
        blobs = {"pdf": pdf, "synctex": synctex_gz, "log": log_data}
        summary = log_output_summary(log_data)
        with self._locked():
            cur = self._read_current()
            if cur and int(cur["id"]) >= gen_id:
                raise GenerationSuperseded(
                    f"generation {gen_id} is older than published generation {cur['id']}")
            dirname = f"gen-{gen_id:06d}"
            final = os.path.join(self.gen_root, dirname)
            if os.path.exists(final):
                raise GenerationSuperseded(f"generation {gen_id} was already published")
            staging = os.path.join(self.gen_root, f".staging-{dirname}-{os.getpid()}")
            shutil.rmtree(staging, ignore_errors=True)
            os.makedirs(staging)
            meta = {"id": gen_id, "revision": revision, "dir": os.path.join("generations", dirname),
                    "pdf": names["pdf"], "synctex": names["synctex"], "log": names["log"],
                    "pages": summary[1] if summary else None, "published_at": time.time(),
                    "consistent": consistent,
                    "sha256": {k: hashlib.sha256(v).hexdigest() for k, v in blobs.items()},
                    "inputs": list(inputs)}
            for key, name in names.items():
                with open(os.path.join(staging, name), "wb") as fh:
                    fh.write(blobs[key])
            Path(staging, "generation.json").write_text(json.dumps(meta, indent=1))
            os.rename(staging, final)
            _write_atomic(os.path.join(self.out_dir, "current.json"),
                          json.dumps(meta, indent=1).encode())
            self._prune(gen_id)
        return Generation.from_dict(meta, self.out_dir)

    def record_failure(self, gen_id: int, revision: str, *, job: str, log_data: bytes | None,
                       info: dict) -> str | None:
        """Keep the failed build's log (for 'open log') without touching the current one."""
        fdir = os.path.join(self.out_dir, "failed")
        os.makedirs(fdir, exist_ok=True)
        path = None
        if log_data:
            path = os.path.join(fdir, f"{job}.log")
            _write_atomic(path, log_data)
        _write_atomic(os.path.join(fdir, "attempt.json"),
                      json.dumps({"id": gen_id, "revision": revision, **info}, indent=1).encode())
        return path

    def _prune(self, current_id: int) -> None:
        ids = [i for i in self._gen_ids() if i <= current_id]
        for old in ids[:-self.keep]:
            shutil.rmtree(os.path.join(self.gen_root, f"gen-{old:06d}"), ignore_errors=True)
        for name in os.listdir(self.gen_root):
            if name.startswith(".staging-"):
                shutil.rmtree(os.path.join(self.gen_root, name), ignore_errors=True)


# --------------------------------------------------------------------------- builder


@dataclass
class FailedBuild:
    id: int
    revision: str
    returncode: int
    diagnostics: list[Diagnostic]
    log: str | None          # path of the kept log, if the build wrote one
    reason: str = ""

    def to_dict(self):
        d = asdict(self)
        d["diagnostics"] = [x.to_dict() for x in self.diagnostics]
        return d


@dataclass
class BuildStatus:
    state: str
    seq: int                                  # grows with every status; drop older ones
    generation: Generation | None = None      # the published (last good) build
    building: int | None = None               # generation id of the running build
    failed: FailedBuild | None = None
    diagnostics: list[Diagnostic] = field(default_factory=list)  # of the last finished build

    def to_dict(self):
        return {"state": self.state, "seq": self.seq,
                "generation": self.generation.to_dict() if self.generation else None,
                "building": self.building,
                "failed": self.failed.to_dict() if self.failed else None,
                "diagnostics": [d.to_dict() for d in self.diagnostics]}


def default_out_dir(key: str) -> str:
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~\\AppData\\Local")
    else:
        base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    digest = hashlib.sha256(key.encode("utf-8", "surrogateescape")).hexdigest()[:16]
    return os.path.join(base, "relay", "tex", digest)


def _user_rc_files() -> list[str]:
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    found = []
    for p in (os.path.join(xdg, "latexmk", "latexmkrc"), os.path.expanduser("~/.latexmkrc")):
        if os.path.isfile(p):
            found.append(p)
    return found


class TexBuilder:
    """Debounced, cancellable latexmk builds of one root, published as generations.

    `root_dir` and `main_file` are paths on the build host (`transport`); `out_dir` is
    local and holds the published generations; `work_dir` is latexmk's `-outdir` on
    the build host (default `out_dir/work` for a local build; required for a remote
    one). It is kept between builds so latexmk can rerun incrementally.

    Callbacks run on the worker thread (or the caller's for `request_build` /
    `mark_stale`): `on_state(BuildStatus)` on every change, `on_published(Generation)`
    when a new generation goes live. Use `BuildStatus.seq` to order them.

    A project's own latexmkrc is Perl that latexmk runs, so unless
    `trust_project_rc` it is skipped (`-norc`); the user's own rc files still apply."""

    def __init__(self, root_dir: str, main_file: str, engine: str = "pdf",
                 out_dir: str | None = None, *, work_dir: str | None = None,
                 transport: Transport | None = None, path_map: PathMap | None = None,
                 debounce: float = 0.4, trust_project_rc: bool = False,
                 extra_args: Sequence[str] = (), timeout: float = 600,
                 on_state: Callable[[BuildStatus], None] | None = None,
                 on_published: Callable[[Generation], None] | None = None,
                 keep_generations: int = 3, verify_restore: bool = True):
        self.transport = transport or LocalTransport()
        remote = self.transport.remote
        self.root_dir = root_dir if remote else os.path.abspath(root_dir)
        mod = posixpath if remote else os.path
        self._mod = mod
        self.main_file = main_file if mod.isabs(main_file) else mod.join(self.root_dir, main_file)
        self.main_file = mod.normpath(self.main_file)
        self.main_dir = mod.dirname(self.main_file)
        self.job = mod.splitext(mod.basename(self.main_file))[0]
        norm = normalize_engine(engine)
        if norm is None:
            raise ValueError(f"unknown TeX engine {engine!r}; use one of {sorted(ENGINES)}")
        self.engine = norm
        self.out_dir = os.path.abspath(out_dir or default_out_dir(
            ("remote:" if remote else "") + self.main_file))
        if work_dir is None:
            if remote:
                raise ValueError("a remote build needs an explicit work_dir on the build host")
            work_dir = os.path.join(self.out_dir, "work")
        self.work_dir = work_dir if remote else os.path.abspath(work_dir)
        self.path_map = path_map or PathMap(self.root_dir, self.root_dir)
        self.debounce = debounce
        self.trust_project_rc = trust_project_rc
        self.extra_args = list(extra_args)
        self.timeout = timeout
        self.on_state = on_state
        self.on_published = on_published
        self.store = GenerationStore(self.out_dir, keep_generations)

        self._cond = threading.Condition(threading.RLock())
        self._due: float | None = None
        self._cancel: threading.Event | None = None
        self._building: int | None = None
        self._closed = False
        self._seq = 0
        self._stale_hint = False
        self._fls_extra: list[str] = []
        self._failed: FailedBuild | None = None
        self._diagnostics: list[Diagnostic] = []
        self._generation = self.store.current(verify=verify_restore)
        self._state = STALE if self._generation else IDLE
        if self._generation:
            self._fls_extra = [self._abs(p) for p in self._generation.inputs]
            if verify_restore:
                try:
                    if self._generation_revision_now() == self._generation.revision \
                            and self._generation.consistent:
                        self._state = LIVE
                except OSError:
                    pass
        self._thread = threading.Thread(target=self._loop, name=f"tex-build-{self.job}", daemon=True)
        self._thread.start()

    # -- public API ---------------------------------------------------------

    @property
    def status(self) -> BuildStatus:
        with self._cond:
            return self._snapshot()

    def build_argv(self) -> list[str]:
        """The latexmk command line, as an argv list."""
        argv = ["latexmk"]
        if not self.trust_project_rc:
            argv.append("-norc")
            if not self.transport.remote:
                for rc in _user_rc_files():
                    argv += ["-r", rc]
        argv += [ENGINES[self.engine][1], "-interaction=nonstopmode", "-file-line-error",
                 "-synctex=1", f"-outdir={self.work_dir}"]
        argv += self.extra_args
        argv.append(self._mod.basename(self.main_file))
        return argv

    def request_build(self, debounce: float | None = None) -> None:
        """Build soon. Calls within the debounce window collapse into one build, and a
        build that is already running is cancelled: its sources are out of date."""
        with self._cond:
            if self._closed:
                return
            delay = self.debounce if debounce is None else debounce
            self._due = time.monotonic() + max(0.0, delay)
            self._stale_hint = False  # a save: the build will read what the editor holds
            if self._cancel is not None:
                self._cancel.set()
            snap = self._set_state(BUILDING)
            self._cond.notify_all()
        self._emit(snap)

    def cancel(self) -> None:
        """Drop a pending request and stop the running build; the current PDF stays."""
        snap = None
        with self._cond:
            self._due = None
            if self._cancel is not None:
                self._cancel.set()  # the worker settles the state when it stops
            elif self._state == BUILDING:
                snap = self._set_state(self._settled_state())
            self._cond.notify_all()
        self._emit(snap)

    def mark_stale(self, revision: str | None = None) -> None:
        """Newer sources exist (saved as `revision`, or unsaved when None).

        A live PDF becomes stale. A running build finishes as stale. A failed state
        stays failed until the next build: its diagnostics still describe the last
        attempt. Passing the published revision (an undo back to it) is a no-op."""
        with self._cond:
            gen = self._generation
            if revision is not None and gen is not None and revision == gen.revision:
                return
            if self._building is not None or self._due is not None:
                self._stale_hint = True
                return
            if self._state != LIVE:
                return
            snap = self._set_state(STALE)
        self._emit(snap)

    def check_sources(self) -> str:
        """Hash the saved sources the published PDF was built from; mark it stale (or
        live again) to match. Returns that hash (or the full source revision before
        anything was built)."""
        rev = self._generation_revision_now()
        with self._cond:
            gen = self._generation
            if self._building is not None or self._due is not None or self._state == FAILED \
                    or gen is None:
                return rev
            want = LIVE if (rev == gen.revision and gen.consistent) else STALE
            if want == self._state:
                return rev
            snap = self._set_state(want)
        self._emit(snap)
        return rev

    def dependencies(self) -> list[Dependency]:
        """The root's files in document order, in the editor's terms, for the editor group."""
        deps = find_dependencies(self.main_file, fs=TransportFS(self.transport))
        return [Dependency(d.kind, self.path_map.to_local(d.path), d.exists,
                           self.path_map.to_local(d.source) if d.source else None, d.line)
                for d in deps]

    def source_revision(self) -> str:
        """The hash a build started now would record."""
        return self._scan()[1]

    def wait_idle(self, timeout: float = 120) -> BuildStatus:
        """Block until nothing is pending or running (tests, scripts)."""
        deadline = time.monotonic() + timeout
        with self._cond:
            while self._due is not None or self._building is not None:
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError("TeX build did not finish in time")
                self._cond.wait(min(left, 0.1))
            return self._snapshot()

    def forward_search(self, source_file: str, line: int, column: int = 0) -> list[PdfLocation]:
        gen = self._require_generation()
        return forward_search(gen.pdf, source_file, line, column, path_map=self.path_map)

    def inverse_search(self, page: int, x: float, y: float) -> SourceLocation | None:
        gen = self._require_generation()
        return inverse_search(gen.pdf, page, x, y, path_map=self.path_map)

    def close(self) -> None:
        with self._cond:
            self._closed = True
            self._due = None
            if self._cancel is not None:
                self._cancel.set()
            self._cond.notify_all()
        self._thread.join(timeout=10)

    # -- internals ----------------------------------------------------------

    def _require_generation(self) -> Generation:
        with self._cond:
            gen = self._generation
        if gen is None:
            raise TexError("nothing has been built yet")
        return gen

    def _snapshot(self) -> BuildStatus:
        return BuildStatus(self._state, self._seq, self._generation, self._building,
                           self._failed, list(self._diagnostics))

    def _set_state(self, state: str) -> BuildStatus:
        self._state = state
        self._seq += 1
        return self._snapshot()

    def _emit(self, snap: BuildStatus | None, published: Generation | None = None) -> None:
        if published is not None and self.on_published:
            try:
                self.on_published(published)
            except Exception:  # a UI callback must not kill the build thread
                log.exception("on_published callback failed")
        if snap is not None and self.on_state:
            try:
                self.on_state(snap)
            except Exception:
                log.exception("on_state callback failed")

    def _abs(self, rel: str) -> str:
        return self._mod.normpath(self._mod.join(self.root_dir, rel))

    def _rel(self, path: str) -> str:
        return self._mod.relpath(path, self.root_dir).replace(os.sep, "/")

    def _scan(self, paths: Sequence[str] | None = None) -> tuple[list[Dependency], str, list[str]]:
        """(dependencies, revision, hashed paths). The revision hashes the root's own
        dependencies plus the project files the last build's `.fls` listed, or
        exactly `paths` when given, so a before/after comparison uses one set."""
        fs = TransportFS(self.transport)
        deps: list[Dependency] = []
        if paths is None:
            deps = find_dependencies(self.main_file, fs=fs)
            paths = [d.path for d in deps if d.exists]
            paths += [p for p in self._fls_extra if p not in paths]
        contents = fs.read_many(list(paths))
        return deps, source_revision(contents, self.root_dir), list(paths)

    def _generation_revision_now(self) -> str:
        gen = self._generation
        if gen is None or not gen.inputs:
            return self._scan()[1]
        return self._scan([self._abs(p) for p in gen.inputs])[1]

    def _settled_state(self) -> str:
        if self._due is not None:
            return BUILDING
        if self._failed is not None:
            return FAILED
        return STALE if self._generation else IDLE

    def _loop(self) -> None:
        while True:
            with self._cond:
                while not self._closed and (self._due is None or time.monotonic() < self._due):
                    wait = None if self._due is None else max(0.0, self._due - time.monotonic())
                    self._cond.wait(wait if wait is not None else 1.0)
                if self._closed:
                    return
                self._due = None
                cancel = threading.Event()
                self._cancel = cancel
                try:
                    gen_id = self.store.allocate()
                except OSError as exc:
                    log.error("cannot allocate a TeX generation in %s: %s", self.out_dir, exc)
                    gen_id = -1
                self._building = gen_id
                snap = self._set_state(BUILDING)
            self._emit(snap)
            finished = False
            try:
                finished = self._build(gen_id, cancel)
            except Exception as exc:  # keep the thread alive and say what happened
                log.exception("TeX build crashed")
                self._finish_failed(gen_id, "", -1, [Diagnostic(
                    "error", self.path_map.to_local(self.main_file), None,
                    f"build crashed: {exc}", "build")], None, str(exc))
                finished = True
            with self._cond:
                self._building = None
                self._cancel = None
                snap = None if finished else self._set_state(self._settled_state())
                self._cond.notify_all()
            self._emit(snap)

    def _finish_failed(self, gen_id, revision, rc, diags, log_path, reason) -> None:
        with self._cond:
            self._failed = FailedBuild(gen_id, revision, rc, diags, log_path, reason)
            self._diagnostics = diags
            snap = self._set_state(self._settled_state())
        self._emit(snap)

    def _build(self, gen_id: int, cancel: threading.Event) -> bool:
        """Run one build. False when it was superseded or cancelled (nothing recorded)."""
        main_local = self.path_map.to_local(self.main_file)
        if gen_id < 0:
            self._finish_failed(gen_id, "", -1, [Diagnostic(
                "error", main_local, None, f"cannot write to {self.out_dir}", "build")], None,
                "out_dir not writable")
            return True
        deps, revision, inputs = self._scan()
        dirs = {self.work_dir}
        for d in deps:
            if d.kind == "include":  # \include writes sub/dir/name.aux under -outdir
                rel = self._mod.relpath(self._mod.dirname(d.path), self.main_dir)
                if rel not in (".", "") and not rel.startswith(".."):
                    dirs.add(self._mod.join(self.work_dir, rel))
        self.transport.makedirs(sorted(dirs))
        result = self.transport.run(self.build_argv(), self.main_dir, env=dict(BUILD_ENV),
                                    cancel=cancel, timeout=self.timeout)
        if result.cancelled or cancel.is_set():
            return False  # superseded: discarded, never published
        j = self._mod.join
        names = {k: j(self.work_dir, self.job + ext) for k, ext in
                 (("pdf", ".pdf"), ("synctex", ".synctex.gz"), ("log", ".log"),
                  ("fls", ".fls"), ("blg", ".blg"))}
        files = self.transport.fetch(list(names.values()))
        got = {k: files.get(p) for k, p in names.items()}
        diags: list[Diagnostic] = []
        if got["log"]:
            diags += parse_log(got["log"], build_cwd=self.main_dir, path_map=self.path_map)
        if got["blg"]:
            diags += parse_blg(got["blg"], build_cwd=self.main_dir, path_map=self.path_map)
        if got["fls"]:
            self._fls_extra = fls_inputs(got["fls"].decode("utf-8", "replace"),
                                         root=self.root_dir, exclude=[self.work_dir],
                                         pathmod=self._mod)
        consistent = self._scan(inputs)[1] == revision
        rel_inputs = [self._rel(p) for p in inputs]

        reason = None
        if result.missing:
            reason = "latexmk is not installed on the build host"
        elif result.timed_out:
            reason = f"the build took longer than {self.timeout:g}s and was stopped"
        elif result.returncode != 0:
            reason = f"latexmk exited with code {result.returncode}"
        if reason is None:
            with self._cond:
                if cancel.is_set():
                    return False
                try:
                    gen = self.store.publish(gen_id, revision, job=self.job, pdf=got["pdf"],
                                             synctex_gz=got["synctex"], log_data=got["log"],
                                             consistent=consistent, inputs=rel_inputs)
                except GenerationSuperseded:
                    return False
                except GenerationRejected as exc:
                    reason = str(exc)
                else:
                    self._generation = gen
                    self._failed = None
                    self._diagnostics = diags
                    stale = not consistent or self._stale_hint
                    snap = self._set_state(BUILDING if self._due is not None
                                           else STALE if stale else LIVE)
            if reason is None:
                self._emit(snap, gen)
                return True

        if not any(d.severity == "error" for d in diags) or result.returncode == 0:
            tail = "\n".join(result.output.strip().splitlines()[-6:]) \
                if result.returncode != 0 else ""
            diags.insert(0, Diagnostic("error", main_local, None,
                                       reason + (f": {tail}" if tail else ""), "build"))
        log_path = self.store.record_failure(
            gen_id, revision, job=self.job, log_data=got["log"],
            info={"returncode": result.returncode, "reason": reason})
        with self._cond:
            if cancel.is_set():
                return False
        self._finish_failed(gen_id, revision, result.returncode, diags, log_path, reason)
        return True
