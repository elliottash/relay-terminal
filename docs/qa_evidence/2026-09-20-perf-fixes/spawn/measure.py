#!/usr/bin/env python3
"""#GMCF decision 5: exec -> window-mapped, before vs after, healthy and with a slow systemd-run.

Reuses the #PF4K startup harness (docs/qa_evidence/2026-09-20-perf-profile/startup/harness).
"""
import os, sys, time, json, signal, statistics, subprocess, shutil

HARNESS = "/home/elliott/repos/relay-terminal/docs/qa_evidence/2026-09-20-perf-profile/startup/harness"
sys.path.insert(0, HARNESS)
import harness

HERE = "/tmp/claude-1000/pf4k/fix/spawn"
BEFORE = "/tmp/claude-1000/pf4k/build/relay"
AFTER = "/home/elliott/repos/relay-terminal/build/relay"
DISPLAY = ":291"


def fake(name, body):
    """A fake systemd-run that delays (or fails) only the `-- true` probe and otherwise delegates."""
    d = os.path.join(HERE, "path-" + name)
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, "systemd-run")
    with open(p, "w") as f:
        f.write("#!/bin/sh\ncase \"$*\" in\n  *'-- true') %s ;;\nesac\nexec /usr/bin/systemd-run \"$@\"\n" % body)
    os.chmod(p, 0o755)
    return d


def stop(pid):
    p = harness.PROCS.get(pid)
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError:
        return
    if p is not None:
        try:
            p.wait(timeout=30)
        except subprocess.TimeoutExpired:
            p.kill(); p.wait()


def scenario(tag, binary, n, path_prefix=None):
    saved = os.environ.get("PATH")
    if path_prefix:
        os.environ["PATH"] = path_prefix + ":" + saved
    rows = []
    prof = os.path.join(HERE, "prof-" + tag)
    try:
        for i in range(n):
            r = harness.run(f"{tag}#{i}", prof, ["--fresh"], DISPLAY, binary=binary, wait=30.0)
            time.sleep(1.0)
            stop(r["pid"])
            rows.append(r)
            time.sleep(0.5)
    finally:
        if path_prefix:
            os.environ["PATH"] = saved
    out = {}
    for k in ("window", "shell", "worker", "wready"):
        vals = [r[k] for r in rows if k in r]
        out[k] = dict(median=round(statistics.median(vals), 1), min=round(min(vals), 1),
                      max=round(max(vals), 1), n=len(vals)) if vals else "never"
    return out, rows


if __name__ == "__main__":
    n = int(os.environ.get("N", "5"))
    slow = fake("slow", "/bin/sleep 5; exit 0")
    failing = fake("fail", "exit 1")
    plan = [
        ("before-healthy", BEFORE, None),
        ("after-healthy", AFTER, None),
        ("before-slow", BEFORE, slow),
        ("after-slow", AFTER, slow),
        ("after-failing", AFTER, failing),
    ]
    if len(sys.argv) > 1:
        plan = [p for p in plan if p[0] in sys.argv[1:]]
    results = {}
    for tag, binary, path in plan:
        runs = n if "slow" not in tag and "failing" not in tag else max(3, n - 2)
        summary, rows = scenario(tag, binary, runs, path)
        results[tag] = dict(summary=summary, runs=rows)
        print("==", tag, json.dumps(summary), flush=True)
    with open(os.path.join(HERE, "measure.json"), "w") as f:
        json.dump(results, f, indent=1)
