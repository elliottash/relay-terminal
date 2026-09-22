#!/usr/bin/env python3
# Debug: why doesn't the Check-button click land? Keeps worker logs, screenshots around the click.
import csv, io, json, os, shutil, subprocess, sys, tempfile, time

BIN = "/home/elliott/repos/relay-terminal/build/relay"
WORK = "/tmp/claude-1000/tryit/orders"
OUT = sys.argv[1]
W, H = 1600, 1000
CARD = json.load(open(f"{WORK}/STAGED.json"))["cards"]["done"]

def sh(*a):
    return subprocess.run(a, capture_output=True, text=True)

disp = next(f":{n}" for n in range(180, 200) if not os.path.exists(f"/tmp/.X11-unix/X{n}"))
os.environ["DISPLAY"] = disp
sandbox = tempfile.mkdtemp(prefix="rl-dbg2.")
os.makedirs(f"{sandbox}/home/.config/RelayTerminal")
os.makedirs(f"{sandbox}/run"); os.chmod(f"{sandbox}/run", 0o700)
os.makedirs(f"{sandbox}/tmp")
bus = f"/run/user/{os.getuid()}/bus"
if os.path.exists(bus):
    os.symlink(bus, f"{sandbox}/run/bus")
env = dict(os.environ, RELAY_KEYRING="off", HOME=f"{sandbox}/home",
           XDG_RUNTIME_DIR=f"{sandbox}/run", TMPDIR=f"{sandbox}/tmp",
           XDG_CONFIG_HOME=f"{sandbox}/home/.config", XDG_DATA_HOME=f"{sandbox}/home/.local/share",
           XDG_CACHE_HOME=f"{sandbox}/home/.cache")
open(f"{sandbox}/home/.config/RelayTerminal/relay.conf", "w").write(
    "[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n")
xvfb = subprocess.Popen(["Xvfb", disp, "-screen", "0", f"{W+40}x{H+40}x24"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
relay = subprocess.Popen([BIN, "--workspace", WORK, "--clean-shell", "--fresh"],
                         cwd=WORK, env=env, stdout=open(f"{OUT}/dbg-relay.log", "w"),
                         stderr=subprocess.STDOUT)
win = None
deadline = time.time() + 60
while time.time() < deadline and not win:
    time.sleep(3)
    for c in sh("xdotool", "search", "--onlyvisible", "--name", ".+").stdout.split():
        d = dict(l.split("=") for l in sh("xdotool", "getwindowgeometry", "--shell", c).stdout.split())
        if int(d.get("WIDTH", 0)) > 800:
            win = c
            break
print("win:", win)
sh("xdotool", "windowmove", win, "0", "0"); sh("xdotool", "windowsize", win, str(W), str(H))
sh("xdotool", "windowfocus", win); time.sleep(5)

def shot(n):
    sh("import", "-window", "root", f"{OUT}/{n}.png"); return f"{OUT}/{n}.png"

def words(p):
    r = sh("tesseract", p, "-", "tsv")
    out = []
    for row in csv.DictReader(io.StringIO(r.stdout), delimiter="\t"):
        try:
            if row["text"].strip():
                out.append((row["text"], int(row["left"]), int(row["top"]),
                            int(row["width"]), int(row["height"])))
        except (KeyError, ValueError):
            pass
    return out

def find(ws, t):
    for w, x, y, wd, h in ws:
        if t.lower() in w.lower():
            return x + wd // 2, y + h // 2

sh("xdotool", "key", "--window", win, "ctrl+shift+s"); time.sleep(8)
ws = words(shot("d1-board"))
pt = find(ws, "Filter"); print("filter:", pt)
sh("xdotool", "mousemove", str(pt[0]), str(pt[1]), "click", "1"); time.sleep(1)
sh("xdotool", "type", "--delay", "40", CARD); time.sleep(3)
ws = words(shot("d2-filtered"))
row = find(ws, "above"); print("row:", row)
sh("xdotool", "mousemove", str(row[0]), str(row[1]), "click", "1"); time.sleep(4)
ws = words(shot("d3-card"))
chk = [(w, x, y, wd, h) for w, x, y, wd, h in ws if "check" in w.lower()]
print("check words:", chk)
listed = find(ws, "listed")
print("listed:", listed)
# the strip Check button: word 'Check' near y of 'listed'
btn = [(w, x, y, wd, h) for w, x, y, wd, h in chk if abs(y - listed[1]) < 20]
print("strip button candidate:", btn)
w, x, y, wd, h = btn[0]
cx, cy = x + wd // 2, y + h // 2
print("clicking", cx, cy)
sh("xdotool", "mousemove", str(cx), str(cy))
print(sh("xdotool", "getmouselocation", "--shell").stdout.strip())
sh("xdotool", "click", "1")
time.sleep(2)
shot("d4-just-after-click")
time.sleep(8)
shot("d5-later")
body = open([f for f in __import__("glob").glob(f"{WORK}/.switchboard/changes/*order-totals*.md")][0]).read()
print("### Check in card:", "### Check" in body)
# keep the worker logs
logs = f"{sandbox}/home/.local/share/relay/logs"
for f in os.listdir(logs):
    shutil.copy(os.path.join(logs, f), OUT)
relay.terminate(); xvfb.terminate(); time.sleep(2)
shutil.rmtree(sandbox, ignore_errors=True)
print("done")
