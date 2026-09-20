#!/usr/bin/env python3
"""Run the startup matrix N times per scenario and print a table.

"cold" evicts the relay binary and every shared library it maps from the page cache with
posix_fadvise(POSIX_FADV_DONTNEED) — no sysctl, no root, nothing system-wide.
"""
import os, sys, time, json, subprocess, statistics, signal, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness

BIN = os.environ.get("RELAY_BIN", "/tmp/claude-1000/pf4k/build/relay")


def evict(path):
    try:
        fd = os.open(path, os.O_RDONLY)
        os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
        os.close(fd)
    except OSError:
        pass


def evict_all():
    evict(BIN)
    out = subprocess.run(["ldd", BIN], capture_output=True, text=True).stdout
    for line in out.splitlines():
        for tok in line.split():
            if tok.startswith("/") and os.path.exists(tok):
                evict(tok)


def stop(pid, prof):
    """SIGTERM -> reaped. A zombie still answers kill(pid,0), so wait() is the only honest test."""
    p = harness.PROCS.get(pid)
    t = time.monotonic()
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError:
        return 0.0
    if p is not None:
        try:
            p.wait(timeout=60)
        except subprocess.TimeoutExpired:
            p.kill(); p.wait()
            return -1.0
    return round((time.monotonic() - t) * 1000, 1)


def scenario(tag, extra, n, cold=False, prof=None, keep_prof=False):
    prof = prof or f"/tmp/claude-1000/pf4k/startup/prof-{tag}"
    rows = []
    for i in range(n):
        if cold:
            evict_all()
            time.sleep(0.3)
        r = harness.run(f"{tag}#{i}", prof, list(extra), os.environ.get("DISPLAY", ":231"))
        time.sleep(1.5)
        r["term_ms"] = stop(r["pid"], prof)
        rows.append(r)
        time.sleep(0.5)
    return rows


def summarize(rows, keys=("window", "shell", "worker", "wready", "term_ms")):
    out = {}
    for k in keys:
        vals = [r[k] for r in rows if k in r]
        if vals:
            out[k] = dict(median=round(statistics.median(vals), 1),
                          min=round(min(vals), 1), max=round(max(vals), 1))
    return out


if __name__ == "__main__":
    n = int(os.environ.get("N", "3"))
    results = {}
    plan = [
        ("warm-fresh", ["--fresh"], False),
        ("cold-fresh", ["--fresh"], True),
    ]
    if len(sys.argv) > 1:
        plan = [p for p in plan if p[0] in sys.argv[1:]]
    for tag, extra, cold in plan:
        rows = scenario(tag, extra, n, cold)
        results[tag] = dict(runs=rows, summary=summarize(rows))
        print(tag, json.dumps(results[tag]["summary"]), flush=True)
    with open("/tmp/claude-1000/pf4k/startup/matrix.json", "w") as f:
        json.dump(results, f, indent=1)
