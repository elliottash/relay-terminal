#!/usr/bin/env python3
"""Idle worker: Pss, threads, and syscall wakeups over N seconds.  idle.py <root> <secs> [--configure]"""
import json, os, subprocess, sys, time

ROOT, SECS = sys.argv[1], int(sys.argv[2])
CONF = "--configure" in sys.argv
scratch = os.path.dirname(os.path.abspath(__file__))
env = dict(os.environ); env["RELAY_KEYRING"] = "off"; env["PYTHONPATH"] = os.path.join(ROOT, "backend")
for k, sub in (("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
               ("XDG_STATE_HOME", "state"), ("XDG_CACHE_HOME", "cache")):
    env[k] = os.path.join(scratch, "xdg-idle", sub); os.makedirs(env[k], exist_ok=True)
ws = os.path.join(scratch, "ws"); os.makedirs(ws, exist_ok=True)
strace_out = os.path.join(scratch, "idle-strace.txt")
cmd = ["strace", "-f", "-c", "-o", strace_out, sys.executable, "-S", "-u",
       os.path.join(ROOT, "backend", "worker.py")]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.DEVNULL, env=env, cwd=ws)


def send(o):
    p.stdin.write((json.dumps(o) + "\n").encode()); p.stdin.flush()


def wait(names, t=60):
    end = time.time() + t
    while time.time() < end:
        line = p.stdout.readline()
        if not line:
            raise SystemExit("died")
        o = json.loads(line)
        if o.get("event") in names:
            return o


wait({"ready"})
if CONF:
    send({"type": "configure", "base_url": "http://127.0.0.1:1/v1", "model": "stub",
          "api_key": "k", "workspace": ws})
    wait({"configured", "error"})
pid = p.pid
# the real worker is strace's child
kids = [int(x) for x in os.listdir(f"/proc/{pid}/task/{pid}/children".replace("children", "")) if False] if False else []
children = open(f"/proc/{pid}/task/{pid}/children").read().split()
wpid = int(children[0]) if children else pid


def snap():
    out = {}
    try:
        for line in open(f"/proc/{wpid}/smaps_rollup"):
            for k in ("Rss:", "Pss:", "Private_Dirty:"):
                if line.startswith(k):
                    out[k.strip(":")] = int(line.split()[1])
    except Exception:
        pass
    out["threads"] = len(os.listdir(f"/proc/{wpid}/task"))
    st = open(f"/proc/{wpid}/stat").read().rsplit(") ", 1)[1].split()
    out["cpu_s"] = (int(st[11]) + int(st[12])) / os.sysconf("SC_CLK_TCK")
    for line in open(f"/proc/{wpid}/sched"):
        if "nr_switches" in line or "nr_voluntary_switches" in line:
            out[line.split(":")[0].strip()] = line.split(":")[1].strip()
    return out


a = snap()
time.sleep(SECS)
b = snap()
print(f"idle {SECS}s  configured={CONF}")
print(f"  Pss {b['Pss']/1024:.1f} MiB   Rss {b['Rss']/1024:.1f} MiB   Private_Dirty {b['Private_Dirty']/1024:.1f} MiB   threads {b['threads']}")
print(f"  CPU used while idle: {(b['cpu_s']-a['cpu_s'])*1000:.0f} ms over {SECS}s")
for k in b:
    if "switch" in k:
        print(f"  {k}: {a.get(k)} -> {b.get(k)}")
send({"type": "shutdown"})
try:
    p.wait(timeout=20)
except subprocess.TimeoutExpired:
    p.kill()
print("--- syscalls while alive (whole process lifetime, incl. startup):")
print(open(strace_out).read()[:2000])
