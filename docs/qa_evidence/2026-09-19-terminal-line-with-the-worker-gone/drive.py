#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Can a composer line still reach the shell once the agent worker is gone? (#N8VK follow-on.)

The report (2026-09-19, an agent re-shooting the IDE-bridge evidence): with the pane's banner
reading "The agent worker exited", `!bash -c '…'` lit the `! terminal` chip and Enter sent
nothing — no command reached the shell.

Five scenes on one live pane, in order. A marker file is the proof: it exists only if the shell
ran the line. The screenshots and their OCR say what the window told the user instead.

  a. worker healthy   `!echo RELAY-ENTER-A > a.txt`            Enter
  b. worker killed    `!echo RELAY-ENTER-B > b.txt`            Enter              (the report)
  c. worker killed    `ls`                                     Enter              (auto: needs the router)
  d. worker killed    `echo RELAY-ENTER-D > d.txt`             Ctrl+Shift+Enter   (terminal mode)
  e. agent restarted  `echo RELAY-ENTER-E > e.txt`             Enter              (auto works again)

Run it against two binaries to get the before/after pair:

    Xvfb :57 -screen 0 1600x1000x24 &
    DISPLAY=:57 python3 worker-gone-drive.py <relay-binary> <RELAY_DATA_DIR> <output-dir> <prefix>

Isolation is turned off in the run's own settings: with a fake XDG_RUNTIME_DIR and no session bus
`systemd-run --user` cannot start anything, so an isolated pane's worker dies at once. That is how
the original report came about — it is worth knowing, but scene (a) needs a worker that lives, and
the scenes after it kill the worker themselves, on purpose.

