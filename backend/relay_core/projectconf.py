# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reusable project configuration: `.relay/project.toml` v1 (docs/TREES-AND-LANDING.md §A3).

One config file tells the workspace/queue service what a project's target branch is, how to
initialise a disposable workspace, how to verify a candidate, what resources a verification
needs, how to build a runnable main release, and what the reconciler's budget is.  Nothing here
is specific to this repository.

Schema (exactly the contract's sections; unknown keys are rejected, never ignored):

    version = 1
    [project]      target = "main"
    [workspace]    exclude = [".board", "docs/qa_evidence"]; max_workspaces = 50; init = []
    [verification] commands = [["python3","-m","pytest","-q"]]; timeout_seconds = 900;
                   ungated = false
    [resources]    memory_bytes / disk_bytes / cpus   (what one verification slot needs)
    [main]         build / install = arrays of argv arrays with {source}/{build}/{dest};
                   executable = "bin/relay"; keep = 2
    [reconcile]    enabled; max_attempts; tokens_per_case; tokens_per_day

Additive extensions beyond the contract (approved on #ASQ4): optional `main.smoke` (argv arrays
run after install with `{dest}`/`{executable}` placeholders), optional `main.timeout_seconds`,
and optional `verification.environment` (a small env overlay; `GIT_*` keys are rejected because
the gate scrubs them).  Omitting them yields exactly the contract schema.

The module has four jobs:

* **load / normalize** — parse the TOML, fill every default, and *reject* bad types and dangerous
  values loudly (unknown keys, relative-path escapes, shell strings that do not explicitly name
  ``sh -c``, ``keep < 2``, unknown ``{placeholders}``).  A rejected config raises
  :class:`ProjectConfigError`; callers (activation, queue submission) treat that as a refusal,
  never as "use defaults".
* **policy_hash** — a stable hash of the *normalized* config.  Every gate verdict carries it so a
  landing is attributable to the exact verification policy that approved it.
* **detect** — offline suggestions for a project that has no config yet.  Suggestions are input
  to a human review, never trust: they are not written anywhere and carry no authority.
* **run_gate** — actually run the project's verification commands in a directory (usually a
  disposable candidate tree) and return the queue's verifier shape ``{ok, policy_hash, log,
  verified, reason?}``.  Fails closed: no commands with ``ungated = false`` is a refusal, and a
  pytest/ctest run that reports *zero tests* fails the gate even when the runner exits 0 (ctest
  famously exits 0 with "No tests were found!!!").  Ambient ``GIT_*`` environment is scrubbed so
  a submitter's shell cannot redirect the queue's git operations.

Resource tradeoffs, stated once: ``timeout_seconds`` is per command (a 10-command suite gets 10x
the budget — simple to reason about, bounded by the command-count cap); ``selected_tests`` from
``Queue.submit`` are run as an *extra* focused pass and never narrow the configured commands, so
a submitter cannot make a landing cheaper by selecting one test — the cost is that selections add
time rather than save it.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import signal
import subprocess
import time
from pathlib import Path, PurePosixPath
from typing import Mapping, Sequence

SCHEMA_VERSION = 1

# --------------------------------------------------------------------------- limits
MAX_COMMANDS = 32                 # per command list; bounds total gate time
MAX_ARGV = 64                     # entries in one argv
MAX_EXCLUDES = 128
MAX_ENV = 64                      # verification.environment entries
MAX_TIMEOUT_SECONDS = 24 * 3600   # one command may never run longer than a day
MAX_LOG_BYTES = 1024 * 1024       # gate log kept in memory / handed to the queue
CMD_LOG_BYTES = 256 * 1024        # per-command captured output (head+tail when over)
MAX_ATTEMPTS = 10                 # reconcile tries; the reconciler itself caps at 2
MAX_TOKENS = 1 << 40              # sane ceiling for token budgets
GATE_ZERO_REASON = "verification ran zero tests"

DEFAULT_CONFIG: dict = {
    "version": SCHEMA_VERSION,
    "project": {"target": "main"},
    "workspace": {"exclude": [], "max_workspaces": 50, "init": []},
    "verification": {"commands": [], "timeout_seconds": 900, "ungated": False, "environment": {}},
    "resources": {"memory_bytes": 8 << 30, "disk_bytes": 4 << 30, "cpus": 2},
    "main": {"build": [], "install": [], "executable": None,
             "smoke": [], "keep": 2, "timeout_seconds": 3600},
    "reconcile": {"enabled": False, "max_attempts": 2,
                  "tokens_per_case": 200000, "tokens_per_day": 10000000},
}

