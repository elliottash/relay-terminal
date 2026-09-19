#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Drive the guest event channel on a live Relay pane and photograph what it does (GT7X, 26.3).

The channel is a file the pane polls, so this harness is deliberately outside the GUI: it starts
the built `relay` under Xvfb with an isolated HOME/XDG_CONFIG_HOME/TMPDIR, waits for a pane's
runtime directory to appear, and then speaks the protocol the way a guest's shim does — event files
dropped into the pane's `guest-events/` spool, with `RELAY_RUNTIME_DIR` / `RELAY_SESSION_TOKEN` /
`RELAY_GUEST_EVENT` / `RELAY_BACKEND_DIR` set. Most of the writes go through
`relay_core.guest_hook` itself, run by absolute path exactly as the installed settings entry runs
it, so the shim is exercised and not only the file format.

A pane only grows a guest chip when its foreground program classifies as `claude` (Pane.h,
`guestProgram`). Real Claude Code is not installed here, so the harness puts a stand-in of that
name in the foreground: it types `bash -c 'exec -a claude sleep 900'` into the composer with the
`!` prefix (the composer's own "run this line in the terminal"), which makes the pane's
`/proc/<pgid>/cmdline` read `claude` — the whole input to the classifier — while leaving the
pane's shell (and its Bash bridge) alive, which is what a real `claude` in a pane looks like.

Each picture is checked, not just taken: the harness OCRs the screenshot and logs what the strip
and the terminal actually say, so the run's output is the evidence's own verification.

    xvfb-run -a python3 claude-hooks-drive.py <output-dir>

Nothing of the user's real session is read: the whole run happens under a fresh temp root.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import uuid

REPO = Path(__file__).resolve().parents[3]
RELAY = REPO / "build" / "relay"
BACKEND = REPO / "backend"
SHIM = BACKEND / "relay_core" / "guest_hook.py"
# The prompt-box strip (the row of chips under the terminal) in the 1600x1000 screen this harness
# asks xvfb-run for. The chip is small and the whole-screen OCR mangles it, so it is read alone.
STRIP_ROWS = (804, 840)


def log(message: str) -> None:
    print(f"[drive] {message}", flush=True)


# ----- the isolated session -----------------------------------------------------------------


def make_isolation() -> dict:
    root = Path(tempfile.mkdtemp(prefix="relay-qa-guest-"))
    home = root / "home"
    for name in ("home", "config", "data", "run", "tmp", "workspace"):
        (root / name).mkdir(parents=True, exist_ok=True)
    (home / ".claude").mkdir(parents=True, exist_ok=True)
    os.chmod(root / "run", 0o700)
    # A settings file with a pre-existing hook and permission, so anything photographed next to it
    # is next to a realistic file rather than an empty one.
    (home / ".claude" / "settings.json").write_text(json.dumps({
        "hooks": {"PreToolUse": [{"matcher": "Bash", "hooks": [
            {"type": "command", "command": "echo pre-existing-hook >/dev/null"}]}]},
        "permissions": {"allow": ["Bash(ls:*)"]},
    }, indent=2) + "\n", encoding="utf-8")
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
    # The helper and the shim must be this worktree's, not an installed Relay's: evidence from
    # another build would say nothing about this one.
    for name in ("RELAY_DATA_DIR", "RELAY_ENGINE_CORE", "PYTHONPATH"):
        environment.pop(name, None)
    environment["RELAY_DATA_DIR"] = str(REPO)
    return {"root": root, "env": environment, "workspace": root / "workspace"}


# ----- talking to the pane ------------------------------------------------------------------


def pane_runtime_dirs(environment: dict) -> list[Path]:
    tmp = Path(environment["TMPDIR"])
    return [entry for entry in sorted(tmp.glob("relay-??????")) if (entry / "owner").exists()]


def owner_pid(directory: Path) -> int:
    for line in (directory / "owner").read_text(encoding="utf-8").splitlines():
        if line.startswith("pid "):
            return int(line.split()[1])
    return -1


def token_for(directory: Path) -> str:
    """The pane's session token, from the `state.json` its own Bash bridge writes: the same token
    every shell envelope carries, and the one the guest channel checks."""
    deadline = time.monotonic() + 40
    while time.monotonic() < deadline:
        state = directory / "state.json"
        if state.exists():
            try:
                return json.loads(state.read_text(encoding="utf-8"))["token"]
            except (ValueError, KeyError, OSError):
                pass
        time.sleep(0.2)
    raise RuntimeError("no state.json with a token: the pane's shell never reported")


def pane_env(directory: Path, environment: dict) -> dict:
    """The environment a hook or statusline running in this pane's shell has: the pane's runtime
    dir, its token, the event spool and the backend directory (startTerminal exports all four).
    No PYTHONPATH — the installed command runs the shim by absolute path and must not need one."""
    child = dict(environment)
    child.pop("PYTHONPATH", None)
    child.update({
        "RELAY_RUNTIME_DIR": str(directory),
        "RELAY_SESSION_TOKEN": token_for(directory),
        "RELAY_GUEST_EVENT": str(directory / "guest-events"),
        "RELAY_BACKEND_DIR": str(BACKEND),
        "RELAY_PYTHON": sys.executable,
    })
    return child


def shim(directory: Path, environment: dict, *args: str, stdin: str = "",
         timeout: float = 60) -> subprocess.CompletedProcess:
    """The shim by absolute path with the installer's marker argument, which is exactly the
    command `.claude/settings.local.json` holds (26.4)."""
    return subprocess.run([sys.executable, str(SHIM), *args, "--relay-guest"],
                          input=stdin, text=True, env=pane_env(directory, environment),
                          capture_output=True, timeout=timeout)


def spool_event(directory: Path, event: str, data: dict) -> None:
    """One envelope written straight onto the spool, the way any other writer on the channel does
    it (26.3): mkstemp in `guest-events/`, then rename to `<time_ns>-<pid>-<counter>.json`."""
    events = directory / "guest-events"
    events.mkdir(mode=0o700, exist_ok=True)
    envelope = {"token": token_for(directory), "sequence": str(uuid.uuid4()), "event": event,
                "guest": "claude", "data": data}
    temporary = events / f".drive-{os.getpid()}"
    temporary.write_text(json.dumps(envelope), encoding="utf-8")
    os.replace(temporary, events / f"{time.time_ns():020d}-{os.getpid()}-{next(_counter)}.json")


_counter = iter(range(1, 1_000_000))


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


def window_size(window: str) -> tuple[int, int]:
    geometry = xdo("getwindowgeometry", "--shell", window).stdout
    size = re.search(r"WIDTH=(\d+)\nHEIGHT=(\d+)", geometry)
    return (int(size.group(1)), int(size.group(2))) if size else (1600, 1000)


def type_text(window: str, text: str) -> None:
    xdo("windowactivate", "--sync", window)
    xdo("type", "--window", window, "--delay", "40", text)


def press(window: str, *keys: str) -> None:
    xdo("windowactivate", "--sync", window)
    for name in keys:
        xdo("key", "--window", window, name)
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


def find_word(path: Path, *needles: str) -> tuple[int, int, int, int] | None:
    """A word's box on the screen, in screen pixels: tesseract's TSV output gives one per word.
    This is how the harness clicks a button whose position the pane computes — the question bar is
    as wide as the label it happens to be showing."""
    result = subprocess.run(["tesseract", str(path), "-", "tsv"], text=True, capture_output=True)
    for line in result.stdout.splitlines()[1:]:
        fields = line.split("\t")
        if len(fields) < 12:
            continue
        # The button glyphs and the chip background confuse the reader, so a box's text comes back
        # with stray punctuation ("[(Deny]"); a word is recognised when the needle is in it.
        if not any(needle in fields[11].strip().lower() for needle in needles):
            continue
        left, top, width, height = (int(fields[6]), int(fields[7]), int(fields[8]), int(fields[9]))
        if width <= 0 or height <= 0:
            continue
        return (left, top, width, height)
    return None


def strip_text(path: Path, rows: tuple[int, int]) -> str:
    """What the prompt-box strip says: the band of the screenshot that holds it, read back. The
    chip is small and the whole-screen pass mangles it, so the strip is read on its own."""
    top, bottom = rows
    result = subprocess.run(["convert", str(path), "-crop", f"1600x{bottom - top}+0+{top}",
                             "+repage", "-resize", "250%", "-colorspace", "Gray", "-normalize",
                             "-"], capture_output=True)
    return subprocess.run(["tesseract", "-", "-", "--psm", "6"], input=result.stdout,
                          capture_output=True).stdout.decode("utf-8", "replace")


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
    try:
        window = window_id()
        log(f"window {window}, size {window_size(window)}")
        time.sleep(6)   # let the Bash bridge report its first prompt

        directories = pane_runtime_dirs(environment)
        if not directories:
            log("no pane runtime directory appeared")
            return 1
        pane = directories[0]
        log(f"pane runtime {pane} (owner pid {owner_pid(pane)})")
        log(f"pane token {token_for(pane)[:8]}…")

        # The stand-in guest, as a child of the pane's own shell (so the shell and its bridge stay
        # alive, as they do around a real `claude`): `!` is the composer's "run this in the
        # terminal" prefix, and `exec -a` makes argv[0] read `claude` for the classifier.
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
        time.sleep(3)   # the 250 ms program poll that classifies it

        # 1. The statusline, through the module the installer configures. This is the path that
        #    feeds the chip: relay_core.guest_hook parses the statusline JSON, forwards the fields
        #    Relay can show as a `statusline` channel event, and prints its passthrough line.
        statusline = {"model": {"display_name": "Claude Sonnet 4.5"},
                      "workspace": {"current_dir": str(isolation["workspace"])},
                      "session_id": "qa-guest-01", "context_window": {"used_percentage": 42}}
        result = shim(pane, environment, "statusline", stdin=json.dumps(statusline))
        log(f"the statusline shim printed {result.stdout.strip()!r}")
        time.sleep(3)
        chip = output / "claude-hooks-01-statusline-chip.png"
        screenshot(chip)
        report("01-statusline-chip", strip_text(chip, STRIP_ROWS), ["Claude Sonnet", "42%"])

        # 2. A PermissionRequest hook — the one claude sends only when it is really about to ask
        #    (26.4) — the shim forwards it and holds on the answer, the pane asks the user, and the
        #    click is what answers. Claude hands a hook its JSON on stdin and closes it, so the
        #    harness does the same with a file rather than a pipe it has to remember to close: the
        #    shim reads to EOF before it forwards anything.
        hook = {"hook_event_name": "PermissionRequest", "tool_name": "Bash",
                "tool_input": {"command": "rm -rf build/ && cmake -S . -B build"}}
        hook_file = isolation["root"] / "hook-permissionrequest.json"
        hook_file.write_text(json.dumps(hook), encoding="utf-8")
        with hook_file.open("r", encoding="utf-8") as hook_input:
            held = subprocess.Popen([sys.executable, str(SHIM), "PermissionRequest", "--relay-guest"],
                                    stdin=hook_input, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True,
                                    env=dict(pane_env(pane, environment),
                                             RELAY_GUEST_PERMISSION_TIMEOUT="90"))
            time.sleep(2.5)
            question = output / "claude-hooks-02-permission-question.png"
            report("02-permission-question", screenshot(question),
                   ["wants to run Bash", "rm -rf build"])

            # The question bar floats over the terminal's top-left and is as wide as the label it
            # is showing, so the harness asks the picture where its buttons are and clicks there —
            # the same click a user makes, through answerGuestPermission(). Allow and Deny sit at
            # the bar's right end in that order with 8 px between them (buildUi's questionRow), so
            # Deny's box gives Allow's.
            deny = find_word(question, "deny")
            answers = pane / "guest-answers"
            answered = False
            if deny is None:
                log("no Deny button found on the question bar")
            else:
                left, top, width, height = deny
                at = (left - 8 - 20, top + height // 2)
                log(f"clicking Allow at {at} (left of Deny's box {deny})")
                xdo("mousemove", str(at[0]), str(at[1]))
                xdo("click", "1")
                time.sleep(1.0)
                # The shim deletes the answer as it reads it, so "was there one" is racy on
                # purpose: what the held hook prints below is the real proof.
                answered = True
            time.sleep(1.0)
            answered_pic = output / "claude-hooks-03-answered.png"
            screenshot(answered_pic)
            report("03-answered", ocr(answered_pic), [], forbid=["wants to run"])
            if not answered:
                log("no click landed on the button")
            log(f"guest-answers/ now holds {sorted(p.name for p in answers.glob('*.json'))}")
            try:
                held.wait(timeout=30)
            except subprocess.TimeoutExpired:
                held.kill()
            out = held.stdout.read() if held.stdout else ""
            log(f"the held hook returned {out.strip()!r}")

        # 3. Two questions at once, answered from the keyboard. The spool keeps both — the slot
        #    it replaced lost the first — and the bar says how many are waiting; Y answers the one
        #    on screen and the next takes its place, N denies that one (26.4).
        holds = []
        for index, command in enumerate(("git push --force", "rm -rf /tmp/relay-qa-two")):
            queued = isolation["root"] / f"hook-queued-{index}.json"
            queued.write_text(json.dumps({"hook_event_name": "PermissionRequest", "tool_name": "Bash",
                                          "tool_input": {"command": command}}), encoding="utf-8")
            with queued.open("r", encoding="utf-8") as handle:
                holds.append(subprocess.Popen([sys.executable, str(SHIM), "PermissionRequest"],
                                              stdin=handle, stdout=subprocess.PIPE,
                                              stderr=subprocess.PIPE, text=True,
                                              env=dict(pane_env(pane, environment),
                                                       RELAY_GUEST_PERMISSION_TIMEOUT="90")))
            time.sleep(1.5)
        time.sleep(2)
        queued_pic = output / "claude-hooks-06-two-questions-queued.png"
        report("06-two-questions-queued", screenshot(queued_pic), ["1 of 2", "git push"])
        press(window, "y")          # the bar has the keyboard: Y allows the one on screen
        time.sleep(2)
        second_pic = output / "claude-hooks-07-second-question.png"
        report("07-second-question", screenshot(second_pic), ["rm -rf /tmp/relay-qa-two"],
               forbid=["git push"])
        press(window, "n")          # ...and N denies the next
        time.sleep(2)
        report("08-queue-empty", screenshot(output / "claude-hooks-08-queue-empty.png"), [],
               forbid=["wants to run"])
        for index, held_two in enumerate(holds):
            try:
                held_two.wait(timeout=30)
            except subprocess.TimeoutExpired:
                held_two.kill()
            log(f"queued hook {index} returned {(held_two.stdout.read() if held_two.stdout else '').strip()!r}")

        # 4. A share past the warn line, written straight onto the spool: the same envelope and the
        #    same poll, without the shim. `state` moves the third field the channel carries.
        spool_event(pane, "statusline", {"model": "Claude Opus 4.6", "context_pct": 94})
        time.sleep(1.5)
        spool_event(pane, "state", {"busy": True})
        time.sleep(2)
        warn = output / "claude-hooks-04-context-warn.png"
        screenshot(warn)
        report("04-context-warn", strip_text(warn, STRIP_ROWS), ["Claude Opus", "94%"])

        # 5. The guest leaves: the chip goes with it, because the pane's program poll sees the
        #    foreground program is no longer a guest (setGuest("")).
        os.kill(guest_pid, 15)
        time.sleep(4)
        report("05-guest-gone", screenshot(output / "claude-hooks-05-guest-gone.png"),
               ["workspace"], forbid=["Claude Opus", "94%"])
    finally:
        process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
        log("relay stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
