#!/usr/bin/env python3
"""Tool-execution wrapper cost, straight on the Executor (no provider).  toolbench.py <src-root>"""
import os, statistics, sys, threading, time, resource

ROOT = sys.argv[1]
sys.path.insert(0, os.path.join(ROOT, "backend"))
from relay_core import tools as T  # noqa: E402

WS = sys.argv[2] if len(sys.argv) > 2 else "/home/elliott/repos/relay-terminal"
events = []
ex = T.ToolExecutor(WS, events.append, threading.Event())


def run(cmd, timeout=600):
    p = ex.prepare("run_command", {"command": cmd})
    return ex.execute(p)


def med(f, n=7):
    xs = []
    for _ in range(n):
        a = time.perf_counter(); f(); xs.append(time.perf_counter() - a)
    return statistics.median(xs) * 1000


def cpu_med(f, n=7):
    xs = []
    for _ in range(n):
        a = time.process_time(); f(); xs.append(time.process_time() - a)
    return statistics.median(xs) * 1000


print(f"workspace {WS}")
print(f"run_command 'true'                : {med(lambda: run('true')):7.2f} ms wall  "
      f"{cpu_med(lambda: run('true')):6.2f} ms worker CPU")
print(f"bare subprocess /bin/bash -c true : {med(lambda: __import__('subprocess').run(['/bin/bash','--noprofile','--norc','-c','true'],capture_output=True)):7.2f} ms wall")
print(f"run_command 'echo hi'             : {med(lambda: run('echo hi')):7.2f} ms wall")

for mb in (1, 10, 50):
    cmd = f"head -c {mb*1024*1024} /dev/zero | tr '\\0' 'a'"
    events.clear()
    r0 = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    t = time.perf_counter(); c = time.process_time()
    out = run(cmd)
    wall = time.perf_counter() - t; cpu = time.process_time() - c
    r1 = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    print(f"run_command producing {mb:3d} MiB      : {wall*1000:7.0f} ms wall  {cpu*1000:6.0f} ms worker CPU  "
          f"result {len(out['output'])} chars, omitted {out['omitted_bytes']}  "
          f"tool_output events {len(events)}  maxRSS {r0/1024:.0f}->{r1/1024:.0f} MiB")

# read_file on a big repo file
big = None
for root, dirs, names in os.walk(WS):
    dirs[:] = [d for d in dirs if d not in (".git", "build")]
    for n in names:
        p = os.path.join(root, n)
        if not n.endswith((".h", ".cpp", ".py", ".md")):
            continue
        try:
            if 60_000 < os.path.getsize(p) < 120_000 and (big is None or os.path.getsize(p) > os.path.getsize(big)):
                big = p
        except OSError:
            pass
if big:
    rel = os.path.relpath(big, WS)
    pr = ex.prepare("read_file", {"path": rel})
    print(f"read_file {rel} ({os.path.getsize(big)/1024:.0f} KiB): {med(lambda: ex.execute(ex.prepare('read_file', {'path': rel}))):7.2f} ms")
print(f"list_directory .                  : {med(lambda: ex.execute(ex.prepare('list_directory', {'path': '.'}))):7.2f} ms")
print(f"rg via run_command (repo-wide)    : {med(lambda: run('rg -n \"board_tools\" -g \"!build\" . | head -50'), n=3):7.2f} ms")
print(f"grep -r via run_command           : {med(lambda: run('grep -rn \"board_tools\" --exclude-dir=build --exclude-dir=.git . | head -50'), n=3):7.2f} ms")
print("loadavg", os.getloadavg())
