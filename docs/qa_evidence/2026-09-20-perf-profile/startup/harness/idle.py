#!/usr/bin/env python3
"""#PF4K startup-area: idle CPU, wakeups and memory of relay + its children.

  idle.py <tag> <panes> <tabs> <seconds>

Builds the layout with xdotool (Ctrl+E = pane.splitRight, Ctrl+T = tab.new), lets it settle,
then samples /proc for `seconds`.  Reports, for relay and every descendant:
  cpu%      (utime+stime deltas / wall)
  wakeups/s (voluntary + nonvoluntary context switches, per thread, summed)
  Pss / Private_Dirty from smaps_rollup
"""
import os, sys, time, json, glob, subprocess, signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness

CLK = os.sysconf("SC_CLK_TCK")
BASE = os.path.dirname(os.path.abspath(__file__))
DISP = os.environ.get("DISPLAY", ":231")


def descendants(pid):
    out = [pid]
    stack = [pid]
    while stack:
        p = stack.pop()
        for t in glob.glob(f"/proc/{p}/task/*/children"):
            try:
                kids = open(t).read().split()
            except OSError:
                continue
            for k in kids:
                k = int(k)
                if k not in out:
                    out.append(k); stack.append(k)
    return out


def cpu_ticks(pid):
    try:
        f = open(f"/proc/{pid}/stat").read()
        fields = f[f.rindex(")") + 2:].split()
        return int(fields[11]) + int(fields[12])      # utime, stime
    except (OSError, ValueError, IndexError):
        return None


def switches(pid):
    v = n = 0
    for t in glob.glob(f"/proc/{pid}/task/*/status"):
        try:
            for line in open(t):
                if line.startswith("voluntary_ctxt_switches"):
                    v += int(line.split()[1])
                elif line.startswith("nonvoluntary_ctxt_switches"):
                    n += int(line.split()[1])
        except OSError:
            pass
    return v, n


def mem(pid):
    out = {}
    try:
        for line in open(f"/proc/{pid}/smaps_rollup"):
            k, _, rest = line.partition(":")
            if k in ("Pss", "Private_Dirty", "Rss"):
                out[k] = int(rest.split()[0])          # kB
    except OSError:
        pass
    return out


def comm(pid):
    try:
        c = open(f"/proc/{pid}/cmdline", "rb").read().decode(errors="replace").split("\0")
        c = [x for x in c if x]
        if not c:
            return open(f"/proc/{pid}/comm").read().strip()
        base = os.path.basename(c[0])
        if base.startswith("python") and len(c) > 1:
            base += " " + os.path.basename(c[-1])
        return base
    except OSError:
        return "?"


def nthreads(pid):
    return len(glob.glob(f"/proc/{pid}/task/*"))


def key(disp, k, n=1, delay=0.6):
    for _ in range(n):
        subprocess.run(["xdotool", "key", "--clearmodifiers", k],
                       env={**os.environ, "DISPLAY": disp}, capture_output=True)
        time.sleep(delay)


def sample(pids, seconds):
    t0 = time.monotonic()
    c0 = {p: cpu_ticks(p) for p in pids}
    s0 = {p: switches(p) for p in pids}
    time.sleep(seconds)
    wall = time.monotonic() - t0
    rows = []
    total_cpu = total_wk = 0.0
    for p in pids:
        c1, s1 = cpu_ticks(p), switches(p)
        if c0.get(p) is None or c1 is None:
            continue
        cpu = (c1 - c0[p]) / CLK / wall * 100
        vol = (s1[0] - s0[p][0]) / wall
        nvol = (s1[1] - s0[p][1]) / wall
        m = mem(p)
        rows.append(dict(pid=p, cmd=comm(p), threads=nthreads(p), cpu_pct=round(cpu, 2),
                         wakeups_s=round(vol + nvol, 1), vol_s=round(vol, 1),
                         nonvol_s=round(nvol, 1), pss_kb=m.get("Pss"), pdirty_kb=m.get("Private_Dirty")))
        total_cpu += cpu; total_wk += vol + nvol
    return rows, round(total_cpu, 2), round(total_wk, 1), round(wall, 1)


def focus(pid):
    """No window manager on the Xvfb display, so input focus has to be set by hand."""
    out = subprocess.run(["xdotool", "search", "--onlyvisible", "--pid", str(pid)],
                         capture_output=True, text=True,
                         env={**os.environ, "DISPLAY": DISP}).stdout.split()
    best = None
    for w in out:
        g = subprocess.run(["xdotool", "getwindowgeometry", w], capture_output=True, text=True,
                           env={**os.environ, "DISPLAY": DISP}).stdout
        if "1x1" not in g and "3x3" not in g:
            best = w
    if best:
        subprocess.run(["xdotool", "windowfocus", "--sync", best], capture_output=True,
                       env={**os.environ, "DISPLAY": DISP})
        subprocess.run(["xdotool", "mousemove", "--window", best, "400", "600"],
                       capture_output=True, env={**os.environ, "DISPLAY": DISP})
    return best


def panecount(pid):
    n = 0
    for t in glob.glob(f"/proc/{pid}/task/*/children"):
        try:
            kids = open(t).read().split()
        except OSError:
            continue
        for k in kids:
            try:
                if "worker.py" in open(f"/proc/{k}/cmdline", "rb").read().decode(errors="replace"):
                    n += 1
            except OSError:
                pass
    return n


def build_layout(pid, panes, tabs):
    focus(pid)
    for _ in range(max(0, tabs - 1)):
        key(DISP, "ctrl+t", delay=1.8)
        focus(pid)
    for _ in range(max(0, panes - tabs)):
        key(DISP, "ctrl+e", delay=1.8)
        focus(pid)
    return panecount(pid)


if __name__ == "__main__":
    tag, panes, tabs, secs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4])
    prof = os.path.join(BASE, f"idle-{tag}")
    r = harness.run(tag, prof, ["--fresh"], DISP)
    pid = r["pid"]
    time.sleep(4)
    mem_start = mem(pid)
    got = build_layout(pid, panes, tabs)
    time.sleep(6)
    pids = descendants(pid)
    load = open("/proc/loadavg").read().split()[0]
    rows, tc, tw, wall = sample(pids, secs)
    out = dict(tag=tag, panes=panes, tabs=tabs, panes_built=got, seconds=wall, load=load,
               mem_at_start=mem_start, total_cpu_pct=tc, total_wakeups_s=tw,
               nproc=len(rows), rows=rows)
    print(json.dumps(out, indent=1))
    with open(prof + "/idle.json", "w") as f:
        json.dump(out, f, indent=1)
    harness.PROCS[pid].send_signal(signal.SIGTERM)
    t = time.monotonic()
    harness.PROCS[pid].wait(timeout=60)
    print("term_ms", round((time.monotonic() - t) * 1000, 1))
