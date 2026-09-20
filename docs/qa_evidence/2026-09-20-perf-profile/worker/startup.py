#!/usr/bin/env python3
"""Measure worker spawn -> {"event":"ready"} wall and CPU time. Usage: startup.py <src-root> [n]"""
import json, os, resource, subprocess, sys, time

root = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 5
env = dict(os.environ)
env["RELAY_KEYRING"] = "off"
env["PYTHONPATH"] = os.path.join(root, "backend")
env.setdefault("XDG_DATA_HOME", "/tmp/claude-1000/pf4k/worker/xdg/data")
env.setdefault("XDG_CONFIG_HOME", "/tmp/claude-1000/pf4k/worker/xdg/config")
env.setdefault("XDG_STATE_HOME", "/tmp/claude-1000/pf4k/worker/xdg/state")
env.setdefault("XDG_CACHE_HOME", "/tmp/claude-1000/pf4k/worker/xdg/cache")
for k in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"):
    os.makedirs(env[k], exist_ok=True)

cmd = [sys.executable, "-S", "-u", os.path.join(root, "backend", "worker.py")]
walls = []
for i in range(n):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, env=env, cwd=root)
    ready = None
    while True:
        line = p.stdout.readline()
        if not line:
            break
        obj = json.loads(line)
        if obj.get("event") == "ready":
            ready = time.perf_counter() - t0
            break
    p.stdin.close()
    p.wait()
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    maxrss = after.ru_maxrss
    walls.append(ready)
    print(f"run {i+1}: ready_wall={ready*1000:.1f} ms  child_cpu(total incl exit)={cpu*1000:.1f} ms  maxrss={maxrss/1024:.1f} MiB")
walls.sort()
print(f"median ready_wall = {walls[len(walls)//2]*1000:.1f} ms   min = {walls[0]*1000:.1f} ms  max = {walls[-1]*1000:.1f} ms")
print(f"python {sys.version.split()[0]}  loadavg {os.getloadavg()}")
