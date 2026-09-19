#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Photograph the composer `/` popup's badged guest rows with a guest actually active (GT7X, 26.8).

The composer track's own evidence covered the catalog and the routing with deterministic tests and
smoke-checked the popup with no guest; this harness fills what was missing: the popup *with a guest
classified in the pane*, showing the guest's commands carrying their `Claude Code · guest` badge,
and a chosen guest command typed into the guest.

Same shape as claude-hooks-drive.py: the built `relay` runs under Xvfb with an isolated
HOME/XDG_CONFIG_HOME/TMPDIR, and a stand-in named `claude` (`bash -c 'exec -a claude sleep 900'`
through the composer's `!` prefix) gives the pane's classifier the argv[0] it reads. The stand-in
leaves the pty in canonical echo mode, so text the composer types into the "guest" echoes on screen
and the second picture can be checked for it. Each picture is OCR'd and asserted, not just taken.

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 composer-drive.py <output-dir>

Nothing of the user's real session is read: the whole run happens under a fresh temp root.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[3]
RELAY = REPO / "build" / "relay"


def log(message: str) -> None:
    print(f"[drive] {message}", flush=True)


# ----- the isolated session -----------------------------------------------------------------


def make_isolation() -> dict:
    root = Path(tempfile.mkdtemp(prefix="relay-qa-composer-"))
    home = root / "home"
    for name in ("home", "config", "data", "run", "tmp", "workspace"):
        (root / name).mkdir(parents=True, exist_ok=True)
    os.chmod(root / "run", 0o700)
    # Relay's instruction-file chooser is a modal dialog on first launch; mark it done so the
    # pictures are of the pane and not of a dialog. QSettings' INI for this app.
    (root / "config" / "RelayTerminal").mkdir(parents=True, exist_ok=True)
    (root / "config" / "RelayTerminal" / "relay.conf").write_text(
        "[instructions]\nonboarded=true\n", encoding="utf-8")
    environment = dict(os.environ)
    environment.update({
        "HOME": str(home),
        "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_RUNTIME_DIR": str(root / "run"),
        "TMPDIR": str(root / "tmp"),
        "RELAY_KEYRING": "off",
        "RELAY_LOCAL_MODELS": str(root / "data" / "local-models.json"),
    })
    # The slash scan must be this worktree's backend: evidence from another build would say
    # nothing about this one.
    for name in ("RELAY_DATA_DIR", "RELAY_ENGINE_CORE", "PYTHONPATH"):
        environment.pop(name, None)
    environment["RELAY_DATA_DIR"] = str(REPO)
    return {"root": root, "env": environment, "workspace": root / "workspace"}


# ----- driving the window -------------------------------------------------------------------


def xdo(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["xdotool", *args], text=True, capture_output=True, check=False)


def window_id() -> str:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        ids = [line for line in xdo("search", "--name", "Relay").stdout.split() if line.strip()]
        if ids:
            return ids[-1]
        time.sleep(0.5)
    raise RuntimeError("no Relay window appeared")


def type_text(window: str, text: str) -> None:
    xdo("windowactivate", "--sync", window)
    xdo("type", "--window", window, "--delay", "40", text)


def press(window: str, *keys: str) -> None:
    xdo("windowactivate", "--sync", window)
    for name in keys:
        xdo("key", "--window", window, name)
        time.sleep(0.3)


def clear_composer(window: str) -> None:
    """Empty the composer. Never Escape: Relay binds Esc on an empty box, while a program runs,
    to interrupting that program — it would kill the stand-in guest (this bug cost a run).
    Ctrl+A + BackSpace empties the box, and an open `/` popup hides itself once the text no
    longer starts with '/'."""
    press(window, "ctrl+a")
    press(window, "BackSpace")
    time.sleep(0.3)


def screenshot(path: Path) -> str:
    """Take the picture and read it back: the whole screen OCR'd, returned as text so the caller
    can assert what the picture is evidence of."""
    path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["import", "-window", "root", str(path)], text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"screenshot failed: {result.stderr}")
    log(f"wrote {path.name}")
    return ocr(path)


def ocr(path: Path) -> str:
    result = subprocess.run(["tesseract", str(path), "-"], text=True, capture_output=True)
    return result.stdout


def region_text(path: Path, top: int, bottom: int) -> str:
    """A horizontal band of the screen, enlarged and normalized first: the popup's text is small,
    and the whole-screen pass mangles it. The popup floats above the composer, bottom-left."""
    result = subprocess.run(["convert", str(path), "-crop", f"1600x{bottom - top}+0+{top}",
                             "+repage", "-resize", "250%", "-colorspace", "Gray", "-normalize",
                             "-"], capture_output=True)
    return subprocess.run(["tesseract", "-", "-", "--psm", "6"], input=result.stdout,
                          capture_output=True).stdout.decode("utf-8", "replace")


