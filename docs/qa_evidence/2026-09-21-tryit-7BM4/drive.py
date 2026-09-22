#!/usr/bin/env python3
# The Try-it preparer's mechanical pass for #7BM4: drives a real Relay (build/relay) under Xvfb on
# an isolated profile through the scenario's machine-checkable steps, locating every target by OCR
# (tesseract TSV) instead of fixed coordinates. One screenshot per step in the out dir. Input is
# xdotool only, into the app's own fields; nothing is typed into a terminal pane.
#
#   python3 drive.py <out dir>
import csv, io, json, os, shutil, subprocess, sys, tempfile, time

BIN = "/home/elliott/repos/relay-terminal/build/relay"
WORK = "/tmp/claude-1000/tryit/orders"
OUT = sys.argv[1]
W, H = 1600, 1000
CARD = json.load(open(f"{WORK}/STAGED.json"))["cards"]["done"]
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

def card_file():
    import glob
    return glob.glob(f"{WORK}/.switchboard/changes/*order-totals*.md")[0]

fails = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        fails.append(name)

try:
    # 1. Board
    key("ctrl+shift+s", 8)
    ws = words(shot("01-board"))
    check("board opens with 3 cards", bool(find(ws, "INBOX")))

    # 2. Filter to the 'done' card and open it
    pt = find(ws, "Filter")
    check("filter box found", bool(pt))
    click(pt); type_(CARD, 3)
    ws = words(shot("02-filtered"))
    row = find(ws, "above")          # 'Order totals are wrong above 100 items'
    check("card row visible after filter", bool(row))
    click(row); time.sleep(4)
    ws = words(shot("03-card"))
    strip = find_below(ws, "listed", "Check") or find_below(ws, "checked", "Check")
    if not strip:                    # click may only have selected; Enter opens
        key("Return", 4)
        ws = words(shot("03-card"))
        strip = find_below(ws, "listed", "Check") or find_below(ws, "checked", "Check")
    check("card page shows Tests strip with Check", bool(strip))
    check("thread claims all tests pass", bool(find(ws, "tests", "pass")))

    # 3. Check
    click(strip)
    body = ""
    for _ in range(10):              # the worker writes the dated block; allow ~20 s
        time.sleep(2)
        body = open(card_file()).read()
        if "### Check" in body:
            break
    ws = words(shot("04-checked"))
    check("### Check block written into the card", "### Check" in body)
    check("findings name the gone / failed / never-run", "rounding" in body and "never" in body.lower())

    # 4. The gate: try to move to Done with the status picker. The menu's 'Done' row OCRs as
    # 'bone', so drive the open menu by keyboard: End jumps to the last item (Done), Enter picks.
    st = find(ws, "verification")
    check("status picker found", bool(st))
    click(st); time.sleep(2)
    ws = words(shot("05-status-picker"))
    check("picker opens (menu lists statuses)", bool(find(ws, "Discussing")))
    keyfocus("End", 1)
    keyfocus("Return", 4)
    ws = words(shot("06-gate"))
    status = [l for l in open(card_file()).read().splitlines() if l.startswith("status:")]
    check("move to Done refused by the gate", status and "needs-verification" in status[0])
    check("gate banner offers Override", bool(find(ws, "Override") or find(ws, "prove")))

    # 5. Test suites pane — the tool row lives on the board view; go back first
    key("Escape", 3)
    ws = words(shot("06b-back"))
    tb = find_on_line(ws, "Profile", "Tests") or find_on_line(ws, "Cleanup(u)", "Tests")
    check("Tests button found", bool(tb))
    click(tb); time.sleep(12)
    ws = words(shot("07-suites"))
    check("summary counts the suite", bool(find(ws, "tests")))
    check("inventory_sync listed flaky first", bool(find(ws, "inventory_sync")))
    row = find(ws, "inventory_sync")
    if row:
        click(row); time.sleep(4)
        ws = words(shot("08-detail"))
        check("detail shows reliability / history", bool(find(ws, "reliable", "reliability", "flake")))

    # 6. Profile the build — the tool row again; the suites pane may have focus
    ws = words(shot("08b-before-profile"))
    pb = None
    for w, x, y, wd, h in ws:                     # 'Profile' is unique to the tool row
        if w.lower() == "profile":
            pb = (x + wd // 2, y + h // 2)
            break
    if not pb:
        key("Escape", 3)
        ws = words(shot("08b-before-profile"))
        for w, x, y, wd, h in ws:
            if w.lower() == "profile":
                pb = (x + wd // 2, y + h // 2)
                break
    check("Profile button found", bool(pb))
    if pb:
        click(pb); time.sleep(3)
        ws = words(shot("09-profile-menu"))
        b = find(ws, "machine)") or find_on_line(ws, "Build", "machine)") or find(ws, "Build")
        check("menu offers Build (this machine)", bool(b))
        if b:
            click(b)
            ws = []
            for _ in range(18):            # up to ~90 s
                time.sleep(5)
                ws = words(shot("10-profile"))
                if find(ws, "report.cpp", "report"):
                    break
            check("profile table names report.cpp first", bool(find(ws, "report")))
finally:
    relay.terminate(); xvfb.terminate()
    time.sleep(2)
    shutil.rmtree(sandbox, ignore_errors=True)
    log.close()

print("shots:", ", ".join(state["shots"]))
print("RESULT:", "PASS" if not fails else f"{len(fails)} failure(s): {fails}")
sys.exit(1 if fails else 0)
