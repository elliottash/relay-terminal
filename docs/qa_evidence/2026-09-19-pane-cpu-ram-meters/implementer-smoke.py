#!/usr/bin/env python3
# GUI smoke for issue #CPUM: pane CPU/memory chip + tab suffix, under Xvfb.
import os, re, subprocess, sys, time
from PIL import Image

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
out = os.path.join(root, "tmp", "qa-cpu-ram")
display = os.environ.get("SMOKE_DISPLAY", ":94")
env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb",
           XDG_CONFIG_HOME=os.path.join(out, "config6"))
subprocess.run(["rm", "-rf", env["XDG_CONFIG_HOME"]])
os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)

xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1440x900x24"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1)
relay = subprocess.Popen([os.path.join(root, "build", "relay"), "--fresh", "--clean-shell",
                          "-w", os.path.join(out, "empty-ws")], cwd=root, env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(7)

def sh(*args):
    subprocess.run(args, env=dict(os.environ, DISPLAY=display), check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def shot(path):
    subprocess.run(["import", "-window", "root", path],
                   env=dict(os.environ, DISPLAY=display), check=True)

def words(png):
    subprocess.run(["convert", png, "-resize", "300%", "-colorspace", "Gray", png + ".big.png"], check=True)
    r = subprocess.run(["tesseract", png + ".big.png", "-", "--psm", "11", "tsv"],
                       capture_output=True, text=True)
    out = []
    for line in r.stdout.splitlines()[1:]:
        f = line.split("\t")
        if len(f) == 12 and f[11].strip():
            out.append((f[11], int(f[6]) // 3, int(f[7]) // 3, int(f[8]) // 3, int(f[9]) // 3))
    return out

win = subprocess.run(["xdotool", "search", "--name", "relay-terminal"],
                     env=dict(os.environ, DISPLAY=display), capture_output=True, text=True).stdout.split()
sh("xdotool", "windowfocus", win[0])
time.sleep(1)

# Dismiss the instruction-files dialog if it is up: click its Combine button.
shot(out + "/step-dialog.png")
found = words(out + "/step-dialog.png")
for i, (text, x, y, w, h) in enumerate(found):
    if text == "now" and i > 0 and found[i - 1][0] == "Not":
        sh("xdotool", "mousemove", str(x + w // 2), str(y + h // 2), "click", "1")
        time.sleep(1.5)
        break

# Hand the keyboard to the terminal (Ctrl+H: control.human), then load the pane's shell tree.
sh("xdotool", "key", "--clearmodifiers", "ctrl+h")
time.sleep(1)
sh("xdotool", "type", "--delay", "15",
   "for i in 1 2 3 4; do yes > /dev/null & done; sleep 300")
shot(out + "/step-typed.png")
sh("xdotool", "key", "--clearmodifiers", "Return")
time.sleep(6)
shot(out + "/implementer-pane-chip.png")

relay.terminate(); xvfb.terminate()
relay.wait(); xvfb.wait()
time.sleep(1)
subprocess.run(["pkill", "-f", "^yes$"], check=False)
print("done")
