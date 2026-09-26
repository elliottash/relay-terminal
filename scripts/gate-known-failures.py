#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run the Python suite and fail only on failures that are not already known (card #3MH4).

The queue gate runs every test, and `main` carried failures the legacy targeted-test procedure
never ran. Until they are fixed, `.relay/known-failures.txt` lists them: a known failure is
reported but tolerated, a new one fails the gate, and a known one that now passes is reported
so the list can shrink. The runner must also finish (its "Ran N tests" line), so a crashed or
hung suite is never mistaken for a clean one.

    scripts/gate-known-failures.py [--known FILE] [--update] [--jobs N]

By default the suite's modules run in parallel, each in its own process with its own data
home and TMPDIR (#VK6J); `--jobs 1` runs scripts/test.sh serially.

`--update` rewrites the list from this run instead of judging it.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULT = re.compile(r"^(?:.*\((?P<id>[\w.]+)\)|(?P<doc>.+?)) \.\.\. (?P<outcome>ok|FAIL|ERROR|skipped.*|expected failure|unexpected success)$")
GUEST_HOME_VARS = ("CLAUDE_CONFIG_DIR", "CODEX_HOME", "RELAY_USER_CLAUDE_CONFIG_DIR",
                   "RELAY_USER_CODEX_HOME", "RELAY_GUEST_HOME")
CLASS_ERROR = re.compile(r"^ERROR: (?P<id>\w+ \([\w.]+\))$")


def run_serial() -> str:
    proc = subprocess.run([str(ROOT / "scripts" / "test.sh")], cwd=ROOT, env=dict(os.environ),
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    return proc.stdout


def module_env(scratch: Path) -> dict:
    """What scripts/test.sh sets, per module: its own data home, so modules running side by
    side never share session autosaves or indexes (#VK6J). TMPDIR is left alone unless it is too
    long for an AF_UNIX socket path (108 bytes); the code under test treats everything below
    $TMPDIR as scratch (test_scratch_ledger), so it is never a per-module directory."""
    env = dict(os.environ)
    # A Relay pane points these at the owner's real guest homes; a test that follows them reads
    # the owner's own Claude and Codex transcripts instead of its synthetic ones, and polls ten
    # seconds for a row that never comes (test_session_protocol: 176 s, 15 failures -> 48 s, 0).
    for name in GUEST_HOME_VARS:
        env.pop(name, None)
    data = scratch / "data"
    data.mkdir(parents=True)
    # A long inherited TMPDIR (a Relay pane's) breaks AF_UNIX socket paths; otherwise leave it
    # exactly as the serial run has it, since the code under test treats $TMPDIR as scratch.
    if len(env.get("TMPDIR", "")) > 40:
        env["TMPDIR"] = "/tmp"
    env.update({"XDG_DATA_HOME": str(data), "RELAY_LOG_ORIGIN": "test",
                "RELAY_KEYRING": "off", "RELAY_LOCAL_MODELS": str(data / "local-models.json"),
                "RELAY_MEMORY_IMPORT": "off",
                "PYTHONPATH": os.pathsep.join(filter(None, [str(ROOT / "backend"), str(ROOT / "tests"),
                                                            os.environ.get("PYTHONPATH")]))})
    return env


def run_parallel(jobs: int, timeout: float) -> str:
    """Every tests/test_*.py module as its own `unittest -v` process, `jobs` at a time, slowest
    first by the last run's timings. Same output lines as the serial run, one module after
    another, so the judgement below is unchanged. A module that overruns `timeout` is killed
    and reported as an error of that module, never as a pass."""
    import concurrent.futures
    import json
    import shutil
    import tempfile
    import time
    modules = sorted(p.stem for p in (ROOT / "tests").glob("test_*.py"))
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "relay" / "test-durations.json"
    try:
        timings = json.loads(cache.read_text())
    except (OSError, ValueError):
        timings = {}
    modules.sort(key=lambda m: -float(timings.get(m, 30.0)))
    # Short and under /tmp: tests make AF_UNIX sockets in TMPDIR, and a socket path is capped at
    # 108 bytes, so a scratch under a long pane TMPDIR broke the guest bridge tests.
    base = Path(tempfile.mkdtemp(prefix="rg", dir="/tmp"))
    index = {m: "%03d" % i for i, m in enumerate(modules)}

    def run(module):
        scratch = base / index[module]
        started = time.monotonic()
        try:
            proc = subprocess.run([sys.executable, "-m", "unittest", "-v", module], cwd=ROOT,
                                  env=module_env(scratch), stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, errors="replace",
                                  timeout=timeout)
            out = proc.stdout
        except subprocess.TimeoutExpired as exc:
            out = (exc.stdout or "") if isinstance(exc.stdout, str) else ""
            out += "\n%s (%s.Timeout) ... ERROR\n" % (module, module)
        return module, out, time.monotonic() - started

    run_parallel.run_one = run
    outputs, durations = {}, {}
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(run, m) for m in modules]
            done = 0
            for future in concurrent.futures.as_completed(futures):
                module, out, seconds = future.result()
                outputs[module], durations[module] = out, seconds
                done += 1
                bad = len(re.findall(r" \.\.\. (?:FAIL|ERROR)$", out, re.M))
                # One line per module, flushed: the gate's live.log shows how far it has got.
                print("== gate %s python %d/%d %s %.0fs%s" % (
                    time.strftime("%H:%M:%S"), done, len(modules), module, seconds,
                    " (%d failing)" % bad if bad else ""), flush=True)
    finally:
        shutil.rmtree(base, ignore_errors=True)
    try:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps({m: round(s, 2) for m, s in durations.items()}, indent=0))
    except OSError:
        pass
    slowest = sorted(durations.items(), key=lambda kv: -kv[1])[:10]
    print("gate-known-failures: %d modules, %d at a time; slowest: %s" % (
        len(modules), jobs, ", ".join("%s %.0fs" % kv for kv in slowest)))
    missing = [m for m in modules if not re.search(r"^Ran \d+ tests? in ", outputs[m], re.M)]
    text = "".join(outputs[m] for m in modules)
    # Every module must finish; one that did not is a failure of the run, not a silence.
    return text if not missing else text + "\nunfinished: %s\n" % " ".join(missing)