Nothing of the user's real session is read: the whole run happens under a fresh temp root.
"""
import json, os, subprocess, sys, time
from pathlib import Path

RELAY = Path(sys.argv[1]).resolve()
DATA = Path(sys.argv[2]).resolve()
OUT = Path(sys.argv[3]).resolve()
TAG = sys.argv[4] if len(sys.argv) > 4 else "shot"
ROOT = Path(os.environ.get("ENTER_ROOT", f"/tmp/claude-1000/wg-{TAG}"))


def log(m): print(f"[drive] {m}", flush=True)


def isolate():
    if ROOT.exists():
        subprocess.run(["rm", "-rf", str(ROOT)], check=True)
    for name in ("home", "config/RelayTerminal", "data", "run", "tmp", "workspace"):
        (ROOT / name).mkdir(parents=True, exist_ok=True)
    os.chmod(ROOT / "run", 0o700)
    (ROOT / "config" / "RelayTerminal" / "relay.conf").write_text(
        "[instructions]\nonboarded=true\n\n[hints]\nenabled=false\n\n[isolation]\nenabled=false\n",
        encoding="utf-8")
    env = dict(os.environ)
    env.update({"HOME": str(ROOT / "home"), "XDG_CONFIG_HOME": str(ROOT / "config"),
                "XDG_DATA_HOME": str(ROOT / "data"), "XDG_RUNTIME_DIR": str(ROOT / "run"),
                "TMPDIR": str(ROOT / "tmp"), "RELAY_KEYRING": "off", "RELAY_DATA_DIR": str(DATA),
                "RELAY_LOCAL_MODELS": str(ROOT / "data" / "local-models.json")})
    for name in ("RELAY_ENGINE_CORE", "PYTHONPATH"):
        env.pop(name, None)
    return env


def xdo(*args):
    return subprocess.run(["xdotool", *args], text=True, capture_output=True, check=False)


def window():
    # There is no window manager under Xvfb, so windowactivate cannot work and typing goes
    # through XTEST to whatever holds the input focus — which is Relay's only window.
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        ids = [i for i in xdo("search", "--name", "Relay").stdout.split() if i.strip()]
        if ids:
            return ids[-1]
        time.sleep(0.5)
    raise RuntimeError("no Relay window appeared")


def type_text(text):
    xdo("type", "--delay", "40", text)


def clear_box():
    xdo("key", "ctrl+a")
    time.sleep(0.2)
    xdo("key", "BackSpace")
    time.sleep(0.4)


def shoot(name, read=True):
    path = OUT / f"{TAG}-{name}.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(["import", "-window", "root", str(path)], text=True, capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(f"screenshot failed: {r.stderr}")
    # OCR the window, not the whole root: the empty margins cost tesseract more than they say,
    # and this run takes eight pictures.
    if not read:
        log(f"wrote {path.name}")
        return ""
    crop = path.with_suffix(".crop.png")
    subprocess.run(["convert", str(path), "-crop", "1320x860+0+0", "+repage", "-colorspace", "gray",
                    str(crop)], check=False)
    text = subprocess.run(["tesseract", str(crop if crop.exists() else path), "-", "--psm", "6"],
                          text=True, capture_output=True).stdout
    crop.unlink(missing_ok=True)
    body = "\n".join(l for l in text.splitlines() if l.strip())
    log(f"wrote {path.name}\n--- {name} ---\n{body}\n--- end {name} ---")
    return text


def ancestors(pid):
    seen = set()
    while pid > 1 and pid not in seen:
        seen.add(pid)
        try:
            stat = Path(f"/proc/{pid}/stat").read_text()
        except OSError:
            break
        pid = int(stat[stat.rindex(")") + 2:].split()[1])
    return seen


def workers(relay_pid):
    found = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            argv = (entry / "cmdline").read_bytes().decode("utf-8", "replace")
            if "backend/worker.py" in argv and relay_pid in ancestors(int(entry.name)):
                found.append(int(entry.name))
        except (OSError, ValueError):
            continue
    return found


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = isolate()
    workspace = ROOT / "workspace"
    relay = subprocess.Popen([str(RELAY), "--fresh", "--clean-shell", "-w", str(workspace)],
                             env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log(f"relay pid {relay.pid} ({RELAY})")
    ran = {}
    try:
        window()
        time.sleep(10)   # the shell's first integrated prompt, and the worker's `ready`
        log(f"agent worker pids: {workers(relay.pid)}")

        # (a) the worker is up: the `!` prefix must run the line, as it always has.
        type_text("!echo RELAY-ENTER-A > a.txt")
        time.sleep(1)
        shoot("01-healthy-bang-typed", read=False)
        xdo("key", "Return")
        time.sleep(3)
        ran["a-bang-worker-up"] = (workspace / "a.txt").exists()
        shoot("02-healthy-bang-sent", read=False)

        # Take the worker away, as a crash would. The pane's banner is what the report describes.
        pids = workers(relay.pid)
        for pid in pids:
            os.kill(pid, 1)   # SIGHUP
        log(f"SIGHUP to {pids}")
        time.sleep(4)
        shoot("03-worker-gone")

        # (b) the reported line: the `! terminal` chip, then Enter.
        clear_box()
        type_text("!echo RELAY-ENTER-B > b.txt")
        time.sleep(1)
        shoot("04-gone-bang-typed", read=False)
        xdo("key", "Return")
        time.sleep(3)
        ran["b-bang-worker-gone"] = (workspace / "b.txt").exists()
        shoot("05-gone-bang-sent")

        # (c) auto: the one mode that genuinely needs the router.
        clear_box()
        type_text("ls")
        time.sleep(1)
        xdo("key", "Return")
        time.sleep(3)
        shoot("06-gone-auto")

        # (d) terminal mode by key: Ctrl+Shift+Enter.
        clear_box()
        type_text("echo RELAY-ENTER-D > d.txt")
        time.sleep(1)
        xdo("key", "ctrl+shift+Return")
        time.sleep(3)
        ran["d-terminal-key-worker-gone"] = (workspace / "d.txt").exists()
        shoot("07-gone-terminal-key", read=False)

        # (e) the banner's own action, then the same auto line again.
        clear_box()
        xdo("key", "ctrl+shift+r")
        time.sleep(8)
        log(f"agent worker pids after the restart: {workers(relay.pid)}")
        type_text("echo RELAY-ENTER-E > e.txt")
        time.sleep(1)
        xdo("key", "Return")
        time.sleep(5)
        ran["e-auto-after-restart"] = (workspace / "e.txt").exists()
        shoot("08-restarted-auto")

        log(f"RESULT {json.dumps(ran, indent=2)}")
        (OUT / f"{TAG}-result.json").write_text(json.dumps(ran, indent=2) + "\n", encoding="utf-8")
    finally:
        relay.terminate()
        try:
            out, _ = relay.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            relay.kill()
            out = ""
        if out:
            log("relay stdout tail:\n" + "\n".join(out.splitlines()[-20:]))
        log("relay stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
