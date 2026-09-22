#!/usr/bin/env python3
"""Interactive xdotool driver for the live end-to-end QA run on #7BM4.

Unlike a one-shot pass script, this keeps the app alive between invocations: `start` launches
Xvfb + relay detached and writes session.json; every other subcommand acts on that session.
That is what lets a Verify turn run for half an hour while the driver polls the card file.

Input is xdotool only -- clicks, chords and text typed into the app's own fields. Nothing is
ever typed into a terminal pane.

  drive.py start                 launch (reads RELAY_KIMI_API_KEY from this process env)
  drive.py shot NAME             screenshot the root window into the evidence dir
  drive.py ocr [NAME]            OCR the last (or a named) screenshot, print word boxes
  drive.py find WORD...          print the centre of the first matching word
  drive.py click X Y             click at a point
  drive.py clicktext WORD        OCR a fresh capture, click the first match
  drive.py key CHORD [HOLD]      xdotool key to the app window
  drive.py keyfocus CHORD        xdotool key to whatever has focus (popup menus)
  drive.py type TEXT             xdotool type into the focused field
  drive.py stop                  kill -TERM relay, then Xvfb
"""
import csv
import io
import json
import os
import subprocess
import sys
import time

RUN = "/tmp/verify-7bm4-fresh"
OUT = "/home/elliott/repos/relay-terminal/docs/qa_evidence/2026-09-21-verify-7BM4"
BIN = "/tmp/verify-7bm4-fresh/src/build/relay"
WORK = "/tmp/verify-7bm4-fresh/src"
W, H = 1600, 1000
STATE = f"{RUN}/session.json"
# No ~/.npm-global/bin: `claude` and `codex` must not be on PATH, or the verifier recommendation
# picks a guest CLI that has no credentials in this sandbox HOME. With them gone, the only
# available runner is the one whose key is in the environment: preset:kimi.
PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"


def sh(*a):
    return subprocess.run(a, capture_output=True, text=True)


def state():
    with open(STATE) as f:
        return json.load(f)


