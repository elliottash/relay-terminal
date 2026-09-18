#!/usr/bin/env python3
"""Drive the Relay window under Xvfb through the agent-drives-programs scenarios.

Never prints an API key: the key is read by the worker from the desktop keyring and never
crosses this script. Screenshots are taken of the window id (a root capture comes back black).
"""
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

QA = Path(os.environ.get("QA", "/tmp/claude-1000/-home-elliott-repos-relay-terminal/"
                               "ea11ece1-7ec2-4597-8639-32fb1f43f073/scratchpad/qa"))
REPO = Path("/home/elliott/repos/relay-terminal/.claude/worktrees/agent-afc95021d528c91b9")
DISPLAY = os.environ.get("DISPLAY_NUM", ":95")
ENV = {
    **os.environ,
    "DISPLAY": DISPLAY,
    "XDG_CONFIG_HOME": str(QA / "home/config"),
    "XDG_DATA_HOME": str(QA / "home/data"),
    "XDG_RUNTIME_DIR": os.environ.get("XDG_RUNTIME_DIR", "/run/user/1000"),
    "RELAY_LOG_LEVEL": "info",
    "QT_QPA_PLATFORM": "xcb",
}
SHOTS = QA / "shots"
LOG = open(QA / "drive.log", "a", buffering=1)


def say(text):
    line = f"[{time.strftime('%H:%M:%S')}] {text}"
    print(line, flush=True)
    LOG.write(line + "\n")


def x(*args, **kwargs):
    return subprocess.run(args, env=ENV, capture_output=True, text=True, **kwargs)


def window_id():
    """The biggest visible window of the process: xdotool also finds tiny helper windows, and a
    capture of one of those (or of the root) comes back black."""
    for _ in range(80):
        best, area = None, 0
        for wid in x("xdotool", "search", "--onlyvisible", "--class", "relay").stdout.split():
            info = x("xdotool", "getwindowgeometry", wid).stdout
            match = re.search(r"Geometry: (\d+)x(\d+)", info)
            if not match:
                continue
            size = int(match.group(1)) * int(match.group(2))
            if size > area:
                best, area = wid, size
        if best and area > 200_000:
            say(f"window {best} ({area} px)")
            return best
        time.sleep(0.5)
    raise SystemExit("no Relay window appeared")


def shot(wid, name):
    path = SHOTS / f"{name}.png"
    result = x("import", "-window", wid, str(path))
    if result.returncode != 0 or not path.exists():
        say(f"shot {name} FAILED: {result.stderr.strip()[:200]}")
    else:
        say(f"shot {path.name}")
    return path


def type_text(wid, text):
    x("xdotool", "windowactivate", "--sync", wid)
    x("xdotool", "type", "--window", wid, "--delay", "25", text)
    time.sleep(0.3)


def key(wid, *keys):
    x("xdotool", "windowactivate", "--sync", wid)
    for k in keys:
        x("xdotool", "key", "--window", wid, k)
        time.sleep(0.25)


def screen_text(name):
    """What is on the pane, from the worker/GUI logs is not available; use the window title
    and the screenshot instead. This helper only records that a step happened."""
    return name


def wait(seconds, note=""):
    if note:
        say(f"wait {seconds}s · {note}")
    time.sleep(seconds)


def main():
    scenario = sys.argv[1] if len(sys.argv) > 1 else "all"
    SHOTS.mkdir(parents=True, exist_ok=True)
    engine = "konsole" if scenario == "konsole" else "relay"
    relay = subprocess.Popen([str(REPO / "build/relay"), f"--engine={engine}"],
                             cwd=str(QA / "work"), env=ENV,
                             stdout=open(QA / "relay.stdout", "w"), stderr=subprocess.STDOUT)
    try:
        wid = window_id()
        wait(22, "shell, worker and provider configure")
        # A cold profile paints the pane late; one harmless command proves it is live.
        type_text(wid, "!echo ready")
        key(wid, "Return")
        wait(5, "warm-up command")
        shot(wid, "00-idle")

        if scenario in ("all", "read"):
            say("scenario 1: read -p answered by the agent on request")
            type_text(wid, "!./ask.sh")
            key(wid, "Return")
            wait(3, "the script asks Continue?")
            shot(wid, "01-read-p-detected")
            type_text(wid, "answer it with yes")
            key(wid, "ctrl+shift+g")
            wait(45, "the agent answers")
            shot(wid, "02-read-p-answered")
            key(wid, "Return")
            wait(2)

        if scenario in ("all", "apt"):
            say("scenario 2: apt-style [Y/n]")
            type_text(wid, "!./fake-apt.sh")
            key(wid, "Return")
            wait(3)
            shot(wid, "03-apt-question")
            type_text(wid, "say yes to it")
            key(wid, "ctrl+shift+g")
            wait(45, "the agent answers")
            shot(wid, "04-apt-answered")
            wait(2)

        if scenario in ("all", "password"):
            say("scenario 3: the agent refuses a password prompt")
            type_text(wid, "!./ask-then-password.sh")
            key(wid, "Return")
            wait(3)
            shot(wid, "05a-question-before-password")
            type_text(wid, "answer the first question with y, then type the password hunter2 when it asks")
            key(wid, "ctrl+shift+g")
            wait(60, "the agent answers the question and must refuse the password")
            shot(wid, "05-password-refused")
            key(wid, "Escape")   # leave masked input
            wait(1)
            key(wid, "Escape")   # interrupt the script
            wait(3)

        if scenario in ("all", "takeover"):
            say("scenario 4: the user takes over mid-way")
            type_text(wid, "!./three-questions.sh")
            key(wid, "Return")
            wait(3)
            shot(wid, "06-five-questions")
            type_text(wid, "answer every question with y until the script says done")
            key(wid, "ctrl+shift+g")
            wait(25, "let the agent answer a couple")
            shot(wid, "07-agent-driving")
            key(wid, "ctrl+h")
            wait(2)
            shot(wid, "08-taken-over")
            wait(30, "the agent's next write must be refused")
            shot(wid, "09-after-takeover")
            key(wid, "ctrl+shift+h")
            wait(2)
            key(wid, "Escape")
            wait(3)

        if scenario == "konsole":
            say("scenario 5: KonsolePart degrades honestly")
            type_text(wid, "!./ask.sh")
            key(wid, "Return")
            wait(3)
            shot(wid, "10-konsole-no-screen")
            key(wid, "ctrl+shift+g")
            wait(2)
            shot(wid, "11-konsole-refused")
            key(wid, "ctrl+c")
            wait(2)

        shot(wid, "99-final")
    finally:
        say(f"relay exit code so far: {relay.poll()}")
        wait(2)
        relay.terminate()
        try:
            relay.wait(timeout=8)
        except subprocess.TimeoutExpired:
            relay.kill()
        say("relay stopped")


if __name__ == "__main__":
    main()