_PLACEHOLDER_RE = re.compile(r"\{([a-z_]+)\}")
#: Which {placeholder} names each command list may use (substituted by the caller, e.g.
#: main_release for ``main.*``).  Verification/workspace-init commands run in a concrete cwd
#: and get none.
_PLACEHOLDERS = {
    ("main", "build"): {"source", "build", "dest"},
    ("main", "install"): {"source", "build", "dest"},
    ("main", "smoke"): {"dest", "executable"},
    ("workspace", "init"): set(),
    ("verification", "commands"): set(),
}
_ENV_KEY_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
_TARGET_BAD = re.compile(r"[\s~^:?*\[\\]|\x00")
_CONFIG_PATH = ".relay/project.toml"


class ProjectConfigError(ValueError):
    """A `.relay/project.toml` that is missing (when required), unparseable, or dangerous."""


# --------------------------------------------------------------------------- normalize
def _err(origin: str, where: str, msg: str) -> ProjectConfigError:
    return ProjectConfigError(f"{origin}: {where}: {msg}")


def _bool(origin, where, value, default):
    if value is None:
        return default
    if not isinstance(value, bool):
        raise _err(origin, where, f"must be a boolean, got {type(value).__name__}")
    return value


def _int(origin, where, value, default, *, lo, hi=None):
    if value is None:
        return default
    if isinstance(value, bool) or not isinstance(value, int):
        raise _err(origin, where, f"must be an integer, got {type(value).__name__}")
    if value < lo or (hi is not None and value > hi):
        raise _err(origin, where, f"must be >= {lo}" + (f" and <= {hi}" if hi else ""))
    return value


def _argv_list(origin, where, value, *, placeholders: set[str]) -> list[list[str]]:
    """One command is an argv array of non-empty strings.  A bare string is rejected: a shell
    command must explicitly name `sh -c` so nobody mistakes it for an argv."""
    if value is None:
        return []
    if not isinstance(value, list) or len(value) > MAX_COMMANDS:
        raise _err(origin, where, f"must be a list of at most {MAX_COMMANDS} argv arrays")
    out = []
    for i, cmd in enumerate(value):
        at = f"{where}[{i}]"
        if isinstance(cmd, str):
            raise _err(origin, at, "a shell command must explicitly name sh -c, "
                                   'e.g. ["sh", "-c", "' + cmd + '"]')
        if not isinstance(cmd, list) or not 1 <= len(cmd) <= MAX_ARGV:
            raise _err(origin, at, f"must be an argv array of 1..{MAX_ARGV} strings")
        argv = []
        for arg in cmd:
            if not isinstance(arg, str) or not arg or "\x00" in arg:
                raise _err(origin, at, "argv entries must be non-empty strings")
            for name in _PLACEHOLDER_RE.findall(arg):
                if name not in placeholders:
                    raise _err(origin, at, f"unknown or disallowed placeholder {{{name}}}"
                                           + (f" (allowed: {sorted(placeholders)})" if placeholders
                                              else " (no placeholders allowed here)"))
            argv.append(arg)
        out.append(argv)
    return out


def _relative_path(origin, where, value, *, none_ok=False):
    if value is None and none_ok:
        return None
    if not isinstance(value, str) or not value:
        raise _err(origin, where, "must be a relative path string")
    p = PurePosixPath(value)
    if p.is_absolute() or ".." in p.parts:
        raise _err(origin, where, "must be a relative path that stays inside the tree")
    return str(p)


def _section(origin, raw: Mapping, name: str, keys: set[str]) -> dict:
    sec = raw.get(name) or {}
    if not isinstance(sec, dict):
        raise _err(origin, name, "must be a table")
    unknown = sorted(set(sec) - keys)
    if unknown:
        raise _err(origin, name, f"unknown key(s) {unknown} (known: {sorted(keys)}); "
                                 "config v1 fails loud instead of ignoring a typo")
    return sec