def start():
    sys.path.insert(0, "/home/elliott/repos/relay-terminal/backend")
    from relay_core import keystore
    os.environ["RELAY_KIMI_API_KEY"] = keystore.lookup("kimi")
    os.makedirs(OUT, exist_ok=True)
    sb = f"{RUN}/sb"
    for d in (f"{sb}/home/.config/RelayTerminal", f"{sb}/run", f"{sb}/tmp",
              f"{sb}/home/.local/share", f"{sb}/home/.cache"):
        os.makedirs(d, exist_ok=True)
    os.chmod(f"{sb}/run", 0o700)
    bus = f"/run/user/{os.getuid()}/bus"
    link = f"{sb}/run/bus"
    if os.path.exists(bus) and not os.path.lexists(link):
        os.symlink(bus, link)
    # Onboarding off, approvals answered (so nothing asks mid-turn), and the main agent and the
    # helper-agent role both pinned to the GLM Coding Plan's Main tier.
    with open(f"{sb}/home/.config/RelayTerminal/relay.conf", "w") as f:
        f.write("[instructions]\nonboarded=true\n"
                "[security]\napprovals_chosen=true\n"
                "[provider]\npreset=kimi\nmodel=kimi-k3\n"
                "[roles]\nswitchboard/preset=kimi\nswitchboard/model=kimi-k3\n")
    disp = next(f":{n}" for n in range(170, 200) if not os.path.exists(f"/tmp/.X11-unix/X{n}"))
    xvfb = subprocess.Popen(["Xvfb", disp, "-screen", "0", f"{W + 40}x{H + 40}x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    time.sleep(3)
    env = dict(os.environ, DISPLAY=disp, RELAY_KEYRING="off", PATH=PATH,
               HOME=f"{sb}/home", XDG_RUNTIME_DIR=f"{sb}/run", TMPDIR=f"{sb}/tmp",
               XDG_CONFIG_HOME=f"{sb}/home/.config", XDG_DATA_HOME=f"{sb}/home/.local/share",
               XDG_CACHE_HOME=f"{sb}/home/.cache", RELAY_SESSION="e2e-7bm4")
    if not env.get("RELAY_KIMI_API_KEY"):
        print("FAIL: RELAY_KIMI_API_KEY is not in this process environment")
        sys.exit(1)
    log = open(f"{RUN}/relay.log", "w")
    relay = subprocess.Popen([BIN, "--workspace", WORK, "--clean-shell", "--fresh"],
                             cwd=WORK, env=env, stdout=log, stderr=subprocess.STDOUT,
                             start_new_session=True)
    os.environ["DISPLAY"] = disp
    win, best = None, 0
    deadline = time.time() + 120
    while time.time() < deadline and not win:
        time.sleep(3)
        if relay.poll() is not None:
            print("FAIL: relay exited before showing a window; see", f"{RUN}/relay.log")
            sys.exit(1)
        for c in sh("xdotool", "search", "--onlyvisible", "--name", ".+").stdout.split():
            g = sh("xdotool", "getwindowgeometry", "--shell", c).stdout
            d = dict(line.split("=") for line in g.split() if "=" in line)
            area = int(d.get("WIDTH", 0)) * int(d.get("HEIGHT", 0))
            if area > best:
                best, win = area, c
    if not win:
        print("FAIL: no window appeared")
        sys.exit(1)
    sh("xdotool", "windowmove", win, "0", "0")
    sh("xdotool", "windowsize", win, str(W), str(H))
    sh("xdotool", "windowfocus", win)
    time.sleep(4)
    with open(STATE, "w") as f:
        json.dump({"display": disp, "win": win, "relay": relay.pid, "xvfb": xvfb.pid,
                   "sandbox": sb, "started": time.time()}, f)
    print(f"started display={disp} win={win} relay={relay.pid} xvfb={xvfb.pid}")


def words(path):
    r = sh("tesseract", path, "-", "tsv")
    out = []
    for row in csv.DictReader(io.StringIO(r.stdout), delimiter="\t"):
        try:
            if row["text"].strip() and row["text"] != "~":
                out.append((row["text"], int(row["left"]), int(row["top"]),
                            int(row["width"]), int(row["height"])))
        except (KeyError, ValueError, TypeError):
            pass
    return out


def shot(name):
    s = state()
    os.environ["DISPLAY"] = s["display"]
    p = f"{OUT}/{name}.png"
    sh("import", "-window", "root", p)
    with open(f"{RUN}/last.txt", "w") as f:
        f.write(p)
    print(p)
    return p


def main():
    cmd = sys.argv[1]
    if cmd == "start":
        return start()
    s = state()
    os.environ["DISPLAY"] = s["display"]
    win = s["win"]
    if cmd == "shot":
        shot(sys.argv[2])
    elif cmd == "ocr":
        p = f"{OUT}/{sys.argv[2]}.png" if len(sys.argv) > 2 else open(f"{RUN}/last.txt").read()
        for w, x, y, wd, h in words(p):
            print(f"{x + wd // 2:5d} {y + h // 2:5d}  {w}")
    elif cmd == "find":
        p = shot("_probe")
        ws = words(p)
        for t in sys.argv[2:]:
            hit = [(w, x, y, wd, h) for w, x, y, wd, h in ws if t.lower() in w.lower()]
            for w, x, y, wd, h in hit:
                print(f"{t}: {w} at {x + wd // 2} {y + h // 2}")
            if not hit:
                print(f"{t}: NOT FOUND")
    elif cmd == "click":
        sh("xdotool", "mousemove", sys.argv[2], sys.argv[3], "click", "1")
        time.sleep(float(sys.argv[4]) if len(sys.argv) > 4 else 1)
    elif cmd == "clicktext":
        p = shot("_probe")
        ws = words(p)
        target = sys.argv[2]
        exact = [t for t in ws if t[0].lower() == target.lower()]
        part = [t for t in ws if target.lower() in t[0].lower()]
        pick = (exact or part)
        if not pick:
            print("NOT FOUND")
            sys.exit(2)
        w, x, y, wd, h = pick[0]
        sh("xdotool", "mousemove", str(x + wd // 2), str(y + h // 2), "click", "1")
        print(f"clicked {w} at {x + wd // 2} {y + h // 2}")
        time.sleep(2)
    elif cmd == "key":
        sh("xdotool", "key", "--window", win, sys.argv[2])
        time.sleep(float(sys.argv[3]) if len(sys.argv) > 3 else 1)
    elif cmd == "keyfocus":
        sh("xdotool", "key", sys.argv[2])
        time.sleep(float(sys.argv[3]) if len(sys.argv) > 3 else 1)
    elif cmd == "type":
        sh("xdotool", "type", "--delay", "1", sys.argv[2])
        time.sleep(2)
    elif cmd == "stop":
        for pid in (s["relay"], s["xvfb"]):
            try:
                os.kill(pid, 15)
            except ProcessLookupError:
                pass
        time.sleep(3)
        print("stopped")
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
