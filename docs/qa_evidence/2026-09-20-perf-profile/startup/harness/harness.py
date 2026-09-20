#!/usr/bin/env python3
"""#PF4K startup-area harness: launch Relay in an isolated profile and time its phases.

Phases timed from exec (t0), by polling cheap observables at ~4 ms:
  window   first top-level window of the relay pid is mapped and viewable (xwininfo -root -tree)
  shell    the pane's shell reached its first prompt: $TMPDIR/relay-*/state.json exists
  worker   the per-pane python worker process exists (pgrep -P relay -f worker.py)
  wready   that worker answered: worker.log in the data dir records its first reply
Usage:  harness.py run  <tag> <profdir> [--extra relay-arg ...]
"""
import os, re, subprocess, sys, time, glob, shutil, json, signal

S = "/tmp/claude-1000/pf4k/startup"
PROCS = {}          # pid -> Popen, so shutdown timing reaps instead of polling a zombie


def fresh_profile(d, keep_runtime=True):
    if os.path.exists(d):
        shutil.rmtree(d, ignore_errors=True)
    for sub in ("cfg", "data", "state", "cache", "tmp", "run"):
        os.makedirs(os.path.join(d, sub), mode=0o700, exist_ok=True)
    os.chmod(d + "/run", 0o700)
    env = dict(os.environ)
    env.update(
        XDG_CONFIG_HOME=d + "/cfg", XDG_DATA_HOME=d + "/data",
        XDG_STATE_HOME=d + "/state", XDG_CACHE_HOME=d + "/cache",
        TMPDIR=d + "/tmp", RELAY_KEYRING="off", RELAY_NO_URL_HANDLER="1",
    )
    if not keep_runtime:          # isolation::available() then fails: systemd --user unreachable
        env["XDG_RUNTIME_DIR"] = d + "/run"
        env.pop("DBUS_SESSION_BUS_ADDRESS", None)
    if os.environ.get("SKIP_FIRSTRUN", "0") == "1":
        # A fresh profile otherwise opens two modal first-run dialogs (instructions onboarding at
        # configure+400 ms, the approvals choice at +800 ms), which swallow every synthetic key.
        os.makedirs(d + "/cfg/RelayTerminal", exist_ok=True)
        with open(d + "/cfg/RelayTerminal/relay.conf", "w") as f:
            f.write("[instructions]\nonboarded=true\n\n[security]\napprovals_chosen=true\n")
            if os.environ.get("NO_ISOLATION", "0") == "1":
                # systemd --user scopes need the real XDG_RUNTIME_DIR; a test that isolates it
                # must turn the per-pane scopes off or every worker exits 1.
                f.write("\n[isolation]\nenabled=false\n")
    return env


def windows_of(pid, display):
    """pids of mapped top-level windows -> True when one belongs to pid."""
    try:
        out = subprocess.run(["xdotool", "search", "--onlyvisible", "--pid", str(pid)],
                             capture_output=True, text=True, timeout=5,
                             env={**os.environ, "DISPLAY": display})
        return out.returncode == 0 and out.stdout.strip() != ""
    except Exception:
        return False


def run(tag, prof, extra, display, keep_runtime=True, binary=None, wait=45.0):
    binary = binary or os.environ.get("RELAY_BIN", "/tmp/claude-1000/pf4k/build/relay")
    env = fresh_profile(prof, keep_runtime)
    env["DISPLAY"] = display
    out = open(prof + "/stdout.log", "wb")
    load0 = open("/proc/loadavg").read().split()[0]
    wall0 = time.time()
    t0 = time.monotonic()
    p = subprocess.Popen([binary, "--clean-shell"] + extra, env=env, stdout=out,
                         stderr=subprocess.STDOUT, cwd=prof)
    marks = {}
    statejson = prof + "/tmp/relay-*/state.json"
    workerlog = prof + "/data/relay/worker.log"
    logdir = prof + "/data/relay/logs"
    deadline = t0 + wait
    while time.monotonic() < deadline:
        now = time.monotonic()
        if "window" not in marks and windows_of(p.pid, display):
            marks["window"] = now - t0
        if "shell" not in marks and glob.glob(statejson):
            marks["shell"] = now - t0
        if "worker" not in marks:
            try:
                kids = open(f"/proc/{p.pid}/task/{p.pid}/children").read().split()
            except OSError:
                kids = []
            for k in list(kids):
                try:
                    cl = open(f"/proc/{k}/cmdline", "rb").read().decode(errors="replace")
                except OSError:
                    continue
                if "worker.py" in cl:
                    marks["worker"] = now - t0
                    break
        if "wready" not in marks:
            try:
                if "type=ready" in open(logdir + "/relay.log").read():
                    marks["wready"] = now - t0
            except OSError:
                pass
        if len(marks) == 4:
            break
        time.sleep(0.004)
    load1 = open("/proc/loadavg").read().split()[0]
    res = dict(tag=tag, pid=p.pid, load_before=load0, load_after=load1, wall0=wall0,
               **{k: round(v * 1000, 1) for k, v in marks.items()})
    print(json.dumps(res), flush=True)
    PROCS[p.pid] = p
    with open(prof + "/relay.pid", "w") as f:
        f.write(str(p.pid))
    return res


if __name__ == "__main__":
    a = sys.argv[1:]
    tag, prof = a[0], a[1]
    extra = a[2:]
    disp = os.environ.get("DISPLAY", ":231")
    keep = os.environ.get("KEEP_RUNTIME", "1") == "1"
    run(tag, prof, extra, disp, keep)