def normalize_config(raw: Mapping, *, origin: str = ".relay/project.toml") -> dict:
    """Validate `raw` (parsed TOML) against config v1 and return the full normalized config —
    every default filled.  Raises ProjectConfigError on anything unknown, mistyped or dangerous."""
    if not isinstance(raw, Mapping):
        raise _err(origin, "<root>", "must be a TOML table")
    known = {"version", "project", "workspace", "verification", "resources", "main", "reconcile"}
    unknown = sorted(set(raw) - known)
    if unknown:
        raise _err(origin, "<root>", f"unknown section(s) {unknown} (known: {sorted(known)})")
    version = raw.get("version", SCHEMA_VERSION)
    if version != SCHEMA_VERSION:
        raise _err(origin, "version", f"only schema version {SCHEMA_VERSION} is supported, "
                                      f"got {version!r}")
    cfg = json.loads(json.dumps(DEFAULT_CONFIG))  # deep copy of defaults

    proj = _section(origin, raw, "project", {"target"})
    target = proj.get("target", "main")
    if (not isinstance(target, str) or not target or _TARGET_BAD.search(target)
            or ".." in target or target.startswith("-") or target.endswith("/")
            or "//" in target or target.endswith(".lock")):
        raise _err(origin, "project.target", f"not a safe branch name: {target!r}")
    cfg["project"]["target"] = target

    ws = _section(origin, raw, "workspace", {"exclude", "max_workspaces", "init"})
    exclude = ws.get("exclude") or []
    if not isinstance(exclude, list) or len(exclude) > MAX_EXCLUDES:
        raise _err(origin, "workspace.exclude", f"must be a list of at most {MAX_EXCLUDES} patterns")
    cfg["workspace"]["exclude"] = [_relative_path(origin, f"workspace.exclude[{i}]", p)
                                   for i, p in enumerate(exclude)]
    cfg["workspace"]["max_workspaces"] = _int(origin, "workspace.max_workspaces",
                                              ws.get("max_workspaces"), 50, lo=1, hi=10000)
    cfg["workspace"]["init"] = _argv_list(origin, "workspace.init", ws.get("init"),
                                          placeholders=_PLACEHOLDERS[("workspace", "init")])

    ver = _section(origin, raw, "verification",
                   {"commands", "timeout_seconds", "ungated", "environment"})
    cfg["verification"]["commands"] = _argv_list(
        origin, "verification.commands", ver.get("commands"),
        placeholders=_PLACEHOLDERS[("verification", "commands")])
    cfg["verification"]["timeout_seconds"] = _int(
        origin, "verification.timeout_seconds", ver.get("timeout_seconds"), 900,
        lo=1, hi=MAX_TIMEOUT_SECONDS)
    cfg["verification"]["ungated"] = _bool(origin, "verification.ungated",
                                           ver.get("ungated"), False)
    env = ver.get("environment") or {}
    if not isinstance(env, dict) or len(env) > MAX_ENV:
        raise _err(origin, "verification.environment",
                   f"must be a table of at most {MAX_ENV} entries")
    for k, v in env.items():
        if not _ENV_KEY_RE.match(k):
            raise _err(origin, "verification.environment", f"bad variable name {k!r}")
        if k.startswith("GIT_"):
            raise _err(origin, "verification.environment",
                       f"{k} would fight the gate's GIT_* scrub; queue git ops must be ambient-free")
        if not isinstance(v, str):
            raise _err(origin, "verification.environment", f"{k} must be a string")
    cfg["verification"]["environment"] = dict(env)

    res = _section(origin, raw, "resources", {"memory_bytes", "disk_bytes", "cpus"})
    cfg["resources"]["memory_bytes"] = _int(origin, "resources.memory_bytes",
                                            res.get("memory_bytes"), 8 << 30, lo=1 << 20)
    cfg["resources"]["disk_bytes"] = _int(origin, "resources.disk_bytes",
                                          res.get("disk_bytes"), 4 << 30, lo=1 << 20)
    cfg["resources"]["cpus"] = _int(origin, "resources.cpus", res.get("cpus"), 2,
                                    lo=0, hi=1024)

    main = _section(origin, raw, "main",
                    {"build", "install", "executable", "keep", "smoke", "timeout_seconds"})
    cfg["main"]["build"] = _argv_list(origin, "main.build", main.get("build"),
                                      placeholders=_PLACEHOLDERS[("main", "build")])
    cfg["main"]["install"] = _argv_list(origin, "main.install", main.get("install"),
                                        placeholders=_PLACEHOLDERS[("main", "install")])
    cfg["main"]["smoke"] = _argv_list(origin, "main.smoke", main.get("smoke"),
                                      placeholders=_PLACEHOLDERS[("main", "smoke")])
    cfg["main"]["executable"] = _relative_path(origin, "main.executable",
                                               main.get("executable"), none_ok=True)
    cfg["main"]["keep"] = _int(origin, "main.keep", main.get("keep"), 2,
                               lo=2)  # invariant 7: never fewer than two completed releases
    cfg["main"]["timeout_seconds"] = _int(origin, "main.timeout_seconds",
                                          main.get("timeout_seconds"), 3600,
                                          lo=1, hi=MAX_TIMEOUT_SECONDS)

    rec = _section(origin, raw, "reconcile",
                   {"enabled", "max_attempts", "tokens_per_case", "tokens_per_day"})
    cfg["reconcile"]["enabled"] = _bool(origin, "reconcile.enabled", rec.get("enabled"), False)
    cfg["reconcile"]["max_attempts"] = _int(origin, "reconcile.max_attempts",
                                            rec.get("max_attempts"), 2, lo=1, hi=MAX_ATTEMPTS)
    cfg["reconcile"]["tokens_per_case"] = _int(origin, "reconcile.tokens_per_case",
                                               rec.get("tokens_per_case"), 200000,
                                               lo=1, hi=MAX_TOKENS)
    cfg["reconcile"]["tokens_per_day"] = _int(origin, "reconcile.tokens_per_day",
                                              rec.get("tokens_per_day"), 10000000,
                                              lo=1, hi=MAX_TOKENS)
    return cfg