def report(name: str, text: str, expect: list[str], forbid: list[str] = ()) -> bool:
    """Say what the picture holds, and whether it is what it was taken for."""
    found = [needle for needle in expect if needle in text]
    missing = [needle for needle in expect if needle not in text]
    log(f"{name}: found {found or 'nothing expected'}; missing {missing or 'nothing'}")
    for needle in forbid:
        if needle in text:
            log(f"{name}: UNEXPECTED {needle!r} is on screen")
            missing.append(needle)
    return not missing


def pane_runtime_dirs(environment: dict) -> list[Path]:
    tmp = Path(environment["TMPDIR"])
    return [entry for entry in sorted(tmp.glob("relay-??????")) if (entry / "owner").exists()]


def claude_under(relay_pid: int) -> int:
    """The pid of a process called `claude` in this Relay's process tree, or 0. `exec -a` sets
    argv[0], which is what the pane's classifier reads out of /proc/<pid>/cmdline."""
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            argv = (entry / "cmdline").read_bytes().split(b"\0")
            if argv and argv[0].endswith(b"claude") and not argv[0].endswith(b"relay_core"):
                pid = int(entry.name)
                if relay_pid in ancestors(pid):
                    return pid
        except (OSError, ValueError):
            continue
    return 0


def ancestors(pid: int) -> set[int]:
    seen = set()
    while pid > 1 and pid not in seen:
        seen.add(pid)
        try:
            stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        except OSError:
            break
        pid = int(stat[stat.rindex(")") + 2:].split()[1])
    return seen


# ----- the run ------------------------------------------------------------------------------


def main() -> int:
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    output.mkdir(parents=True, exist_ok=True)
    if not RELAY.exists():
        log(f"missing {RELAY}: build the worktree first")
        return 1
    isolation = make_isolation()
    environment = isolation["env"]
    log(f"isolated session at {isolation['root']}")

    process = subprocess.Popen([str(RELAY), "--fresh", "-w", str(isolation["workspace"])],
                               env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True)
    log(f"relay pid {process.pid}")
    ok = True
    try:
        window = window_id()
        time.sleep(6)   # let the pane's shell report its first prompt

        # The stand-in guest, as a child of the pane's own shell (so the shell and its bridge stay
        # alive, as they do around a real `claude`).
        type_text(window, "!bash -c 'exec -a claude sleep 900'")
        press(window, "Return")
        guest_pid = 0
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and not guest_pid:
            guest_pid = claude_under(process.pid)
            time.sleep(0.5)
        if not guest_pid:
            log("the stand-in claude never reached the foreground")
            return 1
        log(f"a process called `claude` is in the foreground (pid {guest_pid})")

        # The pane's own classification must publish the slash catalog: guest.json appears within
        # a few seconds of the stand-in reaching the foreground (250 ms poll + python scan + 80 ms
        # channel poll). No manual emit — evidence must come from Relay's own chain.
        pane = None
        for _ in range(20):
            directories = pane_runtime_dirs(environment)
            if directories:
                pane = directories[0]
                break
            time.sleep(0.5)
        if pane is None:
            log("no pane runtime directory appeared")
            return 1
        log(f"pane runtime {pane}")
        for i in range(15):
            guest_json = pane / "guest.json"
            if guest_json.exists():
                log(f"guest.json at +{i}s: {guest_json.read_text(encoding='utf-8')[:240]}")
                break
            time.sleep(1)
        else:
            log("no guest.json within 15 s of the stand-in — classification or the pane's own "
                "scan never published")
            return 1

        # 1. The `/` popup with the guest active: the guest's own commands open the list, badged
        #    `Claude Code · guest`. The catalog can take a few seconds to land after
        #    classification; retry while the badged rows don't.
        popup = output / "implementer-composer-01-slash-popup-badges.png"
        text = ""
        for attempt in range(4):
            time.sleep(4)
            clear_composer(window)
            type_text(window, "/")
            time.sleep(1.5)
            text = screenshot(popup)
            band = region_text(popup, 380, 810)
            if "guest" in band.lower() or "guest" in text.lower():
                text = band + "\n" + text
                break
            log(f"no badged rows yet (attempt {attempt + 1}); closing the popup and waiting")
        ok = report("01-slash-popup-badges", text, ["guest", "doctor", "config"]) and ok

        # 2. Filter to a command only the guest has: the badged `/doctor` row is what the popup
        #    offers. (The composer still holds `/`, so typing appends the filter.)
        type_text(window, "doctor")
        time.sleep(1.0)
        filtered = output / "implementer-composer-02-guest-command-filtered.png"
        text = screenshot(filtered)
        ok = report("02-guest-command-filtered", text + "\n" + region_text(filtered, 380, 810),
                    ["doctor", "guest"]) and ok

        # 3. Accept the row: the composer types the guest command into the pane (bracketed paste,
        #    then Enter), and the canonical-mode pty echoes it on screen.
        press(window, "Return")
        time.sleep(2.5)
        typed = output / "implementer-composer-03-guest-command-typed.png"
        text = screenshot(typed)
        ok = report("03-guest-command-typed", text, ["doctor"]) and ok
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
    log("done" if ok else "done (with missing assertions — see above)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
