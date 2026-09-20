#!/usr/bin/env python3
"""#GMCF decision 5, live: with a systemd-run whose probe takes 5 s, the first pane starts
unisolated and says so, and a pane opened after the answer arrives is isolated.

Reads each pane shell's cgroup: `systemd-run --scope` execs in place, so an isolated shell's
/proc/<pid>/cgroup names a relay-pane-*.scope and an unisolated one does not.
"""
import os, sys, time, subprocess, signal, glob
sys.path.insert(0, "/home/elliott/repos/relay-terminal/docs/qa_evidence/2026-09-20-perf-profile/startup/harness")
import harness

HERE = "/tmp/claude-1000/pf4k/fix/spawn"
BIN = sys.argv[1] if len(sys.argv) > 1 else "/home/elliott/repos/relay-terminal/build/relay"
DISPLAY = ":291"


def descendants(pid):
    out = [pid]
    i = 0
    while i < len(out):
        try:
            out += [int(k) for k in open(f"/proc/{out[i]}/task/{out[i]}/children").read().split()]
        except OSError:
            pass
        i += 1
    return out[1:]


def shells(pid):
    found = []
    for p in descendants(pid):
        try:
            if open(f"/proc/{p}/comm").read().strip() != "bash":
                continue
            cg = open(f"/proc/{p}/cgroup").read().strip()
        except OSError:
            continue
        found.append((p, "relay-pane" in cg, cg.split("/")[-1]))
    return found


os.environ["PATH"] = os.path.join(HERE, "path-slow") + ":" + os.environ["PATH"]
os.environ["SKIP_FIRSTRUN"] = "1"
prof = os.path.join(HERE, "prof-late")
r = harness.run("late", prof, ["--fresh"], DISPLAY, binary=BIN, wait=20.0)
pid = r["pid"]
try:
    time.sleep(2.0)
    first = shells(pid)
    print("first pane shells at t=2s:", first)
    time.sleep(7.0)                      # the 5 s probe has answered by now
    env = {**os.environ, "DISPLAY": DISPLAY}
    wid = subprocess.run(["xdotool", "search", "--onlyvisible", "--pid", str(pid)],
                         capture_output=True, text=True, env=env, timeout=20).stdout.split()[0]
    subprocess.run(["xdotool", "windowfocus", wid], env=env, timeout=20)
    subprocess.run(["xdotool", "key", "--window", wid, "ctrl+e"], env=env, timeout=20)
    time.sleep(4.0)
    after = shells(pid)
    print("all pane shells at t=13s:", after)
    print("VERDICT first-pane-unisolated:", all(not iso for _, iso, _ in first) and len(first) == 1)
    print("VERDICT second-pane-isolated:", any(iso for _, iso, _ in after))
finally:
    os.kill(pid, signal.SIGTERM)
    harness.PROCS[pid].wait(timeout=30)