def policy_hash(config: Mapping) -> str:
    """Stable hash of a normalized config: what a gate verdict attests to.  Re-normalizing an
    already-normalized config is idempotent, so hashing twice is safe."""
    canonical = json.dumps(config, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


# --------------------------------------------------------------------------- load
def _read_at_revision(repo: Path, revision: str) -> bytes | None:
    proc = subprocess.run(
        ["git", "-C", str(repo), "show", f"{revision}:{_CONFIG_PATH}"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if proc.returncode != 0:
        # A missing path in an existing commit and a bad revision both land here; distinguish.
        check = subprocess.run(["git", "-C", str(repo), "rev-parse", "--verify",
                                f"{revision}^{{commit}}"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        if check.returncode != 0:
            raise ProjectConfigError(f"{repo}: cannot resolve revision {revision!r}")
        return None
    return proc.stdout


def load(repo, *, revision: str | None = None, require_file: bool = False) -> dict:
    """Load and normalize the config for `repo` (a path), optionally at a git `revision`.

    A missing file yields the normalized defaults (which are gated with zero commands, so
    `run_gate` fails closed on them) unless `require_file` is set — activation uses that to
    refuse projects that never opted in.  Bad TOML or a rejected value raises
    ProjectConfigError either way.
    """
    repo = Path(repo)
    if revision is not None:
        blob = _read_at_revision(repo, revision)
        origin = f"{repo}/.relay/project.toml@{revision}"
        if blob is None:
            if require_file:
                raise ProjectConfigError(f"{origin}: not present at that revision")
            return normalize_config({}, origin=origin + " (absent, defaults)")
        text = blob.decode("utf-8", errors="replace")
    else:
        path = repo / _CONFIG_PATH
        origin = str(path)
        if not path.is_file():
            if require_file:
                raise ProjectConfigError(f"{path}: no project config; activation refused")
            return normalize_config({}, origin=origin + " (absent, defaults)")
        text = path.read_text(encoding="utf-8", errors="replace")
    import tomllib
    try:
        raw = tomllib.loads(text)
    except tomllib.TOMLDecodeError as exc:
        raise ProjectConfigError(f"{origin}: invalid TOML: {exc}") from exc
    return normalize_config(raw, origin=origin)


# --------------------------------------------------------------------------- detect
def detect(repo) -> dict:
    """Offline suggestions for a project with no config.  This is advice for a human to review
    into `.relay/project.toml`; nothing here is trusted, written, or activated automatically."""
    repo = Path(repo)
    suggestion: dict = {}
    notes: list[str] = []

    def _has(*names) -> bool:
        return any((repo / n).exists() for n in names)

    commands: list[list[str]] = []
    init: list[list[str]] = []
    if _has("tests", "test") or _has("pytest.ini", "tox.ini"):
        commands.append(["python3", "-m", "pytest", "-q"])
        notes.append("found a Python test tree: suggest pytest as a verification command")
    if _has("CMakeLists.txt"):
        init.append(["cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"])
        commands.append(["ctest", "--test-dir", "build", "--output-on-failure"])
        suggestion["main"] = {
            "build": [["cmake", "--build", "{build}", "--parallel"]],
            "install": [["cmake", "--install", "{build}", "--prefix", "{dest}"]],
            "executable": None,
        }
        notes.append("found CMake: suggest ctest as a verification command and "
                     "cmake --install --prefix {dest} as the main-release install; "
                     "main.executable must be filled in by a human")
    if _has("package.json"):
        commands.append(["npm", "test"])
        notes.append("found package.json: suggest npm test")
    if commands:
        suggestion["verification"] = {"commands": commands}
    if init:
        suggestion["workspace"] = {"init": init}
    if not suggestion:
        notes.append("no recognized build or test layout; write verification.commands by hand")
    notes.append("suggestions are not trust: review them into .relay/project.toml; "
                 "activation captures the accepted config and the queue re-reads it per landing")
    return {"suggestion": suggestion, "notes": notes}


# --------------------------------------------------------------------------- run_gate
_PYTEST_ZERO = (re.compile(r"no tests ran"), re.compile(r"collected 0 items"))
_CTEST_ZERO = (re.compile(r"no tests were found", re.IGNORECASE),
               re.compile(r"\btests failed out of 0\b"))


def _runner_kind(argv: Sequence[str]) -> str | None:
    base = os.path.basename(argv[0]) if argv else ""
    if base in ("ctest",):
        return "ctest"
    if base in ("pytest", "py.test") or base.endswith("pytest"):
        return "pytest"
    if re.match(r"python[\d.]*\Z", base) and list(argv[1:3]) == ["-m", "pytest"]:
        return "pytest"
    return None


def _scrubbed_env(extra: Mapping[str, str] | None) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    if extra:
        env.update(extra)
    return env


def _run_command(argv: Sequence[str], cwd: Path, env: Mapping[str, str],
                 timeout: float) -> dict:
    """Run one gate command; never raises.  Timeout kills the whole process group.

    Output goes to a spill file, not a memory pipe: a noisy build may print gigabytes and the
    service must not OOM capturing it.  Only a bounded head+tail is read back for the log."""
    import tempfile
    started = time.monotonic()
    with tempfile.TemporaryFile(prefix="relay-gate-") as sink:
        proc = subprocess.Popen(
            list(argv), cwd=str(cwd), env=dict(env),
            stdout=sink, stderr=subprocess.STDOUT, start_new_session=True)
        timed_out = False
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
            proc.wait()
        duration = time.monotonic() - started
        size = sink.seek(0, os.SEEK_END)
        head_n = 64 * 1024
        sink.seek(0)
        head = sink.read(min(head_n, size))
        if size > CMD_LOG_BYTES:
            sink.seek(size - (CMD_LOG_BYTES - head_n))
            tail = sink.read()
            raw = head + (f"\n... [{size - len(head) - len(tail)} bytes elided] ...\n"
                          .encode()) + tail
        else:
            sink.seek(len(head))
            raw = head + sink.read()
    text = raw.decode("utf-8", errors="replace")
    kind = _runner_kind(argv)
    zero = False
    if proc.returncode == 0 and kind == "pytest":
        zero = any(p.search(text) for p in _PYTEST_ZERO)
    elif proc.returncode == 0 and kind == "ctest":
        zero = any(p.search(text) for p in _CTEST_ZERO)
    return {"argv": list(argv), "exit": proc.returncode, "duration_seconds": round(duration, 3),
            "timed_out": timed_out, "runner": kind, "zero_tests": zero, "output": text}


def _focused_pass(kind: str, base_argv: Sequence[str], selected_tests: Sequence[str]) -> list[str]:
    """The extra pass that runs the caller's selected tests: appended node ids for pytest,
    a -R union for ctest.  Only ever an addition; configured commands are also run verbatim."""
    argv = list(base_argv)
    if kind == "pytest":
        argv.extend(selected_tests)
    elif kind == "ctest":
        argv.extend(["-R", "^(?:" + "|".join(re.escape(t) for t in selected_tests) + ")$"])
    return argv


def run_gate(config: Mapping, cwd, *, selected_tests: Sequence[str] = (),
             env: Mapping[str, str] | None = None) -> dict:
    """Run the project's verification commands in `cwd` and return the queue verifier result
    ``{ok, policy_hash, log, verified, reason?}``.

    `config` is a normalized config from `load()` (it is re-normalized defensively so a caller
    cannot smuggle in a weaker policy to hash).  Fails closed:

    * `ungated = false` with no commands -> not ok, not verified ("nothing to trust").
    * a recognized runner (pytest/ctest) that reports zero tests -> not ok even on exit 0.
    * a timeout kills the command's process group and fails the gate.

    `selected_tests` add an extra focused pass after the full configured commands; they never
    replace or narrow them (contract: extra selected tests cannot remove configured required
    commands).
    """
    cfg = normalize_config(config, origin="<run_gate config>")
    cwd = Path(cwd)
    result: dict = {"ok": False, "policy_hash": policy_hash(cfg), "log": "",
                    "verified": False, "reason": None}
    ver = cfg["verification"]
    log_parts: list[str] = []

    if not ver["commands"]:
        if ver["ungated"]:
            result.update(ok=True, reason="ungated project: no verification commands run",
                          log="ungated = true: gate approves without running anything\n")
            return result
        result.update(reason="no verification commands configured; refusing to approve "
                             "an unverified tree",
                      log="verification.commands is empty and ungated = false: fail closed\n")
        return result

    gate_env = _scrubbed_env(ver["environment"])
    if env:
        gate_env.update(env)
    started = time.monotonic()
    commands: list[dict] = []
    plan = list(ver["commands"])
    focus: list[tuple[str, list[str]]] = []
    if selected_tests:
        for argv in plan:
            kind = _runner_kind(argv)
            if kind:
                focus.append((kind, argv))
        if not focus:
            log_parts.append(f"note: selected_tests {list(selected_tests)} ignored: no "
                             "pytest/ctest command to focus them with\n")

    ok, verified, reason = True, True, None
    for argv in plan:
        log_parts.append("$ " + " ".join(argv) + "\n")
        rec = _run_command(argv, cwd, gate_env, ver["timeout_seconds"])
        commands.append({k: rec[k] for k in ("argv", "exit", "duration_seconds",
                                             "timed_out", "runner", "zero_tests")})
        log_parts.append(rec["output"])
        if not rec["output"].endswith("\n"):
            log_parts.append("\n")
        if rec["timed_out"]:
            ok, verified = False, False
            reason = f"command timed out after {ver['timeout_seconds']}s: {' '.join(argv)}"
            break
        if rec["exit"] != 0:
            ok, verified = False, False
            reason = f"command exited {rec['exit']}: {' '.join(argv)}"
            break
        if rec["zero_tests"]:
            ok, verified = False, False
            reason = f"{GATE_ZERO_REASON} in {' '.join(argv)}; a gate that tested nothing " \
                     "cannot approve"
            break

    if ok and selected_tests and focus:
        kind, base = focus[0]
        argv = _focused_pass(kind, base, selected_tests)
        log_parts.append("$ " + " ".join(argv) + "   # focused pass for selected_tests\n")
        rec = _run_command(argv, cwd, gate_env, ver["timeout_seconds"])
        commands.append({k: rec[k] for k in ("argv", "exit", "duration_seconds",
                                             "timed_out", "runner", "zero_tests")})
        log_parts.append(rec["output"])
        if not rec["output"].endswith("\n"):
            log_parts.append("\n")
        if rec["timed_out"] or rec["exit"] != 0:
            ok = False
            reason = ("focused selected-tests pass timed out" if rec["timed_out"]
                      else f"focused selected-tests pass exited {rec['exit']}")
        elif rec["zero_tests"]:
            ok = False
            reason = f"selected tests matched nothing ({GATE_ZERO_REASON})"

    log = "".join(log_parts)
    if len(log) > MAX_LOG_BYTES:
        log = log[:64 * 1024] + f"\n... [log truncated to {MAX_LOG_BYTES} bytes] ...\n" + \
              log[-(MAX_LOG_BYTES - 64 * 1024):]
    result.update(ok=ok, verified=verified and ok, reason=reason, log=log,
                  duration_seconds=round(time.monotonic() - started, 3), commands=commands)
    return result
