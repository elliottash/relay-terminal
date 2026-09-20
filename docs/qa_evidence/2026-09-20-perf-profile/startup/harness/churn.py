#!/usr/bin/env python3
"""#PF4K startup-area: memory growth across panes/tabs, does it come back, and open/close churn.

  churn.py mem            -- Pss/Private_Dirty at 1 pane, 4 panes, 3 tabs, then closed again
  churn.py churn <n>      -- open and close n panes in a loop; fds, children, runtime dirs, RSS
  churn.py restore <n>    -- build n panes, SIGTERM, relaunch without --fresh, time the restore
"""
import os, sys, time, json, glob, subprocess, signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness, idle

BASE = os.path.dirname(os.path.abspath(__file__))
DISP = os.environ.get("DISPLAY", ":231")
BIN = os.environ.get("RELAY_BIN", "/tmp/claude-1000/pf4k/build/relay")


def snap(pid):
    kids = idle.descendants(pid)
    out = dict(relay=idle.mem(pid), fds=len(glob.glob(f"/proc/{pid}/fd/*")),
               threads=idle.nthreads(pid), nchild=len(kids) - 1,
               workers=idle.panecount(pid))
    tot_pss = tot_pd = 0
    for p in kids:
        m = idle.mem(p)
        tot_pss += m.get("Pss", 0); tot_pd += m.get("Private_Dirty", 0)
    out["tree_pss_kb"] = tot_pss
    out["tree_pdirty_kb"] = tot_pd
    return out


def runtime_dirs(prof):
    return len(glob.glob(prof + "/tmp/relay-*"))


def cmd_mem():
    prof = os.path.join(BASE, "mem")
    r = harness.run("mem", prof, ["--fresh"], DISP)
    pid = r["pid"]
    steps = []
    time.sleep(2)
    steps.append(("just-started", snap(pid), runtime_dirs(prof)))
    time.sleep(6)
    steps.append(("1-pane-settled", snap(pid), runtime_dirs(prof)))
    idle.build_layout(pid, 4, 1)          # 3 splits -> 4 panes, one tab
    time.sleep(6)
    steps.append(("4-panes", snap(pid), runtime_dirs(prof)))
    idle.focus(pid)
    for _ in range(2):                     # +2 tabs (each brings a pane)
        idle.key(DISP, "ctrl+t", delay=1.8); idle.focus(pid)
    time.sleep(6)
    steps.append(("4-panes+3-tabs", snap(pid), runtime_dirs(prof)))
    # close the 3 splits and the 2 extra tabs again (pane.close = Ctrl+Shift+W by default)
    for _ in range(5):
        idle.key(DISP, "ctrl+shift+w", delay=1.5); idle.focus(pid)
    time.sleep(8)
    steps.append(("after-closing", snap(pid), runtime_dirs(prof)))
    time.sleep(20)
    steps.append(("after-closing+20s", snap(pid), runtime_dirs(prof)))
    out = [dict(step=s, runtime_dirs=rd, **v) for s, v, rd in steps]
    print(json.dumps(out, indent=1))
    json.dump(out, open(prof + "/mem.json", "w"), indent=1)
    harness.PROCS[pid].terminate(); harness.PROCS[pid].wait(60)


def cmd_churn(n):
    prof = os.path.join(BASE, "churn")
    r = harness.run("churn", prof, ["--fresh"], DISP)
    pid = r["pid"]
    time.sleep(6)
    base = snap(pid); base["runtime_dirs"] = runtime_dirs(prof)
    rows = [dict(step="baseline", **base)]
    for i in range(n):
        idle.focus(pid)
        idle.key(DISP, "ctrl+e", delay=1.2)
        idle.focus(pid)
        idle.key(DISP, "ctrl+shift+w", delay=1.2)
        if (i + 1) % 5 == 0:
            time.sleep(3)
            s = snap(pid); s["runtime_dirs"] = runtime_dirs(prof)
            rows.append(dict(step=f"after-{i+1}", **s))
            print(json.dumps(rows[-1]), flush=True)
    time.sleep(20)
    s = snap(pid); s["runtime_dirs"] = runtime_dirs(prof)
    rows.append(dict(step="settled", **s))
    print(json.dumps(rows, indent=1))
    json.dump(rows, open(prof + "/churn.json", "w"), indent=1)
    harness.PROCS[pid].terminate(); harness.PROCS[pid].wait(60)


def cmd_restore(panes, tabs):
    """Build a layout, quit with SIGTERM, relaunch WITHOUT --fresh and time the restore.

    windows.lock lives in XDG_RUNTIME_DIR, so a shared one means "another Relay owns the layout"
    and nothing is saved at all.  Private runtime dir here, with the real session bus kept so
    systemd-run --user (the per-pane isolation scope) still works.
    """
    prof = os.path.join(BASE, "restore")
    os.makedirs(prof + "/run", mode=0o700, exist_ok=True)
    os.environ["XDG_RUNTIME_DIR"] = prof + "/run"
    os.environ["NO_ISOLATION"] = "1"
    r = harness.run("build", prof, ["--fresh"], DISP)
    pid = r["pid"]
    time.sleep(5)
    idle.build_layout(pid, panes, tabs)
    time.sleep(8)
    got = max(idle.panecount(pid), len(glob.glob(prof + "/tmp/relay-*")))
    t = time.monotonic()
    harness.PROCS[pid].terminate(); harness.PROCS[pid].wait(120)
    term = round((time.monotonic() - t) * 1000, 1)
    print(json.dumps(dict(step="built", panes_built=got, term_ms=term)), flush=True)
    time.sleep(2)
    # relaunch into the same profile, no --fresh, no wipe
    env = dict(os.environ)
    env.update(XDG_CONFIG_HOME=prof + "/cfg", XDG_DATA_HOME=prof + "/data",
               XDG_STATE_HOME=prof + "/state", XDG_CACHE_HOME=prof + "/cache",
               TMPDIR=prof + "/tmp", RELAY_KEYRING="off", RELAY_NO_URL_HANDLER="1",
               XDG_RUNTIME_DIR=prof + "/run", DISPLAY=DISP)
    t0 = time.monotonic()
    p = subprocess.Popen([BIN, "--clean-shell"], env=env, cwd=prof,
                         stdout=open(prof + "/restore.log", "wb"), stderr=subprocess.STDOUT)
    marks = {}
    while time.monotonic() - t0 < 60:
        now = time.monotonic()
        if "window" not in marks and harness.windows_of(p.pid, DISP):
            marks["window"] = round((now - t0) * 1000, 1)
        w = max(idle.panecount(p.pid), len(glob.glob(prof + "/tmp/relay-*")))
        if w >= got and "allworkers" not in marks:
            marks["allworkers"] = round((now - t0) * 1000, 1)
        if len(marks) == 2:
            break
        time.sleep(0.005)
    time.sleep(8)
    out = dict(step="restored", built=got, restored_workers=idle.panecount(p.pid),
               restored_runtime_dirs=len(glob.glob(prof + "/tmp/relay-*")), **marks,
               **snap(p.pid))
    print(json.dumps(out, indent=1))
    json.dump(out, open(prof + "/restore.json", "w"), indent=1)
    p.terminate(); p.wait(120)


if __name__ == "__main__":
    what = sys.argv[1]
    if what == "mem":
        cmd_mem()
    elif what == "churn":
        cmd_churn(int(sys.argv[2]))
    elif what == "restore":
        cmd_restore(int(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 1)
