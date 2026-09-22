#!/usr/bin/env python3
# The Try-it preparer's mechanical pass for #7BM4: drives a real Relay (build/relay) under Xvfb on
# an isolated profile through the scenario's machine-checkable steps, locating every target by OCR
# (tesseract TSV) instead of fixed coordinates. One screenshot per step in the out dir. Input is
# xdotool only, into the app's own fields; nothing is typed into a terminal pane.
#
#   python3 drive.py <out dir>
import csv, io, json, os, shutil, subprocess, sys, tempfile, time

BIN = "/home/elliott/repos/relay-terminal/build/relay"
WORK = "/tmp/claude-1000/sw1d-gui"
OUT = sys.argv[1]
W, H = 1600, 1000
os.environ.pop("RELAY_OPEN_SOCKET", None)

os.makedirs(OUT, exist_ok=True)


def sh(*a):
    return subprocess.run(a, capture_output=True, text=True)


disp = next(f":{n}" for n in range(170, 200) if not os.path.exists(f"/tmp/.X11-unix/X{n}"))
os.environ["DISPLAY"] = disp
sandbox = tempfile.mkdtemp(prefix="rl-try.")
os.makedirs(f"{sandbox}/home/.config/RelayTerminal")
os.makedirs(f"{sandbox}/run"); os.chmod(f"{sandbox}/run", 0o700)
os.makedirs(f"{sandbox}/tmp")
bus = f"/run/user/{os.getuid()}/bus"
if os.path.exists(bus):
    os.symlink(bus, f"{sandbox}/run/bus")
env = dict(os.environ, DISPLAY=disp, RELAY_KEYRING="off", HOME=f"{sandbox}/home",
           XDG_RUNTIME_DIR=f"{sandbox}/run", TMPDIR=f"{sandbox}/tmp",
           XDG_CONFIG_HOME=f"{sandbox}/home/.config", XDG_DATA_HOME=f"{sandbox}/home/.local/share",
           XDG_CACHE_HOME=f"{sandbox}/home/.cache")
with open(f"{sandbox}/home/.config/RelayTerminal/relay.conf", "w") as f:
    f.write("[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n")

xvfb = subprocess.Popen(["Xvfb", disp, "-screen", "0", f"{W+40}x{H+40}x24"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
log = open(f"{OUT}/relay.log", "w")
relay = subprocess.Popen([BIN, "--workspace", WORK, "--clean-shell", "--fresh"],
                         cwd=WORK, env=env, stdout=log, stderr=subprocess.STDOUT)
win, best = None, 0
deadline = time.time() + 90
while time.time() < deadline and not win:
    time.sleep(3)
    if relay.poll() is not None:
        print("FAIL: relay exited before showing a window (see relay.log)")
        sys.exit(1)
    for c in sh("xdotool", "search", "--onlyvisible", "--name", ".+").stdout.split():
        d = dict(l.split("=") for l in sh("xdotool", "getwindowgeometry", "--shell", c).stdout.split())
        area = int(d.get("WIDTH", 0)) * int(d.get("HEIGHT", 0))
        if area > best:
            best, win = area, c
if not win:
    print("FAIL: no window"); sys.exit(1)
sh("xdotool", "windowmove", win, "0", "0")
sh("xdotool", "windowsize", win, str(W), str(H))
sh("xdotool", "windowfocus", win)
time.sleep(5)

state = {"shots": []}

def shot(name):
    p = f"{OUT}/{name}.png"
    sh("import", "-window", "root", p)
    state["shots"].append(name)
    return p

def words(path):
    r = sh("tesseract", path, "-", "tsv")
    out = []
    for row in csv.DictReader(io.StringIO(r.stdout), delimiter="\t"):
        try:
            if row["text"].strip() and row["text"] != "~":
                out.append((row["text"], int(row["left"]), int(row["top"]),
                            int(row["width"]), int(row["height"])))
        except (KeyError, ValueError):
            pass
    return out

def find(ws, *texts):
    for t in texts:
        for w, x, y, wd, h in ws:
            if t.lower() in w.lower():
                return x + wd // 2, y + h // 2
    return None

def find_on_line(ws, anchor, target):
    """target word on the same text line as anchor (y within 14 px), right of it if possible.
    Exact matches win over substring ones: the button is 'Check', the strip text is 'checked'."""
    a = find(ws, anchor)
    if not a:
        return None
    same = [(w, x, y, wd, h) for w, x, y, wd, h in ws if abs(y - a[1]) < 18]
    exact = [t for t in same if t[0].lower() == target.lower()]
    for w, x, y, wd, h in sorted(exact or same, key=lambda t: t[1]):
        if target.lower() in w.lower():
            return x + wd // 2, y + h // 2
    return None

def find_below(ws, anchor, target, dy_max=70):
    """target word on the anchor's line or just below it (the Tests strip wraps: the Check
    button sits under the 'Tests 4 listed' text at card-page width). Exact matches win over
    substring ones."""
    a = find(ws, anchor)
    if not a:
        return None
    cands = [(w, x, y, wd, h) for w, x, y, wd, h in ws
             if target.lower() in w.lower() and -6 <= y - a[1] <= dy_max]
    exact = [t for t in cands if t[0].lower() == target.lower()]
    pick = exact or cands
    if not pick:
        return None
    w, x, y, wd, h = sorted(pick, key=lambda t: (t[2], t[1]))[0]
    return x + wd // 2, y + h // 2

def click(pt):
    sh("xdotool", "mousemove", str(pt[0]), str(pt[1]), "click", "1")
    time.sleep(1)

def key(k, hold=1):
    sh("xdotool", "key", "--window", win, k)
    time.sleep(hold)

def keyfocus(k, hold=1):
    """key to whichever window has focus (a Qt popup menu is its own window)."""
    sh("xdotool", "key", k)
    time.sleep(hold)

def type_(t, hold=2):
    sh("xdotool", "type", "--delay", "40", t)
    time.sleep(hold)

try:
    key("ctrl+shift+s", 4)
    ws = words(shot("01-row"))
    assert find(ws, "Hygiene") and find(ws, "Performance"), ws
    click(find(ws, "Hygiene"))
    time.sleep(3)
    ws = words(shot("02-hygiene"))
    assert find(ws, "Clean"), ws
    click(find(ws, "Performance"))
    ws = words(shot("03-performance"))
    assert find(ws, "machine") and find(ws, "Python") and find(ws, "app"), ws
    keyfocus("Down", 0.2)
    keyfocus("Return", 1)
    time.sleep(12)
    ws = words(shot("04-performance-pane"))
    assert sum("performance" in w[0].lower() for w in ws) >= 2, ws
    print("PASS: real application row, Hygiene findings/cleanup stage, four-target Performance menu and pane title")
finally:
    relay.terminate()
    xvfb.terminate()