def run_suite(jobs: int = 1, timeout: float = 900) -> tuple[set, bool, str]:
    out = run_serial() if jobs <= 1 else run_parallel(jobs, timeout)
    return parse_failures(out), finished_ok(out), out


def finished_ok(out: str) -> bool:
    return (re.search(r"^Ran \d+ tests? in ", out, re.M) is not None
            and "\nunfinished: " not in out)


def parse_failures(out: str) -> set:
    failed = set()
    for line in out.splitlines():
        m = RESULT.match(line)
        if m and m.group("outcome") in ("FAIL", "ERROR", "unexpected success"):
            failed.add(m.group("id") or m.group("doc").strip())
            continue
        m = CLASS_ERROR.match(line)
        if m and ("setUpClass" in line or "setUpModule" in line or "tearDown" in line):
            failed.add(m.group("id"))
    return failed


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--known", default=str(ROOT / ".relay" / "known-failures.txt"))
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--jobs", type=int,
                    default=int(os.environ.get("RELAY_GATE_JOBS") or min(os.cpu_count() or 1, 12)),
                    help="modules run at once; 1 runs scripts/test.sh serially as before")
    ap.add_argument("--module-timeout", type=float, default=900.0)
    args = ap.parse_args()
    known_path = Path(args.known)
    known = set()
    if known_path.is_file():
        known = {l.strip() for l in known_path.read_text().splitlines()
                 if l.strip() and not l.startswith("#")}
    failed, finished, out = run_suite(args.jobs, args.module_timeout)
    if not finished:
        sys.stdout.write(out[-20000:])
        print("gate-known-failures: the suite did not finish (no 'Ran N tests' line)")
        return 1
    if args.update:
        header = ("# Python tests failing on main when the queue gate began (card #3MH4).\n"
                  "# The gate tolerates these and fails on any other. Remove a line once it passes.\n")
        known_path.write_text(header + "".join(sorted(t + "\n" for t in failed)))
        print("gate-known-failures: wrote %d known failure(s) to %s" % (len(failed), known_path))
        return 0
    new = sorted(failed - known)
    if new and args.jobs > 1 and hasattr(run_parallel, "run_one"):
        # Twelve modules at once can starve a timing-sensitive test. A module with a new
        # failure is run again, alone; only what fails again counts (#VK6J).
        modules = sorted({t.split(".")[0] for t in new if re.match(r"^test_\w+\.", t)})
        rerun = set()
        for module in modules:
            _m, text, _s = run_parallel.run_one(module)
            out += text
            rerun |= parse_failures(text)
        confirmed = {t for t in new if not re.match(r"^test_\w+\.", t)} | (rerun & set(new))
        if set(new) - confirmed:
            print("gate-known-failures: passed when rerun alone (flaky under load): %s"
                  % ", ".join(sorted(set(new) - confirmed)))
        failed = (failed - set(new)) | confirmed
        new = sorted(confirmed)
    fixed = sorted(known - failed)
    print("gate-known-failures: %d failed, %d known, %d new, %d known now passing"
          % (len(failed), len(failed & known), len(new), len(fixed)))
    for t in fixed:
        print("  now passing (remove from %s): %s" % (known_path.name, t))
    if new:
        # The suite's own output for each new failure, so the author sees why.
        sys.stdout.write(out[-60000:])
        for t in new:
            print("  NEW FAILURE: %s" % t)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
